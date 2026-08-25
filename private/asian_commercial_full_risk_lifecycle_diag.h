#ifndef ASIAN_COMMERCIAL_FULL_RISK_LIFECYCLE_DIAG_H
#define ASIAN_COMMERCIAL_FULL_RISK_LIFECYCLE_DIAG_H

#include "asian_affine_route_family_diag.h"

/* Historical two-leaf diagnostic retained outside commercial timing. */
void asian_commercial_full_risk_independent_call_plus_put_diag(
    const asian_affine_family_full_risk_k1_request_t *request,
    asian_affine_family_full_risk_k1_output_t *output);

#endif
