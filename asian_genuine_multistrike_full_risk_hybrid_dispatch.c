#include "private/asian_genuine_multistrike_full_risk_hybrid_dispatch_diag.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef void (*consumer_leaf_t)(
    const asian_genuine_msfr_basis_t *,
    const asian_genuine_msfr_consumer_context_t *,
    const asian_genuine_msfr_strike_t *, double (*)[4]);

typedef void (*phase1_leaf_t)(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);

int asian_genuine_msfr_hybrid_plan_diag(
    uint32_t strike_count, asian_genuine_msfr_hybrid_plan_t *plan)
{
    if (plan == NULL || strike_count == 0u ||
        strike_count > ASIAN_GENUINE_MSFR_MAX_STRIKES)
        return ASIAN_GENUINE_MSFR_STRIKE_COUNT_UNSUPPORTED;
    memset(plan, 0, sizeof(*plan));
    if (strike_count == 1u) {
        plan->phase1_calls = 1u;
        return ASIAN_GENUINE_MSFR_OK;
    }
    if (strike_count == 2u) {
        plan->tile2_calls = 1u;
        return ASIAN_GENUINE_MSFR_OK;
    }
    if (strike_count <= 4u) {
        plan->tile4_calls = 1u;
        plan->padded_outputs = (uint8_t)(4u - strike_count);
        return ASIAN_GENUINE_MSFR_OK;
    }

    plan->tile4_calls = (uint8_t)(strike_count / 4u);
    const uint32_t remainder = strike_count & 3u;
    if (remainder == 1u) {
        plan->tile2_calls = 1u;
        plan->padded_outputs = 1u;
    } else if (remainder == 2u) {
        plan->tile2_calls = 1u;
    } else if (remainder == 3u) {
        plan->tile4_calls += 1u;
        plan->padded_outputs = 1u;
    }
    return ASIAN_GENUINE_MSFR_OK;
}

static int validate_common(
    const asian_genuine_msfr_consumer_context_t *context,
    enum asian_genuine_msfr_estimator estimator,
    const asian_genuine_msfr_accumulator_t *accumulator)
{
    if (context == NULL || ((uintptr_t)context & 63u) != 0u ||
        context->controls == NULL ||
        context->controls->magic != ASIAN_GENUINE_MSFR_STRIKE_MAGIC ||
        context->strike_count == 0u ||
        context->strike_count > ASIAN_GENUINE_MSFR_MAX_STRIKES ||
        context->strike_count != context->controls->strike_count ||
        context->fixing_count != context->controls->fixing_count ||
        context->padded_count_tile2 < context->strike_count ||
        context->padded_count_tile4 < context->strike_count ||
        context->padded_count_tile4 > ASIAN_GENUINE_MSFR_PADDED_STRIKES ||
        accumulator == NULL || ((uintptr_t)accumulator & 63u) != 0u ||
        accumulator->magic != ASIAN_GENUINE_MSFR_ACCUM_MAGIC ||
        accumulator->abi_version != ASIAN_GENUINE_MSFR_ABI_VERSION ||
        accumulator->estimator != (uint16_t)estimator ||
        accumulator->strike_count != context->strike_count ||
        accumulator->controls_magic != context->controls->magic ||
        accumulator->controls_identity != (uintptr_t)context->controls ||
        (estimator != ASIAN_GENUINE_MSFR_ARITHMETIC &&
         estimator != ASIAN_GENUINE_MSFR_GEOMETRIC_CV))
        return ASIAN_GENUINE_MSFR_ACCUMULATOR_MISMATCH;
    if (UINT64_MAX - accumulator->completed_path_count <
            ASIAN_GENUINE_MSFR_PATHS ||
        accumulator->completed_block_count == UINT64_MAX)
        return ASIAN_GENUINE_MSFR_INVALID;
    return ASIAN_GENUINE_MSFR_OK;
}

static int consume_phase1(
    const asian_genuine_msfr_consumer_context_t *context,
    enum asian_genuine_msfr_estimator estimator,
    const asian_genuine_aad_phase1_context_t *phase1_context,
    asian_genuine_msfr_accumulator_t *accumulator)
{
    const asian_genuine_msfr_strike_t *strike =
        &context->controls->strikes[0];
    if (phase1_context == NULL ||
        ((uintptr_t)phase1_context & 63u) != 0u ||
        phase1_context->controls == NULL ||
        phase1_context->controls->magic !=
            ASIAN_GENUINE_AAD_PHASE1_CONTROL_MAGIC ||
        phase1_context->controls->abi_version !=
            ASIAN_GENUINE_AAD_PHASE1_ABI_VERSION ||
        phase1_context->fixing_count != context->fixing_count ||
        phase1_context->route_count + 1u != context->fixing_count ||
        phase1_context->routes == NULL || phase1_context->s_tape == NULL ||
        memcmp(&phase1_context->strike, &strike->strike,
               sizeof(strike->strike)) != 0)
        return ASIAN_GENUINE_MSFR_INVALID;

    const int direct_call =
        (strike->flags & ASIAN_GENUINE_MSFR_DIRECT_CALL) != 0u;
    const asian_genuine_aad_phase1_value_t *phase_exact = direct_call ?
        &phase1_context->controls->geometric_call :
        &phase1_context->controls->geometric_put;
    if (memcmp(phase_exact, strike->geometric_direct,
               sizeof(strike->geometric_direct)) != 0)
        return ASIAN_GENUINE_MSFR_INVALID;

    phase1_leaf_t leaf;
    if (estimator == ASIAN_GENUINE_MSFR_ARITHMETIC)
        leaf = direct_call ?
            asian_genuine_aad_phase1_forward_arithmetic_call_diag :
            asian_genuine_aad_phase1_forward_arithmetic_put_diag;
    else
        leaf = direct_call ? asian_genuine_aad_phase1_forward_cv_call_diag :
                             asian_genuine_aad_phase1_forward_cv_put_diag;

    asian_genuine_aad_phase1_value_t value;
    leaf(phase1_context, &value);
    const double *direct = (const double *)&value;
    for (uint32_t field = 0; field < ASIAN_GENUINE_MSFR_RISK_FIELDS; ++field) {
        const double exact =
            estimator == ASIAN_GENUINE_MSFR_GEOMETRIC_CV ?
            strike->geometric_direct[field] : 0.0;
        accumulator->direct_sums[0][field] +=
            (direct[field] - exact) * 4096.0;
    }
    return ASIAN_GENUINE_MSFR_OK;
}

int asian_genuine_msfr_hybrid_consume_block_diag(
    const asian_genuine_msfr_basis_t *basis,
    const asian_genuine_msfr_consumer_context_t *context,
    enum asian_genuine_msfr_estimator estimator,
    const asian_genuine_aad_phase1_context_t *phase1_context,
    asian_genuine_msfr_accumulator_t *accumulator)
{
    int status = validate_common(context, estimator, accumulator);
    if (status != ASIAN_GENUINE_MSFR_OK) return status;

    const uint32_t strike_count = context->strike_count;
    if (strike_count == 1u) {
        status = consume_phase1(context, estimator, phase1_context,
                                accumulator);
        if (status != ASIAN_GENUINE_MSFR_OK) return status;
    } else {
        if (basis == NULL || ((uintptr_t)basis & 63u) != 0u)
            return ASIAN_GENUINE_MSFR_INVALID;
        consumer_leaf_t tile2, tile4;
        if (estimator == ASIAN_GENUINE_MSFR_ARITHMETIC) {
            tile2 = asian_genuine_msfr_arithmetic_tile2_diag;
            tile4 = asian_genuine_msfr_arithmetic_tile4_diag;
        } else {
            tile2 = asian_genuine_msfr_cv_tile2_diag;
            tile4 = asian_genuine_msfr_cv_tile4_diag;
        }

        uint32_t at = 0u;
        if (strike_count == 2u) {
            tile2(basis, context, context->controls->strikes,
                  accumulator->direct_sums);
            at = 2u;
        } else if (strike_count <= 4u) {
            tile4(basis, context, context->controls->strikes,
                  accumulator->direct_sums);
            at = strike_count;
        } else {
            while (strike_count - at >= 4u) {
                tile4(basis, context, &context->controls->strikes[at],
                      &accumulator->direct_sums[at]);
                at += 4u;
            }
            const uint32_t remainder = strike_count - at;
            if (remainder == 1u || remainder == 2u)
                tile2(basis, context, &context->controls->strikes[at],
                      &accumulator->direct_sums[at]);
            else if (remainder == 3u)
                tile4(basis, context, &context->controls->strikes[at],
                      &accumulator->direct_sums[at]);
            at += remainder;
        }
        if (at != strike_count) return ASIAN_GENUINE_MSFR_INVALID;
    }

    accumulator->completed_path_count += ASIAN_GENUINE_MSFR_PATHS;
    accumulator->completed_block_count += 1u;
    return ASIAN_GENUINE_MSFR_OK;
}
