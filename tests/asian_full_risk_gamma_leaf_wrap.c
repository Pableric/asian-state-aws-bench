#include "private/asian_full_risk_gamma_diag.h"

static uint32_t leaf_calls;

void __real_asian_full_risk_gamma_affine_call_impl_diag(
    const asian_full_risk_gamma_hot_context_t *,
    asian_full_risk_gamma_direct_value_t *);
void __real_asian_full_risk_gamma_affine_put_impl_diag(
    const asian_full_risk_gamma_hot_context_t *,
    asian_full_risk_gamma_direct_value_t *);

void __wrap_asian_full_risk_gamma_affine_call_impl_diag(
    const asian_full_risk_gamma_hot_context_t *context,
    asian_full_risk_gamma_direct_value_t *output)
{
    ++leaf_calls;
    __real_asian_full_risk_gamma_affine_call_impl_diag(context, output);
}

void __wrap_asian_full_risk_gamma_affine_put_impl_diag(
    const asian_full_risk_gamma_hot_context_t *context,
    asian_full_risk_gamma_direct_value_t *output)
{
    ++leaf_calls;
    __real_asian_full_risk_gamma_affine_put_impl_diag(context, output);
}

void asian_full_risk_gamma_leaf_counter_reset(void)
{
    leaf_calls = 0u;
}

uint32_t asian_full_risk_gamma_leaf_counter_read(void)
{
    return leaf_calls;
}
