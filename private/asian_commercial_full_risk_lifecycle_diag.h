#ifndef ASIAN_COMMERCIAL_FULL_RISK_LIFECYCLE_DIAG_H
#define ASIAN_COMMERCIAL_FULL_RISK_LIFECYCLE_DIAG_H

#include "asian_affine_route_family_diag.h"
#include "asian_genuine_aad_phase1_diag.h"

#define ASIAN_COMMERCIAL_FULL_RISK_REQUEST_MAGIC UINT32_C(0x52464341)

typedef struct __attribute__((aligned(64))) {
    asian_genuine_aad_phase1_value_t call;
    asian_genuine_aad_phase1_value_t put;
} asian_commercial_full_risk_output_t;

_Static_assert(sizeof(asian_commercial_full_risk_output_t) == 64,
               "one cache-line full-risk result");

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_route_t routes[ASIAN_META_DIRECTIONS];
    asian_genuine_aad_phase1_controls_t controls;
    asian_genuine_aad_phase1_context_t context;
    float s_tape[ASIAN_GENUINE_AAD_PHASE1_TAPE_FLOATS];
    uint32_t magic;
    uint8_t reserved[60];
} asian_commercial_full_risk_request_t;

int asian_commercial_full_risk_request_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_commercial_full_risk_request_t *request);

int asian_commercial_full_risk_prepared_price(
    const asian_commercial_full_risk_request_t *request,
    asian_commercial_full_risk_output_t *output);

void asian_commercial_full_risk_affine_call_diag(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);
void asian_commercial_full_risk_affine_put_diag(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);

#endif
