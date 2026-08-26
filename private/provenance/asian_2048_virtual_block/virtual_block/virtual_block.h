#ifndef VIRTUAL_BLOCK_H
#define VIRTUAL_BLOCK_H

#include <stddef.h>
#include <stdint.h>

enum {
    VB_POINT_BASE = 8192,
    VB_UNIVERSE = 8192,
    VB_MAX_PATHS = 4096,
    VB_MIN_PATHS = 256,
    VB_PACKET = 32,
    VB_LANES = 16,
    VB_MAX_PACKETS = VB_MAX_PATHS / VB_PACKET,
    VB_MAX_PATTERNS = 16,
    VB_SELECT_BYTES = VB_MAX_PACKETS * 4,
    VB_PATTERNS_OFFSET = 576,
    VB_MAP_BYTES = 1600,
    VB_MAX_DIM = 256
};

typedef struct __attribute__((aligned(64))) {
    uint8_t select[VB_MAX_PACKETS][4];
    uint32_t pattern_count;
    uint32_t dimension;
    uint8_t reserved[56];
    uint32_t patterns[VB_MAX_PATTERNS][VB_LANES];
} vb_map_t;

_Static_assert(offsetof(vb_map_t, patterns) == VB_PATTERNS_OFFSET,
               "vpermd patterns at 576");
_Static_assert(sizeof(vb_map_t) == VB_MAP_BYTES, "map is 1600 bytes");

typedef struct {
    uint32_t total_paths;
    uint32_t tile_paths;
    uint32_t tile_index;
    uint32_t dimension;
    uint32_t packets;
    uint32_t donor_count;
    uint32_t donor_base;
    uint32_t donor_word_bytes;
    uint32_t donor_x_growth_bytes;
    uint32_t map_bytes;
    uint32_t working_set_bytes;
    int donor_closed;
    int two_region_valid;
    int references_full_universe;
} vb_tile_info_t;

int vb_valid_size(uint32_t n);
int vb_valid_pair(uint32_t total_paths, uint32_t tile_paths);
uint32_t vb_tile_count(uint32_t total_paths, uint32_t tile_paths);
uint32_t vb_sobol_word(uint32_t index, const uint32_t directions[32]);
uint32_t vb_d1_index_from_word(uint32_t word);
int vb_directions(uint32_t dimension, uint32_t directions[32]);

int vb_prepare(
    uint32_t total_paths,
    uint32_t tile_paths,
    uint32_t tile_index,
    uint32_t dimension,
    vb_map_t *map,
    vb_tile_info_t *info,
    uint32_t *donor_words
);

void vb_apply(
    const vb_map_t *map,
    const uint32_t *donor_words,
    uint32_t tile_paths,
    uint32_t *dest_words
);

uint32_t vb_gauss_bits(uint32_t word);

#endif
