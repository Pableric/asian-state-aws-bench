#include "private/asian_genuine_multistrike_full_risk_diag.h"
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static double normal_cdf(double x)
{
    return 0.5 * erfc(-x * 0.707106781186547524400844362104849039);
}

static double normal_pdf(double x)
{
    return exp(-0.5 * x * x) * 0.398942280401432677939946059934381868;
}

static int supported_n(uint32_t n)
{
    return n >= ASIAN_GENUINE_MSFR_MIN_FIXINGS &&
           n <= ASIAN_GENUINE_MSFR_MAX_FIXINGS;
}

uint32_t asian_genuine_msfr_producer_fixing_count(uint32_t n)
{
    switch (n) {
    case 16u: case 32u: case 64u: case 128u: case 256u: return n;
    default: return supported_n(n) ? 16u : 0u;
    }
}

static int validate_contract(double s0, double rate, double dividend_yield,
                             double sigma, double maturity, uint32_t n)
{
    if (!supported_n(n)) return ASIAN_GENUINE_MSFR_FIXING_COUNT_UNSUPPORTED;
    if (!(s0 > 0.0) || !isfinite(rate) || !isfinite(dividend_yield) ||
        !isfinite(sigma) || !(maturity > 0.0))
        return ASIAN_GENUINE_MSFR_INVALID;
    if (sigma == 0.0) return ASIAN_GENUINE_MSFR_SIGMA_ZERO_UNSUPPORTED;
    if (sigma < 0.0) return ASIAN_GENUINE_MSFR_INVALID;

    const double dt = maturity / n;
    const float drift =
        (float)((rate - dividend_yield - 0.5 * sigma * sigma) * dt);
    const float diffusion = (float)(sigma * sqrt(dt));
    if (drift < ORDERED_D1_DIAG_MIN_DRIFT ||
        drift > ORDERED_D1_DIAG_MAX_DRIFT || diffusion < 0.0f ||
        diffusion > ORDERED_D1_DIAG_MAX_ALPHA)
        return ASIAN_GENUINE_MSFR_PRODUCER_DOMAIN;

    const double b = maturity * (n + 1.0) / (2.0 * n);
    const double center = log(s0) +
        (rate - dividend_yield - 0.5 * sigma * sigma) * b;
    const double radius = sigma * sqrt(dt) * 6.5 * (n + 1.0) * 0.5;
    if ((float)(center - radius) < -87.0f ||
        (float)(center + radius) > 88.0f)
        return ASIAN_GENUINE_MSFR_EXP_DOMAIN;
    return ASIAN_GENUINE_MSFR_OK;
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
                const uint32_t got =
                    line * 16u + map->patterns[pattern][lane];
                const uint32_t expected =
                    packet * 32u + half * 16u + lane;
                if (got != expected) return 0;
            }
        }
    }
    return 1;
}

int asian_genuine_msfr_prepare_basis_controls(
    asian_genuine_msfr_basis_controls_t *out,
    double s0, double rate, double dividend_yield, double sigma,
    double maturity, uint32_t n)
{
    const int status =
        validate_contract(s0, rate, dividend_yield, sigma, maturity, n);
    if (status != ASIAN_GENUINE_MSFR_OK) {
        if (out != NULL) out->magic = 0u;
        return status;
    }
    if (out == NULL || ((uintptr_t)out & 63u) != 0u)
        return ASIAN_GENUINE_MSFR_INVALID;

    memset(out, 0, sizeof(*out));
    out->abi_version = ASIAN_GENUINE_MSFR_ABI_VERSION;
    out->fixing_count = n;
    out->maturity = (float)maturity;
    out->geometric_b = (float)(maturity * (n + 1.0) / (2.0 * n));
    out->discount = (float)exp(-rate * maturity);
    out->log_s0 = logf((float)s0);
    for (uint32_t k = 0; k < n; ++k)
        out->forward_weights[k] = (float)(k + 1u);
    out->magic = ASIAN_GENUINE_MSFR_BASIS_MAGIC;
    return ASIAN_GENUINE_MSFR_OK;
}

int asian_genuine_msfr_prepare_basis_context(
    asian_genuine_msfr_basis_context_t *out,
    const asian_genuine_route_t *routes,
    const asian_genuine_msfr_basis_controls_t *controls,
    double s0, double rate, double dividend_yield, double sigma,
    double maturity, uint32_t n)
{
    const int status =
        validate_contract(s0, rate, dividend_yield, sigma, maturity, n);
    if (status != ASIAN_GENUINE_MSFR_OK) return status;
    if (out == NULL || ((uintptr_t)out & 63u) != 0u || routes == NULL ||
        ((uintptr_t)routes & 31u) != 0u || controls == NULL ||
        ((uintptr_t)controls & 63u) != 0u ||
        controls->magic != ASIAN_GENUINE_MSFR_BASIS_MAGIC ||
        controls->abi_version != ASIAN_GENUINE_MSFR_ABI_VERSION ||
        controls->fixing_count != n)
        return ASIAN_GENUINE_MSFR_INVALID;

    if (!d1_map_is_identity(routes[0].map) ||
        routes[0].fixing_index != 0u || routes[0].x_base == NULL ||
        routes[0].growth_base == NULL)
        return ASIAN_GENUINE_MSFR_INVALID;
    for (uint32_t k = 0; k < n; ++k) {
        float expected = (float)(n - k) / (float)n, got;
        memcpy(&got, &routes[k].weight_bits, sizeof(got));
        if (routes[k].x_base == NULL || routes[k].growth_base == NULL ||
            routes[k].map == NULL || ((uintptr_t)routes[k].map & 63u) != 0u ||
            routes[k].fixing_index != k ||
            routes[k].map->dimension != k + 1u ||
            memcmp(&got, &expected, sizeof(got)) != 0)
            return ASIAN_GENUINE_MSFR_INVALID;
    }

    const double dt = maturity / n;
    memset(out, 0, sizeof(*out));
    out->routes = routes;
    out->controls = controls;
    out->fixing_count = n;
    out->route_count = n - 1u;
    out->s0 = (float)s0;
    out->inv_n = 1.0f / (float)n;
    out->dt_over_n = (float)(dt / n);
    out->c = (float)(rate - dividend_yield + 0.5 * sigma * sigma);
    out->inv_sigma = (float)(1.0 / sigma);
    out->inv_s0 = (float)(1.0 / s0);
    return ASIAN_GENUINE_MSFR_OK;
}

static void geometric_exact(double s0, double strike, double rate,
                            double dividend_yield, double sigma,
                            double maturity, uint32_t n,
                            asian_genuine_msfr_value_t *call,
                            asian_genuine_msfr_value_t *put)
{
    const double dn = n;
    const double b = maturity * (dn + 1.0) / (2.0 * dn);
    const double dt = maturity / dn;
    const double sum_t = dt * dn * (dn + 1.0) * 0.5;
    const double sum_min = dn * (dn + 1.0) * (2.0 * dn + 1.0) / 6.0;
    const double variance_scale = dt * sum_min / (dn * dn);
    const double variance = sigma * sigma * dt * sum_min / (dn * dn);
    const double mean = (0.0 + dn * log(s0) +
        (rate - dividend_yield - 0.5 * sigma * sigma) * sum_t) / dn;
    const double discount = exp(-rate * maturity);
    const double root = sqrt(variance);
    const double forward = exp(mean + 0.5 * variance);
    const double d2 = (mean - log(strike)) / root;
    const double d1 = d2 + root;
    const double nd1 = normal_cdf(d1), nd2 = normal_cdf(d2);
    const double nmd1 = normal_cdf(-d1), nmd2 = normal_cdf(-d2);
    const double pdf1 = normal_pdf(d1);

    call->price = discount * (forward * nd1 - strike * nd2);
    put->price = discount * (strike * nmd2 - forward * nmd1);
    const double call_m = discount * forward * nd1;
    const double put_m = -discount * forward * nmd1;
    const double call_v =
        0.5 * discount * forward * (nd1 + pdf1 / root);
    const double put_v =
        0.5 * discount * forward * (-nmd1 + pdf1 / root);
    const double mean_sigma = -sigma * b;
    const double variance_sigma = 2.0 * sigma * variance_scale;
    call->delta = call_m / s0;
    put->delta = put_m / s0;
    call->vega = call_m * mean_sigma + call_v * variance_sigma;
    put->vega = put_m * mean_sigma + put_v * variance_sigma;
    call->rho = call_m * b - maturity * call->price;
    put->rho = put_m * b - maturity * put->price;
}

int asian_genuine_msfr_prepare_strikes(
    asian_genuine_msfr_strike_controls_t *out,
    double s0, double rate, double dividend_yield, double sigma,
    double maturity, uint32_t n,
    const float *strikes, uint32_t strike_count)
{
    const int status =
        validate_contract(s0, rate, dividend_yield, sigma, maturity, n);
    if (status != ASIAN_GENUINE_MSFR_OK) {
        if (out != NULL) out->magic = 0u;
        return status;
    }
    if (strike_count == 0u || strike_count > ASIAN_GENUINE_MSFR_MAX_STRIKES) {
        if (out != NULL) out->magic = 0u;
        return ASIAN_GENUINE_MSFR_STRIKE_COUNT_UNSUPPORTED;
    }
    if (out == NULL || ((uintptr_t)out & 63u) != 0u || strikes == NULL)
        return ASIAN_GENUINE_MSFR_INVALID;
    for (uint32_t i = 0; i < strike_count; ++i)
        if (!(strikes[i] > 0.0f) || !isfinite(strikes[i])) {
            out->magic = 0u;
            return ASIAN_GENUINE_MSFR_INVALID;
        }

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

    memset(out, 0, sizeof(*out));
    out->abi_version = ASIAN_GENUINE_MSFR_ABI_VERSION;
    out->strike_count = (uint16_t)strike_count;
    out->fixing_count = n;
    out->s0 = s0;
    out->rate = rate;
    out->dividend_yield = dividend_yield;
    out->sigma = sigma;
    out->maturity = maturity;
    out->discount = discount;
    out->expected_arithmetic = expected_a;
    out->expected_arithmetic_delta = expected_a / s0;
    out->expected_arithmetic_vega = 0.0;
    out->expected_arithmetic_rho = expected_a_rho;
    out->padded_count_tile2 = (strike_count + 1u) & ~1u;
    out->padded_count_tile4 = (strike_count + 3u) & ~3u;

    for (uint32_t i = 0; i < strike_count; ++i) {
        asian_genuine_msfr_value_t call, put;
        geometric_exact(s0, strikes[i], rate, dividend_yield, sigma,
                        maturity, n, &call, &put);
        asian_genuine_msfr_strike_t *record = &out->strikes[i];
        record->strike = strikes[i];
        memcpy(&record->strike_bits, &strikes[i], sizeof(strikes[i]));
        const int direct_call = (double)strikes[i] >= expected_a;
        record->direct_sign = direct_call ? 1.0f : -1.0f;
        if (direct_call) record->flags |= ASIAN_GENUINE_MSFR_DIRECT_CALL;
        if ((double)strikes[i] < expected_a)
            record->flags |= ASIAN_GENUINE_MSFR_CALL_ITM;
        else if ((double)strikes[i] > expected_a)
            record->flags |= ASIAN_GENUINE_MSFR_CALL_OTM;
        else
            record->flags |= ASIAN_GENUINE_MSFR_CALL_ATM;

        const double parity[4] = {
            discount * (expected_a - strikes[i]),
            discount * expected_a / s0,
            0.0,
            discount * (expected_a_rho -
                         maturity * (expected_a - strikes[i]))
        };
        const double *exact = direct_call ? (const double *)&call
                                          : (const double *)&put;
        for (uint32_t field = 0; field < 4u; ++field) {
            record->geometric_direct[field] = exact[field];
            record->call_adjust[field] = direct_call ? 0.0 : parity[field];
            record->put_adjust[field] = direct_call ? -parity[field] : 0.0;
        }
    }
    for (uint32_t i = strike_count; i < out->padded_count_tile4; ++i) {
        out->strikes[i] = out->strikes[strike_count - 1u];
        out->strikes[i].flags |= ASIAN_GENUINE_MSFR_PADDING;
    }
    out->magic = ASIAN_GENUINE_MSFR_STRIKE_MAGIC;
    return ASIAN_GENUINE_MSFR_OK;
}

int asian_genuine_msfr_prepare_consumer_context(
    asian_genuine_msfr_consumer_context_t *out,
    const asian_genuine_msfr_strike_controls_t *controls)
{
    if (out == NULL || ((uintptr_t)out & 63u) != 0u || controls == NULL ||
        ((uintptr_t)controls & 63u) != 0u ||
        controls->magic != ASIAN_GENUINE_MSFR_STRIKE_MAGIC ||
        controls->abi_version != ASIAN_GENUINE_MSFR_ABI_VERSION ||
        controls->strike_count == 0u ||
        controls->strike_count > ASIAN_GENUINE_MSFR_MAX_STRIKES ||
        !supported_n(controls->fixing_count))
        return ASIAN_GENUINE_MSFR_INVALID;
    memset(out, 0, sizeof(*out));
    out->controls = controls;
    out->strike_count = controls->strike_count;
    out->fixing_count = controls->fixing_count;
    out->discount = (float)controls->discount;
    out->maturity = (float)controls->maturity;
    out->padded_count_tile2 = controls->padded_count_tile2;
    out->padded_count_tile4 = controls->padded_count_tile4;
    return ASIAN_GENUINE_MSFR_OK;
}

int asian_genuine_msfr_accumulator_init(
    asian_genuine_msfr_accumulator_t *out,
    const asian_genuine_msfr_consumer_context_t *context,
    enum asian_genuine_msfr_estimator estimator)
{
    if (out == NULL || ((uintptr_t)out & 63u) != 0u || context == NULL ||
        context->controls == NULL ||
        context->controls->magic != ASIAN_GENUINE_MSFR_STRIKE_MAGIC ||
        (estimator != ASIAN_GENUINE_MSFR_ARITHMETIC &&
         estimator != ASIAN_GENUINE_MSFR_GEOMETRIC_CV))
        return ASIAN_GENUINE_MSFR_INVALID;
    memset(out, 0, sizeof(*out));
    out->abi_version = ASIAN_GENUINE_MSFR_ABI_VERSION;
    out->estimator = (uint16_t)estimator;
    out->strike_count = context->strike_count;
    out->controls_magic = context->controls->magic;
    out->controls_identity = (uintptr_t)context->controls;
    out->magic = ASIAN_GENUINE_MSFR_ACCUM_MAGIC;
    return ASIAN_GENUINE_MSFR_OK;
}

typedef void (*consumer_leaf_t)(
    const asian_genuine_msfr_basis_t *,
    const asian_genuine_msfr_consumer_context_t *,
    const asian_genuine_msfr_strike_t *, double (*)[4]);

int asian_genuine_msfr_consume_block(
    const asian_genuine_msfr_basis_t *basis,
    const asian_genuine_msfr_consumer_context_t *context,
    enum asian_genuine_msfr_estimator estimator, uint32_t tile,
    asian_genuine_msfr_accumulator_t *accumulator)
{
    if (basis == NULL || ((uintptr_t)basis & 63u) != 0u || context == NULL ||
        ((uintptr_t)context & 63u) != 0u || context->controls == NULL ||
        context->controls->magic != ASIAN_GENUINE_MSFR_STRIKE_MAGIC ||
        accumulator == NULL || ((uintptr_t)accumulator & 63u) != 0u ||
        accumulator->magic != ASIAN_GENUINE_MSFR_ACCUM_MAGIC ||
        accumulator->abi_version != ASIAN_GENUINE_MSFR_ABI_VERSION ||
        accumulator->estimator != (uint16_t)estimator ||
        accumulator->strike_count != context->strike_count ||
        accumulator->controls_magic != context->controls->magic ||
        accumulator->controls_identity != (uintptr_t)context->controls ||
        (estimator != ASIAN_GENUINE_MSFR_ARITHMETIC &&
         estimator != ASIAN_GENUINE_MSFR_GEOMETRIC_CV) ||
        (tile != 2u && tile != 4u))
        return ASIAN_GENUINE_MSFR_ACCUMULATOR_MISMATCH;
    if (UINT64_MAX - accumulator->completed_path_count <
            ASIAN_GENUINE_MSFR_PATHS ||
        accumulator->completed_block_count == UINT64_MAX)
        return ASIAN_GENUINE_MSFR_INVALID;

    consumer_leaf_t leaf;
    if (estimator == ASIAN_GENUINE_MSFR_ARITHMETIC)
        leaf = tile == 2u ? asian_genuine_msfr_arithmetic_tile2_diag
                          : asian_genuine_msfr_arithmetic_tile4_diag;
    else
        leaf = tile == 2u ? asian_genuine_msfr_cv_tile2_diag
                          : asian_genuine_msfr_cv_tile4_diag;
    const uint32_t padded = tile == 2u ? context->padded_count_tile2
                                       : context->padded_count_tile4;
    for (uint32_t i = 0; i < padded; i += tile)
        leaf(basis, context, &context->controls->strikes[i],
             &accumulator->direct_sums[i]);
    accumulator->completed_path_count += ASIAN_GENUINE_MSFR_PATHS;
    accumulator->completed_block_count += 1u;
    return ASIAN_GENUINE_MSFR_OK;
}

int asian_genuine_msfr_finalize(
    const asian_genuine_msfr_consumer_context_t *context,
    const asian_genuine_msfr_accumulator_t *accumulator,
    asian_genuine_msfr_output_t *output)
{
    if (context == NULL || context->controls == NULL || accumulator == NULL ||
        output == NULL || ((uintptr_t)output & 63u) != 0u ||
        context->controls->magic != ASIAN_GENUINE_MSFR_STRIKE_MAGIC ||
        accumulator->magic != ASIAN_GENUINE_MSFR_ACCUM_MAGIC ||
        accumulator->abi_version != ASIAN_GENUINE_MSFR_ABI_VERSION ||
        accumulator->strike_count != context->strike_count ||
        accumulator->controls_magic != context->controls->magic ||
        accumulator->controls_identity != (uintptr_t)context->controls ||
        accumulator->completed_path_count == 0u)
        return ASIAN_GENUINE_MSFR_ACCUMULATOR_MISMATCH;

    memset(output, 0, sizeof(*output));
    const double inv_paths = 1.0 / accumulator->completed_path_count;
    const int cv = accumulator->estimator == ASIAN_GENUINE_MSFR_GEOMETRIC_CV;
    for (uint32_t i = 0; i < context->strike_count; ++i) {
        const asian_genuine_msfr_strike_t *strike =
            &context->controls->strikes[i];
        double *call = (double *)&output->values[i].call;
        double *put = (double *)&output->values[i].put;
        for (uint32_t field = 0; field < 4u; ++field) {
            double direct = accumulator->direct_sums[i][field] * inv_paths;
            if (cv) direct += strike->geometric_direct[field];
            call[field] = direct + strike->call_adjust[field];
            put[field] = direct + strike->put_adjust[field];
        }
    }
    return ASIAN_GENUINE_MSFR_OK;
}

static uint32_t route_source(const asian_genuine_route_t *route, uint32_t path)
{
    const fragment_map_t *map = route->map;
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    const uint32_t line = map->select[packet][half];
    const uint32_t pattern = map->select[packet][2u + half];
    return line * 16u + map->patterns[pattern][lane];
}

static float from_bits(uint32_t bits)
{
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

static float qualified_exp(float x)
{
    const float log2e = from_bits(UINT32_C(0x3fb8aa3b));
    const float ln2hi = from_bits(UINT32_C(0x3f318000));
    const float ln2lo = from_bits(UINT32_C(0xb95e8083));
    static const uint32_t pbits[9] = {
        UINT32_C(0x3f800000), UINT32_C(0x3f7ffff9),
        UINT32_C(0x3efffffc), UINT32_C(0x3e2aabbf),
        UINT32_C(0x3d2aab67), UINT32_C(0x3c085d88),
        UINT32_C(0x3ab5de3b), UINT32_C(0x3959cfde),
        UINT32_C(0x37d8c471)
    };
    const float exponent = nearbyintf(x * log2e);
    float reduced = fmaf(-ln2hi, exponent, x);
    reduced = fmaf(-ln2lo, exponent, reduced);
    float y = from_bits(pbits[8]);
    for (int i = 7; i >= 0; --i) y = fmaf(reduced, y, from_bits(pbits[i]));
    return scalbnf(y, (int)exponent);
}

void asian_genuine_msfr_forward_probe_diag(
    const asian_genuine_msfr_basis_context_t *ctx, uint32_t packet,
    asian_genuine_msfr_packet_trace_t *trace)
{
    memset(trace, 0, sizeof(*trace));
    for (uint32_t lane = 0; lane < ASIAN_GENUINE_MSFR_PACKET_PATHS; ++lane) {
        const uint32_t path = packet * ASIAN_GENUINE_MSFR_PACKET_PATHS + lane;
        float s = ctx->s0, q = 0.0f, l = 0.0f;
        float cumulative_x = 0.0f, rho_weighted = 0.0f;
        float x_weighted = 0.0f;
        for (uint32_t k = 0; k < ctx->fixing_count; ++k) {
            const asian_genuine_route_t *route = &ctx->routes[k];
            const uint32_t source = route_source(route, path);
            const float x = route->x_base[source];
            float weight;
            memcpy(&weight, &route->weight_bits, sizeof(weight));
            s *= route->growth_base[source];
            q += s;
            l = fmaf(weight, x, l);
            cumulative_x += x;
            rho_weighted = fmaf(ctx->controls->forward_weights[k], s,
                                rho_weighted);
            x_weighted = fmaf(s, cumulative_x, x_weighted);
        }
        const float a = q * ctx->inv_n;
        const float a_rho = rho_weighted * ctx->dt_over_n;
        x_weighted *= ctx->inv_n;
        const float a_vega =
            fmaf(-ctx->c, a_rho, x_weighted) * ctx->inv_sigma;
        const float g = qualified_exp(ctx->controls->log_s0 + l);
        const float g_vega =
            g * ((l - ctx->c * ctx->controls->geometric_b) * ctx->inv_sigma);
        trace->final_s[lane] = s;
        trace->q[lane] = q;
        trace->l[lane] = l;
        trace->basis[ASIAN_GENUINE_MSFR_A][lane] = a;
        trace->basis[ASIAN_GENUINE_MSFR_A_DELTA][lane] = a * ctx->inv_s0;
        trace->basis[ASIAN_GENUINE_MSFR_A_VEGA][lane] = a_vega;
        trace->basis[ASIAN_GENUINE_MSFR_A_RHO][lane] = a_rho;
        trace->basis[ASIAN_GENUINE_MSFR_G][lane] = g;
        trace->basis[ASIAN_GENUINE_MSFR_G_DELTA][lane] = g * ctx->inv_s0;
        trace->basis[ASIAN_GENUINE_MSFR_G_VEGA][lane] = g_vega;
        trace->basis[ASIAN_GENUINE_MSFR_G_RHO][lane] =
            g * ctx->controls->geometric_b;
    }
}

static float reduce_lanes(const float acc[2][16])
{
    float t[16], u[4];
    for (uint32_t lane = 0; lane < 16u; ++lane)
        t[lane] = acc[0][lane] + acc[1][lane];
    for (uint32_t lane = 0; lane < 4u; ++lane) {
        const float low = t[lane] + t[lane + 4u];
        const float high = t[lane + 8u] + t[lane + 12u];
        u[lane] = low + high;
    }
    return (u[0] + u[2]) + (u[1] + u[3]);
}

int asian_genuine_msfr_scalar_consume_block(
    const asian_genuine_msfr_basis_t *basis,
    const asian_genuine_msfr_consumer_context_t *context,
    enum asian_genuine_msfr_estimator estimator,
    asian_genuine_msfr_accumulator_t *accumulator)
{
    if (basis == NULL || context == NULL || accumulator == NULL ||
        accumulator->magic != ASIAN_GENUINE_MSFR_ACCUM_MAGIC ||
        accumulator->estimator != (uint16_t)estimator ||
        accumulator->controls_identity != (uintptr_t)context->controls)
        return ASIAN_GENUINE_MSFR_INVALID;
    float (*lanes)[4][2][16] = calloc(ASIAN_GENUINE_MSFR_MAX_STRIKES,
                                      sizeof(*lanes));
    if (lanes == NULL) return ASIAN_GENUINE_MSFR_INVALID;
    const int cv = estimator == ASIAN_GENUINE_MSFR_GEOMETRIC_CV;
    for (uint32_t path = 0; path < ASIAN_GENUINE_MSFR_PATHS; ++path) {
        const uint32_t half = (path >> 4) & 1u, lane = path & 15u;
        const float a = basis->values[ASIAN_GENUINE_MSFR_A][path];
        const float g = basis->values[ASIAN_GENUINE_MSFR_G][path];
        for (uint32_t i = 0; i < context->strike_count; ++i) {
            const asian_genuine_msfr_strike_t *record =
                &context->controls->strikes[i];
            const float sign = record->direct_sign;
            const float apayload = sign * (a - record->strike);
            const float gpayload = sign * (g - record->strike);
            const int ai = apayload > 0.0f, gi = gpayload > 0.0f;
            const float ap = fmaxf(apayload, 0.0f);
            const float gp = cv ? fmaxf(gpayload, 0.0f) : 0.0f;
            const float price = cv ? context->discount * ap -
                                     context->discount * gp :
                                     context->discount * ap;
            lanes[i][0][half][lane] += price;
            for (uint32_t field = 1; field <= 2; ++field) {
                const float av = ai ? sign * basis->values[field][path] : 0.0f;
                const float gv = cv && gi ?
                    sign * basis->values[field + 4u][path] : 0.0f;
                lanes[i][field][half][lane] +=
                    context->discount * (av - gv);
            }
            const float ar = ai ? sign *
                basis->values[ASIAN_GENUINE_MSFR_A_RHO][path] : 0.0f;
            const float gr = cv && gi ? sign *
                basis->values[ASIAN_GENUINE_MSFR_G_RHO][path] : 0.0f;
            const float rho0 = context->discount * (ar - gr);
            lanes[i][3][half][lane] +=
                fmaf(-context->maturity, price, rho0);
        }
    }
    for (uint32_t i = 0; i < context->strike_count; ++i)
        for (uint32_t field = 0; field < 4u; ++field)
            accumulator->direct_sums[i][field] +=
                reduce_lanes(lanes[i][field]);
    accumulator->completed_path_count += ASIAN_GENUINE_MSFR_PATHS;
    accumulator->completed_block_count += 1u;
    free(lanes);
    return ASIAN_GENUINE_MSFR_OK;
}
