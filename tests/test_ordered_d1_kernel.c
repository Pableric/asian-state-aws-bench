#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "private/european_ordered_setup.h"

extern double price_european_sequence_ordered_d1(
    uint64_t packets,
    const uint32_t* initial,
    const uint32_t* jumps,
    const float* c0,
    const float* c1,
    const european_ordered_d1_tail_context_t* tail
);

static const uint32_t w[32] = {
    0x80000000u, 0x40000000u, 0x20000000u, 0x10000000u,
    0x08000000u, 0x04000000u, 0x02000000u, 0x01000000u,
    0x00800000u, 0x00400000u, 0x00200000u, 0x00100000u,
    0x00080000u, 0x00040000u, 0x00020000u, 0x00010000u,
    0x00008000u, 0x00004000u, 0x00002000u, 0x00001000u,
    0x00000800u, 0x00000400u, 0x00000200u, 0x00000100u,
    0x00000080u, 0x00000040u, 0x00000020u, 0x00000010u,
    0x00000008u, 0x00000004u, 0x00000002u, 0x00000001u,
};

static uint32_t sobol_word(uint64_t index) {
    uint64_t gray = index ^ (index >> 1);
    uint32_t value = 0;
    for (unsigned column = 0; gray; ++column, gray >>= 1) {
        if (gray & 1u) {
            value ^= w[column];
        }
    }
    return value;
}

static float raw_x(uint32_t word) {
    const uint32_t bits = 0x3f800000u | (word >> 9);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static float add_f32(float a, float b) {
    volatile float value = a + b;
    return value;
}

static double reference(uint64_t packets, const float* c0, const float* c1) {
    float a[16] = {0};
    float b[16] = {0};
    for (uint64_t packet = 0; packet < packets; ++packet) {
        const uint64_t index = 8192u + packet * 32u;
        const size_t row = (size_t)(packet & 127u) * 16u;
        for (size_t lane = 0; lane < 16; ++lane) {
            float pa = fmaf(c1[row + lane], raw_x(sobol_word(index + lane)),
                            c0[row + lane]);
            float pb = fmaf(c1[row + lane], raw_x(sobol_word(index + 16u + lane)),
                            c0[row + lane]);
            if (pa < 0.0f) pa = 0.0f;
            if (pb < 0.0f) pb = 0.0f;
            a[lane] = add_f32(a[lane], pa);
            b[lane] = add_f32(b[lane], pb);
        }
    }
    float lanes[16];
    for (size_t lane = 0; lane < 16; ++lane) {
        lanes[lane] = add_f32(a[lane], b[lane]);
    }
    for (size_t lane = 0; lane < 8; ++lane) {
        lanes[lane] = add_f32(lanes[lane], lanes[lane + 8]);
    }
    for (size_t lane = 0; lane < 4; ++lane) {
        lanes[lane] = add_f32(lanes[lane], lanes[lane + 4]);
    }
    const float pair01 = add_f32(lanes[0], lanes[1]);
    const float pair23 = add_f32(lanes[2], lanes[3]);
    return (double)add_f32(pair01, pair23);
}

int main(void) {
    uint32_t initial[32] __attribute__((aligned(64)));
    uint32_t jumps[32] __attribute__((aligned(64))) = {0};
    float c0[128 * 16] __attribute__((aligned(64)));
    float c1[128 * 16] __attribute__((aligned(64)));
    float range2047_lut[2048] __attribute__((aligned(64))) = {0};
    european_ordered_d1_tail_context_t tail = {0};
    tail.range2047_lut = range2047_lut;
    for (size_t lane = 0; lane < 32; ++lane) {
        initial[lane] = sobol_word(8192u + lane);
    }
    for (size_t column = 0; column < 27; ++column) {
        jumps[column] = w[4] ^ w[column + 5];
    }
    for (size_t row = 0; row < 128; ++row) {
        for (size_t lane = 0; lane < 16; ++lane) {
            c0[row * 16 + lane] = 0.01f + (float)row * 0.0001f +
                (float)lane * 0.00001f;
            c1[row * 16 + lane] = 0.25f + (float)lane * 0.001f;
        }
    }
    const uint64_t cases[] = {1, 3, 127, 128, 129, 255, 256, 257, 512, 4096};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const double expected = reference(cases[i], c0, c1);
        const double actual = price_european_sequence_ordered_d1(
            cases[i], initial, jumps, c0, c1, &tail);
        if (actual != expected) {
            fprintf(stderr, "packets=%llu expected=%.17g actual=%.17g\n",
                    (unsigned long long)cases[i], expected, actual);
            return 1;
        }
    }
    puts("ordered_d1_kernel_reference=PASS");
    return 0;
}
