#define _GNU_SOURCE
#include "private/asian_commercial_full_risk_lifecycle_diag.h"
#include "private/asian_genuine_multistrike_full_risk_hybrid_dispatch_diag.h"

#include <cpuid.h>
#include <immintrin.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    WARMUPS = 16,
    SAMPLES = 101,
    PLAN_SAMPLES = 5,
    WORKLOADS = 3,
    LIFECYCLES = 3,
    CACHE_MODES = 2,
};

enum workload_kind {
    WORK_PRICE = 0,
    WORK_PRICE_DELTA = 1,
    WORK_FULL_RISK = 2,
};

enum lifecycle_kind {
    LIFE_PREPARED = 0,
    LIFE_REUSE = 1,
    LIFE_FRESH = 2,
};

enum cache_kind {
    CACHE_WARM = 0,
    CACHE_PRESSURE = 1,
};

typedef struct {
    double rate, dividend, sigma, maturity;
} market_t;

static const market_t frozen_markets[] = {
    {-0.02, 0.01, 0.05, 0.25},
    { 0.00, 0.00, 0.40, 0.25},
    { 0.03, 0.00, 0.20, 1.00},
    { 0.03, 0.01, 0.05, 5.00},
};

static const market_t timed_markets[] = {
    { 0.03, 0.000, 0.20, 1.00},
    {-0.01, 0.015, 0.35, 0.75},
};

static float timed_strikes[] = {100.0f, 110.0f};

typedef struct { double wall, tsc; } timing_t;

typedef struct __attribute__((aligned(64))) {
    asian_affine_family_engine_t engine;
    asian_affine_family_generic_oracle_t oracle;
    asian_affine_family_growth_carrier_t growth[2];
    asian_affine_family_xgrowth_carrier_t xgrowth[2];
    asian_affine_family_growth_carrier_t growth_scratch;
    asian_affine_family_xgrowth_carrier_t xgrowth_scratch;
    asian_affine_family_arithmetic_request_t arithmetic[2][2];
    asian_affine_family_arithmetic_request_t arithmetic_scratch;
    asian_affine_family_full_risk_k1_request_t full_risk[2];
    asian_affine_family_full_risk_k1_request_t full_risk_scratch;
    asian_affine_family_geocv_request_t generic_routes;
    asian_genuine_aad_phase1_controls_t generic_controls;
    asian_genuine_aad_phase1_context_t generic_context;
    asian_genuine_msfr_strike_t generic_parity;
    asian_genuine_msfr_strike_controls_t qualified_strikes;
    asian_genuine_msfr_consumer_context_t qualified_consumer;
    asian_genuine_msfr_accumulator_t qualified_accumulator;
    asian_genuine_aad_phase1_controls_t qualified_phase_controls;
    asian_genuine_aad_phase1_context_t qualified_phase_context;
    asian_genuine_msfr_output_t qualified_output;
    asian_genuine_strip_output_t strip_output[2];
    asian_affine_family_full_risk_k1_output_t full_output;
    asian_affine_family_full_risk_k1_output_t independent_output;
    uint32_t pressure[8192];
} workspace_t;

static workspace_t *workspace;
static volatile uint64_t sink;
static float generic_forward_tape_sentinel[16] __attribute__((aligned(64)));

static const char *const workload_name[] = {
    "PRICE", "PRICE_DELTA", "PRICE_DELTA_VEGA_RHO"
};
static const char *const lifecycle_name[] = {
    "prepared", "reused_carrier", "fresh"
};
static const char *const cache_name[] = {
    "candidate_warm", "pressure_32KiB"
};

static void *aligned_zero(size_t bytes)
{
    void *memory = NULL;
    if (posix_memalign(&memory, 64u, bytes) != 0)
        return NULL;
    memset(memory, 0, bytes);
    return memory;
}

static uint64_t wall_now(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value) != 0)
        abort();
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static inline uint64_t tsc_begin(void)
{
    _mm_lfence();
    const uint64_t value = __rdtsc();
    _mm_lfence();
    return value;
}

static inline uint64_t tsc_end(void)
{
    unsigned auxiliary;
    const uint64_t value = __rdtscp(&auxiliary);
    _mm_lfence();
    return value;
}

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

static double median(double *values, size_t count)
{
    qsort(values, count, sizeof(values[0]), compare_double);
    return values[count / 2u];
}

static uint64_t hash_bytes(uint64_t hash, const void *memory, size_t bytes)
{
    const unsigned char *data = memory;
    while (bytes-- != 0u) {
        hash ^= *data++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void warm_bytes(const void *memory, size_t bytes)
{
    const volatile unsigned char *data = memory;
    uint64_t value = sink;
    for (size_t offset = 0; offset < bytes; offset += 64u)
        value += data[offset];
    sink = value;
}

static void pressure_32k(void)
{
    uint64_t value = sink;
    for (uint32_t index = 0; index < 8192u; index += 16u) {
        workspace->pressure[index] += index + 1u;
        value += workspace->pressure[index];
    }
    sink = value;
}

static int pin_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

static int is_sapphire_rapids(void)
{
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid(1u, &eax, &ebx, &ecx, &edx))
        return 0;
    const unsigned family = ((eax >> 8) & 15u) + ((eax >> 20) & 255u);
    const unsigned model = ((eax >> 4) & 15u) | ((eax >> 12) & 240u);
    return family == 6u && model == 143u;
}

static asian_affine_family_carrier_input_t carrier_input(
    const market_t *market, uint32_t n)
{
    const asian_affine_family_carrier_input_t input = {
        market->rate, market->dividend, market->sigma, market->maturity, n
    };
    return input;
}

static asian_affine_family_request_input_t request_input(
    const market_t *market, uint32_t n, uint32_t variant,
    enum workload_kind workload)
{
    const asian_affine_family_request_input_t input = {
        .s0 = variant == 0u ? 100.0 : 117.0,
        .rate = market->rate,
        .dividend_yield = market->dividend,
        .sigma = market->sigma,
        .maturity = market->maturity,
        .future_fixings = n,
        .completed_fixings = 0u,
        .initial_arithmetic_sum = 0.0,
        .past_log_sum = 0.0,
        .strikes = &timed_strikes[variant],
        .strike_count = 1u,
        .workload = workload == WORK_PRICE ? ASIAN_AFFINE_FAMILY_PRICE :
                    ASIAN_AFFINE_FAMILY_PRICE_DELTA,
    };
    return input;
}

static int generic_full_risk_prepare(
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input)
{
    asian_affine_family_request_input_t route_input = *input;
    route_input.workload = ASIAN_AFFINE_FAMILY_PRICE;
    if (asian_affine_family_geocv_request_prepare(&workspace->engine,
            &workspace->oracle, carrier, &route_input,
            ASIAN_AFFINE_FAMILY_GENERIC, &workspace->generic_routes) != 0)
        return -1;
    if (asian_genuine_aad_phase1_prepare_arithmetic_controls(
            &workspace->generic_controls, input->s0, input->strikes[0],
            input->rate, input->dividend_yield, input->sigma,
            input->maturity, input->future_fixings) != 0)
        return -1;
    if (asian_genuine_msfr_prepare_arithmetic_strike(
            &workspace->generic_parity, input->s0, input->rate,
            input->dividend_yield, input->sigma, input->maturity,
            input->future_fixings, input->strikes[0]) != 0)
        return -1;
    return asian_genuine_aad_phase1_prepare_context(
        &workspace->generic_context, workspace->generic_routes.routes.generic,
        generic_forward_tape_sentinel, &workspace->generic_controls,
        input->s0, input->strikes[0], input->rate, input->dividend_yield,
        input->sigma, input->maturity, input->future_fixings);
}

__attribute__((noinline, used))
void asian_commercial_full_risk_generic_one_side_oracle_diag(
    asian_affine_family_full_risk_k1_output_t *output)
{
    const int direct_call = (workspace->generic_parity.flags &
                             ASIAN_GENUINE_MSFR_DIRECT_CALL) != 0u;
    void (*leaf)(const asian_genuine_aad_phase1_context_t *,
                 asian_genuine_aad_phase1_value_t *) = direct_call ?
        asian_genuine_aad_phase1_forward_arithmetic_call_diag :
        asian_genuine_aad_phase1_forward_arithmetic_put_diag;
    asian_genuine_aad_phase1_value_t *direct = direct_call ?
        &output->call : &output->put;
    leaf(&workspace->generic_context, direct);
    const double *values = (const double *)direct;
    double *call = (double *)&output->call;
    double *put = (double *)&output->put;
    for (uint32_t field = 0; field < ASIAN_GENUINE_MSFR_RISK_FIELDS; ++field) {
        const double sum = 0.0 + (values[field] - 0.0) * 4096.0;
        const double normalized = sum * (1.0 / 4096.0);
        call[field] = normalized + workspace->generic_parity.call_adjust[field];
        put[field] = normalized + workspace->generic_parity.put_adjust[field];
    }
}

static int parity_record_matches(
    const asian_genuine_msfr_strike_t *arithmetic,
    const asian_genuine_msfr_strike_t *qualified)
{
    return arithmetic->strike == qualified->strike &&
        arithmetic->direct_sign == qualified->direct_sign &&
        arithmetic->strike_bits == qualified->strike_bits &&
        arithmetic->flags == qualified->flags &&
        memcmp(arithmetic->call_adjust, qualified->call_adjust,
               sizeof(arithmetic->call_adjust)) == 0 &&
        memcmp(arithmetic->put_adjust, qualified->put_adjust,
               sizeof(arithmetic->put_adjust)) == 0;
}

static int qualified_k1_parity_output(
    const asian_affine_family_request_input_t *input,
    asian_affine_family_full_risk_k1_output_t *output)
{
    if (asian_genuine_msfr_prepare_strikes(&workspace->qualified_strikes,
            input->s0, input->rate, input->dividend_yield, input->sigma,
            input->maturity, input->future_fixings, input->strikes, 1u) != 0 ||
        asian_genuine_msfr_prepare_consumer_context(
            &workspace->qualified_consumer,
            &workspace->qualified_strikes) != 0 ||
        asian_genuine_msfr_accumulator_init(
            &workspace->qualified_accumulator,
            &workspace->qualified_consumer,
            ASIAN_GENUINE_MSFR_ARITHMETIC) != 0 ||
        asian_genuine_aad_phase1_prepare_controls(
            &workspace->qualified_phase_controls, input->s0,
            input->strikes[0], input->rate, input->dividend_yield,
            input->sigma, input->maturity, input->future_fixings) != 0)
        return -1;

    workspace->qualified_phase_context = workspace->generic_context;
    workspace->qualified_phase_context.controls =
        &workspace->qualified_phase_controls;
    if (!parity_record_matches(&workspace->generic_parity,
                               &workspace->qualified_strikes.strikes[0]) ||
        memcmp(&workspace->generic_controls,
               &workspace->qualified_phase_controls,
               offsetof(asian_genuine_aad_phase1_controls_t,
                        geometric_call)) != 0 ||
        asian_genuine_msfr_hybrid_consume_block_diag(
            NULL, &workspace->qualified_consumer,
            ASIAN_GENUINE_MSFR_ARITHMETIC,
            &workspace->qualified_phase_context,
            &workspace->qualified_accumulator) != 0 ||
        asian_genuine_msfr_finalize(&workspace->qualified_consumer,
            &workspace->qualified_accumulator,
            &workspace->qualified_output) != 0)
        return -1;
    memcpy(&output->call, &workspace->qualified_output.values[0].call,
           sizeof(output->call));
    memcpy(&output->put, &workspace->qualified_output.values[0].put,
           sizeof(output->put));
    return 0;
}

static int compare_full_risk_case(const market_t *market, uint32_t n,
                                  uint32_t variant, int strong)
{
    asian_affine_family_full_risk_k1_output_t qualified
        __attribute__((aligned(64)));
    asian_affine_family_full_risk_k1_output_t generic
        __attribute__((aligned(64)));
    asian_affine_family_full_risk_k1_output_t affine
        __attribute__((aligned(64)));
    asian_affine_family_full_risk_k1_output_t repeated
        __attribute__((aligned(64)));
    const asian_affine_family_carrier_input_t ci = carrier_input(market, n);
    const asian_affine_family_request_input_t input =
        request_input(market, n, variant, WORK_FULL_RISK);
    if (asian_affine_family_xgrowth_carrier_prepare(&workspace->engine, &ci,
            &workspace->xgrowth[0]) != 0 ||
        generic_full_risk_prepare(&workspace->xgrowth[0], &input) != 0 ||
        asian_affine_family_full_risk_k1_request_prepare(&workspace->engine,
            &workspace->xgrowth[0], &input, &workspace->full_risk[0]) != 0)
        return -1;

    uint64_t carrier_before = 0u, request_before = 0u;
    if (strong) {
        carrier_before = hash_bytes(UINT64_C(1469598103934665603),
            &workspace->xgrowth[0], sizeof(workspace->xgrowth[0]));
        request_before = hash_bytes(UINT64_C(1469598103934665603),
            &workspace->full_risk[0], sizeof(workspace->full_risk[0]));
    }
    if (qualified_k1_parity_output(&input, &qualified) != 0)
        return -1;
    const asian_genuine_aad_phase1_value_t zero = {0.0, 0.0, 0.0, 0.0};
    if (!parity_record_matches(&workspace->full_risk[0].parity,
                               &workspace->qualified_strikes.strikes[0]) ||
        memcmp(&workspace->full_risk[0].controls,
               &workspace->qualified_phase_controls,
               offsetof(asian_genuine_aad_phase1_controls_t,
                        geometric_call)) != 0 ||
        memcmp(&workspace->full_risk[0].controls.geometric_call,
               &zero, sizeof(zero)) != 0 ||
        memcmp(&workspace->full_risk[0].controls.geometric_put,
               &zero, sizeof(zero)) != 0)
        return -1;
    asian_commercial_full_risk_generic_one_side_oracle_diag(&generic);
    if (asian_affine_family_full_risk_k1_prepared_price(
            &workspace->full_risk[0],
            &affine) != 0 ||
        asian_affine_family_full_risk_k1_prepared_price(
            &workspace->full_risk[0],
            &repeated) != 0 ||
        memcmp(&qualified, &generic, sizeof(qualified)) != 0 ||
        memcmp(&qualified, &affine, sizeof(qualified)) != 0 ||
        memcmp(&affine, &repeated, sizeof(affine)) != 0)
        return -1;
    asian_commercial_full_risk_independent_call_plus_put_diag(
        &workspace->full_risk[0], &workspace->independent_output);
    sink = hash_bytes(sink, &workspace->independent_output,
                      sizeof(workspace->independent_output));
    if (strong &&
        (carrier_before != hash_bytes(UINT64_C(1469598103934665603),
             &workspace->xgrowth[0], sizeof(workspace->xgrowth[0])) ||
         request_before != hash_bytes(UINT64_C(1469598103934665603),
             &workspace->full_risk[0], sizeof(workspace->full_risk[0]))))
        return -1;
    return 0;
}

static int correctness(int exhaustive)
{
    if (asian_affine_family_engine_create(&workspace->engine) != 0 ||
        asian_affine_family_generic_oracle_create(&workspace->oracle) != 0)
        return -1;
    const uint32_t first = exhaustive ? 2u : 64u;
    const uint32_t last = exhaustive ? 256u : 256u;
    for (uint32_t market = 0; market < 4u; ++market) {
        for (uint32_t n = first; n <= last; ++n) {
            if (!exhaustive && n != 64u && n != 256u)
                continue;
            if (compare_full_risk_case(&frozen_markets[market], n,
                    (market + n) & 1u, n == 64u || n == 256u) != 0) {
                fprintf(stderr, "full-risk identity failed market=%u N=%u\n",
                        market, n);
                return -1;
            }
        }
    }
    asian_affine_family_generic_oracle_destroy(&workspace->oracle);
    asian_affine_family_engine_destroy(&workspace->engine);
    memset(&workspace->oracle, 0, sizeof(workspace->oracle));
    memset(&workspace->engine, 0, sizeof(workspace->engine));
    printf("commercial_full_risk_correctness PASS N=2..256=%s "
           "markets=4 call_put_fields=price,delta,vega,rho identity=EXACT "
           "oracle=QUALIFIED_K1_MULTI_STRIKE_PARITY phase1_calls=1 "
           "INDEPENDENT_CALL_PLUS_PUT=RETAINED_NOT_TIMED\n",
           exhaustive ? "YES" : "BOUNDED_64_256");
    return 0;
}

static int prepare_timed_fixture(uint32_t n)
{
    for (uint32_t variant = 0; variant < 2u; ++variant) {
        const asian_affine_family_carrier_input_t ci =
            carrier_input(&timed_markets[variant], n);
        if (asian_affine_family_growth_carrier_prepare(&workspace->engine, &ci,
                &workspace->growth[variant]) != 0 ||
            asian_affine_family_xgrowth_carrier_prepare(&workspace->engine, &ci,
                &workspace->xgrowth[variant]) != 0)
            return -1;
        for (uint32_t workload = 0; workload < 2u; ++workload) {
            const asian_affine_family_request_input_t input = request_input(
                &timed_markets[variant], n, variant,
                (enum workload_kind)workload);
            if (asian_affine_family_arithmetic_request_prepare_growth(
                    &workspace->engine, NULL, &workspace->growth[variant],
                    &input, ASIAN_AFFINE_FAMILY_AFFINE,
                    &workspace->arithmetic[workload][variant]) != 0)
                return -1;
        }
        const asian_affine_family_request_input_t full_input = request_input(
            &timed_markets[variant], n, variant, WORK_FULL_RISK);
        if (asian_affine_family_full_risk_k1_request_prepare(
                &workspace->engine,
                &workspace->xgrowth[variant], &full_input,
                &workspace->full_risk[variant]) != 0)
            return -1;
    }
    return 0;
}

static void warm_full_risk_request(
    const asian_affine_family_full_risk_k1_request_t *request, uint32_t n)
{
    warm_bytes(request->routes, n * sizeof(request->routes[0]));
    warm_bytes(request->controls.forward_weights,
               n * sizeof(request->controls.forward_weights[0]));
    warm_bytes(&request->context, sizeof(request->context));
    warm_bytes(&request->parity, sizeof(request->parity));
}

static void warm_full_risk_plan(uint32_t n)
{
    warm_bytes(workspace->engine.affine_plan, ASIAN_META_PLAN_HEADER_BYTES);
    for (uint32_t fixing = 0; fixing < n; ++fixing)
        warm_bytes(&workspace->engine.affine_plan->contexts[fixing],
                   sizeof(workspace->engine.affine_plan->contexts[fixing]));
}

static void condition(uint32_t n, enum workload_kind workload,
                      enum lifecycle_kind lifecycle,
                      enum cache_kind cache, uint32_t variant)
{
    if (cache == CACHE_PRESSURE) {
        pressure_32k();
        return;
    }
    if (workload == WORK_FULL_RISK) {
        const asian_affine_family_xgrowth_carrier_t *carrier =
            lifecycle == LIFE_FRESH ? &workspace->xgrowth_scratch :
                                      &workspace->xgrowth[variant];
        const asian_affine_family_full_risk_k1_request_t *request =
            lifecycle == LIFE_PREPARED ? &workspace->full_risk[variant] :
                                         &workspace->full_risk_scratch;
        warm_full_risk_plan(n);
        warm_bytes(carrier->x, sizeof(carrier->x));
        warm_bytes(carrier->growth, sizeof(carrier->growth));
        if (lifecycle != LIFE_PREPARED)
            warm_bytes(&carrier->market,
                       sizeof(*carrier) - offsetof(
                           asian_affine_family_xgrowth_carrier_t, market));
        warm_full_risk_request(request, n);
    } else {
        warm_bytes(workspace->engine.affine_plan,
                   sizeof(*workspace->engine.affine_plan));
        const void *carrier = lifecycle == LIFE_FRESH ?
            (const void *)&workspace->growth_scratch :
            (const void *)&workspace->growth[variant];
        const void *request = lifecycle == LIFE_PREPARED ?
            (const void *)&workspace->arithmetic[workload][variant] :
            (const void *)&workspace->arithmetic_scratch;
        warm_bytes(carrier, sizeof(workspace->growth[0]));
        warm_bytes(request, sizeof(workspace->arithmetic[0][0]));
    }
}

static timing_t observe(uint32_t n, enum workload_kind workload,
                        enum lifecycle_kind lifecycle,
                        enum cache_kind cache, uint32_t variant)
{
    const market_t *market = lifecycle == LIFE_REUSE ?
        &timed_markets[0] : &timed_markets[variant];
    const uint32_t carrier_variant = lifecycle == LIFE_REUSE ? 0u : variant;
    const asian_affine_family_request_input_t input =
        request_input(market, n, variant, workload);
    const asian_affine_family_carrier_input_t ci = carrier_input(market, n);
    int status = 0;
    condition(n, workload, lifecycle, cache, carrier_variant);
    const uint64_t wall0 = wall_now();
    const uint64_t ticks0 = tsc_begin();
    if (workload == WORK_FULL_RISK) {
        asian_affine_family_full_risk_k1_request_t *request =
            lifecycle == LIFE_PREPARED ? &workspace->full_risk[variant] :
                                         &workspace->full_risk_scratch;
        if (lifecycle == LIFE_FRESH)
            status = asian_affine_family_xgrowth_carrier_prepare(
                &workspace->engine, &ci, &workspace->xgrowth_scratch);
        if (status == 0 && lifecycle != LIFE_PREPARED) {
            const asian_affine_family_xgrowth_carrier_t *carrier =
                lifecycle == LIFE_FRESH ? &workspace->xgrowth_scratch :
                                          &workspace->xgrowth[0];
            status = asian_affine_family_full_risk_k1_request_prepare(
                &workspace->engine, carrier, &input, request);
        }
        if (status == 0)
            status = asian_affine_family_full_risk_k1_prepared_price(
                request, &workspace->full_output);
    } else {
        asian_affine_family_arithmetic_request_t *request =
            lifecycle == LIFE_PREPARED ?
            &workspace->arithmetic[workload][variant] :
            &workspace->arithmetic_scratch;
        if (lifecycle == LIFE_FRESH)
            status = asian_affine_family_growth_carrier_prepare(
                &workspace->engine, &ci, &workspace->growth_scratch);
        if (status == 0 && lifecycle != LIFE_PREPARED) {
            const asian_affine_family_growth_carrier_t *carrier =
                lifecycle == LIFE_FRESH ? &workspace->growth_scratch :
                                          &workspace->growth[0];
            status = asian_affine_family_arithmetic_request_prepare_growth(
                &workspace->engine, NULL, carrier, &input,
                ASIAN_AFFINE_FAMILY_AFFINE, request);
        }
        if (status == 0)
            status = asian_affine_family_arithmetic_prepared_price(
                request, &workspace->strip_output[workload]);
    }
    const uint64_t ticks1 = tsc_end();
    const uint64_t wall1 = wall_now();
    if (status != 0)
        abort();
    if (workload == WORK_FULL_RISK)
        sink = hash_bytes(sink, &workspace->full_output,
                          sizeof(workspace->full_output));
    else
        sink = hash_bytes(sink, &workspace->strip_output[workload],
                          sizeof(workspace->strip_output[workload]));
    return (timing_t){(double)(wall1 - wall0),
                      (double)(ticks1 - ticks0)};
}

static void measure_cell(uint32_t n, enum lifecycle_kind lifecycle,
                         enum cache_kind cache, timing_t out[WORKLOADS])
{
    double wall[WORKLOADS][SAMPLES];
    double tsc[WORKLOADS][SAMPLES];
    static const enum workload_kind base_order[] = {
        WORK_PRICE, WORK_PRICE_DELTA, WORK_FULL_RISK,
        WORK_FULL_RISK, WORK_PRICE_DELTA, WORK_PRICE
    };
    for (uint32_t group = 0; group < WARMUPS + SAMPLES; ++group) {
        double sum_wall[WORKLOADS] = {0.0, 0.0, 0.0};
        double sum_tsc[WORKLOADS] = {0.0, 0.0, 0.0};
        const uint32_t rotate = group % WORKLOADS;
        for (uint32_t slot = 0; slot < 6u; ++slot) {
            const enum workload_kind workload =
                base_order[(slot + rotate) % 6u];
            const uint32_t variant = (group + slot) & 1u;
            const timing_t value = observe(n, workload, lifecycle, cache,
                                           variant);
            sum_wall[workload] += value.wall;
            sum_tsc[workload] += value.tsc;
        }
        if (group >= WARMUPS) {
            const uint32_t sample = group - WARMUPS;
            for (uint32_t workload = 0; workload < WORKLOADS; ++workload) {
                wall[workload][sample] = 0.5 * sum_wall[workload];
                tsc[workload][sample] = 0.5 * sum_tsc[workload];
            }
        }
    }
    for (uint32_t workload = 0; workload < WORKLOADS; ++workload) {
        out[workload].wall = median(wall[workload], SAMPLES);
        out[workload].tsc = median(tsc[workload], SAMPLES);
    }
}

static timing_t observe_affine_plan(void)
{
    asian_affine_family_engine_t engine __attribute__((aligned(64)));
    memset(&engine, 0, sizeof(engine));
    const uint64_t wall0 = wall_now(), ticks0 = tsc_begin();
    const int status = asian_affine_family_engine_create(&engine);
    const uint64_t ticks1 = tsc_end(), wall1 = wall_now();
    if (status != 0)
        abort();
    asian_affine_family_engine_destroy(&engine);
    return (timing_t){(double)(wall1 - wall0),
                      (double)(ticks1 - ticks0)};
}

static timing_t observe_generic_plan(void)
{
    asian_affine_family_generic_oracle_t oracle __attribute__((aligned(64)));
    memset(&oracle, 0, sizeof(oracle));
    const uint64_t wall0 = wall_now(), ticks0 = tsc_begin();
    const int status = asian_affine_family_generic_oracle_create(&oracle);
    const uint64_t ticks1 = tsc_end(), wall1 = wall_now();
    if (status != 0)
        abort();
    asian_affine_family_generic_oracle_destroy(&oracle);
    return (timing_t){(double)(wall1 - wall0),
                      (double)(ticks1 - ticks0)};
}

static void report_plan_builders(void)
{
    double affine_wall[PLAN_SAMPLES], affine_tsc[PLAN_SAMPLES];
    double generic_wall[PLAN_SAMPLES], generic_tsc[PLAN_SAMPLES];
    for (uint32_t sample = 0; sample < PLAN_SAMPLES; ++sample) {
        timing_t a, g;
        if ((sample & 1u) == 0u) {
            g = observe_generic_plan();
            a = observe_affine_plan();
        } else {
            a = observe_affine_plan();
            g = observe_generic_plan();
        }
        affine_wall[sample] = a.wall;
        affine_tsc[sample] = a.tsc;
        generic_wall[sample] = g.wall;
        generic_tsc[sample] = g.tsc;
    }
    printf("plan_builder wall_ns tsc_ticks identity\n");
    printf("generic_qsort %.1f %.1f PASS\n",
           median(generic_wall, PLAN_SAMPLES),
           median(generic_tsc, PLAN_SAMPLES));
    printf("affine_meta %.1f %.1f PASS\n",
           median(affine_wall, PLAN_SAMPLES),
           median(affine_tsc, PLAN_SAMPLES));
}

static void print_ratio(uint32_t n, enum lifecycle_kind lifecycle,
                        enum cache_kind cache, const char *name,
                        timing_t candidate, timing_t baseline)
{
    const double wall_ratio = candidate.wall / baseline.wall;
    const double tsc_ratio = candidate.tsc / baseline.tsc;
    printf("RATIO N=%u lifecycle=%s cache=%s comparison=%s "
           "wall_ratio=%.6f wall_overhead_pct=%.3f "
           "tsc_ratio=%.6f tsc_overhead_pct=%.3f\n",
           n, lifecycle_name[lifecycle], cache_name[cache], name,
           wall_ratio, 100.0 * (wall_ratio - 1.0),
           tsc_ratio, 100.0 * (tsc_ratio - 1.0));
}

static int timing_run(int cpu)
{
    if (cpu != 0 || pin_cpu(cpu) != 0 || !is_sapphire_rapids()) {
        fprintf(stderr, "CPU 0 on family-6/model-143 Sapphire Rapids required\n");
        return -1;
    }
    report_plan_builders();
    if (asian_affine_family_engine_create(&workspace->engine) != 0 ||
        asian_affine_family_generic_oracle_create(&workspace->oracle) != 0)
        return -1;

    static const uint32_t ns[] = {64u, 256u};
    timing_t headline[2][WORKLOADS];
    puts("N workload lifecycle cache wall_ns tsc_ticks "
         "valuations_per_sec_per_core "
         "effective_path_fixing_updates_per_sec_per_core identity");
    for (uint32_t ni = 0; ni < 2u; ++ni) {
        const uint32_t n = ns[ni];
        if (prepare_timed_fixture(n) != 0)
            return -1;
        for (uint32_t lifecycle = 0; lifecycle < LIFECYCLES; ++lifecycle) {
            for (uint32_t cache = 0; cache < CACHE_MODES; ++cache) {
                timing_t results[WORKLOADS];
                measure_cell(n, (enum lifecycle_kind)lifecycle,
                             (enum cache_kind)cache, results);
                for (uint32_t workload = 0; workload < WORKLOADS; ++workload) {
                    const double valuations = 1.0e9 / results[workload].wall;
                    const double updates = valuations * 4096.0 * (double)n;
                    printf("%u %s %s %s %.1f %.1f %.6f %.6f PASS\n",
                           n, workload_name[workload],
                           lifecycle_name[lifecycle], cache_name[cache],
                           results[workload].wall, results[workload].tsc,
                           valuations, updates);
                    if (lifecycle == LIFE_FRESH && cache == CACHE_WARM)
                        headline[ni][workload] = results[workload];
                }
                print_ratio(n, (enum lifecycle_kind)lifecycle,
                    (enum cache_kind)cache, "price_delta_vs_price",
                    results[WORK_PRICE_DELTA], results[WORK_PRICE]);
                print_ratio(n, (enum lifecycle_kind)lifecycle,
                    (enum cache_kind)cache, "full_risk_vs_price",
                    results[WORK_FULL_RISK], results[WORK_PRICE]);
                print_ratio(n, (enum lifecycle_kind)lifecycle,
                    (enum cache_kind)cache, "full_risk_vs_price_delta",
                    results[WORK_FULL_RISK], results[WORK_PRICE_DELTA]);
            }
        }
    }
    puts("HEADLINE lifecycle=fresh cache=candidate_warm");
    for (uint32_t ni = 0; ni < 2u; ++ni) {
        const uint32_t n = ns[ni];
        printf("N%u price_wall_us=%.3f price_delta_wall_us=%.3f "
               "full_risk_wall_us=%.3f full_risk_vs_price=%.6f "
               "full_risk_vs_price_delta=%.6f identity=PASS\n",
               n, headline[ni][WORK_PRICE].wall / 1000.0,
               headline[ni][WORK_PRICE_DELTA].wall / 1000.0,
               headline[ni][WORK_FULL_RISK].wall / 1000.0,
               headline[ni][WORK_FULL_RISK].wall /
                   headline[ni][WORK_PRICE].wall,
               headline[ni][WORK_FULL_RISK].wall /
                   headline[ni][WORK_PRICE_DELTA].wall);
    }
    const double n256_wall = headline[1][WORK_FULL_RISK].wall;
    const double valuations = 1.0e9 / n256_wall;
    printf("N256_FULL_RISK_SINGLE_CORE wall_us=%.3f valuations_per_sec=%.6f "
           "effective_path_fixing_updates_per_sec=%.6f identity=PASS\n",
           n256_wall / 1000.0, valuations,
           valuations * 4096.0 * 256.0);

    asian_affine_family_generic_oracle_destroy(&workspace->oracle);
    asian_affine_family_engine_destroy(&workspace->engine);
    return 0;
}

int main(int argc, char **argv)
{
    enum { MODE_NONE, MODE_CHECK, MODE_NATIVE_CHECK, MODE_TIMING } mode =
        MODE_NONE;
    int cpu = -1;
    for (int arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--check") == 0)
            mode = MODE_CHECK;
        else if (strcmp(argv[arg], "--native-check") == 0)
            mode = MODE_NATIVE_CHECK;
        else if (strcmp(argv[arg], "--timing") == 0)
            mode = MODE_TIMING;
        else if (strcmp(argv[arg], "--cpu") == 0 && arg + 1 < argc)
            cpu = atoi(argv[++arg]);
        else {
            fprintf(stderr, "invalid arguments\n");
            return 2;
        }
    }
    workspace = aligned_zero(sizeof(*workspace));
    if (workspace == NULL) {
        fprintf(stderr, "workspace allocation failed\n");
        return 2;
    }
    int status;
    if (mode == MODE_CHECK)
        status = correctness(1);
    else if (mode == MODE_NATIVE_CHECK)
        status = correctness(0);
    else if (mode == MODE_TIMING)
        status = timing_run(cpu);
    else {
        fprintf(stderr, "one mode is required\n");
        status = -1;
    }
    free(workspace);
    return status == 0 ? (sink == UINT64_MAX ? 1 : 0) : 2;
}
