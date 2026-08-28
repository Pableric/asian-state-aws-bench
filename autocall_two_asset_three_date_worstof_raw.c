#define _POSIX_C_SOURCE 200112L

#include "private/autocall_two_asset_three_date_worstof_raw_diag.h"
#include "private/asian_genuine_fixed_block_source_diag.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define AUTOCALL_WORSTOF_IDENTITY UINT32_C(0x574f5232)

static int valid_market(const autocall_worstof_market_input_t *m) {
    return m != NULL && isfinite(m->rate) && isfinite(m->maturity) &&
           m->maturity > 0.0 && isfinite(m->dividend_a) &&
           isfinite(m->sigma_a) && m->sigma_a > 0.0 &&
           isfinite(m->dividend_b) && isfinite(m->sigma_b) &&
           m->sigma_b > 0.0 && isfinite(m->rho) && m->rho > -1.0 &&
           m->rho < 1.0;
}

static int prepare_market_constants(
    const autocall_worstof_engine_t *engine,
    const autocall_worstof_market_input_t *input,
    float *drift_a, float *diffusion_a, float *drift_b, float *diffusion_b,
    float *rho, float *cholesky) {
    if (!valid_market(input))
        return AUTOCALL_WORSTOF_INVALID;
    const double dt = input->maturity / 3.0;
    *drift_a = (float)((input->rate - input->dividend_a -
                        0.5 * input->sigma_a * input->sigma_a) * dt);
    *diffusion_a = (float)(input->sigma_a * sqrt(dt));
    *drift_b = (float)((input->rate - input->dividend_b -
                        0.5 * input->sigma_b * input->sigma_b) * dt);
    *diffusion_b = (float)(input->sigma_b * sqrt(dt));
    *rho = (float)input->rho;
    *cholesky = (float)sqrt((1.0 - input->rho) * (1.0 + input->rho));
    if (!isfinite(*drift_a) || !isfinite(*diffusion_a) ||
        !isfinite(*drift_b) || !isfinite(*diffusion_b) ||
        !isfinite(*rho) || !isfinite(*cholesky) ||
        *diffusion_a <= 0.0f || *diffusion_b <= 0.0f ||
        *cholesky <= 0.0f)
        return AUTOCALL_WORSTOF_UNSUPPORTED;

    const float za_lo = engine->signed_z_min;
    const float za_hi = engine->signed_z_max;
    const float zb_bound = (fabsf(*rho) + *cholesky) *
                           fmaxf(-za_lo, za_hi);
    const float a_lo = fmaf(*diffusion_a, za_lo, *drift_a);
    const float a_hi = fmaf(*diffusion_a, za_hi, *drift_a);
    const float b_lo = fmaf(*diffusion_b, -zb_bound, *drift_b);
    const float b_hi = fmaf(*diffusion_b, zb_bound, *drift_b);
    if (!isfinite(a_lo) || !isfinite(a_hi) || !isfinite(b_lo) ||
        !isfinite(b_hi) || a_lo < -87.0f || a_hi > 88.0f ||
        b_lo < -87.0f || b_hi > 88.0f)
        return AUTOCALL_WORSTOF_UNSUPPORTED;
    return AUTOCALL_WORSTOF_OK;
}

static uint64_t next_generation(autocall_worstof_engine_t *engine) {
    uint64_t g = engine->next_generation++;
    if (g == 0u)
        g = engine->next_generation++;
    return g;
}

int autocall_worstof_engine_create(autocall_worstof_engine_t *engine) {
    if (engine == NULL || ((uintptr_t)engine & 63u) != 0u)
        return AUTOCALL_WORSTOF_INVALID;
    memset(engine, 0, sizeof(*engine));
    if (asian_meta_affine_plan_create(&engine->affine_plan) != 0)
        return AUTOCALL_WORSTOF_INVALID;
    if (posix_memalign((void **)&engine->workspace, 64u,
                       sizeof(*engine->workspace)) != 0) {
        asian_meta_affine_plan_destroy(engine->affine_plan);
        engine->affine_plan = NULL;
        return AUTOCALL_WORSTOF_INVALID;
    }
    memset(engine->workspace, 0, sizeof(*engine->workspace));
    {
        const uint32_t lo_bits = UINT32_C(0xc075e22f);
        const uint32_t hi_bits = UINT32_C(0x4075e233);
        memcpy(&engine->signed_z_min, &lo_bits, sizeof(lo_bits));
        memcpy(&engine->signed_z_max, &hi_bits, sizeof(hi_bits));
    }
    engine->next_generation = 1u;
    engine->magic = AUTOCALL_WORSTOF_ENGINE_MAGIC;
    return AUTOCALL_WORSTOF_OK;
}

void autocall_worstof_engine_destroy(autocall_worstof_engine_t *engine) {
    if (engine != NULL) {
        free(engine->workspace);
        asian_meta_affine_plan_destroy(engine->affine_plan);
        memset(engine, 0, sizeof(*engine));
    }
}

static int engine_and_output_ok(const autocall_worstof_engine_t *engine,
                                const void *output) {
    return engine != NULL && engine->magic == AUTOCALL_WORSTOF_ENGINE_MAGIC &&
           engine->affine_plan != NULL && engine->workspace != NULL &&
           output != NULL;
}

__attribute__((noinline, used))
int autocall_worstof_prepared_market_prepare(
    autocall_worstof_engine_t *engine,
    const autocall_worstof_market_input_t *input,
    autocall_worstof_prepared_market_t *market) {
    if (!engine_and_output_ok(engine, market) ||
        ((uintptr_t)market & 63u) != 0u)
        return AUTOCALL_WORSTOF_INVALID;
    int status = prepare_market_constants(
        engine, input, &market->drift_a, &market->diffusion_a,
        &market->drift_b, &market->diffusion_b, &market->rho,
        &market->cholesky);
    if (status != AUTOCALL_WORSTOF_OK)
        return status;
    if (asian_meta_affine_routes_bind(
            engine->affine_plan, NULL, asian_genuine_fixed_block_signed_z,
            AUTOCALL_WORSTOF_DIMENSIONS,
            engine->workspace->source_routes) != 0)
        return AUTOCALL_WORSTOF_INVALID;

    market->asset_a_fused.signed_z = asian_genuine_fixed_block_signed_z;
    market->asset_a_fused.growth_out = market->asset_a_growth;
    market->asset_a_fused.drift = market->drift_a;
    market->asset_a_fused.diffusion = market->diffusion_a;
    market->asset_a_fused.fixing_count = AUTOCALL_WORSTOF_DATES;
    market->asset_a_fused.path_count = AUTOCALL_WORSTOF_PATHS;
    market->asset_a_fused.region_count = 2u;
    market->asset_a_fused.values_per_region = AUTOCALL_WORSTOF_PATHS;
    market->asset_a_fused.first_index = ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    market->asset_a_fused.total_values = AUTOCALL_WORSTOF_DONOR_VALUES;
    market->asset_a_fused.magic =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC;
    market->asset_a_fused.abi_version =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION;
    market->asset_a_fused.reserved0 = 0u;
    memset(market->asset_a_fused.reserved, 0,
           sizeof(market->asset_a_fused.reserved));

    autocall_worstof_b_producer_context_t b = {0};
    b.direct_d1 = engine->workspace->source_routes[0].growth_base;
    b.routes_d2 = engine->workspace->source_routes + 1;
    b.output = &market->asset_b_growth[0][0];
    b.drift_b = market->drift_b;
    b.diffusion_b = market->diffusion_b;
    b.rho = market->rho;
    b.cholesky = market->cholesky;
    b.magic = AUTOCALL_WORSTOF_B_PRODUCER_MAGIC;

    market->input = *input;
    market->generation = next_generation(engine);
    market->magic = AUTOCALL_WORSTOF_PREPARED_MARKET_MAGIC;
    memset(market->reserved, 0, sizeof(market->reserved));
    asian_genuine_arithmetic_fused_source_exp_diag(&market->asset_a_fused);
    autocall_worstof_prepare_asset_b_growth(&b);
    return AUTOCALL_WORSTOF_OK;
}

__attribute__((noinline, used))
int autocall_worstof_inline_market_prepare(
    autocall_worstof_engine_t *engine,
    const autocall_worstof_market_input_t *input,
    autocall_worstof_inline_market_t *market) {
    if (!engine_and_output_ok(engine, market) ||
        ((uintptr_t)market & 63u) != 0u)
        return AUTOCALL_WORSTOF_INVALID;
    int status = prepare_market_constants(
        engine, input, &market->drift_a, &market->diffusion_a,
        &market->drift_b, &market->diffusion_b, &market->rho,
        &market->cholesky);
    if (status != AUTOCALL_WORSTOF_OK)
        return status;
    market->input = *input;
    market->generation = next_generation(engine);
    market->magic = AUTOCALL_WORSTOF_INLINE_MARKET_MAGIC;
    memset(market->reserved, 0, sizeof(market->reserved));
    return AUTOCALL_WORSTOF_OK;
}

static int valid_contract(const autocall_worstof_market_input_t *market,
                          const autocall_worstof_contract_input_t *input) {
    if (market == NULL || input == NULL || !isfinite(input->spot_a) ||
        input->spot_a <= 0.0 || !isfinite(input->spot_b) ||
        input->spot_b <= 0.0 || !isfinite(input->notional) ||
        input->notional <= 0.0 || !isfinite(input->protection_barrier) ||
        input->protection_barrier <= 0.0 ||
        !isfinite(input->terminal_payment_time) ||
        input->terminal_payment_time < market->maturity)
        return 0;
    for (unsigned d = 0; d < AUTOCALL_WORSTOF_DATES; ++d) {
        const double observation = market->maturity * (double)(d + 1u) / 3.0;
        if (!isfinite(input->call_barrier[d]) ||
            input->call_barrier[d] <= 0.0 ||
            !isfinite(input->coupon_barrier[d]) ||
            input->coupon_barrier[d] <= 0.0 ||
            !isfinite(input->coupon_cashflow[d]) ||
            input->coupon_cashflow[d] < 0.0 ||
            !isfinite(input->call_redemption[d]) ||
            input->call_redemption[d] <= 0.0 ||
            !isfinite(input->coupon_payment_time[d]) ||
            input->coupon_payment_time[d] < observation ||
            !isfinite(input->call_payment_time[d]) ||
            input->call_payment_time[d] < observation)
            return 0;
    }
    return 1;
}

static int fill_leaf_common(
    const autocall_worstof_market_input_t *market,
    const autocall_worstof_contract_input_t *input,
    autocall_worstof_date_record_t date[AUTOCALL_WORSTOF_DATES],
    float *spot_a, float *spot_b, float *inverse_spot_a,
    float *inverse_spot_b, float *protection_a, float *protection_b,
    float *discounted_terminal) {
    if (!valid_contract(market, input))
        return AUTOCALL_WORSTOF_INVALID;
    *spot_a = (float)input->spot_a;
    *spot_b = (float)input->spot_b;
    *inverse_spot_a = 1.0f / *spot_a;
    *inverse_spot_b = 1.0f / *spot_b;
    *protection_a = (float)(input->spot_a * input->protection_barrier);
    *protection_b = (float)(input->spot_b * input->protection_barrier);
    *discounted_terminal = (float)(input->notional *
        exp(-market->rate * input->terminal_payment_time));
    for (unsigned d = 0; d < AUTOCALL_WORSTOF_DATES; ++d) {
        date[d].coupon_a = (float)(input->spot_a * input->coupon_barrier[d]);
        date[d].coupon_b = (float)(input->spot_b * input->coupon_barrier[d]);
        date[d].call_a = (float)(input->spot_a * input->call_barrier[d]);
        date[d].call_b = (float)(input->spot_b * input->call_barrier[d]);
        date[d].discounted_coupon = (float)(input->coupon_cashflow[d] *
            exp(-market->rate * input->coupon_payment_time[d]));
        date[d].discounted_call = (float)(input->call_redemption[d] *
            exp(-market->rate * input->call_payment_time[d]));
        date[d].reserved[0] = 0u;
        date[d].reserved[1] = 0u;
    }
    if (!isfinite(*spot_a) || *spot_a <= 0.0f || !isfinite(*spot_b) ||
        *spot_b <= 0.0f || !isfinite(*inverse_spot_a) ||
        *inverse_spot_a <= 0.0f || !isfinite(*inverse_spot_b) ||
        *inverse_spot_b <= 0.0f || !isfinite(*protection_a) ||
        *protection_a <= 0.0f || !isfinite(*protection_b) ||
        *protection_b <= 0.0f || !isfinite(*discounted_terminal) ||
        *discounted_terminal <= 0.0f)
        return AUTOCALL_WORSTOF_UNSUPPORTED;
    for (unsigned d = 0; d < AUTOCALL_WORSTOF_DATES; ++d)
        if (!isfinite(date[d].coupon_a) || date[d].coupon_a <= 0.0f ||
            !isfinite(date[d].coupon_b) || date[d].coupon_b <= 0.0f ||
            !isfinite(date[d].call_a) || date[d].call_a <= 0.0f ||
            !isfinite(date[d].call_b) || date[d].call_b <= 0.0f ||
            !isfinite(date[d].discounted_coupon) ||
            date[d].discounted_coupon < 0.0f ||
            !isfinite(date[d].discounted_call) ||
            date[d].discounted_call <= 0.0f)
            return AUTOCALL_WORSTOF_UNSUPPORTED;
    return AUTOCALL_WORSTOF_OK;
}

__attribute__((noinline, used))
int autocall_worstof_prepared_request_prepare(
    const autocall_worstof_engine_t *engine,
    const autocall_worstof_prepared_market_t *market,
    const autocall_worstof_contract_input_t *input,
    autocall_worstof_prepared_request_t *request) {
    if (engine == NULL || engine->magic != AUTOCALL_WORSTOF_ENGINE_MAGIC ||
        engine->affine_plan == NULL || market == NULL ||
        market->magic != AUTOCALL_WORSTOF_PREPARED_MARKET_MAGIC ||
        request == NULL || ((uintptr_t)request & 63u) != 0u)
        return AUTOCALL_WORSTOF_INVALID;
    if (asian_meta_affine_routes_bind(engine->affine_plan, NULL,
            market->asset_a_growth, AUTOCALL_WORSTOF_DIMENSIONS,
            request->routes) != 0)
        return AUTOCALL_WORSTOF_INVALID;
    autocall_worstof_prepared_leaf_context_t *leaf = &request->leaf;
    int status = fill_leaf_common(
        &market->input, input, leaf->date, &leaf->spot_a, &leaf->spot_b,
        &leaf->inverse_spot_a, &leaf->inverse_spot_b,
        &leaf->protection_a, &leaf->protection_b,
        &leaf->discounted_terminal);
    if (status != AUTOCALL_WORSTOF_OK)
        return status;
    leaf->asset_a_d1 = request->routes[0].growth_base;
    leaf->asset_a_d3 = request->routes + 2;
    leaf->asset_a_d5 = request->routes + 4;
    leaf->asset_b_dates = &market->asset_b_growth[0][0];
    leaf->reserved0 = 0u;
    leaf->inverse_paths = 1.0 / (double)AUTOCALL_WORSTOF_PATHS;
    leaf->magic = AUTOCALL_WORSTOF_PREPARED_REQUEST_MAGIC;
    memset(leaf->reserved, 0, sizeof(leaf->reserved));
    request->market = market;
    request->market_generation = market->generation;
    request->magic = AUTOCALL_WORSTOF_PREPARED_REQUEST_MAGIC;
    memset(request->reserved, 0, sizeof(request->reserved));
    return AUTOCALL_WORSTOF_OK;
}

__attribute__((noinline, used))
int autocall_worstof_inline_request_prepare(
    const autocall_worstof_engine_t *engine,
    const autocall_worstof_inline_market_t *market,
    const autocall_worstof_contract_input_t *input,
    autocall_worstof_inline_request_t *request) {
    if (engine == NULL || engine->magic != AUTOCALL_WORSTOF_ENGINE_MAGIC ||
        engine->affine_plan == NULL || market == NULL ||
        market->magic != AUTOCALL_WORSTOF_INLINE_MARKET_MAGIC ||
        request == NULL || ((uintptr_t)request & 63u) != 0u)
        return AUTOCALL_WORSTOF_INVALID;
    if (asian_meta_affine_routes_bind(engine->affine_plan, NULL,
            asian_genuine_fixed_block_signed_z,
            AUTOCALL_WORSTOF_DIMENSIONS, request->routes) != 0)
        return AUTOCALL_WORSTOF_INVALID;
    autocall_worstof_inline_leaf_context_t *leaf = &request->leaf;
    int status = fill_leaf_common(
        &market->input, input, leaf->date, &leaf->spot_a, &leaf->spot_b,
        &leaf->inverse_spot_a, &leaf->inverse_spot_b,
        &leaf->protection_a, &leaf->protection_b,
        &leaf->discounted_terminal);
    if (status != AUTOCALL_WORSTOF_OK)
        return status;
    leaf->direct_d1 = request->routes[0].growth_base;
    leaf->routes_d2 = request->routes + 1;
    leaf->drift_a = market->drift_a;
    leaf->diffusion_a = market->diffusion_a;
    leaf->drift_b = market->drift_b;
    leaf->diffusion_b = market->diffusion_b;
    leaf->rho = market->rho;
    leaf->cholesky = market->cholesky;
    leaf->reserved0 = 0u;
    leaf->inverse_paths = 1.0 / (double)AUTOCALL_WORSTOF_PATHS;
    leaf->magic = AUTOCALL_WORSTOF_INLINE_REQUEST_MAGIC;
    memset(leaf->reserved, 0, sizeof(leaf->reserved));
    request->market = market;
    request->market_generation = market->generation;
    request->magic = AUTOCALL_WORSTOF_INLINE_REQUEST_MAGIC;
    memset(request->reserved, 0, sizeof(request->reserved));
    return AUTOCALL_WORSTOF_OK;
}

__attribute__((noinline, used))
int autocall_worstof_prepared_price(
    const autocall_worstof_prepared_request_t *request,
    autocall_worstof_output_t *output) {
    if (request == NULL || output == NULL ||
        request->magic != AUTOCALL_WORSTOF_PREPARED_REQUEST_MAGIC ||
        request->leaf.magic != AUTOCALL_WORSTOF_PREPARED_REQUEST_MAGIC ||
        request->leaf.asset_a_d1 != request->routes[0].growth_base ||
        request->leaf.asset_a_d3 != request->routes + 2 ||
        request->leaf.asset_a_d5 != request->routes + 4 ||
        request->market == NULL ||
        request->market->magic != AUTOCALL_WORSTOF_PREPARED_MARKET_MAGIC ||
        request->market_generation == 0u ||
        request->market_generation != request->market->generation)
        return AUTOCALL_WORSTOF_INVALID;
    output->price = autocall_worstof_prepared_leaf(&request->leaf);
    output->market_generation = request->market_generation;
    output->magic = AUTOCALL_WORSTOF_OUTPUT_MAGIC;
    output->identity = AUTOCALL_WORSTOF_IDENTITY;
    return AUTOCALL_WORSTOF_OK;
}

__attribute__((noinline, used))
int autocall_worstof_inline_price(
    const autocall_worstof_inline_request_t *request,
    autocall_worstof_output_t *output) {
    if (request == NULL || output == NULL ||
        request->magic != AUTOCALL_WORSTOF_INLINE_REQUEST_MAGIC ||
        request->leaf.magic != AUTOCALL_WORSTOF_INLINE_REQUEST_MAGIC ||
        request->leaf.direct_d1 != request->routes[0].growth_base ||
        request->leaf.routes_d2 != request->routes + 1 ||
        request->market == NULL ||
        request->market->magic != AUTOCALL_WORSTOF_INLINE_MARKET_MAGIC ||
        request->market_generation == 0u ||
        request->market_generation != request->market->generation)
        return AUTOCALL_WORSTOF_INVALID;
    output->price = autocall_worstof_inline_leaf(&request->leaf);
    output->market_generation = request->market_generation;
    output->magic = AUTOCALL_WORSTOF_OUTPUT_MAGIC;
    output->identity = AUTOCALL_WORSTOF_IDENTITY;
    return AUTOCALL_WORSTOF_OK;
}

__attribute__((noinline, used))
int autocall_worstof_prepared_reuse_total(
    autocall_worstof_engine_t *engine,
    const autocall_worstof_prepared_market_t *market,
    const autocall_worstof_contract_input_t *input,
    autocall_worstof_output_t *output) {
    if (!engine_and_output_ok(engine, output))
        return AUTOCALL_WORSTOF_INVALID;
    int status = autocall_worstof_prepared_request_prepare(
        engine, market, input, &engine->workspace->request.prepared);
    if (status == AUTOCALL_WORSTOF_OK)
        status = autocall_worstof_prepared_price(
            &engine->workspace->request.prepared, output);
    return status;
}

__attribute__((noinline, used))
int autocall_worstof_inline_reuse_total(
    autocall_worstof_engine_t *engine,
    const autocall_worstof_inline_market_t *market,
    const autocall_worstof_contract_input_t *input,
    autocall_worstof_output_t *output) {
    if (!engine_and_output_ok(engine, output))
        return AUTOCALL_WORSTOF_INVALID;
    int status = autocall_worstof_inline_request_prepare(
        engine, market, input, &engine->workspace->request.inline_request);
    if (status == AUTOCALL_WORSTOF_OK)
        status = autocall_worstof_inline_price(
            &engine->workspace->request.inline_request, output);
    return status;
}

__attribute__((noinline, used))
int autocall_worstof_prepared_fresh_total(
    autocall_worstof_engine_t *engine,
    const autocall_worstof_market_input_t *market_input,
    const autocall_worstof_contract_input_t *contract,
    autocall_worstof_output_t *output) {
    if (!engine_and_output_ok(engine, output))
        return AUTOCALL_WORSTOF_INVALID;
    int status = autocall_worstof_prepared_market_prepare(
        engine, market_input, &engine->workspace->market.prepared);
    if (status == AUTOCALL_WORSTOF_OK)
        status = autocall_worstof_prepared_request_prepare(
            engine, &engine->workspace->market.prepared, contract,
            &engine->workspace->request.prepared);
    if (status == AUTOCALL_WORSTOF_OK)
        status = autocall_worstof_prepared_price(
            &engine->workspace->request.prepared, output);
    return status;
}

__attribute__((noinline, used))
int autocall_worstof_inline_fresh_total(
    autocall_worstof_engine_t *engine,
    const autocall_worstof_market_input_t *market_input,
    const autocall_worstof_contract_input_t *contract,
    autocall_worstof_output_t *output) {
    if (!engine_and_output_ok(engine, output))
        return AUTOCALL_WORSTOF_INVALID;
    int status = autocall_worstof_inline_market_prepare(
        engine, market_input, &engine->workspace->market.inline_market);
    if (status == AUTOCALL_WORSTOF_OK)
        status = autocall_worstof_inline_request_prepare(
            engine, &engine->workspace->market.inline_market, contract,
            &engine->workspace->request.inline_request);
    if (status == AUTOCALL_WORSTOF_OK)
        status = autocall_worstof_inline_price(
            &engine->workspace->request.inline_request, output);
    return status;
}
