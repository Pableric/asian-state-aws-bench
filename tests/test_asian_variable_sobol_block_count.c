#define _POSIX_C_SOURCE 200112L

#include "private/asian_variable_sobol_block_count_diag.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *a64(size_t bytes)
{
    void *out = NULL;
    if (posix_memalign(&out, 64u, bytes) != 0) return NULL;
    memset(out, 0, bytes);
    return out;
}

static int exact_double(double left, double right)
{
    return memcmp(&left, &right, sizeof(left)) == 0;
}

static int same_strip(const asian_variable_output_t *a,
                      const asian_variable_output_t *b, uint32_t strikes)
{
    return a->family == b->family && a->strike_count == b->strike_count &&
        memcmp(a->value.strip.values, b->value.strip.values,
               strikes * sizeof(a->value.strip.values[0])) == 0;
}

static int run_strip_case(asian_variable_engine_t *engine,
                          uint32_t block_count, uint32_t n,
                          enum asian_variable_family family,
                          enum asian_affine_family_workload workload,
                          uint32_t strike_count,
                          asian_variable_output_t *affine_out)
{
    const enum asian_variable_carrier_capability cap =
        family == ASIAN_VARIABLE_GEOCV ? ASIAN_VARIABLE_X_GROWTH :
                                         ASIAN_VARIABLE_GROWTH_ONLY;
    asian_variable_carrier_t *carrier = NULL;
    asian_variable_strip_request_t *affine = a64(sizeof(*affine));
    asian_variable_strip_request_t *generic = a64(sizeof(*generic));
    asian_variable_output_t *generic_out = a64(sizeof(*generic_out));
    float strikes[ASIAN_AFFINE_FAMILY_MAX_STRIKES];
    if (affine == NULL || generic == NULL || generic_out == NULL ||
        asian_variable_carrier_create(block_count, cap, &carrier) != 0)
        return -1;
    for (uint32_t k = 0; k < strike_count; ++k)
        strikes[k] = 88.0f + 1.25f * (float)k;
    if (strike_count == 1u) strikes[0] = 100.0f;
    asian_affine_family_carrier_input_t market = {
        .rate=0.03,.dividend_yield=0.0,.sigma=0.20,.maturity=1.0,
        .future_fixings=n,
    };
    asian_affine_family_request_input_t input = {
        .s0=100.0,.rate=market.rate,.dividend_yield=market.dividend_yield,
        .sigma=market.sigma,.maturity=market.maturity,.future_fixings=n,
        .completed_fixings=0,.initial_arithmetic_sum=0.0,.past_log_sum=0.0,
        .strikes=strikes,.strike_count=strike_count,.workload=workload,
    };
    int status = asian_variable_carrier_prepare(engine, block_count, &market,
        carrier);
    if (status == 0) status = asian_variable_strip_request_prepare(engine,
        carrier, &input, family, ASIAN_AFFINE_FAMILY_AFFINE, affine);
    if (status == 0) status = asian_variable_strip_request_prepare(engine,
        carrier, &input, family, ASIAN_AFFINE_FAMILY_GENERIC, generic);
    if (status == 0) status = asian_variable_sobol_price(affine, block_count,
        affine_out);
    if (status == 0) status = asian_variable_sobol_price(generic, block_count,
        generic_out);
    if (status == 0 && !same_strip(affine_out, generic_out, strike_count))
        status = -1;
    asian_variable_carrier_destroy(carrier);
    free(generic_out); free(generic); free(affine);
    return status;
}

static int run_full_risk_case(asian_variable_engine_t *engine,
                              uint32_t block_count, uint32_t n)
{
    asian_variable_carrier_t *carrier = NULL;
    asian_variable_full_risk_request_t *request = a64(sizeof(*request));
    asian_variable_output_t *output = a64(sizeof(*output));
    const float strike = 100.0f;
    asian_affine_family_carrier_input_t market = {
        .rate=0.03,.dividend_yield=0.0,.sigma=0.20,.maturity=1.0,
        .future_fixings=n,
    };
    asian_affine_family_request_input_t input = {
        .s0=100.0,.rate=market.rate,.dividend_yield=market.dividend_yield,
        .sigma=market.sigma,.maturity=market.maturity,.future_fixings=n,
        .completed_fixings=0,.initial_arithmetic_sum=0.0,.past_log_sum=0.0,
        .strikes=&strike,.strike_count=1,.workload=ASIAN_AFFINE_FAMILY_PRICE,
    };
    int status = asian_variable_carrier_create(block_count,
        ASIAN_VARIABLE_X_GROWTH, &carrier);
    if (status == 0) status = asian_variable_carrier_prepare(engine,
        block_count, &market, carrier);
    if (status == 0) status = asian_variable_full_risk_k1_request_prepare(
        engine, carrier, &input, request);
    asian_variable_phase1_invocations_reset();
    if (status == 0) status = asian_variable_sobol_price(request, block_count,
        output);
    if (status == 0 && asian_variable_phase1_invocations() != block_count)
        status = -1;
    asian_variable_carrier_destroy(carrier);
    free(output); free(request);
    return status;
}

static int run_msfr_case(asian_variable_engine_t *engine,
                         uint32_t block_count, uint32_t n,
                         uint32_t strike_count,
                         enum asian_genuine_msfr_estimator estimator)
{
    asian_variable_carrier_t *carrier = NULL;
    asian_variable_msfr_request_t *request = a64(sizeof(*request));
    asian_variable_output_t *output = a64(sizeof(*output));
    float strikes[ASIAN_AFFINE_FAMILY_MAX_STRIKES];
    for (uint32_t k = 0; k < strike_count; ++k)
        strikes[k] = 80.0f + 1.5f * (float)k;
    asian_affine_family_carrier_input_t market = {
        .rate=0.03,.dividend_yield=0.0,.sigma=0.20,.maturity=1.0,
        .future_fixings=n,
    };
    asian_affine_family_request_input_t input = {
        .s0=100.0,.rate=market.rate,.dividend_yield=market.dividend_yield,
        .sigma=market.sigma,.maturity=market.maturity,.future_fixings=n,
        .completed_fixings=0,.initial_arithmetic_sum=0.0,.past_log_sum=0.0,
        .strikes=strikes,.strike_count=strike_count,
        .workload=ASIAN_AFFINE_FAMILY_PRICE_DELTA,
    };
    int status = asian_variable_carrier_create(block_count,
        ASIAN_VARIABLE_X_GROWTH, &carrier);
    if (status == 0) status = asian_variable_carrier_prepare(engine,
        block_count, &market, carrier);
    if (status == 0) status = asian_variable_msfr_request_prepare(engine,
        carrier, &input, estimator, request);
    asian_variable_phase1_invocations_reset();
    if (status == 0) status = asian_variable_sobol_price(request, block_count,
        output);
    if (status == 0 && strike_count == 1u &&
        asian_variable_phase1_invocations() != block_count)
        status = -1;
    if (status == 0 && output->strike_count != strike_count)
        status = -1;
    asian_variable_carrier_destroy(carrier);
    free(output); free(request);
    return status;
}

static int carrier_identity(asian_variable_engine_t *engine)
{
    asian_variable_carrier_t *growth = NULL, *xgrowth = NULL;
    asian_affine_family_carrier_input_t market = {
        .rate=0.03,.dividend_yield=0.01,.sigma=0.20,.maturity=1.0,
        .future_fixings=64,
    };
    int status = asian_variable_carrier_create(16u,
        ASIAN_VARIABLE_GROWTH_ONLY, &growth);
    if (status == 0) status = asian_variable_carrier_create(16u,
        ASIAN_VARIABLE_X_GROWTH, &xgrowth);
    if (status == 0) status = asian_variable_carrier_prepare(engine,16u,
        &market,growth);
    if (status == 0) status = asian_variable_carrier_prepare(engine,16u,
        &market,xgrowth);
    const size_t values = 30u * ASIAN_VARIABLE_PATHS_PER_BLOCK;
    if (status == 0 && memcmp(growth->growth,xgrowth->growth,
            values*sizeof(float)) != 0) status = -1;
    const double dt=market.maturity/(double)market.future_fixings;
    const float drift=(float)((market.rate-market.dividend_yield-
        0.5*market.sigma*market.sigma)*dt);
    const float diffusion=(float)(market.sigma*sqrt(dt));
    for (size_t i=0; status==0 && i<values; ++i) {
        const float expected=fmaf(diffusion,asian_variable_signed_z_bank[i],drift);
        if (memcmp(&expected,&xgrowth->x[i],sizeof(expected)) != 0) status=-1;
    }
    asian_variable_carrier_destroy(xgrowth);
    asian_variable_carrier_destroy(growth);
    return status;
}

static int seasoned_identity(asian_variable_engine_t *engine,
                             uint32_t block_count,
                             enum asian_variable_family family)
{
    asian_variable_carrier_t *carrier = NULL;
    asian_variable_strip_request_t *affine = a64(sizeof(*affine));
    asian_variable_strip_request_t *generic = a64(sizeof(*generic));
    asian_variable_output_t *a = a64(sizeof(*a)), *g = a64(sizeof(*g));
    const float strikes[5] = {85.0f,95.0f,100.0f,105.0f,115.0f};
    asian_affine_family_carrier_input_t market = {
        .rate=0.02,.dividend_yield=0.01,.sigma=0.25,.maturity=0.625,
        .future_fixings=20,
    };
    asian_affine_family_request_input_t input = {
        .s0=102.0,.rate=market.rate,.dividend_yield=market.dividend_yield,
        .sigma=market.sigma,.maturity=market.maturity,.future_fixings=20,
        .completed_fixings=12,.initial_arithmetic_sum=1218.5,
        .past_log_sum=12.0*log(101.25),.strikes=strikes,.strike_count=5,
        .workload=ASIAN_AFFINE_FAMILY_PRICE_DELTA,
    };
    const enum asian_variable_carrier_capability cap =
        family==ASIAN_VARIABLE_GEOCV?ASIAN_VARIABLE_X_GROWTH:
                                        ASIAN_VARIABLE_GROWTH_ONLY;
    int status=asian_variable_carrier_create(block_count,cap,&carrier);
    if(status==0)status=asian_variable_carrier_prepare(engine,block_count,
        &market,carrier);
    if(status==0)status=asian_variable_strip_request_prepare(engine,carrier,
        &input,family,ASIAN_AFFINE_FAMILY_AFFINE,affine);
    if(status==0)status=asian_variable_strip_request_prepare(engine,carrier,
        &input,family,ASIAN_AFFINE_FAMILY_GENERIC,generic);
    if(status==0)status=asian_variable_sobol_price(affine,block_count,a);
    if(status==0)status=asian_variable_sobol_price(generic,block_count,g);
    if(status==0&&!same_strip(a,g,5u))status=-1;
    asian_variable_carrier_destroy(carrier);
    free(g);free(a);free(generic);free(affine);
    return status;
}

static int main_accuracy(asian_variable_engine_t *engine)
{
    static const uint32_t counts[] = {1,2,4,8,16};
    const double call_reference = 5.34876366;
    const double put_reference = 3.85536588;
    double previous_plain = 0.0, previous_cv = 0.0;
    puts("paths side estimator price signed_error abs_error");
    for (uint32_t i = 0; i < 5u; ++i) {
        asian_variable_output_t *plain = a64(sizeof(*plain));
        asian_variable_output_t *cv = a64(sizeof(*cv));
        if (plain == NULL || cv == NULL ||
            run_strip_case(engine, counts[i], 64u, ASIAN_VARIABLE_ARITHMETIC,
                ASIAN_AFFINE_FAMILY_PRICE, 1u, plain) != 0 ||
            run_strip_case(engine, counts[i], 64u, ASIAN_VARIABLE_GEOCV,
                ASIAN_AFFINE_FAMILY_PRICE, 1u, cv) != 0)
            return -1;
        const asian_genuine_strip_value_t *p = &plain->value.strip.values[0];
        const asian_genuine_strip_value_t *g = &cv->value.strip.values[0];
        if (i == 0u) {
            if (!exact_double(p->call_price, 0x1.57d186d85266p+2) ||
                !exact_double(p->put_price, 0x1.f07a78p+1) ||
                !exact_double(g->call_price, 0x1.563fd989e061ap+2) ||
                !exact_double(g->put_price, 0x1.ed571d631bf74p+1))
                return -1;
        } else if (exact_double(previous_plain, p->call_price) ||
                   exact_double(previous_cv, g->call_price)) {
            return -1;
        }
        printf("%u call plain %.17g %+.8g %.8g\n", counts[i]*4096u,
            p->call_price, p->call_price-call_reference,
            fabs(p->call_price-call_reference));
        printf("%u put plain %.17g %+.8g %.8g\n", counts[i]*4096u,
            p->put_price, p->put_price-put_reference,
            fabs(p->put_price-put_reference));
        printf("%u call geocv %.17g %+.8g %.8g\n", counts[i]*4096u,
            g->call_price, g->call_price-call_reference,
            fabs(g->call_price-call_reference));
        printf("%u put geocv %.17g %+.8g %.8g\n", counts[i]*4096u,
            g->put_price, g->put_price-put_reference,
            fabs(g->put_price-put_reference));
        previous_plain = p->call_price; previous_cv = g->call_price;
        free(cv); free(plain);
    }
    if (carrier_identity(engine) != 0 ||
        seasoned_identity(engine, 4u, ASIAN_VARIABLE_ARITHMETIC) != 0 ||
        seasoned_identity(engine, 4u, ASIAN_VARIABLE_GEOCV) != 0 ||
        run_msfr_case(engine, 2u, 64u, 3u,
            ASIAN_GENUINE_MSFR_ARITHMETIC) != 0 ||
        run_msfr_case(engine, 2u, 64u, 32u,
            ASIAN_GENUINE_MSFR_GEOMETRIC_CV) != 0)
        return -1;
    return 0;
}

static int bounded_sde(asian_variable_engine_t *engine)
{
    asian_variable_output_t *out = a64(sizeof(*out));
    if (out == NULL) return -1;
    for (uint32_t n = 2u; n <= 256u; ++n) {
        const uint32_t k = 1u + ((n - 2u) % 32u);
        const enum asian_variable_family family =
            (n & 1u) ? ASIAN_VARIABLE_GEOCV : ASIAN_VARIABLE_ARITHMETIC;
        const enum asian_affine_family_workload workload =
            (n % 3u) ? ASIAN_AFFINE_FAMILY_PRICE :
                       ASIAN_AFFINE_FAMILY_PRICE_DELTA;
        if (run_strip_case(engine, 1u, n, family, workload, k, out) != 0)
            return -1;
    }
    static const uint32_t blocks[] = {1,2,4,8,16};
    static const uint32_t boundary[] = {
        2,3,4,15,16,17,31,32,33,63,64,65,127,128,129,255,256
    };
    uint32_t ordinal = 0u;
    for (uint32_t b = 0; b < 5u; ++b) {
        for (uint32_t i = 0; i < sizeof(boundary)/sizeof(boundary[0]); ++i) {
            if (blocks[b] == 1u && boundary[i] != 64u && boundary[i] != 256u)
                continue;
            const uint32_t k = 1u + (ordinal++ % 32u);
            const enum asian_variable_family family = (ordinal & 1u) ?
                ASIAN_VARIABLE_ARITHMETIC : ASIAN_VARIABLE_GEOCV;
            if (run_strip_case(engine, blocks[b], boundary[i], family,
                    (ordinal % 3u) ? ASIAN_AFFINE_FAMILY_PRICE :
                    ASIAN_AFFINE_FAMILY_PRICE_DELTA, k, out) != 0)
                return -1;
        }
        if (run_full_risk_case(engine, blocks[b], 64u) != 0 ||
            run_full_risk_case(engine, blocks[b], 256u) != 0 ||
            run_msfr_case(engine, blocks[b], 64u, 1u,
                ASIAN_GENUINE_MSFR_ARITHMETIC) != 0 ||
            run_msfr_case(engine, blocks[b], 256u, 2u + b,
                ASIAN_GENUINE_MSFR_GEOMETRIC_CV) != 0)
            return -1;
    }
    free(out);
    return 0;
}

int main(int argc, char **argv)
{
    const int full_sde = argc == 2 && strcmp(argv[1], "--sde") == 0;
    if (asian_variable_structural_check() != 0) {
        fputs("structural_check FAIL\n", stderr);
        return 1;
    }
    puts("structural_check PASS blocks=16 dimensions=256 paths=4096");
    puts("structural_metadata shared_affine_columns=12 additional_bytes_per_block=1024 "
         "donor_reuse=b0-b1:2-3,b2-b5:4-7,b6-b13:8-15,b14-b15:16-31");
    asian_variable_engine_t *engine = a64(sizeof(*engine));
    if (engine == NULL || asian_variable_engine_create(engine, 1) != 0)
        return 1;
    if (main_accuracy(engine) != 0 ||
        (full_sde && bounded_sde(engine) != 0)) {
        fputs("variable_block_correctness FAIL\n", stderr);
        return 1;
    }
    puts("variable_block_correctness PASS block_count_one_exact=YES phase1_per_block=1");
    asian_variable_engine_destroy(engine);
    free(engine);
    return 0;
}
