#ifndef ASIAN_GENUINE_MULTISTRIKE_FULL_RISK_HYBRID_DISPATCH_DIAG_H
#define ASIAN_GENUINE_MULTISTRIKE_FULL_RISK_HYBRID_DISPATCH_DIAG_H

#include <stdint.h>

#include "asian_genuine_aad_phase1_diag.h"
#include "asian_genuine_multistrike_full_risk_diag.h"

/* Private, untimed planning record used by qualification and reporting. */
typedef struct {
    uint8_t phase1_calls;
    uint8_t tile2_calls;
    uint8_t tile4_calls;
    uint8_t padded_outputs;
} asian_genuine_msfr_hybrid_plan_t;

int asian_genuine_msfr_hybrid_plan_diag(
    uint32_t strike_count, asian_genuine_msfr_hybrid_plan_t *plan);

/*
 * Small dispatcher outside every ranked leaf.
 *
 * K=1 consumes phase1_context through the already-qualified direct-side
 * Phase-1 forward full-risk leaf and converts its single-block result back to
 * the existing streaming raw-sum accumulator representation.  For K>=2,
 * basis is consumed by the frozen tile-2/tile-4 leaves in the planned groups.
 * Exactly one block/path-count update is made after all groups succeed.
 */
int asian_genuine_msfr_hybrid_consume_block_diag(
    const asian_genuine_msfr_basis_t *basis,
    const asian_genuine_msfr_consumer_context_t *context,
    enum asian_genuine_msfr_estimator estimator,
    const asian_genuine_aad_phase1_context_t *phase1_context,
    asian_genuine_msfr_accumulator_t *accumulator);

#endif
