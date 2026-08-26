#define _POSIX_C_SOURCE 200112L

#include "private/asian_variable_sobol_block_count_diag.h"

#include "private/asian_geometric_cv_diag.h"
#include "private/asian_genuine_multistrike_full_risk_hybrid_dispatch_diag.h"

#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    VARIABLE_GENERIC_MAGIC = 0x56474d50u,
    GENERIC_IMMEDIATE_MAGIC = 0x47494641u,
    GENERIC_PACKET_MAGIC = 0x47504641u,
};

static float phase1_tape_sentinel[16] __attribute__((aligned(64)));
static uint64_t phase1_counter;

void asian_affine_family_full_risk_k1_affine_call_impl_diag(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);
void asian_affine_family_full_risk_k1_affine_put_impl_diag(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);

static int same_double(double a, double b)
{
    uint64_t aa, bb;
    memcpy(&aa, &a, sizeof(aa));
    memcpy(&bb, &b, sizeof(bb));
    return aa == bb;
}

int asian_variable_block_count_valid(uint32_t block_count)
{
    return block_count == 1u || block_count == 2u || block_count == 4u ||
           block_count == 8u || block_count == 16u;
}

uint32_t asian_variable_required_donor_regions(uint32_t block_count)
{
    switch (block_count) {
    case 1u: case 2u: return 2u;
    case 4u: return 6u;
    case 8u: return 14u;
    case 16u: return 30u;
    default: return 0u;
    }
}

size_t asian_variable_carrier_bytes(uint32_t block_count,
                                    enum asian_variable_carrier_capability cap)
{
    const uint32_t regions = asian_variable_required_donor_regions(block_count);
    if (regions == 0u ||
        (cap != ASIAN_VARIABLE_GROWTH_ONLY && cap != ASIAN_VARIABLE_X_GROWTH))
        return 0u;
    const size_t payload = (size_t)regions * ASIAN_VARIABLE_PATHS_PER_BLOCK *
        sizeof(float);
    return sizeof(asian_variable_carrier_t) +
        (cap == ASIAN_VARIABLE_X_GROWTH ? 2u * payload : payload);
}

static int descriptor_and_metadata_valid(void)
{
    if (asian_variable_w_provenance.magic != ASIAN_VARIABLE_W_MAGIC ||
        asian_variable_w_provenance.abi_version != 1u ||
        asian_variable_w_provenance.column_count != 17u ||
        asian_variable_w_provenance.first_donor_block != 2u ||
        asian_variable_w_provenance.last_donor_block != 31u ||
        asian_variable_w_provenance.physical_permutation != 0u)
        return 0;
    for (uint32_t block = 0; block < ASIAN_VARIABLE_MAX_BLOCKS; ++block) {
        for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
             ++dimension) {
            const asian_variable_block_meta_t *m =
                &asian_variable_block_metadata[block][dimension];
            if (m->base >= ASIAN_META_PATHS ||
                m->donor_block < ASIAN_VARIABLE_FIRST_DONOR_BLOCK ||
                m->donor_block > ASIAN_VARIABLE_LAST_DONOR_BLOCK)
                return 0;
        }
    }
    return 1;
}

static int affine_plan_create(uint32_t block, asian_meta_affine_plan_t **out)
{
    asian_meta_affine_plan_t *plan = NULL;
    if (out == NULL || block >= ASIAN_VARIABLE_MAX_BLOCKS ||
        posix_memalign((void **)&plan, 64u, sizeof(*plan)) != 0)
        return -1;
    memset(plan, 0, sizeof(*plan));
    for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
         ++dimension) {
        asian_meta_direction_descriptor_t descriptor =
            asian_meta_direction_descriptors[dimension];
        const asian_variable_block_meta_t *meta =
            &asian_variable_block_metadata[block][dimension];
        descriptor.base = meta->base;
        descriptor.donor_region = 0u;
        if (asian_meta_affine_context_build(
                &descriptor, &plan->contexts[dimension]) != 0) {
            free(plan);
            return -1;
        }
        plan->donor_region[dimension] =
            (uint8_t)(meta->donor_block - ASIAN_VARIABLE_FIRST_DONOR_BLOCK);
    }
    plan->magic = ASIAN_META_PLAN_MAGIC;
    plan->abi_version = ASIAN_META_DESCRIPTOR_ABI_VERSION;
    plan->dimension_count = ASIAN_META_DIRECTIONS;
    plan->descriptor_bytes = ASIAN_META_DESCRIPTOR_SET_BYTES;
    plan->packet_steps = ASIAN_META_PACKETS;
    *out = plan;
    return 0;
}

static int insert_pattern(fragment_map_t *map, const uint32_t control[16],
                          uint8_t *index)
{
    for (uint32_t pattern = 0; pattern < map->pattern_count; ++pattern) {
        if (memcmp(map->patterns[pattern], control,
                   sizeof(map->patterns[pattern])) == 0) {
            *index = (uint8_t)pattern;
            return 0;
        }
    }
    if (map->pattern_count == FRAG_MAX_PATTERNS)
        return -1;
    memcpy(map->patterns[map->pattern_count], control,
           sizeof(map->patterns[map->pattern_count]));
    *index = (uint8_t)map->pattern_count++;
    return 0;
}

static int generic_plan_create(const asian_meta_affine_plan_t *affine,
                               asian_variable_generic_plan_t **out)
{
    asian_variable_generic_plan_t *plan = NULL;
    if (affine == NULL || out == NULL ||
        posix_memalign((void **)&plan, 64u, sizeof(*plan)) != 0)
        return -1;
    memset(plan, 0, sizeof(*plan));
    for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
         ++dimension) {
        const asian_meta_dim_affine_ctx_t *ctx =
            &affine->contexts[dimension];
        fragment_map_t *map = &plan->maps[dimension];
        map->dimension = dimension + 1u;
        for (uint32_t packet = 0; packet < ASIAN_META_PACKETS; ++packet) {
            uint32_t low[16], high[16];
            const uint32_t offset = ctx->sel2[packet][1];
            for (uint32_t lane = 0; lane < 16u; ++lane) {
                low[lane] = offset ^ ctx->base_control[lane];
                high[lane] = low[lane] ^ ctx->half_delta[lane];
            }
            map->select[packet][0] = ctx->sel2[packet][0];
            map->select[packet][1] = ctx->sel2[packet][0] ^ 1u;
            if (insert_pattern(map, low, &map->select[packet][2]) != 0 ||
                insert_pattern(map, high, &map->select[packet][3]) != 0) {
                free(plan);
                return -1;
            }
        }
        plan->donor_region[dimension] = affine->donor_region[dimension];
    }
    plan->magic = VARIABLE_GENERIC_MAGIC;
    *out = plan;
    return 0;
}

int asian_variable_engine_create(asian_variable_engine_t *engine,
                                 int include_generic_oracle)
{
    if (engine == NULL || ((uintptr_t)engine & 63u) != 0u ||
        !descriptor_and_metadata_valid() ||
        (size_t)((const unsigned char *)asian_variable_signed_z_bank_end -
                 (const unsigned char *)asian_variable_signed_z_bank) !=
            ASIAN_VARIABLE_SIGNED_Z_BYTES)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(engine, 0, sizeof(*engine));
    for (uint32_t block = 0; block < ASIAN_VARIABLE_MAX_BLOCKS; ++block) {
        if (affine_plan_create(block, &engine->affine_plan[block]) != 0)
            goto fail;
        if (include_generic_oracle && generic_plan_create(
                engine->affine_plan[block], &engine->generic_plan[block]) != 0)
            goto fail;
    }
    float lo = asian_variable_signed_z_bank[0];
    float hi = lo;
    for (uint32_t i = 1; i < ASIAN_VARIABLE_DONOR_REGIONS *
                              ASIAN_VARIABLE_PATHS_PER_BLOCK; ++i) {
        if (asian_variable_signed_z_bank[i] < lo) lo = asian_variable_signed_z_bank[i];
        if (asian_variable_signed_z_bank[i] > hi) hi = asian_variable_signed_z_bank[i];
    }
    engine->signed_z_min = lo;
    engine->signed_z_max = hi;
    engine->generic_enabled = include_generic_oracle != 0;
    engine->magic = ASIAN_VARIABLE_ENGINE_MAGIC;
    return ASIAN_AFFINE_FAMILY_OK;
fail:
    asian_variable_engine_destroy(engine);
    return ASIAN_AFFINE_FAMILY_INVALID;
}

void asian_variable_engine_destroy(asian_variable_engine_t *engine)
{
    if (engine == NULL) return;
    for (uint32_t block = 0; block < ASIAN_VARIABLE_MAX_BLOCKS; ++block) {
        free(engine->generic_plan[block]);
        free(engine->affine_plan[block]);
    }
    memset(engine, 0, sizeof(*engine));
}

int asian_variable_carrier_create(uint32_t block_capacity,
                                  enum asian_variable_carrier_capability cap,
                                  asian_variable_carrier_t **out)
{
    const size_t bytes = asian_variable_carrier_bytes(block_capacity, cap);
    if (out == NULL || bytes == 0u) return ASIAN_AFFINE_FAMILY_INVALID;
    *out = NULL;
    asian_variable_carrier_t *carrier = NULL;
    if (posix_memalign((void **)&carrier, 64u, bytes) != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(carrier, 0, bytes);
    float *payload = (float *)(void *)(carrier + 1);
    const uint32_t regions = asian_variable_required_donor_regions(block_capacity);
    if (cap == ASIAN_VARIABLE_X_GROWTH) {
        carrier->x = payload;
        carrier->growth = payload + (size_t)regions * ASIAN_VARIABLE_PATHS_PER_BLOCK;
    } else {
        carrier->growth = payload;
    }
    carrier->block_capacity = (uint8_t)block_capacity;
    carrier->capability = (uint8_t)cap;
    *out = carrier;
    return ASIAN_AFFINE_FAMILY_OK;
}

void asian_variable_carrier_destroy(asian_variable_carrier_t *carrier)
{
    if (carrier != NULL) {
        const size_t bytes = asian_variable_carrier_bytes(
            carrier->block_capacity,
            (enum asian_variable_carrier_capability)carrier->capability);
        if (bytes != 0u) memset(carrier, 0, bytes);
        free(carrier);
    }
}

static int valid_market(const asian_affine_family_carrier_input_t *input)
{
    return input != NULL && isfinite(input->rate) &&
        isfinite(input->dividend_yield) && isfinite(input->sigma) &&
        input->sigma > 0.0 && isfinite(input->maturity) &&
        input->maturity > 0.0 && input->future_fixings >= 1u &&
        input->future_fixings <= ASIAN_META_DIRECTIONS;
}

int asian_variable_carrier_prepare(const asian_variable_engine_t *engine,
                                   uint32_t block_count,
                                   const asian_affine_family_carrier_input_t *input,
                                   asian_variable_carrier_t *carrier)
{
    if (engine == NULL || engine->magic != ASIAN_VARIABLE_ENGINE_MAGIC ||
        carrier == NULL || !valid_market(input) ||
        !asian_variable_block_count_valid(block_count) ||
        block_count > carrier->block_capacity)
        return ASIAN_AFFINE_FAMILY_INVALID;
    const uint32_t regions = asian_variable_required_donor_regions(block_count);
    const double dt = input->maturity / (double)input->future_fixings;
    const float drift = (float)((input->rate - input->dividend_yield -
        0.5 * input->sigma * input->sigma) * dt);
    const float diffusion = (float)(input->sigma * sqrt(dt));
    const float lo = fmaf(diffusion, engine->signed_z_min, drift);
    const float hi = fmaf(diffusion, engine->signed_z_max, drift);
    if (!isfinite(drift) || !isfinite(diffusion) || diffusion <= 0.0f ||
        lo < -87.0f || hi > 88.0f)
        return ASIAN_AFFINE_FAMILY_INVALID;
    for (uint32_t region = 0; region < regions; region += 2u) {
        const size_t offset = (size_t)region * ASIAN_VARIABLE_PATHS_PER_BLOCK;
        if (carrier->capability == ASIAN_VARIABLE_GROWTH_ONLY) {
            asian_genuine_arithmetic_fused_source_exp_context_t ctx
                __attribute__((aligned(64)));
            memset(&ctx, 0, sizeof(ctx));
            ctx.signed_z = asian_variable_signed_z_bank + offset;
            ctx.growth_out = carrier->growth + offset;
            ctx.drift = drift; ctx.diffusion = diffusion;
            ctx.fixing_count = input->future_fixings;
            ctx.path_count = ASIAN_VARIABLE_PATHS_PER_BLOCK;
            ctx.region_count = 2u;
            ctx.values_per_region = ASIAN_VARIABLE_PATHS_PER_BLOCK;
            ctx.first_index = ASIAN_VARIABLE_FIRST_INDEX + (uint32_t)offset;
            ctx.total_values = 2u * ASIAN_VARIABLE_PATHS_PER_BLOCK;
            ctx.magic = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC;
            ctx.abi_version =
                ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION;
            asian_genuine_arithmetic_fused_source_exp_diag(&ctx);
        } else if (carrier->capability == ASIAN_VARIABLE_X_GROWTH) {
            asian_genuine_fixed_block_source_context_t source
                __attribute__((aligned(64)));
            memset(&source, 0, sizeof(source));
            source.signed_z = asian_variable_signed_z_bank + offset;
            source.drift = drift; source.diffusion = diffusion;
            source.magic = ASIAN_GENUINE_FIXED_BLOCK_SOURCE_MAGIC;
            source.abi_version = ASIAN_GENUINE_FIXED_BLOCK_ABI_VERSION;
            asian_genuine_fixed_block_signed_z_one_fma_source_diag(
                &source, carrier->x + offset);
            asian_vector_exp_range_reduced_array_diag(
                carrier->x + offset, carrier->growth + offset);
            asian_vector_exp_range_reduced_array_diag(
                carrier->x + offset + ASIAN_VARIABLE_PATHS_PER_BLOCK,
                carrier->growth + offset + ASIAN_VARIABLE_PATHS_PER_BLOCK);
        } else {
            return ASIAN_AFFINE_FAMILY_INVALID;
        }
    }
    carrier->market = *input;
    carrier->prepared_block_count = (uint8_t)block_count;
    carrier->donor_region_count = (uint8_t)regions;
    carrier->magic = ASIAN_VARIABLE_CARRIER_MAGIC;
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
    if (count == 1u) return ASIAN_AFFINE_FAMILY_LEAF1;
    if (count == 2u) return ASIAN_AFFINE_FAMILY_LEAF2;
    return ASIAN_AFFINE_FAMILY_LEAF4;
}

static int bind_affine_block(const asian_meta_affine_plan_t *plan,
                             const float *x, const float *growth,
                             uint32_t future, uint32_t total,
                             asian_meta_affine_route_t *routes)
{
    if (plan == NULL || growth == NULL || routes == NULL || future < 1u ||
        future > ASIAN_META_DIRECTIONS || total < future)
        return -1;
    for (uint32_t fixing = 0; fixing < future; ++fixing) {
        const size_t offset = (size_t)plan->donor_region[fixing] *
            ASIAN_VARIABLE_PATHS_PER_BLOCK;
        routes[fixing].x_base = x == NULL ? NULL : x + offset;
        routes[fixing].growth_base = growth + offset;
        routes[fixing].map = &plan->contexts[fixing];
        const float weight = (float)(future - fixing) / (float)total;
        memcpy(&routes[fixing].weight_bits, &weight, sizeof(weight));
        routes[fixing].fixing_index = fixing;
    }
    return 0;
}

static int bind_generic_block(const asian_variable_generic_plan_t *plan,
                              const float *x, const float *growth,
                              uint32_t future, uint32_t total,
                              asian_genuine_route_t *routes)
{
    if (plan == NULL || plan->magic != VARIABLE_GENERIC_MAGIC ||
        growth == NULL || routes == NULL || future < 1u ||
        future > ASIAN_META_DIRECTIONS || total < future)
        return -1;
    for (uint32_t fixing = 0; fixing < future; ++fixing) {
        const size_t offset = (size_t)plan->donor_region[fixing] *
            ASIAN_VARIABLE_PATHS_PER_BLOCK;
        routes[fixing].x_base = x == NULL ? NULL : x + offset;
        routes[fixing].growth_base = growth + offset;
        routes[fixing].map = &plan->maps[fixing];
        const float weight = (float)(future - fixing) / (float)total;
        memcpy(&routes[fixing].weight_bits, &weight, sizeof(weight));
        routes[fixing].fixing_index = fixing;
    }
    return 0;
}

static int prepare_arithmetic_block(
    const asian_variable_engine_t *engine,
    const asian_variable_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_affine_family_provider provider, uint32_t block,
    const asian_genuine_strip_context_t *strip,
    asian_affine_family_arithmetic_request_t *request)
{
    const uint32_t total = input->future_fixings + input->completed_fixings;
    int status;
    if (provider == ASIAN_AFFINE_FAMILY_AFFINE) {
        status = bind_affine_block(engine->affine_plan[block], NULL,
            carrier->growth, input->future_fixings, total,
            request->routes.affine);
    } else {
        status = bind_generic_block(engine->generic_plan[block], NULL,
            carrier->growth, input->future_fixings, total,
            request->routes.generic);
    }
    if (status != 0) return -1;
    request->strip = *strip;
    memset(&request->growth, 0, sizeof(request->growth));
    if (provider == ASIAN_AFFINE_FAMILY_AFFINE) {
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
    } else {
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
    }
    request->carrier_growth = carrier->growth;
    request->provider = provider;
    request->leaf = selected_leaf(input->strike_count, input->future_fixings);
    request->strike_count = input->strike_count;
    request->workload = input->workload;
    request->magic = ASIAN_AFFINE_FAMILY_ARITH_REQUEST_MAGIC;
    return 0;
}

static int prepare_geocv_block(
    const asian_variable_engine_t *engine,
    const asian_variable_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_affine_family_provider provider, uint32_t block,
    const asian_genuine_strip_context_t *strip,
    asian_affine_family_geocv_request_t *request)
{
    const uint32_t total = input->future_fixings + input->completed_fixings;
    int status;
    if (provider == ASIAN_AFFINE_FAMILY_AFFINE) {
        status = bind_affine_block(engine->affine_plan[block], carrier->x,
            carrier->growth, input->future_fixings, total,
            request->routes.affine);
    } else {
        status = bind_generic_block(engine->generic_plan[block], carrier->x,
            carrier->growth, input->future_fixings, total,
            request->routes.generic);
    }
    if (status != 0) return -1;
    request->strip = *strip;
    request->leaf = selected_leaf(input->strike_count, input->future_fixings);
    uint32_t log_bits;
    memcpy(&log_bits, &request->strip.log_base, sizeof(log_bits));
    if (provider == ASIAN_AFFINE_FAMILY_AFFINE) {
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
    } else {
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
    }
    request->carrier = NULL;
    request->provider = provider;
    request->strike_count = input->strike_count;
    request->workload = input->workload;
    request->magic = ASIAN_AFFINE_FAMILY_GEOCV_REQUEST_MAGIC;
    return 0;
}

int asian_variable_strip_request_prepare(
    const asian_variable_engine_t *engine,
    const asian_variable_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_variable_family family,
    enum asian_affine_family_provider provider,
    asian_variable_strip_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_VARIABLE_ENGINE_MAGIC ||
        carrier == NULL || carrier->magic != ASIAN_VARIABLE_CARRIER_MAGIC ||
        request == NULL || ((uintptr_t)request & 63u) != 0u ||
        !request_matches_market(&carrier->market, input) ||
        (provider != ASIAN_AFFINE_FAMILY_AFFINE &&
         provider != ASIAN_AFFINE_FAMILY_GENERIC) ||
        (provider == ASIAN_AFFINE_FAMILY_GENERIC && !engine->generic_enabled) ||
        (family != ASIAN_VARIABLE_ARITHMETIC && family != ASIAN_VARIABLE_GEOCV) ||
        (family == ASIAN_VARIABLE_GEOCV &&
         carrier->capability != ASIAN_VARIABLE_X_GROWTH))
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(request, 0, sizeof(*request));
    asian_genuine_strip_context_t strip __attribute__((aligned(64)));
    uint32_t padded = 0u;
    int status;
    if (family == ASIAN_VARIABLE_ARITHMETIC) {
        status = asian_genuine_arithmetic_growth_only_strip_prepare_padded(
            &strip, input->s0, input->rate, input->dividend_yield,
            input->sigma, input->maturity, input->future_fixings,
            input->completed_fixings, input->initial_arithmetic_sum,
            input->past_log_sum, input->strikes, input->strike_count, &padded);
    } else {
        status = asian_geometric_cv_packet_local_strip_prepare_padded(
            &strip, input->s0, input->rate, input->dividend_yield,
            input->sigma, input->maturity, input->future_fixings,
            input->completed_fixings, input->initial_arithmetic_sum,
            input->past_log_sum, input->strikes, input->strike_count, &padded);
    }
    (void)padded;
    if (status != 0) return ASIAN_AFFINE_FAMILY_INVALID;
    const uint32_t blocks = carrier->prepared_block_count;
    for (uint32_t block = 0; block < blocks; ++block) {
        status = family == ASIAN_VARIABLE_ARITHMETIC ?
            prepare_arithmetic_block(engine, carrier, input, provider, block,
                &strip, &request->block[block].arithmetic) :
            prepare_geocv_block(engine, carrier, input, provider, block,
                &strip, &request->block[block].geocv);
        if (status != 0) return ASIAN_AFFINE_FAMILY_INVALID;
    }
    request->block_count = (uint8_t)blocks;
    request->family = (uint8_t)family;
    request->provider = (uint8_t)provider;
    request->strike_count = input->strike_count;
    request->magic = ASIAN_VARIABLE_STRIP_REQUEST_MAGIC;
    return ASIAN_AFFINE_FAMILY_OK;
}

static void geocv_immediate_block(
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

static int geocv_block_price(asian_affine_family_geocv_request_t *request,
                             asian_genuine_strip_output_t *output)
{
    memset(output, 0, sizeof(*output));
    if (request->leaf != ASIAN_AFFINE_FAMILY_MATERIALIZED) {
        geocv_immediate_block(request, output);
    } else {
        const uint32_t future = request->immediate.generic.fixing_count;
        if (future == 1u) {
            float weight;
            memcpy(&weight, &request->immediate.generic.d1_weight_bits,
                   sizeof(weight));
            const float s0 = request->immediate.generic.s0;
            const float *x = request->immediate.generic.d1_x;
            const float *growth = request->immediate.generic.d1_growth;
            for (uint32_t path = 0; path < ASIAN_VARIABLE_PATHS_PER_BLOCK;
                 ++path) {
                request->q[path] = s0 * growth[path];
                request->g[path] = fmaf(weight, x[path], 0.0f);
            }
            asian_genuine_strip_l_to_g_diag(request->g, &request->strip,
                                             request->g);
        } else if (request->provider == ASIAN_AFFINE_FAMILY_GENERIC) {
            asian_affine_family_generic_packet_qg_diag(&request->packet.generic);
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

static int prepare_hot_phase1_context(
    asian_genuine_aad_phase1_context_t *out,
    const asian_meta_affine_route_t *routes,
    const asian_genuine_aad_phase1_controls_t *controls,
    const asian_affine_family_request_input_t *input)
{
    const uint32_t n = input->future_fixings;
    const double sigma = input->sigma;
    if (sigma == 0.0) return ASIAN_GENUINE_AAD_PHASE1_SIGMA_ZERO_UNSUPPORTED;
    if (!isfinite(sigma) || sigma < 0.0 || !isfinite(input->rate) ||
        !isfinite(input->dividend_yield) || !isfinite(input->maturity) ||
        input->maturity <= 0.0)
        return ASIAN_GENUINE_AAD_PHASE1_INVALID;
    const double dt = input->maturity / (double)n;
    const float drift = (float)((input->rate - input->dividend_yield -
        0.5 * sigma * sigma) * dt);
    const float diffusion = (float)(sigma * sqrt(dt));
    if (drift < ORDERED_D1_DIAG_MIN_DRIFT ||
        drift > ORDERED_D1_DIAG_MAX_DRIFT || diffusion < 0.0f ||
        diffusion > ORDERED_D1_DIAG_MAX_ALPHA)
        return ASIAN_GENUINE_AAD_PHASE1_PRODUCER_DOMAIN;
    const double b = input->maturity * ((double)n + 1.0) / (2.0 * (double)n);
    const double center = log(input->s0) + (input->rate -
        input->dividend_yield - 0.5 * sigma * sigma) * b;
    const double radius = sigma * sqrt(dt) * 6.5 * ((double)n + 1.0) * 0.5;
    if ((float)(center - radius) < -87.0f ||
        (float)(center + radius) > 88.0f)
        return ASIAN_GENUINE_AAD_PHASE1_EXP_DOMAIN;
    memset(out, 0, sizeof(*out));
    out->routes = (const asian_genuine_route_t *)(const void *)routes;
    out->s_tape = phase1_tape_sentinel;
    out->controls = controls;
    out->fixing_count = n;
    out->route_count = n - 1u;
    out->s0 = (float)input->s0;
    out->strike = input->strikes[0];
    out->inv_n = 1.0f / (float)n;
    out->dt_over_n = (float)(dt / (double)n);
    out->c = (float)(input->rate - input->dividend_yield +
                     0.5 * sigma * sigma);
    out->inv_sigma = (float)(1.0 / sigma);
    out->inv_s0 = (float)(1.0 / input->s0);
    out->discount = (float)exp(-input->rate * input->maturity);
    return ASIAN_GENUINE_AAD_PHASE1_OK;
}

int asian_variable_full_risk_k1_request_prepare(
    const asian_variable_engine_t *engine,
    const asian_variable_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_variable_full_risk_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_VARIABLE_ENGINE_MAGIC ||
        carrier == NULL || carrier->magic != ASIAN_VARIABLE_CARRIER_MAGIC ||
        carrier->capability != ASIAN_VARIABLE_X_GROWTH || request == NULL ||
        ((uintptr_t)request & 63u) != 0u ||
        !request_matches_market(&carrier->market, input) ||
        input->strike_count != 1u || input->completed_fixings != 0u ||
        input->future_fixings < ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(request, 0, sizeof(*request));
    if (asian_genuine_aad_phase1_prepare_arithmetic_controls(
            &request->controls, input->s0, input->strikes[0], input->rate,
            input->dividend_yield, input->sigma, input->maturity,
            input->future_fixings) != ASIAN_GENUINE_AAD_PHASE1_OK ||
        asian_genuine_msfr_prepare_arithmetic_strike(
            &request->parity, input->s0, input->rate,
            input->dividend_yield, input->sigma, input->maturity,
            input->future_fixings, input->strikes[0]) != ASIAN_GENUINE_MSFR_OK)
        return ASIAN_AFFINE_FAMILY_INVALID;
    for (uint32_t block = 0; block < carrier->prepared_block_count; ++block) {
        asian_affine_family_full_risk_k1_request_t *part = &request->block[block];
        if (bind_affine_block(engine->affine_plan[block], carrier->x,
                carrier->growth, input->future_fixings,
                input->future_fixings, part->routes) != 0)
            return ASIAN_AFFINE_FAMILY_INVALID;
        part->controls = request->controls;
        part->parity = request->parity;
        if (prepare_hot_phase1_context(&part->context, part->routes,
                &part->controls, input) != ASIAN_GENUINE_AAD_PHASE1_OK)
            return ASIAN_AFFINE_FAMILY_INVALID;
        part->magic = ASIAN_AFFINE_FAMILY_FULL_RISK_K1_REQUEST_MAGIC;
    }
    request->block_count = carrier->prepared_block_count;
    request->family = ASIAN_VARIABLE_FULL_RISK;
    request->direct_call = (request->parity.flags &
        ASIAN_GENUINE_MSFR_DIRECT_CALL) != 0u;
    request->strike_count = 1u;
    request->magic = ASIAN_VARIABLE_FULL_RISK_REQUEST_MAGIC;
    return ASIAN_AFFINE_FAMILY_OK;
}

int asian_variable_msfr_request_prepare(
    const asian_variable_engine_t *engine,
    const asian_variable_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_genuine_msfr_estimator estimator,
    asian_variable_msfr_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_VARIABLE_ENGINE_MAGIC ||
        !engine->generic_enabled || carrier == NULL ||
        carrier->magic != ASIAN_VARIABLE_CARRIER_MAGIC ||
        carrier->capability != ASIAN_VARIABLE_X_GROWTH || request == NULL ||
        ((uintptr_t)request & 63u) != 0u ||
        !request_matches_market(&carrier->market, input) ||
        input->completed_fixings != 0u ||
        (estimator != ASIAN_GENUINE_MSFR_ARITHMETIC &&
         estimator != ASIAN_GENUINE_MSFR_GEOMETRIC_CV))
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(request, 0, sizeof(*request));
    if (asian_genuine_msfr_prepare_basis_controls(&request->basis_controls,
            input->s0, input->rate, input->dividend_yield, input->sigma,
            input->maturity, input->future_fixings) != ASIAN_GENUINE_MSFR_OK ||
        asian_genuine_msfr_prepare_strikes(&request->strike_controls,
            input->s0, input->rate, input->dividend_yield, input->sigma,
            input->maturity, input->future_fixings, input->strikes,
            input->strike_count) != ASIAN_GENUINE_MSFR_OK ||
        asian_genuine_msfr_prepare_consumer_context(&request->consumer,
            &request->strike_controls) != ASIAN_GENUINE_MSFR_OK)
        return ASIAN_AFFINE_FAMILY_INVALID;
    if (input->strike_count == 1u &&
        asian_genuine_aad_phase1_prepare_controls(&request->phase1_controls,
            input->s0, input->strikes[0], input->rate,
            input->dividend_yield, input->sigma, input->maturity,
            input->future_fixings) != ASIAN_GENUINE_AAD_PHASE1_OK)
        return ASIAN_AFFINE_FAMILY_INVALID;
    const double dt = input->maturity / (double)input->future_fixings;
    for (uint32_t block = 0; block < carrier->prepared_block_count; ++block) {
        if (bind_generic_block(engine->generic_plan[block], carrier->x,
                carrier->growth, input->future_fixings,
                input->future_fixings, request->routes[block]) != 0)
            return ASIAN_AFFINE_FAMILY_INVALID;
        asian_genuine_msfr_basis_context_t *basis =
            &request->basis_context[block];
        memset(basis, 0, sizeof(*basis));
        basis->routes = request->routes[block];
        basis->controls = &request->basis_controls;
        basis->fixing_count = input->future_fixings;
        basis->route_count = input->future_fixings - 1u;
        basis->s0 = (float)input->s0;
        basis->inv_n = 1.0f / (float)input->future_fixings;
        basis->dt_over_n = (float)(dt / (double)input->future_fixings);
        basis->c = (float)(input->rate - input->dividend_yield +
                           0.5 * input->sigma * input->sigma);
        basis->inv_sigma = (float)(1.0 / input->sigma);
        basis->inv_s0 = (float)(1.0 / input->s0);
        if (input->strike_count == 1u) {
            asian_genuine_aad_phase1_context_t *phase =
                &request->phase1_context[block];
            memset(phase, 0, sizeof(*phase));
            phase->routes = request->routes[block];
            phase->s_tape = phase1_tape_sentinel;
            phase->controls = &request->phase1_controls;
            phase->fixing_count = input->future_fixings;
            phase->route_count = input->future_fixings - 1u;
            phase->s0 = (float)input->s0;
            phase->strike = input->strikes[0];
            phase->inv_n = 1.0f / (float)input->future_fixings;
            phase->dt_over_n = (float)(dt / (double)input->future_fixings);
            phase->c = basis->c;
            phase->inv_sigma = basis->inv_sigma;
            phase->inv_s0 = basis->inv_s0;
            phase->discount = (float)exp(-input->rate * input->maturity);
        }
    }
    request->block_count = carrier->prepared_block_count;
    request->family = ASIAN_VARIABLE_FULL_RISK;
    request->estimator = (uint8_t)estimator;
    request->strike_count = input->strike_count;
    request->magic = ASIAN_VARIABLE_MSFR_REQUEST_MAGIC;
    return ASIAN_AFFINE_FAMILY_OK;
}

__attribute__((noinline, used))
static int price_strip(asian_variable_strip_request_t *request,
                       uint32_t block_count, asian_variable_output_t *output)
{
    double sums[ASIAN_AFFINE_FAMILY_MAX_STRIKES][4] = {{0.0}};
    asian_genuine_strip_output_t temporary __attribute__((aligned(64)));
    for (uint32_t block = 0; block < block_count; ++block) {
        int status;
        if (request->family == ASIAN_VARIABLE_ARITHMETIC)
            status = asian_affine_family_arithmetic_prepared_price(
                &request->block[block].arithmetic, &temporary);
        else
            status = geocv_block_price(&request->block[block].geocv,
                                       &temporary);
        if (status != ASIAN_AFFINE_FAMILY_OK)
            return status;
        if (block_count == 1u) {
            output->value.strip = temporary;
            break;
        }
        for (uint32_t strike = 0; strike < request->strike_count; ++strike) {
            const double *value = (const double *)&temporary.values[strike];
            for (uint32_t field = 0; field < 4u; ++field)
                sums[strike][field] += value[field];
        }
    }
    if (block_count != 1u) {
        memset(&output->value.strip, 0, sizeof(output->value.strip));
        const double weight = 1.0 / (double)block_count;
        for (uint32_t strike = 0; strike < request->strike_count; ++strike) {
            double *value = (double *)&output->value.strip.values[strike];
            for (uint32_t field = 0; field < 4u; ++field)
                value[field] = sums[strike][field] * weight;
        }
    }
    output->family = request->family;
    output->strike_count = request->strike_count;
    return ASIAN_AFFINE_FAMILY_OK;
}

__attribute__((noinline, used))
static int price_full_risk(asian_variable_full_risk_request_t *request,
                           uint32_t block_count, asian_variable_output_t *output)
{
    if (block_count == 1u) {
        ++phase1_counter;
        const int status = asian_affine_family_full_risk_k1_prepared_price(
            &request->block[0], &output->value.full_risk);
        if (status != ASIAN_AFFINE_FAMILY_OK) return status;
    } else {
        double sums[ASIAN_GENUINE_MSFR_RISK_FIELDS] = {0.0,0.0,0.0,0.0};
        for (uint32_t block = 0; block < block_count; ++block) {
            asian_genuine_aad_phase1_value_t direct;
            ++phase1_counter;
            if (request->direct_call)
                asian_affine_family_full_risk_k1_affine_call_impl_diag(
                    &request->block[block].context, &direct);
            else
                asian_affine_family_full_risk_k1_affine_put_impl_diag(
                    &request->block[block].context, &direct);
            const double *value = (const double *)&direct;
            for (uint32_t field = 0; field < ASIAN_GENUINE_MSFR_RISK_FIELDS;
                 ++field)
                sums[field] += (value[field] - 0.0) * 4096.0;
        }
        const double inv_paths = 1.0 / (4096.0 * (double)block_count);
        double *call = (double *)&output->value.full_risk.call;
        double *put = (double *)&output->value.full_risk.put;
        for (uint32_t field = 0; field < ASIAN_GENUINE_MSFR_RISK_FIELDS;
             ++field) {
            const double direct = sums[field] * inv_paths;
            call[field] = direct + request->parity.call_adjust[field];
            put[field] = direct + request->parity.put_adjust[field];
        }
    }
    output->family = ASIAN_VARIABLE_FULL_RISK;
    output->strike_count = 1u;
    return ASIAN_AFFINE_FAMILY_OK;
}

__attribute__((noinline, used))
static int price_msfr(asian_variable_msfr_request_t *request,
                      uint32_t block_count, asian_variable_output_t *output)
{
    asian_genuine_msfr_accumulator_t accumulator __attribute__((aligned(64)));
    const enum asian_genuine_msfr_estimator estimator =
        (enum asian_genuine_msfr_estimator)request->estimator;
    if (asian_genuine_msfr_accumulator_init(&accumulator, &request->consumer,
            estimator) != ASIAN_GENUINE_MSFR_OK)
        return ASIAN_AFFINE_FAMILY_INVALID;
    for (uint32_t block = 0; block < block_count; ++block) {
        const asian_genuine_msfr_basis_t *basis = NULL;
        if (request->strike_count == 1u) {
            ++phase1_counter;
        } else {
            asian_genuine_msfr_basis_forward_diag(
                &request->basis_context[block], &request->basis);
            basis = &request->basis;
        }
        if (asian_genuine_msfr_hybrid_consume_block_diag(
                basis, &request->consumer, estimator,
                request->strike_count == 1u ?
                    &request->phase1_context[block] : NULL,
                &accumulator) != ASIAN_GENUINE_MSFR_OK)
            return ASIAN_AFFINE_FAMILY_INVALID;
    }
    if (asian_genuine_msfr_finalize(&request->consumer, &accumulator,
            &output->value.multi_strike_full_risk) != ASIAN_GENUINE_MSFR_OK)
        return ASIAN_AFFINE_FAMILY_INVALID;
    output->family = ASIAN_VARIABLE_FULL_RISK;
    output->strike_count = request->strike_count;
    return ASIAN_AFFINE_FAMILY_OK;
}

int asian_variable_sobol_price(const void *prepared_request,
                               uint32_t block_count,
                               asian_variable_output_t *output)
{
    if (prepared_request == NULL || output == NULL ||
        ((uintptr_t)output & 63u) != 0u ||
        !asian_variable_block_count_valid(block_count))
        return ASIAN_AFFINE_FAMILY_INVALID;
    const uint32_t magic = *(const uint32_t *)prepared_request;
    memset(output, 0, sizeof(*output));
    if (magic == ASIAN_VARIABLE_STRIP_REQUEST_MAGIC) {
        asian_variable_strip_request_t *request =
            (asian_variable_strip_request_t *)(uintptr_t)prepared_request;
        if (request->block_count != block_count)
            return ASIAN_AFFINE_FAMILY_INVALID;
        return price_strip(request, block_count, output);
    }
    if (magic == ASIAN_VARIABLE_FULL_RISK_REQUEST_MAGIC) {
        asian_variable_full_risk_request_t *request =
            (asian_variable_full_risk_request_t *)(uintptr_t)prepared_request;
        if (request->block_count != block_count)
            return ASIAN_AFFINE_FAMILY_INVALID;
        return price_full_risk(request, block_count, output);
    }
    if (magic == ASIAN_VARIABLE_MSFR_REQUEST_MAGIC) {
        asian_variable_msfr_request_t *request =
            (asian_variable_msfr_request_t *)(uintptr_t)prepared_request;
        if (request->block_count != block_count)
            return ASIAN_AFFINE_FAMILY_INVALID;
        return price_msfr(request, block_count, output);
    }
    return ASIAN_AFFINE_FAMILY_INVALID;
}

uint64_t asian_variable_phase1_invocations(void) { return phase1_counter; }
void asian_variable_phase1_invocations_reset(void) { phase1_counter = 0u; }
