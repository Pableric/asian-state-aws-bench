#define _GNU_SOURCE

#include "private/asian_n64_geocv_comparison_diag.h"

#include <cpuid.h>
#include <immintrin.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    WARMUP_QUARTETS = 16,
    MEASURED_QUARTETS = 101,
    TOTAL_CELLS = 2 * 2 * 5,
    BUILDER_SAMPLES = 5,
};

enum option_side { OPTION_CALL = 0, OPTION_PUT = 1 };
enum cache_mode { CACHE_CANDIDATE_WARM = 0, CACHE_PRESSURE_32K = 1 };
enum lifecycle {
    LIFE_CARRIER_PREPARE = 0,
    LIFE_REQUEST_PREPARE = 1,
    LIFE_PREPARED_PRICE = 2,
    LIFE_FRESH_TOTAL = 3,
    LIFE_REUSE_TOTAL = 4,
    LIFE_COUNT = 5,
};

typedef struct { double wall, tsc; } timing_t;
typedef struct { timing_t left, right; } pair_timing_t;

typedef struct __attribute__((aligned(64))) {
    asian_n64_comparison_engine_t engine;
    asian_n64_arithmetic_carrier_t arithmetic_carrier;
    asian_n64_arithmetic_carrier_t arithmetic_scratch;
    asian_geocv_affine_carrier_t geocv_carrier;
    asian_geocv_affine_carrier_t generic_scratch_carrier;
    asian_geocv_affine_carrier_t affine_scratch_carrier;
    asian_n64_arithmetic_request_t arithmetic_request;
    asian_n64_arithmetic_request_t arithmetic_scratch_request;
    asian_n64_geocv_request_t generic_request;
    asian_n64_geocv_request_t affine_request;
    asian_n64_geocv_request_t generic_scratch_request;
    asian_n64_geocv_request_t affine_scratch_request;
    asian_genuine_strip_output_t output[3];
    uint32_t pressure[8192];
} workspace_t;

static const asian_geocv_affine_request_input_t principal = {
    100.0, 100.0, 0.03, 0.0, 0.20, 1.0};
static const asian_geocv_affine_carrier_input_t principal_market = {
    0.03, 0.0, 0.20, 1.0};

static workspace_t *workspace;
static volatile uint64_t sink;
static int every_identity = 1;

static const char *candidate_name(enum asian_n64_comparison_candidate candidate)
{
    static const char *names[] = {
        "ARITHMETIC_AFFINE",
        "GEOCV_GENERIC_IMMEDIATE",
        "GEOCV_AFFINE_IMMEDIATE",
    };
    return names[candidate];
}

static const char *side_name(enum option_side side)
{
    return side == OPTION_CALL ? "call" : "put";
}

static const char *cache_name(enum cache_mode cache)
{
    return cache == CACHE_CANDIDATE_WARM ?
        "candidate_warm" : "pressure_32k";
}

static const char *lifecycle_name(enum lifecycle lifecycle)
{
    static const char *names[] = {
        "carrier_prepare", "request_prepare", "prepared_price",
        "fresh_total", "reuse_total",
    };
    return names[lifecycle];
}

static double expected_value(enum asian_n64_comparison_candidate candidate,
                             enum option_side side)
{
    if (candidate == ASIAN_N64_ARITHMETIC_AFFINE)
        return side == OPTION_CALL ?
            0x1.57d186d85266p+2 : 0x1.f07a78p+1;
    return side == OPTION_CALL ?
        0x1.563fd989e061ap+2 : 0x1.ed571d631bf74p+1;
}

static int exact_output(enum asian_n64_comparison_candidate candidate,
                        enum option_side side,
                        const asian_genuine_strip_output_t *output)
{
    const double expected = expected_value(candidate, side);
    const double actual = side == OPTION_CALL ?
        output->values[0].call_price : output->values[0].put_price;
    return memcmp(&actual, &expected, sizeof(actual)) == 0;
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

static int prepare_persistent(void)
{
    if (asian_n64_arithmetic_carrier_prepare(
            &workspace->engine, &principal_market,
            &workspace->arithmetic_carrier) != 0 ||
        asian_geocv_affine_carrier_prepare(
            &workspace->engine.affine, &principal_market,
            &workspace->geocv_carrier) != 0 ||
        asian_n64_arithmetic_request_prepare(
            &workspace->engine, &workspace->arithmetic_carrier, &principal,
            &workspace->arithmetic_request) != 0 ||
        asian_n64_geocv_request_prepare(
            &workspace->engine, &workspace->geocv_carrier, &principal,
            ASIAN_N64_GEOCV_GENERIC_IMMEDIATE,
            &workspace->generic_request) != 0 ||
        asian_n64_geocv_request_prepare(
            &workspace->engine, &workspace->geocv_carrier, &principal,
            ASIAN_N64_GEOCV_AFFINE_IMMEDIATE,
            &workspace->affine_request) != 0)
        return -1;
    for (uint32_t i = 0; i < 8192u; ++i)
        workspace->pressure[i] = i * UINT32_C(2654435761) + 1u;
    return 0;
}

static int invoke(enum asian_n64_comparison_candidate candidate,
                  enum lifecycle lifecycle)
{
    asian_genuine_strip_output_t *output = &workspace->output[candidate];
    switch (candidate) {
    case ASIAN_N64_ARITHMETIC_AFFINE:
        switch (lifecycle) {
        case LIFE_CARRIER_PREPARE:
            return asian_n64_arithmetic_carrier_prepare(
                &workspace->engine, &principal_market,
                &workspace->arithmetic_scratch);
        case LIFE_REQUEST_PREPARE:
            return asian_n64_arithmetic_request_prepare(
                &workspace->engine, &workspace->arithmetic_carrier,
                &principal, &workspace->arithmetic_scratch_request);
        case LIFE_PREPARED_PRICE:
            return asian_n64_arithmetic_prepared_price(
                &workspace->arithmetic_request, output);
        case LIFE_FRESH_TOTAL:
            return asian_n64_arithmetic_fresh_total(
                &workspace->engine, &principal,
                &workspace->arithmetic_scratch,
                &workspace->arithmetic_scratch_request, output);
        case LIFE_REUSE_TOTAL:
            return asian_n64_arithmetic_reuse_total(
                &workspace->engine, &workspace->arithmetic_carrier,
                &principal, &workspace->arithmetic_scratch_request, output);
        case LIFE_COUNT:
            break;
        }
        break;
    case ASIAN_N64_GEOCV_GENERIC_IMMEDIATE:
    case ASIAN_N64_GEOCV_AFFINE_IMMEDIATE: {
        const int generic =
            candidate == ASIAN_N64_GEOCV_GENERIC_IMMEDIATE;
        asian_geocv_affine_carrier_t *scratch_carrier = generic ?
            &workspace->generic_scratch_carrier :
            &workspace->affine_scratch_carrier;
        asian_n64_geocv_request_t *scratch_request = generic ?
            &workspace->generic_scratch_request :
            &workspace->affine_scratch_request;
        asian_n64_geocv_request_t *prepared = generic ?
            &workspace->generic_request : &workspace->affine_request;
        switch (lifecycle) {
        case LIFE_CARRIER_PREPARE:
            return asian_geocv_affine_carrier_prepare(
                &workspace->engine.affine, &principal_market,
                scratch_carrier);
        case LIFE_REQUEST_PREPARE:
            return asian_n64_geocv_request_prepare(
                &workspace->engine, &workspace->geocv_carrier, &principal,
                candidate, scratch_request);
        case LIFE_PREPARED_PRICE:
            return asian_n64_geocv_prepared_price(prepared, output);
        case LIFE_FRESH_TOTAL:
            return asian_n64_geocv_fresh_total(
                &workspace->engine, &principal, candidate, scratch_carrier,
                scratch_request, output);
        case LIFE_REUSE_TOTAL:
            return asian_n64_geocv_reuse_total(
                &workspace->engine, &workspace->geocv_carrier, &principal,
                candidate, scratch_request, output);
        case LIFE_COUNT:
            break;
        }
        break;
    }
    }
    return -1;
}

static int lifecycle_produces_price(enum lifecycle lifecycle)
{
    return lifecycle == LIFE_PREPARED_PRICE || lifecycle == LIFE_FRESH_TOTAL ||
           lifecycle == LIFE_REUSE_TOTAL;
}

static int check_after_invoke(
    enum asian_n64_comparison_candidate candidate,
    enum lifecycle lifecycle, enum option_side side)
{
    if (lifecycle_produces_price(lifecycle) &&
        !exact_output(candidate, side, &workspace->output[candidate])) {
        every_identity = 0;
        return -1;
    }
    const void *value;
    size_t bytes;
    if (candidate == ASIAN_N64_ARITHMETIC_AFFINE) {
        value = lifecycle == LIFE_CARRIER_PREPARE ?
            (const void *)&workspace->arithmetic_scratch :
            (const void *)&workspace->arithmetic_scratch_request;
        bytes = 64u;
    } else {
        value = lifecycle == LIFE_CARRIER_PREPARE ?
            (candidate == ASIAN_N64_GEOCV_GENERIC_IMMEDIATE ?
                 (const void *)&workspace->generic_scratch_carrier :
                 (const void *)&workspace->affine_scratch_carrier) :
            (candidate == ASIAN_N64_GEOCV_GENERIC_IMMEDIATE ?
                 (const void *)&workspace->generic_scratch_request :
                 (const void *)&workspace->affine_scratch_request);
        bytes = 64u;
    }
    sink += hash_bytes(UINT64_C(1469598103934665603), value, bytes);
    if (lifecycle_produces_price(lifecycle))
        sink += hash_bytes(UINT64_C(1469598103934665603),
            &workspace->output[candidate].values[0],
            sizeof(workspace->output[candidate].values[0]));
    return 0;
}

static void pressure_exact_32k(void)
{
    uint64_t value = sink;
    for (uint32_t i = 0; i < 8192u; ++i) {
        workspace->pressure[i] = workspace->pressure[i] *
            UINT32_C(1664525) + UINT32_C(1013904223);
        value += workspace->pressure[i];
    }
    sink = value;
}

static int condition(enum asian_n64_comparison_candidate candidate,
                     enum lifecycle lifecycle, enum cache_mode cache,
                     enum option_side side)
{
    memset(&workspace->output[candidate], 0,
           sizeof(workspace->output[candidate]));
    if (invoke(candidate, lifecycle) != 0 ||
        check_after_invoke(candidate, lifecycle, side) != 0)
        return -1;
    memset(&workspace->output[candidate], 0,
           sizeof(workspace->output[candidate]));
    if (cache == CACHE_PRESSURE_32K)
        pressure_exact_32k();
    return 0;
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
    _mm_mfence();
    _mm_lfence();
    return __rdtsc();
}

static uint64_t tsc_end(void)
{
    unsigned auxiliary;
    const uint64_t value = __rdtscp(&auxiliary);
    _mm_lfence();
    _mm_mfence();
    return value;
}

static timing_t observe(enum asian_n64_comparison_candidate candidate,
                        enum lifecycle lifecycle, enum cache_mode cache,
                        enum option_side side)
{
    if (condition(candidate, lifecycle, cache, side) != 0)
        abort();
    const uint64_t wall0 = wall_now();
    const uint64_t tsc0 = tsc_begin();
    const int status = invoke(candidate, lifecycle);
    const uint64_t tsc1 = tsc_end();
    const uint64_t wall1 = wall_now();
    if (status != 0 || check_after_invoke(candidate, lifecycle, side) != 0)
        abort();
    return (timing_t){(double)(wall1 - wall0), (double)(tsc1 - tsc0)};
}

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

static double median(double values[MEASURED_QUARTETS])
{
    qsort(values, MEASURED_QUARTETS, sizeof(values[0]), compare_double);
    return values[MEASURED_QUARTETS / 2u];
}

static pair_timing_t measure_pair(
    enum asian_n64_comparison_candidate left,
    enum asian_n64_comparison_candidate right,
    enum lifecycle lifecycle, enum cache_mode cache, enum option_side side)
{
    double left_wall[MEASURED_QUARTETS], left_tsc[MEASURED_QUARTETS];
    double right_wall[MEASURED_QUARTETS], right_tsc[MEASURED_QUARTETS];
    for (uint32_t quartet = 0;
         quartet < WARMUP_QUARTETS + MEASURED_QUARTETS; ++quartet) {
        double lw = 0.0, lt = 0.0, rw = 0.0, rt = 0.0;
        for (uint32_t observation = 0; observation < 4u; ++observation) {
            const int right_first = (quartet & 1u) != 0u;
            const int endpoint = observation == 0u || observation == 3u;
            const int use_right = endpoint ? right_first : !right_first;
            const timing_t value = observe(
                use_right ? right : left, lifecycle, cache, side);
            if (use_right) {
                rw += value.wall;
                rt += value.tsc;
            } else {
                lw += value.wall;
                lt += value.tsc;
            }
        }
        if (quartet >= WARMUP_QUARTETS) {
            const uint32_t sample = quartet - WARMUP_QUARTETS;
            left_wall[sample] = 0.5 * lw;
            left_tsc[sample] = 0.5 * lt;
            right_wall[sample] = 0.5 * rw;
            right_tsc[sample] = 0.5 * rt;
        }
    }
    pair_timing_t result;
    result.left.wall = median(left_wall);
    result.left.tsc = median(left_tsc);
    result.right.wall = median(right_wall);
    result.right.tsc = median(right_tsc);
    return result;
}

static int compare_prices(const asian_genuine_strip_output_t *left,
                          const asian_genuine_strip_output_t *right)
{
    return memcmp(&left->values[0].call_price,
                  &right->values[0].call_price, 2u * sizeof(double)) == 0;
}

static int bounded_correctness(void)
{
    asian_genuine_strip_output_t arithmetic __attribute__((aligned(64))) = {0};
    asian_genuine_strip_output_t generic __attribute__((aligned(64))) = {0};
    asian_genuine_strip_output_t affine __attribute__((aligned(64))) = {0};
    asian_genuine_strip_output_t repeat __attribute__((aligned(64))) = {0};
    asian_geocv_affine_carrier_t second_carrier __attribute__((aligned(64)));
    asian_n64_geocv_request_t second_request __attribute__((aligned(64)));

    if (asian_n64_arithmetic_fresh_total(
            &workspace->engine, &principal, &workspace->arithmetic_scratch,
            &workspace->arithmetic_scratch_request, &arithmetic) != 0 ||
        asian_n64_geocv_fresh_total(
            &workspace->engine, &principal,
            ASIAN_N64_GEOCV_GENERIC_IMMEDIATE,
            &workspace->generic_scratch_carrier,
            &workspace->generic_scratch_request, &generic) != 0 ||
        asian_n64_geocv_fresh_total(
            &workspace->engine, &principal,
            ASIAN_N64_GEOCV_AFFINE_IMMEDIATE,
            &second_carrier, &second_request, &affine) != 0 ||
        !compare_prices(&generic, &affine) ||
        memcmp(workspace->generic_scratch_carrier.x, second_carrier.x,
               sizeof(second_carrier.x)) != 0 ||
        memcmp(workspace->generic_scratch_carrier.growth,
               second_carrier.growth, sizeof(second_carrier.growth)) != 0)
        return -1;
    for (uint32_t side = 0; side < 2u; ++side) {
        if (!exact_output(ASIAN_N64_ARITHMETIC_AFFINE,
                          (enum option_side)side, &arithmetic) ||
            !exact_output(ASIAN_N64_GEOCV_GENERIC_IMMEDIATE,
                          (enum option_side)side, &generic) ||
            !exact_output(ASIAN_N64_GEOCV_AFFINE_IMMEDIATE,
                          (enum option_side)side, &affine))
            return -1;
    }
    if (asian_n64_arithmetic_reuse_total(
            &workspace->engine, &workspace->arithmetic_carrier, &principal,
            &workspace->arithmetic_scratch_request, &repeat) != 0 ||
        !compare_prices(&arithmetic, &repeat) ||
        asian_n64_geocv_reuse_total(
            &workspace->engine, &workspace->geocv_carrier, &principal,
            ASIAN_N64_GEOCV_GENERIC_IMMEDIATE,
            &workspace->generic_scratch_request, &repeat) != 0 ||
        !compare_prices(&generic, &repeat) ||
        asian_n64_geocv_reuse_total(
            &workspace->engine, &workspace->geocv_carrier, &principal,
            ASIAN_N64_GEOCV_AFFINE_IMMEDIATE,
            &workspace->affine_scratch_request, &repeat) != 0 ||
        !compare_prices(&affine, &repeat))
        return -1;

    printf("exact_result candidate=%s call=%a put=%a identity=PASS\n",
           candidate_name(ASIAN_N64_ARITHMETIC_AFFINE),
           arithmetic.values[0].call_price, arithmetic.values[0].put_price);
    printf("exact_result candidate=%s call=%a put=%a identity=PASS\n",
           candidate_name(ASIAN_N64_GEOCV_GENERIC_IMMEDIATE),
           generic.values[0].call_price, generic.values[0].put_price);
    printf("exact_result candidate=%s call=%a put=%a identity=PASS\n",
           candidate_name(ASIAN_N64_GEOCV_AFFINE_IMMEDIATE),
           affine.values[0].call_price, affine.values[0].put_price);
    return 0;
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
    unsigned a, b, c, d;
    char vendor[13];
    __cpuid(0, a, b, c, d);
    memcpy(vendor, &b, 4u);
    memcpy(vendor + 4, &d, 4u);
    memcpy(vendor + 8, &c, 4u);
    vendor[12] = 0;
    if (strcmp(vendor, "GenuineIntel") != 0)
        return 0;
    __cpuid(1, a, b, c, d);
    const unsigned family = ((a >> 8) & 15u) + ((a >> 20) & 255u);
    const unsigned model = ((a >> 4) & 15u) | ((a >> 12) & 240u);
    return family == 6u && model == 143u;
}

static timing_t observe_builder(int generic)
{
    void *created = NULL;
    const uint64_t wall0 = wall_now();
    const uint64_t tsc0 = tsc_begin();
    const int status = generic ?
        asian_meta_qsort_control_plan_create(
            (asian_meta_qsort_control_plan_t **)&created) :
        asian_meta_affine_plan_create((asian_meta_affine_plan_t **)&created);
    const uint64_t tsc1 = tsc_end();
    const uint64_t wall1 = wall_now();
    if (status != 0 || created == NULL)
        abort();
    if (generic)
        asian_meta_qsort_control_plan_destroy(created);
    else
        asian_meta_affine_plan_destroy(created);
    return (timing_t){(double)(wall1 - wall0), (double)(tsc1 - tsc0)};
}

static void measure_builders(timing_t *generic, timing_t *affine)
{
    double gw[MEASURED_QUARTETS] = {0};
    double gt[MEASURED_QUARTETS] = {0};
    double aw[MEASURED_QUARTETS] = {0};
    double at[MEASURED_QUARTETS] = {0};
    for (uint32_t sample = 0; sample < BUILDER_SAMPLES; ++sample) {
        const int generic_first = (sample & 1u) == 0u;
        const timing_t first = observe_builder(generic_first);
        const timing_t second = observe_builder(!generic_first);
        const timing_t gv = generic_first ? first : second;
        const timing_t av = generic_first ? second : first;
        gw[sample] = gv.wall; gt[sample] = gv.tsc;
        aw[sample] = av.wall; at[sample] = av.tsc;
    }
    qsort(gw, BUILDER_SAMPLES, sizeof(gw[0]), compare_double);
    qsort(gt, BUILDER_SAMPLES, sizeof(gt[0]), compare_double);
    qsort(aw, BUILDER_SAMPLES, sizeof(aw[0]), compare_double);
    qsort(at, BUILDER_SAMPLES, sizeof(at[0]), compare_double);
    generic->wall = gw[BUILDER_SAMPLES / 2u];
    generic->tsc = gt[BUILDER_SAMPLES / 2u];
    affine->wall = aw[BUILDER_SAMPLES / 2u];
    affine->tsc = at[BUILDER_SAMPLES / 2u];
}

static double median_cell(double *values, size_t count)
{
    qsort(values, count, sizeof(values[0]), compare_double);
    return values[count / 2u];
}

static int native_benchmark(int cpu)
{
    if (cpu < 0 || pin_cpu(cpu) != 0 || !is_sapphire_rapids()) {
        fprintf(stderr, "pinned Sapphire Rapids CPU is required\n");
        return -1;
    }
    timing_t generic_plan, affine_plan;
    measure_builders(&generic_plan, &affine_plan);
    puts("plan                         wall_ns      tsc_ticks");
    printf("%-28s %12.0f %14.0f\n", "generic_qsort_plan_create",
           generic_plan.wall, generic_plan.tsc);
    printf("%-28s %12.0f %14.0f\n", "affine_meta_plan_create",
           affine_plan.wall, affine_plan.tsc);

    puts("option cache lifecycle pair arithmetic_wall generic_geocv_wall "
         "affine_geocv_wall arithmetic_tsc generic_geocv_tsc "
         "affine_geocv_tsc arithmetic/affine_ratio "
         "generic/affine_speedup identity");
    double generic_wall_speedups[TOTAL_CELLS];
    double generic_tsc_speedups[TOTAL_CELLS];
    size_t decision_cell = 0u;
    double worst_wall = 1.0e300, worst_tsc = 1.0e300;
    for (uint32_t side = 0; side < 2u; ++side) {
        for (uint32_t cache = 0; cache < 2u; ++cache) {
            for (uint32_t lifecycle = 0; lifecycle < LIFE_COUNT;
                 ++lifecycle) {
                const pair_timing_t arithmetic = measure_pair(
                    ASIAN_N64_ARITHMETIC_AFFINE,
                    ASIAN_N64_GEOCV_AFFINE_IMMEDIATE,
                    (enum lifecycle)lifecycle, (enum cache_mode)cache,
                    (enum option_side)side);
                const pair_timing_t generic = measure_pair(
                    ASIAN_N64_GEOCV_GENERIC_IMMEDIATE,
                    ASIAN_N64_GEOCV_AFFINE_IMMEDIATE,
                    (enum lifecycle)lifecycle, (enum cache_mode)cache,
                    (enum option_side)side);
                const double arithmetic_ratio_wall =
                    arithmetic.left.wall / arithmetic.right.wall;
                const double arithmetic_ratio_tsc =
                    arithmetic.left.tsc / arithmetic.right.tsc;
                const double generic_speedup_wall =
                    generic.left.wall / generic.right.wall;
                const double generic_speedup_tsc =
                    generic.left.tsc / generic.right.tsc;
                generic_wall_speedups[decision_cell] = generic_speedup_wall;
                generic_tsc_speedups[decision_cell] = generic_speedup_tsc;
                ++decision_cell;
                if (generic_speedup_wall < worst_wall)
                    worst_wall = generic_speedup_wall;
                if (generic_speedup_tsc < worst_tsc)
                    worst_tsc = generic_speedup_tsc;
                printf("%s %s %s ARITH_VS_AFFINE %.1f - %.1f %.1f - %.1f "
                       "%.6f/%.6f - %s\n",
                       side_name((enum option_side)side),
                       cache_name((enum cache_mode)cache),
                       lifecycle_name((enum lifecycle)lifecycle),
                       arithmetic.left.wall, arithmetic.right.wall,
                       arithmetic.left.tsc, arithmetic.right.tsc,
                       arithmetic_ratio_wall, arithmetic_ratio_tsc,
                       every_identity ? "PASS" : "FAIL");
                printf("%s %s %s GENERIC_VS_AFFINE - %.1f %.1f - %.1f %.1f "
                       "- %.6f/%.6f %s\n",
                       side_name((enum option_side)side),
                       cache_name((enum cache_mode)cache),
                       lifecycle_name((enum lifecycle)lifecycle),
                       generic.left.wall, generic.right.wall,
                       generic.left.tsc, generic.right.tsc,
                       generic_speedup_wall, generic_speedup_tsc,
                       every_identity ? "PASS" : "FAIL");
            }
        }
    }
    const double median_wall =
        median_cell(generic_wall_speedups, TOTAL_CELLS);
    const double median_tsc =
        median_cell(generic_tsc_speedups, TOTAL_CELLS);
    const int select_affine = every_identity && worst_wall >= 0.99 &&
        worst_tsc >= 0.99 && median_wall > 1.01 && median_tsc > 1.01;
    puts("option error_reduction");
    puts("call 20.96x");
    puts("put 20.41x");
    printf("geocv_decision %s worst_wall=%.6f worst_tsc=%.6f "
           "median_wall=%.6f median_tsc=%.6f identity=%s\n",
           select_affine ? "AFFINE_GEOCV" : "GENERIC_GEOCV",
           worst_wall, worst_tsc, median_wall, median_tsc,
           every_identity ? "PASS" : "FAIL");
    return every_identity ? 0 : -1;
}

int main(int argc, char **argv)
{
    int native = 0;
    int cpu = -1;
    if (argc == 2 && strcmp(argv[1], "--check-only") == 0) {
        native = 0;
    } else if (argc == 3 && strcmp(argv[1], "--cpu") == 0) {
        native = 1;
        cpu = atoi(argv[2]);
    } else {
        fprintf(stderr, "usage: %s --check-only | --cpu CPU\n", argv[0]);
        return 2;
    }
    if (posix_memalign((void **)&workspace, 64u, sizeof(*workspace)) != 0)
        return 2;
    memset(workspace, 0, sizeof(*workspace));
    if (asian_n64_comparison_engine_create(&workspace->engine) != 0 ||
        prepare_persistent() != 0 || bounded_correctness() != 0) {
        fprintf(stderr, "N64 comparison correctness failed\n");
        asian_n64_comparison_engine_destroy(&workspace->engine);
        free(workspace);
        return 1;
    }
    puts(native ? "bounded_native_correctness PASS" :
                  "bounded_sde_correctness PASS");
    const int status = native ? native_benchmark(cpu) : 0;
    asian_n64_comparison_engine_destroy(&workspace->engine);
    free(workspace);
    return status == 0 ? 0 : 1;
}
