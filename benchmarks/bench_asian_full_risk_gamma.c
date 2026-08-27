#define _GNU_SOURCE

#include "private/asian_full_risk_gamma_diag.h"

#include <cpuid.h>
#include <immintrin.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WARMUPS = 16, SAMPLES = 101, INPUTS = 2, SIDES = 2 };
enum workload { CURRENT_RISK, FUSED_GAMMA, TRIPLE_GAMMA, WORKLOAD_COUNT };
enum lifecycle { PREPARED, REUSED, FRESH, LIFECYCLE_COUNT };
enum cache_mode { CANDIDATE_WARM, PRESSURE_32K, CACHE_COUNT };

typedef struct { double rate, dividend, sigma, maturity; } market_t;
typedef struct { double wall, tsc; } timing_t;
typedef struct { timing_t candidate[2]; } pair_result_t;

static const market_t markets[2] = {
    {0.03, 0.0, 0.20, 1.0},
    {-0.01, 0.015, 0.35, 0.75},
};
static const float strikes[SIDES][INPUTS] = {
    {110.0f, 125.0f}, /* direct call */
    { 95.0f, 105.0f}, /* direct put */
};
static const char *const lifecycle_name[LIFECYCLE_COUNT] = {
    "prepared", "reused_carrier", "fresh"};
static const char *const cache_name[CACHE_COUNT] = {
    "candidate_warm", "pressure_32KiB"};
static const char *const side_name[SIDES] = {"call", "put"};

/* Frozen by tests/compare_asian_full_risk_gamma.py using QuantLib 1.39-dev,
 * 1,048,576 paths per replication and 16 independent CRN replications. */
static const double gamma_reference[SIDES] = {
    0.033058556467046252, 0.033058556467106925};
static const double gamma_reference_low[SIDES] = {
    0.033038371683254825, 0.033038371683315512};
static const double gamma_reference_high[SIDES] = {
    0.033078741250837679, 0.033078741250898339};

typedef struct __attribute__((aligned(64))) {
    asian_affine_family_engine_t engine;
    asian_affine_family_xgrowth_carrier_t carrier[INPUTS];
    asian_affine_family_xgrowth_carrier_t carrier_scratch;
    asian_affine_family_full_risk_k1_request_t current[SIDES][INPUTS];
    asian_affine_family_full_risk_k1_request_t current_scratch;
    asian_full_risk_gamma_request_t fused[SIDES][INPUTS];
    asian_full_risk_gamma_request_t fused_scratch;
    asian_full_risk_gamma_triple_request_t triple[SIDES][INPUTS];
    asian_full_risk_gamma_triple_request_t triple_scratch;
    asian_affine_family_full_risk_k1_output_t current_expected[SIDES][INPUTS];
    asian_full_risk_gamma_output_t fused_expected[SIDES][INPUTS];
    asian_full_risk_gamma_output_t triple_expected[SIDES][INPUTS];
    asian_affine_family_full_risk_k1_output_t current_reuse_expected[SIDES][INPUTS];
    asian_full_risk_gamma_output_t fused_reuse_expected[SIDES][INPUTS];
    asian_full_risk_gamma_output_t triple_reuse_expected[SIDES][INPUTS];
    asian_affine_family_full_risk_k1_output_t current_output;
    asian_full_risk_gamma_output_t gamma_output;
    uint32_t pressure[8192];
} workspace_t;

static workspace_t *workspace;
static volatile uint64_t sink_value;

static void *a64(size_t bytes)
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

static uint64_t tsc_begin(void)
{
    _mm_lfence();
    const uint64_t value = __rdtsc();
    _mm_lfence();
    return value;
}

static uint64_t tsc_end(void)
{
    unsigned auxiliary;
    const uint64_t value = __rdtscp(&auxiliary);
    _mm_lfence();
    return value;
}

static int compare_double(const void *a, const void *b)
{
    const double left = *(const double *)a, right = *(const double *)b;
    return (left > right) - (left < right);
}

static double median(double *values, size_t count)
{
    qsort(values, count, sizeof(values[0]), compare_double);
    return values[count / 2u];
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t bytes)
{
    const unsigned char *p = data;
    while (bytes-- != 0u) {
        hash ^= *p++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void warm_bytes(const void *data, size_t bytes)
{
    const volatile unsigned char *p = data;
    uint64_t value = sink_value;
    for (size_t offset = 0; offset < bytes; offset += 64u)
        value += p[offset];
    sink_value = value;
}

static void pressure_32k(void)
{
    uint64_t value = sink_value;
    for (uint32_t index = 0; index < 8192u; index += 16u) {
        workspace->pressure[index] += index + 1u;
        value += workspace->pressure[index];
    }
    sink_value = value;
}

static int sapphire_rapids(void)
{
    unsigned a, b, c, d;
    if (!__get_cpuid(1u, &a, &b, &c, &d))
        return 0;
    const unsigned family = ((a >> 8) & 15u) + ((a >> 20) & 255u);
    const unsigned model = ((a >> 4) & 15u) | ((a >> 12) & 240u);
    return family == 6u && model == 143u;
}

static int pin_cpu(unsigned cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

static asian_affine_family_carrier_input_t carrier_input(uint32_t n,
                                                          uint32_t variant)
{
    const market_t *m = &markets[variant];
    const asian_affine_family_carrier_input_t value = {
        m->rate, m->dividend, m->sigma, m->maturity, n};
    return value;
}

static asian_full_risk_gamma_request_input_t request_input(
    uint32_t n, uint32_t side, uint32_t variant, int reuse)
{
    const market_t *m = reuse ? &markets[0] : &markets[variant];
    static const float reuse_strikes[SIDES][INPUTS] = {
        {110.0f, 120.0f}, {95.0f, 100.0f}};
    static const double reuse_s0[INPUTS] = {100.0, 108.0};
    asian_full_risk_gamma_request_input_t input;
    memset(&input, 0, sizeof(input));
    input.family.s0 = reuse ? reuse_s0[variant] :
        (variant == 0u ? 100.0 : 117.0);
    input.family.rate = m->rate;
    input.family.dividend_yield = m->dividend;
    input.family.sigma = m->sigma;
    input.family.maturity = m->maturity;
    input.family.future_fixings = n;
    input.family.strikes = reuse ? &reuse_strikes[side][variant] :
                                   &strikes[side][variant];
    input.family.strike_count = 1u;
    input.family.workload = ASIAN_AFFINE_FAMILY_PRICE;
    input.gamma_bump_fraction = 0.01;
    input.workload = ASIAN_FULL_RISK_PRICE_DELTA_GAMMA_VEGA_RHO;
    return input;
}

static int parent_fields_equal(
    const asian_affine_family_full_risk_k1_output_t *parent,
    const asian_full_risk_gamma_output_t *gamma)
{
    return memcmp(&parent->call.price, &gamma->call.price, 8u) == 0 &&
           memcmp(&parent->call.delta, &gamma->call.delta, 8u) == 0 &&
           memcmp(&parent->call.vega, &gamma->call.vega, 8u) == 0 &&
           memcmp(&parent->call.rho, &gamma->call.rho, 8u) == 0 &&
           memcmp(&parent->put.price, &gamma->put.price, 8u) == 0 &&
           memcmp(&parent->put.delta, &gamma->put.delta, 8u) == 0 &&
           memcmp(&parent->put.vega, &gamma->put.vega, 8u) == 0 &&
           memcmp(&parent->put.rho, &gamma->put.rho, 8u) == 0;
}

static int setup(uint32_t n)
{
    for (uint32_t variant = 0; variant < INPUTS; ++variant) {
        const asian_affine_family_carrier_input_t ci =
            carrier_input(n, variant);
        if (asian_affine_family_xgrowth_carrier_prepare(&workspace->engine,
                &ci, &workspace->carrier[variant]) != 0)
            return -1;
        for (uint32_t side = 0; side < SIDES; ++side) {
            const asian_full_risk_gamma_request_input_t input =
                request_input(n, side, variant, 0);
            if (asian_affine_family_full_risk_k1_request_prepare(
                    &workspace->engine, &workspace->carrier[variant],
                    &input.family, &workspace->current[side][variant]) != 0 ||
                asian_full_risk_gamma_request_prepare(&workspace->engine,
                    &workspace->carrier[variant], &input,
                    &workspace->fused[side][variant]) != 0 ||
                asian_full_risk_gamma_triple_request_prepare(
                    &workspace->engine, &workspace->carrier[variant], &input,
                    &workspace->triple[side][variant]) != 0 ||
                asian_affine_family_full_risk_k1_prepared_price(
                    &workspace->current[side][variant],
                    &workspace->current_expected[side][variant]) != 0 ||
                asian_full_risk_gamma_prepared_price(
                    &workspace->fused[side][variant],
                    &workspace->fused_expected[side][variant]) != 0 ||
                asian_full_risk_gamma_triple_prepared_price(
                    &workspace->triple[side][variant],
                    &workspace->triple_expected[side][variant]) != 0 ||
                !parent_fields_equal(
                    &workspace->current_expected[side][variant],
                    &workspace->fused_expected[side][variant]) ||
                !isfinite(workspace->fused_expected[side][variant].call.gamma) ||
                !isfinite(workspace->triple_expected[side][variant].call.gamma))
                return -1;
            const int direct_call =
                (workspace->fused[side][variant].parity.flags &
                 ASIAN_GENUINE_MSFR_DIRECT_CALL) != 0u;
            if (direct_call != (side == 0u))
                return -1;

            const asian_full_risk_gamma_request_input_t reuse_input =
                request_input(n, side, variant, 1);
            if (asian_affine_family_full_risk_k1_request_prepare(
                    &workspace->engine, &workspace->carrier[0],
                    &reuse_input.family, &workspace->current_scratch) != 0 ||
                asian_affine_family_full_risk_k1_prepared_price(
                    &workspace->current_scratch,
                    &workspace->current_reuse_expected[side][variant]) != 0 ||
                asian_full_risk_gamma_request_prepare(&workspace->engine,
                    &workspace->carrier[0], &reuse_input,
                    &workspace->fused_scratch) != 0 ||
                asian_full_risk_gamma_prepared_price(&workspace->fused_scratch,
                    &workspace->fused_reuse_expected[side][variant]) != 0 ||
                asian_full_risk_gamma_triple_request_prepare(
                    &workspace->engine, &workspace->carrier[0], &reuse_input,
                    &workspace->triple_scratch) != 0 ||
                asian_full_risk_gamma_triple_prepared_price(
                    &workspace->triple_scratch,
                    &workspace->triple_reuse_expected[side][variant]) != 0)
                return -1;
        }
    }
    return 0;
}

static void warm_routes(const asian_meta_affine_route_t *routes, uint32_t n)
{
    warm_bytes(routes, n * sizeof(routes[0]));
    for (uint32_t fixing = 0; fixing < n; ++fixing)
        warm_bytes(routes[fixing].map, sizeof(*routes[fixing].map));
}

static void condition(uint32_t n, enum workload workload, enum lifecycle life,
                      enum cache_mode cache, uint32_t side, uint32_t variant)
{
    if (cache == PRESSURE_32K) {
        pressure_32k();
        return;
    }
    const asian_affine_family_xgrowth_carrier_t *carrier = life == FRESH ?
        &workspace->carrier_scratch : &workspace->carrier[life == REUSED ? 0u : variant];
    warm_bytes(carrier->x, sizeof(carrier->x));
    warm_bytes(carrier->growth, sizeof(carrier->growth));
    if (workload == CURRENT_RISK) {
        const asian_affine_family_full_risk_k1_request_t *request =
            life == PREPARED ? &workspace->current[side][variant] :
                               &workspace->current_scratch;
        warm_routes(request->routes, n);
        warm_bytes(request->controls.forward_weights,
                   n * sizeof(request->controls.forward_weights[0]));
        warm_bytes(&request->context, sizeof(request->context));
        warm_bytes(&request->parity, sizeof(request->parity));
    } else if (workload == FUSED_GAMMA) {
        const asian_full_risk_gamma_request_t *request = life == PREPARED ?
            &workspace->fused[side][variant] : &workspace->fused_scratch;
        warm_routes(request->routes, n);
        warm_bytes(request->controls.forward_weights,
                   n * sizeof(request->controls.forward_weights[0]));
        warm_bytes(&request->context, sizeof(request->context));
        warm_bytes(&request->parity, sizeof(request->parity));
    } else {
        const asian_full_risk_gamma_triple_request_t *request =
            life == PREPARED ? &workspace->triple[side][variant] :
                               &workspace->triple_scratch;
        for (uint32_t bump = 0; bump < 3u; ++bump) {
            warm_routes(request->request[bump].routes, n);
            warm_bytes(request->request[bump].controls.forward_weights,
                       n * sizeof(float));
            warm_bytes(&request->request[bump].context,
                       sizeof(request->request[bump].context));
            warm_bytes(&request->request[bump].parity,
                       sizeof(request->request[bump].parity));
        }
    }
}

static timing_t observe(uint32_t n, enum workload workload,
                        enum lifecycle life, enum cache_mode cache,
                        uint32_t side, uint32_t variant)
{
    const int reuse = life == REUSED;
    const uint32_t carrier_variant = reuse ? 0u : variant;
    const asian_full_risk_gamma_request_input_t input =
        request_input(n, side, variant, reuse);
    const asian_affine_family_carrier_input_t ci = reuse ?
        carrier_input(n, 0u) : carrier_input(n, variant);
    int status = 0;
    condition(n, workload, life, cache, side, carrier_variant);
    const uint64_t wall0 = wall_now();
    const uint64_t ticks0 = tsc_begin();
    if (life == FRESH)
        status = asian_affine_family_xgrowth_carrier_prepare(
            &workspace->engine, &ci, &workspace->carrier_scratch);
    const asian_affine_family_xgrowth_carrier_t *carrier = life == FRESH ?
        &workspace->carrier_scratch : &workspace->carrier[carrier_variant];
    if (workload == CURRENT_RISK) {
        asian_affine_family_full_risk_k1_request_t *request = life == PREPARED ?
            &workspace->current[side][variant] : &workspace->current_scratch;
        if (status == 0 && life != PREPARED)
            status = asian_affine_family_full_risk_k1_request_prepare(
                &workspace->engine, carrier, &input.family, request);
        if (status == 0)
            status = asian_affine_family_full_risk_k1_prepared_price(
                request, &workspace->current_output);
    } else if (workload == FUSED_GAMMA) {
        asian_full_risk_gamma_request_t *request = life == PREPARED ?
            &workspace->fused[side][variant] : &workspace->fused_scratch;
        if (status == 0 && life != PREPARED)
            status = asian_full_risk_gamma_request_prepare(
                &workspace->engine, carrier, &input, request);
        if (status == 0)
            status = asian_full_risk_gamma_prepared_price(
                request, &workspace->gamma_output);
    } else {
        asian_full_risk_gamma_triple_request_t *request = life == PREPARED ?
            &workspace->triple[side][variant] : &workspace->triple_scratch;
        if (status == 0 && life != PREPARED)
            status = asian_full_risk_gamma_triple_request_prepare(
                &workspace->engine, carrier, &input, request);
        if (status == 0)
            status = asian_full_risk_gamma_triple_prepared_price(
                request, &workspace->gamma_output);
    }
    const uint64_t ticks1 = tsc_end();
    const uint64_t wall1 = wall_now();
    if (status != 0)
        abort();

    if (workload == CURRENT_RISK) {
        sink_value = hash_bytes(sink_value, &workspace->current_output,
                                sizeof(workspace->current_output));
        const asian_affine_family_full_risk_k1_output_t *expected =
            life == REUSED ? &workspace->current_reuse_expected[side][variant] :
                             &workspace->current_expected[side][variant];
        if (memcmp(&workspace->current_output, expected,
                   sizeof(*expected)) != 0)
            abort();
    } else {
        sink_value = hash_bytes(sink_value, &workspace->gamma_output,
                                sizeof(workspace->gamma_output));
        const asian_full_risk_gamma_output_t *expected;
        if (workload == FUSED_GAMMA)
            expected = life == REUSED ?
                &workspace->fused_reuse_expected[side][variant] :
                &workspace->fused_expected[side][variant];
        else
            expected = life == REUSED ?
                &workspace->triple_reuse_expected[side][variant] :
                &workspace->triple_expected[side][variant];
        if (memcmp(&workspace->gamma_output, expected, sizeof(*expected)) != 0)
            abort();
    }
    return (timing_t){(double)(wall1 - wall0),
                      (double)(ticks1 - ticks0)};
}

static pair_result_t paired(uint32_t n, enum lifecycle life,
                            enum cache_mode cache, uint32_t side,
                            enum workload first, enum workload second)
{
    double wall[2][SAMPLES], tsc[2][SAMPLES];
    for (uint32_t group = 0; group < WARMUPS + SAMPLES; ++group) {
        const enum workload order[4] = {
            (group & 1u) ? second : first,
            (group & 1u) ? first : second,
            (group & 1u) ? first : second,
            (group & 1u) ? second : first,
        };
        double sum_wall[2] = {0.0, 0.0}, sum_tsc[2] = {0.0, 0.0};
        for (uint32_t slot = 0; slot < 4u; ++slot) {
            const uint32_t candidate = order[slot] == first ? 0u : 1u;
            const timing_t value = observe(n, order[slot], life, cache, side,
                                           (group + slot) & 1u);
            sum_wall[candidate] += value.wall;
            sum_tsc[candidate] += value.tsc;
        }
        if (group >= WARMUPS) {
            const uint32_t sample = group - WARMUPS;
            for (uint32_t candidate = 0; candidate < 2u; ++candidate) {
                wall[candidate][sample] = 0.5 * sum_wall[candidate];
                tsc[candidate][sample] = 0.5 * sum_tsc[candidate];
            }
        }
    }
    pair_result_t result;
    for (uint32_t candidate = 0; candidate < 2u; ++candidate) {
        result.candidate[candidate].wall = median(wall[candidate], SAMPLES);
        result.candidate[candidate].tsc = median(tsc[candidate], SAMPLES);
    }
    return result;
}

static int preflight(void)
{
    for (uint32_t ni = 0; ni < 2u; ++ni) {
        const uint32_t n = ni == 0u ? 64u : 256u;
        if (setup(n) != 0)
            return -1;
        for (uint32_t side = 0; side < SIDES; ++side)
            for (uint32_t variant = 0; variant < INPUTS; ++variant) {
                asian_full_risk_gamma_output_t scalar
                    __attribute__((aligned(64)));
                if (asian_full_risk_gamma_scalar_oracle(
                        &workspace->fused[side][variant], &scalar) != 0 ||
                    memcmp(&scalar, &workspace->fused_expected[side][variant],
                           sizeof(scalar)) != 0)
                    return -1;
            }
    }
    if (setup(64u) != 0)
        return -1;
    for (uint32_t side = 0; side < SIDES; ++side) {
        float strike = 100.0f;
        const asian_full_risk_gamma_request_input_t input = {
            .family = {
                .s0 = 100.0, .rate = 0.03, .dividend_yield = 0.0,
                .sigma = 0.20, .maturity = 1.0, .future_fixings = 64u,
                .strikes = &strike, .strike_count = 1u,
                .workload = ASIAN_AFFINE_FAMILY_PRICE,
            },
            .gamma_bump_fraction = 0.01,
            .workload = ASIAN_FULL_RISK_PRICE_DELTA_GAMMA_VEGA_RHO,
        };
        asian_full_risk_gamma_request_t *request =
            &workspace->fused_scratch;
        asian_full_risk_gamma_output_t *output = &workspace->gamma_output;
        if (asian_full_risk_gamma_request_prepare(&workspace->engine,
                &workspace->carrier[0], &input, request) != 0 ||
            asian_full_risk_gamma_prepared_price(request, output) != 0 ||
            output->call.gamma != 0x1.12c6p-5 ||
            memcmp(&output->call.gamma, &output->put.gamma,
                   sizeof(double)) != 0)
            return -1;
        const double error = fabs(output->call.gamma -
                                  gamma_reference[side]);
        const double half_width = 0.5 *
            (gamma_reference_high[side] - gamma_reference_low[side]);
        if (!(error > 0.0) || !(half_width < 0.10 * error))
            return -1;
    }
    puts("full_risk_gamma_benchmark_preflight PASS N=64,256 sides=call,put "
         "parent_fields_exact=YES scalar_gamma_exact=YES "
         "independent_reference=RESOLVED_STRONG_10_PERCENT identity=PASS");
    return 0;
}

static int print_principal_reference_error(void)
{
    if (setup(64u) != 0)
        return -1;
    float strike = 100.0f;
    const asian_full_risk_gamma_request_input_t input = {
        .family = {
            .s0 = 100.0, .rate = 0.03, .dividend_yield = 0.0,
            .sigma = 0.20, .maturity = 1.0, .future_fixings = 64u,
            .strikes = &strike, .strike_count = 1u,
            .workload = ASIAN_AFFINE_FAMILY_PRICE,
        },
        .gamma_bump_fraction = 0.01,
        .workload = ASIAN_FULL_RISK_PRICE_DELTA_GAMMA_VEGA_RHO,
    };
    if (asian_full_risk_gamma_request_prepare(&workspace->engine,
            &workspace->carrier[0], &input,
            &workspace->fused_scratch) != 0 ||
        asian_full_risk_gamma_prepared_price(&workspace->fused_scratch,
            &workspace->gamma_output) != 0)
        return -1;
    const double observed[SIDES] = {
        workspace->gamma_output.call.gamma,
        workspace->gamma_output.put.gamma,
    };
    for (uint32_t side = 0; side < SIDES; ++side)
        printf("N64_GAMMA_ACCURACY side=%s Gamma_h=%.17g "
               "effective_bump=%.17g reference_Gamma_h=%.17g "
               "reference_ci_low=%.17g reference_ci_high=%.17g "
               "reference_signed_error=%.17g\n", side_name[side],
               observed[side],
               workspace->gamma_output.call.effective_spot_bump,
               gamma_reference[side], gamma_reference_low[side],
               gamma_reference_high[side],
               observed[side] - gamma_reference[side]);
    return 0;
}

static int timing(void)
{
    double ratios_wall[24], ratios_tsc[24];
    uint32_t ratio_count = 0u;
    int cell_failure = 0;
    if (print_principal_reference_error() != 0)
        return -1;
    printf("N side lifecycle cache current_full_risk_wall_ns "
           "fused_gamma_wall_ns triple_reprice_wall_ns "
           "fused_pair2_wall_ns current_tsc_ticks fused_tsc_ticks "
           "triple_tsc_ticks fused_pair2_tsc_ticks fused_gamma_tax_ratio "
           "fused_gamma_tax_pct fused_gamma_tsc_tax_ratio "
           "fused_vs_triple_wall_speedup fused_vs_triple_tsc_speedup "
           "identity\n");
    for (uint32_t ni = 0; ni < 2u; ++ni) {
        const uint32_t n = ni == 0u ? 64u : 256u;
        if (setup(n) != 0)
            return -1;
        for (uint32_t side = 0; side < SIDES; ++side)
            for (uint32_t life = 0; life < LIFECYCLE_COUNT; ++life)
                for (uint32_t cache = 0; cache < CACHE_COUNT; ++cache) {
                    const pair_result_t tax = paired(n, life, cache, side,
                                                    CURRENT_RISK, FUSED_GAMMA);
                    const pair_result_t triple = paired(n, life, cache, side,
                                                       FUSED_GAMMA, TRIPLE_GAMMA);
                    const double wall_ratio =
                        tax.candidate[1].wall / tax.candidate[0].wall;
                    const double tsc_ratio =
                        tax.candidate[1].tsc / tax.candidate[0].tsc;
                    ratios_wall[ratio_count] = wall_ratio;
                    ratios_tsc[ratio_count++] = tsc_ratio;
                    if (wall_ratio > 1.40 || tsc_ratio > 1.40)
                        cell_failure = 1;
                    printf("%u %s %s %s %.1f %.1f %.1f %.1f %.1f %.1f "
                           "%.1f %.1f %.6f %.3f %.6f %.6f %.6f PASS\n",
                           n, side_name[side], lifecycle_name[life],
                           cache_name[cache], tax.candidate[0].wall,
                           tax.candidate[1].wall, triple.candidate[1].wall,
                           triple.candidate[0].wall, tax.candidate[0].tsc,
                           tax.candidate[1].tsc, triple.candidate[1].tsc,
                           triple.candidate[0].tsc, wall_ratio,
                           100.0 * (wall_ratio - 1.0), tsc_ratio,
                           triple.candidate[1].wall /
                           triple.candidate[0].wall,
                           triple.candidate[1].tsc /
                           triple.candidate[0].tsc);
                    if (life == FRESH && cache == CANDIDATE_WARM)
                        printf("N%u FULL_RISK_GAMMA FRESH side=%s wall_ns=%.1f "
                               "tsc_ticks=%.1f Gamma_h=%a effective_bump=%a "
                               "identity=PASS\n",
                               n, side_name[side],
                               tax.candidate[1].wall, tax.candidate[1].tsc,
                               workspace->fused_expected[side][0].call.gamma,
                               workspace->fused_expected[side][0].call.
                                   effective_spot_bump);
                }
    }
    const double aggregate_wall = median(ratios_wall, ratio_count);
    const double aggregate_tsc = median(ratios_tsc, ratio_count);
    const int ready = !cell_failure && aggregate_wall <= 1.25 &&
                      aggregate_tsc <= 1.25;
    printf("gamma_performance_decision aggregate_wall_ratio=%.6f "
           "aggregate_tsc_ratio=%.6f max_cell_limit=1.400000 "
           "independent_reference=RESOLVED_STRONG_10_PERCENT decision=%s\n",
           aggregate_wall, aggregate_tsc,
           ready ? "FUSED_GAMMA_READY" : "GAMMA_DIAGNOSTIC_ONLY");
    return 0;
}

int main(int argc, char **argv)
{
    workspace = a64(sizeof(*workspace));
    if (workspace == NULL ||
        asian_affine_family_engine_create(&workspace->engine) != 0)
        return 1;
    int result = 0;
    if (argc == 2 && strcmp(argv[1], "--check") == 0) {
        result = preflight();
    } else if (argc == 4 && strcmp(argv[1], "--native-check") == 0 &&
               strcmp(argv[2], "--cpu") == 0) {
        const unsigned cpu = (unsigned)strtoul(argv[3], NULL, 10);
        if (cpu != 0u || !sapphire_rapids() || pin_cpu(cpu) != 0) {
            fprintf(stderr, "Sapphire Rapids CPU=0 is required\n");
            result = -1;
        } else {
            result = preflight();
        }
    } else if (argc == 4 && strcmp(argv[1], "--timing") == 0 &&
               strcmp(argv[2], "--cpu") == 0) {
        const unsigned cpu = (unsigned)strtoul(argv[3], NULL, 10);
        if (cpu != 0u || !sapphire_rapids() || pin_cpu(cpu) != 0) {
            fprintf(stderr, "Sapphire Rapids CPU=0 is required\n");
            result = -1;
        } else {
            result = timing();
        }
    } else {
        fprintf(stderr, "usage: %s --check | --native-check --cpu 0 | "
                        "--timing --cpu 0\n", argv[0]);
        result = -1;
    }
    asian_affine_family_engine_destroy(&workspace->engine);
    free(workspace);
    return result == 0 ? 0 : 1;
}
