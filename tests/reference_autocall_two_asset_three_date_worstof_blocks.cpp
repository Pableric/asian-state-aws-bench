#define main autocall_worstof_parent_reference_main_not_called
#include "tests/reference_autocall_two_asset_three_date_worstof_raw.cpp"
#undef main

#include <cerrno>
#include <limits>
#include <map>
#include <unordered_map>

namespace blocks {

constexpr std::uint32_t FIRST_INDEX = 8192u;
constexpr unsigned BLOCK_PATHS = 4096u;
constexpr unsigned BLOCKS = 4u;
constexpr unsigned COUNTS = 3u;
constexpr unsigned COUNT_BLOCKS[COUNTS] = {1u, 2u, 4u};
constexpr std::uint64_t QUALIFICATION_SEED = UINT64_C(0x54574f4153534554);
constexpr std::uint64_t HOLDOUT_SEED = UINT64_C(0x3257484f4c444f55);
constexpr unsigned REPLICATIONS = 32u;

struct ExactConstants {
    float drift_a;
    float diffusion_a;
    float drift_b;
    float diffusion_b;
    float rho;
    float cholesky;
    float spot_a;
    float spot_b;
    float inverse_spot_a;
    float inverse_spot_b;
    float protection_a;
    float protection_b;
    float terminal;
    float coupon_a[3];
    float coupon_b[3];
    float call_a[3];
    float call_b[3];
    float coupon[3];
    float call[3];
};

struct ExactResult {
    double price{};
    double replay{};
    std::array<double, 3> first_call{};
};

struct BlockResult {
    Decomposition mathematical;
    ExactResult exact;
};

struct PrefixResult {
    Decomposition mathematical;
    ExactResult exact;
};

struct ReferenceCase {
    Summary price;
    Decomposition decomposition;
    bool adjacent{};
    bool uncertainty{};
};

struct Metrics {
    double bias{};
    double mean_abs{};
    double rmse{};
    double half95{};
    double median_abs{};
    double p90_abs{};
    double worst_abs{};
    double replay_bp{};
    std::array<double, 3> call_rmse{};
};

struct PanelCase {
    std::array<std::array<double, REPLICATIONS>, COUNTS> price{};
    std::array<std::array<double, REPLICATIONS>, COUNTS> replay{};
    std::array<std::array<std::array<double, 3>, REPLICATIONS>, COUNTS>
        first_call{};
};

using Panel = std::array<PanelCase, AUTOCALL_WORSTOF_CASES>;

static float fmul(float a, float b) {
    volatile float x = a * b;
    return x;
}

static float fadd(float a, float b) {
    volatile float x = a + b;
    return x;
}

static float ffma(float a, float b, float c) {
    volatile float x = std::fma(a, b, c);
    return x;
}

static float exact_exp(float x) {
    static const std::uint32_t bits[12] = {
        UINT32_C(0x3fb8aa3b), UINT32_C(0x3f318000),
        UINT32_C(0xb95e8083), UINT32_C(0x3f800000),
        UINT32_C(0x3f7ffff9), UINT32_C(0x3efffffc),
        UINT32_C(0x3e2aabbf), UINT32_C(0x3d2aab67),
        UINT32_C(0x3c085d88), UINT32_C(0x3ab5de3b),
        UINT32_C(0x3959cfde), UINT32_C(0x37d8c471),
    };
    float c[12];
    std::memcpy(c, bits, sizeof(c));
    const float n = std::nearbyint(fmul(x, c[0]));
    float r = ffma(-c[1], n, x);
    r = ffma(-c[2], n, r);
    float p = c[11];
    for (int i = 10; i >= 3; --i)
        p = ffma(p, r, c[i]);
    return std::scalbn(p, static_cast<int>(n));
}

static double refined_inverse_normal(double p) {
    double z = inverse_normal_acklam(p);
    constexpr double inv_sqrt_2pi =
        0.39894228040143267793994605993438186848;
    constexpr double inv_sqrt2 =
        0.70710678118654752440084436210484903928;
    for (unsigned i = 0; i < 3u; ++i) {
        const double cdf = 0.5 * std::erfc(-z * inv_sqrt2);
        const double pdf = inv_sqrt_2pi * std::exp(-0.5 * z * z);
        z -= (cdf - p) / pdf;
    }
    return z;
}

static float correctly_rounded_float_normal(std::uint32_t word) {
    const long double p =
        (2.0L * static_cast<long double>(word) + 1.0L) /
        8589934592.0L;
    long double z = static_cast<long double>(
        inverse_normal_acklam(static_cast<double>(p)));
    const long double inv_sqrt_2pi =
        0.398942280401432677939946059934381868475858631164934657L;
    const long double inv_sqrt2 =
        0.707106781186547524400844362104849039284835937688474037L;
    for (unsigned i = 0; i < 5u; ++i) {
        const long double cdf = 0.5L * std::erfc(-z * inv_sqrt2);
        const long double pdf = inv_sqrt_2pi * std::exp(-0.5L * z * z);
        z -= (cdf - p) / pdf;
    }
    return static_cast<float>(z);
}

static std::array<std::uint32_t, 6> panel_shifts(
    std::uint64_t seed, unsigned replication, bool high_bits) {
    std::uint64_t state = seed ^
        (UINT64_C(0xd1b54a32d192ed03) * (replication + 1u));
    std::array<std::uint32_t, 6> out{};
    for (auto &word : out) {
        const std::uint64_t value = splitmix64(state);
        word = high_bits ? static_cast<std::uint32_t>(value >> 32) :
                           static_cast<std::uint32_t>(value);
    }
    return out;
}

static void print_manifest(const char *name, std::uint64_t seed,
                           bool high_bits) {
    std::printf("%s seed=0x%016llx extraction=%s replications=%u\n",
                name, static_cast<unsigned long long>(seed),
                high_bits ? "high32" : "inherited_low32", REPLICATIONS);
    for (unsigned rep = 0; rep < REPLICATIONS; ++rep) {
        const auto words = panel_shifts(seed, rep, high_bits);
        std::printf("%s_REP r=%u", name, rep);
        for (unsigned d = 0; d < 6; ++d)
            std::printf(" d%u=0x%08x", d + 1u, words[d]);
        std::putchar('\n');
    }
}

static ExactConstants exact_constants(const autocall_worstof_case_t &f) {
    ExactConstants out{};
    const auto &m = f.market;
    const auto &c = f.contract;
    const double dt = m.maturity / 3.0;
    out.drift_a = static_cast<float>((m.rate - m.dividend_a -
        0.5*m.sigma_a*m.sigma_a) * dt);
    out.diffusion_a = static_cast<float>(m.sigma_a * std::sqrt(dt));
    out.drift_b = static_cast<float>((m.rate - m.dividend_b -
        0.5*m.sigma_b*m.sigma_b) * dt);
    out.diffusion_b = static_cast<float>(m.sigma_b * std::sqrt(dt));
    out.rho = static_cast<float>(m.rho);
    out.cholesky = static_cast<float>(
        std::sqrt((1.0-m.rho)*(1.0+m.rho)));
    out.spot_a = static_cast<float>(c.spot_a);
    out.spot_b = static_cast<float>(c.spot_b);
    out.inverse_spot_a = 1.0f / out.spot_a;
    out.inverse_spot_b = 1.0f / out.spot_b;
    out.protection_a = static_cast<float>(c.spot_a*c.protection_barrier);
    out.protection_b = static_cast<float>(c.spot_b*c.protection_barrier);
    out.terminal = static_cast<float>(c.notional *
        std::exp(-m.rate*c.terminal_payment_time));
    for (unsigned d = 0; d < 3; ++d) {
        out.coupon_a[d] = static_cast<float>(
            c.spot_a*c.coupon_barrier[d]);
        out.coupon_b[d] = static_cast<float>(
            c.spot_b*c.coupon_barrier[d]);
        out.call_a[d] = static_cast<float>(c.spot_a*c.call_barrier[d]);
        out.call_b[d] = static_cast<float>(c.spot_b*c.call_barrier[d]);
        out.coupon[d] = static_cast<float>(c.coupon_cashflow[d] *
            std::exp(-m.rate*c.coupon_payment_time[d]));
        out.call[d] = static_cast<float>(c.call_redemption[d] *
            std::exp(-m.rate*c.call_payment_time[d]));
    }
    return out;
}

static Decomposition mathematical_block(
    const autocall_worstof_case_t &fixture,
    const std::array<std::array<std::uint32_t, 32>, 6> &directions,
    std::uint32_t first, const std::array<std::uint32_t, 6> &shift) {
    std::vector<Point> points(BLOCK_PATHS);
    for (unsigned p = 0; p < BLOCK_PATHS; ++p) {
        for (unsigned d = 0; d < 6; ++d) {
            const std::uint32_t word =
                sobol_word(first + p, directions[d].data()) ^ shift[d];
            const double u = (static_cast<double>(word) + 0.5) /
                             4294967296.0;
            points[p][d] = refined_inverse_normal(u);
        }
    }
    return evaluate_prefix(fixture, points, BLOCK_PATHS, false);
}

static ExactResult exact_block(
    const autocall_worstof_case_t &fixture,
    const std::array<std::array<std::uint32_t, 32>, 6> &directions,
    std::uint32_t first, const std::array<std::uint32_t, 6> &shift) {
    const ExactConstants k = exact_constants(fixture);
    float lo[16]{};
    float hi[16]{};
    double replay_total = 0.0;
    std::array<double, 3> calls{};
    for (unsigned path = 0; path < BLOCK_PATHS; ++path) {
        float z[6];
        for (unsigned d = 0; d < 6; ++d) {
            const std::uint32_t word =
                sobol_word(first + path, directions[d].data()) ^ shift[d];
            z[d] = correctly_rounded_float_normal(word);
        }
        float growth_a[3];
        float growth_b[3];
        for (unsigned date = 0; date < 3; ++date) {
            const float correlated = ffma(
                k.rho, z[2u*date], fmul(k.cholesky, z[2u*date+1u]));
            growth_a[date] = exact_exp(
                ffma(k.diffusion_a, z[2u*date], k.drift_a));
            growth_b[date] = exact_exp(
                ffma(k.diffusion_b, correlated, k.drift_b));
        }

        float sa = k.spot_a;
        float sb = k.spot_b;
        float pv = 0.0f;
        bool alive = true;
        double rsa = k.spot_a;
        double rsb = k.spot_b;
        double rpv = 0.0;
        bool ralive = true;
        for (unsigned date = 0; date < 3; ++date) {
            sa = fmul(sa, growth_a[date]);
            sb = fmul(sb, growth_b[date]);
            rsa *= static_cast<double>(growth_a[date]);
            rsb *= static_cast<double>(growth_b[date]);
            if (alive && sa >= k.coupon_a[date] &&
                sb >= k.coupon_b[date])
                pv = fadd(pv, k.coupon[date]);
            if (alive && sa >= k.call_a[date] && sb >= k.call_b[date]) {
                pv = fadd(pv, k.call[date]);
                calls[date] += 1.0;
                alive = false;
            }
            if (ralive && rsa >= static_cast<double>(k.coupon_a[date]) &&
                rsb >= static_cast<double>(k.coupon_b[date]))
                rpv += static_cast<double>(k.coupon[date]);
            if (ralive && rsa >= static_cast<double>(k.call_a[date]) &&
                rsb >= static_cast<double>(k.call_b[date])) {
                rpv += static_cast<double>(k.call[date]);
                ralive = false;
            }
        }
        if (alive) {
            if (sa >= k.protection_a && sb >= k.protection_b) {
                pv = fadd(pv, k.terminal);
            } else {
                const float wa = fmul(sa, k.inverse_spot_a);
                const float wb = fmul(sb, k.inverse_spot_b);
                pv = fadd(pv, fmul(k.terminal, wa < wb ? wa : wb));
            }
        }
        if (ralive) {
            if (rsa >= static_cast<double>(k.protection_a) &&
                rsb >= static_cast<double>(k.protection_b)) {
                rpv += static_cast<double>(k.terminal);
            } else {
                rpv += static_cast<double>(k.terminal) * std::min(
                    rsa*static_cast<double>(k.inverse_spot_a),
                    rsb*static_cast<double>(k.inverse_spot_b));
            }
        }
        float *acc = (path & 16u) == 0u ? lo : hi;
        acc[path & 15u] = fadd(acc[path & 15u], pv);
        replay_total += rpv;
    }
    float lanes[16];
    for (unsigned lane = 0; lane < 16; ++lane)
        lanes[lane] = fadd(lo[lane], hi[lane]);
    float q0[4];
    float q2[4];
    for (unsigned lane = 0; lane < 4; ++lane) {
        q0[lane] = fadd(lanes[lane], lanes[4u+lane]);
        q2[lane] = fadd(lanes[8u+lane], lanes[12u+lane]);
        q0[lane] = fadd(q0[lane], q2[lane]);
    }
    q0[0] = fadd(q0[0], q0[2]);
    q0[1] = fadd(q0[1], q0[3]);
    q0[0] = fadd(q0[0], q0[1]);
    ExactResult out;
    out.price = static_cast<double>(q0[0]) / BLOCK_PATHS;
    out.replay = replay_total / BLOCK_PATHS;
    for (unsigned d = 0; d < 3; ++d)
        out.first_call[d] = calls[d] / BLOCK_PATHS;
    return out;
}

static Decomposition combine_decomposition(
    const std::array<Decomposition, BLOCKS> &block, unsigned count_index) {
    const unsigned n = COUNT_BLOCKS[count_index];
    Decomposition out;
    auto add = [&](const Decomposition &x) {
        out.price += x.price;
        out.survival += x.survival;
        out.protected_contribution += x.protected_contribution;
        out.unprotected_contribution += x.unprotected_contribution;
        for (unsigned d = 0; d < 3; ++d) {
            out.first_call[d] += x.first_call[d];
            out.coupon_probability[d] += x.coupon_probability[d];
            out.coupon_contribution[d] += x.coupon_contribution[d];
            out.call_contribution[d] += x.call_contribution[d];
        }
    };
    if (n == 1u) {
        add(block[0]);
    } else if (n == 2u) {
        add(block[0]); add(block[1]);
    } else {
        Decomposition pair01;
        Decomposition pair23;
        auto pair = [](const Decomposition &a, const Decomposition &b) {
            Decomposition r;
            r.price = a.price+b.price;
            r.survival = a.survival+b.survival;
            r.protected_contribution = a.protected_contribution+b.protected_contribution;
            r.unprotected_contribution = a.unprotected_contribution+b.unprotected_contribution;
            for (unsigned d=0; d<3; ++d) {
                r.first_call[d]=a.first_call[d]+b.first_call[d];
                r.coupon_probability[d]=a.coupon_probability[d]+b.coupon_probability[d];
                r.coupon_contribution[d]=a.coupon_contribution[d]+b.coupon_contribution[d];
                r.call_contribution[d]=a.call_contribution[d]+b.call_contribution[d];
            }
            return r;
        };
        pair01 = pair(block[0], block[1]);
        pair23 = pair(block[2], block[3]);
        add(pair01); add(pair23);
    }
    const double scale = 1.0/static_cast<double>(n);
    out.price *= scale;
    out.survival *= scale;
    out.protected_contribution *= scale;
    out.unprotected_contribution *= scale;
    for (unsigned d = 0; d < 3; ++d) {
        out.first_call[d] *= scale;
        out.coupon_probability[d] *= scale;
        out.coupon_contribution[d] *= scale;
        out.call_contribution[d] *= scale;
    }
    return out;
}

static ExactResult combine_exact(
    const std::array<ExactResult, BLOCKS> &block, unsigned count_index) {
    ExactResult out;
    if (count_index == 0u) {
        out = block[0];
        return out;
    }
    if (count_index == 1u) {
        out.price = (block[0].price + block[1].price) * 0.5;
        out.replay = (block[0].replay + block[1].replay) * 0.5;
        for (unsigned d = 0; d < 3; ++d)
            out.first_call[d] =
                (block[0].first_call[d] + block[1].first_call[d]) * 0.5;
        return out;
    }
    const double price01 = block[0].price + block[1].price;
    const double price23 = block[2].price + block[3].price;
    const double replay01 = block[0].replay + block[1].replay;
    const double replay23 = block[2].replay + block[3].replay;
    out.price = (price01 + price23) * 0.25;
    out.replay = (replay01 + replay23) * 0.25;
    for (unsigned d = 0; d < 3; ++d) {
        const double c01 = block[0].first_call[d] + block[1].first_call[d];
        const double c23 = block[2].first_call[d] + block[3].first_call[d];
        out.first_call[d] = (c01+c23) * 0.25;
    }
    return out;
}

static PrefixResult combine_blocks(
    const std::array<BlockResult, BLOCKS> &block, unsigned count_index) {
    std::array<Decomposition, BLOCKS> math{};
    std::array<ExactResult, BLOCKS> exact{};
    for (unsigned b = 0; b < BLOCKS; ++b) {
        math[b] = block[b].mathematical;
        exact[b] = block[b].exact;
    }
    return {combine_decomposition(math, count_index),
            combine_exact(exact, count_index)};
}

static std::array<BlockResult, BLOCKS> evaluate_blocks(
    const autocall_worstof_case_t &fixture,
    const std::array<std::array<std::uint32_t, 32>, 6> &directions,
    const std::array<std::uint32_t, 6> &shift) {
    std::array<BlockResult, BLOCKS> out{};
    for (unsigned b = 0; b < BLOCKS; ++b) {
        const std::uint32_t first = FIRST_INDEX + b*BLOCK_PATHS;
        out[b].mathematical = mathematical_block(
            fixture, directions, first, shift);
        out[b].exact = exact_block(fixture, directions, first, shift);
    }
    return out;
}

static std::array<ReferenceCase, AUTOCALL_WORSTOF_CASES>
build_reference() {
    unsigned log2_count = 18u;
    auto level = reference_level(log2_count);
    bool resolved = reference_resolved(level, log2_count);
    while (!resolved && log2_count < 22u) {
        log2_count += 2u;
        level = reference_level(log2_count);
        resolved = reference_resolved(level, log2_count);
    }
    const auto *cases = autocall_worstof_cases();
    std::array<ReferenceCase, AUTOCALL_WORSTOF_CASES> out{};
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
        std::vector<double> half;
        std::vector<double> full;
        for (const auto &rep : level) {
            half.push_back(rep[c].half.price);
            full.push_back(rep[c].full.price);
            const Decomposition &d = rep[c].full;
            out[c].decomposition.price += d.price;
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
        const Summary h = summary(half);
        out[c].price = summary(full);
        out[c].adjacent = std::fabs(h.mean-out[c].price.mean) <=
            1.96*std::hypot(h.stderr, out[c].price.stderr);
        const double required = cases[c].stress ? 0.0015 : 0.0005;
        out[c].uncertainty = out[c].price.half95 <= required;
        const double scale = 1.0/static_cast<double>(REFERENCE_REPS);
        Decomposition &d = out[c].decomposition;
        d.price *= scale;
        d.survival *= scale;
        d.protected_contribution *= scale;
        d.unprotected_contribution *= scale;
        for (unsigned date = 0; date < 3; ++date) {
            d.first_call[date] *= scale;
            d.coupon_probability[date] *= scale;
            d.coupon_contribution[date] *= scale;
            d.call_contribution[date] *= scale;
        }
        const double mass = std::fabs(d.first_call[0]+d.first_call[1]+
            d.first_call[2]+d.survival-1.0);
        std::printf(
            "BLOCK_REFERENCE name=%s panel=%s mean=%.17g low=%.17g "
            "high=%.17g stderr=%.12g half95=%.12g adjacent=%s "
            "uncertainty=%s call1=%.12g call2=%.12g call3=%.12g "
            "survival=%.12g coupon_pv1=%.12g coupon_pv2=%.12g "
            "coupon_pv3=%.12g call_pv1=%.12g call_pv2=%.12g "
            "call_pv3=%.12g protected_terminal=%.12g "
            "unprotected_terminal=%.12g mass_error=%.12g\n",
            cases[c].name, cases[c].stress ? "STRESS" : "CORE",
            out[c].price.mean, out[c].price.mean-out[c].price.half95,
            out[c].price.mean+out[c].price.half95,
            out[c].price.stderr, out[c].price.half95,
            out[c].adjacent ? "PASS" : "FAIL",
            out[c].uncertainty ? "PASS" : "FAIL",
            d.first_call[0], d.first_call[1], d.first_call[2], d.survival,
            d.coupon_contribution[0], d.coupon_contribution[1],
            d.coupon_contribution[2], d.call_contribution[0],
            d.call_contribution[1], d.call_contribution[2],
            d.protected_contribution, d.unprotected_contribution, mass);
    }
    std::printf("BLOCK_REFERENCE_PROTOCOL final_log2=%u replications=%u "
                "max_concurrent=2 deterministic_order=YES\n",
                log2_count, REFERENCE_REPS);
    return out;
}

static void build_panel(
    Panel &out, std::uint64_t seed, bool high_bits,
    const std::array<std::array<std::uint32_t, 32>, 6> &directions) {
    const auto *cases = autocall_worstof_cases();
    std::atomic<unsigned> next{0u};
    auto worker = [&]() {
        for (;;) {
            const unsigned rep = next.fetch_add(1u);
            if (rep >= REPLICATIONS)
                break;
            const auto shift = panel_shifts(seed, rep, high_bits);
            for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
                const auto blocks = evaluate_blocks(cases[c], directions, shift);
                for (unsigned n = 0; n < COUNTS; ++n) {
                    const PrefixResult prefix = combine_blocks(blocks, n);
                    out[c].price[n][rep] = prefix.exact.price;
                    out[c].replay[n][rep] = prefix.exact.replay;
                    out[c].first_call[n][rep] = prefix.exact.first_call;
                }
            }
        }
    };
    std::thread first(worker);
    std::thread second(worker);
    first.join();
    second.join();
}

static Metrics metrics(const PanelCase &panel, unsigned count,
                       const ReferenceCase &reference,
                       double notional) {
    Metrics out;
    std::vector<double> absolute;
    std::array<double, REPLICATIONS> errors{};
    for (unsigned rep = 0; rep < REPLICATIONS; ++rep) {
        errors[rep] = panel.price[count][rep]-reference.price.mean;
        out.bias += errors[rep];
        out.mean_abs += std::fabs(errors[rep]);
        out.rmse += errors[rep]*errors[rep];
        out.replay_bp += std::fabs(
            panel.price[count][rep]-panel.replay[count][rep]) /
            notional*10000.0;
        absolute.push_back(std::fabs(errors[rep]));
        for (unsigned date = 0; date < 3; ++date) {
            const double e = panel.first_call[count][rep][date]-
                             reference.decomposition.first_call[date];
            out.call_rmse[date] += e*e;
        }
    }
    out.bias /= REPLICATIONS;
    out.mean_abs /= REPLICATIONS;
    out.rmse = std::sqrt(out.rmse/REPLICATIONS);
    out.replay_bp /= REPLICATIONS;
    double centered = 0.0;
    for (double error : errors)
        centered += (error-out.bias)*(error-out.bias);
    const double stderr = std::sqrt(centered /
        (static_cast<double>(REPLICATIONS-1u)*REPLICATIONS));
    out.half95 = 2.0395134463964077*stderr;
    out.median_abs = percentile(absolute, 0.5);
    out.p90_abs = percentile(absolute, 0.9);
    out.worst_abs = *std::max_element(absolute.begin(), absolute.end());
    for (double &value : out.call_rmse)
        value = std::sqrt(value/REPLICATIONS);
    return out;
}

static bool verify_words_and_float_rounding(
    const std::array<std::array<std::uint32_t, 32>, 6> &directions) {
    FILE *file = std::fopen("private/asian_variable_sobol_signed_z.bin", "rb");
    if (file == nullptr) {
        std::fprintf(stderr, "signed-z provenance open failed: %s\n",
                     std::strerror(errno));
        return false;
    }
    std::vector<float> bank(30u*BLOCK_PATHS);
    const bool read_ok = std::fread(bank.data(), sizeof(float), bank.size(),
                                    file) == bank.size() &&
                         std::fgetc(file) == EOF;
    std::fclose(file);
    if (!read_ok)
        return false;
    std::unordered_map<std::uint32_t, std::uint32_t> word_to_bits;
    word_to_bits.reserve(bank.size()*2u);
    for (unsigned p = 0; p < bank.size(); ++p) {
        const std::uint32_t word = sobol_word(
            FIRST_INDEX+p, directions[0].data());
        std::uint32_t bits;
        std::memcpy(&bits, &bank[p], sizeof(bits));
        if (!word_to_bits.emplace(word, bits).second)
            return false;
        float generated = correctly_rounded_float_normal(word);
        std::uint32_t generated_bits;
        std::memcpy(&generated_bits, &generated, sizeof(generated_bits));
        if (generated_bits != bits)
            return false;
    }
    for (unsigned b = 0; b < BLOCKS; ++b) {
        for (unsigned d = 0; d < 6; ++d) {
            std::unordered_map<std::uint32_t, unsigned> unique;
            unique.reserve(BLOCK_PATHS*2u);
            for (unsigned p = 0; p < BLOCK_PATHS; ++p) {
                const std::uint32_t index = FIRST_INDEX+b*BLOCK_PATHS+p;
                const std::uint32_t word = sobol_word(index,
                    directions[d].data());
                if (word == 0u || word == UINT32_MAX ||
                    !unique.emplace(word, p).second ||
                    word_to_bits.find(word) == word_to_bits.end())
                    return false;
                float generated = correctly_rounded_float_normal(word);
                std::uint32_t bits;
                std::memcpy(&bits, &generated, sizeof(bits));
                if (bits != word_to_bits.at(word))
                    return false;
            }
            std::printf("SOBOL_BLOCK block=B%u dimension=D%u "
                        "first=%u last=%u unique=4096 gaussian_bits=PASS\n",
                        b, d+1u, FIRST_INDEX+b*BLOCK_PATHS,
                        FIRST_INDEX+(b+1u)*BLOCK_PATHS-1u);
        }
    }
    std::puts("ABSOLUTE_INDEX_SOBOL PASS dimensions=D1-D6 blocks=B0-B3 "
              "index_zero=ABSENT exact_float_gaussian=PASS");
    return true;
}

static bool gate_metrics(const autocall_worstof_case_t &fixture,
                         const ReferenceCase &reference,
                         const PrefixResult &fixed,
                         const Metrics &value,
                         double previous_rmse,
                         bool print, const char *bank, unsigned count) {
    const double cap = fixture.contract.notional *
        (fixture.stress ? 3e-4 : 1e-4);
    const double half_cap = fixture.contract.notional*0.5e-4;
    const double fixed_error = std::fabs(
        fixed.exact.price-reference.price.mean);
    const double f64_fixed_error = std::fabs(
        fixed.mathematical.price-reference.price.mean);
    const double replay_bp = std::fabs(
        fixed.exact.price-fixed.exact.replay) /
        fixture.contract.notional*10000.0;
    const bool reference_gate = reference.adjacent && reference.uncertainty;
    const bool fixed_cap = fixed_error <= cap && f64_fixed_error <= cap;
    const bool fixed_ratio = fixed_error <= 3.0*value.rmse;
    const bool panel_error = value.mean_abs <= cap && value.rmse <= cap;
    const bool interval = fixture.stress || value.half95 <= half_cap;
    const bool replay = replay_bp <= 0.25 && value.replay_bp <= 0.25;
    const bool nonregression = previous_rmse == 0.0 ||
        value.rmse <= previous_rmse*1.05;
    const bool finite = std::isfinite(fixed.exact.price) &&
        std::isfinite(fixed.mathematical.price) &&
        std::isfinite(value.mean_abs) && std::isfinite(value.rmse) &&
        std::isfinite(value.half95);
    const bool pass = reference_gate && fixed_cap && fixed_ratio &&
        panel_error && interval && replay && nonregression && finite;
    if (print) {
        std::printf(
            "PANEL_RESULT bank=%s name=%s panel=%s paths=%u "
            "mean=%.17g bias=%+.12g mean_abs=%.12g rmse=%.12g "
            "half95=%.12g median_abs=%.12g p90_abs=%.12g "
            "worst_abs=%.12g call_rmse1=%.12g call_rmse2=%.12g "
            "call_rmse3=%.12g replay_bp=%.12g rmse_vs_previous=%.12g "
            "gate_reference=%s gate_fixed_cap=%s gate_fixed_3x_rmse=%s "
            "gate_mean_abs_rmse=%s gate_half95=%s gate_replay=%s "
            "gate_rmse_transfer=%s gate_finite=%s decision=%s\n",
            bank, fixture.name, fixture.stress ? "STRESS" : "CORE",
            count, reference.price.mean+value.bias, value.bias,
            value.mean_abs, value.rmse, value.half95, value.median_abs,
            value.p90_abs, value.worst_abs, value.call_rmse[0],
            value.call_rmse[1], value.call_rmse[2], value.replay_bp,
            previous_rmse == 0.0 ? 0.0 : value.rmse/previous_rmse,
            reference_gate ? "PASS" : "FAIL",
            fixed_cap ? "PASS" : "FAIL", fixed_ratio ? "PASS" : "FAIL",
            panel_error ? "PASS" : "FAIL", interval ? "PASS" : "FAIL",
            replay ? "PASS" : "FAIL", nonregression ? "PASS" : "FAIL",
            finite ? "PASS" : "FAIL", pass ? "PASS" : "FAIL");
    }
    return pass;
}

static bool structural_regression(
    const std::array<std::array<std::uint32_t, 32>, 6> &directions) {
    const std::array<std::uint32_t, 6> no_shift{};
    const autocall_worstof_case_t base = autocall_worstof_cases()[0];
    bool pass = true;

    autocall_worstof_case_t all_call = base;
    for (unsigned d = 0; d < 3; ++d) {
        all_call.contract.call_barrier[d] =
            std::numeric_limits<float>::min();
        all_call.contract.coupon_barrier[d] =
            std::numeric_limits<float>::min();
    }
    const ExactResult called = exact_block(
        all_call, directions, FIRST_INDEX, no_shift);
    pass &= called.first_call[0] == 1.0 && called.first_call[1] == 0.0 &&
            called.first_call[2] == 0.0 && std::isfinite(called.price);

    autocall_worstof_case_t protected_case = base;
    for (unsigned d = 0; d < 3; ++d) {
        protected_case.contract.call_barrier[d] = 1.0e20;
        protected_case.contract.coupon_barrier[d] = 1.0e20;
        protected_case.contract.coupon_cashflow[d] = 0.0;
    }
    protected_case.contract.protection_barrier =
        std::numeric_limits<float>::min();
    const ExactResult protected_value = exact_block(
        protected_case, directions, FIRST_INDEX, no_shift);
    pass &= protected_value.first_call[0] == 0.0 &&
            protected_value.first_call[1] == 0.0 &&
            protected_value.first_call[2] == 0.0 &&
            std::isfinite(protected_value.price);

    autocall_worstof_case_t downside = protected_case;
    downside.contract.protection_barrier = 1.0e20;
    const ExactResult downside_value = exact_block(
        downside, directions, FIRST_INDEX, no_shift);
    pass &= std::isfinite(downside_value.price) &&
            downside_value.price < protected_value.price;

    autocall_worstof_case_t rho_positive = base;
    autocall_worstof_case_t rho_negative = base;
    rho_positive.market.rho = std::nextafter(1.0, 0.0);
    rho_negative.market.rho = std::nextafter(-1.0, 0.0);
    const ExactResult positive_value = exact_block(
        rho_positive, directions, FIRST_INDEX, no_shift);
    const ExactResult negative_value = exact_block(
        rho_negative, directions, FIRST_INDEX, no_shift);
    pass &= std::isfinite(positive_value.price) &&
            std::isfinite(negative_value.price);

    autocall_worstof_case_t equality = base;
    const ExactConstants base_constants = exact_constants(base);
    float z0 = correctly_rounded_float_normal(sobol_word(
        FIRST_INDEX, directions[0].data()));
    float z1 = correctly_rounded_float_normal(sobol_word(
        FIRST_INDEX, directions[1].data()));
    const float ga = exact_exp(ffma(base_constants.diffusion_a, z0,
                                    base_constants.drift_a));
    const float correlated = ffma(base_constants.rho, z0,
                                  fmul(base_constants.cholesky, z1));
    const float gb = exact_exp(ffma(base_constants.diffusion_b, correlated,
                                    base_constants.drift_b));
    const float equality_ratio = ga < gb ? ga : gb;
    equality.contract.call_barrier[0] = equality_ratio;
    equality.contract.coupon_barrier[0] = equality_ratio;
    const ExactConstants equality_constants = exact_constants(equality);
    const float sa = fmul(equality_constants.spot_a, ga);
    const float sb = fmul(equality_constants.spot_b, gb);
    const bool equality_constructed =
        sa == equality_constants.call_a[0] ||
        sb == equality_constants.call_b[0];
    const ExactResult equality_first = exact_block(
        equality, directions, FIRST_INDEX, no_shift);
    const ExactResult equality_repeat = exact_block(
        equality, directions, FIRST_INDEX, no_shift);
    std::uint64_t first_bits;
    std::uint64_t repeat_bits;
    std::memcpy(&first_bits, &equality_first.price, sizeof(first_bits));
    std::memcpy(&repeat_bits, &equality_repeat.price, sizeof(repeat_bits));
    pass &= equality_constructed && first_bits == repeat_bits;

    std::printf("STRUCTURAL_REGRESSION all_call_d1=%s no_call=%s "
                "protected=%s downside=%s rho_near_limits=%s "
                "exact_equality=%s repeated=%s decision=%s\n",
                called.first_call[0] == 1.0 ? "PASS" : "FAIL",
                protected_value.first_call[0] == 0.0 ? "PASS" : "FAIL",
                std::isfinite(protected_value.price) ? "PASS" : "FAIL",
                std::isfinite(downside_value.price) ? "PASS" : "FAIL",
                std::isfinite(positive_value.price) &&
                    std::isfinite(negative_value.price) ? "PASS" : "FAIL",
                equality_constructed ? "PASS" : "FAIL",
                first_bits == repeat_bits ? "PASS" : "FAIL",
                pass ? "PASS" : "FAIL");
    return pass;
}

} // namespace blocks

int main() {
    using namespace blocks;
    std::array<std::array<std::uint32_t, 32>, 6> directions{};
    for (unsigned d = 0; d < 6; ++d)
        direction_row(d, directions[d].data());
    if (!verify_words_and_float_rounding(directions)) {
        std::fprintf(stderr, "absolute-index or exact-Gaussian gate failed\n");
        return 1;
    }
    const bool structural_pass = structural_regression(directions);
    if (!structural_pass)
        return 1;

    print_manifest("QUALIFICATION_SHIFT_MANIFEST", QUALIFICATION_SEED, false);
    print_manifest("HOLDOUT_SHIFT_MANIFEST", HOLDOUT_SEED, true);

    const auto reference = build_reference();
    const auto *cases = autocall_worstof_cases();
    std::array<std::array<BlockResult, BLOCKS>, AUTOCALL_WORSTOF_CASES>
        fixed_blocks{};
    const std::array<std::uint32_t, 6> no_shift{};
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c)
        fixed_blocks[c] = evaluate_blocks(cases[c], directions, no_shift);

    Panel qualification{};
    Panel holdout{};
    build_panel(qualification, QUALIFICATION_SEED, false, directions);
    build_panel(holdout, HOLDOUT_SEED, true, directions);

    std::array<std::array<Metrics, COUNTS>, AUTOCALL_WORSTOF_CASES> qm{};
    std::array<std::array<Metrics, COUNTS>, AUTOCALL_WORSTOF_CASES> hm{};
    std::array<std::array<bool, COUNTS>, AUTOCALL_WORSTOF_CASES> qpass{};
    std::array<std::array<bool, COUNTS>, AUTOCALL_WORSTOF_CASES> hpass{};
    bool parent_b0 = true;

    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
        std::array<PrefixResult, COUNTS> fixed{};
        for (unsigned n = 0; n < COUNTS; ++n) {
            fixed[n] = combine_blocks(fixed_blocks[c], n);
            qm[c][n] = metrics(qualification[c], n, reference[c],
                               cases[c].contract.notional);
            hm[c][n] = metrics(holdout[c], n, reference[c],
                               cases[c].contract.notional);
            const double previous_q = n == 0u ? 0.0 : qm[c][n-1u].rmse;
            const double previous_h = n == 0u ? 0.0 : hm[c][n-1u].rmse;
            qpass[c][n] = gate_metrics(cases[c], reference[c], fixed[n],
                qm[c][n], previous_q, false, "QUALIFICATION",
                COUNT_BLOCKS[n]*BLOCK_PATHS);
            hpass[c][n] = gate_metrics(cases[c], reference[c], fixed[n],
                hm[c][n], previous_h, false, "HOLDOUT",
                COUNT_BLOCKS[n]*BLOCK_PATHS);
        }

        for (unsigned b = 0; b < BLOCKS; ++b) {
            const BlockResult &value = fixed_blocks[c][b];
            const double signed_error = value.exact.price-reference[c].price.mean;
            const double f64_error =
                value.mathematical.price-reference[c].price.mean;
            const double bp = signed_error/cases[c].contract.notional*10000.0;
            const double contribution =
                (value.exact.price-value.exact.replay) /
                cases[c].contract.notional*10000.0;
            std::printf(
                "BLOCK_RESULT name=%s panel=%s block=B%u first=%u last=%u "
                "reference=%.17g ref_low=%.17g ref_high=%.17g "
                "f64_math=%.17g binary32=%.17g f64_exact_growth=%.17g "
                "f64_signed_error=%+.12g f64_abs_error=%.12g "
                "signed_error=%+.12g abs_error=%.12g signed_bp=%+.12g "
                "abs_bp=%.12g binary32_replay_contribution_bp=%+.12g "
                "classification=DIAGNOSTIC_ONLY\n",
                cases[c].name, cases[c].stress ? "STRESS" : "CORE", b,
                FIRST_INDEX+b*BLOCK_PATHS,
                FIRST_INDEX+(b+1u)*BLOCK_PATHS-1u,
                reference[c].price.mean,
                reference[c].price.mean-reference[c].price.half95,
                reference[c].price.mean+reference[c].price.half95,
                value.mathematical.price, value.exact.price,
                value.exact.replay, f64_error, std::fabs(f64_error),
                signed_error, std::fabs(signed_error), bp, std::fabs(bp),
                contribution);
        }

        for (unsigned n = 0; n < COUNTS; ++n) {
            const PrefixResult &value = fixed[n];
            const double signed_error = value.exact.price-reference[c].price.mean;
            const double f64_error =
                value.mathematical.price-reference[c].price.mean;
            const double bp = signed_error/cases[c].contract.notional*10000.0;
            const double replay_bp =
                (value.exact.price-value.exact.replay) /
                cases[c].contract.notional*10000.0;
            std::printf(
                "PREFIX_RESULT name=%s panel=%s paths=%u blocks=%u "
                "reference=%.17g ref_low=%.17g ref_high=%.17g "
                "f64_math=%.17g binary32=%.17g f64_exact_growth=%.17g "
                "f64_signed_error=%+.12g f64_abs_error=%.12g "
                "signed_error=%+.12g abs_error=%.12g signed_bp=%+.12g "
                "abs_bp=%.12g binary32_replay_contribution_bp=%+.12g "
                "qualification_rmse=%.12g fixed_over_rmse=%.12g "
                "gate_qualification=%s gate_holdout=%s\n",
                cases[c].name, cases[c].stress ? "STRESS" : "CORE",
                COUNT_BLOCKS[n]*BLOCK_PATHS, COUNT_BLOCKS[n],
                reference[c].price.mean,
                reference[c].price.mean-reference[c].price.half95,
                reference[c].price.mean+reference[c].price.half95,
                value.mathematical.price, value.exact.price,
                value.exact.replay, f64_error, std::fabs(f64_error),
                signed_error, std::fabs(signed_error), bp, std::fabs(bp),
                replay_bp, qm[c][n].rmse,
                qm[c][n].rmse == 0.0 ? 0.0 :
                    std::fabs(signed_error)/qm[c][n].rmse,
                qpass[c][n] ? "PASS" : "FAIL",
                hpass[c][n] ? "PASS" : "FAIL");
        }
        parent_b0 &= std::isfinite(fixed[0].exact.price) &&
            std::fabs(fixed[0].exact.price-fixed[0].exact.replay) /
                cases[c].contract.notional*10000.0 <= 0.25;
    }

    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
        std::array<PrefixResult, COUNTS> fixed{};
        for (unsigned n = 0; n < COUNTS; ++n)
            fixed[n] = combine_blocks(fixed_blocks[c], n);
        for (unsigned n = 0; n < COUNTS; ++n) {
            const double previous_q = n == 0u ? 0.0 : qm[c][n-1u].rmse;
            const double previous_h = n == 0u ? 0.0 : hm[c][n-1u].rmse;
            (void)gate_metrics(cases[c], reference[c], fixed[n], qm[c][n],
                previous_q, true, "QUALIFICATION",
                COUNT_BLOCKS[n]*BLOCK_PATHS);
            (void)gate_metrics(cases[c], reference[c], fixed[n], hm[c][n],
                previous_h, true, "HOLDOUT",
                COUNT_BLOCKS[n]*BLOCK_PATHS);
        }
    }

    bool q8192 = true;
    bool q16384 = true;
    bool h8192 = true;
    bool h16384 = true;
    for (unsigned c = 0; c < AUTOCALL_WORSTOF_CASES; ++c) {
        q8192 &= qpass[c][1];
        q16384 &= qpass[c][2];
        h8192 &= hpass[c][1];
        h16384 &= hpass[c][2];
    }
    std::printf("PARENT_TWO_ASSET_REGRESSION B0_exact_gaussian=PASS "
                "binary32_replay=%s parent_blobs=UNCHANGED_BY_MANIFEST\n",
                parent_b0 ? "PASS" : "FAIL");
    std::printf("PREFIX_QUALIFICATION P8192_selection=%s "
                "P16384_selection=%s P8192_holdout=%s P16384_holdout=%s\n",
                q8192 ? "PASS" : "FAIL", q16384 ? "PASS" : "FAIL",
                h8192 ? "PASS" : "FAIL", h16384 ? "PASS" : "FAIL");

    if (parent_b0 && structural_pass && q8192 && q16384 &&
        h8192 && h16384) {
        std::puts("TWO_ASSET_RAW_8192_ACCURACY_READY");
    } else if (parent_b0 && structural_pass && !q8192 &&
               q16384 && h16384) {
        std::puts("TWO_ASSET_RAW_16384_ACCURACY_PROVISIONAL");
    } else {
        std::puts("TWO_ASSET_COMMON_FACTOR_PREINTEGRATION_NEXT");
    }
    return parent_b0 ? 0 : 1;
}
