#ifndef ASIAN_BLOCK_LAYOUT_H
#define ASIAN_BLOCK_LAYOUT_H

#include <stdint.h>

enum {
    ASIAN_SOBOL_BLOCK_VALUES = 4096,
    ASIAN_SOBOL_PACKET_VALUES = 32,
    ASIAN_SOBOL_BLOCK_PACKETS = 128,
    ASIAN_SOBOL_BLOCK_BYTES = ASIAN_SOBOL_BLOCK_VALUES * sizeof(uint32_t),
    ASIAN_ASSUMED_L1D_BYTES = 48 * 1024,
    ASIAN_TWO_BLOCK_WORKING_SET_BYTES = 2 * ASIAN_SOBOL_BLOCK_BYTES,
};

_Static_assert(ASIAN_SOBOL_BLOCK_VALUES % ASIAN_SOBOL_PACKET_VALUES == 0,
               "Sobol block must contain whole two-ZMM packets");
_Static_assert(ASIAN_SOBOL_BLOCK_BYTES == 16 * 1024,
               "4096 raw uint32 Sobol values must occupy 16 KiB");
_Static_assert(ASIAN_TWO_BLOCK_WORKING_SET_BYTES == 32 * 1024,
               "D1 plus one selected dimension must occupy 32 KiB");
_Static_assert(ASIAN_TWO_BLOCK_WORKING_SET_BYTES < ASIAN_ASSUMED_L1D_BYTES,
               "two-block working set must leave L1D headroom");

#endif
