#define _GNU_SOURCE
#pragma GCC diagnostic ignored "-Wunused-function"
#define main asian_growth_only_embedded_correctness_main
#include "../tests/test_asian_genuine_arithmetic_growth_only.c"
#undef main

#include "private/asian_ordered_d1_source_opt.h"
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"

#include <cpuid.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

enum {
    WARMUP_QUARTETS = 16,
    MEASURED_QUARTETS = 51,
    QUARTET_OBSERVATIONS = 4,
    CANDIDATE_COUNT = 4,
    PAIR_COUNT = 2,
};

enum candidate {
    CANDIDATE_ORDERED_X = 0,
    CANDIDATE_ORDERED_X_OPT = 1,
    CANDIDATE_FIXED_Z_EXP = 2,
    CANDIDATE_FUSED_Z_EXP = 3,
};

enum phase {
    PHASE_FRONTEND = 0,
    PHASE_COMPLETE = 1,
};

enum cache_mode {
    CACHE_CANDIDATE_WARM = 0,
    CACHE_HISTORICAL_32K = 1,
};

typedef struct {
    enum candidate a;
    enum candidate b;
    const char *name;
} candidate_pair_t;

typedef struct { uint64_t tsc, wall; } duration_t;

static const candidate_pair_t candidate_pairs[PAIR_COUNT] = {
    {CANDIDATE_ORDERED_X, CANDIDATE_ORDERED_X_OPT, "ordered_x"},
    {CANDIDATE_FIXED_Z_EXP, CANDIDATE_FUSED_Z_EXP, "signed_z"},
};

static const char *const candidate_names[CANDIDATE_COUNT] = {
    "ordered_x_exp",
    "ordered_x_opt_exp",
    "fixed_z_exp",
    "fused_z_exp",
};

static fixture_t bench_fixture;
static ordered_d1_diag_context_t *bench_ordered;
static asian_genuine_fixed_block_source_context_t *bench_fixed;
static asian_genuine_arithmetic_fused_source_exp_context_t *bench_fused;
static asian_genuine_strip_context_t *bench_strip;
static asian_genuine_strip_output_t *bench_output;
static uint32_t *bench_pressure;
static float *reference_x;
static float *reference_growth;
static asian_genuine_fixed_block_source_request_t bench_request;
static float bench_drift;
static float bench_diffusion;
static uint32_t bench_n;
static volatile uint64_t bench_sink;

void asian_genuine_arithmetic_downstream_layout_anchor(void);

static uint64_t wall_now(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value) != 0) abort();
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static uint64_t tsc_begin(void)
{
    _mm_lfence();
    return __rdtsc();
}

static uint64_t tsc_end(void)
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

static double median51(double values[MEASURED_QUARTETS])
{
    qsort(values, MEASURED_QUARTETS, sizeof(values[0]), compare_double);
    return values[MEASURED_QUARTETS / 2u];
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
    if (strcmp(vendor, "GenuineIntel") != 0) return 0;
    __cpuid(1, a, b, c, d);
    const unsigned family = ((a >> 8) & 15u) + ((a >> 20) & 255u);
    const unsigned model = ((a >> 4) & 15u) | ((a >> 12) & 240u);
    return family == 6u && model == 0x8fu;
}

static uint64_t touch(const void *memory, size_t bytes)
{
    const unsigned char *data = memory;
    uint64_t value = 0;
    for (size_t offset = 0; offset < bytes; offset += 64u)
        value += data[offset];
    return value;
}

static void run_exp(void)
{
    asian_vector_exp_range_reduced_array_diag(
        bench_fixture.x, bench_fixture.growth);
    asian_vector_exp_range_reduced_array_diag(
        bench_fixture.x + PATHS, bench_fixture.growth + PATHS);
}

__attribute__((noinline)) static void run_ordered_x_frontend(void)
{
    if (ordered_d1_diag_prepare(
            bench_ordered, bench_drift, bench_diffusion,
            ASIAN_ORDERED_D1_SOURCE_OPT_VALUES,
            ORDERED_D1_DIAG_PREPARE_X3, bench_n) != 0)
        abort();
    ordered_d1_x_only_diag(ASIAN_ORDERED_D1_SOURCE_OPT_VALUES / 32u,
                           bench_ordered, bench_fixture.x);
    run_exp();
}

__attribute__((noinline)) static void run_ordered_x_opt_frontend(void)
{
    if (ordered_d1_diag_prepare(
            bench_ordered, bench_drift, bench_diffusion,
            ASIAN_ORDERED_D1_SOURCE_OPT_VALUES,
            ORDERED_D1_DIAG_PREPARE_X3, bench_n) != 0)
        abort();
    asian_ordered_d1_x_static_8192_diag(bench_ordered, bench_fixture.x);
    run_exp();
}

__attribute__((noinline)) static void run_fixed_z_frontend(void)
{
    if (asian_genuine_fixed_block_source_prepare(bench_fixed, &bench_request) !=
        ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK)
        abort();
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(
        bench_fixed, bench_fixture.x);
    run_exp();
}

__attribute__((noinline)) static void run_fused_z_frontend(void)
{
    if (asian_genuine_arithmetic_fused_source_exp_prepare(
            bench_fused, &bench_request, bench_fixture.growth,
            ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES) !=
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_OK)
        abort();
    asian_genuine_arithmetic_fused_source_exp_diag(bench_fused);
}

typedef void (*runner_t)(void);

static const runner_t frontend_runners[CANDIDATE_COUNT] = {
    run_ordered_x_frontend,
    run_ordered_x_opt_frontend,
    run_fixed_z_frontend,
    run_fused_z_frontend,
};

__attribute__((noinline)) static void run_downstream(void)
{
    asian_genuine_arithmetic_growth_only_q_diag(bench_fixture.context);
    asian_genuine_strip_arithmetic_price_1_diag(
        bench_fixture.q, bench_fixture.g_unused, bench_strip,
        bench_strip->strikes, bench_output->values);
}

__attribute__((noinline)) static void run_ordered_x_complete(void)
{
    run_ordered_x_frontend();
    run_downstream();
}

__attribute__((noinline)) static void run_ordered_x_opt_complete(void)
{
    run_ordered_x_opt_frontend();
    run_downstream();
}

__attribute__((noinline)) static void run_fixed_z_complete(void)
{
    run_fixed_z_frontend();
    run_downstream();
}

__attribute__((noinline)) static void run_fused_z_complete(void)
{
    run_fused_z_frontend();
    run_downstream();
}

static const runner_t complete_runners[CANDIDATE_COUNT] = {
    run_ordered_x_complete,
    run_ordered_x_opt_complete,
    run_fixed_z_complete,
    run_fused_z_complete,
};

static uint64_t output_hash(enum phase phase)
{
    if (phase == PHASE_FRONTEND)
        return hash_bytes(UINT64_C(1469598103934665603),
                          bench_fixture.growth,
                          2u * PATHS * sizeof(float));
    return hash_bytes(UINT64_C(1469598103934665603),
                      bench_output->values, 2u * sizeof(double));
}

static void reset_outside_timing(enum phase phase)
{
    if (phase == PHASE_COMPLETE) {
        memset(bench_fixture.q, 0xa5, PATHS * sizeof(float));
        memset(bench_output, 0, sizeof(*bench_output));
    }
}

static void condition_outside_timing(enum candidate candidate,
                                     enum cache_mode cache)
{
    uint64_t value =
        touch(bench_fixture.x, 2u * PATHS * sizeof(float)) +
        touch(bench_fixture.growth, 2u * PATHS * sizeof(float)) +
        touch(bench_fixture.poisoned_routes,
              (size_t)bench_n * sizeof(*bench_fixture.poisoned_routes)) +
        touch(bench_fixture.maps,
              (size_t)bench_n * sizeof(*bench_fixture.maps)) +
        touch(bench_fixture.context, sizeof(*bench_fixture.context)) +
        touch(bench_fixture.q, PATHS * sizeof(float)) +
        touch(bench_strip, sizeof(*bench_strip));

    if (candidate <= CANDIDATE_ORDERED_X_OPT)
        value += touch(bench_ordered, sizeof(*bench_ordered));
    else
        value += touch(asian_genuine_fixed_block_signed_z,
                       ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);

    if (candidate == CANDIDATE_FIXED_Z_EXP)
        value += touch(bench_fixed, sizeof(*bench_fixed));
    if (candidate == CANDIDATE_FUSED_Z_EXP)
        value += touch(bench_fused, sizeof(*bench_fused));

    if (cache == CACHE_HISTORICAL_32K) {
        for (uint32_t i = 0; i < 8192u; ++i) {
            bench_pressure[i] += i + 3u;
            value += bench_pressure[i];
        }
    }
    bench_sink += value;
}

static duration_t measure(enum candidate candidate, enum phase phase,
                          enum cache_mode cache, uint64_t *hash)
{
    const runner_t runner = phase == PHASE_FRONTEND ?
        frontend_runners[candidate] : complete_runners[candidate];
    reset_outside_timing(phase);
    condition_outside_timing(candidate, cache);
    const int cpu_before = sched_getcpu();
    const uint64_t wall0 = wall_now();
    const uint64_t tsc0 = tsc_begin();
    runner();
    const uint64_t tsc1 = tsc_end();
    const uint64_t wall1 = wall_now();
    const int cpu_after = sched_getcpu();
    if (cpu_before < 0 || cpu_before != cpu_after) abort();
    *hash = output_hash(phase);
    bench_sink += *hash;
    return (duration_t){tsc1 - tsc0, wall1 - wall0};
}

static int prepare_cell(uint32_t n)
{
    static const float strike = 100.0f;
    const market_t *market = &markets[2];
    const double dt = market->maturity / (double)n;
    uint32_t padded = 0;

    bench_n = n;
    bench_drift = (float)((market->rate - market->dividend -
                           0.5 * market->sigma * market->sigma) * dt);
    bench_diffusion = (float)(market->sigma * sqrt(dt));
    bench_request = source_request(market, n);

    run_fixed_z_frontend();
    if (asian_genuine_arithmetic_growth_only_prepare(
            bench_fixture.context, bench_fixture.poisoned_routes, n, 100.0f,
            bench_fixture.growth, 2u * PATHS * sizeof(float),
            bench_fixture.q, PATHS * sizeof(float)) !=
            ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_OK ||
        asian_genuine_arithmetic_growth_only_strip_prepare_padded(
            bench_strip, 100.0, market->rate, market->dividend, market->sigma,
            market->maturity, n, 0u, 0.0, 0.0, &strike, 1u, &padded) != 0 ||
        padded != 1u)
        return -1;
    return 0;
}

static int verify_pair(const candidate_pair_t *pair)
{
    unsigned char output[2u * sizeof(double)];

    frontend_runners[pair->a]();
    memcpy(reference_x, bench_fixture.x, 2u * PATHS * sizeof(float));
    memcpy(reference_growth, bench_fixture.growth,
           2u * PATHS * sizeof(float));
    frontend_runners[pair->b]();
    if (memcmp(reference_growth, bench_fixture.growth,
               2u * PATHS * sizeof(float)) != 0)
        return -1;
    if (pair->a == CANDIDATE_ORDERED_X &&
        memcmp(reference_x, bench_fixture.x,
               2u * PATHS * sizeof(float)) != 0)
        return -1;

    reset_outside_timing(PHASE_COMPLETE);
    complete_runners[pair->a]();
    memcpy(output, bench_output->values, sizeof(output));
    reset_outside_timing(PHASE_COMPLETE);
    complete_runners[pair->b]();
    return memcmp(output, bench_output->values, sizeof(output)) == 0 ? 0 : -1;
}

static int time_pair(const candidate_pair_t *pair, enum phase phase,
                     enum cache_mode cache)
{
    double a_wall[MEASURED_QUARTETS];
    double b_wall[MEASURED_QUARTETS];
    double a_tsc[MEASURED_QUARTETS];
    double b_tsc[MEASURED_QUARTETS];
    uint64_t expected_hash = 0;
    int have_hash = 0;

    for (uint32_t quartet = 0;
         quartet < WARMUP_QUARTETS + MEASURED_QUARTETS; ++quartet) {
        const enum candidate abba[QUARTET_OBSERVATIONS] = {
            pair->a, pair->b, pair->b, pair->a};
        const enum candidate baab[QUARTET_OBSERVATIONS] = {
            pair->b, pair->a, pair->a, pair->b};
        const enum candidate *order = (quartet & 1u) != 0u ? baab : abba;
        uint64_t sum_wall[CANDIDATE_COUNT] = {0, 0, 0, 0};
        uint64_t sum_tsc[CANDIDATE_COUNT] = {0, 0, 0, 0};

        for (uint32_t observation = 0;
             observation < QUARTET_OBSERVATIONS; ++observation) {
            uint64_t hash;
            const enum candidate candidate = order[observation];
            const duration_t duration = measure(candidate, phase, cache, &hash);
            if (!have_hash) {
                expected_hash = hash;
                have_hash = 1;
            } else if (hash != expected_hash) {
                return -1;
            }
            sum_wall[candidate] += duration.wall;
            sum_tsc[candidate] += duration.tsc;
        }

        if (quartet >= WARMUP_QUARTETS) {
            const uint32_t sample = quartet - WARMUP_QUARTETS;
            a_wall[sample] = (double)sum_wall[pair->a] * 0.5;
            b_wall[sample] = (double)sum_wall[pair->b] * 0.5;
            a_tsc[sample] = (double)sum_tsc[pair->a] * 0.5;
            b_tsc[sample] = (double)sum_tsc[pair->b] * 0.5;
        }
    }

    const double aw = median51(a_wall);
    const double bw = median51(b_wall);
    const double at = median51(a_tsc);
    const double bt = median51(b_tsc);
    printf("%s\t%u\t%s\t%s\t%s\t%.1f\t%.1f\t%.6f\t"
           "%.1f\t%.1f\t%.6f\tEXACT\n",
           phase == PHASE_FRONTEND ? "front" : "complete", bench_n,
           cache == CACHE_CANDIDATE_WARM ?
               "candidate_warm" : "historical_32KiB_rmw",
           candidate_names[pair->a], candidate_names[pair->b],
           aw, bw, aw / bw, at, bt, at / bt);
    return 0;
}

static int allocate_benchmark(void)
{
    if (fixture_init(&bench_fixture) != 0) return -1;
    bench_ordered = a64(sizeof(*bench_ordered));
    bench_fixed = a64(sizeof(*bench_fixed));
    bench_fused = a64(sizeof(*bench_fused));
    bench_strip = a64(sizeof(*bench_strip));
    bench_output = a64(sizeof(*bench_output));
    bench_pressure = a64(8192u * sizeof(*bench_pressure));
    reference_x = a64(2u * PATHS * sizeof(float));
    reference_growth = a64(2u * PATHS * sizeof(float));
    if (bench_ordered == NULL || bench_fixed == NULL || bench_fused == NULL ||
        bench_strip == NULL || bench_output == NULL || bench_pressure == NULL ||
        reference_x == NULL || reference_growth == NULL)
        return -1;
    for (uint32_t i = 0; i < 8192u; ++i) bench_pressure[i] = i + 1u;
    return 0;
}

static void release_benchmark(void)
{
    free(reference_growth);
    free(reference_x);
    free(bench_pressure);
    free(bench_output);
    free(bench_strip);
    free(bench_fused);
    free(bench_fixed);
    free(bench_ordered);
    fixture_release(&bench_fixture);
}

static int verify_all(void)
{
    static const uint32_t ns[] = {16u, 32u, 64u, 128u, 256u};
    for (uint32_t ni = 0; ni < sizeof(ns) / sizeof(ns[0]); ++ni) {
        if (prepare_cell(ns[ni]) != 0) return -1;
        for (uint32_t pair = 0; pair < PAIR_COUNT; ++pair)
            if (verify_pair(&candidate_pairs[pair]) != 0) return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int cpu = -1;
    int verify_only = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc)
            cpu = atoi(argv[++i]);
        else if (strcmp(argv[i], "--verify-only") == 0)
            verify_only = 1;
        else {
            fprintf(stderr, "usage: %s --cpu CPU | --verify-only\n", argv[0]);
            return 2;
        }
    }

    if (!verify_only) {
        if (cpu < 0) {
            fprintf(stderr, "--cpu is required\n");
            return 2;
        }
        if (pin_cpu(cpu) != 0) {
            perror("sched_setaffinity");
            return 2;
        }
        if (!is_sapphire_rapids()) {
            fprintf(stderr, "verified Sapphire Rapids CPU required\n");
            return 2;
        }
    }

    asian_genuine_arithmetic_downstream_layout_anchor();
    if (allocate_benchmark() != 0) {
        fprintf(stderr, "benchmark fixture allocation failed\n");
        return 2;
    }
    if (verify_all() != 0) {
        fprintf(stderr, "exact-pair verification failed\n");
        release_benchmark();
        return 1;
    }
    if (verify_only) {
        puts("ordered_x_pair=EXACT signed_z_pair=EXACT N=16,32,64,128,256");
        release_benchmark();
        return 0;
    }

    static const uint32_t ns[] = {16u, 32u, 64u, 128u, 256u};
    puts("phase\tN\tcache\tA\tB\tA_wall_ns\tB_wall_ns\t"
         "B_speedup_wall\tA_tsc\tB_tsc\tB_speedup_tsc\tidentity");
    for (uint32_t ni = 0; ni < sizeof(ns) / sizeof(ns[0]); ++ni) {
        if (prepare_cell(ns[ni]) != 0) {
            fprintf(stderr, "cell preparation failed N=%u\n", ns[ni]);
            release_benchmark();
            return 1;
        }
        for (uint32_t cache = 0; cache < 2u; ++cache)
            for (uint32_t phase = 0; phase < 2u; ++phase)
                for (uint32_t pair = 0; pair < PAIR_COUNT; ++pair)
                    if (time_pair(&candidate_pairs[pair], (enum phase)phase,
                                  (enum cache_mode)cache) != 0) {
                        fprintf(stderr,
                                "timing identity failure N=%u cache=%u "
                                "phase=%u pair=%s\n",
                                ns[ni], cache, phase,
                                candidate_pairs[pair].name);
                        release_benchmark();
                        return 1;
                    }
    }

    bench_sink += touch(bench_output, sizeof(*bench_output));
    release_benchmark();
    return bench_sink == UINT64_MAX ? 1 : 0;
}
