#define _POSIX_C_SOURCE 200112L

#include "private/autocall_single_asset_three_date_raw_diag.h"
#include "private/asian_genuine_permute.h"
#include "tests/autocall_single_asset_three_date_cases.h"

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
extern uint64_t autocall_3date_test_leaf_invocations;

static void *a64(size_t bytes)
{
    void *pointer = NULL;
    return posix_memalign(&pointer, 64u, bytes) == 0 ? pointer : NULL;
}

static int bit_equal_double(double a, double b)
{
    uint64_t aa, bb;
    memcpy(&aa, &a, 8u); memcpy(&bb, &b, 8u);
    return aa == bb;
}

static uint32_t sobol_word(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray = index ^ (index >> 1), word = 0u;
    for (uint32_t bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u) word ^= directions[bit];
    return word;
}

static void direction_row(uint32_t dimension, uint32_t out[32])
{
    const unsigned char *record = asian_arithmetic_joe_kuo_256_records +
        (size_t)dimension * JOE_KUO_RECORD_BYTES;
    uint32_t count;
    memcpy(&count, record, 4u);
    if (count != 32u) abort();
    memcpy(out, record + 4u, 128u);
}

static uint32_t generic_source(const fragment_map_t *map, uint32_t path)
{
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    const uint32_t pattern = map->select[packet][2u + half];
    return (uint32_t)map->select[packet][half] * 16u +
           map->patterns[pattern][lane];
}

static uint32_t affine_source(const asian_meta_dim_affine_ctx_t *map,
                              uint32_t path)
{
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    uint32_t control = map->base_control[lane] ^ map->sel2[packet][1];
    if (half != 0u) control ^= map->half_delta[lane];
    return ((uint32_t)(map->sel2[packet][0] ^ half) << 4) | control;
}

static int route_integrity(const asian_meta_affine_plan_t *affine,
    const struct asian_meta_qsort_control_plan *generic)
{
    uint32_t d1[32], target[32];
    direction_row(0u, d1);
    unsigned regions = 0u;
    for (uint32_t dimension = 0; dimension < 3u; ++dimension) {
        direction_row(dimension, target);
        unsigned char seen[ASIAN_META_PATHS] = {0};
        if (affine->donor_region[dimension] !=
            generic->donor_region[dimension]) return -1;
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

static autocall_3date_request_input_t case_input(
    const autocall_frozen_case_t *fixture)
{
    autocall_3date_request_input_t out;
    memset(&out, 0, sizeof(out));
    out.s0 = fixture->s0; out.rate = fixture->rate;
    out.dividend_yield = fixture->dividend; out.sigma = fixture->sigma;
    out.maturity = fixture->maturity; out.notional = fixture->notional;
    out.protection_barrier = fixture->protection;
    out.terminal_payment_time = fixture->maturity *
        (1.0 + fixture->payment_lag_fraction);
    for (uint32_t date = 0; date < 3u; ++date) {
        out.call_barrier[date] = fixture->call_barrier[date];
        out.coupon_barrier[date] = fixture->coupon_barrier[date];
        out.coupon_cashflow[date] = fixture->coupon[date];
        out.call_redemption[date] = fixture->redemption[date];
        const double observation = fixture->maturity * (date + 1.0) / 3.0;
        out.coupon_payment_time[date] = observation +
            fixture->payment_lag_fraction * fixture->maturity;
        out.call_payment_time[date] = observation +
            fixture->payment_lag_fraction * fixture->maturity;
    }
    return out;
}

static autocall_3date_market_input_t market_input(
    const autocall_3date_request_input_t *input)
{
    const autocall_3date_market_input_t out = {
        input->rate, input->dividend_yield, input->sigma, input->maturity};
    return out;
}

static void generic_routes(const struct asian_meta_qsort_control_plan *plan,
    const autocall_3date_carrier_t *carrier, asian_genuine_route_t routes[3])
{
    for (uint32_t dimension = 0; dimension < 3u; ++dimension) {
        routes[dimension].x_base = NULL;
        routes[dimension].growth_base = carrier->growth +
            (size_t)plan->donor_region[dimension] * ASIAN_META_PATHS;
        routes[dimension].map = &plan->maps[dimension];
        routes[dimension].weight_bits = 0u;
        routes[dimension].fixing_index = dimension;
    }
}

static float fmul(float a, float b) { volatile float x = a * b; return x; }
static float fadd(float a, float b) { volatile float x = a + b; return x; }

static double scalar_price(const asian_genuine_route_t routes[3],
                           const autocall_3date_leaf_context_t *context)
{
    float lo[16] = {0}, hi[16] = {0};
    for (uint32_t path = 0; path < AUTOCALL_3DATE_PATHS; ++path) {
        float spot = context->initial_spot, pv = 0.0f;
        int alive = 1;
        for (uint32_t date = 0; date < 3u; ++date) {
            const uint32_t source = generic_source(routes[date].map, path);
            spot = fmul(spot, routes[date].growth_base[source]);
            if (alive && spot >= context->date[date].coupon_barrier)
                pv = fadd(pv, context->date[date].discounted_coupon);
            if (alive && spot >= context->date[date].call_barrier) {
                pv = fadd(pv,
                          context->date[date].discounted_call_redemption);
                alive = 0;
            }
        }
        if (alive) {
            if (spot >= context->protection_barrier) {
                pv = fadd(pv, context->discounted_terminal_notional);
            } else {
                float downside = fmul(spot, context->inverse_initial_spot);
                downside = fmul(downside,
                                context->discounted_terminal_notional);
                pv = fadd(pv, downside);
            }
        }
        float *acc = (path & 16u) == 0u ? lo : hi;
        acc[path & 15u] = fadd(acc[path & 15u], pv);
    }
    float lanes[16];
    for (uint32_t lane = 0; lane < 16u; ++lane)
        lanes[lane] = fadd(lo[lane], hi[lane]);
    float q0[4], q2[4];
    for (uint32_t lane = 0; lane < 4u; ++lane) {
        q0[lane] = fadd(lanes[lane], lanes[4u + lane]);
        q2[lane] = fadd(lanes[8u + lane], lanes[12u + lane]);
        q0[lane] = fadd(q0[lane], q2[lane]);
    }
    q0[0] = fadd(q0[0], q0[2]);
    q0[1] = fadd(q0[1], q0[3]);
    q0[0] = fadd(q0[0], q0[1]);
    return (double)q0[0] * context->inverse_paths;
}

static double f64_growth_replay(const asian_genuine_route_t routes[3],
                                const autocall_3date_leaf_context_t *context)
{
    double total = 0.0;
    for (uint32_t path = 0; path < AUTOCALL_3DATE_PATHS; ++path) {
        double spot = context->initial_spot, pv = 0.0;
        int alive = 1;
        for (uint32_t date = 0; date < 3u; ++date) {
            const uint32_t source = generic_source(routes[date].map, path);
            spot *= (double)routes[date].growth_base[source];
            if (alive && spot >= (double)context->date[date].coupon_barrier)
                pv += (double)context->date[date].discounted_coupon;
            if (alive && spot >= (double)context->date[date].call_barrier) {
                pv += (double)context->date[date].discounted_call_redemption;
                alive = 0;
            }
        }
        if (alive)
            pv += spot >= (double)context->protection_barrier ?
                (double)context->discounted_terminal_notional :
                (double)context->discounted_terminal_notional * spot *
                    (double)context->inverse_initial_spot;
        total += pv;
    }
    return total / AUTOCALL_3DATE_PATHS;
}

static int compare_case(autocall_3date_engine_t *engine,
    const struct asian_meta_qsort_control_plan *generic,
    const autocall_3date_request_input_t *input, int print_case,
    const char *name)
{
    autocall_3date_carrier_t *carrier = a64(sizeof(*carrier));
    autocall_3date_request_t *request = a64(sizeof(*request));
    asian_genuine_route_t generic_route[3] __attribute__((aligned(64)));
    autocall_3date_output_t output, repeated;
    int result = -1;
    if (carrier == NULL || request == NULL) goto done;
    const autocall_3date_market_input_t market = market_input(input);
    if (autocall_3date_market_prepare(engine, &market, carrier) != 0 ||
        autocall_3date_request_prepare(engine, carrier, input, request) != 0)
        goto done;
    generic_routes(generic, carrier, generic_route);
    unsigned char *carrier_copy = malloc(sizeof(*carrier));
    unsigned char *request_copy = malloc(sizeof(*request));
    unsigned char *plan_copy = malloc(sizeof(*engine->affine_plan));
    if (carrier_copy == NULL || request_copy == NULL || plan_copy == NULL) {
        free(plan_copy); free(request_copy); free(carrier_copy); goto done;
    }
    memcpy(carrier_copy, carrier, sizeof(*carrier));
    memcpy(request_copy, request, sizeof(*request));
    memcpy(plan_copy, engine->affine_plan, sizeof(*engine->affine_plan));
    if (autocall_3date_prepared_price(request, &output) != 0 ||
        autocall_3date_prepared_price(request, &repeated) != 0 ||
        memcmp(&output, &repeated, sizeof(output)) != 0 ||
        memcmp(carrier_copy, carrier, sizeof(*carrier)) != 0 ||
        memcmp(request_copy, request, sizeof(*request)) != 0 ||
        memcmp(plan_copy, engine->affine_plan,
               sizeof(*engine->affine_plan)) != 0) {
        free(plan_copy); free(request_copy); free(carrier_copy);
        goto done;
    }
    const double scalar = scalar_price(generic_route, &request->context);
    autocall_3date_leaf_context_t generic_context = request->context;
    generic_context.d1_growth = generic_route[0].growth_base;
    generic_context.routes_d2 =
        (const asian_meta_affine_route_t *)(const void *)(generic_route + 1);
    const double vector_generic =
        autocall_3date_generic_price_leaf_test(&generic_context);
    if (!bit_equal_double(output.price, scalar) ||
        !bit_equal_double(output.price, vector_generic)) {
        fprintf(stderr, "identity name=%s affine=%a scalar=%a generic=%a\n",
                name, output.price, scalar, vector_generic);
        free(plan_copy); free(request_copy); free(carrier_copy); goto done;
    }
    if (print_case) {
        const double replay = f64_growth_replay(generic_route,
                                                &request->context);
        const double bp = fabs(output.price - replay) /
                          input->notional * 10000.0;
        printf("ENGINE_CASE name=%s affine=%.17g f64_growth_replay=%.17g "
               "f32_replay_bp=%.12g identity=PASS\n",
               name, output.price, replay, bp);
    }
    free(plan_copy); free(request_copy); free(carrier_copy);
    result = 0;
done:
    free(request); free(carrier);
    return result;
}

static int scenario_matrix(autocall_3date_engine_t *engine,
    const struct asian_meta_qsort_control_plan *generic)
{
    static const double markets[4][4] = {
        {0.01,0.02,0.08,1.00}, {0.03,0.01,0.20,1.00},
        {0.01,0.00,0.55,1.50}, {-0.01,0.03,0.30,0.75}};
    for (uint32_t market = 0; market < 4u; ++market) {
        autocall_3date_request_input_t base =
            case_input(&autocall_frozen_cases[1]);
        base.rate = markets[market][0]; base.dividend_yield = markets[market][1];
        base.sigma = markets[market][2]; base.maturity = markets[market][3];
        base.terminal_payment_time = base.maturity;
        for (uint32_t d = 0; d < 3u; ++d) {
            base.coupon_payment_time[d] = base.maturity * (d + 1.0) / 3.0;
            base.call_payment_time[d] = base.coupon_payment_time[d];
        }
        for (uint32_t scenario = 0; scenario < 10u; ++scenario) {
            autocall_3date_request_input_t input = base;
            if (scenario == 0u)
                for (uint32_t d=0; d<3u; ++d) input.call_barrier[d]=FLT_MIN;
            if (scenario == 1u)
                for (uint32_t d=0; d<3u; ++d) input.call_barrier[d]=FLT_MAX;
            if (scenario == 2u) {
                input.call_barrier[0]=FLT_MAX; input.call_barrier[1]=100;
            }
            if (scenario == 3u)
                for (uint32_t d=0; d<3u; ++d)
                    input.coupon_barrier[d]=input.call_barrier[d];
            if (scenario == 4u)
                for (uint32_t d=0; d<3u; ++d) input.coupon_cashflow[d]=0;
            if (scenario == 5u) input.protection_barrier=FLT_MIN;
            if (scenario == 6u) input.protection_barrier=FLT_MAX;
            if (scenario == 7u) {
                input.coupon_barrier[0]=200; input.call_barrier[0]=210;
            }
            if (scenario == 8u) {
                input.coupon_barrier[0]=90; input.call_barrier[0]=200;
            }
            if (scenario == 9u) {
                input.call_redemption[0]=99; input.call_redemption[1]=101;
                input.call_redemption[2]=103;
            }
            if (compare_case(engine, generic, &input, 0, "scenario") != 0)
                return -1;
        }
    }
    return 0;
}

static int equality_and_nan(autocall_3date_engine_t *engine,
    const struct asian_meta_qsort_control_plan *generic)
{
    autocall_3date_request_input_t input =
        case_input(&autocall_frozen_cases[1]);
    autocall_3date_market_input_t market = market_input(&input);
    autocall_3date_carrier_t *carrier = a64(sizeof(*carrier));
    if (carrier == NULL || autocall_3date_market_prepare(engine, &market,
                                                         carrier) != 0)
        return -1;
    asian_genuine_route_t routes[3] __attribute__((aligned(64)));
    generic_routes(generic, carrier, routes);
    float spot1 = fmul((float)input.s0,
        routes[0].growth_base[generic_source(routes[0].map, 0u)]);
    float spot2 = fmul(spot1,
        routes[1].growth_base[generic_source(routes[1].map, 0u)]);
    float spot3 = fmul(spot2,
        routes[2].growth_base[generic_source(routes[2].map, 0u)]);
    input.coupon_barrier[0] = spot1;
    input.call_barrier[0] = spot1;
    if (compare_case(engine, generic, &input, 0, "equality_d1") != 0) {
        free(carrier); return -1;
    }
    input = case_input(&autocall_frozen_cases[1]);
    input.call_barrier[0] = FLT_MAX;
    input.coupon_barrier[1] = spot2;
    input.call_barrier[1] = spot2;
    if (compare_case(engine, generic, &input, 0, "equality_d2") != 0) {
        free(carrier); return -1;
    }
    input = case_input(&autocall_frozen_cases[1]);
    for (uint32_t d=0;d<3u;++d) input.call_barrier[d]=FLT_MAX;
    input.coupon_barrier[2] = spot3;
    input.protection_barrier = spot3;
    if (compare_case(engine, generic, &input, 0,
                     "equality_d3_protection") != 0) {
        free(carrier); return -1;
    }

    /* Diagnostic-only NaN carrier injection: ordered comparisons do not
       qualify and the surviving downside path propagates NaN identically. */
    input = case_input(&autocall_frozen_cases[1]);
    for (uint32_t d=0;d<3u;++d) input.call_barrier[d]=FLT_MAX;
    uint32_t qnan_bits=UINT32_C(0x7fc00000);
    memcpy(&carrier->growth[0],&qnan_bits,sizeof(qnan_bits));
    autocall_3date_request_t affine_request;
    if (autocall_3date_request_prepare(engine,carrier,&input,&affine_request)!=0) {
        free(carrier); return -1;
    }
    autocall_3date_output_t nan_output;
    generic_routes(generic,carrier,routes);
    autocall_3date_leaf_context_t generic_context=affine_request.context;
    generic_context.d1_growth=routes[0].growth_base;
    generic_context.routes_d2=
        (const asian_meta_affine_route_t *)(const void *)(routes+1);
    const double scalar_nan=scalar_price(routes,&affine_request.context);
    const double generic_nan=autocall_3date_generic_price_leaf_test(
        &generic_context);
    if (autocall_3date_prepared_price(&affine_request,&nan_output)!=0 ||
        !isnan(nan_output.price) || !isnan(scalar_nan) || !isnan(generic_nan)) {
        free(carrier); return -1;
    }

    if (autocall_3date_market_prepare(engine,&market,carrier)!=0) {
        free(carrier); return -1;
    }
    input.s0 = NAN;
    autocall_3date_request_t request;
    if (autocall_3date_request_prepare(engine, carrier, &input, &request) !=
        AUTOCALL_3DATE_INVALID) { free(carrier); return -1; }
    free(carrier);
    return 0;
}

static int lifecycle_counts(autocall_3date_engine_t *engine)
{
    autocall_3date_request_input_t input =
        case_input(&autocall_frozen_cases[1]);
    autocall_3date_market_input_t market = market_input(&input);
    autocall_3date_carrier_t *carrier = a64(sizeof(*carrier));
    autocall_3date_request_t *request = a64(sizeof(*request));
    autocall_3date_output_t output;
    if (carrier == NULL || request == NULL) return -1;
    autocall_3date_test_leaf_invocations = 0;
    if (autocall_3date_market_prepare(engine, &market, carrier) != 0 ||
        autocall_3date_test_leaf_invocations != 0) return -1;
    if (autocall_3date_request_prepare(engine, carrier, &input, request) != 0 ||
        autocall_3date_test_leaf_invocations != 0) return -1;
    if (autocall_3date_prepared_price(request, &output) != 0 ||
        autocall_3date_test_leaf_invocations != 1) return -1;
    autocall_3date_test_leaf_invocations = 0;
    if (autocall_3date_reuse_total(engine, carrier, &input, &output) != 0 ||
        autocall_3date_test_leaf_invocations != 1) return -1;
    autocall_3date_test_leaf_invocations = 0;
    if (autocall_3date_fresh_total(engine, &input, &output) != 0 ||
        autocall_3date_test_leaf_invocations != 1) return -1;
    free(request); free(carrier);
    return 0;
}

static int invalid_inputs(autocall_3date_engine_t *engine)
{
    autocall_3date_request_input_t base=
        case_input(&autocall_frozen_cases[1]);
    autocall_3date_market_input_t market=market_input(&base);
    autocall_3date_carrier_t *carrier=a64(sizeof(*carrier));
    autocall_3date_request_t *request=a64(sizeof(*request));
    if (!carrier || !request ||
        autocall_3date_market_prepare(engine,&market,carrier)!=0) return -1;
#define REJECT(field, value) do { \
    autocall_3date_request_input_t bad=base; bad.field=(value); \
    if (autocall_3date_request_prepare(engine,carrier,&bad,request)==0) \
        return -1; \
} while (0)
    REJECT(s0,0.0);
    REJECT(notional,0.0);
    REJECT(protection_barrier,0.0);
    REJECT(coupon_cashflow[1],-1.0);
    REJECT(call_redemption[2],0.0);
    REJECT(call_barrier[0],NAN);
    REJECT(coupon_payment_time[1],0.0);
    REJECT(terminal_payment_time,base.maturity-0.01);
#undef REJECT
    autocall_3date_market_input_t bad_market=market;
    bad_market.sigma=0.0;
    if (autocall_3date_market_prepare(engine,&bad_market,carrier)==0)
        return -1;
    free(request); free(carrier); return 0;
}

static int guard_zones(autocall_3date_engine_t *engine)
{
    enum { GUARD = 64 };
    unsigned char *carrier_box = a64(sizeof(autocall_3date_carrier_t)+2*GUARD);
    unsigned char *request_box = a64(sizeof(autocall_3date_request_t)+2*GUARD);
    if (carrier_box == NULL || request_box == NULL) return -1;
    memset(carrier_box, 0xa5, sizeof(autocall_3date_carrier_t)+2*GUARD);
    memset(request_box, 0x5a, sizeof(autocall_3date_request_t)+2*GUARD);
    autocall_3date_carrier_t *carrier =
        (autocall_3date_carrier_t *)(void *)(carrier_box+GUARD);
    autocall_3date_request_t *request =
        (autocall_3date_request_t *)(void *)(request_box+GUARD);
    autocall_3date_request_input_t input =
        case_input(&autocall_frozen_cases[1]);
    autocall_3date_market_input_t market = market_input(&input);
    int ok = autocall_3date_market_prepare(engine,&market,carrier)==0 &&
        autocall_3date_request_prepare(engine,carrier,&input,request)==0;
    for (uint32_t i=0;i<GUARD;++i)
        ok = ok && carrier_box[i]==0xa5 &&
            carrier_box[GUARD+sizeof(*carrier)+i]==0xa5 &&
            request_box[i]==0x5a &&
            request_box[GUARD+sizeof(*request)+i]==0x5a;
    free(request_box); free(carrier_box);
    return ok ? 0 : -1;
}

int main(void)
{
    autocall_3date_engine_t engine __attribute__((aligned(64)));
    autocall_3date_test_leaf_invocations = 0;
    if (autocall_3date_engine_create(&engine) != 0 ||
        autocall_3date_test_leaf_invocations != 0) {
        fprintf(stderr, "engine_create failed\n"); return 1;
    }
    asian_meta_qsort_control_plan_t *opaque = NULL;
    if (asian_meta_qsort_control_plan_create(&opaque) != 0) return 1;
    const struct asian_meta_qsort_control_plan *generic =
        (const struct asian_meta_qsort_control_plan *)opaque;
    if (generic->magic != GENERIC_PLAN_MAGIC ||
        route_integrity(engine.affine_plan, generic) != 0) {
        fprintf(stderr, "route_integrity failed\n"); return 1;
    }
    for (uint32_t i=0;i<AUTOCALL_FROZEN_CASE_COUNT;++i) {
        autocall_3date_request_input_t input =
            case_input(&autocall_frozen_cases[i]);
        if (compare_case(&engine,generic,&input,1,
                         autocall_frozen_cases[i].name) != 0) {
            fprintf(stderr,"case failed %s\n",autocall_frozen_cases[i].name);
            return 1;
        }
    }
    if (scenario_matrix(&engine,generic)!=0 ||
        equality_and_nan(&engine,generic)!=0 ||
        lifecycle_counts(&engine)!=0 || invalid_inputs(&engine)!=0 ||
        guard_zones(&engine)!=0) {
        fprintf(stderr,"correctness matrix failed\n"); return 1;
    }
    printf("bounded_correctness PASS routes_d1_d3=PASS paths=4096 "
           "both_donor_regions=PASS scalar_binary32=PASS generic_vector=PASS "
           "affine=PASS lifecycle_counts=0,0,0,1,1,1 request_bytes=%zu\n",
           sizeof(autocall_3date_request_t));
    asian_meta_qsort_control_plan_destroy(opaque);
    autocall_3date_engine_destroy(&engine);
    return 0;
}
