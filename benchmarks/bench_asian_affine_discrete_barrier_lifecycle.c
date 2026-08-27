#define _GNU_SOURCE

#include "private/asian_affine_discrete_barrier_lifecycle_diag.h"
#include "private/asian_genuine_fixed_block_source_diag.h"

#include <cpuid.h>
#include <immintrin.h>
#include <inttypes.h>
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
    CANDIDATES = 2,
    INPUTS = 2,
};

enum lifecycle { PREPARED, REUSED, FRESH, LIFECYCLE_COUNT };
enum cache_mode { CANDIDATE_WARM, PRESSURE_32K, CACHE_COUNT };

static const char *lifecycle_name[LIFECYCLE_COUNT] = {
    "prepared", "reused_carrier", "fresh"};
static const char *cache_name[CACHE_COUNT] = {
    "candidate_warm", "pressure_32KiB"};

typedef struct { uint64_t wall, tsc; } timing_t;
typedef struct { timing_t candidate[CANDIDATES]; } pair_t;

typedef struct __attribute__((aligned(64))) {
    asian_affine_barrier_engine_t engine;
    asian_affine_barrier_carrier_t prepared_carrier[INPUTS];
    asian_affine_barrier_request_t prepared_request[INPUTS][CANDIDATES];
    asian_affine_barrier_request_t expected_request;
    asian_affine_barrier_output_t expected[LIFECYCLE_COUNT][INPUTS][CANDIDATES];
    asian_affine_barrier_output_t output;
    asian_affine_barrier_request_input_t prepared_input[INPUTS][CANDIDATES];
    asian_affine_barrier_request_input_t reuse_input[INPUTS][CANDIDATES];
    asian_affine_barrier_request_input_t fresh_input[INPUTS][CANDIDATES];
    uint32_t pressure[8192];
} fixture_t;

static fixture_t fixture;
static volatile uint64_t sink_value;

static uint64_t tsc_begin(void)
{
    _mm_lfence();
    return __rdtsc();
}

static uint64_t tsc_end(void)
{
    unsigned int aux;
    const uint64_t value = __rdtscp(&aux);
    _mm_lfence();
    return value;
}

static uint64_t wall_ns(const struct timespec *value)
{
    return (uint64_t)value->tv_sec * UINT64_C(1000000000) +
           (uint64_t)value->tv_nsec;
}

static int compare_u64(const void *a, const void *b)
{
    const uint64_t aa = *(const uint64_t *)a;
    const uint64_t bb = *(const uint64_t *)b;
    return aa < bb ? -1 : aa != bb;
}

static int compare_double(const void *a, const void *b)
{
    const double aa = *(const double *)a;
    const double bb = *(const double *)b;
    return aa < bb ? -1 : aa != bb;
}

static uint64_t median_u64(uint64_t values[SAMPLES])
{
    qsort(values, SAMPLES, sizeof(*values), compare_u64);
    return values[SAMPLES / 2u];
}

static double median_double(double values[SAMPLES])
{
    qsort(values, SAMPLES, sizeof(*values), compare_double);
    return values[SAMPLES / 2u];
}

static int pin_cpu(unsigned cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

static int sapphire_rapids(void)
{
    unsigned int a, b, c, d;
    char vendor[13];
    if (!__get_cpuid(0, &a, &b, &c, &d)) return 0;
    memcpy(vendor, &b, 4u); memcpy(vendor + 4, &d, 4u);
    memcpy(vendor + 8, &c, 4u); vendor[12] = '\0';
    if (strcmp(vendor, "GenuineIntel") != 0 ||
        !__get_cpuid(1, &a, &b, &c, &d)) return 0;
    const unsigned family0 = (a >> 8) & 15u;
    const unsigned model0 = (a >> 4) & 15u;
    const unsigned family = family0 == 15u ? family0 + ((a >> 20) & 255u) : family0;
    const unsigned model = (family0 == 6u || family0 == 15u) ?
        model0 + (((a >> 16) & 15u) << 4) : model0;
    return family == 6u && model == 143u;
}

static asian_affine_barrier_request_input_t make_input(
    uint32_t n, uint32_t which, uint32_t reuse,
    enum asian_affine_barrier_product product,
    enum asian_affine_barrier_direction direction,
    enum asian_affine_barrier_side side)
{
    asian_affine_barrier_request_input_t input;
    memset(&input, 0, sizeof(input));
    if (!reuse && which == 1u) {
        input.s0 = 117.0; input.strike = 110.0;
        input.rate = -0.01; input.dividend_yield = 0.015;
        input.sigma = 0.35; input.maturity = 0.75;
        input.barrier = direction == ASIAN_AFFINE_BARRIER_DOWN ? 100.0 : 140.0;
    } else if (reuse && which == 1u) {
        input.s0 = 108.0; input.strike = 105.0;
        input.rate = 0.03; input.dividend_yield = 0.0;
        input.sigma = 0.20; input.maturity = 1.0;
        input.barrier = direction == ASIAN_AFFINE_BARRIER_DOWN ? 92.0 : 130.0;
    } else {
        input.s0 = 100.0; input.strike = 100.0;
        input.rate = 0.03; input.dividend_yield = 0.0;
        input.sigma = 0.20; input.maturity = 1.0;
        input.barrier = direction == ASIAN_AFFINE_BARRIER_DOWN ? 95.0 : 120.0;
    }
    input.monitoring_count = n;
    input.product = product;
    input.direction = direction;
    input.side = side;
    input.initial_state = ASIAN_AFFINE_BARRIER_KNOWN_ALIVE;
    return input;
}

static asian_affine_barrier_market_input_t market_of(
    const asian_affine_barrier_request_input_t *input)
{
    const asian_affine_barrier_market_input_t market = {
        input->rate, input->dividend_yield, input->sigma, input->maturity,
        input->monitoring_count};
    return market;
}

static int setup_cell(uint32_t n,
                      enum asian_affine_barrier_direction direction,
                      enum asian_affine_barrier_side side)
{
    for (uint32_t input = 0; input < INPUTS; ++input)
        for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate) {
            const enum asian_affine_barrier_product product = candidate == 0u ?
                ASIAN_AFFINE_BARRIER_VANILLA : ASIAN_AFFINE_BARRIER_KNOCK_OUT;
            fixture.prepared_input[input][candidate] =
                make_input(n, input, 0u, product, direction, side);
            fixture.fresh_input[input][candidate] =
                fixture.prepared_input[input][candidate];
            fixture.reuse_input[input][candidate] =
                make_input(n, input, 1u, product, direction, side);
        }
    for (uint32_t input = 0; input < INPUTS; ++input) {
        const asian_affine_barrier_market_input_t market =
            market_of(&fixture.prepared_input[input][0]);
        if (asian_affine_barrier_market_prepare(
                &fixture.engine, &market, &fixture.prepared_carrier[input]) != 0)
            return -1;
        for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate) {
            if (asian_affine_barrier_request_prepare(&fixture.engine,
                    &fixture.prepared_carrier[input],
                    &fixture.prepared_input[input][candidate],
                    &fixture.prepared_request[input][candidate]) != 0 ||
                asian_affine_barrier_prepared_price(
                    &fixture.prepared_request[input][candidate],
                    &fixture.expected[PREPARED][input][candidate]) != 0)
                return -1;
            if (asian_affine_barrier_request_prepare(&fixture.engine,
                    &fixture.prepared_carrier[0],
                    &fixture.reuse_input[input][candidate],
                    &fixture.expected_request) != 0 ||
                asian_affine_barrier_prepared_price(&fixture.expected_request,
                    &fixture.expected[REUSED][input][candidate]) != 0 ||
                asian_affine_barrier_fresh_total(&fixture.engine,
                    &fixture.fresh_input[input][candidate],
                    &fixture.expected[FRESH][input][candidate]) != 0)
                return -1;
        }
    }
    return 0;
}

static void touch_bytes(const void *memory, size_t bytes)
{
    const volatile unsigned char *data = memory;
    uint64_t value = 0u;
    for (size_t offset = 0; offset < bytes; offset += 64u)
        value += data[offset];
    sink_value ^= value;
}

static void condition(enum lifecycle lifecycle, enum cache_mode cache,
                      uint32_t input, uint32_t candidate, uint32_t n)
{
    if (cache == PRESSURE_32K) {
        uint64_t value = 0u;
        for (uint32_t i = 0; i < 8192u; i += 16u) {
            fixture.pressure[i] += i + 3u;
            value += fixture.pressure[i];
        }
        sink_value ^= value;
        return;
    }
    touch_bytes(fixture.engine.affine_plan, ASIAN_META_PLAN_HEADER_BYTES);
    touch_bytes(fixture.engine.affine_plan->contexts,
                (size_t)n * sizeof(fixture.engine.affine_plan->contexts[0]));
    if (lifecycle == PREPARED) {
        touch_bytes(&fixture.prepared_carrier[input],
                    sizeof(fixture.prepared_carrier[input]));
        touch_bytes(&fixture.prepared_request[input][candidate],
                    sizeof(fixture.prepared_request[input][candidate]));
    } else if (lifecycle == REUSED) {
        touch_bytes(&fixture.prepared_carrier[0],
                    sizeof(fixture.prepared_carrier[0]));
        touch_bytes(&fixture.reuse_input[input][candidate],
                    sizeof(fixture.reuse_input[input][candidate]));
    } else {
        touch_bytes(asian_genuine_fixed_block_signed_z,
                    ASIAN_AFFINE_BARRIER_DONOR_VALUES * sizeof(float));
        touch_bytes(&fixture.fresh_input[input][candidate],
                    sizeof(fixture.fresh_input[input][candidate]));
    }
}

static int execute(enum lifecycle lifecycle, uint32_t input,
                   uint32_t candidate)
{
    if (lifecycle == PREPARED)
        return asian_affine_barrier_prepared_price(
            &fixture.prepared_request[input][candidate], &fixture.output);
    if (lifecycle == REUSED)
        return asian_affine_barrier_reuse_total(&fixture.engine,
            &fixture.prepared_carrier[0], &fixture.reuse_input[input][candidate],
            &fixture.output);
    return asian_affine_barrier_fresh_total(&fixture.engine,
        &fixture.fresh_input[input][candidate], &fixture.output);
}

static timing_t observe(enum lifecycle lifecycle, enum cache_mode cache,
                        uint32_t input, uint32_t candidate, uint32_t n)
{
    struct timespec before, after;
    condition(lifecycle, cache, input, candidate, n);
    __asm__ volatile("" ::: "memory");
    clock_gettime(CLOCK_MONOTONIC_RAW, &before);
    const uint64_t first = tsc_begin();
    const int status = execute(lifecycle, input, candidate);
    const uint64_t last = tsc_end();
    clock_gettime(CLOCK_MONOTONIC_RAW, &after);
    __asm__ volatile("" ::: "memory");
    if (status != 0 || memcmp(&fixture.output,
            &fixture.expected[lifecycle][input][candidate],
            sizeof(fixture.output)) != 0) {
        fputs("timed identity failure\n", stderr);
        exit(2);
    }
    uint64_t bits;
    memcpy(&bits, &fixture.output.price, sizeof(bits));
    sink_value ^= bits;
    return (timing_t){wall_ns(&after) - wall_ns(&before), last - first};
}

static pair_t measure_pair(enum lifecycle lifecycle, enum cache_mode cache,
                           uint32_t n)
{
    double wall[CANDIDATES][SAMPLES], ticks[CANDIDATES][SAMPLES];
    for (uint32_t quartet = 0; quartet < WARMUPS + SAMPLES; ++quartet) {
        const uint32_t order[2][4] = {{0u,1u,1u,0u},{1u,0u,0u,1u}};
        double sum_wall[2] = {0}, sum_tsc[2] = {0};
        for (uint32_t observation = 0; observation < 4u; ++observation) {
            const uint32_t candidate = order[quartet & 1u][observation];
            const uint32_t input = (quartet + observation) & 1u;
            const timing_t timing = observe(
                lifecycle, cache, input, candidate, n);
            sum_wall[candidate] += (double)timing.wall;
            sum_tsc[candidate] += (double)timing.tsc;
        }
        if (quartet >= WARMUPS) {
            const uint32_t sample = quartet - WARMUPS;
            for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate) {
                wall[candidate][sample] = sum_wall[candidate] * 0.5;
                ticks[candidate][sample] = sum_tsc[candidate] * 0.5;
            }
        }
    }
    pair_t result;
    for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate) {
        result.candidate[candidate].wall =
            (uint64_t)llround(median_double(wall[candidate]));
        result.candidate[candidate].tsc =
            (uint64_t)llround(median_double(ticks[candidate]));
    }
    return result;
}

static timing_t measure_plan_create(void)
{
    uint64_t walls[SAMPLES], ticks[SAMPLES];
    for (uint32_t sample = 0; sample < WARMUPS + SAMPLES; ++sample) {
        asian_meta_affine_plan_t *plan = NULL;
        struct timespec before, after;
        clock_gettime(CLOCK_MONOTONIC_RAW, &before);
        const uint64_t first = tsc_begin();
        const int status = asian_meta_affine_plan_create(&plan);
        const uint64_t last = tsc_end();
        clock_gettime(CLOCK_MONOTONIC_RAW, &after);
        if (status != 0) exit(2);
        asian_meta_affine_plan_destroy(plan);
        if (sample >= WARMUPS) {
            walls[sample-WARMUPS] = wall_ns(&after)-wall_ns(&before);
            ticks[sample-WARMUPS] = last-first;
        }
    }
    return (timing_t){median_u64(walls), median_u64(ticks)};
}

static timing_t measure_engine_create(void)
{
    uint64_t walls[SAMPLES], ticks[SAMPLES];
    for (uint32_t sample = 0; sample < WARMUPS + SAMPLES; ++sample) {
        asian_affine_barrier_engine_t engine __attribute__((aligned(64)));
        struct timespec before, after;
        clock_gettime(CLOCK_MONOTONIC_RAW, &before);
        const uint64_t first = tsc_begin();
        const int status = asian_affine_barrier_engine_create(&engine);
        const uint64_t last = tsc_end();
        clock_gettime(CLOCK_MONOTONIC_RAW, &after);
        if (status != 0) exit(2);
        asian_affine_barrier_engine_destroy(&engine);
        if (sample >= WARMUPS) {
            walls[sample-WARMUPS] = wall_ns(&after)-wall_ns(&before);
            ticks[sample-WARMUPS] = last-first;
        }
    }
    return (timing_t){median_u64(walls), median_u64(ticks)};
}

static const char *product_name(uint32_t candidate,
                                enum asian_affine_barrier_direction direction,
                                enum asian_affine_barrier_side side)
{
    if (candidate == 0u) {
        if (side == ASIAN_AFFINE_BARRIER_CALL)
            return direction == ASIAN_AFFINE_BARRIER_DOWN ?
                "matched_vanilla_call_for_down" : "matched_vanilla_call_for_up";
        return direction == ASIAN_AFFINE_BARRIER_DOWN ?
            "matched_vanilla_put_for_down" : "matched_vanilla_put_for_up";
    }
    if (direction == ASIAN_AFFINE_BARRIER_DOWN)
        return side == ASIAN_AFFINE_BARRIER_CALL ?
            "down_and_out_call" : "down_and_out_put";
    return side == ASIAN_AFFINE_BARRIER_CALL ?
        "up_and_out_call" : "up_and_out_put";
}

static int bounded_check(void)
{
    if (asian_affine_barrier_engine_create(&fixture.engine) != 0) return -1;
    for (uint32_t ni = 0; ni < 2u; ++ni) {
        const uint32_t n = ni == 0u ? 64u : 256u;
        for (uint32_t direction = 0; direction < 2u; ++direction)
            for (uint32_t side = 0; side < 2u; ++side) {
                if (setup_cell(n,
                        (enum asian_affine_barrier_direction)direction,
                        (enum asian_affine_barrier_side)side) != 0)
                    return -1;
                for (uint32_t lifecycle = 0; lifecycle < LIFECYCLE_COUNT;
                     ++lifecycle)
                    for (uint32_t input = 0; input < INPUTS; ++input)
                        for (uint32_t candidate = 0; candidate < CANDIDATES;
                             ++candidate) {
                            if (execute((enum lifecycle)lifecycle, input,
                                        candidate) != 0 ||
                                memcmp(&fixture.output,
                                    &fixture.expected[lifecycle][input][candidate],
                                    sizeof(fixture.output)) != 0) return -1;
                        }
            }
    }
    asian_affine_barrier_engine_destroy(&fixture.engine);
    return 0;
}

static int run_timing(unsigned cpu)
{
    if (!sapphire_rapids() || pin_cpu(cpu) != 0) {
        fputs("Sapphire Rapids family-6/model-143 CPU 0 is required\n", stderr);
        return 2;
    }
    memset(&fixture, 0, sizeof(fixture));
    for (uint32_t i = 0; i < 8192u; ++i) fixture.pressure[i] = i + 1u;
    const timing_t plan = measure_plan_create();
    const timing_t init = measure_engine_create();
    if (asian_affine_barrier_engine_create(&fixture.engine) != 0) return 2;
    printf("engine_component                 wall_ns     tsc_ticks\n");
    printf("affine_plan_create %20"PRIu64" %13"PRIu64"\n",plan.wall,plan.tsc);
    printf("engine_initialize_total %17"PRIu64" %13"PRIu64"\n",init.wall,init.tsc);
    puts("N product lifecycle cache wall_ns tsc_ticks valuations_per_sec "
         "path_monitoring_updates_per_sec identity");

    double wall_ratios[48], tsc_ratios[48];
    uint32_t ratio_count = 0u;
    double headline_wall[2][2] = {{0}};
    uint64_t headline_tsc[2][2] = {{0}};
    const char *headline_product[2][2] = {{NULL}};
    for (uint32_t ni = 0; ni < 2u; ++ni) {
        const uint32_t n = ni == 0u ? 64u : 256u;
        for (uint32_t direction = 0; direction < 2u; ++direction)
            for (uint32_t side = 0; side < 2u; ++side) {
                if (setup_cell(n,
                        (enum asian_affine_barrier_direction)direction,
                        (enum asian_affine_barrier_side)side) != 0) return 2;
                for (uint32_t lifecycle = 0; lifecycle < LIFECYCLE_COUNT;
                     ++lifecycle)
                    for (uint32_t cache = 0; cache < CACHE_COUNT; ++cache) {
                        const pair_t pair = measure_pair(
                            (enum lifecycle)lifecycle,
                            (enum cache_mode)cache, n);
                        for (uint32_t candidate = 0; candidate < CANDIDATES;
                             ++candidate) {
                            const double wall = pair.candidate[candidate].wall;
                            const double valuations = 1.0e9 / wall;
                            printf("%u %s %s %s %"PRIu64" %"PRIu64
                                   " %.6f %.6f PASS\n", n,
                                product_name(candidate,
                                    (enum asian_affine_barrier_direction)direction,
                                    (enum asian_affine_barrier_side)side),
                                lifecycle_name[lifecycle], cache_name[cache],
                                pair.candidate[candidate].wall,
                                pair.candidate[candidate].tsc, valuations,
                                valuations * ASIAN_AFFINE_BARRIER_PATHS * n);
                        }
                        const double wr = (double)pair.candidate[1].wall /
                                          pair.candidate[0].wall;
                        const double tr = (double)pair.candidate[1].tsc /
                                          pair.candidate[0].tsc;
                        wall_ratios[ratio_count] = wr;
                        tsc_ratios[ratio_count++] = tr;
                        printf("barrier_tax N=%u product=%s lifecycle=%s cache=%s "
                               "wall_ratio=%.6f wall_overhead_pct=%.3f "
                               "tsc_ratio=%.6f tsc_overhead_pct=%.3f\n",
                               n, product_name(1u,
                                   (enum asian_affine_barrier_direction)direction,
                                   (enum asian_affine_barrier_side)side),
                               lifecycle_name[lifecycle], cache_name[cache],
                               wr, 100.0*(wr-1.0), tr, 100.0*(tr-1.0));
                        if (lifecycle == FRESH &&
                            pair.candidate[1].wall > headline_wall[ni][cache]) {
                            headline_wall[ni][cache] = pair.candidate[1].wall;
                            headline_tsc[ni][cache] = pair.candidate[1].tsc;
                            headline_product[ni][cache] = product_name(1u,
                                (enum asian_affine_barrier_direction)direction,
                                (enum asian_affine_barrier_side)side);
                        }
                    }
            }
    }
    double wall_log = 0.0, tsc_log = 0.0, worst_wall = 0.0, worst_tsc = 0.0;
    for (uint32_t i = 0; i < ratio_count; ++i) {
        wall_log += log(wall_ratios[i]); tsc_log += log(tsc_ratios[i]);
        if (wall_ratios[i] > worst_wall) worst_wall = wall_ratios[i];
        if (tsc_ratios[i] > worst_tsc) worst_tsc = tsc_ratios[i];
    }
    const double aggregate_wall = exp(wall_log / ratio_count);
    const double aggregate_tsc = exp(tsc_log / ratio_count);
    for (uint32_t ni = 0; ni < 2u; ++ni)
        for (uint32_t cache = 0; cache < CACHE_COUNT; ++cache)
            printf("N%u_BARRIER_FRESH cache=%s worst_product=%s wall_ns=%.0f "
                   "tsc_ticks=%"PRIu64" valuations_per_sec=%.6f "
                   "path_monitoring_updates_per_sec=%.6f identity=PASS\n",
                   ni == 0u ? 64u : 256u,
                   cache_name[cache], headline_product[ni][cache],
                   headline_wall[ni][cache], headline_tsc[ni][cache],
                   1.0e9 / headline_wall[ni][cache],
                   1.0e9 / headline_wall[ni][cache] *
                       ASIAN_AFFINE_BARRIER_PATHS *
                       (ni == 0u ? 64u : 256u));
    printf("barrier_decision_summary aggregate_wall_ratio=%.6f "
           "aggregate_tsc_ratio=%.6f worst_wall_ratio=%.6f "
           "worst_tsc_ratio=%.6f cells=%u\n", aggregate_wall, aggregate_tsc,
           worst_wall, worst_tsc, ratio_count);
    for (uint32_t i = 0; i < ratio_count; ++i)
        if (wall_ratios[i] > 1.10 || tsc_ratios[i] > 1.10)
            printf("failing_cell index=%u wall_ratio=%.6f tsc_ratio=%.6f\n",
                   i, wall_ratios[i], tsc_ratios[i]);
    puts(aggregate_wall < 1.05 && aggregate_tsc < 1.05 &&
         worst_wall <= 1.10 && worst_tsc <= 1.10 ?
         "AFFINE_BARRIER_READY" : "AFFINE_BARRIER_CORRECTNESS_ONLY");
    asian_affine_barrier_engine_destroy(&fixture.engine);
    return 0;
}

int main(int argc, char **argv)
{
    int timing = 0, native_check = 0;
    unsigned cpu = 0u;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--check") == 0) continue;
        if (strcmp(argv[i], "--native-check") == 0) native_check = 1;
        else if (strcmp(argv[i], "--timing") == 0) timing = 1;
        else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc)
            cpu = (unsigned)strtoul(argv[++i], NULL, 10);
        else return 2;
    }
    if (timing) return run_timing(cpu);
    if (native_check && (!sapphire_rapids() || cpu != 0u || pin_cpu(cpu) != 0))
        return 2;
    memset(&fixture, 0, sizeof(fixture));
    if (bounded_check() != 0) return 2;
    puts("bounded_barrier_correctness PASS direct_d1=YES affine_d2_dn=YES");
    return 0;
}
