#include "private/asian_geometric_cv_diag.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static double normal_cdf(double x)
{
    return .5 * erfc(-x * 0.707106781186547524400844362104849039);
}

static double exact_payoff(double mean, double variance, double strike,
                           double discount, int type)
{
    if (variance == 0.0) {
        const double g = exp(mean);
        return discount * (type == ASIAN_GEOMETRIC_CALL ?
            fmax(g - strike, 0.0) : fmax(strike - g, 0.0));
    }
    const double root = sqrt(variance);
    const double forward = exp(mean + .5 * variance);
    const double d2 = (mean - log(strike)) / root;
    const double d1 = d2 + root;
    if (type == ASIAN_GEOMETRIC_CALL)
        return discount * (forward * normal_cdf(d1) - strike * normal_cdf(d2));
    return discount * (strike * normal_cdf(-d2) - forward * normal_cdf(-d1));
}

int asian_geometric_cv_prepare(
    asian_geometric_cv_context_t *out,
    double s0, double strike, double rate, double dividend_yield,
    double sigma, double maturity,
    uint32_t future_fixings, uint32_t completed_fixings,
    double initial_q, double past_log_sum, int option_type)
{
    if (out == NULL || ((uintptr_t)out & 63u) != 0u ||
        !(s0 > 0.0) || !(strike > 0.0) || !isfinite(rate) ||
        !isfinite(dividend_yield) || !(sigma >= 0.0) ||
        !(maturity > 0.0) || future_fixings == 0 || future_fixings > 256 ||
        completed_fixings > UINT32_MAX - future_fixings ||
        !isfinite(initial_q) || !isfinite(past_log_sum) ||
        (option_type != ASIAN_GEOMETRIC_CALL && option_type != ASIAN_GEOMETRIC_PUT)) {
        if (out != NULL) out->magic = 0;
        return -1;
    }
    const uint32_t total = completed_fixings + future_fixings;
    const double dt = maturity / future_fixings;
    const double mu = rate - dividend_yield - .5 * sigma * sigma;
    const double n = future_fixings;
    const double m = total;
    const double sum_t = dt * n * (n + 1.0) * .5;
    const double sum_min = n * (n + 1.0) * (2.0 * n + 1.0) / 6.0;
    const double mean = (past_log_sum + n * log(s0) + mu * sum_t) / m;
    const double variance = sigma * sigma * dt * sum_min / (m * m);
    const double discount = exp(-rate * maturity);
    const double l0 = (past_log_sum - completed_fixings * log(s0)) / m;
    const double weight_sum = n * (n + 1.0) / (2.0 * m);
    const double center = log(s0) + l0 + mu * dt * weight_sum;
    const double radius = sigma * sqrt(dt) * 6.5 * weight_sum;
    const float bound_lo = (float)(center - radius);
    const float bound_hi = (float)(center + radius);
    /* The unchecked vector leaf deliberately supports normal float outputs only. */
    if (!(bound_lo >= -87.0f) || !(bound_hi <= 88.0f)) {
        out->magic = 0;
        return -2;
    }

    memset(out, 0, sizeof(*out));
    out->abi_version = ASIAN_GEOMETRIC_CV_ABI_VERSION;
    out->option_type = (uint16_t)option_type;
    out->future_fixings = future_fixings;
    out->completed_fixings = completed_fixings;
    out->total_fixings = total;
    out->inv_total = 1.0f / (float)total;
    out->strike = (float)strike;
    out->discount = (float)discount;
    out->log_s0 = logf((float)s0);
    out->payoff_sign = option_type == ASIAN_GEOMETRIC_CALL ? 1.0f : -1.0f;
    out->initial_l = (float)l0;
    out->exp_input_min = -87.0f;
    out->exp_input_max = 88.0f;
    out->geometric_exact = exact_payoff(mean, variance, strike, discount, option_type);
    out->log_mean = mean;
    out->log_variance = variance;
    out->initial_q = initial_q;
    out->past_log_sum = past_log_sum;
    out->magic = ASIAN_GEOMETRIC_CV_MAGIC;
    return 0;
}

int asian_geometric_cv_exp_preflight(
    const asian_geometric_cv_context_t *context,
    const float *l, size_t count,
    float *observed_min, float *observed_max)
{
    if (context == NULL || l == NULL || count == 0 ||
        context->magic != ASIAN_GEOMETRIC_CV_MAGIC)
        return -1;
    float lo = INFINITY, hi = -INFINITY;
    for (size_t i = 0; i < count; ++i) {
        const float value = context->log_s0 + l[i];
        if (!isfinite(value) || value < context->exp_input_min ||
            value > context->exp_input_max)
            return -2;
        if (value < lo) lo = value;
        if (value > hi) hi = value;
    }
    if (observed_min != NULL) *observed_min = lo;
    if (observed_max != NULL) *observed_max = hi;
    return 0;
}
