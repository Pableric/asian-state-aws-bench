#define _POSIX_C_SOURCE 200112L

#include "private/autocall_two_asset_three_date_worstof_raw_diag.h"
#include "private/asian_genuine_fixed_block_source_diag.h"
#include "private/asian_genuine_permute.h"
#include "tests/autocall_two_asset_three_date_worstof_cases.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GENERIC_PLAN_MAGIC = 0x54525351u, JOE_KUO_RECORD_BYTES = 132 };

struct asian_meta_qsort_control_plan {
    uint32_t magic;
    uint8_t donor_region[ASIAN_META_DIRECTIONS];
    uint8_t reserved[60];
    fragment_map_t maps[ASIAN_META_DIRECTIONS];
};

extern const unsigned char asian_arithmetic_joe_kuo_256_records[];
extern uint64_t autocall_worstof_prepared_leaf_invocations;
extern uint64_t autocall_worstof_inline_leaf_invocations;
double autocall_worstof_generic_vector_price_test(
    const autocall_worstof_prepared_leaf_context_t *);

static void *a64(size_t bytes) {
    void *p = NULL;
    return posix_memalign(&p, 64u, bytes) == 0 ? p : NULL;
}

static int same_double(double a, double b) {
    uint64_t aa;
    uint64_t bb;
    memcpy(&aa, &a, sizeof(aa));
    memcpy(&bb, &b, sizeof(bb));
    return aa == bb;
}

static int same_float(float a, float b) {
    uint32_t aa;
    uint32_t bb;
    memcpy(&aa, &a, sizeof(aa));
    memcpy(&bb, &b, sizeof(bb));
    return aa == bb;
}

static float fmul(float a, float b) {
    volatile float x = a * b;
    return x;
}

static float fadd(float a, float b) {
    volatile float x = a + b;
    return x;
}

static float ffma(float a, float b, float c) {
    volatile float x = fmaf(a, b, c);
    return x;
}

static uint32_t sobol_word(uint32_t index, const uint32_t directions[32]) {
    uint32_t gray = index ^ (index >> 1);
    uint32_t word = 0u;
    for (uint32_t bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u)
            word ^= directions[bit];
    return word;
}

static void direction_row(uint32_t dimension, uint32_t out[32]) {
    const unsigned char *record = asian_arithmetic_joe_kuo_256_records +
        (size_t)dimension * JOE_KUO_RECORD_BYTES;
    uint32_t count;
    memcpy(&count, record, sizeof(count));
    if (count != 32u)
        abort();
    memcpy(out, record + 4u, 32u * sizeof(uint32_t));
}

static uint32_t generic_source(const fragment_map_t *map, uint32_t path) {
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    const uint32_t pattern = map->select[packet][2u + half];
    return (uint32_t)map->select[packet][half] * 16u +
           map->patterns[pattern][lane];
}

static uint32_t affine_source(const asian_meta_dim_affine_ctx_t *map,
                              uint32_t path) {
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    uint32_t control = map->base_control[lane] ^ map->sel2[packet][1];
    if (half != 0u)
        control ^= map->half_delta[lane];
    return ((uint32_t)(map->sel2[packet][0] ^ half) << 4) | control;
}

static int route_integrity(
    const asian_meta_affine_plan_t *affine,
    const struct asian_meta_qsort_control_plan *generic) {
    uint32_t d1[32];
    uint32_t target[32];
    direction_row(0u, d1);
    unsigned regions = 0u;
    for (uint32_t dimension = 0; dimension < 6u; ++dimension) {
        direction_row(dimension, target);
        unsigned char seen[ASIAN_META_PATHS] = {0};
        if (affine->donor_region[dimension] !=
            generic->donor_region[dimension])
            return -1;
        regions |= 1u << affine->donor_region[dimension];
        for (uint32_t path = 0; path < ASIAN_META_PATHS; ++path) {
            const uint32_t gs = generic_source(&generic->maps[dimension], path);
            const uint32_t as = affine_source(&affine->contexts[dimension], path);
            if (gs != as || gs >= ASIAN_META_PATHS || seen[gs] != 0u)
                return -1;
            seen[gs] = 1u;
            const uint32_t donor = 8192u +
                generic->donor_region[dimension] * ASIAN_META_PATHS + gs;
            if (sobol_word(donor, d1) != sobol_word(8192u + path, target))
                return -1;
        }
    }
    return regions == 3u ? 0 : -1;
}

static float exact_exp(float x) {
    static const uint32_t bits[12] = {
        UINT32_C(0x3fb8aa3b), UINT32_C(0x3f318000),
        UINT32_C(0xb95e8083), UINT32_C(0x3f800000),
        UINT32_C(0x3f7ffff9), UINT32_C(0x3efffffc),
        UINT32_C(0x3e2aabbf), UINT32_C(0x3d2aab67),
        UINT32_C(0x3c085d88), UINT32_C(0x3ab5de3b),
        UINT32_C(0x3959cfde), UINT32_C(0x37d8c471),
    };
    float c[12];
    memcpy(c, bits, sizeof(c));
    const float n = nearbyintf(fmul(x, c[0]));
    float r = ffma(-c[1], n, x);
    r = ffma(-c[2], n, r);
    float p = c[11];
    for (int i = 10; i >= 3; --i)
        p = ffma(p, r, c[i]);
    return scalbnf(p, (int)n);
}

static int carrier_bits(
    const struct asian_meta_qsort_control_plan *generic,
    const autocall_worstof_prepared_market_t *market) {
    for (uint32_t source = 0; source < AUTOCALL_WORSTOF_DONOR_VALUES;
         ++source) {
        float x = ffma(market->diffusion_a,
                       asian_genuine_fixed_block_signed_z[source],
                       market->drift_a);
        if (!same_float(market->asset_a_growth[source], exact_exp(x))) {
            fprintf(stderr, "asset-a growth mismatch source=%u got=%a ref=%a\n",
                    source, market->asset_a_growth[source], exact_exp(x));
            return -1;
        }
    }
    for (uint32_t date = 0; date < 3u; ++date) {
        const uint32_t u_dim = 2u * date;
        const uint32_t v_dim = u_dim + 1u;
        for (uint32_t path = 0; path < AUTOCALL_WORSTOF_PATHS; ++path) {
            const uint32_t us = generic_source(&generic->maps[u_dim], path);
            const uint32_t vs = generic_source(&generic->maps[v_dim], path);
            const float *u_base = asian_genuine_fixed_block_signed_z +
                generic->donor_region[u_dim] * ASIAN_META_PATHS;
            const float *v_base = asian_genuine_fixed_block_signed_z +
                generic->donor_region[v_dim] * ASIAN_META_PATHS;
            const float correlated = ffma(
                market->rho, u_base[us], fmul(market->cholesky, v_base[vs]));
            const float x = ffma(market->diffusion_b, correlated,
                                 market->drift_b);
            const float ref = exact_exp(x);
            if (!same_float(market->asset_b_growth[date][path], ref)) {
                fprintf(stderr,
                        "asset-b growth mismatch date=%u path=%u got=%a ref=%a\n",
                        date, path, market->asset_b_growth[date][path], ref);
                return -1;
            }
        }
    }
    return 0;
}

static double scalar_price(
    const struct asian_meta_qsort_control_plan *generic,
    const autocall_worstof_prepared_market_t *market,
    const autocall_worstof_prepared_leaf_context_t *leaf) {
    float lo[16] = {0};
    float hi[16] = {0};
    for (uint32_t path = 0; path < AUTOCALL_WORSTOF_PATHS; ++path) {
        float sa = leaf->spot_a;
        float sb = leaf->spot_b;
        float pv = 0.0f;
        int alive = 1;
        for (uint32_t date = 0; date < 3u; ++date) {
            const uint32_t dim = 2u * date;
            const uint32_t source = generic_source(&generic->maps[dim], path);
            const float *base = market->asset_a_growth +
                generic->donor_region[dim] * ASIAN_META_PATHS;
            sa = fmul(sa, base[source]);
            sb = fmul(sb, market->asset_b_growth[date][path]);
            if (alive && sa >= leaf->date[date].coupon_a &&
                sb >= leaf->date[date].coupon_b)
                pv = fadd(pv, leaf->date[date].discounted_coupon);
            if (alive && sa >= leaf->date[date].call_a &&
                sb >= leaf->date[date].call_b) {
                pv = fadd(pv, leaf->date[date].discounted_call);
                alive = 0;
            }
        }
        if (alive) {
            if (sa >= leaf->protection_a && sb >= leaf->protection_b) {
                pv = fadd(pv, leaf->discounted_terminal);
            } else {
                const float wa = fmul(sa, leaf->inverse_spot_a);
                const float wb = fmul(sb, leaf->inverse_spot_b);
                const float worst = wa < wb ? wa : wb;
                pv = fadd(pv, fmul(leaf->discounted_terminal, worst));
            }
        }
        float *acc = (path & 16u) == 0u ? lo : hi;
        acc[path & 15u] = fadd(acc[path & 15u], pv);
    }
    float lanes[16];
    for (uint32_t lane = 0; lane < 16u; ++lane)
        lanes[lane] = fadd(lo[lane], hi[lane]);
    float q0[4];
    float q2[4];
    for (uint32_t lane = 0; lane < 4u; ++lane) {
        q0[lane] = fadd(lanes[lane], lanes[4u + lane]);
        q2[lane] = fadd(lanes[8u + lane], lanes[12u + lane]);
        q0[lane] = fadd(q0[lane], q2[lane]);
    }
    q0[0] = fadd(q0[0], q0[2]);
    q0[1] = fadd(q0[1], q0[3]);
    q0[0] = fadd(q0[0], q0[1]);
    return (double)q0[0] * leaf->inverse_paths;
}

static double f64_growth_replay(
    const struct asian_meta_qsort_control_plan *generic,
    const autocall_worstof_prepared_market_t *market,
    const autocall_worstof_prepared_leaf_context_t *leaf) {
    double total = 0.0;
    for (uint32_t path = 0; path < AUTOCALL_WORSTOF_PATHS; ++path) {
        double sa = leaf->spot_a;
        double sb = leaf->spot_b;
        double pv = 0.0;
        int alive = 1;
        for (uint32_t date = 0; date < 3u; ++date) {
            const uint32_t dim = 2u * date;
            const uint32_t source = generic_source(&generic->maps[dim], path);
            const float *base = market->asset_a_growth +
                generic->donor_region[dim] * ASIAN_META_PATHS;
            sa *= (double)base[source];
            sb *= (double)market->asset_b_growth[date][path];
            if (alive && sa >= (double)leaf->date[date].coupon_a &&
                sb >= (double)leaf->date[date].coupon_b)
                pv += (double)leaf->date[date].discounted_coupon;
            if (alive && sa >= (double)leaf->date[date].call_a &&
                sb >= (double)leaf->date[date].call_b) {
                pv += (double)leaf->date[date].discounted_call;
                alive = 0;
            }
        }
        if (alive) {
            if (sa >= (double)leaf->protection_a &&
                sb >= (double)leaf->protection_b)
                pv += (double)leaf->discounted_terminal;
            else
                pv += (double)leaf->discounted_terminal *
                    fmin(sa * (double)leaf->inverse_spot_a,
                         sb * (double)leaf->inverse_spot_b);
        }
        total += pv;
    }
    return total * leaf->inverse_paths;
}

static void generic_routes(
    const struct asian_meta_qsort_control_plan *generic,
    const float *growth, asian_genuine_route_t routes[6]) {
    for (unsigned d = 0; d < 6u; ++d) {
        routes[d].x_base = NULL;
        routes[d].growth_base = growth +
            (size_t)generic->donor_region[d] * ASIAN_META_PATHS;
        routes[d].map = &generic->maps[d];
        routes[d].weight_bits = 0u;
        routes[d].fixing_index = d;
    }
}

static int compare_case(
    autocall_worstof_engine_t *engine,
    const struct asian_meta_qsort_control_plan *generic,
    const autocall_worstof_case_t *fixture, int check_carrier, int print_case) {
    autocall_worstof_prepared_market_t *pm = a64(sizeof(*pm));
    autocall_worstof_inline_market_t *im = a64(sizeof(*im));
    autocall_worstof_prepared_request_t *pr = a64(sizeof(*pr));
    autocall_worstof_inline_request_t *ir = a64(sizeof(*ir));
    unsigned char *pm_copy = NULL;
    unsigned char *im_copy = NULL;
    unsigned char *pr_copy = NULL;
    unsigned char *ir_copy = NULL;
    int result = -1;
    if (pm == NULL || im == NULL || pr == NULL || ir == NULL)
        goto done;
    if (autocall_worstof_prepared_market_prepare(
            engine, &fixture->market, pm) != 0 ||
        autocall_worstof_inline_market_prepare(
            engine, &fixture->market, im) != 0 ||
        autocall_worstof_prepared_request_prepare(
            engine, pm, &fixture->contract, pr) != 0 ||
        autocall_worstof_inline_request_prepare(
            engine, im, &fixture->contract, ir) != 0)
        goto done;
    if (check_carrier && carrier_bits(generic, pm) != 0)
        goto done;
    pm_copy = malloc(sizeof(*pm));
    im_copy = malloc(sizeof(*im));
    pr_copy = malloc(sizeof(*pr));
    ir_copy = malloc(sizeof(*ir));
    if (pm_copy == NULL || im_copy == NULL || pr_copy == NULL ||
        ir_copy == NULL)
        goto done;
    memcpy(pm_copy, pm, sizeof(*pm));
    memcpy(im_copy, im, sizeof(*im));
    memcpy(pr_copy, pr, sizeof(*pr));
    memcpy(ir_copy, ir, sizeof(*ir));
    autocall_worstof_output_t po;
    autocall_worstof_output_t io;
    autocall_worstof_output_t repeated;
    if (autocall_worstof_prepared_price(pr, &po) != 0 ||
        autocall_worstof_inline_price(ir, &io) != 0 ||
        autocall_worstof_inline_price(ir, &repeated) != 0)
        goto done;
    const double scalar = scalar_price(generic, pm, &pr->leaf);
    asian_genuine_route_t gr[6] __attribute__((aligned(64)));
    generic_routes(generic, pm->asset_a_growth, gr);
    autocall_worstof_prepared_leaf_context_t generic_context = pr->leaf;
    generic_context.asset_a_d1 = gr[0].growth_base;
    generic_context.asset_a_d3 =
        (const asian_meta_affine_route_t *)(const void *)(gr + 2);
    generic_context.asset_a_d5 =
        (const asian_meta_affine_route_t *)(const void *)(gr + 4);
    const double vector_generic =
        autocall_worstof_generic_vector_price_test(&generic_context);
    const double replay = f64_growth_replay(generic, pm, &pr->leaf);
    if (!same_double(po.price, io.price) ||
        !same_double(po.price, repeated.price) ||
        !same_double(po.price, scalar) ||
        !same_double(po.price, vector_generic) ||
        memcmp(pm, pm_copy, sizeof(*pm)) != 0 ||
        memcmp(im, im_copy, sizeof(*im)) != 0 ||
        memcmp(pr, pr_copy, sizeof(*pr)) != 0 ||
        memcmp(ir, ir_copy, sizeof(*ir)) != 0) {
        fprintf(stderr,
                "identity case=%s prepared=%a inline=%a scalar=%a generic=%a repeat=%a\n",
                fixture->name, po.price, io.price, scalar, vector_generic,
                repeated.price);
        goto done;
    }
    const double replay_bp = fabs(po.price - replay) /
                             fixture->contract.notional * 10000.0;
    if (print_case)
        printf("WORSTOF_CASE name=%s panel=%s price=%.17g "
               "f64_growth_replay=%.17g f32_replay_bp=%.12g identity=PASS\n",
               fixture->name, fixture->stress ? "STRESS" : "CORE", po.price,
               replay, replay_bp);
    result = 0;
done:
    free(ir_copy);
    free(pr_copy);
    free(im_copy);
    free(pm_copy);
    free(ir);
    free(pr);
    free(im);
    free(pm);
    return result;
}

static int lifecycle_counts(autocall_worstof_engine_t *engine,
                            const autocall_worstof_case_t *fixture) {
    autocall_worstof_prepared_market_t *pm = a64(sizeof(*pm));
    autocall_worstof_inline_market_t *im = a64(sizeof(*im));
    autocall_worstof_prepared_request_t *pr = a64(sizeof(*pr));
    autocall_worstof_inline_request_t *ir = a64(sizeof(*ir));
    autocall_worstof_output_t out;
    if (pm == NULL || im == NULL || pr == NULL || ir == NULL)
        return -1;
#define EXPECT_COUNTS(P, I) do { \
    if (autocall_worstof_prepared_leaf_invocations != (P) || \
        autocall_worstof_inline_leaf_invocations != (I)) return -1; \
} while (0)
    autocall_worstof_prepared_leaf_invocations = 0;
    autocall_worstof_inline_leaf_invocations = 0;
    autocall_worstof_engine_t temporary __attribute__((aligned(64)));
    if (autocall_worstof_engine_create(&temporary) != 0)
        return -1;
    autocall_worstof_engine_destroy(&temporary);
    EXPECT_COUNTS(0, 0);
    if (autocall_worstof_prepared_market_prepare(
            engine, &fixture->market, pm) != 0 ||
        autocall_worstof_inline_market_prepare(
            engine, &fixture->market, im) != 0)
        return -1;
    EXPECT_COUNTS(0, 0);
    if (autocall_worstof_prepared_request_prepare(
            engine, pm, &fixture->contract, pr) != 0 ||
        autocall_worstof_inline_request_prepare(
            engine, im, &fixture->contract, ir) != 0)
        return -1;
    EXPECT_COUNTS(0, 0);
    if (autocall_worstof_prepared_price(pr, &out) != 0)
        return -1;
    EXPECT_COUNTS(1, 0);
    if (autocall_worstof_inline_price(ir, &out) != 0)
        return -1;
    EXPECT_COUNTS(1, 1);
    autocall_worstof_prepared_leaf_invocations = 0;
    autocall_worstof_inline_leaf_invocations = 0;
    if (autocall_worstof_prepared_reuse_total(
            engine, pm, &fixture->contract, &out) != 0)
        return -1;
    EXPECT_COUNTS(1, 0);
    autocall_worstof_prepared_leaf_invocations = 0;
    if (autocall_worstof_prepared_fresh_total(
            engine, &fixture->market, &fixture->contract, &out) != 0)
        return -1;
    EXPECT_COUNTS(1, 0);
    autocall_worstof_prepared_leaf_invocations = 0;
    if (autocall_worstof_inline_reuse_total(
            engine, im, &fixture->contract, &out) != 0)
        return -1;
    EXPECT_COUNTS(0, 1);
    autocall_worstof_inline_leaf_invocations = 0;
    if (autocall_worstof_inline_fresh_total(
            engine, &fixture->market, &fixture->contract, &out) != 0)
        return -1;
    EXPECT_COUNTS(0, 1);
#undef EXPECT_COUNTS
    free(ir);
    free(pr);
    free(im);
    free(pm);
    return 0;
}

typedef struct {
    unsigned char *allocation;
    void *object;
    size_t bytes;
} guarded_t;

static guarded_t guarded_create(size_t bytes) {
    guarded_t out = {0};
    out.allocation = a64(bytes + 128u);
    if (out.allocation != NULL) {
        memset(out.allocation, 0xa5, bytes + 128u);
        out.object = out.allocation + 64u;
        out.bytes = bytes;
    }
    return out;
}

static int guarded_ok(const guarded_t *guard) {
    for (unsigned i = 0; i < 64u; ++i)
        if (guard->allocation[i] != 0xa5 ||
            guard->allocation[64u+guard->bytes+i] != 0xa5)
            return 0;
    return 1;
}

static int guard_zones(autocall_worstof_engine_t *engine,
                       const autocall_worstof_case_t *fixture) {
    guarded_t pmg = guarded_create(sizeof(autocall_worstof_prepared_market_t));
    guarded_t img = guarded_create(sizeof(autocall_worstof_inline_market_t));
    guarded_t prg = guarded_create(sizeof(autocall_worstof_prepared_request_t));
    guarded_t irg = guarded_create(sizeof(autocall_worstof_inline_request_t));
    if (!pmg.object || !img.object || !prg.object || !irg.object)
        return -1;
    autocall_worstof_output_t a, b;
    int ok = autocall_worstof_prepared_market_prepare(
            engine, &fixture->market, pmg.object) == 0 &&
        autocall_worstof_inline_market_prepare(
            engine, &fixture->market, img.object) == 0 &&
        autocall_worstof_prepared_request_prepare(
            engine, pmg.object, &fixture->contract, prg.object) == 0 &&
        autocall_worstof_inline_request_prepare(
            engine, img.object, &fixture->contract, irg.object) == 0 &&
        autocall_worstof_prepared_price(prg.object, &a) == 0 &&
        autocall_worstof_inline_price(irg.object, &b) == 0 &&
        same_double(a.price, b.price) && guarded_ok(&pmg) &&
        guarded_ok(&img) && guarded_ok(&prg) && guarded_ok(&irg);
    free(irg.allocation);
    free(prg.allocation);
    free(img.allocation);
    free(pmg.allocation);
    return ok ? 0 : -1;
}

static int edge_matrix(
    autocall_worstof_engine_t *engine,
    const struct asian_meta_qsort_control_plan *generic,
    const autocall_worstof_case_t *base) {
    autocall_worstof_case_t edge = *base;
    edge.name = "EDGE";
    edge.market.rho = nextafter(1.0, 0.0);
    if (compare_case(engine, generic, &edge, 0, 0) != 0)
        return -1;
    edge.market.rho = nextafter(-1.0, 0.0);
    if (compare_case(engine, generic, &edge, 0, 0) != 0)
        return -1;

    edge = *base;
    edge.name = "ALL_CALL_D1";
    for (unsigned d = 0; d < 3; ++d) {
        edge.contract.call_barrier[d] = FLT_MIN;
        edge.contract.coupon_barrier[d] = FLT_MIN;
    }
    if (compare_case(engine, generic, &edge, 0, 0) != 0)
        return -1;

    edge = *base;
    edge.name = "NO_CALL_PROTECTED";
    for (unsigned d = 0; d < 3; ++d) {
        edge.contract.call_barrier[d] = 1.0e20;
        edge.contract.coupon_barrier[d] = 1.0e20;
        edge.contract.coupon_cashflow[d] = 0.0;
    }
    edge.contract.protection_barrier = FLT_MIN;
    if (compare_case(engine, generic, &edge, 0, 0) != 0)
        return -1;
    edge.name = "NO_CALL_DOWNSIDE";
    edge.contract.protection_barrier = 1.0e20;
    if (compare_case(engine, generic, &edge, 0, 0) != 0)
        return -1;

    autocall_worstof_prepared_market_t *market = a64(sizeof(*market));
    if (market == NULL)
        return -1;
    edge = *base;
    edge.name = "EXACT_EQUALITY";
    edge.contract.spot_a = 1.0;
    edge.contract.spot_b = 1.0;
    if (autocall_worstof_prepared_market_prepare(
            engine, &edge.market, market) != 0) {
        free(market);
        return -1;
    }
    const uint32_t a_source = generic_source(&generic->maps[0], 0u);
    const float *a_base = market->asset_a_growth +
        generic->donor_region[0] * ASIAN_META_PATHS;
    const float ratio = fminf(a_base[a_source], market->asset_b_growth[0][0]);
    edge.contract.coupon_barrier[0] = ratio;
    edge.contract.call_barrier[0] = ratio;
    free(market);
    if (compare_case(engine, generic, &edge, 0, 0) != 0)
        return -1;
    return 0;
}

static int invalid_inputs(autocall_worstof_engine_t *engine,
                          const autocall_worstof_case_t *fixture) {
    autocall_worstof_inline_market_t market;
    autocall_worstof_market_input_t m = fixture->market;
    m.rho = 1.0;
    if (autocall_worstof_inline_market_prepare(engine, &m, &market) !=
        AUTOCALL_WORSTOF_INVALID)
        return -1;
    m = fixture->market;
    m.sigma_b = 0.0;
    if (autocall_worstof_inline_market_prepare(engine, &m, &market) !=
        AUTOCALL_WORSTOF_INVALID)
        return -1;
    if (autocall_worstof_inline_market_prepare(
            engine, &fixture->market, &market) != 0)
        return -1;
    autocall_worstof_contract_input_t c = fixture->contract;
    autocall_worstof_inline_request_t request;
    c.coupon_cashflow[0] = -1.0;
    if (autocall_worstof_inline_request_prepare(
            engine, &market, &c, &request) != AUTOCALL_WORSTOF_INVALID)
        return -1;
    c = fixture->contract;
    c.call_barrier[1] = NAN;
    if (autocall_worstof_inline_request_prepare(
            engine, &market, &c, &request) != AUTOCALL_WORSTOF_INVALID)
        return -1;
    return 0;
}

int main(void) {
    autocall_worstof_engine_t engine __attribute__((aligned(64)));
    asian_meta_qsort_control_plan_t *opaque = NULL;
    if (autocall_worstof_engine_create(&engine) != 0 ||
        asian_meta_qsort_control_plan_create(&opaque) != 0) {
        fprintf(stderr, "engine/oracle construction failed\n");
        return 1;
    }
    const struct asian_meta_qsort_control_plan *generic =
        (const struct asian_meta_qsort_control_plan *)(const void *)opaque;
    if (route_integrity(engine.affine_plan, generic) != 0) {
        fprintf(stderr, "D1-D6 route integrity failed\n");
        return 1;
    }
    const autocall_worstof_case_t *cases = autocall_worstof_cases();
    unsigned char *plan_copy = malloc(sizeof(*engine.affine_plan));
    if (plan_copy == NULL)
        return 1;
    memcpy(plan_copy, engine.affine_plan, sizeof(*engine.affine_plan));
    for (unsigned i = 0; i < AUTOCALL_WORSTOF_CASES; ++i)
        if (compare_case(&engine, generic, &cases[i], i == 0u, 1) != 0)
            return 1;
    if (lifecycle_counts(&engine, &cases[0]) != 0 ||
        invalid_inputs(&engine, &cases[0]) != 0 ||
        guard_zones(&engine, &cases[0]) != 0 ||
        edge_matrix(&engine, generic, &cases[0]) != 0 ||
        memcmp(plan_copy, engine.affine_plan,
               sizeof(*engine.affine_plan)) != 0) {
        fprintf(stderr, "lifecycle/input gate failed\n");
        return 1;
    }
    free(plan_copy);
    printf("route_integrity D1-D6 paths=4096 both_regions=PASS\n");
    printf("carrier_identity correlated_normal_growth=PASS\n");
    printf("lifecycle_leaf_counts engine=0 market=0 request=0 prepared=1 reused=1 fresh=1\n");
    printf("edge_matrix rho_near_limits=PASS all_call=PASS no_call=PASS "
           "protected=PASS downside=PASS equality=PASS guards=PASS\n");
    printf("inactive_call_barrier marginal_crossing_bound=%.12g "
           "limit=1e-14 decision=PASS\n",
           0.5*erfc(8.5/sqrt(2.0)));
    printf("bounded_sde_correctness PASS\n");
    asian_meta_qsort_control_plan_destroy(opaque);
    autocall_worstof_engine_destroy(&engine);
    return 0;
}
