#ifndef ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_STRIP_ADAPTER_H
#define ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_STRIP_ADAPTER_H

#include "asian_genuine_arithmetic_growth_only_diag.h"

enum asian_genuine_arithmetic_growth_only_immediate_status {
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_USED = 0,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_FALLBACK = 1,
    ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_INVALID = -1,
};

int asian_genuine_arithmetic_growth_only_strip_prepare_padded(
    asian_genuine_strip_context_t *out,
    double s0, double rate, double dividend_yield, double sigma,
    double maturity, uint32_t future_fixings, uint32_t completed_fixings,
    double initial_q, double past_log_sum,
    const float *strikes, uint32_t strike_count,
    uint32_t *padded_count);

int asian_genuine_arithmetic_growth_only_strip_consume_padded(
    const float q_future[ASIAN_GENUINE_STRIP_PATHS],
    const float aligned_unused_g[ASIAN_GENUINE_STRIP_PATHS],
    const asian_genuine_strip_context_t *context,
    uint32_t requested_count, int price_delta,
    asian_genuine_strip_output_t *output);

/*
 * Fixed dispatch only: K=1, tile 2 for K=2, and tile 4 for K=3/4.  K=3 is
 * already padded by prepare_padded.  A rejected tile-4 price+Delta leaf and
 * every K>=5 shape return IMMEDIATE_FALLBACK for Stage-1 handling.
 */
int asian_genuine_arithmetic_growth_only_immediate_consume(
    const asian_genuine_arithmetic_growth_only_context_t *growth_context,
    const asian_genuine_strip_context_t *strip_context,
    uint32_t requested_count, int price_delta,
    int tile4_price_delta_accepted,
    asian_genuine_strip_output_t *output);

#endif
