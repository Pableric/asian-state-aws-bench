/* Export the concrete 4096→2×2048 path transpose. No engine/kernel changes. */
#include "virtual_block.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

enum { N = 4096, D = 256, TILE = 2048 };

static uint32_t transpose[N];
static uint32_t inverse[N];
static uint8_t region[D];
static uint8_t swap_bit[D];
static uint32_t orig_word[N];
static uint32_t tiled_word[N];
static uint32_t recovered[N];

static void die(const char *msg, int dim) {
    printf("GLOBAL_2048_TRANSPOSE=NO\n");
    if (dim > 0)
        printf("first_counterexample_dimension=%d\n", dim);
    printf("%s\n", msg);
    exit(1);
}

static int write_u32_le(const char *path, const uint32_t *x, uint32_t n) {
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f)
        return -1;
    for (i = 0; i < n; ++i) {
        uint8_t b[4];
        b[0] = (uint8_t)(x[i] & 0xFFu);
        b[1] = (uint8_t)((x[i] >> 8) & 0xFFu);
        b[2] = (uint8_t)((x[i] >> 16) & 0xFFu);
        b[3] = (uint8_t)((x[i] >> 24) & 0xFFu);
        if (fwrite(b, 1, 4, f) != 4) {
            fclose(f);
            return -1;
        }
    }
    return fclose(f);
}

static uint32_t donor_offset(uint32_t global, int *rg) {
    if (global >= 8192u && global < 12288u) {
        *rg = 2;
        return global - 8192u;
    }
    if (global >= 12288u && global < 16384u) {
        *rg = 3;
        return global - 12288u;
    }
    *rg = -1;
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "results";
    char path[1024];
    uint32_t i, d, k, t;
    uint8_t seen[N];
    FILE *js;

    for (i = 0; i < N; ++i) {
        transpose[i] = i; /* tiled slot i holds original dest offset i */
        inverse[i] = i;
    }

    memset(seen, 0, sizeof seen);
    for (i = 0; i < N; ++i) {
        if (transpose[i] >= N || seen[transpose[i]])
            die("transpose is not a bijection", 0);
        seen[transpose[i]] = 1;
        if (inverse[transpose[i]] != i)
            die("inverse_transpose is inconsistent", 0);
    }
    for (i = 0; i < TILE; ++i)
        if (transpose[i] >= TILE)
            die("tile0 is not original offsets [0,2048)", 0);
    for (i = TILE; i < N; ++i)
        if (transpose[i] < TILE)
            die("tile1 is not original offsets [2048,4096)", 0);

    for (d = 0; d < D; ++d) {
        uint32_t dirs[32];
        uint32_t d1[32];
        uint32_t n_low = 0, n_high = 0;
        uint8_t half0[TILE], half1[TILE];

        if (vb_directions((uint32_t)(d + 1u), dirs) != 0 ||
            vb_directions(1u, d1) != 0)
            die("direction load failed", (int)(d + 1u));

        for (k = 0; k < N; ++k) {
            uint32_t dest = 8192u + k;
            orig_word[k] = vb_sobol_word(dest, dirs);
            if (vb_sobol_word(vb_d1_index_from_word(orig_word[k]), d1) !=
                orig_word[k])
                die("D1 inverse word mismatch", (int)(d + 1u));
        }

        for (i = 0; i < N; ++i)
            tiled_word[i] = orig_word[transpose[i]];
        for (k = 0; k < N; ++k)
            recovered[k] = tiled_word[inverse[k]];
        if (memcmp(recovered, orig_word, sizeof orig_word) != 0)
            die("inverse transposition did not restore Sobol words",
                (int)(d + 1u));

        memset(half0, 0, sizeof half0);
        memset(half1, 0, sizeof half1);
        for (t = 0; t < 2u; ++t) {
            uint32_t base = t * TILE;
            int tile_rg = -1;
            n_low = 0;
            n_high = 0;
            memset(half0, 0, sizeof half0);
            memset(half1, 0, sizeof half1);
            for (i = 0; i < TILE; ++i) {
                uint32_t orig = transpose[base + i];
                uint32_t g = vb_d1_index_from_word(orig_word[orig]);
                int r = 0;
                uint32_t off = donor_offset(g, &r);
                if (r < 0)
                    die("donor outside regions 2 and 3", (int)(d + 1u));
                if (i == 0)
                    tile_rg = r;
                else if (r != tile_rg)
                    die("tile uses two donor regions", (int)(d + 1u));
                if (off < TILE) {
                    if (half0[off])
                        die("duplicate donor in H0", (int)(d + 1u));
                    half0[off] = 1;
                    n_low++;
                } else {
                    uint32_t h = off - TILE;
                    if (half1[h])
                        die("duplicate donor in H1", (int)(d + 1u));
                    half1[h] = 1;
                    n_high++;
                }
            }
            if (!((n_low == TILE && n_high == 0) ||
                  (n_low == 0 && n_high == TILE)))
                die("tile is not a single donor half", (int)(d + 1u));
            if (t == 0) {
                region[d] = (uint8_t)tile_rg;
                swap_bit[d] = (uint8_t)(n_high == TILE);
            } else {
                if (tile_rg != region[d])
                    die("tiles disagree on donor region", (int)(d + 1u));
                if (swap_bit[d] == (n_high == TILE))
                    die("both tiles used the same donor half", (int)(d + 1u));
            }
        }
    }

    for (i = 0; i < 16u; ++i) {
        uint32_t dest_base = i * 4096u;
        for (d = 0; d < D; ++d) {
            uint32_t dirs[32];
            uint32_t n_low = 0, n_high = 0;
            uint32_t donor_block = 0;
            if (vb_directions((uint32_t)(d + 1u), dirs) != 0)
                die("direction load failed on variable block", (int)(d + 1u));
            {
                uint32_t tile;
                for (tile = 0; tile < 2u; ++tile) {
                    n_low = 0;
                    n_high = 0;
                    for (k = 0; k < TILE; ++k) {
                        uint32_t orig = transpose[tile * TILE + k];
                        uint32_t g = vb_d1_index_from_word(
                            vb_sobol_word(dest_base + orig, dirs));
                        uint32_t blk = g / 4096u;
                        uint32_t off = g % 4096u;
                        if (k == 0 && tile == 0)
                            donor_block = blk;
                        else if (blk != donor_block)
                            die("variable block tile spans two donor windows",
                                (int)(d + 1u));
                        if (off < TILE)
                            n_low++;
                        else
                            n_high++;
                    }
                    if (!((n_low == TILE && n_high == 0) ||
                          (n_low == 0 && n_high == TILE))) {
                        printf("GLOBAL_2048_TRANSPOSE=NO\n");
                        printf("first_counterexample_dimension=%u\n", d + 1u);
                        printf("variable_block=%u dest_base=%u tile=%u mixed_halves low=%u high=%u\n",
                               i, dest_base, tile, n_low, n_high);
                        return 1;
                    }
                }
            }
        }
    }

    snprintf(path, sizeof path, "%s/transpose_4096_to_2x2048.bin", dir);
    if (write_u32_le(path, transpose, N))
        die("write transpose bin failed", 0);
    snprintf(path, sizeof path, "%s/inverse_transpose_4096_to_2x2048.bin", dir);
    if (write_u32_le(path, inverse, N))
        die("write inverse bin failed", 0);

    snprintf(path, sizeof path, "%s/transpose_4096_to_2x2048.json", dir);
    js = fopen(path, "w");
    if (!js)
        die("write json failed", 0);
    fputs("{\n", js);
    fputs("  \"GLOBAL_2048_TRANSPOSE\": \"YES\",\n", js);
    fputs("  \"convention\": \"tiled_slot[i] holds original dest offset transpose[i]; orig[j] = tiled[inverse_transpose[j]]\",\n", js);
    fputs("  \"note\": \"The unique linear 2048/2048 split is dest-offset bit 11, already contiguous, so transpose is identity.\",\n", js);
    fputs("  \"universe\": {\"target_paths\": 4096, \"indices\": [8192, 12287], \"dimensions\": 256, \"scrambling\": false, \"digital_shift\": false},\n", js);
    fputs("  \"generator_source\": {\n", js);
    fputs("    \"direction_numbers\": \"d1_gf2_workbench/c/joe_kuo_v_1_256.h\",\n", js);
    fputs("    \"table\": \"d1_gf2_workbench/data/new-joe-kuo-6.21201\",\n", js);
    fputs("    \"word\": \"virtual_block.c vb_sobol_word\",\n", js);
    fputs("    \"donor_index\": \"virtual_block.c vb_d1_index_from_word\",\n", js);
    fputs("    \"exporter\": \"virtual_block/export_2048_transpose.c\"\n", js);
    fputs("  },\n", js);
    fputs("  \"tile0_original_offsets\": [0, 2047],\n", js);
    fputs("  \"tile1_original_offsets\": [2048, 4095],\n", js);
    fputs("  \"H0_offsets\": [0, 2047],\n", js);
    fputs("  \"H1_offsets\": [2048, 4095],\n", js);
    fprintf(js, "  \"binary\": {\"transpose\": \"transpose_4096_to_2x2048.bin\", \"inverse\": \"inverse_transpose_4096_to_2x2048.bin\", \"dtype\": \"uint32_le\", \"count\": %d},\n", N);
    fputs("  \"transpose\": [", js);
    for (i = 0; i < N; ++i)
        fprintf(js, "%s%u", i ? "," : "", transpose[i]);
    fputs("],\n", js);
    fputs("  \"inverse_transpose\": [", js);
    for (i = 0; i < N; ++i)
        fprintf(js, "%s%u", i ? "," : "", inverse[i]);
    fputs("],\n", js);
    fputs("  \"per_dimension\": [\n", js);
    for (d = 0; d < D; ++d) {
        uint32_t region_base = region[d] == 2u ? 8192u : 12288u;
        uint32_t tile0_donor = region_base + (swap_bit[d] ? TILE : 0u);
        uint32_t tile1_donor = region_base + (swap_bit[d] ? 0u : TILE);
        fprintf(js,
                "    {\"dimension\": %u, \"donor_region\": %u, \"swap\": %u, "
                "\"tile0_donor_base\": %u, \"tile1_donor_base\": %u, "
                "\"tile0_donors\": 2048, \"tile1_donors\": 2048}%s\n",
                d + 1u, region[d], swap_bit[d], tile0_donor, tile1_donor,
                d + 1u == D ? "" : ",");
    }
    fputs("  ]\n}\n", js);
    fclose(js);

    snprintf(path, sizeof path, "%s/transpose_2048_verification.json", dir);
    js = fopen(path, "w");
    if (!js)
        die("write verification json failed", 0);
    fputs("{\n", js);
    fputs("  \"GLOBAL_2048_TRANSPOSE\": \"YES\",\n", js);
    fputs("  \"identical_transpose_all_dimensions\": true,\n", js);
    fputs("  \"tile_unique_paths\": 2048,\n", js);
    fputs("  \"bijection\": true,\n", js);
    fputs("  \"D1_D256_inverse_restores_original_words\": true,\n", js);
    fputs("  \"no_duplicate_or_omitted_path\": true,\n", js);
    fputs("  \"each_tile_donors\": 2048,\n", js);
    fputs("  \"not_both_complete_4096_regions\": true,\n", js);
    fputs("  \"variable_blocks_0_15_same_transpose\": true,\n", js);
    fputs("  \"region2_count\": ", js);
    {
        uint32_t r2 = 0, r3 = 0, sw = 0;
        for (d = 0; d < D; ++d) {
            if (region[d] == 2)
                r2++;
            else
                r3++;
            sw += swap_bit[d];
        }
        fprintf(js, "%u,\n  \"region3_count\": %u,\n  \"swap_count\": %u\n", r2, r3, sw);
    }
    fputs("}\n", js);
    fclose(js);

    printf("GLOBAL_2048_TRANSPOSE=YES\n");
    printf("transpose=identity dest_offset_bit11\n");
    printf("wrote %s/transpose_4096_to_2x2048.bin\n", dir);
    printf("wrote %s/inverse_transpose_4096_to_2x2048.bin\n", dir);
    printf("wrote %s/transpose_4096_to_2x2048.json\n", dir);
    printf("wrote %s/transpose_2048_verification.json\n", dir);
    return 0;
}
