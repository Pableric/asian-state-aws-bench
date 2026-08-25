#include "private/asian_geocv_affine_lifecycle_diag.h"

#include "private/asian_geometric_cv_diag.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int same_double(double a, double b)
{
    uint64_t ab, bb;
    memcpy(&ab, &a, sizeof(ab));
    memcpy(&bb, &b, sizeof(bb));
    return ab == bb;
}

static int validate_source_once(double rate, double dividend_yield,
                                double sigma, double maturity)
{
    asian_genuine_fixed_block_source_request_t request;
    asian_genuine_fixed_block_source_context_t context
        __attribute__((aligned(64)));
    memset(&request, 0, sizeof(request));
    request.target_start_index = ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    request.path_count = ASIAN_GEOCV_AFFINE_PATHS;
    request.block_count = 1u;
    request.fixing_count = ASIAN_GEOCV_AFFINE_N64;
    request.s0 = 100.0;
    request.rate = rate;
    request.dividend_yield = dividend_yield;
    request.sigma = sigma;
    request.maturity = maturity;
    request.signed_z = asian_genuine_fixed_block_signed_z;
    request.signed_z_bytes = ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES;
    return asian_genuine_fixed_block_source_prepare(&context, &request);
}

int asian_geocv_affine_engine_create(asian_geocv_affine_engine_t *engine)
{
    if (engine == NULL || ((uintptr_t)engine & 63u) != 0u)
        return -1;
    memset(engine, 0, sizeof(*engine));
    if (validate_source_once(0.03, 0.0, 0.20, 1.0) != 0 ||
        validate_source_once(-0.01, 0.015, 0.35, 0.75) != 0 ||
        asian_meta_affine_plan_create(&engine->plan) != 0)
        return -1;
    uint32_t lo_bits = UINT32_C(0xc075e22f);
    uint32_t hi_bits = UINT32_C(0x4075e233);
    memcpy(&engine->signed_z_min, &lo_bits, sizeof(lo_bits));
    memcpy(&engine->signed_z_max, &hi_bits, sizeof(hi_bits));
    engine->magic = ASIAN_GEOCV_AFFINE_ENGINE_MAGIC;
    return 0;
}

void asian_geocv_affine_engine_destroy(asian_geocv_affine_engine_t *engine)
{
    if (engine != NULL) {
        asian_meta_affine_plan_destroy(engine->plan);
        memset(engine, 0, sizeof(*engine));
    }
}

__attribute__((noinline, used))
int asian_geocv_affine_carrier_prepare(
    const asian_geocv_affine_engine_t *engine,
    const asian_geocv_affine_carrier_input_t *input,
    asian_geocv_affine_carrier_t *carrier)
{
    if (engine == NULL || input == NULL || carrier == NULL ||
        engine->magic != ASIAN_GEOCV_AFFINE_ENGINE_MAGIC ||
        engine->plan == NULL || ((uintptr_t)carrier & 63u) != 0u ||
        !isfinite(input->rate) || !isfinite(input->dividend_yield) ||
        !isfinite(input->sigma) || !(input->sigma > 0.0) ||
        !isfinite(input->maturity) || !(input->maturity > 0.0))
        return -1;

    const double dt = input->maturity / (double)ASIAN_GEOCV_AFFINE_N64;
    const float drift = (float)((input->rate - input->dividend_yield -
                                 0.5 * input->sigma * input->sigma) * dt);
    const float diffusion = (float)(input->sigma * sqrt(dt));
    if (!isfinite(drift) || !isfinite(diffusion) || !(diffusion > 0.0f) ||
        drift < -0.25f || drift > 0.25f || diffusion > 0.20f)
        return -1;
    const float x0 = fmaf(diffusion, engine->signed_z_min, drift);
    const float x1 = fmaf(diffusion, engine->signed_z_max, drift);
    if (!isfinite(x0) || !isfinite(x1) || x0 < -87.0f || x1 > 88.0f)
        return -1;

    memset(&carrier->source, 0, sizeof(carrier->source));
    carrier->source.signed_z = asian_genuine_fixed_block_signed_z;
    carrier->source.drift = drift;
    carrier->source.diffusion = diffusion;
    carrier->source.magic = ASIAN_GENUINE_FIXED_BLOCK_SOURCE_MAGIC;
    carrier->source.abi_version = ASIAN_GENUINE_FIXED_BLOCK_ABI_VERSION;
    carrier->rate = input->rate;
    carrier->dividend_yield = input->dividend_yield;
    carrier->sigma = input->sigma;
    carrier->maturity = input->maturity;
    carrier->magic = ASIAN_GEOCV_AFFINE_CARRIER_MAGIC;

    /* Exact qualified lifecycle: materialize X once, then unchanged exp. */
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(&carrier->source,
                                                            carrier->x);
    asian_vector_exp_range_reduced_array_diag(carrier->x, carrier->growth);
    asian_vector_exp_range_reduced_array_diag(
        carrier->x + ASIAN_GEOCV_AFFINE_PATHS,
        carrier->growth + ASIAN_GEOCV_AFFINE_PATHS);
    return 0;
}

static int valid_request(const asian_geocv_affine_carrier_t *carrier,
                         const asian_geocv_affine_request_input_t *input)
{
    return carrier != NULL && input != NULL &&
        carrier->magic == ASIAN_GEOCV_AFFINE_CARRIER_MAGIC &&
        isfinite(input->s0) && input->s0 > 0.0 &&
        isfinite(input->strike) && input->strike > 0.0 &&
        same_double(input->rate, carrier->rate) &&
        same_double(input->dividend_yield, carrier->dividend_yield) &&
        same_double(input->sigma, carrier->sigma) &&
        same_double(input->maturity, carrier->maturity);
}

__attribute__((noinline, used))
int asian_geocv_affine_request_prepare(
    const asian_geocv_affine_engine_t *engine,
    const asian_geocv_affine_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    asian_geocv_affine_request_t *request)
{
    if (engine == NULL || request == NULL ||
        engine->magic != ASIAN_GEOCV_AFFINE_ENGINE_MAGIC ||
        engine->plan == NULL || ((uintptr_t)request & 63u) != 0u ||
        !valid_request(carrier, input))
        return -1;
    if (asian_meta_affine_routes_bind(engine->plan, carrier->x,
            carrier->growth, ASIAN_GEOCV_AFFINE_N64, request->routes) != 0)
        return -1;

    const float strike = (float)input->strike;
    uint32_t padded = 0u;
    if (asian_geometric_cv_packet_local_strip_prepare_padded(
            &request->strip, input->s0, input->rate,
            input->dividend_yield, input->sigma, input->maturity,
            ASIAN_GEOCV_AFFINE_N64, 0u, 0.0, 0.0, &strike, 1u,
            &padded) != 0 || padded != 1u)
        return -1;

    asian_geometric_cv_packet_local_context_t qualified
        __attribute__((aligned(64)));
    memset(&qualified, 0, sizeof(qualified));
    qualified.d1_x = request->routes[0].x_base;
    qualified.d1_growth = request->routes[0].growth_base;
    qualified.routes_d2 = request->routes + 1;
    qualified.fixing_count = ASIAN_GEOCV_AFFINE_N64;
    qualified.s0 = (float)input->s0;
    qualified.d1_weight_bits = request->routes[0].weight_bits;
    memcpy(&qualified.terminal_log_base_bits, &request->strip.log_base, 4u);
    qualified.magic = ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAGIC;
    qualified.abi_version = ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ABI_VERSION;
    if (asian_geometric_cv_immediate_prepare(
            &request->immediate, &qualified, &request->strip) != 0)
        return -1;
    request->carrier = carrier;
    request->magic = ASIAN_GEOCV_AFFINE_REQUEST_MAGIC;
    return 0;
}

__attribute__((noinline, used))
int asian_geocv_affine_prepared_price(
    const asian_geocv_affine_request_t *request,
    asian_genuine_strip_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_GEOCV_AFFINE_REQUEST_MAGIC ||
        request->carrier == NULL ||
        request->carrier->magic != ASIAN_GEOCV_AFFINE_CARRIER_MAGIC ||
        ((uintptr_t)output & 63u) != 0u)
        return -1;
    memset(output, 0, sizeof(*output));
    return asian_geometric_cv_immediate_run(
        &request->immediate, &request->strip, 1u, 0, output);
}

__attribute__((noinline, used))
int asian_geocv_affine_fresh_total(
    const asian_geocv_affine_engine_t *engine,
    const asian_geocv_affine_request_input_t *input,
    asian_geocv_affine_carrier_t *carrier,
    asian_geocv_affine_request_t *request,
    asian_genuine_strip_output_t *output)
{
    const asian_geocv_affine_carrier_input_t carrier_input = {
        input->rate, input->dividend_yield, input->sigma, input->maturity};
    int status = asian_geocv_affine_carrier_prepare(
        engine, &carrier_input, carrier);
    if (status == 0)
        status = asian_geocv_affine_request_prepare(
            engine, carrier, input, request);
    if (status == 0)
        status = asian_geocv_affine_prepared_price(request, output);
    return status;
}

__attribute__((noinline, used))
int asian_geocv_affine_reuse_total(
    const asian_geocv_affine_engine_t *engine,
    const asian_geocv_affine_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    asian_geocv_affine_request_t *request,
    asian_genuine_strip_output_t *output)
{
    int status = asian_geocv_affine_request_prepare(
        engine, carrier, input, request);
    if (status == 0)
        status = asian_geocv_affine_prepared_price(request, output);
    return status;
}
