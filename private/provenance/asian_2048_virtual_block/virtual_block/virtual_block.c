#include "virtual_block.h"

#include "../d1_gf2_workbench/c/joe_kuo_v_1_256.h"

#include <string.h>

int vb_valid_size(uint32_t n) {
    return n == 256u || n == 512u || n == 1024u || n == 2048u || n == 4096u;
}

int vb_valid_pair(uint32_t total_paths, uint32_t tile_paths) {
    return vb_valid_size(total_paths) && vb_valid_size(tile_paths) &&
           tile_paths <= total_paths && (total_paths % tile_paths) == 0u;
}

uint32_t vb_tile_count(uint32_t total_paths, uint32_t tile_paths) {
    if (!vb_valid_pair(total_paths, tile_paths)) {
        return 0u;
    }
    return total_paths / tile_paths;
}

uint32_t vb_sobol_word(uint32_t index, const uint32_t directions[32]) {
    uint32_t gray = index ^ (index >> 1);
    uint32_t value = 0;
    unsigned bit = 0;
    while (gray != 0u) {
        if ((gray & 1u) != 0u) {
            value ^= directions[bit];
        }
        gray >>= 1;
        ++bit;
    }
    return value;
}

uint32_t vb_d1_index_from_word(uint32_t word) {
    uint32_t gray = 0;
    for (unsigned bit = 0; bit < 32u; ++bit) {
        if ((word & (UINT32_C(1) << (31u - bit))) != 0u) {
            gray |= UINT32_C(1) << bit;
        }
    }
    uint32_t index = 0;
    for (int bit = 31; bit >= 0; --bit) {
        uint32_t lane = (gray >> (unsigned)bit) & 1u;
        if (bit < 31) {
            lane ^= (index >> (unsigned)(bit + 1)) & 1u;
        }
        index |= lane << (unsigned)bit;
    }
    return index;
}

int vb_directions(uint32_t dimension, uint32_t directions[32]) {
    if (dimension < 1u || dimension > (uint32_t)VB_MAX_DIM || directions == NULL) {
        return -1;
    }
    memcpy(directions, d1gf2_V[dimension - 1u], 32u * sizeof(uint32_t));
    return 0;
}

uint32_t vb_gauss_bits(uint32_t word) {
    return word;
}

static int intern_pattern(
    vb_map_t *map,
    uint32_t *pattern_count,
    const uint32_t control[VB_LANES]
) {
    uint32_t pattern;
    for (pattern = 0; pattern < *pattern_count; ++pattern) {
        if (memcmp(map->patterns[pattern], control, sizeof(uint32_t) * VB_LANES) == 0) {
            return (int)pattern;
        }
    }
    if (*pattern_count >= (uint32_t)VB_MAX_PATTERNS) {
        return -1;
    }
    memcpy(map->patterns[*pattern_count], control, sizeof(uint32_t) * VB_LANES);
    pattern = *pattern_count;
    *pattern_count += 1u;
    return (int)pattern;
}

int vb_prepare(
    uint32_t total_paths,
    uint32_t tile_paths,
    uint32_t tile_index,
    uint32_t dimension,
    vb_map_t *map,
    vb_tile_info_t *info,
    uint32_t *donor_words
) {
    uint32_t directions[32];
    uint32_t sources[VB_MAX_PATHS];
    uint32_t dest;
    uint32_t donor_base;
    uint32_t donor_max;
    uint32_t packets;
    uint32_t pattern_count;
    uint32_t packet;
    uint32_t d1[32];

    if (!vb_valid_pair(total_paths, tile_paths) ||
        tile_index >= (total_paths / tile_paths) ||
        map == NULL || info == NULL ||
        ((uintptr_t)map & 63u) != 0u) {
        return -1;
    }
    if (vb_directions(dimension, directions) != 0) {
        return -1;
    }
    if (vb_directions(1u, d1) != 0) {
        return -1;
    }

    const uint32_t dest_base =
        (uint32_t)VB_POINT_BASE + tile_index * tile_paths;
    for (dest = 0; dest < tile_paths; ++dest) {
        const uint32_t word = vb_sobol_word(dest_base + dest, directions);
        const uint32_t global = vb_d1_index_from_word(word);
        if (global < (uint32_t)VB_POINT_BASE ||
            global >= (uint32_t)VB_POINT_BASE + (uint32_t)VB_UNIVERSE) {
            return -1;
        }
        sources[dest] = global;
        if (vb_sobol_word(global, d1) != word) {
            return -1;
        }
    }

    donor_base = sources[0];
    donor_max = sources[0];
    for (dest = 1; dest < tile_paths; ++dest) {
        if (sources[dest] < donor_base) {
            donor_base = sources[dest];
        }
        if (sources[dest] > donor_max) {
            donor_max = sources[dest];
        }
    }
    if (donor_max - donor_base + 1u != tile_paths ||
        (donor_base % tile_paths) != 0u) {
        return -1;
    }

    {
        uint8_t seen[VB_MAX_PATHS];
        memset(seen, 0, tile_paths);
        for (dest = 0; dest < tile_paths; ++dest) {
            const uint32_t rel = sources[dest] - donor_base;
            if (rel >= tile_paths || seen[rel] != 0u) {
                return -1;
            }
            seen[rel] = 1u;
            sources[dest] = rel;
        }
    }

    memset(map, 0, sizeof(*map));
    map->dimension = dimension;
    pattern_count = 0;
    packets = tile_paths / (uint32_t)VB_PACKET;
    for (packet = 0; packet < packets; ++packet) {
        uint32_t half;
        const uint32_t src_packet =
            sources[packet * (uint32_t)VB_PACKET] / (uint32_t)VB_PACKET;
        uint32_t lane;
        for (lane = 0; lane < (uint32_t)VB_PACKET; ++lane) {
            if (sources[packet * (uint32_t)VB_PACKET + lane] /
                    (uint32_t)VB_PACKET != src_packet) {
                return -1;
            }
        }
        for (half = 0; half < 2u; ++half) {
            const uint32_t base =
                packet * (uint32_t)VB_PACKET + half * (uint32_t)VB_LANES;
            const uint32_t line = sources[base] / (uint32_t)VB_LANES;
            uint32_t control[VB_LANES];
            int found;
            if (half == 1u &&
                (line ^ (sources[packet * (uint32_t)VB_PACKET] /
                         (uint32_t)VB_LANES)) != 1u) {
                return -1;
            }
            for (lane = 0; lane < (uint32_t)VB_LANES; ++lane) {
                const uint32_t source = sources[base + lane];
                if (source / (uint32_t)VB_LANES != line) {
                    return -1;
                }
                control[lane] = source % (uint32_t)VB_LANES;
            }
            found = intern_pattern(map, &pattern_count, control);
            if (found < 0) {
                return -1;
            }
            map->select[packet][half] = (uint8_t)line;
            map->select[packet][2u + half] = (uint8_t)found;
        }
    }
    map->pattern_count = pattern_count;

    memset(info, 0, sizeof(*info));
    info->total_paths = total_paths;
    info->tile_paths = tile_paths;
    info->tile_index = tile_index;
    info->dimension = dimension;
    info->packets = packets;
    info->donor_count = tile_paths;
    info->donor_base = donor_base;
    info->donor_word_bytes = tile_paths * 4u;
    info->donor_x_growth_bytes = tile_paths * 8u;
    info->map_bytes = (uint32_t)VB_MAP_BYTES;
    info->working_set_bytes = info->map_bytes + info->donor_x_growth_bytes;
    info->donor_closed = 1;
    info->two_region_valid = 1;
    info->references_full_universe = 0;

    if (donor_words != NULL) {
        uint32_t i;
        for (i = 0; i < tile_paths; ++i) {
            donor_words[i] = vb_sobol_word(donor_base + i, d1);
        }
    }
    return 0;
}

void vb_apply(
    const vb_map_t *map,
    const uint32_t *donor_words,
    uint32_t tile_paths,
    uint32_t *dest_words
) {
    uint32_t packet;
    const uint32_t packets = tile_paths / (uint32_t)VB_PACKET;
    for (packet = 0; packet < packets; ++packet) {
        uint32_t half;
        for (half = 0; half < 2u; ++half) {
            const uint32_t line = map->select[packet][half];
            const uint32_t *control =
                map->patterns[map->select[packet][2u + half]];
            const uint32_t *source = donor_words + line * (uint32_t)VB_LANES;
            uint32_t lane;
            uint32_t *out =
                dest_words + packet * (uint32_t)VB_PACKET +
                half * (uint32_t)VB_LANES;
            for (lane = 0; lane < (uint32_t)VB_LANES; ++lane) {
                out[lane] = source[control[lane]];
            }
        }
    }
}
