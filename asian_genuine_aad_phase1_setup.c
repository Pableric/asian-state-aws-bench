#include "private/asian_genuine_aad_phase1_diag.h"
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static double normal_cdf(double x)
{
    return 0.5 * erfc(-x * 0.707106781186547524400844362104849039);
}

static double normal_pdf(double x)
{
    return exp(-0.5 * x * x) * 0.398942280401432677939946059934381868;
}

static int d1_map_is_identity(const fragment_map_t *map)
{
    if (map == NULL || map->dimension != 1u) return 0;
    for (uint32_t packet = 0; packet < 128u; ++packet) {
        for (uint32_t half = 0; half < 2u; ++half) {
            const uint32_t line = map->select[packet][half];
            const uint32_t pattern = map->select[packet][2u + half];
            if (pattern >= map->pattern_count) return 0;
            for (uint32_t lane = 0; lane < 16u; ++lane) {
                const uint32_t got = line * 16u + map->patterns[pattern][lane];
                const uint32_t expected = packet * 32u + half * 16u + lane;
                if (got != expected) return 0;
            }
        }
    }
    return 1;
}

uint32_t asian_genuine_aad_phase1_producer_fixing_count(uint32_t n)
{
    switch (n) {
    case 16u: case 32u: case 64u: case 128u: case 256u: return n;
    default: return n >= ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS &&
                    n <= ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS ? 16u : 0u;
    }
}

static void geometric_exact(double s0, double strike, double rate,
                            double dividend_yield, double sigma,
                            double maturity, uint32_t fixing_count,
                            asian_genuine_aad_phase1_value_t *call,
                            asian_genuine_aad_phase1_value_t *put)
{
    const double n = fixing_count;
    const double b = maturity * (n + 1.0) / (2.0 * n);
    const double dt = maturity / n;
    const double sum_t = dt * n * (n + 1.0) * 0.5;
    const double sum_min = n * (n + 1.0) * (2.0*n + 1.0) / 6.0;
    const double variance_scale = dt * sum_min / (n*n);
    /* Match the qualified geometric-control price operation order exactly. */
    const double variance = sigma * sigma * dt * sum_min / (n*n);
    const double mean = (0.0 + n*log(s0) +
        (rate - dividend_yield - 0.5*sigma*sigma) * sum_t) / n;
    const double discount = exp(-rate*maturity);

    if (variance == 0.0) {
        const double g = exp(mean);
        const int call_itm = g > strike;
        const int put_itm = g < strike;
        call->price = discount * (call_itm ? g-strike : 0.0);
        put->price = discount * (put_itm ? strike-g : 0.0);
        call->delta = call_itm ? discount*g/s0 : 0.0;
        put->delta = put_itm ? -discount*g/s0 : 0.0;
        call->vega = 0.0;
        put->vega = 0.0;
        call->rho = call_itm ? discount*g*b - maturity*call->price : 0.0;
        put->rho = put_itm ? -discount*g*b - maturity*put->price : 0.0;
        return;
    }

    const double root = sqrt(variance);
    const double forward = exp(mean + 0.5*variance);
    const double d2 = (mean - log(strike)) / root;
    const double d1 = d2 + root;
    const double nd1 = normal_cdf(d1), nd2 = normal_cdf(d2);
    const double nmd1 = normal_cdf(-d1), nmd2 = normal_cdf(-d2);
    const double pdf1 = normal_pdf(d1);
    call->price = discount * (forward*nd1 - strike*nd2);
    put->price = discount * (strike*nmd2 - forward*nmd1);

    const double call_m = discount*forward*nd1;
    const double put_m = -discount*forward*nmd1;
    const double call_v = 0.5*discount*forward*(nd1 + pdf1/root);
    const double put_v = 0.5*discount*forward*(-nmd1 + pdf1/root);
    const double mean_sigma = -sigma*b;
    const double variance_sigma = 2.0*sigma*variance_scale;
    call->delta = call_m/s0;
    put->delta = put_m/s0;
    call->vega = call_m*mean_sigma + call_v*variance_sigma;
    put->vega = put_m*mean_sigma + put_v*variance_sigma;
    call->rho = call_m*b - maturity*call->price;
    put->rho = put_m*b - maturity*put->price;
}

int asian_genuine_aad_phase1_prepare_controls(
    asian_genuine_aad_phase1_controls_t *out,
    double s0, double strike, double rate, double dividend_yield,
    double sigma, double maturity, uint32_t n)
{
    if (n < ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS ||
        n > ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS) {
        if (out != NULL) out->magic = 0u;
        return ASIAN_GENUINE_AAD_PHASE1_FIXING_COUNT_UNSUPPORTED;
    }
    if (out == NULL || ((uintptr_t)out & 63u) != 0u || !(s0 > 0.0) ||
        !(strike > 0.0) || !isfinite(rate) || !isfinite(dividend_yield) ||
        !(sigma >= 0.0) || !(maturity > 0.0)) {
        if (out != NULL) out->magic = 0u;
        return ASIAN_GENUINE_AAD_PHASE1_INVALID;
    }
    memset(out, 0, sizeof(*out));
    out->abi_version = ASIAN_GENUINE_AAD_PHASE1_ABI_VERSION;
    out->fixing_count = n;
    out->maturity = (float)maturity;
    out->geometric_b = (float)(maturity*(n+1.0)/(2.0*n));
    out->discount = (float)exp(-rate*maturity);
    out->log_s0 = logf((float)s0);
    for (uint32_t k = 0; k < n; ++k) out->forward_weights[k] = (float)(k+1u);
    geometric_exact(s0,strike,rate,dividend_yield,sigma,maturity,n,
                    &out->geometric_call,&out->geometric_put);
    out->magic = ASIAN_GENUINE_AAD_PHASE1_CONTROL_MAGIC;
    return ASIAN_GENUINE_AAD_PHASE1_OK;
}

int asian_genuine_aad_phase1_prepare_context(
    asian_genuine_aad_phase1_context_t *out,
    const asian_genuine_route_t *routes, float *s_tape,
    const asian_genuine_aad_phase1_controls_t *controls,
    double s0, double strike, double rate, double dividend_yield,
    double sigma, double maturity, uint32_t n)
{
    if (n < ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS ||
        n > ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS)
        return ASIAN_GENUINE_AAD_PHASE1_FIXING_COUNT_UNSUPPORTED;
    if (out == NULL || ((uintptr_t)out & 63u) != 0u || routes == NULL ||
        ((uintptr_t)routes & 31u) != 0u || s_tape == NULL ||
        ((uintptr_t)s_tape & 63u) != 0u || controls == NULL ||
        ((uintptr_t)controls & 63u) != 0u ||
        controls->magic != ASIAN_GENUINE_AAD_PHASE1_CONTROL_MAGIC ||
        controls->abi_version != ASIAN_GENUINE_AAD_PHASE1_ABI_VERSION ||
        controls->fixing_count != n || !(s0 > 0.0) || !(strike > 0.0) ||
        !isfinite(rate) || !isfinite(dividend_yield) ||
        !isfinite(sigma) || !(maturity > 0.0)) {
        return ASIAN_GENUINE_AAD_PHASE1_INVALID;
    }
    if (sigma == 0.0) return ASIAN_GENUINE_AAD_PHASE1_SIGMA_ZERO_UNSUPPORTED;
    if (sigma < 0.0) return ASIAN_GENUINE_AAD_PHASE1_INVALID;

    const double dt = maturity/n;
    const float drift = (float)((rate-dividend_yield-0.5*sigma*sigma)*dt);
    const float diffusion = (float)(sigma*sqrt(dt));
    if (drift < ORDERED_D1_DIAG_MIN_DRIFT ||
        drift > ORDERED_D1_DIAG_MAX_DRIFT || diffusion < 0.0f ||
        diffusion > ORDERED_D1_DIAG_MAX_ALPHA) {
        return ASIAN_GENUINE_AAD_PHASE1_PRODUCER_DOMAIN;
    }
    const double b = maturity*(n+1.0)/(2.0*n);
    const double center = log(s0) +
        (rate-dividend_yield-0.5*sigma*sigma)*b;
    const double radius = sigma*sqrt(dt)*6.5*(n+1.0)*0.5;
    if ((float)(center-radius) < -87.0f || (float)(center+radius) > 88.0f)
        return ASIAN_GENUINE_AAD_PHASE1_EXP_DOMAIN;

    if (!d1_map_is_identity(routes[0].map) || routes[0].fixing_index != 0u ||
        routes[0].x_base == NULL || routes[0].growth_base == NULL)
        return ASIAN_GENUINE_AAD_PHASE1_INVALID;
    for (uint32_t k = 0; k < n; ++k) {
        float expected = (float)(n-k)/(float)n, got;
        memcpy(&got, &routes[k].weight_bits, sizeof(got));
        if (routes[k].x_base == NULL || routes[k].growth_base == NULL ||
            routes[k].map == NULL || ((uintptr_t)routes[k].map & 63u) != 0u ||
            routes[k].fixing_index != k || routes[k].map->dimension != k+1u ||
            memcmp(&got,&expected,sizeof(got)) != 0)
            return ASIAN_GENUINE_AAD_PHASE1_INVALID;
    }

    memset(out,0,sizeof(*out));
    out->routes=routes;
    out->s_tape=s_tape;
    out->controls=controls;
    out->fixing_count=n;
    out->route_count=n-1u;
    out->s0=(float)s0;
    out->strike=(float)strike;
    out->inv_n=1.0f/(float)n;
    out->dt_over_n=(float)(dt/n);
    out->c=(float)(rate-dividend_yield+0.5*sigma*sigma);
    out->inv_sigma=(float)(1.0/sigma);
    out->inv_s0=(float)(1.0/s0);
    out->discount=(float)exp(-rate*maturity);
    return ASIAN_GENUINE_AAD_PHASE1_OK;
}
