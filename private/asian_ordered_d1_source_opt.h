#ifndef ASIAN_ORDERED_D1_SOURCE_OPT_H
#define ASIAN_ORDERED_D1_SOURCE_OPT_H

#include "../ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"

enum { ASIAN_ORDERED_D1_SOURCE_OPT_VALUES = 8192 };

/*
 * Additive fixed-block diagnostic candidate imported from 73e92b4.
 * The context must have X3 prepared for canonical D1 index 8192.  The
 * 64-byte-aligned output receives exactly indices 8192..16383.
 */
void asian_ordered_d1_x_static_8192_diag(
    const ordered_d1_diag_context_t *context,
    float x_out[ASIAN_ORDERED_D1_SOURCE_OPT_VALUES]);

#endif
