#include "private/asian_commercial_full_risk_lifecycle_diag.h"

#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static int same_double(double left, double right)
{
    uint64_t a, b;
    memcpy(&a, &left, sizeof(a));
    memcpy(&b, &right, sizeof(b));
    return a == b;
}

static int input_matches_carrier(
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input)
{
    return carrier != NULL &&
        carrier->magic == ASIAN_AFFINE_FAMILY_XGROWTH_MAGIC &&
        input != NULL && isfinite(input->s0) && input->s0 > 0.0 &&
        input->strikes != NULL && input->strike_count == 1u &&
        input->completed_fixings == 0u &&
        input->future_fixings >= ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS &&
        input->future_fixings <= ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS &&
        input->future_fixings == carrier->market.future_fixings &&
        same_double(input->rate, carrier->market.rate) &&
        same_double(input->dividend_yield,
                    carrier->market.dividend_yield) &&
        same_double(input->sigma, carrier->market.sigma) &&
        same_double(input->maturity, carrier->market.maturity);
}

/* Private production-style counterpart of the qualified generic context
 * preparer.  Structural route qualification belongs to engine lifetime; this
 * request boundary performs only ordinary input/domain checks and scalar bind.
 */
static int prepare_hot_context(
    asian_genuine_aad_phase1_context_t *out,
    const asian_meta_affine_route_t *routes, float *s_tape,
    const asian_genuine_aad_phase1_controls_t *controls,
    const asian_affine_family_request_input_t *input)
{
    const uint32_t n = input->future_fixings;
    const double sigma = input->sigma;
    if (sigma == 0.0)
        return ASIAN_GENUINE_AAD_PHASE1_SIGMA_ZERO_UNSUPPORTED;
    if (!isfinite(sigma) || sigma < 0.0 || !isfinite(input->rate) ||
        !isfinite(input->dividend_yield) ||
        !isfinite(input->maturity) || input->maturity <= 0.0)
        return ASIAN_GENUINE_AAD_PHASE1_INVALID;

    const double dt = input->maturity / (double)n;
    const float drift = (float)((input->rate - input->dividend_yield -
                                 0.5 * sigma * sigma) * dt);
    const float diffusion = (float)(sigma * sqrt(dt));
    if (drift < ORDERED_D1_DIAG_MIN_DRIFT ||
        drift > ORDERED_D1_DIAG_MAX_DRIFT || diffusion < 0.0f ||
        diffusion > ORDERED_D1_DIAG_MAX_ALPHA)
        return ASIAN_GENUINE_AAD_PHASE1_PRODUCER_DOMAIN;

    const double b = input->maturity * ((double)n + 1.0) /
                     (2.0 * (double)n);
    const double center = log(input->s0) +
        (input->rate - input->dividend_yield - 0.5 * sigma * sigma) * b;
    const double radius = sigma * sqrt(dt) * 6.5 *
                          ((double)n + 1.0) * 0.5;
    if ((float)(center - radius) < -87.0f ||
        (float)(center + radius) > 88.0f)
        return ASIAN_GENUINE_AAD_PHASE1_EXP_DOMAIN;

    memset(out, 0, sizeof(*out));
    out->routes = (const asian_genuine_route_t *)(const void *)routes;
    out->s_tape = s_tape;
    out->controls = controls;
    out->fixing_count = n;
    out->route_count = n - 1u;
    out->s0 = (float)input->s0;
    out->strike = input->strikes[0];
    out->inv_n = 1.0f / (float)n;
    out->dt_over_n = (float)(dt / (double)n);
    out->c = (float)(input->rate - input->dividend_yield +
                     0.5 * sigma * sigma);
    out->inv_sigma = (float)(1.0 / sigma);
    out->inv_s0 = (float)(1.0 / input->s0);
    out->discount = (float)exp(-input->rate * input->maturity);
    return ASIAN_GENUINE_AAD_PHASE1_OK;
}

__attribute__((noinline, used))
int asian_commercial_full_risk_request_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_commercial_full_risk_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_AFFINE_FAMILY_ENGINE_MAGIC ||
        engine->affine_plan == NULL || request == NULL ||
        ((uintptr_t)request & 63u) != 0u ||
        !input_matches_carrier(carrier, input))
        return ASIAN_AFFINE_FAMILY_INVALID;

    const uint32_t n = input->future_fixings;
    if (asian_meta_affine_routes_bind(engine->affine_plan, carrier->x,
            carrier->growth, n, request->routes) != 0)
        return ASIAN_AFFINE_FAMILY_INVALID;
    if (asian_genuine_aad_phase1_prepare_controls(&request->controls,
            input->s0, input->strikes[0], input->rate,
            input->dividend_yield, input->sigma, input->maturity, n) !=
            ASIAN_GENUINE_AAD_PHASE1_OK)
        return ASIAN_AFFINE_FAMILY_INVALID;
    if (prepare_hot_context(&request->context, request->routes,
            request->s_tape, &request->controls, input) !=
            ASIAN_GENUINE_AAD_PHASE1_OK)
        return ASIAN_AFFINE_FAMILY_INVALID;
    request->magic = ASIAN_COMMERCIAL_FULL_RISK_REQUEST_MAGIC;
    return ASIAN_AFFINE_FAMILY_OK;
}

__attribute__((noinline, used))
int asian_commercial_full_risk_prepared_price(
    const asian_commercial_full_risk_request_t *request,
    asian_commercial_full_risk_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_COMMERCIAL_FULL_RISK_REQUEST_MAGIC ||
        ((uintptr_t)output & 63u) != 0u)
        return ASIAN_AFFINE_FAMILY_INVALID;
    memset(output, 0, sizeof(*output));
    asian_commercial_full_risk_affine_call_diag(&request->context,
                                                 &output->call);
    asian_commercial_full_risk_affine_put_diag(&request->context,
                                                &output->put);
    return ASIAN_AFFINE_FAMILY_OK;
}
