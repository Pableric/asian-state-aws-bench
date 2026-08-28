#define _POSIX_C_SOURCE 200112L

#include "private/autocall_single_asset_three_date_raw_diag.h"
#include "private/asian_genuine_fixed_block_source_diag.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int same_double(double a, double b)
{
    uint64_t aa, bb;
    memcpy(&aa, &a, sizeof(aa));
    memcpy(&bb, &b, sizeof(bb));
    return aa == bb;
}

static int valid_market(const autocall_3date_market_input_t *input)
{
    return input != NULL && isfinite(input->rate) &&
        isfinite(input->dividend_yield) && isfinite(input->sigma) &&
        input->sigma > 0.0 && isfinite(input->maturity) &&
        input->maturity > 0.0;
}

int autocall_3date_engine_create(autocall_3date_engine_t *engine)
{
    if (engine == NULL || ((uintptr_t)engine & 63u) != 0u)
        return AUTOCALL_3DATE_INVALID;
    memset(engine, 0, sizeof(*engine));
    if (asian_meta_affine_plan_create(&engine->affine_plan) != 0)
        return AUTOCALL_3DATE_INVALID;
    if (posix_memalign((void **)&engine->workspace, 64u,
                       sizeof(*engine->workspace)) != 0) {
        asian_meta_affine_plan_destroy(engine->affine_plan);
        engine->affine_plan = NULL;
        return AUTOCALL_3DATE_INVALID;
    }
    memset(engine->workspace, 0, sizeof(*engine->workspace));
    const uint32_t lo_bits = UINT32_C(0xc075e22f);
    const uint32_t hi_bits = UINT32_C(0x4075e233);
    memcpy(&engine->signed_z_min, &lo_bits, sizeof(lo_bits));
    memcpy(&engine->signed_z_max, &hi_bits, sizeof(hi_bits));
    engine->next_generation = 1u;
    engine->magic = AUTOCALL_3DATE_ENGINE_MAGIC;
    return AUTOCALL_3DATE_OK;
}

void autocall_3date_engine_destroy(autocall_3date_engine_t *engine)
{
    if (engine != NULL) {
        free(engine->workspace);
        asian_meta_affine_plan_destroy(engine->affine_plan);
        memset(engine, 0, sizeof(*engine));
    }
}

__attribute__((noinline, used))
int autocall_3date_market_prepare(autocall_3date_engine_t *engine,
    const autocall_3date_market_input_t *input,
    autocall_3date_carrier_t *carrier)
{
    if (engine == NULL || engine->magic != AUTOCALL_3DATE_ENGINE_MAGIC ||
        engine->affine_plan == NULL || carrier == NULL ||
        ((uintptr_t)carrier & 63u) != 0u || !valid_market(input))
        return AUTOCALL_3DATE_INVALID;

    const double dt = input->maturity / 3.0;
    const float drift = (float)((input->rate - input->dividend_yield -
                                 0.5 * input->sigma * input->sigma) * dt);
    const float diffusion = (float)(input->sigma * sqrt(dt));
    if (!isfinite(drift) || !isfinite(diffusion) || diffusion <= 0.0f)
        return AUTOCALL_3DATE_UNSUPPORTED;
    const float lo = fmaf(diffusion, engine->signed_z_min, drift);
    const float hi = fmaf(diffusion, engine->signed_z_max, drift);
    if (!isfinite(lo) || !isfinite(hi) || lo < -87.0f || hi > 88.0f)
        return AUTOCALL_3DATE_UNSUPPORTED;

    memset(&carrier->fused, 0, sizeof(carrier->fused));
    carrier->fused.signed_z = asian_genuine_fixed_block_signed_z;
    carrier->fused.growth_out = carrier->growth;
    carrier->fused.drift = drift;
    carrier->fused.diffusion = diffusion;
    carrier->fused.fixing_count = AUTOCALL_3DATE_DATES;
    carrier->fused.path_count = AUTOCALL_3DATE_PATHS;
    carrier->fused.region_count = 2u;
    carrier->fused.values_per_region = AUTOCALL_3DATE_PATHS;
    carrier->fused.first_index = ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    carrier->fused.total_values = AUTOCALL_3DATE_DONOR_VALUES;
    carrier->fused.magic = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC;
    carrier->fused.abi_version =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION;
    carrier->market = *input;
    carrier->generation = engine->next_generation++;
    if (carrier->generation == 0u)
        carrier->generation = engine->next_generation++;
    carrier->magic = AUTOCALL_3DATE_CARRIER_MAGIC;
    memset(carrier->reserved, 0, sizeof(carrier->reserved));
    asian_genuine_arithmetic_fused_source_exp_diag(&carrier->fused);
    return AUTOCALL_3DATE_OK;
}

static int valid_request(const autocall_3date_carrier_t *carrier,
                         const autocall_3date_request_input_t *input)
{
    if (carrier == NULL || input == NULL ||
        carrier->magic != AUTOCALL_3DATE_CARRIER_MAGIC ||
        !isfinite(input->s0) || input->s0 <= 0.0 ||
        !isfinite(input->notional) || input->notional <= 0.0 ||
        !isfinite(input->protection_barrier) ||
        input->protection_barrier <= 0.0 ||
        !isfinite(input->rate) || !isfinite(input->dividend_yield) ||
        !isfinite(input->sigma) || input->sigma <= 0.0 ||
        !isfinite(input->maturity) || input->maturity <= 0.0 ||
        !same_double(input->rate, carrier->market.rate) ||
        !same_double(input->dividend_yield,
                     carrier->market.dividend_yield) ||
        !same_double(input->sigma, carrier->market.sigma) ||
        !same_double(input->maturity, carrier->market.maturity) ||
        !isfinite(input->terminal_payment_time) ||
        input->terminal_payment_time < input->maturity)
        return 0;
    for (uint32_t date = 0; date < AUTOCALL_3DATE_DATES; ++date) {
        const double observation = input->maturity * (double)(date + 1u) / 3.0;
        if (!isfinite(input->call_barrier[date]) ||
            input->call_barrier[date] <= 0.0 ||
            !isfinite(input->coupon_barrier[date]) ||
            input->coupon_barrier[date] <= 0.0 ||
            !isfinite(input->coupon_cashflow[date]) ||
            input->coupon_cashflow[date] < 0.0 ||
            !isfinite(input->call_redemption[date]) ||
            input->call_redemption[date] <= 0.0 ||
            !isfinite(input->coupon_payment_time[date]) ||
            input->coupon_payment_time[date] < observation ||
            !isfinite(input->call_payment_time[date]) ||
            input->call_payment_time[date] < observation)
            return 0;
    }
    return 1;
}

__attribute__((noinline, used))
int autocall_3date_request_prepare(const autocall_3date_engine_t *engine,
    const autocall_3date_carrier_t *carrier,
    const autocall_3date_request_input_t *input,
    autocall_3date_request_t *request)
{
    if (engine == NULL || engine->magic != AUTOCALL_3DATE_ENGINE_MAGIC ||
        engine->affine_plan == NULL || request == NULL ||
        ((uintptr_t)request & 63u) != 0u || !valid_request(carrier, input))
        return AUTOCALL_3DATE_INVALID;
    if (asian_meta_affine_routes_bind(engine->affine_plan, NULL,
            carrier->growth, AUTOCALL_3DATE_DATES, request->routes) != 0)
        return AUTOCALL_3DATE_INVALID;

    autocall_3date_leaf_context_t *context = &request->context;
    memset(context, 0, sizeof(*context));
    context->d1_growth = request->routes[0].growth_base;
    context->routes_d2 = request->routes + 1;
    for (uint32_t date = 0; date < AUTOCALL_3DATE_DATES; ++date) {
        context->date[date].coupon_barrier =
            (float)input->coupon_barrier[date];
        context->date[date].call_barrier =
            (float)input->call_barrier[date];
        context->date[date].discounted_coupon = (float)(
            input->coupon_cashflow[date] *
            exp(-input->rate * input->coupon_payment_time[date]));
        context->date[date].discounted_call_redemption = (float)(
            input->call_redemption[date] *
            exp(-input->rate * input->call_payment_time[date]));
    }
    context->initial_spot = (float)input->s0;
    context->inverse_initial_spot = 1.0f / (float)input->s0;
    context->protection_barrier = (float)input->protection_barrier;
    context->discounted_terminal_notional = (float)(
        input->notional * exp(-input->rate * input->terminal_payment_time));
    context->inverse_paths = 1.0 / (double)AUTOCALL_3DATE_PATHS;
    if (!isfinite(context->initial_spot) || context->initial_spot <= 0.0f ||
        !isfinite(context->inverse_initial_spot) ||
        context->inverse_initial_spot <= 0.0f ||
        !isfinite(context->protection_barrier) ||
        context->protection_barrier <= 0.0f ||
        !isfinite(context->discounted_terminal_notional) ||
        context->discounted_terminal_notional <= 0.0f)
        return AUTOCALL_3DATE_UNSUPPORTED;
    for (uint32_t date = 0; date < AUTOCALL_3DATE_DATES; ++date)
        if (!isfinite(context->date[date].coupon_barrier) ||
            context->date[date].coupon_barrier <= 0.0f ||
            !isfinite(context->date[date].call_barrier) ||
            context->date[date].call_barrier <= 0.0f ||
            !isfinite(context->date[date].discounted_coupon) ||
            context->date[date].discounted_coupon < 0.0f ||
            !isfinite(context->date[date].discounted_call_redemption) ||
            context->date[date].discounted_call_redemption <= 0.0f)
            return AUTOCALL_3DATE_UNSUPPORTED;
    context->magic = AUTOCALL_3DATE_REQUEST_MAGIC;
    request->carrier = carrier;
    request->carrier_generation = carrier->generation;
    request->magic = AUTOCALL_3DATE_REQUEST_MAGIC;
    memset(request->reserved, 0, sizeof(request->reserved));
    return AUTOCALL_3DATE_OK;
}

__attribute__((noinline, used))
int autocall_3date_prepared_price(const autocall_3date_request_t *request,
    autocall_3date_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != AUTOCALL_3DATE_REQUEST_MAGIC ||
        request->context.magic != AUTOCALL_3DATE_REQUEST_MAGIC ||
        request->context.d1_growth != request->routes[0].growth_base ||
        request->context.routes_d2 != request->routes + 1 ||
        request->carrier == NULL ||
        request->carrier->magic != AUTOCALL_3DATE_CARRIER_MAGIC ||
        request->carrier_generation == 0u ||
        request->carrier_generation != request->carrier->generation)
        return AUTOCALL_3DATE_INVALID;
    output->price = autocall_3date_affine_price_leaf(&request->context);
    output->carrier_generation = request->carrier_generation;
    output->magic = AUTOCALL_3DATE_OUTPUT_MAGIC;
    output->reserved = 0u;
    return AUTOCALL_3DATE_OK;
}

__attribute__((noinline, used))
int autocall_3date_reuse_total(autocall_3date_engine_t *engine,
    const autocall_3date_carrier_t *carrier,
    const autocall_3date_request_input_t *input,
    autocall_3date_output_t *output)
{
    if (engine == NULL || engine->magic != AUTOCALL_3DATE_ENGINE_MAGIC ||
        engine->workspace == NULL)
        return AUTOCALL_3DATE_INVALID;
    int status = autocall_3date_request_prepare(
        engine, carrier, input, &engine->workspace->request);
    if (status == AUTOCALL_3DATE_OK)
        status = autocall_3date_prepared_price(
            &engine->workspace->request, output);
    return status;
}

__attribute__((noinline, used))
int autocall_3date_fresh_total(autocall_3date_engine_t *engine,
    const autocall_3date_request_input_t *input,
    autocall_3date_output_t *output)
{
    if (engine == NULL || input == NULL || engine->workspace == NULL)
        return AUTOCALL_3DATE_INVALID;
    const autocall_3date_market_input_t market = {
        input->rate, input->dividend_yield, input->sigma, input->maturity};
    int status = autocall_3date_market_prepare(
        engine, &market, &engine->workspace->carrier);
    if (status == AUTOCALL_3DATE_OK)
        status = autocall_3date_request_prepare(
            engine, &engine->workspace->carrier, input,
            &engine->workspace->request);
    if (status == AUTOCALL_3DATE_OK)
        status = autocall_3date_prepared_price(
            &engine->workspace->request, output);
    return status;
}
