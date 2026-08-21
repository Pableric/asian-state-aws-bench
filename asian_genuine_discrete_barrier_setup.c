#include "private/asian_genuine_discrete_barrier_diag.h"

#include <math.h>
#include <string.h>

static int d1_map_is_identity(const fragment_map_t *map)
{
    if (map == NULL || map->dimension != 1u) return 0;
    for (uint32_t packet = 0; packet < ASIAN_BARRIER_PACKETS; ++packet) {
        for (uint32_t half = 0; half < 2; ++half) {
            const uint32_t line = map->select[packet][half];
            const uint32_t pattern = map->select[packet][2u + half];
            if (pattern >= map->pattern_count) return 0;
            for (uint32_t lane = 0; lane < 16; ++lane) {
                const uint32_t source = line * 16u + map->patterns[pattern][lane];
                const uint32_t expected = packet * 32u + half * 16u + lane;
                if (source != expected) return 0;
            }
        }
    }
    return 1;
}

int asian_barrier_prepare_compact(
    const asian_genuine_route_t *qualified_routes,
    uint32_t monitoring_count,
    float initial_spot,
    float barrier,
    float strike,
    double discount,
    uint16_t *mask_table,
    asian_barrier_growth_route_t *compact_routes,
    asian_barrier_context_t *context)
{
    if (qualified_routes == NULL || context == NULL ||
        monitoring_count == 0u ||
        monitoring_count > ASIAN_BARRIER_MAX_MONITORING ||
        !isfinite(initial_spot) || initial_spot <= 0.0f ||
        !isfinite(barrier) || barrier <= 0.0f ||
        !isfinite(strike) || strike < 0.0f ||
        !isfinite(discount) || discount <= 0.0 ||
        !d1_map_is_identity(qualified_routes[0].map) ||
        qualified_routes[0].fixing_index != 0u ||
        qualified_routes[0].growth_base == NULL ||
        (monitoring_count > 1u && compact_routes == NULL)) {
        return -1;
    }

    for (uint32_t dimension = 1; dimension < monitoring_count; ++dimension) {
        const asian_genuine_route_t *source = &qualified_routes[dimension];
        if (source->growth_base == NULL || source->map == NULL ||
            source->fixing_index != dimension ||
            source->map->dimension != dimension + 1u) {
            return -1;
        }
        compact_routes[dimension - 1u].growth_base = source->growth_base;
        compact_routes[dimension - 1u].map = source->map;
    }

    memset(context, 0, sizeof(*context));
    context->routes = compact_routes;
    context->d1_growth = qualified_routes[0].growth_base;
    context->mask_table = mask_table;
    context->route_count = monitoring_count - 1u;
    context->monitoring_count = monitoring_count;
    context->initial_spot = initial_spot;
    context->barrier = barrier;
    context->strike = strike;
    context->payoff_scale = discount / ASIAN_BARRIER_PATHS;

    if (mask_table != NULL) {
        for (uint32_t i = 0; i < ASIAN_BARRIER_HALF_MASKS; ++i)
            mask_table[i] = UINT16_MAX;
    }
    return 0;
}
