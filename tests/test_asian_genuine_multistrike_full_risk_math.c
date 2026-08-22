#define _POSIX_C_SOURCE 200112L
#include "asian_genuine_multistrike_full_risk_reference.h"
#include "private/asian_genuine_multistrike_full_risk_diag.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *a64(size_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes) != 0) return NULL;
    memset(p, 0, bytes);
    return p;
}

static int close_enough(double a, double b, double tolerance)
{
    return fabs(a - b) <= tolerance;
}

int main(void)
{
    const double s0 = 100.0, rate = 0.03, q = 0.0, sigma = 0.20, t = 1.0;
    const uint32_t ns[] = {2, 3, 16, 127, 256};
    const float strikes[] = {70.0f, 100.0f, 100.5f, 130.0f};
    asian_genuine_msfr_strike_controls_t *controls = a64(sizeof(*controls));
    if (controls == NULL) return 2;
    double max_control_error = 0.0, max_parity_error = 0.0;

    for (uint32_t ni = 0; ni < sizeof(ns) / sizeof(ns[0]); ++ni) {
        const uint32_t n = ns[ni];
        if (asian_genuine_msfr_prepare_strikes(controls, s0, rate, q, sigma,
              t, n, strikes, 4) != ASIAN_GENUINE_MSFR_OK) return 2;
        for (uint32_t i = 0; i < 4u; ++i) {
            asian_msfr_ref_value_t call, put, parity;
            asian_msfr_ref_geometric_exact(s0, strikes[i], rate, q, sigma,
                                           t, n, &call, &put);
            asian_msfr_ref_arithmetic_parity(s0, strikes[i], rate, q, t, n,
                                             &parity);
            const asian_genuine_msfr_strike_t *record = &controls->strikes[i];
            const asian_msfr_ref_value_t *direct =
                record->flags & ASIAN_GENUINE_MSFR_DIRECT_CALL ? &call : &put;
            const double *dv = (const double *)direct;
            const double *pv = (const double *)&parity;
            for (uint32_t field = 0; field < 4u; ++field) {
                const double ce = fabs(record->geometric_direct[field] - dv[field]);
                if (ce > max_control_error) max_control_error = ce;
                const double got_parity =
                    record->call_adjust[field] - record->put_adjust[field];
                const double pe = fabs(got_parity - pv[field]);
                if (pe > max_parity_error) max_parity_error = pe;
            }
        }

        double x[256];
        const double dt = t / n;
        for (uint32_t k = 0; k < n; ++k)
            x[k] = (rate - q - 0.5 * sigma * sigma) * dt +
                   sigma * sqrt(dt) * sin((double)(k + 1u));
        asian_msfr_ref_basis_t basis;
        asian_msfr_ref_targeted(x, n, s0, rate, q, sigma, t, &basis);
        const double h = 1e-5;
        asian_msfr_ref_basis_t up, down;
        asian_msfr_ref_targeted(x, n, s0 + h, rate, q, sigma, t, &up);
        asian_msfr_ref_targeted(x, n, s0 - h, rate, q, sigma, t, &down);
        if (!close_enough((up.a - down.a) / (2.0 * h), basis.a_delta, 1e-8) ||
            !close_enough((up.g - down.g) / (2.0 * h), basis.g_delta, 1e-8))
            return 1;
    }

    for (uint32_t n = 2; n <= 256u; ++n) {
        asian_genuine_msfr_basis_controls_t *basis_controls =
            a64(sizeof(*basis_controls));
        if (basis_controls == NULL) return 2;
        if (asian_genuine_msfr_prepare_basis_controls(basis_controls, s0, rate,
              q, sigma, t, n) != ASIAN_GENUINE_MSFR_OK ||
            basis_controls->fixing_count != n ||
            asian_genuine_msfr_producer_fixing_count(n) == 0u) return 1;
        free(basis_controls);
    }

    {
        const double rates[] = {-0.02, 0.0, 0.03};
        const double sigmas[] = {0.05, 0.20, 0.50};
        const float arbitrary[] = {nextafterf(100.0f, 0.0f), 83.25f,
                                   nextafterf(100.0f, INFINITY), 141.0f};
        for (uint32_t ri = 0; ri < 3u; ++ri)
            for (uint32_t si = 0; si < 3u; ++si)
                if (asian_genuine_msfr_prepare_strikes(
                      controls, s0, rates[ri], q, sigmas[si], t, 17u,
                      arbitrary, 4u) != ASIAN_GENUINE_MSFR_OK)
                    return 1;

        const double b = t * 17.0 / 32.0;
        const float deterministic = (float)(s0 * exp(rate * b));
        const float sigma_zero_strikes[] = {
            nextafterf(deterministic, 0.0f), deterministic,
            nextafterf(deterministic, INFINITY)};
        for (uint32_t i = 0; i < 3u; ++i) {
            asian_msfr_ref_value_t call, put;
            asian_msfr_ref_geometric_exact(s0, sigma_zero_strikes[i], rate,
                                           q, 0.0, t, 16u, &call, &put);
            const double *cv = (const double *)&call;
            const double *pv = (const double *)&put;
            for (uint32_t field = 0; field < 4u; ++field)
                if (!isfinite(cv[field]) || !isfinite(pv[field])) return 1;
            if (call.vega != 0.0 || put.vega != 0.0) return 1;
        }
    }

    float invalid_strikes[33] = {100.0f};
    for (uint32_t i = 1; i < 33u; ++i) invalid_strikes[i] = 100.0f + i;
    if (asian_genuine_msfr_prepare_strikes(controls, s0, rate, q, sigma, t,
          1, strikes, 4) != ASIAN_GENUINE_MSFR_FIXING_COUNT_UNSUPPORTED ||
        asian_genuine_msfr_prepare_strikes(controls, s0, rate, q, sigma, t,
          257, strikes, 4) != ASIAN_GENUINE_MSFR_FIXING_COUNT_UNSUPPORTED ||
        asian_genuine_msfr_prepare_strikes(controls, s0, rate, q, 0.0, t,
          16, strikes, 4) != ASIAN_GENUINE_MSFR_SIGMA_ZERO_UNSUPPORTED ||
        asian_genuine_msfr_prepare_strikes(controls, s0, rate, q, sigma, t,
          16, strikes, 0) != ASIAN_GENUINE_MSFR_STRIKE_COUNT_UNSUPPORTED ||
        asian_genuine_msfr_prepare_strikes(controls, s0, rate, q, sigma, t,
          16, invalid_strikes, 33) !=
          ASIAN_GENUINE_MSFR_STRIKE_COUNT_UNSUPPORTED)
        return 1;
    invalid_strikes[0] = NAN;
    if (asian_genuine_msfr_prepare_strikes(controls, s0, rate, q, sigma, t,
          16, invalid_strikes, 1) != ASIAN_GENUINE_MSFR_INVALID) return 1;
    invalid_strikes[0] = -1.0f;
    if (asian_genuine_msfr_prepare_strikes(controls, s0, rate, q, sigma, t,
          16, invalid_strikes, 1) != ASIAN_GENUINE_MSFR_INVALID) return 1;

    printf("asian_genuine_multistrike_full_risk_math=PASS "
           "max_control_error=%.9g max_parity_error=%.9g domain_N=2..256\n",
           max_control_error, max_parity_error);
    free(controls);
    return max_control_error <= 1e-13 && max_parity_error <= 1e-13 ? 0 : 1;
}
