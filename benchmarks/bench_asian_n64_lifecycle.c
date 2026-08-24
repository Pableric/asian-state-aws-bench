#define _GNU_SOURCE

#include "asian_arithmetic_pricer.h"
#include "private/asian_genuine_arithmetic_fused_source_exp_diag.h"
#include "private/asian_genuine_arithmetic_growth_only_diag.h"
#include "private/asian_genuine_arithmetic_growth_only_sha256.h"
#include "private/asian_genuine_arithmetic_growth_only_strip_adapter.h"
#include "private/asian_genuine_fixed_block_source_diag.h"
#include "private/asian_genuine_permute.h"

#include <cpuid.h>
#include <immintrin.h>
#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    N64 = 64,
    WARMUPS = 16,
    SAMPLES = 101,
    PLAN_MAGIC = 0x4e4c5041u,
    ENGINE_MAGIC = 0x454c364eu,
    CARRIER_MAGIC = 0x5243364eu,
    REQUEST_MAGIC = 0x5152364eu,
    JOE_KUO_256_BYTES = 33792,
};

/* Exact private layout at the pinned production parent. */
struct asian_arithmetic_route_plan {
    uint32_t magic;
    uint8_t donor_region[ASIAN_ARITHMETIC_MAX_FIXINGS];
    uint8_t reserved[60];
    fragment_map_t maps[ASIAN_ARITHMETIC_MAX_FIXINGS];
};

_Static_assert(offsetof(struct asian_arithmetic_route_plan, maps) == 320,
               "production route-plan map offset");

extern const unsigned char asian_arithmetic_joe_kuo_256_records[];

typedef struct __attribute__((aligned(64))) {
    const asian_arithmetic_route_plan_t *plan;
    float signed_z_min;
    float signed_z_max;
    uint32_t magic;
    uint8_t reserved[36];
} clean_engine_t;

typedef struct __attribute__((aligned(64))) {
    float growth[ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DONOR_VALUES];
    asian_genuine_arithmetic_fused_source_exp_context_t fused;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    uint32_t fixing_count;
    uint32_t magic;
    uint8_t reserved[24];
} clean_carrier_t;

typedef struct __attribute__((aligned(64))) {
    asian_genuine_route_t routes[N64];
    asian_genuine_arithmetic_growth_only_context_t growth_context;
    asian_genuine_strip_context_t strip;
    float q[ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_PATHS];
    const clean_carrier_t *carrier;
    uint32_t strike_count;
    asian_arithmetic_selected_path_t selected_path;
    uint32_t magic;
    uint8_t reserved[40];
} clean_request_t;

typedef struct {
    const fragment_map_t *maps;
    const uint8_t *donor_region;
    uint32_t fixing_count;
} clean_plan_binding_t;

typedef struct {
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
} carrier_input_t;

typedef struct __attribute__((aligned(64))) {
    clean_carrier_t component_carrier;
    clean_carrier_t carrier_a;
    clean_carrier_t carrier_b;
    clean_carrier_t fresh_carrier;
    clean_request_t component_request;
    clean_request_t request_a;
    clean_request_t request_b;
    clean_request_t reuse_request_a;
    clean_request_t reuse_request_b;
    clean_request_t fresh_request;
} persistent_workspace_t;

typedef struct { uint64_t wall, tsc; } timing_t;

enum operation {
    OP_BRACKET,
    OP_ROUTE_PLAN_CREATE,
    OP_CURRENT_PREPARE,
    OP_CURRENT_PRICE,
    OP_CURRENT_PREPARE_FIRST,
    OP_CLEAN_PLAN_LOOKUP,
    OP_CLEAN_CARRIER_PREPARE,
    OP_CLEAN_REQUEST_PREPARE,
    OP_CLEAN_PREPARED_PRICE,
    OP_CLEAN_FRESH,
    OP_REUSE_REQUEST_PREPARE,
    OP_REUSE_PREPARED_PRICE,
    OP_REUSE_TOTAL,
};

static const float strike_a = 100.0f;
static const float strike_b = 110.0f;

static const asian_arithmetic_request_t request_a = {
    .s0 = 100.0, .rate = 0.03, .dividend_yield = 0.0,
    .sigma = 0.20, .maturity = 1.0, .future_fixings = N64,
    .completed_fixings = 0, .completed_arithmetic_sum = 0.0,
    .completed_log_sum = 0.0, .strikes = &strike_a, .strike_count = 1,
    .workload = ASIAN_ARITHMETIC_PRICE,
};

static const asian_arithmetic_request_t request_b = {
    .s0 = 117.0, .rate = -0.01, .dividend_yield = 0.015,
    .sigma = 0.35, .maturity = 0.75, .future_fixings = N64,
    .completed_fixings = 0, .completed_arithmetic_sum = 0.0,
    .completed_log_sum = 0.0, .strikes = &strike_b, .strike_count = 1,
    .workload = ASIAN_ARITHMETIC_PRICE,
};

static const asian_arithmetic_request_t reuse_request_b = {
    .s0 = 117.0, .rate = 0.03, .dividend_yield = 0.0,
    .sigma = 0.20, .maturity = 1.0, .future_fixings = N64,
    .completed_fixings = 0, .completed_arithmetic_sum = 0.0,
    .completed_log_sum = 0.0, .strikes = &strike_b, .strike_count = 1,
    .workload = ASIAN_ARITHMETIC_PRICE,
};

static clean_engine_t engine;
static persistent_workspace_t *workspace;
static asian_arithmetic_route_plan_t *production_plan;
static asian_arithmetic_prepared_t *current_prepared[2];
static asian_arithmetic_prepared_t *reuse_current_prepared[2];
static asian_arithmetic_prepared_t *temporary_prepared;
static asian_arithmetic_route_plan_t *temporary_plan;
static clean_plan_binding_t temporary_binding;
static asian_arithmetic_result_t timed_result;
static uint32_t active_input;
static volatile uint64_t sink_value;

static const asian_arithmetic_request_t *fresh_request(uint32_t input)
{
    return input == 0u ? &request_a : &request_b;
}

static const asian_arithmetic_request_t *reuse_request(uint32_t input)
{
    return input == 0u ? &request_a : &reuse_request_b;
}

static carrier_input_t carrier_from_request(const asian_arithmetic_request_t *r)
{
    return (carrier_input_t){r->rate, r->dividend_yield, r->sigma, r->maturity};
}

static void *aligned_zero(size_t bytes)
{
    void *pointer = NULL;
    if (posix_memalign(&pointer, 64u, bytes) != 0) return NULL;
    memset(pointer, 0, bytes);
    return pointer;
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

static int same_double(double a, double b)
{
    uint64_t ab, bb;
    memcpy(&ab, &a, sizeof(ab));
    memcpy(&bb, &b, sizeof(bb));
    return ab == bb;
}

__attribute__((noinline, used))
int asian_n64_clean_plan_lookup(const clean_engine_t *clean,
                                clean_plan_binding_t *out)
{
    if (clean == NULL || out == NULL || clean->magic != ENGINE_MAGIC ||
        clean->plan == NULL ||
        ((const struct asian_arithmetic_route_plan *)clean->plan)->magic !=
          PLAN_MAGIC)
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;
    const struct asian_arithmetic_route_plan *plan =
        (const struct asian_arithmetic_route_plan *)clean->plan;
    out->maps = plan->maps;
    out->donor_region = plan->donor_region;
    out->fixing_count = N64;
    return ASIAN_ARITHMETIC_OK;
}

__attribute__((noinline, used))
int asian_n64_clean_carrier_prepare(const clean_engine_t *clean,
                                    const carrier_input_t *input,
                                    clean_carrier_t *out)
{
    if (clean == NULL || input == NULL || out == NULL ||
        clean->magic != ENGINE_MAGIC || ((uintptr_t)out & 63u) != 0u ||
        !isfinite(input->rate) || !isfinite(input->dividend_yield) ||
        !isfinite(input->sigma) || !(input->sigma > 0.0) ||
        !isfinite(input->maturity) || !(input->maturity > 0.0))
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;

    const double dt = input->maturity / (double)N64;
    const float drift = (float)((input->rate - input->dividend_yield -
                                 0.5 * input->sigma * input->sigma) * dt);
    const float diffusion = (float)(input->sigma * sqrt(dt));
    if (!isfinite(drift) || !isfinite(diffusion) || !(diffusion > 0.0f) ||
        drift < -0.25f || drift > 0.25f || diffusion > 0.20f)
        return ASIAN_ARITHMETIC_UNSUPPORTED_CONTRACT;
    const float x0 = fmaf(diffusion, clean->signed_z_min, drift);
    const float x1 = fmaf(diffusion, clean->signed_z_max, drift);
    if (!isfinite(x0) || !isfinite(x1) || x0 < -87.0f || x1 > 88.0f)
        return ASIAN_ARITHMETIC_UNSUPPORTED_CONTRACT;

    memset(&out->fused, 0, sizeof(out->fused));
    out->fused.signed_z = asian_genuine_fixed_block_signed_z;
    out->fused.growth_out = out->growth;
    out->fused.drift = drift;
    out->fused.diffusion = diffusion;
    out->fused.fixing_count = N64;
    out->fused.path_count = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_PATHS;
    out->fused.region_count = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_REGIONS;
    out->fused.values_per_region =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_VALUES_PER_REGION;
    out->fused.first_index = ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    out->fused.total_values =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_TOTAL_VALUES;
    out->fused.magic = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC;
    out->fused.abi_version =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION;
    out->rate = input->rate;
    out->dividend_yield = input->dividend_yield;
    out->sigma = input->sigma;
    out->maturity = input->maturity;
    out->fixing_count = N64;
    out->magic = CARRIER_MAGIC;
    asian_genuine_arithmetic_fused_source_exp_diag(&out->fused);
    return ASIAN_ARITHMETIC_OK;
}

static int valid_clean_request(const clean_carrier_t *carrier,
                               const asian_arithmetic_request_t *request)
{
    if (carrier == NULL || request == NULL || carrier->magic != CARRIER_MAGIC ||
        request->strikes == NULL || !isfinite(request->s0) ||
        !(request->s0 > 0.0) || request->future_fixings != N64 ||
        request->completed_fixings != 0u ||
        !same_double(request->completed_arithmetic_sum, 0.0) ||
        !same_double(request->completed_log_sum, 0.0) ||
        request->strike_count != 1u || request->workload != ASIAN_ARITHMETIC_PRICE ||
        !isfinite(request->strikes[0]) || !(request->strikes[0] > 0.0f) ||
        !same_double(request->rate, carrier->rate) ||
        !same_double(request->dividend_yield, carrier->dividend_yield) ||
        !same_double(request->sigma, carrier->sigma) ||
        !same_double(request->maturity, carrier->maturity))
        return 0;
    return 1;
}

__attribute__((noinline, used))
int asian_n64_clean_request_prepare(const clean_engine_t *clean,
                                    const clean_carrier_t *carrier,
                                    const asian_arithmetic_request_t *request,
                                    clean_request_t *out)
{
    clean_plan_binding_t binding;
    if (out == NULL || ((uintptr_t)out & 63u) != 0u ||
        !valid_clean_request(carrier, request) ||
        asian_n64_clean_plan_lookup(clean, &binding) != ASIAN_ARITHMETIC_OK)
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;

    for (uint32_t fixing = 0; fixing < N64; ++fixing) {
        out->routes[fixing].x_base = NULL;
        out->routes[fixing].growth_base = carrier->growth +
            (size_t)binding.donor_region[fixing] * ASIAN_ARITHMETIC_PATHS;
        out->routes[fixing].map = &binding.maps[fixing];
        out->routes[fixing].weight_bits = 0u;
        out->routes[fixing].fixing_index = fixing;
    }

    memset(&out->growth_context, 0, sizeof(out->growth_context));
    out->growth_context.d1_growth = carrier->growth;
    out->growth_context.routes_d2 = out->routes + 1;
    out->growth_context.q_out = out->q;
    out->growth_context.fixing_count = N64;
    out->growth_context.s0 = (float)request->s0;
    out->growth_context.magic = ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC;
    out->growth_context.abi_version =
        ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION;

    uint32_t padded = 0;
    if (asian_genuine_arithmetic_growth_only_strip_prepare_padded(
            &out->strip, request->s0, request->rate,
            request->dividend_yield, request->sigma, request->maturity,
            N64, 0u, 0.0, 0.0, request->strikes, 1u, &padded) != 0 ||
        padded != 1u)
        return ASIAN_ARITHMETIC_UNSUPPORTED_CONTRACT;
    out->carrier = carrier;
    out->strike_count = 1u;
    out->selected_path = ASIAN_ARITHMETIC_SELECTED_STAGE1;
    out->magic = REQUEST_MAGIC;
    return ASIAN_ARITHMETIC_OK;
}

__attribute__((noinline, used))
int asian_n64_clean_prepared_price(clean_request_t *request,
                                   asian_arithmetic_result_t *out)
{
    if (request == NULL || out == NULL || request->magic != REQUEST_MAGIC ||
        request->carrier == NULL || request->carrier->magic != CARRIER_MAGIC)
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;
    asian_genuine_strip_output_t internal __attribute__((aligned(64)));
    memset(&internal, 0, sizeof(internal));
    asian_genuine_arithmetic_growth_only_q_diag(&request->growth_context);
    asian_genuine_strip_arithmetic_price_1_diag(
        request->q, request->q, &request->strip, request->strip.strikes,
        internal.values);
    memset(out, 0, sizeof(*out));
    out->strike_count = request->strike_count;
    out->selected_path = request->selected_path;
    out->values[0].call_price = internal.values[0].call_price;
    out->values[0].put_price = internal.values[0].put_price;
    out->values[0].call_delta = internal.values[0].call_delta;
    out->values[0].put_delta = internal.values[0].put_delta;
    return ASIAN_ARITHMETIC_OK;
}

__attribute__((noinline, used))
int asian_n64_clean_fresh_total(const clean_engine_t *clean,
                                const asian_arithmetic_request_t *request,
                                clean_carrier_t *carrier,
                                clean_request_t *prepared,
                                asian_arithmetic_result_t *out)
{
    const carrier_input_t input = carrier_from_request(request);
    int status = asian_n64_clean_carrier_prepare(clean, &input, carrier);
    if (status == ASIAN_ARITHMETIC_OK)
        status = asian_n64_clean_request_prepare(clean, carrier, request, prepared);
    if (status == ASIAN_ARITHMETIC_OK)
        status = asian_n64_clean_prepared_price(prepared, out);
    return status;
}

__attribute__((noinline, used))
int asian_n64_clean_reuse_total(const clean_engine_t *clean,
                                const clean_carrier_t *carrier,
                                const asian_arithmetic_request_t *request,
                                clean_request_t *prepared,
                                asian_arithmetic_result_t *out)
{
    int status = asian_n64_clean_request_prepare(clean, carrier, request, prepared);
    if (status == ASIAN_ARITHMETIC_OK)
        status = asian_n64_clean_prepared_price(prepared, out);
    return status;
}

static uint64_t wall_now(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value) != 0) abort();
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

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

static double median101(double values[SAMPLES])
{
    qsort(values, SAMPLES, sizeof(values[0]), compare_double);
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
    if (strcmp(vendor, "GenuineIntel") != 0) return 0;
    __cpuid(1, a, b, c, d);
    const unsigned family = ((a >> 8) & 15u) + ((a >> 20) & 255u);
    const unsigned model = ((a >> 4) & 15u) | ((a >> 12) & 240u);
    return family == 6u && model == 143u;
}

static int vector_supported(void)
{
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx512f") &&
           __builtin_cpu_supports("avx512bw") &&
           __builtin_cpu_supports("fma");
}

static int initialize_engine(void)
{
    static const char signed_z_sha[] =
        "ecf3bb854e98bedcf724d0743438457ccf8b600e1264cb537741ce0b9d90d98d";
    static const char joe_kuo_sha[] =
        "6b121e226aab38e81e1b941b42d4c4409beec7b769cce060d13801a6849fe31a";
    char digest[65];
    if (!vector_supported() ||
        asian_genuine_arithmetic_growth_only_memory_sha256(
            asian_genuine_fixed_block_signed_z,
            ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES, digest) != 0 ||
        strcmp(digest, signed_z_sha) != 0 ||
        asian_genuine_arithmetic_growth_only_memory_sha256(
            asian_arithmetic_joe_kuo_256_records,
            JOE_KUO_256_BYTES, digest) != 0 ||
        strcmp(digest, joe_kuo_sha) != 0 ||
        asian_arithmetic_route_plan_create(&production_plan) !=
          ASIAN_ARITHMETIC_OK)
        return -1;
    workspace = aligned_zero(sizeof(*workspace));
    if (workspace == NULL) return -1;
    uint32_t lo_bits = UINT32_C(0xc075e22f);
    uint32_t hi_bits = UINT32_C(0x4075e233);
    memcpy(&engine.signed_z_min, &lo_bits, sizeof(lo_bits));
    memcpy(&engine.signed_z_max, &hi_bits, sizeof(hi_bits));
    engine.plan = production_plan;
    engine.magic = ENGINE_MAGIC;
    return 0;
}

static void release_engine(void)
{
    for (uint32_t i = 0; i < 2u; ++i) {
        asian_arithmetic_prepared_destroy(reuse_current_prepared[i]);
        asian_arithmetic_prepared_destroy(current_prepared[i]);
    }
    free(workspace);
    workspace = NULL;
    asian_arithmetic_route_plan_destroy(production_plan);
    production_plan = NULL;
    memset(&engine, 0, sizeof(engine));
}

static int prepare_persistent_cells(void)
{
    const carrier_input_t ca = carrier_from_request(&request_a);
    const carrier_input_t cb = carrier_from_request(&request_b);
    if (asian_n64_clean_carrier_prepare(&engine, &ca, &workspace->carrier_a) != 0 ||
        asian_n64_clean_carrier_prepare(&engine, &cb, &workspace->carrier_b) != 0 ||
        asian_n64_clean_request_prepare(&engine, &workspace->carrier_a,
          &request_a, &workspace->request_a) != 0 ||
        asian_n64_clean_request_prepare(&engine, &workspace->carrier_b,
          &request_b, &workspace->request_b) != 0 ||
        asian_n64_clean_request_prepare(&engine, &workspace->carrier_a,
          &request_a, &workspace->reuse_request_a) != 0 ||
        asian_n64_clean_request_prepare(&engine, &workspace->carrier_a,
          &reuse_request_b, &workspace->reuse_request_b) != 0)
        return -1;
    const asian_arithmetic_request_t *fresh[2] = {&request_a, &request_b};
    const asian_arithmetic_request_t *reuse[2] = {&request_a, &reuse_request_b};
    for (uint32_t i = 0; i < 2u; ++i)
        if (asian_arithmetic_prepare(production_plan, fresh[i],
              &current_prepared[i]) != ASIAN_ARITHMETIC_OK ||
            asian_arithmetic_prepare(production_plan, reuse[i],
              &reuse_current_prepared[i]) != ASIAN_ARITHMETIC_OK)
            return -1;
    return 0;
}

static int compare_result(const char *name, uint32_t variant,
                          const asian_arithmetic_result_t *expected,
                          const asian_arithmetic_result_t *actual)
{
    if (memcmp(expected, actual, sizeof(*expected)) == 0) return 0;
    fprintf(stderr,
            "%s variant=%u expected_call=%a actual_call=%a "
            "expected_put=%a actual_put=%a expected_count=%u actual_count=%u "
            "expected_path=%u actual_path=%u\n",
            name, variant, expected->values[0].call_price,
            actual->values[0].call_price, expected->values[0].put_price,
            actual->values[0].put_price, expected->strike_count,
            actual->strike_count, expected->selected_path,
            actual->selected_path);
    return -1;
}

static int bounded_correctness(void)
{
    const asian_arithmetic_request_t *all[4] = {
        &request_a, &request_b, &request_a, &reuse_request_b};
    clean_carrier_t *carrier = &workspace->fresh_carrier;
    clean_request_t *prepared = &workspace->fresh_request;
    for (uint32_t i = 0; i < 4u; ++i) {
        asian_arithmetic_prepared_t *current = NULL;
        asian_arithmetic_result_t expected, clean, repeat;
        if (asian_arithmetic_prepare(production_plan, all[i], &current) != 0 ||
            asian_arithmetic_price_prepared(current, &expected) != 0 ||
            asian_n64_clean_fresh_total(&engine, all[i], carrier, prepared,
              &clean) != 0 ||
            asian_n64_clean_fresh_total(&engine, all[i], carrier, prepared,
              &repeat) != 0 ||
            compare_result("current-clean", i, &expected, &clean) != 0 ||
            compare_result("clean-repeat", i, &clean, &repeat) != 0) {
            asian_arithmetic_prepared_destroy(current);
            return -1;
        }
        asian_arithmetic_prepared_destroy(current);
    }
    for (uint32_t i = 0; i < 2u; ++i) {
        asian_arithmetic_result_t expected, clean, repeat;
        const asian_arithmetic_request_t *r = reuse_request(i);
        if (asian_arithmetic_price_prepared(reuse_current_prepared[i],
              &expected) != 0 ||
            asian_n64_clean_reuse_total(&engine, &workspace->carrier_a, r,
              &workspace->fresh_request, &clean) != 0 ||
            asian_n64_clean_reuse_total(&engine, &workspace->carrier_a, r,
              &workspace->fresh_request, &repeat) != 0 ||
            compare_result("reuse-current-clean", i, &expected, &clean) != 0 ||
            compare_result("reuse-clean-repeat", i, &clean, &repeat) != 0)
            return -1;
    }
    return 0;
}

__attribute__((noinline)) static int run_bracket(void)
{
    __asm__ volatile("" ::: "memory");
    return 0;
}

__attribute__((noinline)) static int run_route_plan_create(void)
{
    temporary_plan = NULL;
    return asian_arithmetic_route_plan_create(&temporary_plan);
}

__attribute__((noinline)) static int run_current_prepare(void)
{
    temporary_prepared = NULL;
    return asian_arithmetic_prepare(production_plan, fresh_request(active_input),
                                    &temporary_prepared);
}

__attribute__((noinline)) static int run_current_price(void)
{
    return asian_arithmetic_price_prepared(current_prepared[active_input],
                                           &timed_result);
}

__attribute__((noinline)) static int run_current_prepare_first(void)
{
    temporary_prepared = NULL;
    int status = asian_arithmetic_prepare(production_plan,
        fresh_request(active_input), &temporary_prepared);
    if (status == 0)
        status = asian_arithmetic_price_prepared(temporary_prepared,
                                                  &timed_result);
    return status;
}

__attribute__((noinline)) static int run_clean_plan_lookup(void)
{
    return asian_n64_clean_plan_lookup(&engine, &temporary_binding);
}

__attribute__((noinline)) static int run_clean_carrier_prepare(void)
{
    const carrier_input_t input = carrier_from_request(fresh_request(active_input));
    return asian_n64_clean_carrier_prepare(&engine, &input,
                                            &workspace->component_carrier);
}

__attribute__((noinline)) static int run_clean_request_prepare(void)
{
    const clean_carrier_t *carrier = active_input == 0u ?
        &workspace->carrier_a : &workspace->carrier_b;
    return asian_n64_clean_request_prepare(&engine, carrier,
        fresh_request(active_input), &workspace->component_request);
}

__attribute__((noinline)) static int run_clean_prepared_price(void)
{
    clean_request_t *request = active_input == 0u ?
        &workspace->request_a : &workspace->request_b;
    return asian_n64_clean_prepared_price(request, &timed_result);
}

__attribute__((noinline)) static int run_clean_fresh(void)
{
    return asian_n64_clean_fresh_total(&engine, fresh_request(active_input),
        &workspace->fresh_carrier, &workspace->fresh_request, &timed_result);
}

__attribute__((noinline)) static int run_reuse_request_prepare(void)
{
    return asian_n64_clean_request_prepare(&engine, &workspace->carrier_a,
        reuse_request(active_input), &workspace->component_request);
}

__attribute__((noinline)) static int run_reuse_prepared_price(void)
{
    clean_request_t *request = active_input == 0u ?
        &workspace->reuse_request_a : &workspace->reuse_request_b;
    return asian_n64_clean_prepared_price(request, &timed_result);
}

__attribute__((noinline)) static int run_reuse_total(void)
{
    return asian_n64_clean_reuse_total(&engine, &workspace->carrier_a,
        reuse_request(active_input), &workspace->fresh_request, &timed_result);
}

typedef int (*runner_t)(void);

static const runner_t runners[] = {
    run_bracket, run_route_plan_create, run_current_prepare,
    run_current_price, run_current_prepare_first, run_clean_plan_lookup,
    run_clean_carrier_prepare, run_clean_request_prepare,
    run_clean_prepared_price, run_clean_fresh, run_reuse_request_prepare,
    run_reuse_prepared_price, run_reuse_total,
};

static void finish_observation(enum operation operation)
{
    uint64_t value = UINT64_C(1469598103934665603);
    if (operation == OP_ROUTE_PLAN_CREATE) {
        value ^= (uintptr_t)temporary_plan;
        asian_arithmetic_route_plan_destroy(temporary_plan);
        temporary_plan = NULL;
    } else if (operation == OP_CURRENT_PREPARE ||
               operation == OP_CURRENT_PREPARE_FIRST) {
        value ^= (uintptr_t)temporary_prepared;
        if (operation == OP_CURRENT_PREPARE_FIRST)
            value = hash_bytes(value, &timed_result, sizeof(timed_result));
        asian_arithmetic_prepared_destroy(temporary_prepared);
        temporary_prepared = NULL;
    } else if (operation == OP_CLEAN_CARRIER_PREPARE) {
        value = hash_bytes(value, &workspace->component_carrier.growth[17],
                           sizeof(float));
    } else if (operation == OP_CLEAN_REQUEST_PREPARE ||
               operation == OP_REUSE_REQUEST_PREPARE) {
        value = hash_bytes(value, &workspace->component_request.strip,
                           sizeof(workspace->component_request.strip));
    } else if (operation == OP_CLEAN_PLAN_LOOKUP) {
        value ^= (uintptr_t)temporary_binding.maps;
    } else if (operation != OP_BRACKET) {
        value = hash_bytes(value, &timed_result, sizeof(timed_result));
    }
    sink_value += value;
}

static timing_t observe(enum operation operation, uint32_t input,
                        asian_arithmetic_result_t *snapshot)
{
    active_input = input;
    const int cpu_before = sched_getcpu();
    const uint64_t wall0 = wall_now();
    const uint64_t tsc0 = tsc_begin();
    const int status = runners[operation]();
    const uint64_t tsc1 = tsc_end();
    const uint64_t wall1 = wall_now();
    const int cpu_after = sched_getcpu();
    if (status != 0 || cpu_before < 0 || cpu_before != cpu_after) abort();
    if (snapshot != NULL) *snapshot = timed_result;
    finish_observation(operation);
    return (timing_t){wall1 - wall0, tsc1 - tsc0};
}

static timing_t measure_scalar(enum operation operation)
{
    double wall[SAMPLES], tsc[SAMPLES];
    for (uint32_t sample = 0; sample < WARMUPS + SAMPLES; ++sample) {
        const timing_t value = observe(operation, sample & 1u, NULL);
        if (sample >= WARMUPS) {
            const uint32_t index = sample - WARMUPS;
            wall[index] = (double)value.wall;
            tsc[index] = (double)value.tsc;
        }
    }
    return (timing_t){(uint64_t)median101(wall), (uint64_t)median101(tsc)};
}

static int measure_fresh_pair(timing_t *current, timing_t *clean)
{
    double current_wall[SAMPLES], current_tsc[SAMPLES];
    double clean_wall[SAMPLES], clean_tsc[SAMPLES];
    for (uint32_t quartet = 0; quartet < WARMUPS + SAMPLES; ++quartet) {
        static const enum operation abba[4] = {
            OP_CURRENT_PREPARE_FIRST, OP_CLEAN_FRESH,
            OP_CLEAN_FRESH, OP_CURRENT_PREPARE_FIRST};
        static const enum operation baab[4] = {
            OP_CLEAN_FRESH, OP_CURRENT_PREPARE_FIRST,
            OP_CURRENT_PREPARE_FIRST, OP_CLEAN_FRESH};
        const enum operation *order = (quartet & 1u) != 0u ? baab : abba;
        const uint32_t input = quartet & 1u;
        uint64_t wall_sum[2] = {0, 0};
        uint64_t tsc_sum[2] = {0, 0};
        asian_arithmetic_result_t expected;
        int have_expected = 0;
        for (uint32_t observation = 0; observation < 4u; ++observation) {
            asian_arithmetic_result_t result;
            const enum operation operation = order[observation];
            const timing_t value = observe(operation, input, &result);
            if (!have_expected) {
                expected = result;
                have_expected = 1;
            } else if (memcmp(&expected, &result, sizeof(result)) != 0) {
                fprintf(stderr, "paired output mismatch quartet=%u input=%u\n",
                        quartet, input);
                return -1;
            }
            const uint32_t slot = operation == OP_CURRENT_PREPARE_FIRST ? 0u : 1u;
            wall_sum[slot] += value.wall;
            tsc_sum[slot] += value.tsc;
        }
        if (quartet >= WARMUPS) {
            const uint32_t sample = quartet - WARMUPS;
            current_wall[sample] = (double)wall_sum[0] * 0.5;
            current_tsc[sample] = (double)tsc_sum[0] * 0.5;
            clean_wall[sample] = (double)wall_sum[1] * 0.5;
            clean_tsc[sample] = (double)tsc_sum[1] * 0.5;
        }
    }
    current->wall = (uint64_t)median101(current_wall);
    current->tsc = (uint64_t)median101(current_tsc);
    clean->wall = (uint64_t)median101(clean_wall);
    clean->tsc = (uint64_t)median101(clean_tsc);
    return 0;
}

static void print_metric(const char *name, timing_t timing)
{
    printf("%-34s %12" PRIu64 " %12" PRIu64 "\n",
           name, timing.wall, timing.tsc);
}

static const char *classification(uint64_t wall)
{
    if (wall < 25000u) return "VERY_INTERESTING";
    if (wall < 40000u) return "COMMERCIALLY_CREDIBLE";
    if (wall < 60000u) return "WEAK";
    return "SERIOUS_FAILURE";
}

static int run_timing(void)
{
    timing_t current_first, clean_fresh;
    const timing_t bracket = measure_scalar(OP_BRACKET);
    const timing_t route_plan = measure_scalar(OP_ROUTE_PLAN_CREATE);
    const timing_t current_prepare = measure_scalar(OP_CURRENT_PREPARE);
    const timing_t current_price = measure_scalar(OP_CURRENT_PRICE);
    const timing_t plan_lookup = measure_scalar(OP_CLEAN_PLAN_LOOKUP);
    const timing_t carrier_prepare = measure_scalar(OP_CLEAN_CARRIER_PREPARE);
    const timing_t request_prepare = measure_scalar(OP_CLEAN_REQUEST_PREPARE);
    const timing_t prepared_price = measure_scalar(OP_CLEAN_PREPARED_PRICE);
    const timing_t reuse_request_prepare =
        measure_scalar(OP_REUSE_REQUEST_PREPARE);
    const timing_t reuse_prepared_price =
        measure_scalar(OP_REUSE_PREPARED_PRICE);
    const timing_t reuse_total = measure_scalar(OP_REUSE_TOTAL);
    if (measure_fresh_pair(&current_first, &clean_fresh) != 0) return -1;

    puts("metric                              wall_ns     tsc_ticks");
    print_metric("timing_bracket", bracket);
    print_metric("route_plan_create", route_plan);
    print_metric("current_api_prepare", current_prepare);
    print_metric("current_api_price_prepared", current_price);
    print_metric("current_api_prepare_plus_first", current_first);
    print_metric("clean_plan_lookup", plan_lookup);
    print_metric("clean_carrier_prepare", carrier_prepare);
    print_metric("clean_request_prepare", request_prepare);
    print_metric("clean_prepared_price", prepared_price);
    print_metric("clean_fresh_total", clean_fresh);
    print_metric("clean_reuse_request_prepare", reuse_request_prepare);
    print_metric("clean_reuse_prepared_price", reuse_prepared_price);
    print_metric("clean_reuse_total", reuse_total);
    printf("%-34s %s\n", "fresh_decision", classification(clean_fresh.wall));
    return 0;
}

int main(int argc, char **argv)
{
    int timing = 0;
    int native = 0;
    int cpu = -1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--check") == 0) {
            timing = 0;
        } else if (strcmp(argv[i], "--timing") == 0) {
            timing = 1;
            native = 1;
        } else if (strcmp(argv[i], "--native-check") == 0) {
            timing = 0;
            native = 1;
        } else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
            cpu = atoi(argv[++i]);
        } else {
            fprintf(stderr,
                    "usage: %s --check | --native-check --cpu CPU | "
                    "--timing --cpu CPU\n", argv[0]);
            return 2;
        }
    }
    if (native && (cpu != 0 || pin_cpu(cpu) != 0 || !is_sapphire_rapids())) {
        fprintf(stderr, "Sapphire Rapids CPU 0 is required\n");
        return 2;
    }
    if (initialize_engine() != 0 || prepare_persistent_cells() != 0) {
        fprintf(stderr, "engine initialization failed\n");
        release_engine();
        return 2;
    }
    if (!timing) {
        if (bounded_correctness() != 0) {
            fprintf(stderr, "bounded native correctness failed\n");
            release_engine();
            return 1;
        }
        puts("bounded_native_correctness PASS");
        release_engine();
        return 0;
    }
    const int status = run_timing();
    sink_value += hash_bytes(UINT64_C(1469598103934665603),
                             &timed_result, sizeof(timed_result));
    release_engine();
    return status != 0 || sink_value == UINT64_MAX ? 1 : 0;
}
