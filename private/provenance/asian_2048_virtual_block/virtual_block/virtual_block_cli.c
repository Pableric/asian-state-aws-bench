#include "virtual_block.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
    fputs(
        "virtual_block_tool <command>\n"
        "  test\n"
        "  report\n"
        "  emit --total N --tile N [--tile-index I] --dim D\n"
        "\n"
        "total_paths and tile_paths are independent. Both must be one of\n"
        "256,512,1024,2048,4096 with tile_paths <= total_paths and exact\n"
        "divisibility. Dest indices are 8192 .. 8192+total_paths-1.\n",
        stderr);
}

static int cmd_report(void) {
    static const uint32_t sizes[] = {256u, 512u, 1024u, 2048u, 4096u};
    uint32_t si;
    uint32_t ti;
    vb_map_t *map = aligned_alloc(64, sizeof(*map));
    if (map == NULL) {
        return 2;
    }
    puts("total_paths tile_paths tiles donor_count donor_word_bytes "
         "donor_x_growth_bytes map_bytes working_set_bytes "
         "donor_closed full_universe_x_growth_bytes");
    for (si = 0; si < 5u; ++si) {
        for (ti = 0; ti < 5u; ++ti) {
            const uint32_t total = sizes[si];
            const uint32_t tile = sizes[ti];
            vb_tile_info_t info;
            if (!vb_valid_pair(total, tile)) {
                continue;
            }
            if (vb_prepare(total, tile, 0, 1, map, &info, NULL) != 0) {
                free(map);
                return 2;
            }
            printf("%u %u %u %u %u %u %u %u %s %u\n",
                   total, tile, vb_tile_count(total, tile),
                   info.donor_count, info.donor_word_bytes,
                   info.donor_x_growth_bytes, info.map_bytes,
                   info.working_set_bytes,
                   info.donor_closed ? "tile_closed" : "open",
                   (uint32_t)VB_UNIVERSE * 8u);
        }
    }
    fputs(
        "# Per dimension a tile is donor-closed on exactly tile_paths "
        "contiguous D1 indices.\n"
        "# D1..D256 together still touch the full 8192-point universe "
        "(64 KiB x+growth).\n"
        "# Two 2048-path tiles do not drop that shared universe to 32 KiB.\n",
        stdout);
    free(map);
    return 0;
}

static int cmd_emit(int argc, char **argv) {
    uint32_t total = 0;
    uint32_t tile = 0;
    uint32_t index = 0;
    uint32_t dim = 0;
    int i;
    vb_map_t *map;
    vb_tile_info_t info;
    uint32_t *donor;
    uint32_t *got;
    uint32_t p;
    for (i = 0; i < argc; ++i) {
        if (!strcmp(argv[i], "--total") && i + 1 < argc) {
            total = (uint32_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--tile") && i + 1 < argc) {
            tile = (uint32_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--tile-index") && i + 1 < argc) {
            index = (uint32_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--dim") && i + 1 < argc) {
            dim = (uint32_t)atoi(argv[++i]);
        } else {
            usage();
            return 2;
        }
    }
    map = aligned_alloc(64, sizeof(*map));
    donor = aligned_alloc(64, VB_MAX_PATHS * 4u);
    got = aligned_alloc(64, VB_MAX_PATHS * 4u);
    if (map == NULL || donor == NULL || got == NULL) {
        return 2;
    }
    if (vb_prepare(total, tile, index, dim, map, &info, donor) != 0) {
        fprintf(stderr, "prepare rejected\n");
        return 2;
    }
    vb_apply(map, donor, tile, got);
    printf("total=%u tile=%u tile_index=%u dim=%u packets=%u "
           "donor_count=%u donor_base=%u donor_closed=%d "
           "two_region=%d map_bytes=%u x_growth_bytes=%u "
           "working_set_bytes=%u patterns=%u\n",
           info.total_paths, info.tile_paths, info.tile_index, info.dimension,
           info.packets, info.donor_count, info.donor_base, info.donor_closed,
           info.two_region_valid, info.map_bytes, info.donor_x_growth_bytes,
           info.working_set_bytes, map->pattern_count);
    for (p = 0; p < info.packets; ++p) {
        printf("packet %u select=%u,%u,%u,%u\n", p,
               map->select[p][0], map->select[p][1],
               map->select[p][2], map->select[p][3]);
    }
    free(map);
    free(donor);
    free(got);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    if (!strcmp(argv[1], "test")) {
        fprintf(stderr, "run: make -C virtual_block test\n");
        return 2;
    }
    if (!strcmp(argv[1], "report")) {
        return cmd_report();
    }
    if (!strcmp(argv[1], "emit")) {
        return cmd_emit(argc - 2, argv + 2);
    }
    usage();
    return 2;
}
