#include "private/asian_affine_route_family_diag.h"

#include "private/asian_geometric_cv_diag.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    GENERIC_PLAN_MAGIC = 0x54525351u,
    GENERIC_IMMEDIATE_MAGIC = 0x47494641u,
    GENERIC_PACKET_MAGIC = 0x47504641u,
};

/* Frozen private layout created by asian_meta_qsort_control_plan_create(). */
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
    uint64_t aa, bb;
    memcpy(&aa, &a, sizeof(aa));
    memcpy(&bb, &b, sizeof(bb));
    return aa == bb;
}

static int valid_market(const asian_affine_family_carrier_input_t *input)
{
    return input != NULL && isfinite(input->rate) &&
        isfinite(input->dividend_yield) && isfinite(input->sigma) &&
        input->sigma > 0.0 && isfinite(input->maturity) &&
        input->maturity > 0.0 && input->future_fixings >= 1u &&
        input->future_fixings <= ASIAN_AFFINE_FAMILY_MAX_FIXINGS;
}

static int qualified_coefficients(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_carrier_input_t *input,
    float *drift, float *diffusion)
{
    if (engine == NULL || engine->magic != ASIAN_AFFINE_FAMILY_ENGINE_MAGIC ||
        engine->affine_plan == NULL || !valid_market(input) ||
        drift == NULL || diffusion == NULL)
        return -1;
    const double dt = input->maturity / (double)input->future_fixings;
    *drift = (float)((input->rate - input->dividend_yield -
                      0.5 * input->sigma * input->sigma) * dt);
    *diffusion = (float)(input->sigma * sqrt(dt));
    if (!isfinite(*drift) || !isfinite(*diffusion) || *diffusion <= 0.0f)
        return -1;
    const float lo = fmaf(*diffusion, engine->signed_z_min, *drift);
    const float hi = fmaf(*diffusion, engine->signed_z_max, *drift);
    return isfinite(lo) && isfinite(hi) && lo >= -87.0f && hi <= 88.0f
        ? 0 : -1;
}

int asian_affine_family_engine_create(asian_affine_family_engine_t *engine)
{
    if (engine == NULL || ((uintptr_t)engine & 63u) != 0u)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(engine, 0, sizeof(*engine));
    if (asian_meta_affine_plan_create(&engine->affine_plan) != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    const uint32_t lo_bits = UINT32_C(0xc075e22f);
    const uint32_t hi_bits = UINT32_C(0x4075e233);
    memcpy(&engine->signed_z_min, &lo_bits, sizeof(lo_bits));
    memcpy(&engine->signed_z_max, &hi_bits, sizeof(hi_bits));
    engine->magic = ASIAN_AFFINE_FAMILY_ENGINE_MAGIC;
    return ASIAN_AFFINE_FAMILY_OK;
}

void asian_affine_family_engine_destroy(asian_affine_family_engine_t *engine)
{
    if (engine != NULL) {
        asian_meta_affine_plan_destroy(engine->affine_plan);
        memset(engine, 0, sizeof(*engine));
    }
}

int asian_affine_family_generic_oracle_create(
    asian_affine_family_generic_oracle_t *oracle)
{
    if (oracle == NULL || ((uintptr_t)oracle & 63u) != 0u)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(oracle, 0, sizeof(*oracle));
    if (asian_meta_qsort_control_plan_create(&oracle->generic_plan) != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    oracle->magic = ASIAN_AFFINE_FAMILY_ORACLE_MAGIC;
    return ASIAN_AFFINE_FAMILY_OK;
}

void asian_affine_family_generic_oracle_destroy(
    asian_affine_family_generic_oracle_t *oracle)
{
    if (oracle != NULL) {
        asian_meta_qsort_control_plan_destroy(oracle->generic_plan);
        memset(oracle, 0, sizeof(*oracle));
    }
}

__attribute__((noinline, used))
int asian_affine_family_growth_carrier_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_carrier_input_t *input,
    asian_affine_family_growth_carrier_t *carrier)
{
    float drift, diffusion;
    if (carrier == NULL || ((uintptr_t)carrier & 63u) != 0u ||
        qualified_coefficients(engine, input, &drift, &diffusion) != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(&carrier->fused, 0, sizeof(carrier->fused));
    carrier->fused.signed_z = asian_genuine_fixed_block_signed_z;
    carrier->fused.growth_out = carrier->growth;
    carrier->fused.drift = drift;
    carrier->fused.diffusion = diffusion;
    carrier->fused.fixing_count = input->future_fixings;
    carrier->fused.path_count = ASIAN_AFFINE_FAMILY_PATHS;
    carrier->fused.region_count = 2u;
    carrier->fused.values_per_region = ASIAN_AFFINE_FAMILY_PATHS;
    carrier->fused.first_index = ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    carrier->fused.total_values = ASIAN_AFFINE_FAMILY_DONOR_VALUES;
    carrier->fused.magic = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC;
    carrier->fused.abi_version =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION;
    carrier->market = *input;
    carrier->magic = ASIAN_AFFINE_FAMILY_GROWTH_MAGIC;
    asian_genuine_arithmetic_fused_source_exp_diag(&carrier->fused);
    return ASIAN_AFFINE_FAMILY_OK;
}

__attribute__((noinline, used))
int asian_affine_family_xgrowth_carrier_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_carrier_input_t *input,
    asian_affine_family_xgrowth_carrier_t *carrier)
{
    float drift, diffusion;
    if (carrier == NULL || ((uintptr_t)carrier & 63u) != 0u ||
        qualified_coefficients(engine, input, &drift, &diffusion) != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(&carrier->source, 0, sizeof(carrier->source));
    carrier->source.signed_z = asian_genuine_fixed_block_signed_z;
    carrier->source.drift = drift;
    carrier->source.diffusion = diffusion;
    carrier->source.magic = ASIAN_GENUINE_FIXED_BLOCK_SOURCE_MAGIC;
    carrier->source.abi_version = ASIAN_GENUINE_FIXED_BLOCK_ABI_VERSION;
    carrier->market = *input;
    carrier->magic = ASIAN_AFFINE_FAMILY_XGROWTH_MAGIC;
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(
        &carrier->source, carrier->x);
    asian_vector_exp_range_reduced_array_diag(carrier->x, carrier->growth);
    asian_vector_exp_range_reduced_array_diag(
        carrier->x + ASIAN_AFFINE_FAMILY_PATHS,
        carrier->growth + ASIAN_AFFINE_FAMILY_PATHS);
    return ASIAN_AFFINE_FAMILY_OK;
}

static int request_matches_market(
    const asian_affine_family_carrier_input_t *market,
    const asian_affine_family_request_input_t *input)
{
    return market != NULL && input != NULL && isfinite(input->s0) &&
        input->s0 > 0.0 && input->strikes != NULL &&
        input->strike_count >= 1u &&
        input->strike_count <= ASIAN_AFFINE_FAMILY_MAX_STRIKES &&
        input->future_fixings == market->future_fixings &&
        input->future_fixings >= 1u && input->completed_fixings <= 255u &&
        input->future_fixings + input->completed_fixings <= 256u &&
        isfinite(input->initial_arithmetic_sum) &&
        isfinite(input->past_log_sum) &&
        same_double(input->rate, market->rate) &&
        same_double(input->dividend_yield, market->dividend_yield) &&
        same_double(input->sigma, market->sigma) &&
        same_double(input->maturity, market->maturity) &&
        (input->workload == ASIAN_AFFINE_FAMILY_PRICE ||
         input->workload == ASIAN_AFFINE_FAMILY_PRICE_DELTA);
}

static enum asian_affine_family_leaf selected_leaf(uint32_t count,
                                                    uint32_t future)
{
    if (future == 1u || count >= 5u)
        return ASIAN_AFFINE_FAMILY_MATERIALIZED;
    if (count == 1u)
        return ASIAN_AFFINE_FAMILY_LEAF1;
    if (count == 2u)
        return ASIAN_AFFINE_FAMILY_LEAF2;
    return ASIAN_AFFINE_FAMILY_LEAF4;
}

static int valid_provider(enum asian_affine_family_provider provider,
                          const asian_affine_family_generic_oracle_t *oracle)
{
    if (provider == ASIAN_AFFINE_FAMILY_AUTO)
        return ASIAN_AFFINE_FAMILY_POLICY_UNFROZEN;
    if (provider != ASIAN_AFFINE_FAMILY_GENERIC &&
        provider != ASIAN_AFFINE_FAMILY_AFFINE)
        return ASIAN_AFFINE_FAMILY_INVALID;
    if (provider == ASIAN_AFFINE_FAMILY_GENERIC &&
        (oracle == NULL || oracle->magic != ASIAN_AFFINE_FAMILY_ORACLE_MAGIC ||
         oracle->generic_plan == NULL))
        return ASIAN_AFFINE_FAMILY_ORACLE_REQUIRED;
    return ASIAN_AFFINE_FAMILY_OK;
}

static int bind_generic(
    const asian_affine_family_generic_oracle_t *oracle,
    const float *x, const float *growth, uint32_t future, uint32_t total,
    asian_genuine_route_t *routes)
{
    const struct asian_meta_qsort_control_plan *plan =
        (const struct asian_meta_qsort_control_plan *)oracle->generic_plan;
    if (plan == NULL || plan->magic != GENERIC_PLAN_MAGIC || growth == NULL ||
        routes == NULL || future < 1u || future > ASIAN_META_DIRECTIONS ||
        total < future)
        return -1;
    for (uint32_t fixing = 0; fixing < future; ++fixing) {
        const size_t offset =
            (size_t)plan->donor_region[fixing] * ASIAN_AFFINE_FAMILY_PATHS;
        routes[fixing].x_base = x == NULL ? NULL : x + offset;
        routes[fixing].growth_base = growth + offset;
        routes[fixing].map = &plan->maps[fixing];
        const float weight = (float)(future - fixing) / (float)total;
        memcpy(&routes[fixing].weight_bits, &weight, sizeof(weight));
        routes[fixing].fixing_index = fixing;
    }
    return 0;
}

static int bind_affine(
    const asian_affine_family_engine_t *engine,
    const float *x, const float *growth, uint32_t future, uint32_t total,
    asian_meta_affine_route_t *routes)
{
    if (engine == NULL || engine->magic != ASIAN_AFFINE_FAMILY_ENGINE_MAGIC ||
        engine->affine_plan == NULL || growth == NULL || routes == NULL ||
        future < 1u || future > ASIAN_META_DIRECTIONS || total < future)
        return -1;
    if (future >= 2u) {
        if (asian_meta_affine_routes_bind(engine->affine_plan, x, growth,
                                          future, routes) != 0)
            return -1;
    } else {
        const size_t offset =
            (size_t)engine->affine_plan->donor_region[0] *
            ASIAN_AFFINE_FAMILY_PATHS;
        routes[0].x_base = x == NULL ? NULL : x + offset;
        routes[0].growth_base = growth + offset;
        routes[0].map = &engine->affine_plan->contexts[0];
        routes[0].fixing_index = 0u;
    }
    for (uint32_t fixing = 0; fixing < future; ++fixing) {
        const float weight = (float)(future - fixing) / (float)total;
        memcpy(&routes[fixing].weight_bits, &weight, sizeof(weight));
    }
    return 0;
}

static int arithmetic_request_prepare_common(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_generic_oracle_t *oracle,
    const float *growth,
    const asian_affine_family_carrier_input_t *market,
    const asian_affine_family_request_input_t *input,
    enum asian_affine_family_provider provider,
    asian_affine_family_arithmetic_request_t *request)
{
    const int provider_status = valid_provider(provider, oracle);
    if (provider_status != ASIAN_AFFINE_FAMILY_OK)
        return provider_status;
    if (engine == NULL || engine->magic != ASIAN_AFFINE_FAMILY_ENGINE_MAGIC ||
        request == NULL || ((uintptr_t)request & 63u) != 0u ||
        growth == NULL || !request_matches_market(market, input))
        return ASIAN_AFFINE_FAMILY_INVALID;
    const uint32_t total = input->future_fixings + input->completed_fixings;
    int status;
    if (provider == ASIAN_AFFINE_FAMILY_GENERIC)
        status = bind_generic(oracle, NULL, growth, input->future_fixings,
                              total, request->routes.generic);
    else
        status = bind_affine(engine, NULL, growth, input->future_fixings,
                             total, request->routes.affine);
    if (status != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    uint32_t padded = 0u;
    if (asian_genuine_arithmetic_growth_only_strip_prepare_padded(
            &request->strip, input->s0, input->rate,
            input->dividend_yield, input->sigma, input->maturity,
            input->future_fixings, input->completed_fixings,
            input->initial_arithmetic_sum, input->past_log_sum,
            input->strikes, input->strike_count, &padded) != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    (void)padded;
    memset(&request->growth, 0, sizeof(request->growth));
    if (provider == ASIAN_AFFINE_FAMILY_GENERIC) {
        request->growth.generic.d1_growth =
            request->routes.generic[0].growth_base;
        request->growth.generic.routes_d2 = request->routes.generic + 1;
        request->growth.generic.q_out = request->q;
        request->growth.generic.fixing_count = input->future_fixings;
        request->growth.generic.s0 = (float)input->s0;
        request->growth.generic.magic =
            ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC;
        request->growth.generic.abi_version =
            ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION;
    } else {
        request->growth.affine.d1_growth =
            request->routes.affine[0].growth_base;
        request->growth.affine.routes_d2 = request->routes.affine + 1;
        request->growth.affine.q_out = request->q;
        request->growth.affine.fixing_count = input->future_fixings;
        request->growth.affine.s0 = (float)input->s0;
        request->growth.affine.magic =
            ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC;
        request->growth.affine.abi_version =
            ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION;
    }
    request->carrier_growth = growth;
    request->provider = provider;
    request->leaf = selected_leaf(input->strike_count, input->future_fixings);
    request->strike_count = input->strike_count;
    request->workload = input->workload;
    request->magic = ASIAN_AFFINE_FAMILY_ARITH_REQUEST_MAGIC;
    return ASIAN_AFFINE_FAMILY_OK;
}

__attribute__((noinline, used))
int asian_affine_family_arithmetic_request_prepare_growth(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_generic_oracle_t *oracle,
    const asian_affine_family_growth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_affine_family_provider provider,
    asian_affine_family_arithmetic_request_t *request)
{
    if (carrier == NULL || carrier->magic != ASIAN_AFFINE_FAMILY_GROWTH_MAGIC)
        return ASIAN_AFFINE_FAMILY_INVALID;
    return arithmetic_request_prepare_common(engine, oracle, carrier->growth,
        &carrier->market, input, provider, request);
}

__attribute__((noinline, used))
int asian_affine_family_arithmetic_request_prepare_xgrowth(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_generic_oracle_t *oracle,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_affine_family_provider provider,
    asian_affine_family_arithmetic_request_t *request)
{
    if (carrier == NULL || carrier->magic != ASIAN_AFFINE_FAMILY_XGROWTH_MAGIC)
        return ASIAN_AFFINE_FAMILY_INVALID;
    return arithmetic_request_prepare_common(engine, oracle, carrier->growth,
        &carrier->market, input, provider, request);
}

static void arithmetic_immediate(
    asian_affine_family_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output)
{
    const int delta = request->workload == ASIAN_AFFINE_FAMILY_PRICE_DELTA;
    if (request->provider == ASIAN_AFFINE_FAMILY_GENERIC) {
        const asian_genuine_arithmetic_growth_only_context_t *context =
            &request->growth.generic;
        if (request->leaf == ASIAN_AFFINE_FAMILY_LEAF1) {
            if (delta) asian_genuine_arithmetic_growth_only_price_delta_1_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
            else asian_genuine_arithmetic_growth_only_price_1_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
        } else if (request->leaf == ASIAN_AFFINE_FAMILY_LEAF2) {
            if (delta) asian_genuine_arithmetic_growth_only_price_delta_2_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
            else asian_genuine_arithmetic_growth_only_price_2_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
        } else {
            if (delta) asian_genuine_arithmetic_growth_only_price_delta_4_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
            else asian_genuine_arithmetic_growth_only_price_4_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
        }
    } else {
        const asian_meta_growth_only_context_t *context = &request->growth.affine;
        if (request->leaf == ASIAN_AFFINE_FAMILY_LEAF1) {
            if (delta) asian_meta_arithmetic_growth_only_price_delta_1_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
            else asian_meta_arithmetic_growth_only_price_1_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
        } else if (request->leaf == ASIAN_AFFINE_FAMILY_LEAF2) {
            if (delta) asian_meta_arithmetic_growth_only_price_delta_2_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
            else asian_meta_arithmetic_growth_only_price_2_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
        } else {
            if (delta) asian_meta_arithmetic_growth_only_price_delta_4_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
            else asian_meta_arithmetic_growth_only_price_4_diag(
                context, &request->strip, request->strip.strikes,
                output->values);
        }
    }
}

__attribute__((noinline, used))
int asian_affine_family_arithmetic_prepared_price(
    asian_affine_family_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_AFFINE_FAMILY_ARITH_REQUEST_MAGIC ||
        ((uintptr_t)output & 63u) != 0u)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(output, 0, sizeof(*output));
    if (request->leaf != ASIAN_AFFINE_FAMILY_MATERIALIZED) {
        arithmetic_immediate(request, output);
    } else {
        if (request->growth.generic.fixing_count == 1u) {
            const float s0 = request->growth.generic.s0;
            for (uint32_t path = 0; path < ASIAN_AFFINE_FAMILY_PATHS; ++path)
                request->q[path] = s0 * request->carrier_growth[path];
        } else if (request->provider == ASIAN_AFFINE_FAMILY_GENERIC) {
            asian_genuine_arithmetic_growth_only_q_diag(
                &request->growth.generic);
        } else {
            asian_meta_arithmetic_growth_only_q_diag(&request->growth.affine);
        }
        if (asian_genuine_arithmetic_growth_only_strip_consume_padded(
                request->q, request->q, &request->strip,
                request->strike_count,
                request->workload == ASIAN_AFFINE_FAMILY_PRICE_DELTA,
                output) != 0)
            return ASIAN_AFFINE_FAMILY_INVALID;
    }
    memset(output->values + request->strike_count, 0,
        (ASIAN_AFFINE_FAMILY_MAX_STRIKES - request->strike_count) *
        sizeof(output->values[0]));
    return ASIAN_AFFINE_FAMILY_OK;
}

__attribute__((noinline, used))
int asian_affine_family_geocv_request_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_generic_oracle_t *oracle,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_affine_family_provider provider,
    asian_affine_family_geocv_request_t *request)
{
    const int provider_status = valid_provider(provider, oracle);
    if (provider_status != ASIAN_AFFINE_FAMILY_OK)
        return provider_status;
    if (engine == NULL || engine->magic != ASIAN_AFFINE_FAMILY_ENGINE_MAGIC ||
        carrier == NULL || carrier->magic != ASIAN_AFFINE_FAMILY_XGROWTH_MAGIC ||
        request == NULL || ((uintptr_t)request & 63u) != 0u ||
        !request_matches_market(&carrier->market, input))
        return ASIAN_AFFINE_FAMILY_INVALID;
    const uint32_t total = input->future_fixings + input->completed_fixings;
    int status;
    if (provider == ASIAN_AFFINE_FAMILY_GENERIC)
        status = bind_generic(oracle, carrier->x, carrier->growth,
            input->future_fixings, total, request->routes.generic);
    else
        status = bind_affine(engine, carrier->x, carrier->growth,
            input->future_fixings, total, request->routes.affine);
    if (status != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    uint32_t padded = 0u;
    if (asian_geometric_cv_packet_local_strip_prepare_padded(
            &request->strip, input->s0, input->rate,
            input->dividend_yield, input->sigma, input->maturity,
            input->future_fixings, input->completed_fixings,
            input->initial_arithmetic_sum, input->past_log_sum,
            input->strikes, input->strike_count, &padded) != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    (void)padded;
    request->leaf = selected_leaf(input->strike_count, input->future_fixings);
    uint32_t log_bits;
    memcpy(&log_bits, &request->strip.log_base, sizeof(log_bits));
    if (provider == ASIAN_AFFINE_FAMILY_GENERIC) {
        asian_affine_family_generic_immediate_context_t *immediate =
            &request->immediate.generic;
        memset(immediate, 0, sizeof(*immediate));
        immediate->d1_x = request->routes.generic[0].x_base;
        immediate->d1_growth = request->routes.generic[0].growth_base;
        immediate->routes_d2 = request->routes.generic + 1;
        immediate->fixing_count = input->future_fixings;
        immediate->s0 = (float)input->s0;
        immediate->d1_weight_bits = request->routes.generic[0].weight_bits;
        immediate->terminal_log_base_bits = log_bits;
        immediate->magic = GENERIC_IMMEDIATE_MAGIC;
        immediate->abi_version = 1u;
        asian_affine_family_generic_packet_context_t *packet =
            &request->packet.generic;
        memset(packet, 0, sizeof(*packet));
        packet->d1_x = immediate->d1_x;
        packet->d1_growth = immediate->d1_growth;
        packet->routes_d2 = immediate->routes_d2;
        packet->q_out = request->q;
        packet->g_out = request->g;
        packet->fixing_count = input->future_fixings;
        packet->s0 = immediate->s0;
        packet->d1_weight_bits = immediate->d1_weight_bits;
        packet->terminal_log_base_bits = log_bits;
        packet->magic = GENERIC_PACKET_MAGIC;
        packet->abi_version = 1u;
    } else {
        asian_geometric_cv_immediate_context_t *immediate =
            &request->immediate.affine;
        memset(immediate, 0, sizeof(*immediate));
        immediate->d1_x = request->routes.affine[0].x_base;
        immediate->d1_growth = request->routes.affine[0].growth_base;
        immediate->routes_d2 = request->routes.affine + 1;
        immediate->fixing_count = input->future_fixings;
        immediate->s0 = (float)input->s0;
        immediate->d1_weight_bits = request->routes.affine[0].weight_bits;
        immediate->terminal_log_base_bits = log_bits;
        immediate->magic = ASIAN_GEOMETRIC_CV_IMMEDIATE_MAGIC;
        immediate->abi_version = ASIAN_GEOMETRIC_CV_IMMEDIATE_ABI_VERSION;
        asian_geometric_cv_packet_local_context_t *packet =
            &request->packet.affine;
        memset(packet, 0, sizeof(*packet));
        packet->d1_x = immediate->d1_x;
        packet->d1_growth = immediate->d1_growth;
        packet->routes_d2 = immediate->routes_d2;
        packet->q_out = request->q;
        packet->g_out = request->g;
        packet->fixing_count = input->future_fixings;
        packet->s0 = immediate->s0;
        packet->d1_weight_bits = immediate->d1_weight_bits;
        packet->terminal_log_base_bits = log_bits;
        packet->magic = ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAGIC;
        packet->abi_version = ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ABI_VERSION;
    }
    request->carrier = carrier;
    request->provider = provider;
    request->strike_count = input->strike_count;
    request->workload = input->workload;
    request->magic = ASIAN_AFFINE_FAMILY_GEOCV_REQUEST_MAGIC;
    return ASIAN_AFFINE_FAMILY_OK;
}

#define GENERIC_INVOKE(mode, width) \
__attribute__((noinline, used)) \
void asian_affine_family_generic_geocv_invoke_##mode##_##width( \
    const asian_affine_family_generic_immediate_context_t *context, \
    const asian_genuine_strip_context_t *strip, \
    const asian_genuine_strip_strike_t *strikes, \
    asian_genuine_strip_output_t *output) \
{ \
    __asm__ volatile( \
        "call asian_affine_family_generic_geocv_" #mode "_" #width "_diag" \
        : "+D"(context), "+S"(strip), "+d"(strikes), "+c"(output) \
        : \
        : "rax", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", \
          "k1", "k2", \
          "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", \
          "zmm7", "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13", \
          "zmm14", "zmm15", "zmm16", "zmm17", "zmm18", "zmm19", \
          "zmm20", "zmm21", "zmm22", "zmm23", "zmm24", "zmm25", \
          "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31", \
          "cc", "memory"); \
}

GENERIC_INVOKE(price, 1)
GENERIC_INVOKE(price_delta, 1)
GENERIC_INVOKE(price, 2)
GENERIC_INVOKE(price_delta, 2)
GENERIC_INVOKE(price, 4)
GENERIC_INVOKE(price_delta, 4)
#undef GENERIC_INVOKE

static void geocv_immediate(
    asian_affine_family_geocv_request_t *request,
    asian_genuine_strip_output_t *output)
{
    const int delta = request->workload == ASIAN_AFFINE_FAMILY_PRICE_DELTA;
    if (request->provider == ASIAN_AFFINE_FAMILY_GENERIC) {
        const asian_affine_family_generic_immediate_context_t *context =
            &request->immediate.generic;
        if (request->leaf == ASIAN_AFFINE_FAMILY_LEAF1) {
            if (delta) asian_affine_family_generic_geocv_invoke_price_delta_1(
                context, &request->strip, request->strip.strikes, output);
            else asian_affine_family_generic_geocv_invoke_price_1(
                context, &request->strip, request->strip.strikes, output);
        } else if (request->leaf == ASIAN_AFFINE_FAMILY_LEAF2) {
            if (delta) asian_affine_family_generic_geocv_invoke_price_delta_2(
                context, &request->strip, request->strip.strikes, output);
            else asian_affine_family_generic_geocv_invoke_price_2(
                context, &request->strip, request->strip.strikes, output);
        } else {
            if (delta) asian_affine_family_generic_geocv_invoke_price_delta_4(
                context, &request->strip, request->strip.strikes, output);
            else asian_affine_family_generic_geocv_invoke_price_4(
                context, &request->strip, request->strip.strikes, output);
        }
    } else {
        const asian_geometric_cv_immediate_context_t *context =
            &request->immediate.affine;
        if (request->leaf == ASIAN_AFFINE_FAMILY_LEAF1) {
            if (delta) asian_geometric_cv_immediate_invoke_price_delta_1(
                context, &request->strip, request->strip.strikes, output);
            else asian_geometric_cv_immediate_invoke_price_1(
                context, &request->strip, request->strip.strikes, output);
        } else if (request->leaf == ASIAN_AFFINE_FAMILY_LEAF2) {
            if (delta) asian_geometric_cv_immediate_invoke_price_delta_2(
                context, &request->strip, request->strip.strikes, output);
            else asian_geometric_cv_immediate_invoke_price_2(
                context, &request->strip, request->strip.strikes, output);
        } else {
            if (delta) asian_geometric_cv_immediate_invoke_price_delta_4(
                context, &request->strip, request->strip.strikes, output);
            else asian_geometric_cv_immediate_invoke_price_4(
                context, &request->strip, request->strip.strikes, output);
        }
    }
}

__attribute__((noinline, used))
int asian_affine_family_geocv_prepared_price(
    asian_affine_family_geocv_request_t *request,
    asian_genuine_strip_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_AFFINE_FAMILY_GEOCV_REQUEST_MAGIC ||
        request->carrier == NULL ||
        request->carrier->magic != ASIAN_AFFINE_FAMILY_XGROWTH_MAGIC ||
        ((uintptr_t)output & 63u) != 0u)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(output, 0, sizeof(*output));
    if (request->leaf != ASIAN_AFFINE_FAMILY_MATERIALIZED) {
        geocv_immediate(request, output);
    } else {
        const uint32_t future = request->immediate.generic.fixing_count;
        if (future == 1u) {
            float weight;
            memcpy(&weight, &request->immediate.generic.d1_weight_bits,
                   sizeof(weight));
            const float s0 = request->immediate.generic.s0;
            for (uint32_t path = 0; path < ASIAN_AFFINE_FAMILY_PATHS; ++path) {
                request->q[path] = s0 * request->carrier->growth[path];
                request->g[path] = fmaf(weight, request->carrier->x[path],
                                        0.0f);
            }
            asian_genuine_strip_l_to_g_diag(request->g, &request->strip,
                                             request->g);
        } else if (request->provider == ASIAN_AFFINE_FAMILY_GENERIC) {
            asian_affine_family_generic_packet_qg_diag(
                &request->packet.generic);
        } else {
            asian_geometric_cv_packet_local_qg_diag(&request->packet.affine);
        }
        if (asian_geometric_cv_packet_local_strip_consume_padded(
                request->q, request->g, &request->strip,
                request->strike_count,
                request->workload == ASIAN_AFFINE_FAMILY_PRICE_DELTA,
                output) != 0)
            return ASIAN_AFFINE_FAMILY_INVALID;
    }
    memset(output->values + request->strike_count, 0,
        (ASIAN_AFFINE_FAMILY_MAX_STRIKES - request->strike_count) *
        sizeof(output->values[0]));
    return ASIAN_AFFINE_FAMILY_OK;
}
