#define _POSIX_C_SOURCE 200112L

#include "private/asian_affine_discrete_barrier_lifecycle_diag.h"

#include "private/asian_genuine_fixed_block_source_diag.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct __attribute__((aligned(64))) asian_affine_barrier_workspace {
    asian_affine_barrier_carrier_t carrier;
    asian_affine_barrier_request_t request;
};

static int same_double(double a, double b)
{
    uint64_t aa, bb;
    memcpy(&aa, &a, sizeof(aa));
    memcpy(&bb, &b, sizeof(bb));
    return aa == bb;
}

static int valid_market(const asian_affine_barrier_market_input_t *input)
{
    return input != NULL && isfinite(input->rate) &&
        isfinite(input->dividend_yield) && isfinite(input->sigma) &&
        input->sigma > 0.0 && isfinite(input->maturity) &&
        input->maturity > 0.0 &&
        input->monitoring_count >= ASIAN_AFFINE_BARRIER_MIN_MONITORING &&
        input->monitoring_count <= ASIAN_AFFINE_BARRIER_MAX_MONITORING;
}

int asian_affine_barrier_engine_create(asian_affine_barrier_engine_t *engine)
{
    if (engine == NULL || ((uintptr_t)engine & 63u) != 0u)
        return ASIAN_AFFINE_BARRIER_INVALID;
    memset(engine, 0, sizeof(*engine));
    if (asian_meta_affine_plan_create(&engine->affine_plan) != 0)
        return ASIAN_AFFINE_BARRIER_INVALID;
    if (posix_memalign((void **)&engine->workspace, 64u,
                       sizeof(*engine->workspace)) != 0) {
        asian_meta_affine_plan_destroy(engine->affine_plan);
        engine->affine_plan = NULL;
        return ASIAN_AFFINE_BARRIER_INVALID;
    }
    memset(engine->workspace, 0, sizeof(*engine->workspace));
    const uint32_t lo_bits = UINT32_C(0xc075e22f);
    const uint32_t hi_bits = UINT32_C(0x4075e233);
    memcpy(&engine->signed_z_min, &lo_bits, sizeof(lo_bits));
    memcpy(&engine->signed_z_max, &hi_bits, sizeof(hi_bits));
    engine->magic = ASIAN_AFFINE_BARRIER_ENGINE_MAGIC;
    return ASIAN_AFFINE_BARRIER_OK;
}

void asian_affine_barrier_engine_destroy(asian_affine_barrier_engine_t *engine)
{
    if (engine != NULL) {
        free(engine->workspace);
        asian_meta_affine_plan_destroy(engine->affine_plan);
        memset(engine, 0, sizeof(*engine));
    }
}

__attribute__((noinline, used))
int asian_affine_barrier_market_prepare(
    const asian_affine_barrier_engine_t *engine,
    const asian_affine_barrier_market_input_t *input,
    asian_affine_barrier_carrier_t *carrier)
{
    if (engine == NULL || engine->magic != ASIAN_AFFINE_BARRIER_ENGINE_MAGIC ||
        engine->affine_plan == NULL || carrier == NULL ||
        ((uintptr_t)carrier & 63u) != 0u || !valid_market(input))
        return ASIAN_AFFINE_BARRIER_INVALID;

    const double dt = input->maturity / (double)input->monitoring_count;
    const float drift = (float)((input->rate - input->dividend_yield -
                                 0.5 * input->sigma * input->sigma) * dt);
    const float diffusion = (float)(input->sigma * sqrt(dt));
    if (!isfinite(drift) || !isfinite(diffusion) || diffusion <= 0.0f)
        return ASIAN_AFFINE_BARRIER_UNSUPPORTED;
    const float lo = fmaf(diffusion, engine->signed_z_min, drift);
    const float hi = fmaf(diffusion, engine->signed_z_max, drift);
    if (!isfinite(lo) || !isfinite(hi) || lo < -87.0f || hi > 88.0f)
        return ASIAN_AFFINE_BARRIER_UNSUPPORTED;

    memset(&carrier->fused, 0, sizeof(carrier->fused));
    carrier->fused.signed_z = asian_genuine_fixed_block_signed_z;
    carrier->fused.growth_out = carrier->growth;
    carrier->fused.drift = drift;
    carrier->fused.diffusion = diffusion;
    carrier->fused.fixing_count = input->monitoring_count;
    carrier->fused.path_count = ASIAN_AFFINE_BARRIER_PATHS;
    carrier->fused.region_count = 2u;
    carrier->fused.values_per_region = ASIAN_AFFINE_BARRIER_PATHS;
    carrier->fused.first_index = ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    carrier->fused.total_values = ASIAN_AFFINE_BARRIER_DONOR_VALUES;
    carrier->fused.magic = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC;
    carrier->fused.abi_version =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION;
    carrier->market = *input;
    carrier->magic = ASIAN_AFFINE_BARRIER_CARRIER_MAGIC;
    asian_genuine_arithmetic_fused_source_exp_diag(&carrier->fused);
    return ASIAN_AFFINE_BARRIER_OK;
}

static int valid_request(const asian_affine_barrier_carrier_t *carrier,
                         const asian_affine_barrier_request_input_t *input)
{
    return carrier != NULL && input != NULL &&
        carrier->magic == ASIAN_AFFINE_BARRIER_CARRIER_MAGIC &&
        isfinite(input->s0) && input->s0 > 0.0 &&
        isfinite(input->strike) && input->strike >= 0.0 &&
        isfinite(input->barrier) && input->barrier > 0.0 &&
        input->monitoring_count == carrier->market.monitoring_count &&
        same_double(input->rate, carrier->market.rate) &&
        same_double(input->dividend_yield,
                    carrier->market.dividend_yield) &&
        same_double(input->sigma, carrier->market.sigma) &&
        same_double(input->maturity, carrier->market.maturity) &&
        (input->product == ASIAN_AFFINE_BARRIER_VANILLA ||
         input->product == ASIAN_AFFINE_BARRIER_KNOCK_OUT) &&
        (input->direction == ASIAN_AFFINE_BARRIER_DOWN ||
         input->direction == ASIAN_AFFINE_BARRIER_UP) &&
        (input->side == ASIAN_AFFINE_BARRIER_CALL ||
         input->side == ASIAN_AFFINE_BARRIER_PUT) &&
        (input->initial_state == ASIAN_AFFINE_BARRIER_KNOWN_ALIVE ||
         input->initial_state == ASIAN_AFFINE_BARRIER_ALREADY_KNOCKED_OUT);
}

__attribute__((noinline, used))
int asian_affine_barrier_request_prepare(
    const asian_affine_barrier_engine_t *engine,
    const asian_affine_barrier_carrier_t *carrier,
    const asian_affine_barrier_request_input_t *input,
    asian_affine_barrier_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_AFFINE_BARRIER_ENGINE_MAGIC ||
        engine->affine_plan == NULL || request == NULL ||
        ((uintptr_t)request & 63u) != 0u || !valid_request(carrier, input))
        return ASIAN_AFFINE_BARRIER_INVALID;
    if (asian_meta_affine_routes_bind(engine->affine_plan, NULL,
            carrier->growth, input->monitoring_count, request->routes) != 0)
        return ASIAN_AFFINE_BARRIER_INVALID;

    asian_affine_barrier_context_t *context = &request->context;
    memset(context, 0, sizeof(*context));
    context->routes_d2 = request->routes + 1;
    context->d1_growth = request->routes[0].growth_base;
    context->route_count = input->monitoring_count - 1u;
    context->monitoring_count = input->monitoring_count;
    context->initial_spot = (float)input->s0;
    context->barrier = (float)input->barrier;
    context->strike = (float)input->strike;
    context->initial_alive_mask =
        input->initial_state == ASIAN_AFFINE_BARRIER_KNOWN_ALIVE ?
        UINT16_MAX : 0u;
    context->payoff_scale = exp(-input->rate * input->maturity) /
                            (double)ASIAN_AFFINE_BARRIER_PATHS;
    context->magic = ASIAN_AFFINE_BARRIER_REQUEST_MAGIC;
    request->carrier = carrier;
    request->product = input->product;
    request->direction = input->direction;
    request->side = input->side;
    request->magic = ASIAN_AFFINE_BARRIER_REQUEST_MAGIC;
    return ASIAN_AFFINE_BARRIER_OK;
}

static double invoke_leaf(const asian_affine_barrier_request_t *request)
{
    const asian_affine_barrier_context_t *context = &request->context;
    if (request->product == ASIAN_AFFINE_BARRIER_KNOCK_OUT) {
        if (request->direction == ASIAN_AFFINE_BARRIER_DOWN)
            return request->side == ASIAN_AFFINE_BARRIER_CALL ?
                asian_affine_barrier_down_call_self_interleaved_diag(context) :
                asian_affine_barrier_down_put_self_interleaved_diag(context);
        return request->side == ASIAN_AFFINE_BARRIER_CALL ?
            asian_affine_barrier_up_call_self_grouped_diag(context) :
            asian_affine_barrier_up_put_self_grouped_diag(context);
    }
    if (request->direction == ASIAN_AFFINE_BARRIER_DOWN)
        return request->side == ASIAN_AFFINE_BARRIER_CALL ?
            asian_affine_barrier_vanilla_call_interleaved_diag(context) :
            asian_affine_barrier_vanilla_put_interleaved_diag(context);
    return request->side == ASIAN_AFFINE_BARRIER_CALL ?
        asian_affine_barrier_vanilla_call_grouped_diag(context) :
        asian_affine_barrier_vanilla_put_grouped_diag(context);
}

__attribute__((noinline, used))
int asian_affine_barrier_prepared_price(
    const asian_affine_barrier_request_t *request,
    asian_affine_barrier_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_AFFINE_BARRIER_REQUEST_MAGIC ||
        request->context.magic != ASIAN_AFFINE_BARRIER_REQUEST_MAGIC ||
        request->carrier == NULL ||
        request->carrier->magic != ASIAN_AFFINE_BARRIER_CARRIER_MAGIC)
        return ASIAN_AFFINE_BARRIER_INVALID;
    output->price = invoke_leaf(request);
    output->monitoring_count = request->context.monitoring_count;
    output->product = request->product;
    output->direction = request->direction;
    output->side = request->side;
    output->magic = ASIAN_AFFINE_BARRIER_OUTPUT_MAGIC;
    output->reserved = 0u;
    return ASIAN_AFFINE_BARRIER_OK;
}

__attribute__((noinline, used))
int asian_affine_barrier_reuse_total(
    asian_affine_barrier_engine_t *engine,
    const asian_affine_barrier_carrier_t *carrier,
    const asian_affine_barrier_request_input_t *input,
    asian_affine_barrier_output_t *output)
{
    if (engine == NULL || engine->magic != ASIAN_AFFINE_BARRIER_ENGINE_MAGIC ||
        engine->workspace == NULL)
        return ASIAN_AFFINE_BARRIER_INVALID;
    int status = asian_affine_barrier_request_prepare(
        engine, carrier, input, &engine->workspace->request);
    if (status == ASIAN_AFFINE_BARRIER_OK)
        status = asian_affine_barrier_prepared_price(
            &engine->workspace->request, output);
    return status;
}

__attribute__((noinline, used))
int asian_affine_barrier_fresh_total(
    asian_affine_barrier_engine_t *engine,
    const asian_affine_barrier_request_input_t *input,
    asian_affine_barrier_output_t *output)
{
    if (engine == NULL || input == NULL || engine->workspace == NULL)
        return ASIAN_AFFINE_BARRIER_INVALID;
    const asian_affine_barrier_market_input_t market = {
        input->rate, input->dividend_yield, input->sigma, input->maturity,
        input->monitoring_count};
    int status = asian_affine_barrier_market_prepare(
        engine, &market, &engine->workspace->carrier);
    if (status == ASIAN_AFFINE_BARRIER_OK)
        status = asian_affine_barrier_request_prepare(
            engine, &engine->workspace->carrier, input,
            &engine->workspace->request);
    if (status == ASIAN_AFFINE_BARRIER_OK)
        status = asian_affine_barrier_prepared_price(
            &engine->workspace->request, output);
    return status;
}

double asian_affine_barrier_knock_in_from_parity(double matched_vanilla,
                                                  double knock_out)
{
    return matched_vanilla - knock_out;
}
