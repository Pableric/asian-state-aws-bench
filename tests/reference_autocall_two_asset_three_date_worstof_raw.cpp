#include "tests/autocall_two_asset_three_date_worstof_cases.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" const unsigned char asian_arithmetic_joe_kuo_256_records[];

namespace {

constexpr std::uint64_t SHIFT_SEED = UINT64_C(0x54574f4153534554);
constexpr unsigned PANEL_REPS = 32;
constexpr unsigned REFERENCE_REPS = 16;
constexpr unsigned PANEL_LOG2 = 12;

struct Decomposition {
    double price{};
    std::array<double, 3> first_call{};
    std::array<double, 3> coupon_probability{};
    std::array<double, 3> coupon_contribution{};
    std::array<double, 3> call_contribution{};
    double survival{};
    double protected_contribution{};
    double unprotected_contribution{};
};

struct PairResult {
    Decomposition half;
    Decomposition full;
};

struct Summary {
    double mean{};
    double stderr{};
    double half95{};
};

static std::uint64_t splitmix64(std::uint64_t &state) {
    std::uint64_t z = (state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static std::array<std::uint32_t, 6> shifts(unsigned replication) {
    std::uint64_t state = SHIFT_SEED ^
        (UINT64_C(0xd1b54a32d192ed03) * (replication + 1u));
    std::array<std::uint32_t, 6> out{};
    for (auto &word : out)
        word = static_cast<std::uint32_t>(splitmix64(state));
    return out;
}

static double inverse_normal_acklam(double p) {
    static constexpr double a[] = {
        -39.69683028665376, 220.9460984245205, -275.9285104469687,
        138.3577518672690, -30.66479806614716, 2.506628277459239};
    static constexpr double c[] = {
        -0.007784894002430293, -0.3223964580411365,
        -2.400758277161838, -2.549732539343734,
        4.374664141464968, 2.938163982698783};
    static constexpr double d[] = {
        0.007784695709041462, 0.3224671290700398,
        2.445134137142996, 3.754408661907416};
    static constexpr double b[] = {
        -54.47609879822406, 161.5858368580409, -155.6989798598866,
        66.80131188771972, -13.28068155288572};
    if (p < 0.02425) {
        const double q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    if (p > 0.97575) {
        const double q = std::sqrt(-2.0 * std::log(1.0-p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    const double q = p - 0.5;
    const double r = q * q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
           (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
}

static void direction_row(unsigned dimension, std::uint32_t out[32]) {
    const unsigned char *record = asian_arithmetic_joe_kuo_256_records +
        static_cast<std::size_t>(dimension) * 132u;
    std::uint32_t count;
    std::memcpy(&count, record, sizeof(count));
    if (count != 32u)
        std::abort();
    std::memcpy(out, record + 4u, 32u * sizeof(std::uint32_t));
}

static std::uint32_t sobol_word(std::uint32_t index,
                                const std::uint32_t directions[32]) {
    std::uint32_t gray = index ^ (index >> 1);
    std::uint32_t word = 0u;
    for (unsigned bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u)
            word ^= directions[bit];
    return word;
}

using Point = std::array<double, 6>;

static std::vector<Point> gaussian_points(
    unsigned log2_count, unsigned replication, bool shifted,
    std::uint32_t first_index) {
    std::uint32_t directions[6][32];
    for (unsigned d = 0; d < 6; ++d)
        direction_row(d, directions[d]);
    const auto shift = shifts(replication);
    const std::size_t count = std::size_t{1} << log2_count;
    std::vector<Point> points(count);
    for (std::size_t p = 0; p < count; ++p) {
        for (unsigned d = 0; d < 6; ++d) {
            std::uint32_t word = sobol_word(
                first_index + static_cast<std::uint32_t>(p), directions[d]);
            if (shifted)
                word ^= shift[d];
            const double u = (static_cast<double>(word) + 0.5) /
                             4294967296.0;
            points[p][d] = inverse_normal_acklam(u);
        }
    }
    return points;
}

static Decomposition evaluate_prefix(
    const autocall_worstof_case_t &fixture,
    const std::vector<Point> &points, std::size_t count,
    bool reverse_assets) {
    const auto &m = fixture.market;
    const auto &c = fixture.contract;
    const double dt = m.maturity / 3.0;
    const double drift_a = (m.rate - m.dividend_a -
        0.5*m.sigma_a*m.sigma_a) * dt;
    const double drift_b = (m.rate - m.dividend_b -
        0.5*m.sigma_b*m.sigma_b) * dt;
    const double diffusion_a = m.sigma_a * std::sqrt(dt);
    const double diffusion_b = m.sigma_b * std::sqrt(dt);
    const double cholesky = std::sqrt((1.0-m.rho)*(1.0+m.rho));
    Decomposition out;
    for (std::size_t p = 0; p < count; ++p) {
        double la = 1.0;
        double lb = 1.0;
        double pv = 0.0;
        bool alive = true;
        for (unsigned date = 0; date < 3; ++date) {
            const double u = points[p][2u*date + (reverse_assets ? 1u : 0u)];
            const double v = points[p][2u*date + (reverse_assets ? 0u : 1u)];
            const double za = u;
            const double zb = m.rho*u + cholesky*v;
            la *= std::exp(drift_a + diffusion_a*za);
            lb *= std::exp(drift_b + diffusion_b*zb);
            const double worst = std::min(la, lb);
            const double coupon_discount =
                std::exp(-m.rate*c.coupon_payment_time[date]);
            const double call_discount =
                std::exp(-m.rate*c.call_payment_time[date]);
            if (alive && worst >= c.coupon_barrier[date]) {
                const double contribution =
                    c.coupon_cashflow[date] * coupon_discount;
                pv += contribution;
                out.coupon_probability[date] += 1.0;
                out.coupon_contribution[date] += contribution;
            }
            if (alive && worst >= c.call_barrier[date]) {
                const double contribution =
                    c.call_redemption[date] * call_discount;
                pv += contribution;
                out.first_call[date] += 1.0;
                out.call_contribution[date] += contribution;
                alive = false;
            }
        }
        if (alive) {
            out.survival += 1.0;
            const double worst = std::min(la, lb);
            const double discount =
                std::exp(-m.rate*c.terminal_payment_time);
            if (worst >= c.protection_barrier) {
                const double contribution = c.notional * discount;
                pv += contribution;
                out.protected_contribution += contribution;
            } else {
                const double contribution = c.notional * worst * discount;
                pv += contribution;
                out.unprotected_contribution += contribution;
            }
        }
        out.price += pv;
    }
    const double inv = 1.0/static_cast<double>(count);
    out.price *= inv;
    out.survival *= inv;
    out.protected_contribution *= inv;
    out.unprotected_contribution *= inv;
    for (unsigned d = 0; d < 3; ++d) {
        out.first_call[d] *= inv;
        out.coupon_probability[d] *= inv;
        out.coupon_contribution[d] *= inv;
        out.call_contribution[d] *= inv;
    }
    return out;
}

static double independent_state_machine(
    const autocall_worstof_case_t &fixture,
    const std::vector<Point> &points, std::size_t count) {
    const auto &m = fixture.market;
    const auto &c = fixture.contract;
    const double dt = m.maturity/3.0;
    const double da = (m.rate-m.dividend_a-0.5*m.sigma_a*m.sigma_a)*dt;
    const double db = (m.rate-m.dividend_b-0.5*m.sigma_b*m.sigma_b)*dt;
    const double va = m.sigma_a*std::sqrt(dt);
    const double vb = m.sigma_b*std::sqrt(dt);
    const double root = std::sqrt((1.0-m.rho)*(1.0+m.rho));
    double total = 0.0;
    for (std::size_t p = 0; p < count; ++p) {
        double a = 1.0;
        double b = 1.0;
        double pv = 0.0;
        bool alive = true;
#define DIRECT_DATE(D, U, V) do { \
    a *= std::exp(da + va*points[p][U]); \
    b *= std::exp(db + vb*(m.rho*points[p][U] + root*points[p][V])); \
    const double w = std::min(a, b); \
    if (alive && w >= c.coupon_barrier[D]) \
        pv += c.coupon_cashflow[D]*std::exp(-m.rate*c.coupon_payment_time[D]); \
    if (alive && w >= c.call_barrier[D]) { \
        pv += c.call_redemption[D]*std::exp(-m.rate*c.call_payment_time[D]); \
        alive = false; \
    } \
} while (0)
        DIRECT_DATE(0, 0, 1);
        DIRECT_DATE(1, 2, 3);
        DIRECT_DATE(2, 4, 5);
#undef DIRECT_DATE
        if (alive) {
            const double worst = std::min(a, b);
            pv += c.notional*std::exp(-m.rate*c.terminal_payment_time)*
                  (worst >= c.protection_barrier ? 1.0 : worst);
        }
        total += pv;
    }
    return total/static_cast<double>(count);
}

static Summary summary(const std::vector<double> &values) {
    Summary out;
    for (double x : values)
        out.mean += x;
    out.mean /= static_cast<double>(values.size());
    double ss = 0.0;
    for (double x : values)
        ss += (x-out.mean)*(x-out.mean);
    if (values.size() > 1)
        out.stderr = std::sqrt(ss /
            (static_cast<double>(values.size()-1)*values.size()));
    out.half95 = 2.131449545559323 * out.stderr; /* t(15), two-sided 95%. */
    return out;
}

static std::vector<std::array<PairResult, AUTOCALL_WORSTOF_CASES>>
reference_level(unsigned log2_count) {
    std::vector<std::array<PairResult, AUTOCALL_WORSTOF_CASES>>
        result(REFERENCE_REPS);
    std::atomic<unsigned> next{0};
    const autocall_worstof_case_t *cases = autocall_worstof_cases();
    auto worker = [&]() {
        for (;;) {
            const unsigned rep = next.fetch_add(1);
            if (rep >= REFERENCE_REPS)
                break;
            const auto points = gaussian_points(log2_count, rep, true, 1u);
            const std::size_t full = std::size_t{1} << log2_count;
            for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
                result[rep][c].half =
                    evaluate_prefix(cases[c], points, full/2u, false);
                result[rep][c].full =
                    evaluate_prefix(cases[c], points, full, false);
            }
        }
    };
    std::thread first(worker);
    std::thread second(worker);
    first.join();
    second.join();
    return result;
}

static bool reference_resolved(
    const std::vector<std::array<PairResult, AUTOCALL_WORSTOF_CASES>> &level,
    unsigned log2_count) {
    const autocall_worstof_case_t *cases = autocall_worstof_cases();
    bool resolved = true;
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
        std::vector<double> half;
        std::vector<double> full;
        for (const auto &rep : level) {
            half.push_back(rep[c].half.price);
            full.push_back(rep[c].full.price);
        }
        const Summary h = summary(half);
        const Summary f = summary(full);
        const double combined = 1.96*std::hypot(h.stderr, f.stderr);
        const bool adjacent = std::fabs(h.mean-f.mean) <= combined;
        const double required_half = cases[c].stress ? 0.0015 : 0.0005;
        const bool uncertainty = f.half95 <= required_half;
        resolved &= adjacent && uncertainty;
        std::printf("REFERENCE_LEVEL name=%s log2_samples=%u replications=%u "
                    "half_mean=%.17g mean=%.17g stderr=%.12g half95=%.12g "
                    "adjacent=%s uncertainty=%s\n",
                    cases[c].name, log2_count, REFERENCE_REPS,
                    h.mean, f.mean, f.stderr, f.half95,
                    adjacent ? "PASS" : "FAIL",
                    uncertainty ? "PASS" : "FAIL");
    }
    return resolved;
}

static double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        std::floor(p*static_cast<double>(values.size()-1)));
    return values[index];
}

} // namespace

int main() {
    std::printf("SHIFT_MANIFEST seed=0x%016llx replications=%u dimensions=6 ",
                static_cast<unsigned long long>(SHIFT_SEED), PANEL_REPS);
    for (unsigned rep = 0; rep < PANEL_REPS; ++rep) {
        const auto s = shifts(rep);
        std::uint64_t folded = 0;
        for (unsigned d = 0; d < 6; ++d)
            folded ^= static_cast<std::uint64_t>(s[d]) << ((d & 1u)*32u);
        std::printf("r%u=%016llx%s", rep,
                    static_cast<unsigned long long>(folded),
                    rep + 1u == PANEL_REPS ? "\n" : ",");
    }

    unsigned level_log2 = 18;
    auto high = reference_level(level_log2);
    bool resolved = reference_resolved(high, level_log2);
    while (!resolved && level_log2 < 22) {
        level_log2 += 2;
        high = reference_level(level_log2);
        resolved = reference_resolved(high, level_log2);
    }

    const autocall_worstof_case_t *cases = autocall_worstof_cases();
    bool accuracy_pass = resolved;
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
        std::vector<double> high_prices;
        for (const auto &rep : high)
            high_prices.push_back(rep[c].full.price);
        const Summary reference = summary(high_prices);
        Decomposition decomposition;
        for (const auto &rep : high) {
            const Decomposition &d = rep[c].full;
            decomposition.price += d.price;
            decomposition.survival += d.survival;
            decomposition.protected_contribution += d.protected_contribution;
            decomposition.unprotected_contribution += d.unprotected_contribution;
            for (unsigned i = 0; i < 3; ++i) {
                decomposition.first_call[i] += d.first_call[i];
                decomposition.coupon_probability[i] += d.coupon_probability[i];
                decomposition.coupon_contribution[i] += d.coupon_contribution[i];
                decomposition.call_contribution[i] += d.call_contribution[i];
            }
        }
        const double inv_rep = 1.0/REFERENCE_REPS;
        decomposition.survival *= inv_rep;
        decomposition.protected_contribution *= inv_rep;
        decomposition.unprotected_contribution *= inv_rep;
        for (unsigned i = 0; i < 3; ++i) {
            decomposition.first_call[i] *= inv_rep;
            decomposition.coupon_probability[i] *= inv_rep;
            decomposition.coupon_contribution[i] *= inv_rep;
            decomposition.call_contribution[i] *= inv_rep;
        }
        const double mass = std::fabs(
            decomposition.first_call[0] + decomposition.first_call[1] +
            decomposition.first_call[2] + decomposition.survival - 1.0);
        if (mass > 1e-10)
            accuracy_pass = false;

        const auto canonical_points = gaussian_points(12, 0, false, 8192u);
        const double canonical =
            evaluate_prefix(cases[c], canonical_points, 4096u, false).price;
        const double independent =
            independent_state_machine(cases[c], canonical_points, 4096u);
        const double independent_diff = std::fabs(canonical-independent);
        if (independent_diff > 1e-12)
            accuracy_pass = false;
        std::vector<double> errors;
        std::vector<double> abs_errors;
        std::array<std::vector<double>, 3> call_errors;
        double order_delta = 0.0;
        for (unsigned rep = 0; rep < PANEL_REPS; ++rep) {
            const auto points = gaussian_points(PANEL_LOG2, rep, true, 8192u);
            const Decomposition value =
                evaluate_prefix(cases[c], points, 4096u, false);
            const Decomposition reversed =
                evaluate_prefix(cases[c], points, 4096u, true);
            const double error = value.price-reference.mean;
            errors.push_back(error);
            abs_errors.push_back(std::fabs(error));
            order_delta += std::fabs(value.price-reversed.price);
            for (unsigned d = 0; d < 3; ++d)
                call_errors[d].push_back(value.first_call[d]-
                    decomposition.first_call[d]);
        }
        double mean_error = 0.0;
        double mean_abs = 0.0;
        double mse = 0.0;
        for (unsigned rep = 0; rep < PANEL_REPS; ++rep) {
            mean_error += errors[rep];
            mean_abs += abs_errors[rep];
            mse += errors[rep]*errors[rep];
        }
        mean_error /= PANEL_REPS;
        mean_abs /= PANEL_REPS;
        const double rmse = std::sqrt(mse/PANEL_REPS);
        double centered = 0.0;
        for (double x : errors)
            centered += (x-mean_error)*(x-mean_error);
        const double stderr = std::sqrt(centered /
            (static_cast<double>(PANEL_REPS-1)*PANEL_REPS));
        const double half95 = 2.0395134463964077*stderr;
        const double cap = cases[c].stress ?
            cases[c].contract.notional*3e-4 :
            cases[c].contract.notional*1e-4;
        const double canonical_error = std::fabs(canonical-reference.mean);
        const bool panel_pass = mean_abs <= cap && rmse <= cap &&
            (cases[c].stress || half95 <=
                cases[c].contract.notional*0.5e-4) &&
            canonical_error <= cap && canonical_error <= 3.0*rmse;
        accuracy_pass &= panel_pass;
        std::array<double, 3> call_rmse{};
        for (unsigned d = 0; d < 3; ++d) {
            for (double e : call_errors[d])
                call_rmse[d] += e*e;
            call_rmse[d] = std::sqrt(call_rmse[d]/PANEL_REPS);
        }
        std::printf(
            "REFERENCE_CASE name=%s panel=%s reference=%.17g ref_low=%.17g "
            "ref_high=%.17g canonical=%.17g independent=%.17g "
            "independent_diff=%.12g canonical_error=%.12g "
            "mean_error=%+.12g mean_abs_error=%.12g signed_bias=%+.12g "
            "rmse=%.12g half95=%.12g median_abs=%.12g p90_abs=%.12g "
            "worst_abs=%.12g call1=%.12g call2=%.12g call3=%.12g "
            "survival=%.12g coupon1=%.12g coupon2=%.12g coupon3=%.12g "
            "coupon_pv1=%.12g coupon_pv2=%.12g coupon_pv3=%.12g "
            "call_pv1=%.12g call_pv2=%.12g call_pv3=%.12g "
            "protected_terminal=%.12g unprotected_terminal=%.12g "
            "mass_error=%.12g call_rmse1=%.12g call_rmse2=%.12g "
            "call_rmse3=%.12g coordinate_order_mean_abs=%.12g decision=%s\n",
            cases[c].name, cases[c].stress ? "STRESS" : "CORE",
            reference.mean, reference.mean-reference.half95,
            reference.mean+reference.half95, canonical, independent,
            independent_diff, canonical_error,
            mean_error, mean_abs, mean_error, rmse, half95,
            percentile(abs_errors, 0.5), percentile(abs_errors, 0.9),
            *std::max_element(abs_errors.begin(), abs_errors.end()),
            decomposition.first_call[0], decomposition.first_call[1],
            decomposition.first_call[2], decomposition.survival,
            decomposition.coupon_probability[0],
            decomposition.coupon_probability[1],
            decomposition.coupon_probability[2],
            decomposition.coupon_contribution[0],
            decomposition.coupon_contribution[1],
            decomposition.coupon_contribution[2],
            decomposition.call_contribution[0],
            decomposition.call_contribution[1],
            decomposition.call_contribution[2],
            decomposition.protected_contribution,
            decomposition.unprotected_contribution, mass,
            call_rmse[0], call_rmse[1], call_rmse[2],
            order_delta/PANEL_REPS, panel_pass ? "PASS" : "FAIL");
    }
    std::printf("HIGH_REFERENCE native_portable=YES sde=NO timed_lifecycle=NO "
                "max_concurrent_replications=2 final_log2_samples=%u "
                "replications=%u resolved=%s\n",
                level_log2, REFERENCE_REPS, resolved ? "YES" : "NO");
    std::puts(accuracy_pass ? "TWO_ASSET_4096_ACCURACY_QUALIFIED" :
                             "TWO_ASSET_4096_ACCURACY_NOT_QUALIFIED");
    return 0;
}
