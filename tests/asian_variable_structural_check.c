#define _POSIX_C_SOURCE 200112L

#include "private/asian_variable_sobol_block_count_diag.h"

#include <stdlib.h>
#include <string.h>

enum { JOE_KUO_RECORD_BYTES = 33 * 4 };

extern const unsigned char asian_arithmetic_joe_kuo_256_records[];

static uint32_t sobol_word(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray = index ^ (index >> 1), out = 0u;
    for (uint32_t bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u) out ^= directions[bit];
    return out;
}

static void load_row(uint32_t dimension, uint32_t directions[32])
{
    const unsigned char *record = asian_arithmetic_joe_kuo_256_records +
        (size_t)dimension * JOE_KUO_RECORD_BYTES;
    memcpy(directions, record + 4, 32u * sizeof(*directions));
}

static int compare_u32(const void *a, const void *b)
{
    const uint32_t left = *(const uint32_t *)a;
    const uint32_t right = *(const uint32_t *)b;
    return (left > right) - (left < right);
}

static uint32_t affine_local(const asian_meta_direction_descriptor_t *d,
                             uint32_t base, uint32_t path)
{
    uint32_t local = base;
    for (uint32_t bit = 0; bit < 12u; ++bit)
        if ((path & (1u << bit)) != 0u) local ^= d->column[bit];
    return local;
}

static uint32_t context_local(const asian_meta_dim_affine_ctx_t *ctx,
                              uint32_t path)
{
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    const uint32_t line = ctx->sel2[packet][0] ^ half;
    uint32_t control = ctx->sel2[packet][1] ^ ctx->base_control[lane];
    if (half != 0u) control ^= ctx->half_delta[lane];
    return line * 16u + control;
}

static uint32_t generic_local(const fragment_map_t *map, uint32_t path)
{
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    return (uint32_t)map->select[packet][half] * 16u +
        map->patterns[map->select[packet][2u + half]][lane];
}

int asian_variable_structural_check(void)
{
    asian_variable_engine_t *engine = NULL;
    if (posix_memalign((void **)&engine, 64u, sizeof(*engine)) != 0)
        return -1;
    if (asian_variable_engine_create(engine, 1) != ASIAN_AFFINE_FAMILY_OK) {
        free(engine);
        return -1;
    }
    uint32_t d1[32], directions[32];
    load_row(0u, d1);
    for (uint32_t bit = 0; bit < 32u; ++bit)
        if (d1[bit] != (UINT32_C(1) << (31u - bit))) goto fail;
    if (asian_variable_w_provenance.special_w[0] != d1[0]) goto fail;
    for (uint32_t bit = 1; bit < 17u; ++bit)
        if (asian_variable_w_provenance.special_w[bit] !=
            (d1[bit] ^ d1[bit - 1])) goto fail;

    uint32_t *all_words = malloc(ASIAN_VARIABLE_MAX_BLOCKS *
        ASIAN_VARIABLE_PATHS_PER_BLOCK * sizeof(*all_words));
    if (all_words == NULL) goto fail;
    for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
         ++dimension) {
        load_row(dimension, directions);
        for (uint32_t block = 0; block < ASIAN_VARIABLE_MAX_BLOCKS; ++block) {
            uint8_t seen[ASIAN_VARIABLE_PATHS_PER_BLOCK] = {0};
            uint32_t donor_mask = 0u;
            const asian_variable_block_meta_t *meta =
                &asian_variable_block_metadata[block][dimension];
            donor_mask |= UINT32_C(1) <<
                (meta->donor_block - ASIAN_VARIABLE_FIRST_DONOR_BLOCK);
            const asian_meta_dim_affine_ctx_t *ctx =
                &engine->affine_plan[block]->contexts[dimension];
            const fragment_map_t *map =
                &engine->generic_plan[block]->maps[dimension];
            if (engine->affine_plan[block]->donor_region[dimension] !=
                    meta->donor_block - ASIAN_VARIABLE_FIRST_DONOR_BLOCK ||
                engine->generic_plan[block]->donor_region[dimension] !=
                    meta->donor_block - ASIAN_VARIABLE_FIRST_DONOR_BLOCK)
                goto fail_words;
            for (uint32_t path = 0; path < ASIAN_VARIABLE_PATHS_PER_BLOCK;
                 ++path) {
                const uint32_t target_index = ASIAN_VARIABLE_FIRST_INDEX +
                    block * ASIAN_VARIABLE_PATHS_PER_BLOCK + path;
                const uint32_t target = sobol_word(target_index, directions);
                const uint32_t local = affine_local(
                    &asian_meta_direction_descriptors[dimension], meta->base,
                    path);
                if (local >= ASIAN_VARIABLE_PATHS_PER_BLOCK || seen[local] ||
                    context_local(ctx, path) != local ||
                    generic_local(map, path) != local ||
                    sobol_word((uint32_t)meta->donor_block *
                        ASIAN_VARIABLE_PATHS_PER_BLOCK + local, d1) != target ||
                    target == 0u || target == UINT32_MAX)
                    goto fail_words;
                seen[local] = 1u;
                all_words[block * ASIAN_VARIABLE_PATHS_PER_BLOCK + path] =
                    target;
            }
            (void)donor_mask;
        }
        qsort(all_words, ASIAN_VARIABLE_MAX_BLOCKS *
            ASIAN_VARIABLE_PATHS_PER_BLOCK, sizeof(*all_words), compare_u32);
        for (uint32_t i = 1; i < ASIAN_VARIABLE_MAX_BLOCKS *
                                  ASIAN_VARIABLE_PATHS_PER_BLOCK; ++i)
            if (all_words[i - 1] == all_words[i]) goto fail_words;
    }
    for (uint32_t block = 0; block < ASIAN_VARIABLE_MAX_BLOCKS; ++block) {
        uint32_t mask = 0u;
        for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
             ++dimension)
            mask |= UINT32_C(1) <<
                (asian_variable_block_metadata[block][dimension].donor_block -
                 ASIAN_VARIABLE_FIRST_DONOR_BLOCK);
        if (mask != asian_variable_donor_masks[block]) goto fail_words;
    }
    free(all_words);
    asian_variable_engine_destroy(engine);
    free(engine);
    return 0;
fail_words:
    free(all_words);
fail:
    asian_variable_engine_destroy(engine);
    free(engine);
    return -1;
}
