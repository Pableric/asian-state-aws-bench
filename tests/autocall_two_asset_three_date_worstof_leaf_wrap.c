#include "private/autocall_two_asset_three_date_worstof_raw_diag.h"

#include <stdint.h>

uint64_t autocall_worstof_prepared_leaf_invocations;
uint64_t autocall_worstof_inline_leaf_invocations;

double __real_autocall_worstof_prepared_leaf(
    const autocall_worstof_prepared_leaf_context_t *);
double __real_autocall_worstof_inline_leaf(
    const autocall_worstof_inline_leaf_context_t *);

double __wrap_autocall_worstof_prepared_leaf(
    const autocall_worstof_prepared_leaf_context_t *context) {
    ++autocall_worstof_prepared_leaf_invocations;
    return __real_autocall_worstof_prepared_leaf(context);
}

double __wrap_autocall_worstof_inline_leaf(
    const autocall_worstof_inline_leaf_context_t *context) {
    ++autocall_worstof_inline_leaf_invocations;
    return __real_autocall_worstof_inline_leaf(context);
}
