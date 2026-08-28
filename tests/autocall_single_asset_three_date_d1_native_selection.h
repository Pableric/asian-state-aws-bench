#ifndef AUTOCALL_SINGLE_ASSET_THREE_DATE_D1_NATIVE_SELECTION_H
#define AUTOCALL_SINGLE_ASSET_THREE_DATE_D1_NATIVE_SELECTION_H

#include <float.h>
#include <stddef.h>
#include <stdint.h>

/* Benchmark-control policy: terminal self-passing counts are deliberately
 * absent unless they also passed the next-prefix/global qualification gate. */
static inline int autocall_d1_native_select_best_global(
    uint32_t global_mask, const double *primary_score, size_t count)
{
    int selected = -1;
    double best = DBL_MAX;
    for (size_t i = 0; i < count; ++i) {
        if ((global_mask & (UINT32_C(1) << i)) != 0u &&
            primary_score[i] < best) {
            best = primary_score[i];
            selected = (int)i;
        }
    }
    return selected;
}

#endif
