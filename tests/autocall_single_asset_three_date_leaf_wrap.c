#include "private/autocall_single_asset_three_date_raw_diag.h"

#include <stdint.h>

uint64_t autocall_3date_test_leaf_invocations;

double __real_autocall_3date_affine_price_leaf(
    const autocall_3date_leaf_context_t *);

double __wrap_autocall_3date_affine_price_leaf(
    const autocall_3date_leaf_context_t *context)
{
    ++autocall_3date_test_leaf_invocations;
    return __real_autocall_3date_affine_price_leaf(context);
}
