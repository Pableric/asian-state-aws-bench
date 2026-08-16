
#include "sobol.h"
#include "gaussian_range_schedule_2048.h"
#include "gaussian_scheduled_coeff_values_2048.h"
#include "gaussian_tail_coeff_values_2048.h"
#include "gaussian_linear_coeff_values_2048.h"
#include "gaussian_first_patch_values_2048.h"
#include <stdalign.h>
#include <stdint.h>

// Dimension-1 Sobol data used by the AVX-512 generator.
//
// words[0..31] are the direction numbers. For dimension 1 they are just
// 0x80000000 >> bit, but keeping them here lets the scalar Gray-code
// accumulator use the same load path as the original generator.
//
// words[32..63] are the precomputed strided initial values loaded into
// zmm12/zmm13. The assembly never reads beyond these 64 words.
alignas(64) static const struct {
    uint32_t words[64];
} sobol_data = {
    .words = {
        0x80000000u, 0x40000000u, 0x20000000u, 0x10000000u,
        0x08000000u, 0x04000000u, 0x02000000u, 0x01000000u,
        0x00800000u, 0x00400000u, 0x00200000u, 0x00100000u,
        0x00080000u, 0x00040000u, 0x00020000u, 0x00010000u,
        0x00008000u, 0x00004000u, 0x00002000u, 0x00001000u,
        0x00000800u, 0x00000400u, 0x00000200u, 0x00000100u,
        0x00000080u, 0x00000040u, 0x00000020u, 0x00000010u,
        0x00000008u, 0x00000004u, 0x00000002u, 0x00000001u,

        0x00000000u, 0x01800000u, 0x00c00000u, 0x01400000u,
        0x00600000u, 0x01e00000u, 0x00a00000u, 0x01200000u,
        0x00300000u, 0x01b00000u, 0x00f00000u, 0x01700000u,
        0x00500000u, 0x01d00000u, 0x00900000u, 0x01100000u,
        0x00100000u, 0x01900000u, 0x00d00000u, 0x01500000u,
        0x00700000u, 0x01f00000u, 0x00b00000u, 0x01300000u,
        0x00200000u, 0x01a00000u, 0x00e00000u, 0x01600000u,
        0x00400000u, 0x01c00000u, 0x00800000u, 0x01000000u,
    }
};

extern void generate_sobol_sequence(
    float* output_buffer,
    uint64_t number_of_iterations,
    const uint32_t* direction_vectors,
    const float* gauss_c0,
    const float* gauss_c1,
    const float* gauss_c2,
    const uint32_t* gauss_fold_mask,
    const float* gauss_tail_c0,
    const float* gauss_tail_c1,
    const float* gauss_tail_c2,
    const float* gauss_tail_c3,
    const float* gauss_tail_c4,
    const float* gauss_tail_c5,
    const float* gauss_range2047_lut,
    const uint32_t* gauss_range2047_mask,
    const float* gauss_linear_c0,
    const float* gauss_linear_c1,
    const float* gauss_first_patch_zmm
);

extern void generate_sobol_sequence_debug(
    float* output_buffer,
    const uint32_t* direction_vectors,
    const float* gauss_c0,
    const float* gauss_c1,
    const float* gauss_c2,
    const uint32_t* gauss_fold_mask,
    gauss_debug_step_t* debug
);

int generate_sobol(float* output, uint64_t num_blocks) {
    if (!output)
        return -1;
    if (num_blocks == 0)
        return -1;

    const uint32_t *direction_vectors = &sobol_data.words[0];
    uint64_t last_internal_block = num_blocks * 2 - 1;
    
    generate_sobol_sequence(
        output,
        last_internal_block,
        direction_vectors,
        gauss_sched_c0,
        gauss_sched_c1,
        gauss_sched_c2,
        gauss_fold_mask_2048,
        gauss_tail_c0,
        gauss_tail_c1,
        gauss_tail_c2,
        gauss_tail_c3,
        gauss_tail_c4,
        gauss_tail_c5,
        gauss_range2047_lut,
        gauss_range2047_mask,
        gauss_linear_c0,
        gauss_linear_c1,
        gauss_first_patch_zmm
    );

    return 0;
}

int generate_sobol_debug(float* output, gauss_debug_step_t* debug) {
    if (!output || !debug)
        return -1;

    const uint32_t *direction_vectors = &sobol_data.words[0];

    generate_sobol_sequence_debug(
        output,
        direction_vectors,
        gauss_sched_c0,
        gauss_sched_c1,
        gauss_sched_c2,
        gauss_fold_mask_2048,
        debug
    );

    return 0;
}
