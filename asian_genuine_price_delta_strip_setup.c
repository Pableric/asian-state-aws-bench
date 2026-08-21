#include "private/asian_genuine_price_delta_strip_diag.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static double normal_cdf(double x)
{
    return 0.5 * erfc(-x * 0.707106781186547524400844362104849039);
}

static void geometric_exact(double mean, double variance, double strike,
                            double discount, double dmean_ds0,
                            double *call, double *put,
                            double *call_delta, double *put_delta)
{
    if (variance == 0.0) {
        const double g = exp(mean);
        *call = discount * fmax(g - strike, 0.0);
        *put = discount * fmax(strike - g, 0.0);
        *call_delta = g > strike ? discount * g * dmean_ds0 : 0.0;
        *put_delta = g < strike ? -discount * g * dmean_ds0 : 0.0;
        return;
    }
    const double root = sqrt(variance);
    const double forward = exp(mean + 0.5 * variance);
    const double d2 = (mean - log(strike)) / root;
    const double d1 = d2 + root;
    *call = discount * (forward * normal_cdf(d1) - strike * normal_cdf(d2));
    *put = discount * (strike * normal_cdf(-d2) - forward * normal_cdf(-d1));
    *call_delta = discount * forward * normal_cdf(d1) * dmean_ds0;
    *put_delta = -discount * forward * normal_cdf(-d1) * dmean_ds0;
}

static int supported_strike_count(uint32_t n)
{
    return n == 1 || n == 4 || n == 8 || n == 16 || n == 32;
}

int asian_genuine_strip_fixed_strikes(uint32_t n, float out[32])
{
    static const float all[32] = {
        70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,
        100.5f,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130
    };
    static const uint8_t i1[1] = {15};
    static const uint8_t i4[4] = {5,15,16,26};
    static const uint8_t i8[8] = {0,5,10,15,16,21,26,31};
    static const uint8_t i16[16] = {0,3,5,7,10,12,14,15,16,17,19,21,24,26,28,31};
    const uint8_t *indices = 0;
    if (out == 0) return -1;
    switch (n) {
    case 1: indices = i1; break;
    case 4: indices = i4; break;
    case 8: indices = i8; break;
    case 16: indices = i16; break;
    case 32: memcpy(out, all, sizeof all); return 0;
    default: return -1;
    }
    for (uint32_t i = 0; i < n; ++i) out[i] = all[indices[i]];
    return 0;
}

int asian_genuine_strip_prepare(
    asian_genuine_strip_context_t *out,
    double s0, double rate, double dividend_yield, double sigma, double maturity,
    uint32_t future_fixings, uint32_t completed_fixings,
    double initial_q, double past_log_sum,
    const float *strikes, uint32_t strike_count)
{
    if (out == 0 || ((uintptr_t)out & 63u) != 0u || strikes == 0 ||
        !(s0 > 0.0) || !isfinite(rate) || !isfinite(dividend_yield) ||
        !(sigma >= 0.0) || !(maturity > 0.0) ||
        future_fixings == 0 || future_fixings > 256 ||
        completed_fixings > 256 - future_fixings ||
        !isfinite(initial_q) || !isfinite(past_log_sum) ||
        !supported_strike_count(strike_count)) {
        if (out != 0) out->magic = 0;
        return -1;
    }
    float fixed[32];
    if (asian_genuine_strip_fixed_strikes(strike_count, fixed)) return -1;
    for (uint32_t i = 0; i < strike_count; ++i) {
        uint32_t got, expected;
        memcpy(&got, &strikes[i], 4);
        memcpy(&expected, &fixed[i], 4);
        if (got != expected) { out->magic = 0; return -1; }
    }

    const uint32_t total = completed_fixings + future_fixings;
    const double f = future_fixings, m = total;
    const double dt = maturity / f;
    const double carry = rate - dividend_yield;
    const double mu = carry - 0.5 * sigma * sigma;
    const double sum_t = dt * f * (f + 1.0) * 0.5;
    const double sum_min = f * (f + 1.0) * (2.0 * f + 1.0) / 6.0;
    const double log_base = past_log_sum / m + (f / m) * log(s0);
    const double log_mean = log_base + mu * sum_t / m;
    const double log_variance = sigma * sigma * dt * sum_min / (m * m);
    const double discount = exp(-rate * maturity);
    double expected_q_future = 0.0;
    double expected_q_delta = 0.0;
    for (uint32_t j = 1; j <= future_fixings; ++j) {
        const double growth = exp(carry * dt * j);
        expected_q_future += s0 * growth;
        expected_q_delta += growth;
    }
    const double expected_a = (initial_q + expected_q_future) / m;
    const double expected_a_delta = expected_q_delta / m;
    const double weight_sum = f * (f + 1.0) / (2.0 * m);
    const double center = log_base + mu * dt * weight_sum;
    const double radius = sigma * sqrt(dt) * 6.5 * weight_sum;
    const float bound_lo = (float)(center - radius);
    const float bound_hi = (float)(center + radius);
    if (!(bound_lo >= -87.0f) || !(bound_hi <= 88.0f)) {
        out->magic = 0;
        return -2;
    }

    memset(out, 0, sizeof *out);
    out->abi_version = ASIAN_GENUINE_STRIP_ABI_VERSION;
    out->strike_count = (uint16_t)strike_count;
    out->future_fixings = future_fixings;
    out->completed_fixings = completed_fixings;
    out->total_fixings = total;
    out->inv_total = 1.0f / (float)total;
    out->initial_q = (float)initial_q;
    out->discount = (float)discount;
    out->delta_q_scale = (float)(discount / (m * s0));
    out->delta_g_scale = (float)(discount * f / (m * s0));
    out->log_base = (float)log_base;
    out->expected_arithmetic = (float)expected_a;
    out->expected_arithmetic_delta = (float)(discount * expected_a_delta);
    out->exp_input_min = -87.0f;
    out->exp_input_max = 88.0f;

    uint32_t nearest = 0;
    double nearest_distance = DBL_MAX;
    for (uint32_t i = 0; i < strike_count; ++i) {
        const double strike = strikes[i];
        double cg, pg, cdg, pdg;
        geometric_exact(log_mean, log_variance, strike, discount,
                        f / (m * s0), &cg, &pg, &cdg, &pdg);
        const double price_parity = discount * (expected_a - strike);
        const double delta_parity = discount * expected_a_delta;
        const int direct_call = strike >= expected_a;
        asian_genuine_strip_strike_t *record = &out->strikes[i];
        record->strike = strikes[i];
        record->direct_sign = direct_call ? 1.0f : -1.0f;
        record->geometric_price_exact_direct = direct_call ? cg : pg;
        record->geometric_delta_exact_direct = direct_call ? cdg : pdg;
        record->call_price_adjust = direct_call ? 0.0 : price_parity;
        record->put_price_adjust = direct_call ? -price_parity : 0.0;
        record->call_delta_adjust = direct_call ? 0.0 : delta_parity;
        record->put_delta_adjust = direct_call ? -delta_parity : 0.0;
        memcpy(&record->strike_bits, &strikes[i], 4);
        record->flags = direct_call ? ASIAN_GENUINE_STRIP_DIRECT_CALL : 0;
        if (strike < expected_a) record->flags |= ASIAN_GENUINE_STRIP_CALL_ITM;
        else if (strike > expected_a) record->flags |= ASIAN_GENUINE_STRIP_CALL_OTM;
        else record->flags |= ASIAN_GENUINE_STRIP_CALL_ATM;
        const double distance = fabs(strike - expected_a);
        if (distance < nearest_distance) { nearest = i; nearest_distance = distance; }
    }
    out->strikes[nearest].flags |= ASIAN_GENUINE_STRIP_NEAREST_ATM;
    out->magic = ASIAN_GENUINE_STRIP_MAGIC;
    return 0;
}

int asian_genuine_strip_exp_preflight(
    const asian_genuine_strip_context_t *context,
    const float l[ASIAN_GENUINE_STRIP_PATHS],
    float *observed_min, float *observed_max)
{
    if (context == 0 || l == 0 || context->magic != ASIAN_GENUINE_STRIP_MAGIC)
        return -1;
    float lo = INFINITY, hi = -INFINITY;
    for (size_t i = 0; i < ASIAN_GENUINE_STRIP_PATHS; ++i) {
        const float value = context->log_base + l[i];
        if (!isfinite(value) || value < context->exp_input_min ||
            value > context->exp_input_max) return -2;
        if (value < lo) lo = value;
        if (value > hi) hi = value;
    }
    if (observed_min) *observed_min = lo;
    if (observed_max) *observed_max = hi;
    return 0;
}

typedef void (*price_leaf_t)(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);

int asian_genuine_strip_price_diag(
    const float q[ASIAN_GENUINE_STRIP_PATHS],
    const float g[ASIAN_GENUINE_STRIP_PATHS],
    const asian_genuine_strip_context_t *context,
    enum asian_genuine_strip_estimator estimator, uint32_t tile,
    asian_genuine_strip_output_t *output)
{
    if (q == 0 || g == 0 || context == 0 || output == 0 ||
        ((uintptr_t)q & 63u) || ((uintptr_t)g & 63u) ||
        ((uintptr_t)output & 63u) || context->magic != ASIAN_GENUINE_STRIP_MAGIC ||
        (estimator != ASIAN_GENUINE_STRIP_ARITHMETIC &&
         estimator != ASIAN_GENUINE_STRIP_GEOMETRIC_CV) ||
        (tile != 4 && tile != 8)) return -1;
    const uint32_t n = context->strike_count;
    if (n == 1) {
        price_leaf_t leaf = estimator == ASIAN_GENUINE_STRIP_ARITHMETIC ?
            asian_genuine_strip_arithmetic_price_1_diag : asian_genuine_strip_cv_price_1_diag;
        leaf(q, g, context, context->strikes, output->values);
        return 0;
    }
    uint32_t i = 0;
    if (tile == 8) {
        price_leaf_t leaf = estimator == ASIAN_GENUINE_STRIP_ARITHMETIC ?
            asian_genuine_strip_arithmetic_price_8_diag : asian_genuine_strip_cv_price_8_diag;
        for (; i + 8 <= n; i += 8)
            leaf(q, g, context, context->strikes + i, output->values + i);
    }
    price_leaf_t leaf = estimator == ASIAN_GENUINE_STRIP_ARITHMETIC ?
        asian_genuine_strip_arithmetic_price_4_diag : asian_genuine_strip_cv_price_4_diag;
    for (; i < n; i += 4)
        leaf(q, g, context, context->strikes + i, output->values + i);
    return 0;
}

int asian_genuine_strip_price_delta_diag(
    const float q[ASIAN_GENUINE_STRIP_PATHS],
    const float g[ASIAN_GENUINE_STRIP_PATHS],
    const asian_genuine_strip_context_t *context,
    enum asian_genuine_strip_estimator estimator, uint32_t tile,
    asian_genuine_strip_output_t *output)
{
    if (q == 0 || g == 0 || context == 0 || output == 0 ||
        ((uintptr_t)q & 63u) || ((uintptr_t)g & 63u) ||
        ((uintptr_t)output & 63u) || context->magic != ASIAN_GENUINE_STRIP_MAGIC ||
        (estimator != ASIAN_GENUINE_STRIP_ARITHMETIC &&
         estimator != ASIAN_GENUINE_STRIP_GEOMETRIC_CV) ||
        (tile != 4 && tile != 8)) return -1;
    const uint32_t n = context->strike_count;
    if (n == 1) {
        price_leaf_t leaf = estimator == ASIAN_GENUINE_STRIP_ARITHMETIC ?
            asian_genuine_strip_arithmetic_price_delta_1_diag :
            asian_genuine_strip_cv_price_delta_1_diag;
        leaf(q, g, context, context->strikes, output->values);
        return 0;
    }
    uint32_t i = 0;
    if (tile == 8) {
        price_leaf_t leaf = estimator == ASIAN_GENUINE_STRIP_ARITHMETIC ?
            asian_genuine_strip_arithmetic_price_delta_8_diag :
            asian_genuine_strip_cv_price_delta_8_diag;
        for (; i + 8 <= n; i += 8)
            leaf(q, g, context, context->strikes + i, output->values + i);
    }
    price_leaf_t leaf = estimator == ASIAN_GENUINE_STRIP_ARITHMETIC ?
        asian_genuine_strip_arithmetic_price_delta_4_diag :
        asian_genuine_strip_cv_price_delta_4_diag;
    for (; i < n; i += 4)
        leaf(q, g, context, context->strikes + i, output->values + i);
    return 0;
}
