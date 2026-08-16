#ifndef EUROPEAN_ORDERED_SETUP_H
#define EUROPEAN_ORDERED_SETUP_H

#include <stdint.h>

#define EUROPEAN_ORDERED_D1_COEFF_FLOATS 2048u
#define EUROPEAN_ORDERED_D1_HARD_FLOATS 64u
#define EUROPEAN_ORDERED_D1_CUBIC_FLOATS 48u
#define EUROPEAN_ORDERED_D1_MAX_ALPHA 0.20f

typedef struct __attribute__((aligned(64))) {
    float params[4];
    const float* range2047_lut;
    uint8_t reserved[40];
    float fast_c0[EUROPEAN_ORDERED_D1_HARD_FLOATS];
    float fast_c1[EUROPEAN_ORDERED_D1_HARD_FLOATS];
    float cubic_c0[EUROPEAN_ORDERED_D1_CUBIC_FLOATS];
    float cubic_c1[EUROPEAN_ORDERED_D1_CUBIC_FLOATS];
    float cubic_c2[EUROPEAN_ORDERED_D1_CUBIC_FLOATS];
    float cubic_c3[EUROPEAN_ORDERED_D1_CUBIC_FLOATS];
} european_ordered_d1_tail_context_t;

void european_build_ordered_d1_schedule(
    const float params[4],
    float* combined_c0,
    float* combined_c1,
    european_ordered_d1_tail_context_t* tail,
    const float* range2047_lut
);

#endif
