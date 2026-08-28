#include "private/autocall_single_asset_three_date_d1_native_diag.h"

#include <stdint.h>

static uint64_t test_leaf_calls;

void __real_autocall_d1_native_price5_leaf(
    const autocall_d1_native_leaf_context_t *, double [5]);

void __wrap_autocall_d1_native_price5_leaf(
    const autocall_d1_native_leaf_context_t *context, double prices[5])
{
    ++test_leaf_calls;
    __real_autocall_d1_native_price5_leaf(context, prices);
}

void autocall_d1_native_test_leaf_calls_reset(void)
{
    test_leaf_calls = 0;
}

uint64_t autocall_d1_native_test_leaf_calls(void)
{
    return test_leaf_calls;
}
