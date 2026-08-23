#define _POSIX_C_SOURCE 200112L

#include "asian_arithmetic_pricer.h"

#include "private/asian_arithmetic_pricer_test.h"
#include "private/asian_genuine_arithmetic_growth_only_strip_adapter.h"
#include "private/asian_genuine_fixed_block_source_diag.h"
#include "private/asian_genuine_permute.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    ASIAN_ARITHMETIC_PLAN_MAGIC = 0x4e4c5041u,
    ASIAN_ARITHMETIC_PREPARED_MAGIC = 0x44525041u,
    JOE_KUO_RECORD_BYTES = 33 * 4,
    SOURCE_HASH_SLOTS = 32768,
};

extern const unsigned char asian_arithmetic_joe_kuo_256_records[];

struct asian_arithmetic_route_plan {
    uint32_t magic;
    uint8_t donor_region[ASIAN_ARITHMETIC_MAX_FIXINGS];
    uint8_t reserved[60];
    fragment_map_t maps[ASIAN_ARITHMETIC_MAX_FIXINGS];
};

struct __attribute__((aligned(64))) asian_arithmetic_prepared {
    uint32_t magic;
    uint32_t strike_count;
    asian_arithmetic_workload_t workload;
    asian_arithmetic_selected_path_t selected_path;
    const asian_arithmetic_route_plan_t *plan;
    uint8_t reserved0[40];
    float growth[ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DONOR_VALUES];
    float q[ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_PATHS];
    asian_genuine_route_t routes[ASIAN_ARITHMETIC_MAX_FIXINGS];
    asian_genuine_arithmetic_fused_source_exp_context_t fused;
    asian_genuine_arithmetic_growth_only_context_t growth_context;
    asian_genuine_strip_context_t strip;
};

_Static_assert(offsetof(struct asian_arithmetic_prepared, growth) % 64u == 0u,
               "growth alignment");
_Static_assert(offsetof(struct asian_arithmetic_prepared, q) % 64u == 0u,
               "Q alignment");
_Static_assert(offsetof(struct asian_arithmetic_prepared, routes) % 32u == 0u,
               "route alignment");

static void *aligned_zero(size_t bytes)
{
    void *pointer = NULL;
    if (posix_memalign(&pointer, 64u, bytes) != 0)
        return NULL;
    memset(pointer, 0, bytes);
    return pointer;
}

static uint32_t sobol_word(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray = index ^ (index >> 1);
    uint32_t word = 0;
    for (uint32_t bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u)
            word ^= directions[bit];
    return word;
}

static int load_direction_row(uint32_t dimension, uint32_t out[32])
{
    if (dimension >= ASIAN_ARITHMETIC_MAX_FIXINGS || out == NULL)
        return -1;
    const unsigned char *record = asian_arithmetic_joe_kuo_256_records +
        (size_t)dimension * JOE_KUO_RECORD_BYTES;
    uint32_t count;
    memcpy(&count, record, sizeof(count));
    if (count != 32u)
        return -1;
    memcpy(out, record + sizeof(count), 32u * sizeof(*out));
    return 0;
}

static uint32_t source_hash_slot(uint32_t word)
{
    return (word * UINT32_C(2654435761)) & (SOURCE_HASH_SLOTS - 1u);
}

static int build_map(const uint16_t source[ASIAN_ARITHMETIC_PATHS],
                     uint32_t dimension, fragment_map_t *map)
{
    memset(map, 0, sizeof(*map));
    uint32_t pattern_count = 0;
    for (uint32_t packet = 0; packet < FRAG_BLOCK_PACKETS; ++packet) {
        for (uint32_t half = 0; half < 2u; ++half) {
            const uint32_t base = packet * 32u + half * 16u;
            const uint32_t line = source[base] / FRAG_LANES;
            uint32_t controls[FRAG_LANES];
            for (uint32_t lane = 0; lane < FRAG_LANES; ++lane) {
                if (source[base + lane] / FRAG_LANES != line)
                    return -1;
                controls[lane] = source[base + lane] & (FRAG_LANES - 1u);
            }
            uint32_t pattern = 0;
            while (pattern < pattern_count &&
                   memcmp(map->patterns[pattern], controls,
                          sizeof(controls)) != 0)
                ++pattern;
            if (pattern == pattern_count) {
                if (pattern_count == FRAG_MAX_PATTERNS)
                    return -1;
                memcpy(map->patterns[pattern_count], controls,
                       sizeof(controls));
                ++pattern_count;
            }
            map->select[packet][half] = (uint8_t)line;
            map->select[packet][2u + half] = (uint8_t)pattern;
        }
    }
    map->pattern_count = pattern_count;
    map->dimension = dimension + 1u;
    return 0;
}

int asian_arithmetic_route_plan_create(asian_arithmetic_route_plan_t **out)
{
    if (out == NULL)
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;
    *out = NULL;

    asian_arithmetic_route_plan_t *plan = aligned_zero(sizeof(*plan));
    uint32_t *source0 = aligned_zero(ASIAN_ARITHMETIC_PATHS * sizeof(*source0));
    uint32_t *source1 = aligned_zero(ASIAN_ARITHMETIC_PATHS * sizeof(*source1));
    uint32_t *keys = aligned_zero(SOURCE_HASH_SLOTS * sizeof(*keys));
    uint16_t *values = aligned_zero(SOURCE_HASH_SLOTS * sizeof(*values));
    uint8_t *used = aligned_zero(SOURCE_HASH_SLOTS * sizeof(*used));
    uint16_t *source = aligned_zero(ASIAN_ARITHMETIC_PATHS * sizeof(*source));
    if (plan == NULL || source0 == NULL || source1 == NULL || keys == NULL ||
        values == NULL || used == NULL || source == NULL) {
        free(source); free(used); free(values); free(keys);
        free(source1); free(source0); free(plan);
        return ASIAN_ARITHMETIC_PREPARATION_FAILED;
    }

    uint32_t directions[32];
    if (load_direction_row(0u, directions) != 0) {
        free(source); free(used); free(values); free(keys);
        free(source1); free(source0); free(plan);
        return ASIAN_ARITHMETIC_PREPARATION_FAILED;
    }
    for (uint32_t path = 0; path < ASIAN_ARITHMETIC_PATHS; ++path) {
        source0[path] = sobol_word(8192u + path, directions);
        source1[path] = sobol_word(12288u + path, directions);
    }

    for (uint32_t donor = 0; donor < 2u; ++donor) {
        const uint32_t *words = donor == 0u ? source0 : source1;
        for (uint32_t index = 0; index < ASIAN_ARITHMETIC_PATHS; ++index) {
            const uint32_t word = words[index];
            uint32_t slot = source_hash_slot(word);
            while (used[slot] != 0u) {
                if (keys[slot] == word) {
                    free(source); free(used); free(values); free(keys);
                    free(source1); free(source0); free(plan);
                    return ASIAN_ARITHMETIC_PREPARATION_FAILED;
                }
                slot = (slot + 1u) & (SOURCE_HASH_SLOTS - 1u);
            }
            used[slot] = 1u;
            keys[slot] = word;
            values[slot] = (uint16_t)(index | (donor << 12));
        }
    }

    for (uint32_t dimension = 0;
         dimension < ASIAN_ARITHMETIC_MAX_FIXINGS; ++dimension) {
        if (load_direction_row(dimension, directions) != 0) {
            free(source); free(used); free(values); free(keys);
            free(source1); free(source0); free(plan);
            return ASIAN_ARITHMETIC_PREPARATION_FAILED;
        }
        uint32_t donor_region = UINT32_MAX;
        for (uint32_t path = 0; path < ASIAN_ARITHMETIC_PATHS; ++path) {
            const uint32_t word = sobol_word(8192u + path, directions);
            uint32_t slot = source_hash_slot(word);
            uint32_t probes = 0;
            while (used[slot] != 0u && keys[slot] != word) {
                slot = (slot + 1u) & (SOURCE_HASH_SLOTS - 1u);
                ++probes;
            }
            if (used[slot] == 0u || probes == SOURCE_HASH_SLOTS) {
                free(source); free(used); free(values); free(keys);
                free(source1); free(source0); free(plan);
                return ASIAN_ARITHMETIC_PREPARATION_FAILED;
            }
            const uint32_t donor = values[slot] >> 12;
            if (donor_region == UINT32_MAX)
                donor_region = donor;
            else if (donor_region != donor) {
                free(source); free(used); free(values); free(keys);
                free(source1); free(source0); free(plan);
                return ASIAN_ARITHMETIC_PREPARATION_FAILED;
            }
            source[path] = values[slot] & 4095u;
        }
        if (build_map(source, dimension, &plan->maps[dimension]) != 0) {
            free(source); free(used); free(values); free(keys);
            free(source1); free(source0); free(plan);
            return ASIAN_ARITHMETIC_PREPARATION_FAILED;
        }
        plan->donor_region[dimension] = (uint8_t)donor_region;
    }

    free(source); free(used); free(values); free(keys);
    free(source1); free(source0);
    plan->magic = ASIAN_ARITHMETIC_PLAN_MAGIC;
    *out = plan;
    return ASIAN_ARITHMETIC_OK;
}

void asian_arithmetic_route_plan_destroy(asian_arithmetic_route_plan_t *plan)
{
    if (plan != NULL) {
        plan->magic = 0u;
        free(plan);
    }
}

static int valid_request(const asian_arithmetic_request_t *request)
{
    if (request == NULL || request->strikes == NULL ||
        !isfinite(request->s0) || !(request->s0 > 0.0) ||
        !isfinite(request->rate) || !isfinite(request->dividend_yield) ||
        !isfinite(request->sigma) || !(request->sigma > 0.0) ||
        !isfinite(request->maturity) || !(request->maturity > 0.0) ||
        !isfinite(request->completed_arithmetic_sum) ||
        !isfinite(request->completed_log_sum) ||
        request->future_fixings < ASIAN_ARITHMETIC_MIN_FIXINGS ||
        request->future_fixings > ASIAN_ARITHMETIC_MAX_FIXINGS ||
        request->completed_fixings >
          ASIAN_ARITHMETIC_MAX_FIXINGS - request->future_fixings ||
        request->strike_count == 0u ||
        request->strike_count > ASIAN_ARITHMETIC_MAX_STRIKES ||
        (request->workload != ASIAN_ARITHMETIC_PRICE &&
         request->workload != ASIAN_ARITHMETIC_PRICE_DELTA))
        return 0;
    for (uint32_t i = 0; i < request->strike_count; ++i)
        if (!isfinite(request->strikes[i]) || !(request->strikes[i] > 0.0f))
            return 0;
    return 1;
}

static asian_arithmetic_selected_path_t selected_path(
    uint32_t strike_count, asian_arithmetic_workload_t workload)
{
    if ((strike_count == 1u && workload == ASIAN_ARITHMETIC_PRICE) ||
        (strike_count == 3u && workload == ASIAN_ARITHMETIC_PRICE_DELTA) ||
        strike_count >= 5u)
        return ASIAN_ARITHMETIC_SELECTED_STAGE1;
    return ASIAN_ARITHMETIC_SELECTED_IMMEDIATE;
}

int asian_arithmetic_prepare(
    const asian_arithmetic_route_plan_t *plan,
    const asian_arithmetic_request_t *request,
    asian_arithmetic_prepared_t **out)
{
    if (out == NULL)
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;
    *out = NULL;
    if (plan == NULL || plan->magic != ASIAN_ARITHMETIC_PLAN_MAGIC ||
        !valid_request(request))
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;
    if (!__builtin_cpu_supports("avx512f") ||
        !__builtin_cpu_supports("avx512bw") ||
        !__builtin_cpu_supports("fma"))
        return ASIAN_ARITHMETIC_UNSUPPORTED_CONTRACT;

    asian_arithmetic_prepared_t *prepared = aligned_zero(sizeof(*prepared));
    if (prepared == NULL)
        return ASIAN_ARITHMETIC_PREPARATION_FAILED;

    prepared->plan = plan;
    prepared->strike_count = request->strike_count;
    prepared->workload = request->workload;
    prepared->selected_path = selected_path(request->strike_count,
                                             request->workload);

    asian_genuine_fixed_block_source_request_t source_request;
    memset(&source_request, 0, sizeof(source_request));
    source_request.target_start_index = ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    source_request.path_count = ASIAN_GENUINE_FIXED_BLOCK_PATHS;
    source_request.block_count = 1u;
    source_request.block_ordinal = 0u;
    source_request.fixing_count = request->future_fixings;
    source_request.s0 = request->s0;
    source_request.rate = request->rate;
    source_request.dividend_yield = request->dividend_yield;
    source_request.sigma = request->sigma;
    source_request.maturity = request->maturity;
    source_request.signed_z = asian_genuine_fixed_block_signed_z;
    source_request.signed_z_bytes = ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES;

    if (asian_genuine_arithmetic_fused_source_exp_prepare(&prepared->fused,
          &source_request, prepared->growth, sizeof(prepared->growth)) !=
          ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_OK) {
        free(prepared);
        return ASIAN_ARITHMETIC_UNSUPPORTED_CONTRACT;
    }

    for (uint32_t fixing = 0; fixing < request->future_fixings; ++fixing) {
        asian_genuine_route_t *route = &prepared->routes[fixing];
        route->x_base = NULL;
        route->growth_base = prepared->growth +
            (size_t)plan->donor_region[fixing] * ASIAN_ARITHMETIC_PATHS;
        route->map = &plan->maps[fixing];
        route->weight_bits = 0u;
        route->fixing_index = fixing;
    }

    asian_genuine_arithmetic_fused_source_exp_diag(&prepared->fused);
    if (asian_genuine_arithmetic_growth_only_prepare(&prepared->growth_context,
          prepared->routes, request->future_fixings, (float)request->s0,
          prepared->growth, sizeof(prepared->growth), prepared->q,
          sizeof(prepared->q)) != ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_OK) {
        free(prepared);
        return ASIAN_ARITHMETIC_PREPARATION_FAILED;
    }

    uint32_t padded_count;
    if (asian_genuine_arithmetic_growth_only_strip_prepare_padded(
          &prepared->strip, request->s0, request->rate,
          request->dividend_yield, request->sigma, request->maturity,
          request->future_fixings, request->completed_fixings,
          request->completed_arithmetic_sum, request->completed_log_sum,
          request->strikes, request->strike_count, &padded_count) != 0) {
        free(prepared);
        return ASIAN_ARITHMETIC_UNSUPPORTED_CONTRACT;
    }

    prepared->magic = ASIAN_ARITHMETIC_PREPARED_MAGIC;
    *out = prepared;
    return ASIAN_ARITHMETIC_OK;
}

int asian_arithmetic_price_prepared(
    asian_arithmetic_prepared_t *prepared,
    asian_arithmetic_result_t *out)
{
    if (prepared == NULL || out == NULL ||
        prepared->magic != ASIAN_ARITHMETIC_PREPARED_MAGIC ||
        prepared->plan == NULL ||
        prepared->plan->magic != ASIAN_ARITHMETIC_PLAN_MAGIC)
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;

    asian_genuine_strip_output_t internal __attribute__((aligned(64)));
    memset(&internal, 0, sizeof(internal));
    asian_genuine_arithmetic_fused_source_exp_diag(&prepared->fused);

    int status;
    if (prepared->selected_path == ASIAN_ARITHMETIC_SELECTED_IMMEDIATE) {
        status = asian_genuine_arithmetic_growth_only_immediate_consume(
            &prepared->growth_context, &prepared->strip,
            prepared->strike_count,
            prepared->workload == ASIAN_ARITHMETIC_PRICE_DELTA,
            1, &internal);
        if (status != ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_USED)
            return ASIAN_ARITHMETIC_EXECUTION_FAILED;
    } else {
        asian_genuine_arithmetic_growth_only_q_diag(&prepared->growth_context);
        status = asian_genuine_arithmetic_growth_only_strip_consume_padded(
            prepared->q, prepared->q, &prepared->strip,
            prepared->strike_count,
            prepared->workload == ASIAN_ARITHMETIC_PRICE_DELTA,
            &internal);
        if (status != 0)
            return ASIAN_ARITHMETIC_EXECUTION_FAILED;
    }

    memset(out, 0, sizeof(*out));
    out->strike_count = prepared->strike_count;
    out->selected_path = prepared->selected_path;
    for (uint32_t i = 0; i < prepared->strike_count; ++i) {
        out->values[i].call_price = internal.values[i].call_price;
        out->values[i].put_price = internal.values[i].put_price;
        out->values[i].call_delta = internal.values[i].call_delta;
        out->values[i].put_delta = internal.values[i].put_delta;
    }
    return ASIAN_ARITHMETIC_OK;
}

void asian_arithmetic_prepared_destroy(asian_arithmetic_prepared_t *prepared)
{
    if (prepared != NULL) {
        prepared->magic = 0u;
        free(prepared);
    }
}

int asian_arithmetic_test_views(
    asian_arithmetic_prepared_t *prepared,
    asian_genuine_arithmetic_fused_source_exp_context_t **fused,
    asian_genuine_arithmetic_growth_only_context_t **growth,
    asian_genuine_strip_context_t **strip,
    float **q)
{
    if (prepared == NULL || fused == NULL || growth == NULL || strip == NULL ||
        q == NULL || prepared->magic != ASIAN_ARITHMETIC_PREPARED_MAGIC)
        return -1;
    *fused = &prepared->fused;
    *growth = &prepared->growth_context;
    *strip = &prepared->strip;
    *q = prepared->q;
    return 0;
}
