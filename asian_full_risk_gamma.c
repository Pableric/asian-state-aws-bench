#include "private/asian_full_risk_gamma_diag.h"

#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef void (*parent_phase1_leaf_t)(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);
typedef void (*gamma_phase1_leaf_t)(
    const asian_full_risk_gamma_hot_context_t *,
    asian_full_risk_gamma_direct_value_t *);

void asian_affine_family_full_risk_k1_affine_call_impl_diag(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);
void asian_affine_family_full_risk_k1_affine_put_impl_diag(
    const asian_genuine_aad_phase1_context_t *,
    asian_genuine_aad_phase1_value_t *);

static float gamma_forward_tape_sentinel[16] __attribute__((aligned(64)));

static int same_double(double left, double right)
{
    uint64_t a, b;
    memcpy(&a, &left, sizeof(a));
    memcpy(&b, &right, sizeof(b));
    return a == b;
}

static int supported_fraction(double value)
{
    return value == 0.005 || value == 0.01 || value == 0.02;
}

static int input_matches_carrier(
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_full_risk_gamma_request_input_t *input)
{
    const asian_affine_family_request_input_t *f =
        input == NULL ? NULL : &input->family;
    return carrier != NULL &&
        carrier->magic == ASIAN_AFFINE_FAMILY_XGROWTH_MAGIC &&
        f != NULL && input->workload ==
            ASIAN_FULL_RISK_PRICE_DELTA_GAMMA_VEGA_RHO &&
        isfinite(f->s0) && f->s0 > 0.0 && f->strikes != NULL &&
        f->strike_count == 1u && isfinite(f->strikes[0]) &&
        f->strikes[0] > 0.0f &&
        f->future_fixings >= ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS &&
        f->future_fixings <= ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS &&
        f->completed_fixings <= 256u - f->future_fixings &&
        isfinite(f->initial_arithmetic_sum) &&
        isfinite(f->past_log_sum) &&
        f->future_fixings == carrier->market.future_fixings &&
        same_double(f->rate, carrier->market.rate) &&
        same_double(f->dividend_yield, carrier->market.dividend_yield) &&
        same_double(f->sigma, carrier->market.sigma) &&
        same_double(f->maturity, carrier->market.maturity);
}

static int validate_domain(const asian_affine_family_request_input_t *input)
{
    const uint32_t n = input->future_fixings;
    const double sigma = input->sigma;
    if (sigma == 0.0)
        return ASIAN_FULL_RISK_GAMMA_DOMAIN;
    if (!isfinite(sigma) || sigma < 0.0 || !isfinite(input->rate) ||
        !isfinite(input->dividend_yield) || !isfinite(input->maturity) ||
        input->maturity <= 0.0)
        return ASIAN_FULL_RISK_GAMMA_INVALID;

    const double dt = input->maturity / (double)n;
    const float drift = (float)((input->rate - input->dividend_yield -
                                 0.5 * sigma * sigma) * dt);
    const float diffusion = (float)(sigma * sqrt(dt));
    if (drift < ORDERED_D1_DIAG_MIN_DRIFT ||
        drift > ORDERED_D1_DIAG_MAX_DRIFT || diffusion < 0.0f ||
        diffusion > ORDERED_D1_DIAG_MAX_ALPHA)
        return ASIAN_FULL_RISK_GAMMA_DOMAIN;

    const double b = input->maturity * ((double)n + 1.0) /
                     (2.0 * (double)n);
    const double center = log(input->s0) +
        (input->rate - input->dividend_yield - 0.5 * sigma * sigma) * b;
    const double radius = sigma * sqrt(dt) * 6.5 *
                          ((double)n + 1.0) * 0.5;
    return (float)(center - radius) < -87.0f ||
           (float)(center + radius) > 88.0f ?
        ASIAN_FULL_RISK_GAMMA_DOMAIN : ASIAN_FULL_RISK_GAMMA_OK;
}

static int prepare_seasoned_parity(
    asian_genuine_msfr_strike_t *record,
    const asian_affine_family_request_input_t *input)
{
    const uint32_t future = input->future_fixings;
    const uint32_t total = future + input->completed_fixings;
    const double dt = input->maturity / (double)future;
    const double carry = input->rate - input->dividend_yield;
    double expected_future_sum = 0.0;
    double expected_future_rho_sum = 0.0;
    for (uint32_t fixing = 1u; fixing <= future; ++fixing) {
        const double time = dt * (double)fixing;
        const double expected_s = input->s0 * exp(carry * time);
        expected_future_sum += expected_s;
        expected_future_rho_sum += time * expected_s;
    }
    const double expected_a =
        (input->initial_arithmetic_sum + expected_future_sum) /
        (double)total;
    const double expected_a_delta =
        expected_future_sum / ((double)total * input->s0);
    const double expected_a_rho = expected_future_rho_sum / (double)total;
    const double strike = input->strikes[0];
    const double discount = exp(-input->rate * input->maturity);
    const int direct_call = strike >= expected_a;
    const double parity[ASIAN_GENUINE_MSFR_RISK_FIELDS] = {
        discount * (expected_a - strike),
        discount * expected_a_delta,
        0.0,
        discount * (expected_a_rho -
                    input->maturity * (expected_a - strike)),
    };

    memset(record, 0, sizeof(*record));
    record->strike = input->strikes[0];
    record->direct_sign = direct_call ? 1.0f : -1.0f;
    memcpy(&record->strike_bits, &record->strike, sizeof(record->strike));
    if (direct_call)
        record->flags |= ASIAN_GENUINE_MSFR_DIRECT_CALL;
    if (strike < expected_a)
        record->flags |= ASIAN_GENUINE_MSFR_CALL_ITM;
    else if (strike > expected_a)
        record->flags |= ASIAN_GENUINE_MSFR_CALL_OTM;
    else
        record->flags |= ASIAN_GENUINE_MSFR_CALL_ATM;
    for (uint32_t field = 0; field < ASIAN_GENUINE_MSFR_RISK_FIELDS;
         ++field) {
        record->call_adjust[field] = direct_call ? 0.0 : parity[field];
        record->put_adjust[field] = direct_call ? -parity[field] : 0.0;
    }
    return 0;
}

static int prepare_hot_context(
    asian_full_risk_gamma_hot_context_t *out,
    const asian_meta_affine_route_t *routes,
    const asian_genuine_aad_phase1_controls_t *controls,
    const asian_affine_family_request_input_t *input,
    float effective_strike, float effective_bump,
    double reciprocal_bump_square)
{
    const uint32_t future = input->future_fixings;
    const uint32_t total = future + input->completed_fixings;
    const double dt = input->maturity / (double)future;
    const float discount = (float)exp(-input->rate * input->maturity);
    const float future_weight = (float)future / (float)total;
    asian_genuine_aad_phase1_context_t *hot = &out->phase1;

    memset(out, 0, sizeof(*out));
    hot->routes = (const asian_genuine_route_t *)(const void *)routes;
    hot->s_tape = gamma_forward_tape_sentinel;
    hot->controls = controls;
    hot->fixing_count = future;
    hot->route_count = future - 1u;
    hot->s0 = (float)input->s0;
    hot->strike = effective_strike;
    hot->inv_n = 1.0f / (float)future;
    hot->dt_over_n = (float)(dt / (double)future);
    hot->c = (float)(input->rate - input->dividend_yield +
                     0.5 * input->sigma * input->sigma);
    hot->inv_sigma = (float)(1.0 / input->sigma);
    hot->inv_s0 = (float)(1.0 / input->s0);
    hot->discount = discount * future_weight;
    out->effective_spot_bump = effective_bump;
    out->future_weight = future_weight;
    out->reciprocal_bump_square = reciprocal_bump_square;
    out->completed_fixings = input->completed_fixings;
    out->total_fixings = total;
    return 0;
}

__attribute__((noinline, used))
int asian_full_risk_gamma_request_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_full_risk_gamma_request_input_t *input,
    asian_full_risk_gamma_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_AFFINE_FAMILY_ENGINE_MAGIC ||
        engine->affine_plan == NULL || request == NULL ||
        ((uintptr_t)request & 63u) != 0u ||
        !input_matches_carrier(carrier, input))
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    request->magic = 0u;
    if (!isfinite(input->gamma_bump_fraction) ||
        !supported_fraction(input->gamma_bump_fraction))
        return ASIAN_FULL_RISK_GAMMA_UNSUPPORTED_BUMP;
    const int domain = validate_domain(&input->family);
    if (domain != ASIAN_FULL_RISK_GAMMA_OK)
        return domain;

    const double bump_product =
        input->gamma_bump_fraction * input->family.s0;
    const float effective_bump = (float)bump_product;
    if (!isfinite(effective_bump) || !(effective_bump > 0.0f) ||
        !(input->family.s0 - (double)effective_bump > 0.0))
        return ASIAN_FULL_RISK_GAMMA_DOMAIN;
    const double bump_double = (double)effective_bump;
    const double reciprocal_bump_square =
        1.0 / (bump_double * bump_double);
    if (!isfinite(reciprocal_bump_square) ||
        !(reciprocal_bump_square > 0.0))
        return ASIAN_FULL_RISK_GAMMA_DOMAIN;

    const uint32_t future = input->family.future_fixings;
    const uint32_t total = future + input->family.completed_fixings;
    const double effective_strike_double =
        ((double)total * (double)input->family.strikes[0] -
         input->family.initial_arithmetic_sum) / (double)future;
    const float effective_strike = (float)effective_strike_double;
    if (!isfinite(effective_strike))
        return ASIAN_FULL_RISK_GAMMA_DOMAIN;

    if (asian_meta_affine_routes_bind(engine->affine_plan, carrier->x,
            carrier->growth, future, request->routes) != 0)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    if (asian_genuine_aad_phase1_prepare_arithmetic_controls(
            &request->controls, input->family.s0,
            input->family.strikes[0], input->family.rate,
            input->family.dividend_yield, input->family.sigma,
            input->family.maturity, future) !=
            ASIAN_GENUINE_AAD_PHASE1_OK)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    if (input->family.completed_fixings == 0u) {
        if (asian_genuine_msfr_prepare_arithmetic_strike(&request->parity,
                input->family.s0, input->family.rate,
                input->family.dividend_yield, input->family.sigma,
                input->family.maturity, future,
                input->family.strikes[0]) != ASIAN_GENUINE_MSFR_OK)
            return ASIAN_FULL_RISK_GAMMA_INVALID;
    } else if (prepare_seasoned_parity(&request->parity,
                                       &input->family) != 0) {
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    }
    prepare_hot_context(&request->context, request->routes,
        &request->controls, &input->family, effective_strike, effective_bump,
        reciprocal_bump_square);
    request->requested_bump_fraction = input->gamma_bump_fraction;
    request->original_strike = input->family.strikes[0];
    request->initial_arithmetic_sum = input->family.initial_arithmetic_sum;
    request->magic = ASIAN_FULL_RISK_GAMMA_REQUEST_MAGIC;
    return ASIAN_FULL_RISK_GAMMA_OK;
}

static void parity_finalize(
    const asian_genuine_msfr_strike_t *parity,
    const asian_genuine_aad_phase1_value_t *direct,
    double gamma, double bump, asian_full_risk_gamma_output_t *output)
{
    const double *values = (const double *)direct;
    double *call_fields[] = {
        &output->call.price, &output->call.delta,
        &output->call.vega, &output->call.rho,
    };
    double *put_fields[] = {
        &output->put.price, &output->put.delta,
        &output->put.vega, &output->put.rho,
    };
    for (uint32_t field = 0; field < ASIAN_GENUINE_MSFR_RISK_FIELDS;
         ++field) {
        const double sum = 0.0 + (values[field] - 0.0) * 4096.0;
        const double normalized = sum * (1.0 / 4096.0);
        *call_fields[field] = normalized + parity->call_adjust[field];
        *put_fields[field] = normalized + parity->put_adjust[field];
    }
    output->call.gamma = gamma;
    output->put.gamma = gamma;
    output->call.effective_spot_bump = bump;
    output->put.effective_spot_bump = bump;
    memset(output->reserved, 0, sizeof(output->reserved));
}

__attribute__((noinline, used))
int asian_full_risk_gamma_prepared_price(
    const asian_full_risk_gamma_request_t *request,
    asian_full_risk_gamma_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_FULL_RISK_GAMMA_REQUEST_MAGIC ||
        ((uintptr_t)output & 63u) != 0u)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    const int direct_call =
        (request->parity.flags & ASIAN_GENUINE_MSFR_DIRECT_CALL) != 0u;
    const gamma_phase1_leaf_t leaf = direct_call ?
        asian_full_risk_gamma_affine_call_impl_diag :
        asian_full_risk_gamma_affine_put_impl_diag;
    asian_full_risk_gamma_direct_value_t direct;
    leaf(&request->context, &direct);
    parity_finalize(&request->parity, &direct.parent, direct.gamma,
        (double)request->context.effective_spot_bump, output);
    return ASIAN_FULL_RISK_GAMMA_OK;
}

static uint32_t routed_offset(const asian_meta_affine_route_t *route,
                              uint32_t packet, uint32_t half, uint32_t lane)
{
    const asian_meta_dim_affine_ctx_t *map = route->map;
    uint32_t line = map->sel2[packet][0];
    uint32_t control = (uint32_t)map->sel2[packet][1] ^
                       map->base_control[lane];
    if (half != 0u) {
        line ^= 1u;
        control ^= map->half_delta[lane];
    }
    return line * 16u + (control & 15u);
}

static float reduce_grouped(const float acc[2][16])
{
    float merged[16];
    for (uint32_t lane = 0; lane < 16u; ++lane)
        merged[lane] = acc[0][lane] + acc[1][lane];
    float first[4], second[4];
    for (uint32_t lane = 0; lane < 4u; ++lane) {
        first[lane] = merged[lane] + merged[4u + lane];
        second[lane] = merged[8u + lane] + merged[12u + lane];
        first[lane] = first[lane] + second[lane];
    }
    first[0] = first[0] + first[2];
    first[1] = first[1] + first[3];
    return first[0] + first[1];
}

static void scalar_direct(
    const asian_full_risk_gamma_request_t *request, int put,
    asian_full_risk_gamma_direct_value_t *out)
{
    float accum[6][2][16] = {{{0.0f}}};
    const asian_genuine_aad_phase1_context_t *context =
        &request->context.phase1;
    const uint32_t n = context->fixing_count;
    for (uint32_t packet = 0; packet < 128u; ++packet) {
        for (uint32_t half = 0; half < 2u; ++half) {
            for (uint32_t lane = 0; lane < 16u; ++lane) {
                const uint32_t path = packet * 32u + half * 16u + lane;
                float s = context->s0;
                float q = 0.0f, xsum = 0.0f;
                float arho = 0.0f, xdot = 0.0f;
                for (uint32_t fixing = 0; fixing < n; ++fixing) {
                    const asian_meta_affine_route_t *route =
                        &request->routes[fixing];
                    const uint32_t offset = fixing == 0u ? path :
                        routed_offset(route, packet, half, lane);
                    const float x = route->x_base[offset];
                    const float growth = route->growth_base[offset];
                    s = s * growth;
                    q = q + s;
                    xsum = xsum + x;
                    arho = fmaf(request->controls.forward_weights[fixing],
                                 s, arho);
                    xdot = fmaf(s, xsum, xdot);
                }
                const float a = q * context->inv_n;
                const float a_rho = arho * context->dt_over_n;
                float a_vega = xdot * context->inv_n;
                a_vega = fmaf(-context->c, a_rho, a_vega);
                a_vega = a_vega * context->inv_sigma;
                const float a_delta = a * context->inv_s0;
                const float plus = fmaf(request->context.effective_spot_bump,
                                        a_delta, a);
                const float minus = fmaf(-request->context.effective_spot_bump,
                                         a_delta, a);
                const float sign = put ? -1.0f : 1.0f;
                const float raw_base = sign * (a - context->strike);
                const float raw_plus = sign * (plus - context->strike);
                const float raw_minus = sign * (minus - context->strike);
                const int active = raw_base > 0.0f;
                const float price =
                    (active ? raw_base : 0.0f) * context->discount;
                const float delta =
                    (active ? sign * a_delta : 0.0f) * context->discount;
                const float vega =
                    (active ? sign * a_vega : 0.0f) * context->discount;
                float rho =
                    (active ? sign * a_rho : 0.0f) * context->discount;
                rho = fmaf(-request->controls.maturity, price, rho);
                const float plus_price =
                    (raw_plus > 0.0f ? raw_plus : 0.0f) * context->discount;
                const float minus_price =
                    (raw_minus > 0.0f ? raw_minus : 0.0f) * context->discount;
                const float values[6] = {
                    price, delta, vega, rho, plus_price, minus_price};
                for (uint32_t field = 0; field < 6u; ++field)
                    accum[field][half][lane] =
                        accum[field][half][lane] + values[field];
            }
        }
    }
    double reduced[6];
    for (uint32_t field = 0; field < 6u; ++field)
        reduced[field] = (double)reduce_grouped(accum[field]) *
                         (1.0 / 4096.0);
    out->parent.price = reduced[0];
    out->parent.delta = reduced[1];
    out->parent.vega = reduced[2];
    out->parent.rho = reduced[3];
    out->gamma = ((reduced[4] - 2.0 * reduced[0]) + reduced[5]) *
                 request->context.reciprocal_bump_square;
}

int asian_full_risk_gamma_scalar_path_averages(
    const asian_full_risk_gamma_request_t *request,
    float base[ASIAN_AFFINE_FAMILY_PATHS],
    float plus[ASIAN_AFFINE_FAMILY_PATHS],
    float minus[ASIAN_AFFINE_FAMILY_PATHS])
{
    if (request == NULL || base == NULL || plus == NULL || minus == NULL ||
        request->magic != ASIAN_FULL_RISK_GAMMA_REQUEST_MAGIC)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    const asian_genuine_aad_phase1_context_t *context =
        &request->context.phase1;
    for (uint32_t packet = 0; packet < 128u; ++packet)
        for (uint32_t half = 0; half < 2u; ++half)
            for (uint32_t lane = 0; lane < 16u; ++lane) {
                const uint32_t path = packet * 32u + half * 16u + lane;
                float s = context->s0, sum = 0.0f;
                for (uint32_t fixing = 0; fixing < context->fixing_count;
                     ++fixing) {
                    const asian_meta_affine_route_t *route =
                        &request->routes[fixing];
                    const uint32_t offset = fixing == 0u ? path :
                        routed_offset(route, packet, half, lane);
                    s = s * route->growth_base[offset];
                    sum = sum + s;
                }
                const float a = sum * context->inv_n;
                const float tangent = a * context->inv_s0;
                base[path] = a;
                plus[path] = fmaf(request->context.effective_spot_bump,
                                  tangent, a);
                minus[path] = fmaf(-request->context.effective_spot_bump,
                                   tangent, a);
            }
    return ASIAN_FULL_RISK_GAMMA_OK;
}

int asian_full_risk_gamma_scalar_oracle(
    const asian_full_risk_gamma_request_t *request,
    asian_full_risk_gamma_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_FULL_RISK_GAMMA_REQUEST_MAGIC ||
        ((uintptr_t)output & 63u) != 0u)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    const int direct_call =
        (request->parity.flags & ASIAN_GENUINE_MSFR_DIRECT_CALL) != 0u;
    asian_full_risk_gamma_direct_value_t direct;
    scalar_direct(request, !direct_call, &direct);
    parity_finalize(&request->parity, &direct.parent, direct.gamma,
        (double)request->context.effective_spot_bump, output);
    return ASIAN_FULL_RISK_GAMMA_OK;
}

__attribute__((noinline, used))
int asian_full_risk_gamma_parent_fields_oracle(
    const asian_full_risk_gamma_request_t *request,
    asian_full_risk_gamma_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_FULL_RISK_GAMMA_REQUEST_MAGIC ||
        ((uintptr_t)output & 63u) != 0u)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    const int direct_call =
        (request->parity.flags & ASIAN_GENUINE_MSFR_DIRECT_CALL) != 0u;
    const parent_phase1_leaf_t leaf = direct_call ?
        asian_affine_family_full_risk_k1_affine_call_impl_diag :
        asian_affine_family_full_risk_k1_affine_put_impl_diag;
    asian_genuine_aad_phase1_value_t direct;
    leaf(&request->context.phase1, &direct);
    parity_finalize(&request->parity, &direct, 0.0,
        (double)request->context.effective_spot_bump, output);
    return ASIAN_FULL_RISK_GAMMA_OK;
}

int asian_full_risk_gamma_triple_reprice(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_full_risk_gamma_request_input_t *input,
    asian_full_risk_gamma_output_t *output)
{
    if (output == NULL || ((uintptr_t)output & 63u) != 0u || input == NULL)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    asian_full_risk_gamma_triple_request_t request
        __attribute__((aligned(64)));
    if (asian_full_risk_gamma_triple_request_prepare(engine, carrier, input,
            &request) != ASIAN_FULL_RISK_GAMMA_OK)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    return asian_full_risk_gamma_triple_prepared_price(&request, output);
}

__attribute__((noinline, used))
int asian_full_risk_gamma_triple_request_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_full_risk_gamma_request_input_t *input,
    asian_full_risk_gamma_triple_request_t *request)
{
    if (request == NULL || ((uintptr_t)request & 63u) != 0u || input == NULL)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    request->magic = 0u;
    const float h = (float)(input->gamma_bump_fraction * input->family.s0);
    if (!isfinite(h) || !(h > 0.0f))
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    const double spots[3] = {
        input->family.s0 - (double)h,
        input->family.s0,
        input->family.s0 + (double)h,
    };
    for (uint32_t index = 0; index < 3u; ++index) {
        asian_full_risk_gamma_request_input_t bumped = *input;
        bumped.family.s0 = spots[index];
        if (asian_full_risk_gamma_request_prepare(engine, carrier, &bumped,
                &request->request[index]) != ASIAN_FULL_RISK_GAMMA_OK)
            return ASIAN_FULL_RISK_GAMMA_INVALID;
    }
    request->effective_spot_bump = (double)h;
    request->reciprocal_bump_square = 1.0 / ((double)h * (double)h);
    memset(request->reserved, 0, sizeof(request->reserved));
    request->magic = ASIAN_FULL_RISK_GAMMA_TRIPLE_MAGIC;
    return ASIAN_FULL_RISK_GAMMA_OK;
}

__attribute__((noinline, used))
int asian_full_risk_gamma_triple_prepared_price(
    const asian_full_risk_gamma_triple_request_t *request,
    asian_full_risk_gamma_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_FULL_RISK_GAMMA_TRIPLE_MAGIC ||
        ((uintptr_t)output & 63u) != 0u)
        return ASIAN_FULL_RISK_GAMMA_INVALID;
    asian_full_risk_gamma_output_t values[3]
        __attribute__((aligned(64)));
    for (uint32_t index = 0; index < 3u; ++index)
        if (asian_full_risk_gamma_parent_fields_oracle(
                &request->request[index], &values[index]) !=
                ASIAN_FULL_RISK_GAMMA_OK)
            return ASIAN_FULL_RISK_GAMMA_INVALID;
    *output = values[1];
    output->call.gamma =
        ((values[2].call.price - 2.0 * values[1].call.price) +
         values[0].call.price) * request->reciprocal_bump_square;
    output->put.gamma =
        ((values[2].put.price - 2.0 * values[1].put.price) +
         values[0].put.price) * request->reciprocal_bump_square;
    output->call.effective_spot_bump = request->effective_spot_bump;
    output->put.effective_spot_bump = request->effective_spot_bump;
    return ASIAN_FULL_RISK_GAMMA_OK;
}
