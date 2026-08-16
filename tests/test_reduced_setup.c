#include "private/european_reduced_setup.h"
#include "european_reduced_exp_slots.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t ulp_distance(float a, float b) {
    uint32_t ua;
    uint32_t ub;
    memcpy(&ua, &a, sizeof(ua));
    memcpy(&ub, &b, sizeof(ub));
    if ((ua ^ ub) & 0x80000000u) {
        return (a == b) ? 0u : UINT32_MAX;
    }
    return ua > ub ? ua - ub : ub - ua;
}

static int compare_value(
    const char* table,
    size_t case_index,
    size_t index,
    float expected,
    float actual,
    uint32_t* worst_ulp
) {
    const uint32_t distance = ulp_distance(expected, actual);
    if (distance > *worst_ulp) {
        *worst_ulp = distance;
    }
    if (distance > 1u) {
        fprintf(
            stderr,
            "%s mismatch case=%zu index=%zu expected=%.9g actual=%.9g ulp=%u\n",
            table, case_index, index, expected, actual, distance);
        return 1;
    }
    return 0;
}

int main(void) {
    static const float cases[][4] = {
        {0.200000000f, 95.1229401f, -90.4837418f, 0.0f},
        {0.100000001f, -119.401497f, 92.5925903f, 0.0f},
        {0.0500000007f, 79.6019974f, -104.475128f, 0.0f},
        {0.199900001f, -61.0f, 57.0f, 0.0f},
        {0.000100000f, 1.0f, -0.999f, 0.0f},
    };
    float vector_tail[
        EUROPEAN_REDUCED_EXP_TAIL_STREAMS * EUROPEAN_REDUCED_EXP_TAIL_STRIDE
    ] __attribute__((aligned(64)));
    float scalar_tail[
        EUROPEAN_REDUCED_EXP_TAIL_STREAMS * EUROPEAN_REDUCED_EXP_TAIL_STRIDE
    ] __attribute__((aligned(64)));
    float vector_c0[128 * 32] __attribute__((aligned(64)));
    float vector_c1[128 * 32] __attribute__((aligned(64)));
    float scalar_c0[128 * 32] __attribute__((aligned(64)));
    float scalar_c1[128 * 32] __attribute__((aligned(64)));
    uint32_t worst_ulp = 0;
    int failed = 0;

    for (size_t case_index = 0; case_index < sizeof(cases) / sizeof(cases[0]);
         ++case_index) {
        memset(vector_tail, 0, sizeof(vector_tail));
        memset(scalar_tail, 0, sizeof(scalar_tail));
        memset(vector_c0, 0, sizeof(vector_c0));
        memset(vector_c1, 0, sizeof(vector_c1));
        memset(scalar_c0, 0, sizeof(scalar_c0));
        memset(scalar_c1, 0, sizeof(scalar_c1));
        european_build_reduced_tail_schedule(cases[case_index], vector_tail);
        european_build_reduced_tail_schedule_scalar(cases[case_index], scalar_tail);
        european_build_composed_normal_schedule(
            cases[case_index], vector_c0, vector_c1);
        european_build_composed_normal_schedule_scalar(
            cases[case_index], scalar_c0, scalar_c1);

        for (size_t stream = 0; stream < EUROPEAN_REDUCED_EXP_TAIL_STREAMS;
             ++stream) {
            for (size_t slot = 0; slot < EUROPEAN_REDUCED_EXP_TAIL_SLOTS; ++slot) {
                const size_t index =
                    stream * EUROPEAN_REDUCED_EXP_TAIL_STRIDE + slot;
                failed |= compare_value(
                    "tail", case_index, index,
                    scalar_tail[index], vector_tail[index], &worst_ulp);
            }
        }
        for (size_t pair = 0; pair < 128; ++pair) {
            for (size_t lane = 0; lane < 16; ++lane) {
                const size_t index = pair * 32 + lane;
                failed |= compare_value(
                    "normal_c0", case_index, index,
                    scalar_c0[index], vector_c0[index], &worst_ulp);
                failed |= compare_value(
                    "normal_c1", case_index, index,
                    scalar_c1[index], vector_c1[index], &worst_ulp);
            }
        }
    }

    printf("reduced_setup_cases=%zu worst_ulp=%u\n",
           sizeof(cases) / sizeof(cases[0]), worst_ulp);
    return failed ? 1 : 0;
}
