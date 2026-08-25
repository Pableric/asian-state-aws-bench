#define _GNU_SOURCE

#include "private/asian_geocv_affine_lifecycle_diag.h"

#include <cpuid.h>
#include <immintrin.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WARMUPS = 16, SAMPLES = 101 };

typedef struct { uint64_t wall, tsc; } duration_t;

typedef struct __attribute__((aligned(64))) {
    asian_geocv_affine_engine_t engine;
    asian_geocv_affine_carrier_t carrier_a;
    asian_geocv_affine_carrier_t carrier_b;
    asian_geocv_affine_carrier_t scratch_carrier;
    asian_geocv_affine_request_t request_a;
    asian_geocv_affine_request_t request_b;
    asian_geocv_affine_request_t reuse_a;
    asian_geocv_affine_request_t reuse_b;
    asian_geocv_affine_request_t scratch_request;
    asian_genuine_strip_output_t output;
    float materialized_q[ASIAN_GEOCV_AFFINE_PATHS];
    float materialized_g[ASIAN_GEOCV_AFFINE_PATHS];
} workspace_t;

static const asian_geocv_affine_request_input_t input_a = {
    100.0, 100.0, 0.03, 0.0, 0.20, 1.0};
static const asian_geocv_affine_request_input_t input_b = {
    117.0, 110.0, -0.01, 0.015, 0.35, 0.75};
static const asian_geocv_affine_request_input_t reuse_b = {
    117.0, 110.0, 0.03, 0.0, 0.20, 1.0};

static workspace_t *workspace;
static volatile uint64_t sink;

static asian_geocv_affine_carrier_input_t carrier_input(
    const asian_geocv_affine_request_input_t *input)
{
    const asian_geocv_affine_carrier_input_t value = {
        input->rate, input->dividend_yield, input->sigma, input->maturity};
    return value;
}

static uint64_t result_hash(const asian_genuine_strip_output_t *output)
{
    uint64_t value = UINT64_C(1469598103934665603);
    const unsigned char *bytes = (const unsigned char *)&output->values[0];
    for (size_t i = 0; i < sizeof(output->values[0]); ++i) {
        value ^= bytes[i];
        value *= UINT64_C(1099511628211);
    }
    return value;
}

static int same_price(const asian_genuine_strip_output_t *a,
                      const asian_genuine_strip_output_t *b)
{
    return memcmp(&a->values[0].call_price, &b->values[0].call_price,
                  2u * sizeof(double)) == 0;
}

static int materialized_compare(const asian_geocv_affine_carrier_t *carrier,
                                asian_geocv_affine_request_t *request,
                                const asian_genuine_strip_output_t *expected)
{
    asian_geometric_cv_packet_local_context_t context
        __attribute__((aligned(64)));
    asian_genuine_strip_output_t actual __attribute__((aligned(64)));
    memset(&actual, 0, sizeof(actual));
    if (asian_geometric_cv_packet_local_prepare(
            &context, request->routes, ASIAN_GEOCV_AFFINE_N64,
            request->immediate.s0, carrier->x, sizeof(carrier->x),
            carrier->growth, sizeof(carrier->growth), &request->strip,
            workspace->materialized_q, sizeof(workspace->materialized_q),
            workspace->materialized_g, sizeof(workspace->materialized_g)) != 0)
        return -1;
    asian_geometric_cv_packet_local_qg_diag(&context);
    if (asian_geometric_cv_packet_local_strip_consume_padded(
            workspace->materialized_q, workspace->materialized_g,
            &request->strip, 1u, 0, &actual) != 0)
        return -1;
    return same_price(expected, &actual) ? 0 : -1;
}

static int check_expected_accuracy_bits(
    const asian_genuine_strip_output_t *output)
{
    const double expected_call = 0x1.563fd989e061ap+2;
    const double expected_put = 0x1.ed571d631bf74p+1;
    return memcmp(&output->values[0].call_price, &expected_call,
                  sizeof(expected_call)) == 0 &&
           memcmp(&output->values[0].put_price, &expected_put,
                  sizeof(expected_put)) == 0;
}

static int bounded_correctness(void)
{
    asian_genuine_strip_output_t first __attribute__((aligned(64)));
    asian_genuine_strip_output_t repeat __attribute__((aligned(64)));
    const asian_geocv_affine_carrier_input_t ca = carrier_input(&input_a);
    const asian_geocv_affine_carrier_input_t cb = carrier_input(&input_b);
    if (asian_geocv_affine_carrier_prepare(
            &workspace->engine, &ca, &workspace->carrier_a) != 0 ||
        asian_geocv_affine_carrier_prepare(
            &workspace->engine, &cb, &workspace->carrier_b) != 0 ||
        asian_geocv_affine_request_prepare(
            &workspace->engine, &workspace->carrier_a, &input_a,
            &workspace->request_a) != 0 ||
        asian_geocv_affine_request_prepare(
            &workspace->engine, &workspace->carrier_b, &input_b,
            &workspace->request_b) != 0 ||
        asian_geocv_affine_request_prepare(
            &workspace->engine, &workspace->carrier_a, &input_a,
            &workspace->reuse_a) != 0 ||
        asian_geocv_affine_request_prepare(
            &workspace->engine, &workspace->carrier_a, &reuse_b,
            &workspace->reuse_b) != 0)
        return -1;

    asian_geocv_affine_request_t *requests[3] = {
        &workspace->request_a, &workspace->request_b, &workspace->reuse_b};
    asian_geocv_affine_carrier_t *carriers[3] = {
        &workspace->carrier_a, &workspace->carrier_b, &workspace->carrier_a};
    for (uint32_t i = 0; i < 3u; ++i) {
        if (asian_geocv_affine_prepared_price(requests[i], &first) != 0 ||
            asian_geocv_affine_prepared_price(requests[i], &repeat) != 0 ||
            !same_price(&first, &repeat) ||
            materialized_compare(carriers[i], requests[i], &first) != 0)
            return -1;
        if (i == 0u && !check_expected_accuracy_bits(&first)) {
            fprintf(stderr,
                    "N64 GeoCV accuracy-bit mismatch call=%a put=%a\n",
                    first.values[0].call_price, first.values[0].put_price);
            return -1;
        }
    }

    if (asian_geocv_affine_fresh_total(
            &workspace->engine, &input_a, &workspace->scratch_carrier,
            &workspace->scratch_request, &first) != 0 ||
        !check_expected_accuracy_bits(&first) ||
        asian_geocv_affine_reuse_total(
            &workspace->engine, &workspace->carrier_a, &input_a,
            &workspace->scratch_request, &repeat) != 0 ||
        !same_price(&first, &repeat))
        return -1;
    printf("n64_geocv_exact call=%a put=%a accuracy_harness_bits=YES\n",
           first.values[0].call_price, first.values[0].put_price);
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

static int compare_u64(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static uint64_t median(uint64_t values[SAMPLES])
{
    qsort(values, SAMPLES, sizeof(values[0]), compare_u64);
    return values[SAMPLES / 2u];
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

enum operation {
    OP_BRACKET,
    OP_CARRIER,
    OP_REQUEST,
    OP_PREPARED_PRICE,
    OP_FRESH,
    OP_REUSE,
};

static int invoke(enum operation operation, uint32_t variant)
{
    const asian_geocv_affine_request_input_t *input =
        variant ? &input_b : &input_a;
    const asian_geocv_affine_carrier_input_t ci = carrier_input(input);
    asian_geocv_affine_carrier_t *carrier =
        variant ? &workspace->carrier_b : &workspace->carrier_a;
    asian_geocv_affine_request_t *request =
        variant ? &workspace->request_b : &workspace->request_a;
    switch (operation) {
    case OP_BRACKET:
        return 0;
    case OP_CARRIER:
        return asian_geocv_affine_carrier_prepare(
            &workspace->engine, &ci, &workspace->scratch_carrier);
    case OP_REQUEST:
        return asian_geocv_affine_request_prepare(
            &workspace->engine, carrier, input, &workspace->scratch_request);
    case OP_PREPARED_PRICE:
        return asian_geocv_affine_prepared_price(request, &workspace->output);
    case OP_FRESH:
        return asian_geocv_affine_fresh_total(
            &workspace->engine, input, &workspace->scratch_carrier,
            &workspace->scratch_request, &workspace->output);
    case OP_REUSE: {
        const asian_geocv_affine_request_input_t *reuse =
            variant ? &reuse_b : &input_a;
        return asian_geocv_affine_reuse_total(
            &workspace->engine, &workspace->carrier_a, reuse,
            &workspace->scratch_request, &workspace->output);
    }
    }
    return -1;
}

static duration_t measure(enum operation operation)
{
    uint64_t walls[SAMPLES], ticks[SAMPLES];
    for (uint32_t i = 0; i < WARMUPS; ++i)
        if (invoke(operation, i & 1u) != 0)
            abort();
    for (uint32_t i = 0; i < SAMPLES; ++i) {
        const uint32_t variant = i & 1u;
        const uint64_t w0 = wall_now();
        const uint64_t t0 = tsc_begin();
        if (invoke(operation, variant) != 0)
            abort();
        const uint64_t t1 = tsc_end();
        const uint64_t w1 = wall_now();
        walls[i] = w1 - w0;
        ticks[i] = t1 - t0;
        sink += result_hash(&workspace->output);
    }
    return (duration_t){median(walls), median(ticks)};
}

static void print_metric(const char *name, duration_t value)
{
    printf("%-28s %10" PRIu64 " %12" PRIu64 "\n",
           name, value.wall, value.tsc);
}

static int native_benchmark(int cpu)
{
    if (cpu < 0 || pin_cpu(cpu) != 0 || !is_sapphire_rapids()) {
        fprintf(stderr, "native timing requires a pinned Sapphire Rapids CPU\n");
        return -1;
    }
    puts("metric                          wall_ns    tsc_ticks");
    print_metric("timing_bracket", measure(OP_BRACKET));
    print_metric("exact_x_growth_prepare", measure(OP_CARRIER));
    print_metric("request_prepare", measure(OP_REQUEST));
    print_metric("prepared_price", measure(OP_PREPARED_PRICE));
    print_metric("fresh_total", measure(OP_FRESH));
    print_metric("reuse_total", measure(OP_REUSE));
    return 0;
}

int main(int argc, char **argv)
{
    int native = 0, cpu = -1;
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--check-only") == 0)) {
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
    if (asian_geocv_affine_engine_create(&workspace->engine) != 0 ||
        bounded_correctness() != 0) {
        fprintf(stderr, "N64 affine GeoCV correctness failed\n");
        asian_geocv_affine_engine_destroy(&workspace->engine);
        free(workspace);
        return 1;
    }
    puts(native ? "bounded_native_correctness PASS" :
                  "bounded_sde_correctness PASS");
    const int status = native ? native_benchmark(cpu) : 0;
    asian_geocv_affine_engine_destroy(&workspace->engine);
    free(workspace);
    return status == 0 ? 0 : 1;
}
