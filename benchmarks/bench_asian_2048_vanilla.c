#define _GNU_SOURCE

#include "private/asian_2048_vanilla_diag.h"

#include <cpuid.h>
#include <immintrin.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WARMUPS = 16, SAMPLES = 101, CANDIDATES = 2,
       LIFECYCLES = 3, CACHES = 2 };
enum candidate { VANILLA_2048, VANILLA_4096 };
enum lifecycle { PREPARED, REUSED, FRESH };
enum cache_mode { CANDIDATE_WARM, PRESSURE_32KIB };

typedef struct { double wall, tsc; } timing_t;
typedef struct { double rate, dividend, sigma, maturity, s0; float strike; }
    input_t;

static const input_t inputs[2] = {
    {0.03, 0.0, 0.20, 1.0, 100.0, 100.0f},
    {-0.01, 0.015, 0.35, 0.75, 117.0, 110.0f},
};
static const char *lifecycle_names[] = {
    "prepared", "reused_carrier", "fresh"
};
static const char *cache_names[] = {"candidate_warm", "pressure_32KiB"};

typedef struct {
    asian_2048_engine_t *engine;
    asian_2048_carrier_t *prepared_carrier[2];
    asian_2048_carrier_t *scratch_carrier[CANDIDATES];
    asian_2048_request_t *prepared_2048[2];
    asian_4096_vanilla_request_t *prepared_4096[2];
    asian_2048_request_t *scratch_2048;
    asian_4096_vanilla_request_t *scratch_4096;
    asian_2048_output_t output[CANDIDATES];
    asian_2048_output_t expected[CANDIDATES][LIFECYCLES][2];
    uint32_t pressure[8192];
    uint32_t n;
} fixture_t;

static fixture_t fixture;
static volatile uint64_t sink;
extern const float asian_genuine_fixed_block_signed_z[];

static void *a64(size_t bytes)
{
    void *pointer = NULL;
    if (posix_memalign(&pointer, 64u, bytes) != 0)
        return NULL;
    memset(pointer, 0, bytes);
    return pointer;
}

static uint64_t wall_now(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value) != 0)
        abort();
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + value.tv_nsec;
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

static double median(double values[SAMPLES])
{
    qsort(values, SAMPLES, sizeof(values[0]), compare_double);
    return values[SAMPLES / 2];
}

static int is_spr(void)
{
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid(1u, &eax, &ebx, &ecx, &edx))
        return 0;
    const unsigned family = ((eax >> 8) & 15u) + ((eax >> 20) & 255u);
    const unsigned model = ((eax >> 4) & 15u) | ((eax >> 12) & 240u);
    return family == 6u && model == 143u;
}

static int pin_cpu(uint32_t cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

static void warm_bytes(const void *memory, size_t bytes)
{
    const volatile unsigned char *p = memory;
    uint64_t value = sink;
    for (size_t offset = 0; offset < bytes; offset += 64u)
        value += p[offset];
    sink = value;
}

static void pressure(void)
{
    uint64_t value = sink;
    for (uint32_t i = 0; i < 8192u; i += 16u) {
        fixture.pressure[i] += i + 1u;
        value += fixture.pressure[i];
    }
    sink = value;
}

static asian_affine_family_carrier_input_t carrier_input(uint32_t variant)
{
    const input_t *in = &inputs[variant];
    return (asian_affine_family_carrier_input_t){
        in->rate, in->dividend, in->sigma, in->maturity, fixture.n
    };
}

static asian_affine_family_request_input_t request_input(uint32_t variant,
                                                          int reuse)
{
    const input_t *request = &inputs[variant];
    const input_t *market = reuse ? &inputs[0] : request;
    return (asian_affine_family_request_input_t){
        .s0 = request->s0,
        .rate = market->rate,
        .dividend_yield = market->dividend,
        .sigma = market->sigma,
        .maturity = market->maturity,
        .future_fixings = fixture.n,
        .completed_fixings = 0u,
        .initial_arithmetic_sum = 0.0,
        .past_log_sum = 0.0,
        .strikes = &request->strike,
        .strike_count = 1u,
        .workload = ASIAN_AFFINE_FAMILY_PRICE,
    };
}

static int prepare_request(enum candidate candidate,
                           const asian_2048_carrier_t *carrier,
                           const asian_affine_family_request_input_t *input,
                           void *request)
{
    if (candidate == VANILLA_2048)
        return asian_2048_request_prepare(fixture.engine, carrier, input,
                                          request);
    return asian_4096_vanilla_request_prepare(fixture.engine, carrier, input,
                                              request);
}

static int price(enum candidate candidate, const void *request,
                 asian_2048_output_t *output)
{
    if (candidate == VANILLA_2048)
        return asian_2048_prepared_price((asian_2048_request_t *)request,
                                         output);
    return asian_4096_vanilla_prepared_price(request, output);
}

static void *prepared_request(enum candidate candidate, uint32_t variant)
{
    return candidate == VANILLA_2048 ?
        (void *)fixture.prepared_2048[variant] :
        (void *)fixture.prepared_4096[variant];
}

static void *scratch_request(enum candidate candidate)
{
    return candidate == VANILLA_2048 ? (void *)fixture.scratch_2048 :
                                      (void *)fixture.scratch_4096;
}

static int run(enum candidate candidate, enum lifecycle lifecycle,
               uint32_t variant)
{
    if (lifecycle == PREPARED)
        return price(candidate, prepared_request(candidate, variant),
                     &fixture.output[candidate]);
    const int reuse = lifecycle == REUSED;
    const asian_affine_family_request_input_t ri =
        request_input(variant, reuse);
    asian_2048_carrier_t *carrier = reuse ? fixture.prepared_carrier[0] :
        fixture.scratch_carrier[candidate];
    if (!reuse) {
        const asian_affine_family_carrier_input_t ci = carrier_input(variant);
        if (asian_2048_carrier_prepare(fixture.engine, &ci, carrier) != 0)
            return -1;
    }
    if (prepare_request(candidate, carrier, &ri,
                        scratch_request(candidate)) != 0)
        return -1;
    return price(candidate, scratch_request(candidate),
                 &fixture.output[candidate]);
}

static void condition(enum candidate candidate, enum lifecycle lifecycle,
                      enum cache_mode cache, uint32_t variant)
{
    if (cache == PRESSURE_32KIB) {
        pressure();
        return;
    }
    if (candidate == VANILLA_2048) {
        warm_bytes(fixture.engine->plan, ASIAN_2048_PLAN_HEADER_BYTES);
        warm_bytes(fixture.engine->plan->contexts,
                   fixture.n * sizeof(fixture.engine->plan->contexts[0]));
    } else {
        warm_bytes(fixture.engine->parent.affine_plan,
                   ASIAN_META_PLAN_HEADER_BYTES);
        warm_bytes(fixture.engine->parent.affine_plan->contexts,
                   fixture.n * sizeof(
                       fixture.engine->parent.affine_plan->contexts[0]));
    }
    if (lifecycle != FRESH)
        warm_bytes(fixture.prepared_carrier[lifecycle == PREPARED ? variant : 0]
                       ->growth, ASIAN_2048_GROWTH_BYTES);
    if (lifecycle == REUSED)
        warm_bytes(&fixture.prepared_carrier[0]->market,
                   sizeof(fixture.prepared_carrier[0]->market));
    if (lifecycle == FRESH)
        warm_bytes(asian_genuine_fixed_block_signed_z,
                   ASIAN_2048_GROWTH_BYTES);
    if (lifecycle == FRESH)
        warm_bytes(fixture.scratch_carrier[candidate]->growth,
                   ASIAN_2048_GROWTH_BYTES);
    if (lifecycle == PREPARED) {
        if (candidate == VANILLA_2048) {
            const asian_2048_request_t *request =
                fixture.prepared_2048[variant];
            warm_bytes(request->routes,
                       fixture.n * sizeof(request->routes[0]));
            warm_bytes(&request->growth, sizeof(request->growth));
            warm_bytes(&request->payoff, sizeof(request->payoff));
            warm_bytes(&request->strike, sizeof(request->strike));
        } else {
            const asian_4096_vanilla_request_t *request =
                fixture.prepared_4096[variant];
            warm_bytes(request->routes,
                       fixture.n * sizeof(request->routes[0]));
            warm_bytes(&request->growth, sizeof(request->growth));
            warm_bytes(&request->payoff, sizeof(request->payoff));
            warm_bytes(&request->strike, sizeof(request->strike));
        }
    }
}

static int validate(enum candidate candidate, enum lifecycle lifecycle,
                    uint32_t variant)
{
    const asian_2048_output_t *expected =
        &fixture.expected[candidate][lifecycle][variant];
    if (memcmp(&fixture.output[candidate], expected, sizeof(*expected)) != 0) {
        fprintf(stderr, "identity mismatch candidate=%u lifecycle=%u variant=%u\n",
                candidate, lifecycle, variant);
        return -1;
    }
    const unsigned char *bytes =
        (const unsigned char *)(const void *)&fixture.output[candidate];
    sink ^= bytes[(candidate * 7u + variant * 3u) % sizeof(*expected)];
    return 0;
}

static int observe(enum candidate candidate, enum lifecycle lifecycle,
                   enum cache_mode cache, uint32_t variant, timing_t *timing)
{
    condition(candidate, lifecycle, cache, variant);
    const uint64_t wall0 = wall_now();
    const uint64_t tsc0 = tsc_begin();
    const int status = run(candidate, lifecycle, variant);
    const uint64_t tsc1 = tsc_end();
    const uint64_t wall1 = wall_now();
    if (status != 0 || validate(candidate, lifecycle, variant) != 0)
        return -1;
    timing->wall = (double)(wall1 - wall0);
    timing->tsc = (double)(tsc1 - tsc0);
    return 0;
}

static int expected_outputs(void)
{
    for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate)
        for (uint32_t lifecycle = 0; lifecycle < LIFECYCLES; ++lifecycle)
            for (uint32_t variant = 0; variant < 2u; ++variant) {
                if (run((enum candidate)candidate,
                        (enum lifecycle)lifecycle, variant) != 0)
                    return -1;
                fixture.expected[candidate][lifecycle][variant] =
                    fixture.output[candidate];
                if (run((enum candidate)candidate,
                        (enum lifecycle)lifecycle, variant) != 0 ||
                    memcmp(&fixture.output[candidate],
                        &fixture.expected[candidate][lifecycle][variant],
                        sizeof(asian_2048_output_t)) != 0)
                    return -1;
            }
    return 0;
}

static int fixture_create(asian_2048_engine_t *engine, uint32_t n)
{
    memset(&fixture, 0, sizeof(fixture));
    fixture.engine = engine;
    fixture.n = n;
    for (uint32_t variant = 0; variant < 2u; ++variant) {
        fixture.prepared_carrier[variant] = a64(sizeof(asian_2048_carrier_t));
        fixture.prepared_2048[variant] = a64(sizeof(asian_2048_request_t));
        fixture.prepared_4096[variant] =
            a64(sizeof(asian_4096_vanilla_request_t));
    }
    for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate)
        fixture.scratch_carrier[candidate] = a64(sizeof(asian_2048_carrier_t));
    fixture.scratch_2048 = a64(sizeof(*fixture.scratch_2048));
    fixture.scratch_4096 = a64(sizeof(*fixture.scratch_4096));
    if (fixture.prepared_carrier[0] == NULL ||
        fixture.prepared_carrier[1] == NULL ||
        fixture.prepared_2048[0] == NULL || fixture.prepared_2048[1] == NULL ||
        fixture.prepared_4096[0] == NULL ||
        fixture.prepared_4096[1] == NULL ||
        fixture.scratch_carrier[0] == NULL ||
        fixture.scratch_carrier[1] == NULL ||
        fixture.scratch_2048 == NULL || fixture.scratch_4096 == NULL)
        return -1;
    for (uint32_t variant = 0; variant < 2u; ++variant)
        asian_2048_carrier_initialize(fixture.prepared_carrier[variant]);
    for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate)
        asian_2048_carrier_initialize(fixture.scratch_carrier[candidate]);
    for (uint32_t variant = 0; variant < 2u; ++variant) {
        const asian_affine_family_carrier_input_t ci = carrier_input(variant);
        const asian_affine_family_request_input_t ri = request_input(variant, 0);
        if (asian_2048_carrier_prepare(engine, &ci,
                fixture.prepared_carrier[variant]) != 0 ||
            asian_2048_request_prepare(engine,
                fixture.prepared_carrier[variant], &ri,
                fixture.prepared_2048[variant]) != 0 ||
            asian_4096_vanilla_request_prepare(engine,
                fixture.prepared_carrier[variant], &ri,
                fixture.prepared_4096[variant]) != 0)
            return -1;
    }
    return expected_outputs();
}

static void fixture_destroy(void)
{
    free(fixture.scratch_4096); free(fixture.scratch_2048);
    for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate)
        free(fixture.scratch_carrier[candidate]);
    for (uint32_t variant = 0; variant < 2u; ++variant) {
        free(fixture.prepared_4096[variant]);
        free(fixture.prepared_2048[variant]);
        free(fixture.prepared_carrier[variant]);
    }
    memset(&fixture, 0, sizeof(fixture));
}

static int paired(enum lifecycle lifecycle, enum cache_mode cache,
                  timing_t result[CANDIDATES])
{
    double wall[CANDIDATES][SAMPLES], tsc[CANDIDATES][SAMPLES];
    for (uint32_t phase = 0; phase < WARMUPS + SAMPLES; ++phase) {
        const enum candidate order[4] = {
            phase & 1u ? VANILLA_4096 : VANILLA_2048,
            phase & 1u ? VANILLA_2048 : VANILLA_4096,
            phase & 1u ? VANILLA_2048 : VANILLA_4096,
            phase & 1u ? VANILLA_4096 : VANILLA_2048,
        };
        timing_t observations[CANDIDATES][2];
        uint32_t counts[CANDIDATES] = {0u, 0u};
        for (uint32_t slot = 0; slot < 4u; ++slot) {
            const enum candidate candidate = order[slot];
            const uint32_t variant = (phase + slot) & 1u;
            if (observe(candidate, lifecycle, cache, variant,
                        &observations[candidate][counts[candidate]++]) != 0)
                return -1;
        }
        if (phase >= WARMUPS) {
            const uint32_t sample = phase - WARMUPS;
            for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate) {
                wall[candidate][sample] =
                    (observations[candidate][0].wall +
                     observations[candidate][1].wall) * 0.5;
                tsc[candidate][sample] =
                    (observations[candidate][0].tsc +
                     observations[candidate][1].tsc) * 0.5;
            }
        }
    }
    for (uint32_t candidate = 0; candidate < CANDIDATES; ++candidate) {
        result[candidate].wall = median(wall[candidate]);
        result[candidate].tsc = median(tsc[candidate]);
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t cpu = UINT32_MAX;
    int check_host = 0;
    for (int arg = 1; arg < argc; ++arg)
        if (strcmp(argv[arg], "--cpu") == 0 && ++arg < argc)
            cpu = (uint32_t)strtoul(argv[arg], NULL, 10);
        else if (strcmp(argv[arg], "--check-host") == 0)
            check_host = 1;
        else
            return 2;
    if (cpu != 0u || !is_spr() || pin_cpu(cpu) != 0) {
        fprintf(stderr, "Sapphire Rapids family-6/model-143 CPU=0 required\n");
        return 2;
    }
    if (check_host)
        return 0;
    asian_2048_engine_t *engine = a64(sizeof(*engine));
    if (engine == NULL || asian_2048_engine_create(engine) != 0)
        return 1;

    const uint32_t ns[5] = {16u, 32u, 64u, 128u, 256u};
    timing_t results[5][LIFECYCLES][CACHES][CANDIDATES];
    asian_2048_output_t n64_accuracy[CANDIDATES] = {{0.0, 0.0}, {0.0, 0.0}};
    puts("N paths lifecycle cache wall_ns tsc_ticks valuations_per_sec "
         "path_fixing_updates_per_sec correctness");
    for (uint32_t ni = 0; ni < 5u; ++ni) {
        if (fixture_create(engine, ns[ni]) != 0)
            return 1;
        for (uint32_t lifecycle = 0; lifecycle < LIFECYCLES; ++lifecycle)
            for (uint32_t cache = 0; cache < CACHES; ++cache) {
                timing_t *row = results[ni][lifecycle][cache];
                if (paired((enum lifecycle)lifecycle,
                           (enum cache_mode)cache, row) != 0)
                    return 1;
                for (uint32_t candidate = 0; candidate < CANDIDATES;
                     ++candidate) {
                    const uint32_t paths = candidate == VANILLA_2048 ?
                        2048u : 4096u;
                    const double valuations = 1e9 / row[candidate].wall;
                    const double updates = valuations * paths * ns[ni];
                    printf("%u %u %s %s %.1f %.1f %.6f %.6f PASS\n",
                        ns[ni], paths, lifecycle_names[lifecycle],
                        cache_names[cache], row[candidate].wall,
                        row[candidate].tsc, valuations, updates);
                }
                const double latency = row[VANILLA_4096].wall /
                                       row[VANILLA_2048].wall;
                printf("ratio N=%u lifecycle=%s cache=%s "
                       "latency_ratio_4096_over_2048=%.6f "
                       "updates_per_sec_ratio_2048_over_4096=%.6f\n",
                       ns[ni], lifecycle_names[lifecycle], cache_names[cache],
                       latency, latency * 0.5);
            }
        if (ns[ni] == 64u) {
            n64_accuracy[VANILLA_2048] =
                fixture.expected[VANILLA_2048][PREPARED][0];
            n64_accuracy[VANILLA_4096] =
                fixture.expected[VANILLA_4096][PREPARED][0];
        }
        fixture_destroy();
    }

    puts("N 2048_us 4096_us latency_ratio normalized_efficiency");
    int win = 1;
    for (uint32_t ni = 0; ni < 5u; ++ni) {
        const timing_t *row = results[ni][PREPARED][CANDIDATE_WARM];
        const double latency = row[VANILLA_4096].wall /
                               row[VANILLA_2048].wall;
        const double efficiency = latency * 0.5;
        printf("%u %.6f %.6f %.6f %.6f\n", ns[ni],
               row[VANILLA_2048].wall / 1000.0,
               row[VANILLA_4096].wall / 1000.0, latency, efficiency);
        if ((ns[ni] == 128u || ns[ni] == 256u) && efficiency < 1.01)
            win = 0;
    }
    const double references[2] = {5.34876366, 3.85536588};
    const char *sides[2] = {"call", "put"};
    puts("side reference plain_2048 plain_2048_signed_error "
         "plain_2048_abs_error plain_4096 plain_4096_signed_error "
         "plain_4096_abs_error");
    for (uint32_t side = 0; side < 2u; ++side) {
        const double value2048 = side == 0u ?
            n64_accuracy[VANILLA_2048].call_price :
            n64_accuracy[VANILLA_2048].put_price;
        const double value4096 = side == 0u ?
            n64_accuracy[VANILLA_4096].call_price :
            n64_accuracy[VANILLA_4096].put_price;
        printf("%s %.8f %.17g %.17g %.17g %.17g %.17g %.17g\n",
               sides[side], references[side], value2048,
               value2048 - references[side],
               fabs(value2048 - references[side]), value4096,
               value4096 - references[side],
               fabs(value4096 - references[side]));
    }
    puts(win ? "performance_verdict=2048_PER_PATH_WIN" :
               "performance_verdict=NO_2048_PER_PATH_WIN");
    asian_2048_engine_destroy(engine);
    free(engine);
    return 0;
}
