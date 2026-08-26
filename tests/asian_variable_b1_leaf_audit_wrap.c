#include "private/asian_variable_sobol_block_count_diag.h"

static uint64_t b1_leaf_invocations;

int __real_asian_affine_family_arithmetic_prepared_price(
    asian_affine_family_arithmetic_request_t *, asian_genuine_strip_output_t *);

int __wrap_asian_affine_family_arithmetic_prepared_price(
    asian_affine_family_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output)
{
    ++b1_leaf_invocations;
    return __real_asian_affine_family_arithmetic_prepared_price(request, output);
}

void __real_asian_geometric_cv_immediate_invoke_price_1(
    const asian_geometric_cv_immediate_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_output_t *);

void __wrap_asian_geometric_cv_immediate_invoke_price_1(
    const asian_geometric_cv_immediate_context_t *context,
    const asian_genuine_strip_context_t *strip,
    const asian_genuine_strip_strike_t *strikes,
    asian_genuine_strip_output_t *output)
{
    ++b1_leaf_invocations;
    __real_asian_geometric_cv_immediate_invoke_price_1(
        context, strip, strikes, output);
}

void asian_variable_b1_leaf_audit_reset(void)
{
    b1_leaf_invocations = 0u;
}

uint64_t asian_variable_b1_leaf_audit_count(void)
{
    return b1_leaf_invocations;
}
