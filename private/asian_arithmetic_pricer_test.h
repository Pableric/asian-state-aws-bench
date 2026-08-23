#ifndef ASIAN_ARITHMETIC_PRICER_TEST_H
#define ASIAN_ARITHMETIC_PRICER_TEST_H

#include "../asian_arithmetic_pricer.h"
#include "asian_genuine_arithmetic_growth_only_diag.h"
#include "asian_genuine_arithmetic_fused_source_exp_diag.h"
#include "asian_genuine_price_delta_strip_diag.h"

int asian_arithmetic_test_views(
    asian_arithmetic_prepared_t *prepared,
    asian_genuine_arithmetic_fused_source_exp_context_t **fused,
    asian_genuine_arithmetic_growth_only_context_t **growth,
    asian_genuine_strip_context_t **strip,
    float **q);

#endif
