#ifndef ASIAN_ORDERED_D1_SOURCE_OPT_H
#define ASIAN_ORDERED_D1_SOURCE_OPT_H

#include "../ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"

#define ASIAN_ORDERED_D1_SOURCE_OPT_VALUES 8192u

/*
 * Additive fixed-block diagnostic candidate.
 *
 * The context must have X3 and GROWTH3 prepared for canonical D1 index 8192.
 * x_out and growth_out must be distinct, 64-byte-aligned 8192-float arrays.
 * The leaf processes exactly indices 8192..16383 and performs no validation.
 */
void asian_ordered_d1_x_growth_static_8192_diag(
    const ordered_d1_diag_context_t *context,
    float x_out[ASIAN_ORDERED_D1_SOURCE_OPT_VALUES],
    float growth_out[ASIAN_ORDERED_D1_SOURCE_OPT_VALUES]);

/* Same fixed source block, requiring only ORDERED_D1_DIAG_PREPARE_X3. */
void asian_ordered_d1_x_static_8192_diag(
    const ordered_d1_diag_context_t *context,
    float x_out[ASIAN_ORDERED_D1_SOURCE_OPT_VALUES]);

#endif
