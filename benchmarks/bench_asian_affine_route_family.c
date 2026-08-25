#define _GNU_SOURCE
#include "private/asian_affine_route_family_diag.h"

#include <cpuid.h>
#include <immintrin.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    WARMUPS = 16,
    SAMPLES = 101,
    BUILDER_SAMPLES = 5,
    GRID_CELLS = 20,
};

enum family_kind { FAMILY_ARITHMETIC = 0, FAMILY_GEOCV = 1 };
enum cache_mode { CACHE_WARM = 0, CACHE_PRESSURE = 1 };
enum candidate_kind { CAND_ARITH_AFFINE = 0, CAND_GEOCV_GENERIC = 1,
                      CAND_GEOCV_AFFINE = 2 };
enum lifecycle_kind { LIFE_CARRIER = 0, LIFE_REQUEST = 1, LIFE_PRICE = 2,
                      LIFE_FRESH = 3, LIFE_REUSE = 4, LIFE_COUNT = 5 };

typedef struct { double wall, tsc; } timing_t;
typedef struct { timing_t left, right; } pair_timing_t;

typedef struct __attribute__((aligned(64))) {
    asian_affine_family_engine_t engine;
    asian_affine_family_generic_oracle_t oracle;
    asian_affine_family_growth_carrier_t growth;
    asian_affine_family_growth_carrier_t growth_scratch;
    asian_affine_family_xgrowth_carrier_t xgrowth;
    asian_affine_family_xgrowth_carrier_t xgrowth_scratch[2];
    asian_affine_family_arithmetic_request_t arithmetic[2];
    asian_affine_family_arithmetic_request_t arithmetic_scratch;
    asian_affine_family_geocv_request_t geocv[2];
    asian_affine_family_geocv_request_t geocv_scratch[2];
    asian_genuine_strip_output_t output[3];
    uint32_t pressure[8192];
} workspace_t;

static workspace_t *workspace;
static volatile uint64_t sink;
static int identity_pass = 1;
static float principal_strike = 100.0f;
static float reuse_strike = 110.0f;

static const asian_affine_family_carrier_input_t principal_market = {
    0.03, 0.0, 0.20, 1.0, 64u};
static const asian_affine_family_carrier_input_t alternate_market = {
    -0.01, 0.015, 0.35, 0.75, 64u};

static asian_affine_family_request_input_t lifecycle_input(uint32_t variant,
                                                            int fresh)
{
    const asian_affine_family_carrier_input_t *market =
        fresh && variant != 0u ? &alternate_market : &principal_market;
    asian_affine_family_request_input_t out = {
        .s0 = variant == 0u ? 100.0 : 117.0,
        .rate = market->rate,
        .dividend_yield = market->dividend_yield,
        .sigma = market->sigma,
        .maturity = market->maturity,
        .future_fixings = 64u,
        .completed_fixings = 0u,
        .initial_arithmetic_sum = 0.0,
        .past_log_sum = 0.0,
        .strikes = variant == 0u ? &principal_strike : &reuse_strike,
        .strike_count = 1u,
        .workload = ASIAN_AFFINE_FAMILY_PRICE,
    };
    return out;
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
    unsigned aux;
    const uint64_t value = __rdtscp(&aux);
    _mm_lfence();
    return value;
}

static int compare_double(const void *a, const void *b)
{
    const double aa = *(const double *)a, bb = *(const double *)b;
    return (aa > bb) - (aa < bb);
}

static double median(double *values, size_t count)
{
    qsort(values, count, sizeof(values[0]), compare_double);
    if ((count & 1u) != 0u)
        return values[count / 2u];
    return 0.5 * (values[count / 2u - 1u] + values[count / 2u]);
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

static void pressure_32k(void)
{
    uint64_t value = sink;
    for (uint32_t i = 0; i < 8192u; i += 16u) {
        workspace->pressure[i] += 1u;
        value += workspace->pressure[i];
    }
    sink = value;
}

static void warm_bytes(const void *memory, size_t bytes)
{
    const volatile unsigned char *p = memory;
    uint64_t value = sink;
    for (size_t offset = 0; offset < bytes; offset += 64u)
        value += p[offset];
    sink = value;
}

static int pin_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

static int is_sapphire_rapids(void)
{
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid(1u, &eax, &ebx, &ecx, &edx)) return 0;
    const unsigned family = ((eax >> 8) & 15u) + ((eax >> 20) & 255u);
    const unsigned model = ((eax >> 4) & 15u) | ((eax >> 12) & 240u);
    return family == 6u && model == 143u;
}

static void make_strikes(float out[32], uint32_t count, uint32_t seed)
{
    for (uint32_t i = 0; i < count; ++i)
        out[i] = 80.0f + (float)((i * 13u + seed * 7u) % 31u) * 1.5f;
}

static asian_affine_family_request_input_t grid_request(
    uint32_t n, const float *strikes, uint32_t k, uint32_t workload)
{
    const asian_affine_family_request_input_t out = {
        .s0 = 100.0, .rate = 0.03, .dividend_yield = 0.0,
        .sigma = 0.20, .maturity = 1.0, .future_fixings = n,
        .completed_fixings = 0u, .initial_arithmetic_sum = 0.0,
        .past_log_sum = 0.0, .strikes = strikes, .strike_count = k,
        .workload = (enum asian_affine_family_workload)workload,
    };
    return out;
}

static void condition_grid(enum family_kind family, int affine,
                           enum cache_mode cache)
{
    if (cache == CACHE_PRESSURE) {
        pressure_32k();
        return;
    }
    if (family == FAMILY_ARITHMETIC) {
        warm_bytes(&workspace->arithmetic[affine],
                   sizeof(workspace->arithmetic[affine]));
        warm_bytes(workspace->growth.growth, sizeof(workspace->growth.growth));
    } else {
        warm_bytes(&workspace->geocv[affine],
                   sizeof(workspace->geocv[affine]));
        warm_bytes(workspace->xgrowth.x, sizeof(workspace->xgrowth.x));
        warm_bytes(workspace->xgrowth.growth,
                   sizeof(workspace->xgrowth.growth));
    }
}

static timing_t observe_grid(enum family_kind family, int affine,
                             enum cache_mode cache)
{
    asian_genuine_strip_output_t output __attribute__((aligned(64)));
    memset(&output, 0, sizeof(output));
    condition_grid(family, affine, cache);
    const uint64_t w0 = wall_now(), t0 = tsc_begin();
    const int status = family == FAMILY_ARITHMETIC ?
        asian_affine_family_arithmetic_prepared_price(
            &workspace->arithmetic[affine], &output) :
        asian_affine_family_geocv_prepared_price(
            &workspace->geocv[affine], &output);
    const uint64_t t1 = tsc_end(), w1 = wall_now();
    if (status != 0) abort();
    sink = hash_bytes(sink, &output, sizeof(output));
    return (timing_t){(double)(w1 - w0), (double)(t1 - t0)};
}

static timing_t paired_grid(enum family_kind family, enum cache_mode cache)
{
    double generic_w[SAMPLES], generic_t[SAMPLES];
    double affine_w[SAMPLES], affine_t[SAMPLES];
    for (uint32_t quartet = 0; quartet < WARMUPS + SAMPLES; ++quartet) {
        double gw = 0.0, gt = 0.0, aw = 0.0, at = 0.0;
        const int affine_first = (quartet & 1u) != 0u;
        for (uint32_t observation = 0; observation < 4u; ++observation) {
            const int affine = observation == 0u || observation == 3u ?
                affine_first : !affine_first;
            const timing_t value = observe_grid(family, affine, cache);
            if (affine) { aw += value.wall; at += value.tsc; }
            else { gw += value.wall; gt += value.tsc; }
        }
        if (quartet >= WARMUPS) {
            const uint32_t sample = quartet - WARMUPS;
            generic_w[sample] = 0.5 * gw;
            generic_t[sample] = 0.5 * gt;
            affine_w[sample] = 0.5 * aw;
            affine_t[sample] = 0.5 * at;
        }
    }
    return (timing_t){
        median(generic_w, SAMPLES) / median(affine_w, SAMPLES),
        median(generic_t, SAMPLES) / median(affine_t, SAMPLES)};
}

static int prepare_grid_cell(enum family_kind family, uint32_t n, uint32_t k,
                             uint32_t workload)
{
    float strikes[32]; make_strikes(strikes, k, n + k + workload);
    const asian_affine_family_carrier_input_t market =
        {0.03, 0.0, 0.20, 1.0, n};
    const asian_affine_family_request_input_t request =
        grid_request(n, strikes, k, workload);
    asian_genuine_strip_output_t generic __attribute__((aligned(64)));
    asian_genuine_strip_output_t affine __attribute__((aligned(64)));
    if (family == FAMILY_ARITHMETIC) {
        if (asian_affine_family_growth_carrier_prepare(
                &workspace->engine, &market, &workspace->growth) != 0 ||
            asian_affine_family_arithmetic_request_prepare_growth(
                &workspace->engine, &workspace->oracle, &workspace->growth,
                &request, ASIAN_AFFINE_FAMILY_GENERIC,
                &workspace->arithmetic[0]) != 0 ||
            asian_affine_family_arithmetic_request_prepare_growth(
                &workspace->engine, &workspace->oracle, &workspace->growth,
                &request, ASIAN_AFFINE_FAMILY_AFFINE,
                &workspace->arithmetic[1]) != 0 ||
            asian_affine_family_arithmetic_prepared_price(
                &workspace->arithmetic[0], &generic) != 0 ||
            asian_affine_family_arithmetic_prepared_price(
                &workspace->arithmetic[1], &affine) != 0)
            return -1;
    } else {
        if (asian_affine_family_xgrowth_carrier_prepare(
                &workspace->engine, &market, &workspace->xgrowth) != 0 ||
            asian_affine_family_geocv_request_prepare(
                &workspace->engine, &workspace->oracle, &workspace->xgrowth,
                &request, ASIAN_AFFINE_FAMILY_GENERIC,
                &workspace->geocv[0]) != 0 ||
            asian_affine_family_geocv_request_prepare(
                &workspace->engine, &workspace->oracle, &workspace->xgrowth,
                &request, ASIAN_AFFINE_FAMILY_AFFINE,
                &workspace->geocv[1]) != 0 ||
            asian_affine_family_geocv_prepared_price(
                &workspace->geocv[0], &generic) != 0 ||
            asian_affine_family_geocv_prepared_price(
                &workspace->geocv[1], &affine) != 0)
            return -1;
    }
    if (memcmp(&generic, &affine, sizeof(generic)) != 0)
        return -1;
    return 0;
}

static int benchmark_grid(void)
{
    static const uint32_t ns[] = {16u, 32u, 64u, 128u, 256u};
    static const uint32_t ks[] = {1u, 2u, 3u, 4u, 32u};
    puts("family N worst_wall worst_tsc median_wall median_tsc decision");
    for (uint32_t family = 0; family < 2u; ++family) {
        for (uint32_t ni = 0; ni < 5u; ++ni) {
            double wall[GRID_CELLS], tsc[GRID_CELLS];
            uint32_t cell = 0u;
            for (uint32_t ki = 0; ki < 5u; ++ki) {
                for (uint32_t workload = 0; workload < 2u; ++workload) {
                    if (prepare_grid_cell((enum family_kind)family, ns[ni],
                                          ks[ki], workload) != 0)
                        return -1;
                    for (uint32_t cache = 0; cache < 2u; ++cache) {
                        const timing_t value = paired_grid(
                            (enum family_kind)family,
                            (enum cache_mode)cache);
                        wall[cell] = value.wall; tsc[cell] = value.tsc;
                        ++cell;
                    }
                }
            }
            double sorted_w[GRID_CELLS], sorted_t[GRID_CELLS];
            memcpy(sorted_w, wall, sizeof(wall));
            memcpy(sorted_t, tsc, sizeof(tsc));
            double worst_w = wall[0], worst_t = tsc[0];
            for (uint32_t i = 1; i < GRID_CELLS; ++i) {
                if (wall[i] < worst_w) worst_w = wall[i];
                if (tsc[i] < worst_t) worst_t = tsc[i];
            }
            const double med_w = median(sorted_w, GRID_CELLS);
            const double med_t = median(sorted_t, GRID_CELLS);
            const int affine = identity_pass && worst_w >= 0.99 &&
                worst_t >= 0.99 && med_w > 1.01 && med_t > 1.01;
            printf("%s %u %.6f %.6f %.6f %.6f %s\n",
                family == FAMILY_ARITHMETIC ? "arithmetic" : "geocv",
                ns[ni], worst_w, worst_t, med_w, med_t,
                affine ? "AFFINE_ROUTE" : "GENERIC_ROUTE");
        }
    }
    return 0;
}

static int invoke_lifecycle(enum candidate_kind candidate,
                            enum lifecycle_kind lifecycle, uint32_t variant)
{
    const int geocv = candidate != CAND_ARITH_AFFINE;
    const enum asian_affine_family_provider provider =
        candidate == CAND_GEOCV_GENERIC ? ASIAN_AFFINE_FAMILY_GENERIC :
                                         ASIAN_AFFINE_FAMILY_AFFINE;
    const asian_affine_family_request_input_t reuse =
        lifecycle_input(variant, 0);
    const asian_affine_family_request_input_t fresh =
        lifecycle_input(variant, 1);
    const asian_affine_family_carrier_input_t *market =
        variant == 0u ? &principal_market : &alternate_market;
    asian_genuine_strip_output_t *output = &workspace->output[candidate];
    if (!geocv) {
        switch (lifecycle) {
        case LIFE_CARRIER:
            return asian_affine_family_growth_carrier_prepare(
                &workspace->engine, market, &workspace->growth_scratch);
        case LIFE_REQUEST:
            return asian_affine_family_arithmetic_request_prepare_growth(
                &workspace->engine, &workspace->oracle, &workspace->growth,
                &reuse, provider, &workspace->arithmetic_scratch);
        case LIFE_PRICE:
            return asian_affine_family_arithmetic_prepared_price(
                &workspace->arithmetic[1], output);
        case LIFE_FRESH:
            if (asian_affine_family_growth_carrier_prepare(
                    &workspace->engine, market,
                    &workspace->growth_scratch) != 0)
                return -1;
            if (asian_affine_family_arithmetic_request_prepare_growth(
                    &workspace->engine, &workspace->oracle,
                    &workspace->growth_scratch, &fresh, provider,
                    &workspace->arithmetic_scratch) != 0)
                return -1;
            return asian_affine_family_arithmetic_prepared_price(
                &workspace->arithmetic_scratch, output);
        case LIFE_REUSE:
            if (asian_affine_family_arithmetic_request_prepare_growth(
                    &workspace->engine, &workspace->oracle,
                    &workspace->growth, &reuse, provider,
                    &workspace->arithmetic_scratch) != 0)
                return -1;
            return asian_affine_family_arithmetic_prepared_price(
                &workspace->arithmetic_scratch, output);
        case LIFE_COUNT: return -1;
        }
    } else {
        const uint32_t index = candidate == CAND_GEOCV_GENERIC ? 0u : 1u;
        switch (lifecycle) {
        case LIFE_CARRIER:
            return asian_affine_family_xgrowth_carrier_prepare(
                &workspace->engine, market, &workspace->xgrowth_scratch[index]);
        case LIFE_REQUEST:
            return asian_affine_family_geocv_request_prepare(
                &workspace->engine, &workspace->oracle, &workspace->xgrowth,
                &reuse, provider, &workspace->geocv_scratch[index]);
        case LIFE_PRICE:
            return asian_affine_family_geocv_prepared_price(
                &workspace->geocv[index], output);
        case LIFE_FRESH:
            if (asian_affine_family_xgrowth_carrier_prepare(
                    &workspace->engine, market,
                    &workspace->xgrowth_scratch[index]) != 0)
                return -1;
            if (asian_affine_family_geocv_request_prepare(
                    &workspace->engine, &workspace->oracle,
                    &workspace->xgrowth_scratch[index], &fresh, provider,
                    &workspace->geocv_scratch[index]) != 0)
                return -1;
            return asian_affine_family_geocv_prepared_price(
                &workspace->geocv_scratch[index], output);
        case LIFE_REUSE:
            if (asian_affine_family_geocv_request_prepare(
                    &workspace->engine, &workspace->oracle,
                    &workspace->xgrowth, &reuse, provider,
                    &workspace->geocv_scratch[index]) != 0)
                return -1;
            return asian_affine_family_geocv_prepared_price(
                &workspace->geocv_scratch[index], output);
        case LIFE_COUNT: return -1;
        }
    }
    return -1;
}

static void condition_lifecycle(enum candidate_kind candidate,
                                enum cache_mode cache)
{
    if (cache == CACHE_PRESSURE) { pressure_32k(); return; }
    if (candidate == CAND_ARITH_AFFINE) {
        warm_bytes(&workspace->growth, sizeof(workspace->growth));
        warm_bytes(&workspace->arithmetic[1], sizeof(workspace->arithmetic[1]));
    } else {
        const uint32_t index = candidate == CAND_GEOCV_GENERIC ? 0u : 1u;
        warm_bytes(&workspace->xgrowth, sizeof(workspace->xgrowth));
        warm_bytes(&workspace->geocv[index], sizeof(workspace->geocv[index]));
    }
}

static timing_t observe_lifecycle(enum candidate_kind candidate,
                                  enum lifecycle_kind lifecycle,
                                  enum cache_mode cache, uint32_t variant,
                                  uint32_t side)
{
    memset(&workspace->output[candidate], 0,
           sizeof(workspace->output[candidate]));
    condition_lifecycle(candidate, cache);
    const uint64_t w0 = wall_now(), t0 = tsc_begin();
    const int status = invoke_lifecycle(candidate, lifecycle, variant);
    const uint64_t t1 = tsc_end(), w1 = wall_now();
    if (status != 0) abort();
    const double selected = side == 0u ?
        workspace->output[candidate].values[0].call_price :
        workspace->output[candidate].values[0].put_price;
    sink = hash_bytes(sink, &selected, sizeof(selected));
    return (timing_t){(double)(w1 - w0), (double)(t1 - t0)};
}

static pair_timing_t lifecycle_pair(enum candidate_kind left,
                                    enum candidate_kind right,
                                    enum lifecycle_kind lifecycle,
                                    enum cache_mode cache, uint32_t side)
{
    double lw[SAMPLES], lt[SAMPLES], rw[SAMPLES], rt[SAMPLES];
    for (uint32_t quartet = 0; quartet < WARMUPS + SAMPLES; ++quartet) {
        timing_t lsum = {0}, rsum = {0};
        const int left_first = (quartet & 1u) == 0u;
        for (uint32_t observation = 0; observation < 4u; ++observation) {
            const int use_left = observation == 0u || observation == 3u ?
                left_first : !left_first;
            const uint32_t variant = (quartet + observation) & 1u;
            const timing_t value = observe_lifecycle(
                use_left ? left : right, lifecycle, cache, variant, side);
            if (use_left) {
                lsum.wall += value.wall; lsum.tsc += value.tsc;
            } else {
                rsum.wall += value.wall; rsum.tsc += value.tsc;
            }
        }
        if (quartet >= WARMUPS) {
            const uint32_t sample = quartet - WARMUPS;
            lw[sample] = 0.5 * lsum.wall; lt[sample] = 0.5 * lsum.tsc;
            rw[sample] = 0.5 * rsum.wall; rt[sample] = 0.5 * rsum.tsc;
        }
    }
    return (pair_timing_t){
        {median(lw, SAMPLES), median(lt, SAMPLES)},
        {median(rw, SAMPLES), median(rt, SAMPLES)}};
}

static const char *life_name(uint32_t life)
{
    static const char *names[] = {"carrier_prepare", "request_prepare",
        "prepared_price", "fresh_total", "reuse_total"};
    return names[life];
}

static int lifecycle_identity(void)
{
    const asian_affine_family_request_input_t input = lifecycle_input(0u, 0);
    if (asian_affine_family_growth_carrier_prepare(
            &workspace->engine, &principal_market, &workspace->growth) != 0 ||
        asian_affine_family_xgrowth_carrier_prepare(
            &workspace->engine, &principal_market, &workspace->xgrowth) != 0 ||
        asian_affine_family_arithmetic_request_prepare_xgrowth(
            &workspace->engine, &workspace->oracle, &workspace->xgrowth,
            &input, ASIAN_AFFINE_FAMILY_AFFINE,
            &workspace->arithmetic[0]) != 0 ||
        asian_affine_family_arithmetic_request_prepare_growth(
            &workspace->engine, &workspace->oracle, &workspace->growth, &input,
            ASIAN_AFFINE_FAMILY_AFFINE, &workspace->arithmetic[1]) != 0 ||
        asian_affine_family_geocv_request_prepare(
            &workspace->engine, &workspace->oracle, &workspace->xgrowth, &input,
            ASIAN_AFFINE_FAMILY_GENERIC, &workspace->geocv[0]) != 0 ||
        asian_affine_family_geocv_request_prepare(
            &workspace->engine, &workspace->oracle, &workspace->xgrowth, &input,
            ASIAN_AFFINE_FAMILY_AFFINE, &workspace->geocv[1]) != 0)
        return -1;
    for (uint32_t c = 0; c < 3u; ++c)
        if (invoke_lifecycle((enum candidate_kind)c, LIFE_PRICE, 0u) != 0)
            return -1;
    const asian_genuine_strip_value_t *a = &workspace->output[0].values[0];
    const asian_genuine_strip_value_t *g = &workspace->output[1].values[0];
    const asian_genuine_strip_value_t *f = &workspace->output[2].values[0];
    if (a->call_price != 0x1.57d186d85266p+2 ||
        a->put_price != 0x1.f07a78p+1 ||
        g->call_price != 0x1.563fd989e061ap+2 ||
        g->put_price != 0x1.ed571d631bf74p+1 ||
        memcmp(g, f, sizeof(*g)) != 0)
        return -1;
    return 0;
}

static int benchmark_lifecycle(void)
{
    double advisory_w[12], advisory_t[12];
    uint32_t advisory_count = 0u;
    puts("option cache lifecycle arithmetic_wall generic_geocv_wall affine_geocv_wall arithmetic_tsc generic_geocv_tsc affine_geocv_tsc arithmetic/affine_ratio generic/affine_speedup identity");
    for (uint32_t side = 0; side < 2u; ++side) {
        for (uint32_t cache = 0; cache < 2u; ++cache) {
            for (uint32_t life = 0; life < LIFE_COUNT; ++life) {
                const pair_timing_t arithmetic = lifecycle_pair(
                    CAND_ARITH_AFFINE, CAND_GEOCV_AFFINE,
                    (enum lifecycle_kind)life, (enum cache_mode)cache, side);
                const pair_timing_t generic = lifecycle_pair(
                    CAND_GEOCV_GENERIC, CAND_GEOCV_AFFINE,
                    (enum lifecycle_kind)life, (enum cache_mode)cache, side);
                const double arw = arithmetic.left.wall / arithmetic.right.wall;
                const double art = arithmetic.left.tsc / arithmetic.right.tsc;
                const double gew = generic.left.wall / generic.right.wall;
                const double get = generic.left.tsc / generic.right.tsc;
                printf("%s %s %s %.1f %.1f %.1f %.1f %.1f %.1f %.6f/%.6f %.6f/%.6f %s\n",
                    side == 0u ? "call" : "put",
                    cache == 0u ? "candidate_warm" : "pressure_32k",
                    life_name(life), arithmetic.left.wall, generic.left.wall,
                    generic.right.wall, arithmetic.left.tsc, generic.left.tsc,
                    generic.right.tsc, arw, art, gew, get,
                    identity_pass ? "PASS" : "FAIL");
                if (life >= LIFE_PRICE) {
                    advisory_w[advisory_count] = gew;
                    advisory_t[advisory_count] = get;
                    ++advisory_count;
                }
            }
        }
    }
    double worst_w = advisory_w[0], worst_t = advisory_t[0];
    double sorted_w[12], sorted_t[12];
    memcpy(sorted_w, advisory_w, sizeof(sorted_w));
    memcpy(sorted_t, advisory_t, sizeof(sorted_t));
    for (uint32_t i = 1; i < advisory_count; ++i) {
        if (advisory_w[i] < worst_w) worst_w = advisory_w[i];
        if (advisory_t[i] < worst_t) worst_t = advisory_t[i];
    }
    const double med_w = median(sorted_w, advisory_count);
    const double med_t = median(sorted_t, advisory_count);
    const int affine = identity_pass && worst_w >= 0.99 && worst_t >= 0.99 &&
        med_w > 1.01 && med_t > 1.01;
    printf("n64_lifecycle_advisory %s worst_wall=%.6f worst_tsc=%.6f median_wall=%.6f median_tsc=%.6f\n",
        affine ? "AFFINE_ROUTE" : "GENERIC_ROUTE", worst_w, worst_t,
        med_w, med_t);
    return 0;
}

static timing_t observe_builder(int affine)
{
    void *created = NULL;
    const uint64_t w0 = wall_now(), t0 = tsc_begin();
    const int status = affine ?
        asian_meta_affine_plan_create((asian_meta_affine_plan_t **)&created) :
        asian_meta_qsort_control_plan_create(
            (asian_meta_qsort_control_plan_t **)&created);
    const uint64_t t1 = tsc_end(), w1 = wall_now();
    if (status != 0 || created == NULL) abort();
    if (affine) asian_meta_affine_plan_destroy(created);
    else asian_meta_qsort_control_plan_destroy(created);
    return (timing_t){(double)(w1 - w0), (double)(t1 - t0)};
}

static void benchmark_builders(void)
{
    double gw[BUILDER_SAMPLES], gt[BUILDER_SAMPLES];
    double aw[BUILDER_SAMPLES], at[BUILDER_SAMPLES];
    for (uint32_t i = 0; i < BUILDER_SAMPLES; ++i) {
        const int affine_first = (i & 1u) != 0u;
        const timing_t first = observe_builder(affine_first);
        const timing_t second = observe_builder(!affine_first);
        const timing_t a = affine_first ? first : second;
        const timing_t g = affine_first ? second : first;
        aw[i] = a.wall; at[i] = a.tsc; gw[i] = g.wall; gt[i] = g.tsc;
    }
    puts("plan wall_ns tsc_ticks speedup");
    const double gmw = median(gw, BUILDER_SAMPLES);
    const double gmt = median(gt, BUILDER_SAMPLES);
    const double amw = median(aw, BUILDER_SAMPLES);
    const double amt = median(at, BUILDER_SAMPLES);
    printf("generic_qsort_plan %.0f %.0f 1.000000\n", gmw, gmt);
    printf("affine_meta_plan %.0f %.0f %.6f/%.6f\n",
           amw, amt, gmw / amw, gmt / amt);
}

static int initialize(void)
{
    workspace = aligned_alloc(64u, sizeof(*workspace));
    if (workspace == NULL) return -1;
    memset(workspace, 0, sizeof(*workspace));
    for (uint32_t i = 0; i < 8192u; ++i)
        workspace->pressure[i] = i * UINT32_C(2654435761) + 1u;
    if (asian_affine_family_engine_create(&workspace->engine) != 0 ||
        asian_affine_family_generic_oracle_create(&workspace->oracle) != 0 ||
        lifecycle_identity() != 0)
        return -1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--check") == 0) {
        if (initialize() != 0) return 1;
        puts("bounded_family_correctness PASS");
        return 0;
    }
    if (argc != 3 || strcmp(argv[1], "--cpu") != 0) {
        fprintf(stderr, "usage: %s --check | --cpu CPU\n", argv[0]);
        return 2;
    }
    const int cpu = atoi(argv[2]);
    if (cpu != 0 || pin_cpu(cpu) != 0 || !is_sapphire_rapids()) {
        fprintf(stderr, "CPU 0 on family-6/model-143 Sapphire Rapids required\n");
        return 2;
    }
    if (initialize() != 0) return 1;
    benchmark_builders();
    if (benchmark_grid() != 0 || lifecycle_identity() != 0 ||
        benchmark_lifecycle() != 0)
        return 1;
    return identity_pass ? 0 : 1;
}
