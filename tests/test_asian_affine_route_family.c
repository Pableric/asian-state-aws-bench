#define _POSIX_C_SOURCE 200112L
#include "private/asian_affine_route_family_diag.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double rate, dividend, sigma, maturity;
} market_t;

static const market_t markets[] = {
    {-0.02, 0.01, 0.05, 0.25},
    { 0.00, 0.00, 0.40, 0.25},
    { 0.03, 0.00, 0.20, 1.00},
    { 0.03, 0.01, 0.05, 5.00},
};
static const market_t principal = {0.03, 0.0, 0.20, 1.0};

typedef struct {
    unsigned char *storage;
    asian_genuine_strip_output_t *value;
} guarded_output_t;

static void *a64(size_t bytes)
{
    void *p = NULL;
    return posix_memalign(&p, 64u, bytes) == 0 ? p : NULL;
}

static int guarded_create(guarded_output_t *out)
{
    out->storage = a64(sizeof(*out->value) + 128u);
    if (out->storage == NULL)
        return -1;
    memset(out->storage, 0xa5, sizeof(*out->value) + 128u);
    out->value = (asian_genuine_strip_output_t *)(out->storage + 64u);
    return 0;
}

static int guarded_ok(const guarded_output_t *out)
{
    for (size_t i = 0; i < 64u; ++i)
        if (out->storage[i] != 0xa5u ||
            out->storage[64u + sizeof(*out->value) + i] != 0xa5u)
            return 0;
    return 1;
}

static void strikes_for(float out[32], uint32_t count, uint32_t seed)
{
    for (uint32_t i = 0; i < count; ++i)
        out[i] = 72.5f + (float)((i * 17u + seed * 11u) % 37u) * 1.75f;
    if (count >= 4u) {
        out[0] = 100.0f;
        out[1] = nextafterf(100.0f, INFINITY);
        out[2] = nextafterf(100.0f, -INFINITY);
    }
}

static asian_affine_family_carrier_input_t carrier_input(
    const market_t *market, uint32_t future)
{
    const asian_affine_family_carrier_input_t out = {
        market->rate, market->dividend, market->sigma, market->maturity,
        future};
    return out;
}

static asian_affine_family_request_input_t request_input(
    const market_t *market, uint32_t future, uint32_t completed,
    const float *strikes, uint32_t count,
    enum asian_affine_family_workload workload)
{
    const asian_affine_family_request_input_t out = {
        .s0 = 100.0,
        .rate = market->rate,
        .dividend_yield = market->dividend,
        .sigma = market->sigma,
        .maturity = market->maturity,
        .future_fixings = future,
        .completed_fixings = completed,
        .initial_arithmetic_sum = completed == 0u ? 0.0 : 95.0 * completed,
        .past_log_sum = completed == 0u ? 0.0 : log(95.0) * completed,
        .strikes = strikes,
        .strike_count = count,
        .workload = workload,
    };
    return out;
}

static int compare_arithmetic(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_generic_oracle_t *oracle,
    const asian_affine_family_growth_carrier_t *growth,
    const asian_affine_family_xgrowth_carrier_t *xgrowth,
    const asian_affine_family_request_input_t *input)
{
    asian_affine_family_arithmetic_request_t *generic = a64(sizeof(*generic));
    asian_affine_family_arithmetic_request_t *affine = a64(sizeof(*affine));
    asian_affine_family_arithmetic_request_t *reused = a64(sizeof(*reused));
    guarded_output_t go = {0}, ao = {0}, ro = {0};
    int result = -1;
    if (generic == NULL || affine == NULL || reused == NULL ||
        guarded_create(&go) != 0 || guarded_create(&ao) != 0 ||
        guarded_create(&ro) != 0)
        goto done;
    if (asian_affine_family_arithmetic_request_prepare_growth(
            engine, oracle, growth, input, ASIAN_AFFINE_FAMILY_GENERIC,
            generic) != 0 ||
        asian_affine_family_arithmetic_request_prepare_growth(
            engine, oracle, growth, input, ASIAN_AFFINE_FAMILY_AFFINE,
            affine) != 0 ||
        asian_affine_family_arithmetic_request_prepare_xgrowth(
            engine, oracle, xgrowth, input, ASIAN_AFFINE_FAMILY_AFFINE,
            reused) != 0 ||
        asian_affine_family_arithmetic_prepared_price(generic, go.value) != 0 ||
        asian_affine_family_arithmetic_prepared_price(affine, ao.value) != 0 ||
        asian_affine_family_arithmetic_prepared_price(reused, ro.value) != 0 ||
        memcmp(go.value, ao.value, sizeof(*go.value)) != 0 ||
        memcmp(ao.value, ro.value, sizeof(*ao.value)) != 0 ||
        !guarded_ok(&go) || !guarded_ok(&ao) || !guarded_ok(&ro))
        goto done;
    result = 0;
done:
    free(go.storage); free(ao.storage); free(ro.storage);
    free(generic); free(affine); free(reused);
    return result;
}

static int compare_geocv(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_generic_oracle_t *oracle,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_genuine_strip_output_t *captured)
{
    asian_affine_family_geocv_request_t *generic = a64(sizeof(*generic));
    asian_affine_family_geocv_request_t *affine = a64(sizeof(*affine));
    guarded_output_t go = {0}, ao = {0};
    int result = -1;
    if (generic == NULL || affine == NULL || guarded_create(&go) != 0 ||
        guarded_create(&ao) != 0)
        goto done;
    if (asian_affine_family_geocv_request_prepare(
            engine, oracle, carrier, input, ASIAN_AFFINE_FAMILY_GENERIC,
            generic) != 0 ||
        asian_affine_family_geocv_request_prepare(
            engine, oracle, carrier, input, ASIAN_AFFINE_FAMILY_AFFINE,
            affine) != 0 ||
        asian_affine_family_geocv_prepared_price(generic, go.value) != 0 ||
        asian_affine_family_geocv_prepared_price(affine, ao.value) != 0 ||
        memcmp(go.value, ao.value, sizeof(*go.value)) != 0 ||
        !guarded_ok(&go) || !guarded_ok(&ao))
        goto done;
    if (captured != NULL)
        memcpy(captured, ao.value, sizeof(*captured));
    result = 0;
done:
    free(go.storage); free(ao.storage); free(generic); free(affine);
    return result;
}

static int lifecycle_boundaries(asian_affine_family_engine_t *engine)
{
    asian_affine_family_generic_oracle_t absent;
    asian_affine_family_growth_carrier_t *growth = a64(sizeof(*growth));
    asian_affine_family_xgrowth_carrier_t *xgrowth = a64(sizeof(*xgrowth));
    asian_affine_family_arithmetic_request_t *request = a64(sizeof(*request));
    float strike = 100.0f;
    const asian_affine_family_carrier_input_t carrier =
        carrier_input(&principal, 64u);
    const asian_affine_family_request_input_t input = request_input(
        &principal, 64u, 0u, &strike, 1u, ASIAN_AFFINE_FAMILY_PRICE);
    memset(&absent, 0, sizeof(absent));
    if (growth == NULL || xgrowth == NULL || request == NULL ||
        engine->affine_plan == NULL ||
        asian_affine_family_growth_carrier_prepare(engine, &carrier, growth) !=
            ASIAN_AFFINE_FAMILY_OK ||
        asian_affine_family_xgrowth_carrier_prepare(engine, &carrier, xgrowth) !=
            ASIAN_AFFINE_FAMILY_OK ||
        memcmp(growth->growth, xgrowth->growth,
               sizeof(growth->growth)) != 0 ||
        asian_affine_family_arithmetic_request_prepare_growth(
            engine, &absent, growth, &input, ASIAN_AFFINE_FAMILY_GENERIC,
            request) != ASIAN_AFFINE_FAMILY_ORACLE_REQUIRED ||
        asian_affine_family_arithmetic_request_prepare_growth(
            engine, NULL, growth, &input, ASIAN_AFFINE_FAMILY_AUTO,
            request) != ASIAN_AFFINE_FAMILY_POLICY_UNFROZEN) {
        free(growth); free(xgrowth); free(request); return -1;
    }
    free(growth); free(xgrowth); free(request); return 0;
}

static int exact_n64(asian_affine_family_engine_t *engine,
                     asian_affine_family_generic_oracle_t *oracle)
{
    asian_affine_family_growth_carrier_t *growth = a64(sizeof(*growth));
    asian_affine_family_xgrowth_carrier_t *xgrowth = a64(sizeof(*xgrowth));
    asian_affine_family_arithmetic_request_t *ar = a64(sizeof(*ar));
    asian_genuine_strip_output_t *ao = a64(sizeof(*ao));
    asian_genuine_strip_output_t *go = a64(sizeof(*go));
    float strike = 100.0f;
    const asian_affine_family_carrier_input_t carrier =
        carrier_input(&principal, 64u);
    const asian_affine_family_request_input_t input = request_input(
        &principal, 64u, 0u, &strike, 1u, ASIAN_AFFINE_FAMILY_PRICE);
    int result = -1;
    if (growth == NULL || xgrowth == NULL || ar == NULL || ao == NULL ||
        go == NULL ||
        asian_affine_family_growth_carrier_prepare(engine, &carrier, growth) != 0 ||
        asian_affine_family_xgrowth_carrier_prepare(engine, &carrier, xgrowth) != 0 ||
        asian_affine_family_arithmetic_request_prepare_growth(
            engine, oracle, growth, &input, ASIAN_AFFINE_FAMILY_AFFINE, ar) != 0 ||
        asian_affine_family_arithmetic_prepared_price(ar, ao) != 0 ||
        compare_geocv(engine, oracle, xgrowth, &input, go) != 0)
        goto done;
    if (ao->values[0].call_price != 0x1.57d186d85266p+2 ||
        ao->values[0].put_price != 0x1.f07a78p+1 ||
        go->values[0].call_price != 0x1.563fd989e061ap+2 ||
        go->values[0].put_price != 0x1.ed571d631bf74p+1)
        goto done;
    result = 0;
done:
    free(growth); free(xgrowth); free(ar); free(ao); free(go);
    return result;
}

static int matrix(asian_affine_family_engine_t *engine,
                  asian_affine_family_generic_oracle_t *oracle)
{
    static const uint32_t fixings[] = {2u, 3u, 16u, 32u, 64u, 128u, 256u};
    static const uint32_t counts[] = {1u, 2u, 3u, 4u, 5u, 17u, 32u};
    asian_affine_family_growth_carrier_t *growth = a64(sizeof(*growth));
    asian_affine_family_xgrowth_carrier_t *xgrowth = a64(sizeof(*xgrowth));
    if (growth == NULL || xgrowth == NULL) {
        free(growth); free(xgrowth); return -1;
    }
    for (uint32_t m = 0; m < 4u; ++m) {
        for (size_t ni = 0; ni < sizeof(fixings) / sizeof(fixings[0]); ++ni) {
            const uint32_t n = fixings[ni];
            const asian_affine_family_carrier_input_t ci =
                carrier_input(&markets[m], n);
            if (asian_affine_family_growth_carrier_prepare(
                    engine, &ci, growth) != 0 ||
                asian_affine_family_xgrowth_carrier_prepare(
                    engine, &ci, xgrowth) != 0) {
                fprintf(stderr, "carrier N=%u market=%u\n", n, m);
                goto fail;
            }
            for (size_t ki = 0; ki < sizeof(counts) / sizeof(counts[0]); ++ki) {
                const uint32_t count = counts[ki];
                float strikes[32]; strikes_for(strikes, count, n + m + count);
                for (uint32_t workload = 0; workload < 2u; ++workload) {
                    const asian_affine_family_request_input_t input =
                        request_input(&markets[m], n, 0u, strikes, count,
                            (enum asian_affine_family_workload)workload);
                    if (compare_arithmetic(engine, oracle, growth, xgrowth,
                                           &input) != 0 ||
                        compare_geocv(engine, oracle, xgrowth, &input, NULL) != 0) {
                        fprintf(stderr, "matrix N=%u market=%u K=%u W=%u\n",
                                n, m, count, workload);
                        goto fail;
                    }
                }
            }
        }
    }
    free(growth); free(xgrowth); return 0;
fail:
    free(growth); free(xgrowth); return -1;
}

static int seasoning(asian_affine_family_engine_t *engine,
                     asian_affine_family_generic_oracle_t *oracle)
{
    static const uint32_t totals[] = {16u, 32u, 64u, 128u, 256u};
    static const uint32_t counts[] = {1u, 4u, 5u, 32u};
    asian_affine_family_growth_carrier_t *growth = a64(sizeof(*growth));
    asian_affine_family_xgrowth_carrier_t *xgrowth = a64(sizeof(*xgrowth));
    if (growth == NULL || xgrowth == NULL) goto fail;
    for (size_t ti = 0; ti < sizeof(totals) / sizeof(totals[0]); ++ti) {
        const uint32_t total = totals[ti];
        const uint32_t completed_cases[] = {1u, total / 2u, total - 2u,
                                            total - 1u};
        for (size_t ci = 0; ci < 4u; ++ci) {
            const uint32_t completed = completed_cases[ci];
            const uint32_t future = total - completed;
            const market_t *market = &markets[(ti + ci) % 4u];
            const asian_affine_family_carrier_input_t carrier =
                carrier_input(market, future);
            if (asian_affine_family_growth_carrier_prepare(
                    engine, &carrier, growth) != 0 ||
                asian_affine_family_xgrowth_carrier_prepare(
                    engine, &carrier, xgrowth) != 0)
                goto fail;
            for (size_t ki = 0; ki < 4u; ++ki) {
                float strikes[32];
                strikes_for(strikes, counts[ki], total + completed + counts[ki]);
                for (uint32_t workload = 0; workload < 2u; ++workload) {
                    const asian_affine_family_request_input_t input =
                        request_input(market, future, completed, strikes,
                            counts[ki],
                            (enum asian_affine_family_workload)workload);
                    if (compare_arithmetic(engine, oracle, growth, xgrowth,
                                           &input) != 0 ||
                        compare_geocv(engine, oracle, xgrowth, &input, NULL) != 0)
                        goto fail;
                }
            }
        }
    }
    free(growth); free(xgrowth); return 0;
fail:
    free(growth); free(xgrowth); return -1;
}

int main(void)
{
    asian_affine_family_engine_t *engine = a64(sizeof(*engine));
    asian_affine_family_generic_oracle_t *oracle = a64(sizeof(*oracle));
    if (engine == NULL || oracle == NULL) {
        fprintf(stderr, "family allocation failed\n");
        return 1;
    }
    if (asian_affine_family_engine_create(engine) != 0) {
        fprintf(stderr, "family engine failed\n");
        return 1;
    }
    if (lifecycle_boundaries(engine) != 0) {
        fprintf(stderr, "family lifecycle boundaries failed\n");
        return 1;
    }
    if (asian_affine_family_generic_oracle_create(oracle) != 0) {
        fprintf(stderr, "family generic oracle failed\n");
        return 1;
    }
    if (exact_n64(engine, oracle) != 0) {
        fprintf(stderr, "family N64 exact bits failed\n");
        return 1;
    }
    if (matrix(engine, oracle) != 0) {
        fprintf(stderr, "family matrix failed\n");
        return 1;
    }
    if (seasoning(engine, oracle) != 0) {
        fprintf(stderr, "family seasoning failed\n");
        return 1;
    }
    puts("family_routes D1..D256 PASS");
    puts("family_arithmetic N=2..256 K=1..32 PASS");
    puts("family_geocv N=2..256 K=1..32 PASS");
    puts("family_seasoning PASS");
    puts("family_n64_bits PASS");
    asian_affine_family_generic_oracle_destroy(oracle);
    asian_affine_family_engine_destroy(engine);
    free(oracle); free(engine);
    return 0;
}
