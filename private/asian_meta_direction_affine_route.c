#define _POSIX_C_SOURCE 200112L

#include "asian_meta_direction_affine_route.h"

#include <stdlib.h>
#include <string.h>

enum {
    ASIAN_META_PACKET_COLUMNS = 7,
    ASIAN_META_FIRST_PACKET_COLUMN = 5,
};

static int descriptor_valid(const asian_meta_direction_descriptor_t *d)
{
    if (d == NULL || d->magic != ASIAN_META_DESCRIPTOR_MAGIC ||
        d->abi_version != ASIAN_META_DESCRIPTOR_ABI_VERSION ||
        d->donor_region > 1u || d->base >= ASIAN_META_PATHS)
        return 0;
    for (uint32_t bit = 0; bit < ASIAN_META_COLUMNS; ++bit)
        if (d->column[bit] >= ASIAN_META_PATHS)
            return 0;

    /* Lane directions cannot change the source line. */
    for (uint32_t bit = 0; bit < 4u; ++bit)
        if (d->column[bit] >= 16u)
            return 0;

    /* The second half is the adjacent source line, plus a control delta. */
    if ((d->column[4] & 16u) == 0u || (d->column[4] & ~31u) != 0u)
        return 0;
    return 1;
}

int asian_meta_affine_context_build(
    const asian_meta_direction_descriptor_t *descriptor,
    asian_meta_dim_affine_ctx_t *out)
{
    if (!descriptor_valid(descriptor) || out == NULL ||
        ((uintptr_t)out & 63u) != 0u)
        return -1;

    memset(out, 0, sizeof(*out));

    /* Normalized first-half controls depend only on path bits 0...3. */
    for (uint32_t lane = 0; lane < 16u; ++lane) {
        uint32_t control = 0u;
        for (uint32_t bit = 0; bit < 4u; ++bit)
            if ((lane & (1u << bit)) != 0u)
                control ^= descriptor->column[bit];
        out->base_control[lane] = control;
        out->half_delta[lane] = descriptor->column[4] & 15u;
    }
    out->delta = descriptor->column[4] & 15u;

    uint16_t packet_jump[ASIAN_META_PACKET_COLUMNS];
    uint16_t cumulative = 0u;
    for (uint32_t bit = 0; bit < ASIAN_META_PACKET_COLUMNS; ++bit) {
        cumulative ^= descriptor->column[ASIAN_META_FIRST_PACKET_COLUMN + bit];
        packet_jump[bit] = cumulative;
    }

    /*
     * Production construction is packet-native: exactly 128 first-half
     * states, no 4,096-entry mapping buffer and no full-path recurrence.
     */
    uint16_t packet_state = descriptor->base;
    for (uint32_t packet = 0; packet < ASIAN_META_PACKETS; ++packet) {
        out->sel2[packet][0] = (uint8_t)(packet_state >> 4);
        out->sel2[packet][1] = (uint8_t)(packet_state & 15u);
        if (packet + 1u < ASIAN_META_PACKETS)
            packet_state ^= packet_jump[__builtin_ctz(packet + 1u)];
    }
    return 0;
}

int asian_meta_affine_plan_create(asian_meta_affine_plan_t **out)
{
    if (out == NULL)
        return -1;
    *out = NULL;
    asian_meta_affine_plan_t *plan = NULL;
    if (posix_memalign((void **)&plan, 64u, sizeof(*plan)) != 0)
        return -1;
    memset(plan, 0, sizeof(*plan));

    for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
         ++dimension) {
        const asian_meta_direction_descriptor_t *descriptor =
            &asian_meta_direction_descriptors[dimension];
        if (asian_meta_affine_context_build(
                descriptor, &plan->contexts[dimension]) != 0) {
            free(plan);
            return -1;
        }
        plan->donor_region[dimension] = descriptor->donor_region;
    }
    plan->abi_version = ASIAN_META_DESCRIPTOR_ABI_VERSION;
    plan->dimension_count = ASIAN_META_DIRECTIONS;
    plan->descriptor_bytes = ASIAN_META_DESCRIPTOR_SET_BYTES;
    plan->packet_steps = ASIAN_META_PACKETS;
    plan->magic = ASIAN_META_PLAN_MAGIC;
    *out = plan;
    return 0;
}

void asian_meta_affine_plan_destroy(asian_meta_affine_plan_t *plan)
{
    if (plan != NULL) {
        plan->magic = 0u;
        free(plan);
    }
}

int asian_meta_affine_routes_bind(
    const asian_meta_affine_plan_t *plan,
    const float *x_donors, const float *growth_donors,
    uint32_t fixing_count, asian_meta_affine_route_t *routes)
{
    if (plan == NULL || plan->magic != ASIAN_META_PLAN_MAGIC ||
        plan->dimension_count != ASIAN_META_DIRECTIONS ||
        growth_donors == NULL || routes == NULL ||
        fixing_count < 2u || fixing_count > ASIAN_META_DIRECTIONS ||
        ((uintptr_t)routes & 31u) != 0u)
        return -1;
    for (uint32_t fixing = 0; fixing < fixing_count; ++fixing) {
        const size_t donor_offset =
            (size_t)plan->donor_region[fixing] * ASIAN_META_PATHS;
        routes[fixing].x_base = x_donors == NULL ? NULL :
            x_donors + donor_offset;
        routes[fixing].growth_base = growth_donors + donor_offset;
        routes[fixing].map = &plan->contexts[fixing];
        const float weight = (float)(fixing_count - fixing) /
                             (float)fixing_count;
        memcpy(&routes[fixing].weight_bits, &weight, sizeof(weight));
        routes[fixing].fixing_index = fixing;
    }
    return 0;
}
