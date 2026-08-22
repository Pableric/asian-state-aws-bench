#include "private/asian_genuine_arithmetic_growth_only_diag.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int span(const void *pointer, size_t bytes, uintptr_t *first,
                uintptr_t *last)
{
    const uintptr_t begin = (uintptr_t)pointer;
    if (pointer == NULL || bytes == 0u || bytes - 1u > UINTPTR_MAX - begin)
        return -1;
    *first = begin;
    *last = begin + bytes;
    return 0;
}

static int overlap(const void *a, size_t a_bytes, const void *b, size_t b_bytes)
{
    uintptr_t af, al, bf, bl;
    if (span(a,a_bytes,&af,&al) != 0 || span(b,b_bytes,&bf,&bl) != 0)
        return 1;
    return af < bl && bf < al;
}

static int map_source(const fragment_map_t *map, uint32_t path,
                      uint32_t *source)
{
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    const uint32_t pattern = map->select[packet][2u + half];
    if (map->pattern_count == 0u || map->pattern_count > FRAG_MAX_PATTERNS ||
        pattern >= map->pattern_count)
        return -1;
    const uint32_t control = map->patterns[pattern][lane];
    if (control >= FRAG_LANES)
        return -1;
    *source = (uint32_t)map->select[packet][half] * FRAG_LANES + control;
    return *source < FRAG_BLOCK_VALUES ? 0 : -1;
}

int asian_genuine_arithmetic_growth_only_prepare(
    asian_genuine_arithmetic_growth_only_context_t *out,
    const asian_genuine_route_t *routes, uint32_t fixing_count,
    float s0, const float *growth_donors, size_t growth_donor_bytes,
    float *q_out, size_t q_out_bytes)
{
    if (out == NULL || routes == NULL || growth_donors == NULL || q_out == NULL)
        return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID;
    if (fixing_count < ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MIN_FIXINGS ||
        fixing_count > ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAX_FIXINGS)
        return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_FIXINGS_UNSUPPORTED;
    if (((uintptr_t)out & 63u) != 0u || ((uintptr_t)routes & 31u) != 0u ||
        ((uintptr_t)growth_donors & 63u) != 0u ||
        ((uintptr_t)q_out & 63u) != 0u)
        return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ALIGNMENT;
    if (growth_donor_bytes !=
          ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DONOR_BYTES ||
        q_out_bytes != ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_Q_BYTES ||
        !isfinite(s0) || !(s0 > 0.0f))
        return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID;

    const size_t route_bytes = (size_t)fixing_count * sizeof(*routes);
    if (overlap(out,sizeof(*out),routes,route_bytes) ||
        overlap(out,sizeof(*out),growth_donors,growth_donor_bytes) ||
        overlap(out,sizeof(*out),q_out,q_out_bytes) ||
        overlap(q_out,q_out_bytes,routes,route_bytes) ||
        overlap(q_out,q_out_bytes,growth_donors,growth_donor_bytes))
        return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ALIAS;

    for (uint32_t i = 0;
         i < ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DONOR_VALUES; ++i)
        if (!isfinite(growth_donors[i]) || !(growth_donors[i] > 0.0f))
            return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DOMAIN;

    for (uint32_t fixing = 0; fixing < fixing_count; ++fixing) {
        const asian_genuine_route_t *route = &routes[fixing];
        const fragment_map_t *map = route->map;
        if (map == NULL || ((uintptr_t)map & 63u) != 0u ||
            map->dimension != fixing + 1u ||
            (route->growth_base != growth_donors &&
             route->growth_base != growth_donors + FRAG_BLOCK_VALUES) ||
            overlap(out,sizeof(*out),map,sizeof(*map)) ||
            overlap(q_out,q_out_bytes,map,sizeof(*map)))
            return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ROUTE_INVALID;
        for (uint32_t path = 0; path < FRAG_BLOCK_VALUES; ++path) {
            uint32_t source;
            if (map_source(map,path,&source) != 0 ||
                (fixing == 0u &&
                 (route->growth_base != growth_donors || source != path)))
                return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ROUTE_INVALID;
        }
    }

    /* Cold validation of the exact recurrence domain. */
    for (uint32_t path = 0; path < FRAG_BLOCK_VALUES; ++path) {
        float s = s0;
        float q = 0.0f;
        for (uint32_t fixing = 0; fixing < fixing_count; ++fixing) {
            uint32_t source;
            if (map_source(routes[fixing].map,path,&source) != 0)
                return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ROUTE_INVALID;
            s = s * routes[fixing].growth_base[source];
            q = q + s;
            if (!isfinite(s) || !(s > 0.0f) || !isfinite(q) || !(q > 0.0f))
                return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DOMAIN;
        }
    }

    asian_genuine_arithmetic_growth_only_context_t prepared;
    memset(&prepared,0,sizeof(prepared));
    prepared.d1_growth = growth_donors;
    prepared.routes_d2 = routes + 1;
    prepared.q_out = q_out;
    prepared.fixing_count = fixing_count;
    prepared.s0 = s0;
    prepared.magic = ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC;
    prepared.abi_version = ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION;
    memcpy(out,&prepared,sizeof(prepared));
    return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_OK;
}
