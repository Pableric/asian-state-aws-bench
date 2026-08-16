#ifndef SOBOL_H
#define SOBOL_H

#include <stdint.h>

#define SOBOL_BLOCK_SIZE 8192

typedef struct {
    uint32_t sobol_raw[32];
    uint32_t sobol_shifted[32];
    uint32_t folded_delta[32];
    uint32_t folded_local[32];
    uint32_t fold_mask[32];
    float s[32];
    float c2[32];
    float c1[32];
    float c0[32];
    float out[32];
} gauss_debug_step_t;

/**
 * Generate dimension-1 Sobol Gaussian values.
 *
 * @param output    Caller-allocated buffer, must hold num_blocks * SOBOL_BLOCK_SIZE floats
 * @param num_blocks Number of blocks to generate (must be > 0)
 * @return          0 on success, -1 on invalid arguments
 */
int generate_sobol(float* output, uint64_t num_blocks);

/**
 * Generate and dump the first two-zmm Gaussian transform step for internal chunk 2.
 *
 * This is a debug helper only. It mirrors the production assembly path for the
 * first 32 values after the initial skipped chunks.
 */
int generate_sobol_debug(float* output, gauss_debug_step_t* debug);

#endif
