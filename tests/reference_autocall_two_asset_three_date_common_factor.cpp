#define main autocall_worstof_parent_reference_main_not_called
#include "tests/reference_autocall_two_asset_three_date_worstof_raw.cpp"
#undef main

#include <functional>
#include <limits>
#include <numeric>

namespace common_factor {

constexpr std::uint32_t FIRST_INDEX = 8192u;
constexpr unsigned REPLICATIONS = 32u;
constexpr unsigned ORDERS = 4u;
constexpr unsigned COUNTS = 7u;
constexpr unsigned PATH_COUNTS[COUNTS] = {
    64u, 128u, 256u, 512u, 1024u, 2048u, 4096u};
constexpr std::uint64_t QUALIFICATION_SEED =
    UINT64_C(0x54574f4153534554);
constexpr std::uint64_t HOLDOUT_SEED = UINT64_C(0x3257484f4c444f55);
constexpr std::uint64_t REFERENCE_SEED = UINT64_C(0x434f4d4d4f4e5246);
constexpr unsigned REFERENCE_REPLICATIONS = 16u;
constexpr const char *PARENT_REFERENCE_STDOUT_SHA256 =
    "79be9a385ada95d912cec150e83470ab731e9ab194a7a4790f9ca05993caf550";
static constexpr double PARENT_6D_MEAN[AUTOCALL_WORSTOF_CASES] = {
    100.70878948736562, 100.95474657654479, 100.65652325715764,
    100.69617792910326, 98.15350695756068, 98.153973309483305,
    70.563416743476409, 100.28792948998768, 90.326390831132926,
    97.538814562326465, 94.176342477288955, 93.148102001459137,
    94.17667339727555, 93.148605472212893, 93.169888694705591,
    92.860584252170909, 93.170486208281403, 92.861030030121512,
    92.703046724863171, 92.703046724863171, 92.703512736608374,
    92.703512736608374, 93.733001588166672, 93.127848345073843,
    93.733469346565073, 93.128312846999648, 93.723042409750221,
    93.150614388894198, 93.723528771551273, 93.151115777976571,
    93.709923583218497, 93.16401376007903, 93.710408962650277,
    93.164443986883199, 84.958850781129343, 83.938427339527721,
    84.958920733668066, 83.938521044389859,
};
static constexpr double PARENT_6D_STDERR[AUTOCALL_WORSTOF_CASES] = {
    0.000195194085942, 0.000131448134547, 0.000137495333957,
    0.000236521200847, 0.000147236428826, 0.000366707994188,
    0.000226244404376, 0.000176348841553, 0.000266252366459,
    0.00017101043843, 7.3884384957e-05, 9.93604337783e-05,
    0.000279570754062, 0.000285235665122, 0.000135069283898,
    0.000110034913337, 0.000278370546217, 0.000307686863091,
    0.000109030894947, 0.000109030894947, 0.00030365189977,
    0.00030365189977, 0.000108969692846, 0.000109638588963,
    0.00030327988894, 0.000303815869098, 0.000110912535552,
    0.000105922770669, 0.000307157261586, 0.000299111658815,
    0.000104333474763, 0.000113162488293, 0.000300488386459,
    0.000301376338795, 0.000114792114513, 2.92603555077e-05,
    0.000157474021497, 4.11250367735e-05,
};

/* Coordinates map to E1,C2,E2,C3,E3. */
constexpr unsigned ORDER_MAP[ORDERS][5] = {
    {0u, 1u, 2u, 3u, 4u},
    {1u, 0u, 2u, 3u, 4u},
    {0u, 2u, 1u, 4u, 3u},
    {1u, 3u, 0u, 2u, 4u},
};
constexpr const char *ORDER_NAME[ORDERS] = {"R0", "R1", "R2", "R3"};

struct Residual {
    double e1{};
    double c2{};
    double e2{};
    double c3{};
    double e3{};
};

struct ConditionalResult {
    double price{};
    std::array<double, 3> first_call{};
    std::array<double, 3> coupon_probability{};
    std::array<double, 3> coupon_contribution{};
    std::array<double, 3> call_contribution{};
    double survival{};
    double protected_probability{};
    double protected_contribution{};
    double downside_probability{};
    double unprotected_contribution{};
    double mass_error{};
};

struct Prepared {
    double drift_a{};
    double drift_b{};
    double alpha_a{};
    double alpha_b{};
    double common{};
    double spread{};
    double beta_a{};
    double beta_b{};
    double inv_beta_a{};
    double inv_beta_b{};
    std::array<double, 3> log_call{};
    std::array<double, 3> log_coupon{};
    double log_protection{};
    std::array<double, 3> coupon{};
    std::array<double, 3> redemption{};
    double terminal{};
};

struct FrozenReference {
    Summary price{};
    Decomposition decomposition{};
    bool adjacent{};
    bool uncertainty{};
};

struct Metrics {
    double mean{};
    double bias{};
    double mean_abs{};
    double rmse{};
    double half95{};
    double median_abs{};
    double p90_abs{};
    double worst_abs{};
    double variance{};
    std::array<double, 3> call_rmse{};
};

using Replications = std::array<double, REPLICATIONS>;
using CallReplications =
    std::array<std::array<double, 3>, REPLICATIONS>;
struct PanelCell {
    Replications price{};
    CallReplications first_call{};
};
using Panel = std::array<std::array<std::array<PanelCell,
    AUTOCALL_WORSTOF_CASES>, COUNTS>, ORDERS>;

static double normal_cdf(double x) {
    return 0.5 * std::erfc(-x *
        0.70710678118654752440084436210484903928);
}

static double normal_pdf(double x) {
    return 0.39894228040143267793994605993438186848 *
           std::exp(-0.5*x*x);
}

static Prepared prepare(const autocall_worstof_case_t &fixture) {
    const auto &m = fixture.market;
    const auto &x = fixture.contract;
    const double dt = m.maturity / 3.0;
    Prepared p;
    p.drift_a = (m.rate-m.dividend_a-0.5*m.sigma_a*m.sigma_a)*dt;
    p.drift_b = (m.rate-m.dividend_b-0.5*m.sigma_b*m.sigma_b)*dt;
    p.alpha_a = m.sigma_a*std::sqrt(dt);
    p.alpha_b = m.sigma_b*std::sqrt(dt);
    p.common = std::sqrt((1.0+m.rho)*0.5);
    p.spread = std::sqrt((1.0-m.rho)*0.5);
    p.beta_a = p.alpha_a*p.common;
    p.beta_b = p.alpha_b*p.common;
    p.inv_beta_a = 1.0/p.beta_a;
    p.inv_beta_b = 1.0/p.beta_b;
    p.log_protection = std::log(x.protection_barrier);
    p.terminal = x.notional*std::exp(-m.rate*x.terminal_payment_time);
    for (unsigned i = 0; i < 3; ++i) {
        p.log_call[i] = std::log(x.call_barrier[i]);
        p.log_coupon[i] = std::log(x.coupon_barrier[i]);
        p.coupon[i] = x.coupon_cashflow[i]*
            std::exp(-m.rate*x.coupon_payment_time[i]);
        p.redemption[i] = x.call_redemption[i]*
            std::exp(-m.rate*x.call_payment_time[i]);
    }
    return p;
}

static void advance_logs(const Prepared &p, const Residual &z,
                         std::array<double, 3> &ra,
                         std::array<double, 3> &rb) {
    ra[0] = p.drift_a + p.alpha_a*p.spread*z.e1;
    rb[0] = p.drift_b - p.alpha_b*p.spread*z.e1;
    ra[1] = ra[0] + p.drift_a +
        p.alpha_a*(p.common*z.c2+p.spread*z.e2);
    rb[1] = rb[0] + p.drift_b +
        p.alpha_b*(p.common*z.c2-p.spread*z.e2);
    ra[2] = ra[1] + p.drift_a +
        p.alpha_a*(p.common*z.c3+p.spread*z.e3);
    rb[2] = rb[1] + p.drift_b +
        p.alpha_b*(p.common*z.c3-p.spread*z.e3);
}

static double threshold(double log_barrier, double ra, double rb,
                        const Prepared &p) {
    return std::max((log_barrier-ra)*p.inv_beta_a,
                    (log_barrier-rb)*p.inv_beta_b);
}

static double moment(double beta, double upper) {
    return std::exp(0.5*beta*beta)*normal_cdf(upper-beta);
}

static ConditionalResult conditional_value_prepared(
    const Prepared &p, const Residual &z) {
    std::array<double, 3> ra{};
    std::array<double, 3> rb{};
    advance_logs(p, z, ra, rb);
    ConditionalResult out;
    double prior_threshold = std::numeric_limits<double>::infinity();
    double prior_cdf = 1.0;
    for (unsigned date = 0; date < 3; ++date) {
        const double h = threshold(p.log_call[date], ra[date], rb[date], p);
        const double q = threshold(p.log_coupon[date], ra[date], rb[date], p);
        const double hc = normal_cdf(h);
        const double qc = normal_cdf(q);
        out.coupon_probability[date] = std::max(prior_cdf-qc, 0.0);
        out.first_call[date] = std::max(prior_cdf-hc, 0.0);
        out.coupon_contribution[date] =
            p.coupon[date]*out.coupon_probability[date];
        out.call_contribution[date] =
            p.redemption[date]*out.first_call[date];
        out.price += out.coupon_contribution[date] +
                     out.call_contribution[date];
        prior_threshold = std::min(prior_threshold, h);
        prior_cdf = std::min(prior_cdf, hc);
    }
    out.survival = prior_cdf;
    const double protection = threshold(
        p.log_protection, ra[2], rb[2], p);
    const double protection_cdf = normal_cdf(protection);
    out.protected_probability = std::max(prior_cdf-protection_cdf, 0.0);
    out.downside_probability = normal_cdf(
        std::min(prior_threshold, protection));
    out.protected_contribution = p.terminal*out.protected_probability;
    const double upper = std::min(prior_threshold, protection);
    const double delta = p.beta_a-p.beta_b;
    double downside = 0.0;
    if (delta == 0.0) {
        const double log_scale = std::min(ra[2], rb[2]);
        downside = std::exp(log_scale)*moment(p.beta_a, upper);
    } else {
        const double cross = (rb[2]-ra[2])/delta;
        const double cut = std::min(cross, upper);
        const double ma_cut = std::exp(ra[2]+0.5*p.beta_a*p.beta_a)*
            normal_cdf(cut-p.beta_a);
        const double mb_cut = std::exp(rb[2]+0.5*p.beta_b*p.beta_b)*
            normal_cdf(cut-p.beta_b);
        const double ma_upper = std::exp(ra[2]+0.5*p.beta_a*p.beta_a)*
            normal_cdf(upper-p.beta_a);
        const double mb_upper = std::exp(rb[2]+0.5*p.beta_b*p.beta_b)*
            normal_cdf(upper-p.beta_b);
        if (delta > 0.0)
            downside = ma_cut + (mb_upper-mb_cut);
        else
            downside = mb_cut + (ma_upper-ma_cut);
    }
    out.unprotected_contribution = p.terminal*downside;
    out.price += out.protected_contribution + out.unprotected_contribution;
    out.mass_error = std::fabs(out.first_call[0]+out.first_call[1]+
        out.first_call[2]+out.survival-1.0);
    return out;
}

static ConditionalResult conditional_value(
    const autocall_worstof_case_t &fixture, const Residual &z) {
    return conditional_value_prepared(prepare(fixture), z);
}

static double hard_payoff(const autocall_worstof_case_t &fixture,
                          const Residual &z, double y) {
    const Prepared p = prepare(fixture);
    std::array<double, 3> ra{};
    std::array<double, 3> rb{};
    advance_logs(p, z, ra, rb);
    double pv = 0.0;
    bool alive = true;
    for (unsigned date = 0; date < 3; ++date) {
        const double a = std::exp(ra[date]+p.beta_a*y);
        const double b = std::exp(rb[date]+p.beta_b*y);
        const double worst = std::min(a, b);
        if (alive && worst >= fixture.contract.coupon_barrier[date])
            pv += p.coupon[date];
        if (alive && worst >= fixture.contract.call_barrier[date]) {
            pv += p.redemption[date];
            alive = false;
        }
    }
    if (alive) {
        const double a = std::exp(ra[2]+p.beta_a*y);
        const double b = std::exp(rb[2]+p.beta_b*y);
        const double worst = std::min(a, b);
        pv += p.terminal *
            (worst >= fixture.contract.protection_barrier ? 1.0 : worst);
    }
    return pv;
}

struct GaussLegendreRule {
    std::vector<double> x;
    std::vector<double> w;
};

static GaussLegendreRule gauss_legendre_rule(unsigned count) {
    GaussLegendreRule rule;
    rule.x.resize(count);
    rule.w.resize(count);
    const unsigned half = (count+1u)/2u;
    for (unsigned i = 0; i < half; ++i) {
        double z = std::cos(3.141592653589793238462643383279502884 *
                            (static_cast<double>(i)+0.75) /
                            (static_cast<double>(count)+0.5));
        double derivative = 0.0;
        for (unsigned iteration = 0; iteration < 32u; ++iteration) {
            double p0 = 1.0;
            double p1 = z;
            for (unsigned n = 2; n <= count; ++n) {
                const double pn = ((2.0*n-1.0)*z*p1-(n-1.0)*p0)/n;
                p0 = p1;
                p1 = pn;
            }
            derivative = count*(z*p1-p0)/(z*z-1.0);
            const double next = z-p1/derivative;
            if (std::fabs(next-z) <= 2.0e-16) {
                z = next;
                break;
            }
            z = next;
        }
        rule.x[i] = -z;
        rule.x[count-1u-i] = z;
        const double weight = 2.0/((1.0-z*z)*derivative*derivative);
        rule.w[i] = weight;
        rule.w[count-1u-i] = weight;
    }
    return rule;
}

static double gauss_legendre_integral(
    const std::function<double(double)> &f, double a, double b,
    const GaussLegendreRule &rule) {
    const double midpoint = 0.5*(a+b);
    const double half_width = 0.5*(b-a);
    double total = 0.0;
    for (unsigned i = 0; i < rule.x.size(); ++i)
        total += rule.w[i]*f(midpoint+half_width*rule.x[i]);
    return half_width*total;
}

static double direct_integral(const autocall_worstof_case_t &fixture,
                              const Residual &z) {
    const Prepared p = prepare(fixture);
    std::array<double, 3> ra{};
    std::array<double, 3> rb{};
    advance_logs(p, z, ra, rb);
    std::vector<double> cuts{-12.0, 12.0};
    for (unsigned date = 0; date < 3; ++date) {
        cuts.push_back(threshold(p.log_call[date], ra[date], rb[date], p));
        cuts.push_back(threshold(p.log_coupon[date], ra[date], rb[date], p));
    }
    cuts.push_back(threshold(p.log_protection, ra[2], rb[2], p));
    if (p.beta_a != p.beta_b)
        cuts.push_back((rb[2]-ra[2])/(p.beta_a-p.beta_b));
    cuts.erase(std::remove_if(cuts.begin(), cuts.end(), [](double x) {
        return !(x > -12.0 && x < 12.0);
    }), cuts.end());
    cuts.push_back(-12.0);
    cuts.push_back(12.0);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    const auto integrand = [&](double y) {
        return hard_payoff(fixture, z, y)*normal_pdf(y);
    };
    static const GaussLegendreRule rule64 = gauss_legendre_rule(64u);
    static const GaussLegendreRule rule128 = gauss_legendre_rule(128u);
    double total64 = 0.0;
    double total128 = 0.0;
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        const double a = cuts[i-1];
        const double b = cuts[i];
        if (!(b > a))
            continue;
        total64 += gauss_legendre_integral(integrand, a, b, rule64);
        total128 += gauss_legendre_integral(integrand, a, b, rule128);
    }
    if (std::fabs(total128-total64) > 1.0e-10)
        return std::numeric_limits<double>::quiet_NaN();
    return total128;
}

static std::array<std::uint32_t, 5> panel_shifts(
    std::uint64_t seed, unsigned replication, bool high_bits) {
    std::uint64_t state = seed ^
        (UINT64_C(0xd1b54a32d192ed03)*(replication+1u));
    std::array<std::uint32_t, 5> out{};
    for (auto &word : out) {
        const std::uint64_t value = splitmix64(state);
        word = high_bits ? static_cast<std::uint32_t>(value >> 32) :
                           static_cast<std::uint32_t>(value);
    }
    return out;
}

static void print_manifest(const char *name, std::uint64_t seed,
                           bool high_bits) {
    std::printf("%s seed=0x%016llx stepping=parent_splitmix64 "
                "extraction=%s dimensions=5 replications=%u\n", name,
                static_cast<unsigned long long>(seed),
                high_bits ? "high32" : "low32", REPLICATIONS);
    for (unsigned rep = 0; rep < REPLICATIONS; ++rep) {
        const auto words = panel_shifts(seed, rep, high_bits);
        std::printf("%s_REP r=%u", name, rep);
        for (unsigned d = 0; d < 5; ++d)
            std::printf(" d%u=0x%08x", d+1u, words[d]);
        std::putchar('\n');
    }
}

static Residual residual_point(
    std::uint32_t index, unsigned order,
    const std::array<std::array<std::uint32_t, 32>, 5> &directions,
    const std::array<std::uint32_t, 5> &shift) {
    std::array<double, 5> coordinate{};
    std::array<double, 5> variable{};
    for (unsigned d = 0; d < 5; ++d) {
        const std::uint32_t word =
            sobol_word(index, directions[d].data()) ^ shift[d];
        const double u = (static_cast<double>(word)+0.5)/4294967296.0;
        coordinate[d] = inverse_normal_acklam(u);
        variable[ORDER_MAP[order][d]] = coordinate[d];
    }
    return {variable[0], variable[1], variable[2], variable[3], variable[4]};
}

static std::array<FrozenReference, AUTOCALL_WORSTOF_CASES>
build_reference(
    const std::array<std::array<std::uint32_t, 32>, 5> &directions) {
    const auto *cases = autocall_worstof_cases();
    std::array<Prepared, AUTOCALL_WORSTOF_CASES> prepared{};
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c)
        prepared[c] = prepare(cases[c]);

    std::array<Summary, AUTOCALL_WORSTOF_CASES> raw_summary{};
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
        raw_summary[c].mean = PARENT_6D_MEAN[c];
        raw_summary[c].stderr = PARENT_6D_STDERR[c];
        raw_summary[c].half95 =
            2.131449545559323*PARENT_6D_STDERR[c];
    }
    std::printf("PARENT_6D_REFERENCE frozen_log2_samples=22 "
                "replications=16 stdout_sha256=%s\n",
                PARENT_REFERENCE_STDOUT_SHA256);

    struct ConditionalPair {
        ConditionalResult half;
        ConditionalResult full;
    };
    auto accumulate = [](ConditionalResult &to, const ConditionalResult &v) {
        to.price += v.price;
        to.survival += v.survival;
        to.protected_contribution += v.protected_contribution;
        to.unprotected_contribution += v.unprotected_contribution;
        for (unsigned d = 0; d < 3; ++d) {
            to.first_call[d] += v.first_call[d];
            to.coupon_probability[d] += v.coupon_probability[d];
            to.coupon_contribution[d] += v.coupon_contribution[d];
            to.call_contribution[d] += v.call_contribution[d];
        }
    };
    auto scale = [](ConditionalResult &v, double factor) {
        v.price *= factor;
        v.survival *= factor;
        v.protected_contribution *= factor;
        v.unprotected_contribution *= factor;
        for (unsigned d = 0; d < 3; ++d) {
            v.first_call[d] *= factor;
            v.coupon_probability[d] *= factor;
            v.coupon_contribution[d] *= factor;
            v.call_contribution[d] *= factor;
        }
    };

    unsigned log2_count = 16u;
    std::vector<std::array<ConditionalPair, AUTOCALL_WORSTOF_CASES>> level;
    bool resolved = false;
    while (!resolved && log2_count <= 20u) {
        const unsigned paths = 1u << log2_count;
        level.assign(REFERENCE_REPLICATIONS, {});
        std::atomic<unsigned> next{0u};
        auto worker = [&]() {
            for (;;) {
                const unsigned rep = next.fetch_add(1u);
                if (rep >= REFERENCE_REPLICATIONS)
                    break;
                const auto shift = panel_shifts(
                    REFERENCE_SEED, rep, true);
                for (unsigned path = 0; path < paths; ++path) {
                    const Residual z = residual_point(
                        1u+path, 0u, directions, shift);
                    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
                        const ConditionalResult value =
                            conditional_value_prepared(prepared[c], z);
                        if (path < paths/2u)
                            accumulate(level[rep][c].half, value);
                        accumulate(level[rep][c].full, value);
                    }
                }
                for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
                    scale(level[rep][c].half, 2.0/paths);
                    scale(level[rep][c].full, 1.0/paths);
                }
            }
        };
        std::thread first(worker);
        std::thread second(worker);
        first.join();
        second.join();
        resolved = true;
        for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
            std::vector<double> half;
            std::vector<double> full;
            for (const auto &rep : level) {
                half.push_back(rep[c].half.price);
                full.push_back(rep[c].full.price);
            }
            const Summary hs = summary(half);
            const Summary fs = summary(full);
            const bool adjacent = std::fabs(hs.mean-fs.mean) <=
                1.96*std::hypot(hs.stderr, fs.stderr);
            const bool uncertainty = fs.half95 <=
                (cases[c].stress ? 0.0015 : 0.0005);
            resolved &= adjacent && uncertainty;
            std::printf("CONDITIONAL_REFERENCE_LEVEL name=%s "
                        "log2_samples=%u replications=%u half_mean=%.17g "
                        "mean=%.17g stderr=%.12g half95=%.12g "
                        "adjacent=%s uncertainty=%s\n", cases[c].name,
                        log2_count, REFERENCE_REPLICATIONS, hs.mean, fs.mean,
                        fs.stderr, fs.half95, adjacent ? "PASS" : "FAIL",
                        uncertainty ? "PASS" : "FAIL");
        }
        if (!resolved)
            log2_count += 2u;
    }

    std::array<FrozenReference, AUTOCALL_WORSTOF_CASES> out{};
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
        std::vector<double> half;
        std::vector<double> full;
        for (const auto &rep : level) {
            half.push_back(rep[c].half.price);
            full.push_back(rep[c].full.price);
            const auto &d = rep[c].full;
            out[c].decomposition.survival += d.survival;
            out[c].decomposition.protected_contribution +=
                d.protected_contribution;
            out[c].decomposition.unprotected_contribution +=
                d.unprotected_contribution;
            for (unsigned date = 0; date < 3; ++date) {
                out[c].decomposition.first_call[date] += d.first_call[date];
                out[c].decomposition.coupon_probability[date] +=
                    d.coupon_probability[date];
                out[c].decomposition.coupon_contribution[date] +=
                    d.coupon_contribution[date];
                out[c].decomposition.call_contribution[date] +=
                    d.call_contribution[date];
            }
        }
        out[c].price = summary(full);
        const Summary hs = summary(half);
        out[c].adjacent = std::fabs(hs.mean-out[c].price.mean) <=
            1.96*std::hypot(hs.stderr, out[c].price.stderr);
        out[c].uncertainty = out[c].price.half95 <=
            (cases[c].stress ? 0.0015 : 0.0005);
        const double inv = 1.0/static_cast<double>(level.size());
        out[c].decomposition.survival *= inv;
        out[c].decomposition.protected_contribution *= inv;
        out[c].decomposition.unprotected_contribution *= inv;
        for (unsigned date = 0; date < 3; ++date) {
            out[c].decomposition.first_call[date] *= inv;
            out[c].decomposition.coupon_probability[date] *= inv;
            out[c].decomposition.coupon_contribution[date] *= inv;
            out[c].decomposition.call_contribution[date] *= inv;
        }
        const double raw_difference =
            std::fabs(out[c].price.mean-raw_summary[c].mean);
        const bool raw_crosscheck = raw_difference <=
            1.96*std::hypot(out[c].price.stderr, raw_summary[c].stderr);
        std::printf("COMMON_FACTOR_REFERENCE name=%s panel=%s mean=%.17g "
                    "stderr=%.12g half95=%.12g adjacent=%s uncertainty=%s "
                    "call1=%.12g call2=%.12g call3=%.12g survival=%.12g "
                    "coupon1=%.12g coupon2=%.12g coupon3=%.12g "
                    "redemption1=%.12g redemption2=%.12g redemption3=%.12g "
                    "protected=%.12g unprotected=%.12g "
                    "parent_6d_mean=%.17g parent_6d_half95=%.12g "
                    "parent_crosscheck=%s\n",
                    cases[c].name, cases[c].stress ? "STRESS" : "CORE",
                    out[c].price.mean, out[c].price.stderr,
                    out[c].price.half95,
                    out[c].adjacent ? "PASS" : "FAIL",
                    out[c].uncertainty ? "PASS" : "FAIL",
                    out[c].decomposition.first_call[0],
                    out[c].decomposition.first_call[1],
                    out[c].decomposition.first_call[2],
                    out[c].decomposition.survival,
                    out[c].decomposition.coupon_contribution[0],
                    out[c].decomposition.coupon_contribution[1],
                    out[c].decomposition.coupon_contribution[2],
                    out[c].decomposition.call_contribution[0],
                    out[c].decomposition.call_contribution[1],
                    out[c].decomposition.call_contribution[2],
                    out[c].decomposition.protected_contribution,
                    out[c].decomposition.unprotected_contribution,
                    raw_summary[c].mean, raw_summary[c].half95,
                    raw_crosscheck ? "PASS" : "UNRESOLVED");
    }
    return out;
}

static Metrics metrics(const PanelCell &cell, const FrozenReference &reference) {
    Metrics out;
    std::vector<double> absolute;
    for (unsigned rep = 0; rep < REPLICATIONS; ++rep) {
        const double e = cell.price[rep]-reference.price.mean;
        out.mean += cell.price[rep];
        out.bias += e;
        out.mean_abs += std::fabs(e);
        out.rmse += e*e;
        absolute.push_back(std::fabs(e));
        for (unsigned date = 0; date < 3; ++date) {
            const double ce = cell.first_call[rep][date]-
                reference.decomposition.first_call[date];
            out.call_rmse[date] += ce*ce;
        }
    }
    out.mean /= REPLICATIONS;
    out.bias /= REPLICATIONS;
    out.mean_abs /= REPLICATIONS;
    out.rmse = std::sqrt(out.rmse/REPLICATIONS);
    double centered = 0.0;
    for (double value : cell.price)
        centered += (value-out.mean)*(value-out.mean);
    out.variance = centered/static_cast<double>(REPLICATIONS-1u);
    out.half95 = 2.0395134463964077*
        std::sqrt(out.variance/REPLICATIONS);
    std::sort(absolute.begin(), absolute.end());
    out.median_abs = absolute[15];
    out.p90_abs = absolute[27];
    out.worst_abs = absolute.back();
    for (double &v : out.call_rmse)
        v = std::sqrt(v/REPLICATIONS);
    return out;
}

static Panel build_panel(
    std::uint64_t seed, bool high_bits,
    const std::array<std::array<std::uint32_t, 32>, 5> &directions) {
    Panel panel{};
    const auto *cases = autocall_worstof_cases();
    std::array<Prepared, AUTOCALL_WORSTOF_CASES> prepared{};
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c)
        prepared[c] = prepare(cases[c]);
    for (unsigned rep = 0; rep < REPLICATIONS; ++rep) {
        const auto shift = panel_shifts(seed, rep, high_bits);
        for (unsigned order = 0; order < ORDERS; ++order) {
            std::array<std::array<double, AUTOCALL_WORSTOF_CASES>,
                       COUNTS> sums{};
            std::array<std::array<std::array<double, 3>,
                       AUTOCALL_WORSTOF_CASES>, COUNTS> calls{};
            unsigned count_index = 0u;
            for (unsigned path = 0; path < PATH_COUNTS[COUNTS-1]; ++path) {
                const Residual z = residual_point(
                    FIRST_INDEX+path, order, directions, shift);
                for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
                    const ConditionalResult value =
                        conditional_value_prepared(prepared[c], z);
                    sums[count_index][c] += value.price;
                    for (unsigned date = 0; date < 3; ++date)
                        calls[count_index][c][date] += value.first_call[date];
                }
                if (path+1u == PATH_COUNTS[count_index]) {
                    const double inv = 1.0/PATH_COUNTS[count_index];
                    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
                        panel[order][count_index][c].price[rep] =
                            sums[count_index][c]*inv;
                        for (unsigned date = 0; date < 3; ++date)
                            panel[order][count_index][c]
                                .first_call[rep][date] =
                                calls[count_index][c][date]*inv;
                    }
                    ++count_index;
                    if (count_index == COUNTS)
                        break;
                    sums[count_index] = sums[count_index-1u];
                    calls[count_index] = calls[count_index-1u];
                }
            }
        }
    }
    return panel;
}

static std::array<std::array<ConditionalResult, AUTOCALL_WORSTOF_CASES>,
                  COUNTS> fixed_prefixes(
    unsigned order,
    const std::array<std::array<std::uint32_t, 32>, 5> &directions) {
    std::array<std::array<ConditionalResult, AUTOCALL_WORSTOF_CASES>,
               COUNTS> out{};
    std::array<ConditionalResult, AUTOCALL_WORSTOF_CASES> sums{};
    const auto *cases = autocall_worstof_cases();
    std::array<Prepared, AUTOCALL_WORSTOF_CASES> prepared{};
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c)
        prepared[c] = prepare(cases[c]);
    const std::array<std::uint32_t, 5> no_shift{};
    unsigned count_index = 0u;
    for (unsigned path = 0; path < PATH_COUNTS[COUNTS-1]; ++path) {
        const Residual z = residual_point(
            FIRST_INDEX+path, order, directions, no_shift);
        for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
            const ConditionalResult v = conditional_value_prepared(
                prepared[c], z);
            sums[c].price += v.price;
            sums[c].mass_error = std::max(sums[c].mass_error, v.mass_error);
            for (unsigned d = 0; d < 3; ++d)
                sums[c].first_call[d] += v.first_call[d];
        }
        if (path+1u == PATH_COUNTS[count_index]) {
            const double inv = 1.0/PATH_COUNTS[count_index];
            for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
                out[count_index][c] = sums[c];
                out[count_index][c].price *= inv;
                for (unsigned d = 0; d < 3; ++d)
                    out[count_index][c].first_call[d] *= inv;
            }
            ++count_index;
            if (count_index == COUNTS)
                break;
        }
    }
    return out;
}

static bool stage0_math() {
    const auto *cases = autocall_worstof_cases();
    const std::array<Residual, 13> corpus{{
        {}, {1,0,0,0,0}, {-1,0,0,0,0}, {0,1,0,0,0},
        {0,-1,0,0,0}, {0,0,1,0,0}, {0,0,-1,0,0},
        {0,0,0,1,0}, {0,0,0,-1,0}, {0,0,0,0,1},
        {0,0,0,0,-1}, {4,-4,4,-4,4}, {-4,4,-4,4,-4}
    }};
    bool pass = true;
    double worst = 0.0;
    double worst_mass = 0.0;
    double worst_label = 0.0;
    const char *worst_name = "NONE";
    unsigned worst_residual = 0u;
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
        autocall_worstof_case_t swapped = cases[c];
        std::swap(swapped.market.dividend_a, swapped.market.dividend_b);
        std::swap(swapped.market.sigma_a, swapped.market.sigma_b);
        std::swap(swapped.contract.spot_a, swapped.contract.spot_b);
        for (unsigned ri = 0; ri < corpus.size(); ++ri) {
            const Residual &z = corpus[ri];
            const ConditionalResult closed = conditional_value(cases[c], z);
            const double integrated = direct_integral(cases[c], z);
            const Residual reversed{
                -z.e1, z.c2, -z.e2, z.c3, -z.e3};
            const ConditionalResult label_value =
                conditional_value(swapped, reversed);
            const double difference = std::fabs(closed.price-integrated);
            const double label_difference =
                std::fabs(closed.price-label_value.price);
            if (difference/cases[c].contract.notional > worst) {
                worst = difference/cases[c].contract.notional;
                worst_name = cases[c].name;
                worst_residual = ri;
                std::printf("STAGE0_WORST_UPDATE name=%s residual=%u "
                            "closed=%.17g integrated=%.17g diff=%.12g\n",
                            worst_name, worst_residual, closed.price,
                            integrated, difference);
            }
            worst_mass = std::max(worst_mass, closed.mass_error);
            worst_label = std::max(worst_label,
                label_difference/cases[c].contract.notional);
            pass &= std::isfinite(closed.price) &&
                    difference <= cases[c].contract.notional*1.0e-11 &&
                    label_difference <=
                        cases[c].contract.notional*1.0e-12 &&
                    closed.mass_error <= 1.0e-12;
        }
    }
    std::printf("STAGE0_COMMON_FACTOR_MATH cases=%u residual_vectors=%zu "
                "worst_relative=%.12g worst_case=%s worst_residual=%u "
                "worst_mass_error=%.12g worst_asset_label_relative=%.12g "
                "direct_integration=PAIRED_GL64_GL128_SPLIT decision=%s\n",
                AUTOCALL_WORSTOF_CASES, corpus.size(), worst, worst_name,
                worst_residual, worst_mass, worst_label,
                pass ? "PASS" : "FAIL");
    return pass;
}

static bool case_gate(const autocall_worstof_case_t &fixture,
                      const FrozenReference &reference,
                      const ConditionalResult &fixed,
                      const Metrics &m, double previous_rmse) {
    const double cap = fixture.contract.notional*
        (fixture.stress ? 3.0e-4 : 1.0e-4);
    const bool reference_ok = reference.adjacent && reference.uncertainty;
    const double fixed_error = std::fabs(fixed.price-reference.price.mean);
    const bool fixed_ok = fixed_error <= cap && fixed_error <= 3.0*m.rmse;
    const bool panel_ok = m.mean_abs <= cap && m.rmse <= cap;
    const bool interval_ok = fixture.stress ||
        m.half95 <= fixture.contract.notional*0.5e-4;
    const bool transfer = previous_rmse == 0.0 ||
        m.rmse <= previous_rmse*1.05;
    return reference_ok && fixed_ok && panel_ok && interval_ok && transfer &&
           std::isfinite(m.rmse) && fixed.mass_error <= 1.0e-12;
}

static double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[(values.size()-1u)/2u];
}

static double p90(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(
        std::floor(0.9*static_cast<double>(values.size()-1u)))];
}

} // namespace common_factor

int main() {
    using namespace common_factor;
    if (!stage0_math()) {
        std::puts("TWO_ASSET_COMMON_FACTOR_MATH_NOT_QUALIFIED");
        return 1;
    }
    std::array<std::array<std::uint32_t, 32>, 5> directions{};
    for (unsigned d = 0; d < 5; ++d)
        direction_row(d, directions[d].data());

    print_manifest("QUALIFICATION_SHIFT_MANIFEST", QUALIFICATION_SEED, false);
    print_manifest("HOLDOUT_SHIFT_MANIFEST", HOLDOUT_SEED, true);
    const auto references = build_reference(directions);
    const auto qualification = build_panel(
        QUALIFICATION_SEED, false, directions);
    const auto *cases = autocall_worstof_cases();

    std::array<decltype(fixed_prefixes(0u, directions)), ORDERS> fixed{};
    std::array<std::array<std::array<Metrics,
        AUTOCALL_WORSTOF_CASES>, COUNTS>, ORDERS> qm{};
    std::array<std::array<std::array<bool,
        AUTOCALL_WORSTOF_CASES>, COUNTS>, ORDERS> qpass{};
    for (unsigned order = 0; order < ORDERS; ++order) {
        fixed[order] = fixed_prefixes(order, directions);
        for (unsigned count = 0; count < COUNTS; ++count) {
            for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
                qm[order][count][c] = metrics(
                    qualification[order][count][c], references[c]);
                const double previous = count == 0u ? 0.0 :
                    qm[order][count-1u][c].rmse;
                qpass[order][count][c] = case_gate(
                    cases[c], references[c], fixed[order][count][c],
                    qm[order][count][c], previous);
                std::printf("QUALIFICATION_RESULT order=%s paths=%u "
                            "name=%s panel=%s mean=%.17g bias=%+.12g "
                            "mean_abs=%.12g rmse=%.12g half95=%.12g "
                            "median_abs=%.12g p90_abs=%.12g "
                            "worst_abs=%.12g fixed=%.17g "
                            "fixed_abs=%.12g fixed_over_rmse=%.12g "
                            "call_rmse1=%.12g call_rmse2=%.12g "
                            "call_rmse3=%.12g decision=%s\n",
                            ORDER_NAME[order], PATH_COUNTS[count],
                            cases[c].name,
                            cases[c].stress ? "STRESS" : "CORE",
                            qm[order][count][c].mean,
                            qm[order][count][c].bias,
                            qm[order][count][c].mean_abs,
                            qm[order][count][c].rmse,
                            qm[order][count][c].half95,
                            qm[order][count][c].median_abs,
                            qm[order][count][c].p90_abs,
                            qm[order][count][c].worst_abs,
                            fixed[order][count][c].price,
                            std::fabs(fixed[order][count][c].price-
                                      references[c].price.mean),
                            std::fabs(fixed[order][count][c].price-
                                      references[c].price.mean) /
                                qm[order][count][c].rmse,
                            qm[order][count][c].call_rmse[0],
                            qm[order][count][c].call_rmse[1],
                            qm[order][count][c].call_rmse[2],
                            qpass[order][count][c] ? "PASS" : "FAIL");
            }
        }
    }

    std::vector<double> r0_variance;
    for (unsigned count = 0; count < COUNTS; ++count)
        for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c)
            r0_variance.push_back(qm[0][count][c].variance /
                (cases[c].contract.notional*cases[c].contract.notional));
    const double r0_median = median(r0_variance);
    const double r0_p90 = p90(r0_variance);
    unsigned selected_order = 0u;
    double selected_variance = r0_median;
    for (unsigned order = 0; order < ORDERS; ++order) {
        std::vector<double> variance;
        double core_ss = 0.0;
        double stress_ss = 0.0;
        double r0_core_ss = 0.0;
        double r0_stress_ss = 0.0;
        unsigned core_n = 0u;
        unsigned stress_n = 0u;
        bool no_gate_regression = true;
        for (unsigned count = 0; count < COUNTS; ++count) {
            for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
                const double n = cases[c].contract.notional;
                variance.push_back(qm[order][count][c].variance/(n*n));
                if (qpass[0][count][c] && !qpass[order][count][c])
                    no_gate_regression = false;
                const double e = qm[order][count][c].rmse/n*10000.0;
                const double e0 = qm[0][count][c].rmse/n*10000.0;
                if (cases[c].stress) {
                    stress_ss += e*e;
                    r0_stress_ss += e0*e0;
                    ++stress_n;
                } else {
                    core_ss += e*e;
                    r0_core_ss += e0*e0;
                    ++core_n;
                }
            }
        }
        const double vm = median(variance);
        const double vp = p90(variance);
        const double core_rmse = std::sqrt(core_ss/core_n);
        const double stress_rmse = std::sqrt(stress_ss/stress_n);
        const double r0_core = std::sqrt(r0_core_ss/core_n);
        const double r0_stress = std::sqrt(r0_stress_ss/stress_n);
        const bool clear = order == 0u || vm <= 0.95*r0_median;
        const bool tail = vp <= 1.25*r0_p90;
        const bool classes = core_rmse <= r0_core && stress_rmse <= r0_stress;
        const bool eligible = clear && tail && classes && no_gate_regression;
        std::printf("ORDER_SELECTION order=%s median_variance=%.12g "
                    "ratio_vs_R0=%.12g p90_variance=%.12g "
                    "p90_ratio_vs_R0=%.12g core_aggregate_rmse_bp=%.12g "
                    "stress_aggregate_rmse_bp=%.12g "
                    "gate_5pct=%s gate_p90=%s gate_class_rmse=%s "
                    "gate_no_case_regression=%s eligible=%s\n",
                    ORDER_NAME[order], vm, vm/r0_median, vp, vp/r0_p90,
                    core_rmse, stress_rmse, clear ? "PASS" : "FAIL",
                    tail ? "PASS" : "FAIL", classes ? "PASS" : "FAIL",
                    no_gate_regression ? "PASS" : "FAIL",
                    eligible ? "YES" : "NO");
        if (order != 0u && eligible && vm < selected_variance) {
            selected_order = order;
            selected_variance = vm;
        }
    }
    std::printf("ORDER_SELECTED order=%s holdout_may_reselect=NO\n",
                ORDER_NAME[selected_order]);

    std::array<bool, COUNTS> self{};
    std::array<bool, COUNTS> global{};
    for (unsigned count = 0; count < COUNTS; ++count) {
        self[count] = true;
        for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c)
            self[count] &= qpass[selected_order][count][c];
    }
    for (unsigned count = 0; count+1u < COUNTS; ++count)
        global[count] = self[count] && self[count+1u];
    global[COUNTS-1u] = false;

    int selected_count = -1;
    double selected_score = std::numeric_limits<double>::infinity();
    for (unsigned count = 0; count < COUNTS; ++count) {
        std::vector<double> scores;
        for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
            const double bp = qm[selected_order][count][c].rmse /
                cases[c].contract.notional*10000.0;
            scores.push_back(bp*bp*PATH_COUNTS[count]);
        }
        const double score = median(scores);
        std::printf("PATH_SELECTION paths=%u self=%s global=%s "
                    "work_proxy_median=%.12g\n", PATH_COUNTS[count],
                    self[count] ? "PASS" : "FAIL",
                    global[count] ? "YES" : "NO", score);
        if (global[count] && score < selected_score) {
            selected_count = static_cast<int>(count);
            selected_score = score;
        }
    }
    if (selected_count < 0) {
        std::puts("TWO_ASSET_COMMON_FACTOR_PATH_COUNT_UNRESOLVED");
        return 0;
    }

    const auto holdout = build_panel(HOLDOUT_SEED, true, directions);
    bool holdout_pass = true;
    for (unsigned count = static_cast<unsigned>(selected_count);
         count <= static_cast<unsigned>(selected_count)+1u; ++count) {
        for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
            const Metrics hm = metrics(
                holdout[selected_order][count][c], references[c]);
            const Metrics previous = count == 0u ? Metrics{} : metrics(
                holdout[selected_order][count-1u][c], references[c]);
            const bool pass = case_gate(cases[c], references[c],
                fixed[selected_order][count][c], hm,
                count == 0u ? 0.0 : previous.rmse);
            holdout_pass &= pass;
            std::printf("HOLDOUT_RESULT order=%s paths=%u name=%s "
                        "panel=%s mean=%.17g bias=%+.12g mean_abs=%.12g "
                        "rmse=%.12g half95=%.12g median_abs=%.12g "
                        "p90_abs=%.12g worst_abs=%.12g "
                        "call_rmse1=%.12g call_rmse2=%.12g "
                        "call_rmse3=%.12g decision=%s\n",
                        ORDER_NAME[selected_order], PATH_COUNTS[count],
                        cases[c].name, cases[c].stress ? "STRESS" : "CORE",
                        hm.mean, hm.bias, hm.mean_abs, hm.rmse, hm.half95,
                        hm.median_abs, hm.p90_abs, hm.worst_abs,
                        hm.call_rmse[0], hm.call_rmse[1], hm.call_rmse[2],
                        pass ? "PASS" : "FAIL");
        }
    }
    std::printf("PORTABLE_SELECTION order=%s paths=%u successor=%u "
                "holdout=%s work_proxy=%.12g\n", ORDER_NAME[selected_order],
                PATH_COUNTS[selected_count], PATH_COUNTS[selected_count+1],
                holdout_pass ? "PASS" : "FAIL", selected_score);
    if (!holdout_pass) {
        std::puts("TWO_ASSET_COMMON_FACTOR_PATH_COUNT_UNRESOLVED");
        return 0;
    }
    std::puts("PORTABLE_COMMON_FACTOR_QUALIFIED native_stage=REQUIRED");
    return 0;
}
