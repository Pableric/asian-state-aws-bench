#include "private/asian_affine_route_family_diag.h"

#include <stdint.h>

typedef void (*phase1_impl_leaf_t)(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);

/* These direct-side symbols are implementation leaves, not family valuation
 * entry points.  The family header intentionally exposes only the two-sided
 * wrapper below. */
void asian_affine_family_full_risk_k1_affine_call_impl_diag(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);
void asian_affine_family_full_risk_k1_affine_put_impl_diag(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);

__attribute__((noinline, used))
int asian_affine_family_full_risk_k1_prepared_price(
    const asian_affine_family_full_risk_k1_request_t *request,
    asian_affine_family_full_risk_k1_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_AFFINE_FAMILY_FULL_RISK_K1_REQUEST_MAGIC ||
        ((uintptr_t)output & 63u) != 0u)
        return ASIAN_AFFINE_FAMILY_INVALID;

    const int direct_call =
        (request->parity.flags & ASIAN_GENUINE_MSFR_DIRECT_CALL) != 0u;
    const phase1_impl_leaf_t implementation_leaf = direct_call ?
        asian_affine_family_full_risk_k1_affine_call_impl_diag :
        asian_affine_family_full_risk_k1_affine_put_impl_diag;
    asian_genuine_aad_phase1_value_t *direct = direct_call ?
        &output->call : &output->put;

    /* Exactly one simulation.  Both public sides are finalized from the
     * qualified K=1 direct-side/parity record prepared with the request. */
    implementation_leaf(&request->context, direct);

    const double *values = (const double *)direct;
    double *call = (double *)&output->call;
    double *put = (double *)&output->put;
    for (uint32_t field = 0; field < ASIAN_GENUINE_MSFR_RISK_FIELDS; ++field) {
        /* Match the qualified one-block accumulator/finalizer operation order. */
        const double sum = 0.0 + (values[field] - 0.0) * 4096.0;
        const double normalized = sum * (1.0 / 4096.0);
        call[field] = normalized + request->parity.call_adjust[field];
        put[field] = normalized + request->parity.put_adjust[field];
    }
    return ASIAN_AFFINE_FAMILY_OK;
}
