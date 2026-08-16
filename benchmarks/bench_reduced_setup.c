#include "private/european_reduced_setup.h"
#include "european_reduced_exp_slots.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum { ROUNDS = 21, WARMUPS = 100 };

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
}

static int compare_double(const void* left, const void* right) {
    const double a = *(const double*)left;
    const double b = *(const double*)right;
    return (a > b) - (a < b);
}

static void build_vector(
    const float params[4],
    float* tail,
    float* c0,
    float* c1
) {
    european_build_reduced_tail_schedule(params, tail);
    european_build_composed_normal_schedule(params, c0, c1);
}

static void build_scalar(
    const float params[4],
    float* tail,
    float* c0,
    float* c1
) {
    european_build_reduced_tail_schedule_scalar(params, tail);
    european_build_composed_normal_schedule_scalar(params, c0, c1);
}

static double measure(
    void (*builder)(const float*, float*, float*, float*),
    const float params[4],
    size_t iterations,
    float* tail,
    float* c0,
    float* c1
) {
    const double start = now_seconds();
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        builder(params, tail, c0, c1);
    }
    return (now_seconds() - start) / (double)iterations;
}

int main(int argc, char** argv) {
    const size_t iterations =
        argc > 1 ? (size_t)strtoull(argv[1], NULL, 10) : 10000u;
    if (iterations == 0) {
        return 2;
    }
    const float params[4] = {
        0.200000000f, 95.1229401f, -90.4837418f, 0.0f
    };
    float tail[
        EUROPEAN_REDUCED_EXP_TAIL_STREAMS * EUROPEAN_REDUCED_EXP_TAIL_STRIDE
    ] __attribute__((aligned(64)));
    float c0[128 * 32] __attribute__((aligned(64)));
    float c1[128 * 32] __attribute__((aligned(64)));
    double scalar[ROUNDS];
    double vector[ROUNDS];

    for (size_t warmup = 0; warmup < WARMUPS; ++warmup) {
        build_scalar(params, tail, c0, c1);
        build_vector(params, tail, c0, c1);
    }
    for (size_t round = 0; round < ROUNDS; ++round) {
        if ((round & 1u) == 0) {
            scalar[round] = measure(
                build_scalar, params, iterations, tail, c0, c1);
            vector[round] = measure(
                build_vector, params, iterations, tail, c0, c1);
        } else {
            vector[round] = measure(
                build_vector, params, iterations, tail, c0, c1);
            scalar[round] = measure(
                build_scalar, params, iterations, tail, c0, c1);
        }
    }
    qsort(scalar, ROUNDS, sizeof(scalar[0]), compare_double);
    qsort(vector, ROUNDS, sizeof(vector[0]), compare_double);

    volatile float checksum =
        tail[0] + tail[EUROPEAN_REDUCED_EXP_TAIL_STRIDE] + c0[0] + c1[0];
    printf(
        "RESULT iterations=%zu scalar_setup_ns=%.3f vector_setup_ns=%.3f "
        "speedup=%.3fx checksum=%.9g\n",
        iterations,
        scalar[ROUNDS / 2] * 1.0e9,
        vector[ROUNDS / 2] * 1.0e9,
        scalar[ROUNDS / 2] / vector[ROUNDS / 2],
        checksum);
    return 0;
}
