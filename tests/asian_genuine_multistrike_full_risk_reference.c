#include "asian_genuine_multistrike_full_risk_reference.h"

#include <math.h>
#include <string.h>

static double normal_cdf(double x)
{
    return 0.5 * erfc(-x * 0.707106781186547524400844362104849039);
}

static double normal_pdf(double x)
{
    return exp(-0.5 * x * x) * 0.398942280401432677939946059934381868;
}

void asian_msfr_ref_targeted(const double *x, uint32_t n,
                             double s0, double rate, double dividend_yield,
                             double sigma, double maturity,
                             asian_msfr_ref_basis_t *out)
{
    memset(out, 0, sizeof(*out));
    double s = s0, q = 0.0, l = 0.0;
    double cumulative_x = 0.0, rho_weighted = 0.0, x_weighted = 0.0;
    for (uint32_t k = 0; k < n; ++k) {
        s *= exp(x[k]);
        q += s;
        l = fma((double)(n - k) / n, x[k], l);
        cumulative_x += x[k];
        rho_weighted = fma(k + 1.0, s, rho_weighted);
        x_weighted = fma(s, cumulative_x, x_weighted);
    }
    const double dt = maturity / n;
    const double c = rate - dividend_yield + 0.5 * sigma * sigma;
    const double b = maturity * (n + 1.0) / (2.0 * n);
    out->a = q / n;
    out->a_delta = out->a / s0;
    out->a_rho = dt * rho_weighted / n;
    out->a_vega = (x_weighted / n - c * out->a_rho) / sigma;
    out->l = l;
    out->g = s0 * exp(l);
    out->g_delta = out->g / s0;
    out->g_rho = out->g * b;
    out->g_vega = out->g * (l - c * b) / sigma;
}

void asian_msfr_ref_sample(const asian_msfr_ref_basis_t *b,
                           double strike, double rate, double maturity,
                           int put, int cv, asian_msfr_ref_value_t *out)
{
    const double sign = put ? -1.0 : 1.0;
    const double discount = exp(-rate * maturity);
    const double am = sign * (b->a - strike);
    const double gm = sign * (b->g - strike);
    const double ai = am > 0.0 ? sign : 0.0;
    const double gi = cv && gm > 0.0 ? sign : 0.0;
    const double ap = am > 0.0 ? am : 0.0;
    const double gp = cv && gm > 0.0 ? gm : 0.0;
    out->price = discount * (ap - gp);
    out->delta = discount * (ai * b->a_delta - gi * b->g_delta);
    out->vega = discount * (ai * b->a_vega - gi * b->g_vega);
    out->rho = discount * (ai * b->a_rho - gi * b->g_rho) -
               maturity * out->price;
}

void asian_msfr_ref_geometric_exact(double s0, double strike, double rate,
                                    double dividend_yield, double sigma,
                                    double maturity, uint32_t n,
                                    asian_msfr_ref_value_t *call,
                                    asian_msfr_ref_value_t *put)
{
    const double dn = n, dt = maturity / dn;
    const double b = maturity * (dn + 1.0) / (2.0 * dn);
    const double sum_t = dt * dn * (dn + 1.0) * 0.5;
    const double sum_min = dn * (dn + 1.0) * (2.0 * dn + 1.0) / 6.0;
    const double variance_scale = dt * sum_min / (dn * dn);
    const double variance = sigma * sigma * variance_scale;
    const double mean = log(s0) +
        (rate - dividend_yield - 0.5 * sigma * sigma) * sum_t / dn;
    const double discount = exp(-rate * maturity);
    const double root = sqrt(variance);
    const double forward = exp(mean + 0.5 * variance);
    if (sigma == 0.0) {
        const double call_active = forward > strike ? 1.0 : 0.0;
        const double put_active = forward < strike ? 1.0 : 0.0;
        call->price = discount * (call_active ? forward - strike : 0.0);
        put->price = discount * (put_active ? strike - forward : 0.0);
        call->delta = discount * call_active * forward / s0;
        put->delta = -discount * put_active * forward / s0;
        call->vega = 0.0;
        put->vega = 0.0;
        call->rho = discount * call_active * forward * b -
                    maturity * call->price;
        put->rho = -discount * put_active * forward * b -
                   maturity * put->price;
        return;
    }
    const double d2 = (mean - log(strike)) / root;
    const double d1 = d2 + root;
    const double nd1 = normal_cdf(d1), nd2 = normal_cdf(d2);
    const double nmd1 = normal_cdf(-d1), nmd2 = normal_cdf(-d2);
    const double pdf1 = normal_pdf(d1);
    call->price = discount * (forward * nd1 - strike * nd2);
    put->price = discount * (strike * nmd2 - forward * nmd1);
    const double cm = discount * forward * nd1;
    const double pm = -discount * forward * nmd1;
    const double cv = 0.5 * discount * forward * (nd1 + pdf1 / root);
    const double pv = 0.5 * discount * forward * (-nmd1 + pdf1 / root);
    const double mean_sigma = -sigma * b;
    const double variance_sigma = 2.0 * sigma * variance_scale;
    call->delta = cm / s0;
    put->delta = pm / s0;
    call->vega = cm * mean_sigma + cv * variance_sigma;
    put->vega = pm * mean_sigma + pv * variance_sigma;
    call->rho = cm * b - maturity * call->price;
    put->rho = pm * b - maturity * put->price;
}

void asian_msfr_ref_arithmetic_parity(double s0, double strike, double rate,
                                      double dividend_yield, double maturity,
                                      uint32_t n,
                                      asian_msfr_ref_value_t *parity)
{
    const double dt = maturity / n;
    double expected_a = 0.0, expected_a_rho = 0.0;
    for (uint32_t k = 0; k < n; ++k) {
        const double t = (k + 1.0) * dt;
        const double expected_s = s0 * exp((rate - dividend_yield) * t);
        expected_a += expected_s;
        expected_a_rho += t * expected_s;
    }
    expected_a /= n;
    expected_a_rho /= n;
    const double discount = exp(-rate * maturity);
    parity->price = discount * (expected_a - strike);
    parity->delta = discount * expected_a / s0;
    parity->vega = 0.0;
    parity->rho = discount *
        (expected_a_rho - maturity * (expected_a - strike));
}
