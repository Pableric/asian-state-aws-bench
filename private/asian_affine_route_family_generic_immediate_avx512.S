/*
 * Reuse the exact qualified generic K=1 donor as the macro source, then
 * instantiate only the missing widths.  The preprocessor rename prevents a
 * duplicate symbol while preserving every provider/payoff/reduction byte.
 */
#define asian_n64_generic_geocv_immediate_price_1_diag asian_affine_family_generic_geocv_price_1_diag
#include "asian_n64_generic_geocv_immediate_avx512.s"

.section .text
IMMEDIATE_LEAF asian_affine_family_generic_geocv_price_delta_1_diag,1,1
IMMEDIATE_LEAF asian_affine_family_generic_geocv_price_2_diag,2,0
IMMEDIATE_LEAF asian_affine_family_generic_geocv_price_delta_2_diag,2,1
IMMEDIATE_LEAF asian_affine_family_generic_geocv_price_4_diag,4,0
IMMEDIATE_LEAF asian_affine_family_generic_geocv_price_delta_4_diag,4,1
