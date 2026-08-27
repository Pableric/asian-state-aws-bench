#ifndef ASIAN_FULL_RISK_GAMMA_DIAG_H
#define ASIAN_FULL_RISK_GAMMA_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_affine_route_family_diag.h"

#define ASIAN_FULL_RISK_GAMMA_REQUEST_MAGIC UINT32_C(0x47465241) /* ARFG */
#define ASIAN_FULL_RISK_GAMMA_TRIPLE_MAGIC UINT32_C(0x54524641) /* AFRT */

enum asian_full_risk_gamma_status {
    ASIAN_FULL_RISK_GAMMA_OK = 0,
    ASIAN_FULL_RISK_GAMMA_INVALID = -1,
    ASIAN_FULL_RISK_GAMMA_UNSUPPORTED_BUMP = -2,
    ASIAN_FULL_RISK_GAMMA_DOMAIN = -3,
};

enum asian_full_risk_gamma_workload {
    ASIAN_FULL_RISK_CURRENT_PRICE_DELTA_VEGA_RHO = 0,
    ASIAN_FULL_RISK_PRICE_DELTA_GAMMA_VEGA_RHO = 1,
};

typedef struct {
    asian_affine_family_request_input_t family;
    double gamma_bump_fraction;
    enum asian_full_risk_gamma_workload workload;
} asian_full_risk_gamma_request_input_t;

/* The qualified 64-byte Phase-1 context remains at byte zero. */
typedef struct __attribute__((aligned(64))) {
    asian_genuine_aad_phase1_context_t phase1;
    float effective_spot_bump;
    float future_weight;
    double reciprocal_bump_square;
    uint32_t completed_fixings;
    uint32_t total_fixings;
    uint8_t reserved[40];
} asian_full_risk_gamma_hot_context_t;

_Static_assert(offsetof(asian_full_risk_gamma_hot_context_t, phase1) == 0,
               "qualified Phase-1 context prefix");
_Static_assert(offsetof(asian_full_risk_gamma_hot_context_t,
                        effective_spot_bump) == 64,
               "Gamma bump hot ABI");
_Static_assert(offsetof(asian_full_risk_gamma_hot_context_t,
                        reciprocal_bump_square) == 72,
               "Gamma reciprocal-square hot ABI");
_Static_assert(sizeof(asian_full_risk_gamma_hot_context_t) == 128,
               "two-line Gamma hot context");

typedef struct {
    asian_genuine_aad_phase1_value_t parent;
    double gamma;
} asian_full_risk_gamma_direct_value_t;

typedef struct {
    double price;
    double delta;
    double gamma;
    double vega;
    double rho;
    double effective_spot_bump;
} asian_full_risk_gamma_side_value_t;

typedef struct __attribute__((aligned(64))) {
    asian_full_risk_gamma_side_value_t call;
    asian_full_risk_gamma_side_value_t put;
    uint8_t reserved[32];
} asian_full_risk_gamma_output_t;

_Static_assert(sizeof(asian_full_risk_gamma_output_t) == 128,
               "two-line two-sided Gamma result");

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_route_t routes[ASIAN_META_DIRECTIONS];
    asian_genuine_aad_phase1_controls_t controls;
    asian_full_risk_gamma_hot_context_t context;
    asian_genuine_msfr_strike_t parity;
    double requested_bump_fraction;
    double original_strike;
    double initial_arithmetic_sum;
    uint32_t magic;
    uint8_t reserved[28];
} asian_full_risk_gamma_request_t;

typedef struct __attribute__((aligned(64))) {
    asian_full_risk_gamma_request_t request[3];
    double effective_spot_bump;
    double reciprocal_bump_square;
    uint32_t magic;
    uint8_t reserved[44];
} asian_full_risk_gamma_triple_request_t;

int asian_full_risk_gamma_request_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_full_risk_gamma_request_input_t *input,
    asian_full_risk_gamma_request_t *request);

int asian_full_risk_gamma_prepared_price(
    const asian_full_risk_gamma_request_t *request,
    asian_full_risk_gamma_output_t *output);

/* Diagnostic-only oracles.  They are never referenced by the timed leaf. */
int asian_full_risk_gamma_scalar_oracle(
    const asian_full_risk_gamma_request_t *request,
    asian_full_risk_gamma_output_t *output);
int asian_full_risk_gamma_parent_fields_oracle(
    const asian_full_risk_gamma_request_t *request,
    asian_full_risk_gamma_output_t *output);
int asian_full_risk_gamma_scalar_path_averages(
    const asian_full_risk_gamma_request_t *request,
    float base[ASIAN_AFFINE_FAMILY_PATHS],
    float plus[ASIAN_AFFINE_FAMILY_PATHS],
    float minus[ASIAN_AFFINE_FAMILY_PATHS]);
int asian_full_risk_gamma_triple_reprice(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_full_risk_gamma_request_input_t *input,
    asian_full_risk_gamma_output_t *output);
int asian_full_risk_gamma_triple_request_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_full_risk_gamma_request_input_t *input,
    asian_full_risk_gamma_triple_request_t *request);
int asian_full_risk_gamma_triple_prepared_price(
    const asian_full_risk_gamma_triple_request_t *request,
    asian_full_risk_gamma_output_t *output);

void asian_full_risk_gamma_affine_call_impl_diag(
    const asian_full_risk_gamma_hot_context_t *,
    asian_full_risk_gamma_direct_value_t *);
void asian_full_risk_gamma_affine_put_impl_diag(
    const asian_full_risk_gamma_hot_context_t *,
    asian_full_risk_gamma_direct_value_t *);

/* Linked test instrumentation; absent from the native timing executable. */
void asian_full_risk_gamma_leaf_counter_reset(void);
uint32_t asian_full_risk_gamma_leaf_counter_read(void);

#endif
