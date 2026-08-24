#define _POSIX_C_SOURCE 200112L
#include "private/asian_ordered_d1_source_opt.h"
#include "private/asian_geometric_cv_diag.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile float sink;

static void *alloc64(size_t bytes)
{
    void *pointer = NULL;
    if (posix_memalign(&pointer, 64, bytes) != 0) {
        return NULL;
    }
    return pointer;
}

int main(int argc, char **argv)
{
    if (argc != 2 ||
        (strcmp(argv[1], "current") != 0 &&
         strcmp(argv[1], "optimized") != 0 &&
         strcmp(argv[1], "baseline") != 0 &&
         strcmp(argv[1], "candidate") != 0)) {
        return 2;
    }
    const int current = strcmp(argv[1], "current") == 0;
    const int optimized = strcmp(argv[1], "optimized") == 0;
    ordered_d1_diag_context_t *context = alloc64(sizeof(*context));
    float *x = alloc64(ASIAN_ORDERED_D1_SOURCE_OPT_VALUES * sizeof(float));
    float *growth = alloc64(ASIAN_ORDERED_D1_SOURCE_OPT_VALUES * sizeof(float));
    if (context == NULL || x == NULL || growth == NULL ||
        ordered_d1_diag_prepare(
            context,
            (float)((0.03 - 0.5 * 0.20 * 0.20) / 32.0),
            (float)(0.20 / sqrt(32.0)),
            ASIAN_ORDERED_D1_SOURCE_OPT_VALUES,
            ORDERED_D1_DIAG_PREPARE_X3 |
                (current || optimized
                     ? 0u
                     : ORDERED_D1_DIAG_PREPARE_GROWTH3),
            32) != 0) {
        return 2;
    }
    if (current || optimized) {
        if (optimized) {
            asian_ordered_d1_x_static_8192_diag(context, x);
        } else {
            ordered_d1_x_only_diag(
                ASIAN_ORDERED_D1_SOURCE_OPT_VALUES / 32, context, x);
        }
        asian_vector_exp_range_reduced_array_diag(x, growth);
        asian_vector_exp_range_reduced_array_diag(
            x + ASIAN_ORDERED_D1_SOURCE_OPT_VALUES / 2,
            growth + ASIAN_ORDERED_D1_SOURCE_OPT_VALUES / 2);
    } else if (argv[1][0] == 'b') {
        ordered_d1_x_growth_local_diag(
            ASIAN_ORDERED_D1_SOURCE_OPT_VALUES / 32, context, x, growth);
    } else {
        asian_ordered_d1_x_growth_static_8192_diag(context, x, growth);
    }
    sink += x[0] + growth[ASIAN_ORDERED_D1_SOURCE_OPT_VALUES - 1];
    printf("asian_ordered_d1_source_symbol=%s sink=%.9g\n", argv[1], sink);
    free(growth);
    free(x);
    free(context);
    return 0;
}
