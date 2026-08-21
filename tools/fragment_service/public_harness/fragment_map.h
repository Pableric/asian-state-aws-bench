#ifndef FRAGMENT_MAP_H
#define FRAGMENT_MAP_H

#include <stddef.h>
#include <stdint.h>

enum {
    FRAG_BLOCK_VALUES = 4096,
    FRAG_PACKET_VALUES = 32,
    FRAG_LANES = 16,
    FRAG_BLOCK_PACKETS = FRAG_BLOCK_VALUES / FRAG_PACKET_VALUES,
    FRAG_BLOCK_LINES = FRAG_BLOCK_VALUES / FRAG_LANES,
    FRAG_POINT_BASE = 8192,
    FRAG_ALIGNMENT = 64,
    FRAG_BLOCK_BYTES = FRAG_BLOCK_VALUES * (int)sizeof(float),
    FRAG_MAX_PATTERNS = 16,
    FRAG_SELECT_BYTES = FRAG_BLOCK_PACKETS * 4,
    FRAG_PATTERNS_OFFSET = 576,
    FRAG_MAP_BYTES = 1600,
    FRAG_PRODUCER_COEFF_BYTES = 16 * 1024,
    FRAG_ROW_WORDS = 33,
    FRAG_DIRECTION_WORDS = 32,
    /*
     * Joe--Kuo dimension numbers are one-based: 1 is D1, 21201 is the last
     * table row. File offsets use (dimension - 1).
     */
    FRAG_JOE_KUO_DIMENSION_MIN = 1,
    FRAG_JOE_KUO_DIMENSION_MAX = 21201,
};

/*
 * 64-byte-aligned prepared map. Must stay live and unmodified while any
 * fragment call uses it. Packet ordinal is 0..127. D1 Z and this map must
 * both be 64-byte aligned. fragment_prepare_map returns -1 on a zero/out-of-
 * range dimension, NULL/unaligned map, or a dimension that is not an exact
 * permutation of D1 points 8192..12287 with one source line per destination
 * ZMM. The hot fragment does not range-check packet.
 *
 * select[packet] = {line_a, line_b, pattern_a, pattern_b}
 * patterns[] start at offset 576.
 */
typedef struct __attribute__((aligned(64))) {
    uint8_t select[FRAG_BLOCK_PACKETS][4];
    uint32_t pattern_count;
    uint32_t dimension;
    uint8_t reserved[56];
    uint32_t patterns[FRAG_MAX_PATTERNS][FRAG_LANES];
} fragment_map_t;

_Static_assert(offsetof(fragment_map_t, patterns) == FRAG_PATTERNS_OFFSET,
               "vpermd patterns must start at offset 576");
_Static_assert(sizeof(fragment_map_t) == FRAG_MAP_BYTES,
               "map footprint is 1600 bytes");

int fragment_load_directions(uint32_t dimension, uint32_t directions[32]);
uint32_t fragment_sobol_word(uint32_t index, const uint32_t directions[32]);
uint32_t fragment_d1_index_from_word(uint32_t word);
int fragment_prepare_map(
    uint32_t dimension,
    const uint32_t direction_words[32],
    fragment_map_t *map
);
void fragment_apply_packet(
    const float *d1_z,
    const fragment_map_t *map,
    uint32_t packet,
    float out_a[FRAG_LANES],
    float out_b[FRAG_LANES]
);

#endif
