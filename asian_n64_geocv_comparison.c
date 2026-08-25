#include "private/asian_n64_geocv_comparison_diag.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { GENERIC_PLAN_MAGIC = 0x54525351u };

/* Exact private layout produced by the frozen generic qsort builder. */
struct asian_meta_qsort_control_plan {
    uint32_t magic;
    uint8_t donor_region[ASIAN_META_DIRECTIONS];
    uint8_t reserved[60];
    fragment_map_t maps[ASIAN_META_DIRECTIONS];
};

_Static_assert(offsetof(struct asian_meta_qsort_control_plan, maps) == 320,
               "generic plan map offset");

static int same_double(double a, double b)
{
    uint64_t ab, bb;
    memcpy(&ab, &a, sizeof(ab));
    memcpy(&bb, &b, sizeof(bb));
    return ab == bb;
}

static int valid_market(const asian_geocv_affine_carrier_input_t *input)
{
    return input != NULL && isfinite(input->rate) &&
        isfinite(input->dividend_yield) && isfinite(input->sigma) &&
        input->sigma > 0.0 && isfinite(input->maturity) &&
        input->maturity > 0.0;
}

static int valid_request(const asian_geocv_affine_request_input_t *input)
{
    return input != NULL && isfinite(input->s0) && input->s0 > 0.0 &&
        isfinite(input->strike) && input->strike > 0.0;
}

int asian_n64_comparison_engine_create(asian_n64_comparison_engine_t *engine)
{
    if (engine == NULL || ((uintptr_t)engine & 63u) != 0u)
        return -1;
    memset(engine, 0, sizeof(*engine));
    if (asian_geocv_affine_engine_create(&engine->affine) != 0 ||
        asian_meta_qsort_control_plan_create(&engine->generic_plan) != 0) {
        asian_geocv_affine_engine_destroy(&engine->affine);
        return -1;
    }
    engine->magic = ASIAN_N64_COMPARISON_ENGINE_MAGIC;
    return 0;
}

void asian_n64_comparison_engine_destroy(asian_n64_comparison_engine_t *engine)
{
    if (engine != NULL) {
        asian_meta_qsort_control_plan_destroy(engine->generic_plan);
        asian_geocv_affine_engine_destroy(&engine->affine);
        memset(engine, 0, sizeof(*engine));
    }
}

__attribute__((noinline, used))
int asian_n64_arithmetic_carrier_prepare(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_carrier_input_t *input,
    asian_n64_arithmetic_carrier_t *carrier)
{
    if (engine == NULL || engine->magic != ASIAN_N64_COMPARISON_ENGINE_MAGIC ||
        carrier == NULL || ((uintptr_t)carrier & 63u) != 0u ||
        !valid_market(input))
        return -1;
    const double dt = input->maturity / (double)ASIAN_N64_COMPARISON_FIXINGS;
    const float drift = (float)((input->rate - input->dividend_yield -
                                 0.5 * input->sigma * input->sigma) * dt);
    const float diffusion = (float)(input->sigma * sqrt(dt));
    if (!isfinite(drift) || !isfinite(diffusion) || diffusion <= 0.0f ||
        drift < -0.25f || drift > 0.25f || diffusion > 0.20f)
        return -1;
    const float x0 = fmaf(diffusion, engine->affine.signed_z_min, drift);
    const float x1 = fmaf(diffusion, engine->affine.signed_z_max, drift);
    if (!isfinite(x0) || !isfinite(x1) || x0 < -87.0f || x1 > 88.0f)
        return -1;

    memset(&carrier->fused, 0, sizeof(carrier->fused));
    carrier->fused.signed_z = asian_genuine_fixed_block_signed_z;
    carrier->fused.growth_out = carrier->growth;
    carrier->fused.drift = drift;
    carrier->fused.diffusion = diffusion;
    carrier->fused.fixing_count = ASIAN_N64_COMPARISON_FIXINGS;
    carrier->fused.path_count = ASIAN_N64_COMPARISON_PATHS;
    carrier->fused.region_count = 2u;
    carrier->fused.values_per_region = ASIAN_N64_COMPARISON_PATHS;
    carrier->fused.first_index = ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    carrier->fused.total_values = ASIAN_N64_COMPARISON_DONOR_VALUES;
    carrier->fused.magic = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC;
    carrier->fused.abi_version =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION;
    carrier->rate = input->rate;
    carrier->dividend_yield = input->dividend_yield;
    carrier->sigma = input->sigma;
    carrier->maturity = input->maturity;
    carrier->magic = ASIAN_N64_COMPARISON_ARITH_CARRIER_MAGIC;
    asian_genuine_arithmetic_fused_source_exp_diag(&carrier->fused);
    return 0;
}

static int request_matches_arithmetic_carrier(
    const asian_n64_arithmetic_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input)
{
    return valid_request(input) && carrier != NULL &&
        carrier->magic == ASIAN_N64_COMPARISON_ARITH_CARRIER_MAGIC &&
        same_double(input->rate, carrier->rate) &&
        same_double(input->dividend_yield, carrier->dividend_yield) &&
        same_double(input->sigma, carrier->sigma) &&
        same_double(input->maturity, carrier->maturity);
}

__attribute__((noinline, used))
int asian_n64_arithmetic_request_prepare(
    const asian_n64_comparison_engine_t *engine,
    const asian_n64_arithmetic_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    asian_n64_arithmetic_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_N64_COMPARISON_ENGINE_MAGIC ||
        request == NULL || ((uintptr_t)request & 63u) != 0u ||
        !request_matches_arithmetic_carrier(carrier, input) ||
        asian_meta_affine_routes_bind(engine->affine.plan, NULL,
            carrier->growth, ASIAN_N64_COMPARISON_FIXINGS,
            request->routes) != 0)
        return -1;
    const float strike = (float)input->strike;
    uint32_t padded = 0u;
    if (asian_genuine_arithmetic_growth_only_strip_prepare_padded(
            &request->strip, input->s0, input->rate,
            input->dividend_yield, input->sigma, input->maturity,
            ASIAN_N64_COMPARISON_FIXINGS, 0u, 0.0, 0.0, &strike, 1u,
            &padded) != 0 || padded != 1u)
        return -1;
    memset(&request->growth, 0, sizeof(request->growth));
    request->growth.d1_growth = carrier->growth;
    request->growth.routes_d2 = request->routes + 1;
    request->growth.fixing_count = ASIAN_N64_COMPARISON_FIXINGS;
    request->growth.s0 = (float)input->s0;
    request->growth.magic = ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC;
    request->growth.abi_version =
        ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION;
    request->carrier = carrier;
    request->magic = ASIAN_N64_COMPARISON_ARITH_REQUEST_MAGIC;
    return 0;
}

__attribute__((noinline, used))
int asian_n64_arithmetic_prepared_price(
    const asian_n64_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output)
{
    asian_meta_arithmetic_growth_only_price_1_diag(
        &request->growth, &request->strip, request->strip.strikes,
        output->values);
    return 0;
}

__attribute__((noinline, used))
int asian_n64_arithmetic_fresh_total(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_request_input_t *input,
    asian_n64_arithmetic_carrier_t *carrier,
    asian_n64_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output)
{
    const asian_geocv_affine_carrier_input_t market = {
        input->rate, input->dividend_yield, input->sigma, input->maturity};
    int status = asian_n64_arithmetic_carrier_prepare(engine, &market, carrier);
    if (status == 0)
        status = asian_n64_arithmetic_request_prepare(
            engine, carrier, input, request);
    if (status == 0)
        status = asian_n64_arithmetic_prepared_price(request, output);
    return status;
}

__attribute__((noinline, used))
int asian_n64_arithmetic_reuse_total(
    const asian_n64_comparison_engine_t *engine,
    const asian_n64_arithmetic_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    asian_n64_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output)
{
    int status = asian_n64_arithmetic_request_prepare(
        engine, carrier, input, request);
    if (status == 0)
        status = asian_n64_arithmetic_prepared_price(request, output);
    return status;
}

static int generic_routes_bind(
    const asian_meta_qsort_control_plan_t *opaque,
    const float *x, const float *growth, asian_genuine_route_t routes[64])
{
    const struct asian_meta_qsort_control_plan *plan =
        (const struct asian_meta_qsort_control_plan *)opaque;
    if (plan == NULL || plan->magic != GENERIC_PLAN_MAGIC)
        return -1;
    for (uint32_t fixing = 0; fixing < ASIAN_N64_COMPARISON_FIXINGS;
         ++fixing) {
        const size_t offset = (size_t)plan->donor_region[fixing] *
                              ASIAN_N64_COMPARISON_PATHS;
        routes[fixing].x_base = x + offset;
        routes[fixing].growth_base = growth + offset;
        routes[fixing].map = &plan->maps[fixing];
        const float weight =
            (float)(ASIAN_N64_COMPARISON_FIXINGS - fixing) /
            (float)ASIAN_N64_COMPARISON_FIXINGS;
        memcpy(&routes[fixing].weight_bits, &weight, sizeof(weight));
        routes[fixing].fixing_index = fixing;
    }
    return 0;
}

static int request_matches_geocv_carrier(
    const asian_geocv_affine_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input)
{
    return valid_request(input) && carrier != NULL &&
        carrier->magic == ASIAN_GEOCV_AFFINE_CARRIER_MAGIC &&
        same_double(input->rate, carrier->rate) &&
        same_double(input->dividend_yield, carrier->dividend_yield) &&
        same_double(input->sigma, carrier->sigma) &&
        same_double(input->maturity, carrier->maturity);
}

__attribute__((noinline, used))
int asian_n64_geocv_request_prepare(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    enum asian_n64_comparison_candidate candidate,
    asian_n64_geocv_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_N64_COMPARISON_ENGINE_MAGIC ||
        request == NULL || ((uintptr_t)request & 63u) != 0u ||
        !request_matches_geocv_carrier(carrier, input) ||
        (candidate != ASIAN_N64_GEOCV_GENERIC_IMMEDIATE &&
         candidate != ASIAN_N64_GEOCV_AFFINE_IMMEDIATE))
        return -1;
    if (candidate == ASIAN_N64_GEOCV_GENERIC_IMMEDIATE) {
        if (generic_routes_bind(engine->generic_plan, carrier->x,
                carrier->growth, request->routes.generic) != 0)
            return -1;
    } else if (asian_meta_affine_routes_bind(engine->affine.plan, carrier->x,
                   carrier->growth, ASIAN_N64_COMPARISON_FIXINGS,
                   request->routes.affine) != 0) {
        return -1;
    }

    const float strike = (float)input->strike;
    uint32_t padded = 0u;
    if (asian_geometric_cv_packet_local_strip_prepare_padded(
            &request->strip, input->s0, input->rate,
            input->dividend_yield, input->sigma, input->maturity,
            ASIAN_N64_COMPARISON_FIXINGS, 0u, 0.0, 0.0, &strike, 1u,
            &padded) != 0 || padded != 1u)
        return -1;
    uint32_t log_base_bits;
    memcpy(&log_base_bits, &request->strip.log_base, sizeof(log_base_bits));
    if (candidate == ASIAN_N64_GEOCV_GENERIC_IMMEDIATE) {
        asian_n64_generic_immediate_context_t *context =
            &request->immediate.generic;
        memset(context, 0, sizeof(*context));
        context->d1_x = request->routes.generic[0].x_base;
        context->d1_growth = request->routes.generic[0].growth_base;
        context->routes_d2 = request->routes.generic + 1;
        context->fixing_count = ASIAN_N64_COMPARISON_FIXINGS;
        context->s0 = (float)input->s0;
        context->d1_weight_bits = request->routes.generic[0].weight_bits;
        context->terminal_log_base_bits = log_base_bits;
        context->magic = ASIAN_N64_GENERIC_IMMEDIATE_MAGIC;
        context->abi_version = 1u;
    } else {
        asian_geometric_cv_immediate_context_t *context =
            &request->immediate.affine;
        memset(context, 0, sizeof(*context));
        context->d1_x = request->routes.affine[0].x_base;
        context->d1_growth = request->routes.affine[0].growth_base;
        context->routes_d2 = request->routes.affine + 1;
        context->fixing_count = ASIAN_N64_COMPARISON_FIXINGS;
        context->s0 = (float)input->s0;
        context->d1_weight_bits = request->routes.affine[0].weight_bits;
        context->terminal_log_base_bits = log_base_bits;
        context->magic = ASIAN_GEOMETRIC_CV_IMMEDIATE_MAGIC;
        context->abi_version = ASIAN_GEOMETRIC_CV_IMMEDIATE_ABI_VERSION;
    }
    request->carrier = carrier;
    request->candidate = (uint32_t)candidate;
    request->magic = ASIAN_N64_COMPARISON_GEOCV_REQUEST_MAGIC;
    return 0;
}

__attribute__((noinline, used))
void asian_n64_generic_geocv_immediate_invoke_price_1(
    const asian_n64_generic_immediate_context_t *context,
    const asian_genuine_strip_context_t *strip,
    const asian_genuine_strip_strike_t *strikes,
    asian_genuine_strip_output_t *output)
{
    __asm__ volatile(
        "call asian_n64_generic_geocv_immediate_price_1_diag"
        : "+D"(context), "+S"(strip), "+d"(strikes), "+c"(output)
        :
        : "rax", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
          "k1", "k2",
          "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6",
          "zmm7", "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13",
          "zmm14", "zmm15", "zmm16", "zmm17", "zmm18", "zmm19",
          "zmm20", "zmm21", "zmm22", "zmm23", "zmm24", "zmm25",
          "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31",
          "cc", "memory");
}

__attribute__((noinline, used))
int asian_n64_geocv_prepared_price(
    const asian_n64_geocv_request_t *request,
    asian_genuine_strip_output_t *output)
{
    if (request->candidate == ASIAN_N64_GEOCV_GENERIC_IMMEDIATE) {
        asian_n64_generic_geocv_immediate_invoke_price_1(
            &request->immediate.generic, &request->strip,
            request->strip.strikes, output);
    } else {
        asian_geometric_cv_immediate_invoke_price_1(
            &request->immediate.affine, &request->strip,
            request->strip.strikes, output);
    }
    return 0;
}

__attribute__((noinline, used))
int asian_n64_geocv_fresh_total(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_request_input_t *input,
    enum asian_n64_comparison_candidate candidate,
    asian_geocv_affine_carrier_t *carrier,
    asian_n64_geocv_request_t *request,
    asian_genuine_strip_output_t *output)
{
    const asian_geocv_affine_carrier_input_t market = {
        input->rate, input->dividend_yield, input->sigma, input->maturity};
    int status = asian_geocv_affine_carrier_prepare(
        &engine->affine, &market, carrier);
    if (status == 0)
        status = asian_n64_geocv_request_prepare(
            engine, carrier, input, candidate, request);
    if (status == 0)
        status = asian_n64_geocv_prepared_price(request, output);
    return status;
}

__attribute__((noinline, used))
int asian_n64_geocv_reuse_total(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    enum asian_n64_comparison_candidate candidate,
    asian_n64_geocv_request_t *request,
    asian_genuine_strip_output_t *output)
{
    int status = asian_n64_geocv_request_prepare(
        engine, carrier, input, candidate, request);
    if (status == 0)
        status = asian_n64_geocv_prepared_price(request, output);
    return status;
}
