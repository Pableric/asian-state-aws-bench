#include "virtual_block.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *msg) {
    fprintf(stderr, "FAIL %s\n", msg);
    return 1;
}

static int expect_reject(uint32_t total, uint32_t tile, uint32_t index, uint32_t dim) {
    vb_map_t *map = aligned_alloc(64, sizeof(*map));
    vb_tile_info_t info;
    uint32_t donor[VB_MAX_PATHS];
    int rc;
    if (map == NULL) {
        return fail("alloc");
    }
    rc = vb_prepare(total, tile, index, dim, map, &info, donor);
    free(map);
    return rc == -1 ? 0 : fail("expected reject");
}

static int cmp_u32(const void *a, const void *b) {
    const uint32_t x = *(const uint32_t *)a;
    const uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int unique_count(const uint32_t *words, uint32_t n) {
    uint32_t tmp[VB_MAX_PATHS];
    uint32_t i;
    uint32_t uniq = 1;
    memcpy(tmp, words, n * sizeof(uint32_t));
    qsort(tmp, n, sizeof(uint32_t), cmp_u32);
    for (i = 1; i < n; ++i) {
        if (tmp[i] == tmp[i - 1u]) {
            return (int)uniq;
        }
        uniq += 1u;
    }
    return (int)uniq;
}

static int has_short_period(const uint32_t *words, uint32_t n) {
    uint32_t p;
    for (p = 1; p < n; ++p) {
        uint32_t k;
        if ((n % p) != 0u) {
            continue;
        }
        for (k = p; k < n; ++k) {
            if (words[k] != words[k - p]) {
                break;
            }
        }
        if (k == n) {
            return 1;
        }
    }
    return 0;
}

static int reconstruct_ok(
    uint32_t total,
    uint32_t tile,
    uint32_t tile_index,
    uint32_t dim,
    const uint32_t *got
) {
    uint32_t V[32];
    uint32_t i;
    const uint32_t dest_base =
        (uint32_t)VB_POINT_BASE + tile_index * tile;
    (void)total;
    if (vb_directions(dim, V) != 0) {
        return 0;
    }
    for (i = 0; i < tile; ++i) {
        const uint32_t want = vb_sobol_word(dest_base + i, V);
        if (got[i] != want || vb_gauss_bits(got[i]) != vb_gauss_bits(want)) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    static const uint32_t sizes[] = {256u, 512u, 1024u, 2048u, 4096u};
    vb_map_t *map = aligned_alloc(64, sizeof(*map));
    uint32_t *donor = aligned_alloc(64, VB_MAX_PATHS * 4u);
    uint32_t *got = aligned_alloc(64, VB_MAX_PATHS * 4u);
    uint32_t *full[VB_MAX_DIM];
    uint32_t si;
    uint32_t dim;
    uint32_t in_block_4096 = 0;
    uint32_t next_block_4096 = 0;

    if (map == NULL || donor == NULL || got == NULL) {
        return fail("alloc");
    }
    for (dim = 0; dim < (uint32_t)VB_MAX_DIM; ++dim) {
        full[dim] = aligned_alloc(64, VB_MAX_PATHS * 4u);
        if (full[dim] == NULL) {
            return fail("alloc full");
        }
    }

    if (expect_reject(256, 512, 0, 1) != 0) {
        return 1;
    }
    if (expect_reject(4096, 768, 0, 1) != 0) {
        return 1;
    }
    if (expect_reject(4096, 256, 16, 1) != 0) {
        return 1;
    }
    if (expect_reject(4096, 4096, 0, 0) != 0) {
        return 1;
    }
    if (expect_reject(4096, 4096, 0, 257) != 0) {
        return 1;
    }

    for (dim = 1; dim <= (uint32_t)VB_MAX_DIM; ++dim) {
        vb_tile_info_t info;
        if (vb_prepare(4096, 4096, 0, dim, map, &info, donor) != 0) {
            return fail("prepare 4096");
        }
        vb_apply(map, donor, 4096, full[dim - 1u]);
        if (!reconstruct_ok(4096, 4096, 0, dim, full[dim - 1u])) {
            return fail("word/gauss 4096");
        }
        if (unique_count(full[dim - 1u], 4096) != 4096) {
            return fail("unique 4096");
        }
        if (has_short_period(full[dim - 1u], 4096)) {
            return fail("period 4096");
        }
        if (info.donor_count != 4096u || !info.donor_closed ||
            !info.two_region_valid || info.references_full_universe) {
            return fail("info 4096");
        }
        if (info.donor_base == (uint32_t)VB_POINT_BASE) {
            in_block_4096 += 1u;
        } else if (info.donor_base == (uint32_t)VB_POINT_BASE + 4096u) {
            next_block_4096 += 1u;
        } else {
            return fail("4096 donor base");
        }
    }
    if (in_block_4096 != 130u || next_block_4096 != 126u) {
        fprintf(stderr, "block split %u %u\n", in_block_4096, next_block_4096);
        return fail("4096 block split");
    }

    for (si = 0; si < 5u; ++si) {
        const uint32_t total = sizes[si];
        uint32_t ti;
        for (ti = 0; ti < 5u; ++ti) {
            const uint32_t tile = sizes[ti];
            uint32_t ntiles;
            uint32_t t;
            if (!vb_valid_pair(total, tile)) {
                if (vb_prepare(total, tile, 0, 1, map, &(vb_tile_info_t){0}, donor) != -1) {
                    return fail("invalid pair accepted");
                }
                continue;
            }
            ntiles = vb_tile_count(total, tile);
            for (dim = 1; dim <= (uint32_t)VB_MAX_DIM; ++dim) {
                uint32_t concat[VB_MAX_PATHS];
                uint32_t filled = 0;
                vb_tile_info_t info;
                for (t = 0; t < ntiles; ++t) {
                    if (vb_prepare(total, tile, t, dim, map, &info, donor) != 0) {
                        fprintf(stderr, "prepare T=%u tile=%u dim=%u t=%u\n",
                                total, tile, dim, t);
                        return fail("prepare");
                    }
                    if (info.donor_count != tile || !info.donor_closed ||
                        info.donor_word_bytes != tile * 4u ||
                        info.donor_x_growth_bytes != tile * 8u ||
                        info.working_set_bytes !=
                            (uint32_t)VB_MAP_BYTES + tile * 8u) {
                        return fail("donor accounting");
                    }
                    vb_apply(map, donor, tile, got);
                    if (!reconstruct_ok(total, tile, t, dim, got)) {
                        return fail("route word/gauss");
                    }
                    if (unique_count(got, tile) != (int)tile) {
                        return fail("unique tile");
                    }
                    if (has_short_period(got, tile)) {
                        return fail("short period");
                    }
                    memcpy(concat + filled, got, tile * 4u);
                    filled += tile;
                }
                if (filled != total) {
                    return fail("concat length");
                }
                if (total < 4096u) {
                    uint32_t i;
                    for (i = 0; i < total; ++i) {
                        if (concat[i] != full[dim - 1u][i]) {
                            return fail("prefix nesting");
                        }
                    }
                } else {
                    uint32_t i;
                    for (i = 0; i < 4096u; ++i) {
                        if (concat[i] != full[dim - 1u][i]) {
                            return fail("tile concat 4096");
                        }
                    }
                }
            }
        }
    }

    printf("virtual_block=PASS "
           "dims=1..256 totals=256,512,1024,2048,4096 "
           "4096_donor_split=130+126 "
           "two_region=kept donor_count=tile_paths\n");
    return 0;
}
