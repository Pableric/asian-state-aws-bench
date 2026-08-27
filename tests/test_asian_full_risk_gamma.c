#define _GNU_SOURCE

#include "private/asian_full_risk_gamma_diag.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double rate, dividend, sigma, maturity;
} market_t;

static const market_t markets[4] = {
    {-0.02, 0.01, 0.05, 0.25},
    { 0.00, 0.00, 0.40, 0.25},
    { 0.03, 0.00, 0.20, 1.00},
    { 0.03, 0.01, 0.05, 5.00},
};

typedef struct {
    unsigned char *allocation;
    asian_full_risk_gamma_output_t *value;
} guarded_output_t;

static void *a64(size_t bytes)
{
    void *value = NULL;
    if (posix_memalign(&value, 64u, bytes) != 0)
        return NULL;
    memset(value, 0, bytes);
    return value;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t bytes)
{
    const unsigned char *value = data;
    while (bytes-- != 0u) {
        hash ^= *value++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int guarded_create(guarded_output_t *guard)
{
    guard->allocation = a64(sizeof(*guard->value) + 128u);
    if (guard->allocation == NULL)
        return -1;
    memset(guard->allocation, 0xa5, sizeof(*guard->value) + 128u);
    guard->value = (asian_full_risk_gamma_output_t *)(guard->allocation + 64u);
    return 0;
}

static int guarded_ok(const guarded_output_t *guard)
{
    for (size_t index = 0; index < 64u; ++index)
        if (guard->allocation[index] != 0xa5u ||
            guard->allocation[64u + sizeof(*guard->value) + index] != 0xa5u)
            return 0;
    return 1;
}

static int byte_guards_ok(const unsigned char *allocation,
                          size_t payload_bytes)
{
    for (size_t index = 0; index < 64u; ++index)
        if (allocation[index] != 0xa5u ||
            allocation[64u + payload_bytes + index] != 0xa5u)
            return 0;
    return 1;
}

static int same_parent_fields(
    const asian_full_risk_gamma_output_t *gamma,
    const asian_affine_family_full_risk_k1_output_t *parent)
{
    return memcmp(&gamma->call.price, &parent->call.price, sizeof(double)) == 0 &&
           memcmp(&gamma->call.delta, &parent->call.delta, sizeof(double)) == 0 &&
           memcmp(&gamma->call.vega, &parent->call.vega, sizeof(double)) == 0 &&
           memcmp(&gamma->call.rho, &parent->call.rho, sizeof(double)) == 0 &&
           memcmp(&gamma->put.price, &parent->put.price, sizeof(double)) == 0 &&
           memcmp(&gamma->put.delta, &parent->put.delta, sizeof(double)) == 0 &&
           memcmp(&gamma->put.vega, &parent->put.vega, sizeof(double)) == 0 &&
           memcmp(&gamma->put.rho, &parent->put.rho, sizeof(double)) == 0;
}

static int check_case(asian_affine_family_engine_t *engine, uint32_t n,
                      uint32_t market_index, uint32_t completed,
                      float strike, double fraction, int verbose)
{
    const market_t *market = &markets[market_index % 4u];
    unsigned char *carrier_allocation =
        a64(sizeof(asian_affine_family_xgrowth_carrier_t) + 128u);
    asian_affine_family_xgrowth_carrier_t *carrier = carrier_allocation == NULL ?
        NULL : (asian_affine_family_xgrowth_carrier_t *)(carrier_allocation +
                                                         64u);
    asian_full_risk_gamma_request_t *request = a64(sizeof(*request));
    asian_full_risk_gamma_request_t *repeat_request = a64(sizeof(*request));
    asian_affine_family_full_risk_k1_request_t *parent_request =
        a64(sizeof(*parent_request));
    guarded_output_t fused = {0}, scalar = {0}, repeated = {0}, triple = {0};
    asian_full_risk_gamma_output_t parent_fields
        __attribute__((aligned(64)));
    asian_affine_family_full_risk_k1_output_t parent_output
        __attribute__((aligned(64)));
    int result = -1;
    uint64_t engine_hash = 0u, plan_hash = 0u, carrier_hash = 0u;
    const char *stage = "allocation";
    if (carrier_allocation != NULL)
        memset(carrier_allocation, 0xa5,
               sizeof(asian_affine_family_xgrowth_carrier_t) + 128u);
    if (carrier == NULL || request == NULL || repeat_request == NULL ||
        parent_request == NULL || guarded_create(&fused) != 0 ||
        guarded_create(&scalar) != 0 || guarded_create(&repeated) != 0 ||
        guarded_create(&triple) != 0)
        goto done;

    const asian_affine_family_carrier_input_t carrier_input = {
        market->rate, market->dividend, market->sigma, market->maturity, n};
    asian_full_risk_gamma_request_input_t input;
    memset(&input, 0, sizeof(input));
    input.family.s0 = 100.0;
    input.family.rate = market->rate;
    input.family.dividend_yield = market->dividend;
    input.family.sigma = market->sigma;
    input.family.maturity = market->maturity;
    input.family.future_fixings = n;
    input.family.completed_fixings = completed;
    input.family.initial_arithmetic_sum = completed == 0u ?
        0.0 : 95.0 * (double)completed;
    input.family.past_log_sum = completed == 0u ?
        0.0 : log(95.0) * (double)completed;
    input.family.strikes = &strike;
    input.family.strike_count = 1u;
    input.family.workload = ASIAN_AFFINE_FAMILY_PRICE;
    input.gamma_bump_fraction = fraction;
    input.workload = ASIAN_FULL_RISK_PRICE_DELTA_GAMMA_VEGA_RHO;

    stage = "carrier_prepare";
    asian_full_risk_gamma_leaf_counter_reset();
    if (asian_affine_family_xgrowth_carrier_prepare(engine, &carrier_input,
            carrier) != ASIAN_AFFINE_FAMILY_OK ||
        asian_full_risk_gamma_leaf_counter_read() != 0u)
        goto done;
    engine_hash = hash_bytes(UINT64_C(1469598103934665603), engine,
                             sizeof(*engine));
    plan_hash = hash_bytes(UINT64_C(1469598103934665603),
                           engine->affine_plan,
                           sizeof(*engine->affine_plan));
    carrier_hash = hash_bytes(UINT64_C(1469598103934665603), carrier,
                              sizeof(*carrier));
    asian_full_risk_gamma_leaf_counter_reset();
    stage = "gamma_prepare";
    if (asian_full_risk_gamma_request_prepare(engine, carrier, &input,
            request) != ASIAN_FULL_RISK_GAMMA_OK ||
        asian_full_risk_gamma_leaf_counter_read() != 0u)
        goto done;
    const asian_full_risk_gamma_request_t frozen_request = *request;
    stage = "repeat_prepare";
    if (asian_full_risk_gamma_request_prepare(engine, carrier, &input,
            repeat_request) != ASIAN_FULL_RISK_GAMMA_OK ||
        memcmp(request->routes, repeat_request->routes,
               n * sizeof(request->routes[0])) != 0 ||
        memcmp(&request->controls, &repeat_request->controls,
               sizeof(request->controls)) != 0 ||
        memcmp(&request->parity, &repeat_request->parity,
               sizeof(request->parity)) != 0 ||
        memcmp((const unsigned char *)&request->context + 24u,
               (const unsigned char *)&repeat_request->context + 24u,
               sizeof(request->context) - 24u) != 0)
        goto done;

    stage = "fused_price";
    asian_full_risk_gamma_leaf_counter_reset();
    if (asian_full_risk_gamma_prepared_price(request, fused.value) !=
            ASIAN_FULL_RISK_GAMMA_OK ||
        asian_full_risk_gamma_leaf_counter_read() != 1u ||
        memcmp(request, &frozen_request, sizeof(*request)) != 0)
        goto done;
    stage = "scalar_identity";
    if (asian_full_risk_gamma_scalar_oracle(request, scalar.value) !=
            ASIAN_FULL_RISK_GAMMA_OK ||
        memcmp(fused.value, scalar.value, sizeof(*fused.value)) != 0)
        goto mismatch;
    stage = "repeat_identity";
    if (asian_full_risk_gamma_prepared_price(request, repeated.value) !=
            ASIAN_FULL_RISK_GAMMA_OK ||
        memcmp(fused.value, repeated.value, sizeof(*fused.value)) != 0 ||
        !guarded_ok(&fused) || !guarded_ok(&scalar) ||
        !guarded_ok(&repeated))
        goto done;
    stage = "gamma_fields";
    if (memcmp(&fused.value->call.gamma, &fused.value->put.gamma,
               sizeof(double)) != 0 || !isfinite(fused.value->call.gamma))
        goto done;
    stage = "unchanged_parent_fields";
    if (asian_full_risk_gamma_parent_fields_oracle(request,
            &parent_fields) != ASIAN_FULL_RISK_GAMMA_OK ||
        memcmp(&fused.value->call.price, &parent_fields.call.price,
               2u * sizeof(double)) != 0 ||
        memcmp(&fused.value->call.vega, &parent_fields.call.vega,
               2u * sizeof(double)) != 0 ||
        memcmp(&fused.value->put.price, &parent_fields.put.price,
               2u * sizeof(double)) != 0 ||
        memcmp(&fused.value->put.vega, &parent_fields.put.vega,
               2u * sizeof(double)) != 0)
        goto done;

    if (completed == 0u) {
        stage = "parent_identity";
        if (asian_affine_family_full_risk_k1_request_prepare(engine, carrier,
                &input.family, parent_request) != ASIAN_AFFINE_FAMILY_OK ||
            memcmp(request->routes, parent_request->routes,
                   n * sizeof(request->routes[0])) != 0 ||
            memcmp(&request->controls, &parent_request->controls,
                   sizeof(request->controls)) != 0 ||
            memcmp((const unsigned char *)&request->context.phase1 + 24u,
                   (const unsigned char *)&parent_request->context + 24u,
                   sizeof(parent_request->context) - 24u) != 0 ||
            memcmp(&request->parity, &parent_request->parity,
                   sizeof(request->parity)) != 0 ||
            asian_affine_family_full_risk_k1_prepared_price(parent_request,
                &parent_output) != ASIAN_AFFINE_FAMILY_OK ||
            !same_parent_fields(fused.value, &parent_output))
            goto done;
    }
    stage = "triple_reprice";
    if (asian_full_risk_gamma_triple_reprice(engine, carrier, &input,
            triple.value) != ASIAN_FULL_RISK_GAMMA_OK ||
        !isfinite(triple.value->call.gamma) ||
        !isfinite(triple.value->put.gamma) || !guarded_ok(&triple))
        goto done;
    if (verbose)
        printf("gamma_case N=%u market=%u completed=%u strike=%a fraction=%a "
               "side=%s bump=%a call_gamma=%a put_gamma=%a "
               "triple_call=%a triple_put=%a identity=PASS\n",
               n, market_index, completed, strike, fraction,
               (request->parity.flags & ASIAN_GENUINE_MSFR_DIRECT_CALL) ?
                   "call" : "put",
               (double)request->context.effective_spot_bump,
               fused.value->call.gamma, fused.value->put.gamma,
               triple.value->call.gamma, triple.value->put.gamma);
    stage = "immutable_inputs";
    if (engine_hash != hash_bytes(UINT64_C(1469598103934665603), engine,
                                  sizeof(*engine)) ||
        plan_hash != hash_bytes(UINT64_C(1469598103934665603),
                                engine->affine_plan,
                                sizeof(*engine->affine_plan)) ||
        carrier_hash != hash_bytes(UINT64_C(1469598103934665603), carrier,
                                   sizeof(*carrier)) ||
        !byte_guards_ok(carrier_allocation, sizeof(*carrier)))
        goto done;
    result = 0;
    goto done;

mismatch:
    fprintf(stderr,
        "gamma mismatch N=%u market=%u completed=%u strike=%a fraction=%a\n"
        " fused call=%a,%a,%a,%a,%a put=%a,%a,%a,%a,%a\n"
        " scalar call=%a,%a,%a,%a,%a put=%a,%a,%a,%a,%a\n",
        n, market_index, completed, strike, fraction,
        fused.value->call.price, fused.value->call.delta,
        fused.value->call.gamma, fused.value->call.vega,
        fused.value->call.rho, fused.value->put.price,
        fused.value->put.delta, fused.value->put.gamma,
        fused.value->put.vega, fused.value->put.rho,
        scalar.value->call.price, scalar.value->call.delta,
        scalar.value->call.gamma, scalar.value->call.vega,
        scalar.value->call.rho, scalar.value->put.price,
        scalar.value->put.delta, scalar.value->put.gamma,
        scalar.value->put.vega, scalar.value->put.rho);
done:
    if (result != 0)
        fprintf(stderr, "gamma case failed stage=%s N=%u completed=%u\n",
                stage, n, completed);
    free(fused.allocation); free(scalar.allocation);
    free(repeated.allocation); free(triple.allocation);
    free(parent_request); free(repeat_request); free(request);
    free(carrier_allocation);
    return result;
}

static int invalid_input_checks(asian_affine_family_engine_t *engine)
{
    const market_t *market = &markets[2];
    const uint32_t n = 64u;
    float strike = 100.0f;
    asian_affine_family_xgrowth_carrier_t *carrier = a64(sizeof(*carrier));
    asian_full_risk_gamma_request_t *request = a64(sizeof(*request));
    if (carrier == NULL || request == NULL)
        return -1;
    const asian_affine_family_carrier_input_t ci = {
        market->rate, market->dividend, market->sigma, market->maturity, n};
    asian_full_risk_gamma_request_input_t input = {
        .family = {
            .s0 = 100.0, .rate = market->rate,
            .dividend_yield = market->dividend, .sigma = market->sigma,
            .maturity = market->maturity, .future_fixings = n,
            .completed_fixings = 0u, .initial_arithmetic_sum = 0.0,
            .past_log_sum = 0.0, .strikes = &strike, .strike_count = 1u,
            .workload = ASIAN_AFFINE_FAMILY_PRICE,
        },
        .gamma_bump_fraction = 0.01,
        .workload = ASIAN_FULL_RISK_PRICE_DELTA_GAMMA_VEGA_RHO,
    };
    int result = -1;
    if (asian_affine_family_xgrowth_carrier_prepare(engine, &ci, carrier) != 0)
        goto done;
    const double bad[] = {0.0, -0.01, 0.015, NAN, INFINITY};
    for (size_t index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        input.gamma_bump_fraction = bad[index];
        asian_full_risk_gamma_leaf_counter_reset();
        if (asian_full_risk_gamma_request_prepare(engine, carrier, &input,
                request) == ASIAN_FULL_RISK_GAMMA_OK ||
            asian_full_risk_gamma_leaf_counter_read() != 0u)
            goto done;
    }
    input.gamma_bump_fraction = 0.02;
    input.family.s0 = 1.0e-45;
    if (asian_full_risk_gamma_request_prepare(engine, carrier, &input,
            request) == ASIAN_FULL_RISK_GAMMA_OK)
        goto done;
    result = 0;
done:
    free(request); free(carrier);
    return result;
}

static int accuracy_rows(asian_affine_family_engine_t *engine)
{
    const uint32_t n = 64u;
    const market_t *market = &markets[2];
    float strike = 100.0f;
    const asian_affine_family_carrier_input_t ci = {
        market->rate, market->dividend, market->sigma, market->maturity, n};
    asian_affine_family_xgrowth_carrier_t *carrier = a64(sizeof(*carrier));
    asian_full_risk_gamma_request_t *request = a64(sizeof(*request));
    asian_full_risk_gamma_output_t fused __attribute__((aligned(64)));
    asian_full_risk_gamma_output_t scalar __attribute__((aligned(64)));
    asian_full_risk_gamma_output_t triple __attribute__((aligned(64)));
    if (carrier == NULL || request == NULL ||
        asian_affine_family_xgrowth_carrier_prepare(engine, &ci, carrier) != 0)
        return -1;
    const double fractions[] = {0.005, 0.01, 0.02};
    for (size_t index = 0; index < 3u; ++index) {
        asian_full_risk_gamma_request_input_t input;
        memset(&input, 0, sizeof(input));
        input.family.s0 = 100.0;
        input.family.rate = market->rate;
        input.family.dividend_yield = market->dividend;
        input.family.sigma = market->sigma;
        input.family.maturity = market->maturity;
        input.family.future_fixings = n;
        input.family.strikes = &strike;
        input.family.strike_count = 1u;
        input.family.workload = ASIAN_AFFINE_FAMILY_PRICE;
        input.gamma_bump_fraction = fractions[index];
        input.workload = ASIAN_FULL_RISK_PRICE_DELTA_GAMMA_VEGA_RHO;
        if (asian_full_risk_gamma_request_prepare(engine, carrier, &input,
                request) != 0 ||
            asian_full_risk_gamma_prepared_price(request, &fused) != 0 ||
            asian_full_risk_gamma_scalar_oracle(request, &scalar) != 0 ||
            asian_full_risk_gamma_triple_reprice(engine, carrier, &input,
                &triple) != 0 ||
            memcmp(&fused, &scalar, sizeof(fused)) != 0) {
            free(request); free(carrier);
            return -1;
        }
        printf("OUR_GAMMA basis=64 first_tick=1 step_tick=1 n=64 "
               "s0=%a strike=%a q=%a r=%a sigma=%a maturity=%a "
               "fraction=%a bump=%a fused_call=%a fused_put=%a "
               "triple_call=%a triple_put=%a scalar_identity=YES "
               "call_put_gamma_equal=%s\n",
               100.0, 100.0, 0.0, market->rate, market->sigma,
               market->maturity, fractions[index],
               fused.call.effective_spot_bump,
               fused.call.gamma, fused.put.gamma,
               triple.call.gamma, triple.put.gamma,
               memcmp(&fused.call.gamma, &fused.put.gamma,
                      sizeof(double)) == 0 ? "YES" : "NO");
    }
    free(request); free(carrier);
    return 0;
}

static int targeted_kink_and_history_checks(
    asian_affine_family_engine_t *engine)
{
    const uint32_t n = 64u;
    const market_t *market = &markets[2];
    float strike = 100.0f;
    const asian_affine_family_carrier_input_t ci = {
        market->rate, market->dividend, market->sigma, market->maturity, n};
    asian_affine_family_xgrowth_carrier_t *carrier = a64(sizeof(*carrier));
    asian_full_risk_gamma_request_t *request = a64(sizeof(*request));
    asian_full_risk_gamma_triple_request_t *triple = a64(sizeof(*triple));
    float *base = a64(4096u * sizeof(float));
    float *plus = a64(4096u * sizeof(float));
    float *minus = a64(4096u * sizeof(float));
    int result = -1;
    if (carrier == NULL || request == NULL || triple == NULL || base == NULL ||
        plus == NULL || minus == NULL)
        goto done;
    asian_full_risk_gamma_request_input_t input;
    memset(&input, 0, sizeof(input));
    input.family.s0 = 100.0;
    input.family.rate = market->rate;
    input.family.dividend_yield = market->dividend;
    input.family.sigma = market->sigma;
    input.family.maturity = market->maturity;
    input.family.future_fixings = n;
    input.family.strikes = &strike;
    input.family.strike_count = 1u;
    input.family.workload = ASIAN_AFFINE_FAMILY_PRICE;
    input.gamma_bump_fraction = 0.01;
    input.workload = ASIAN_FULL_RISK_PRICE_DELTA_GAMMA_VEGA_RHO;
    if (asian_affine_family_xgrowth_carrier_prepare(engine, &ci, carrier) != 0 ||
        asian_full_risk_gamma_request_prepare(engine, carrier, &input,
            request) != 0 ||
        asian_full_risk_gamma_scalar_path_averages(request, base, plus,
            minus) != 0)
        goto done;

    const float kinks[3] = {minus[17], base[123], plus[2049]};
    const float *series[3] = {minus, base, plus};
    for (uint32_t kind = 0; kind < 3u; ++kind) {
        uint32_t equal = 0u;
        for (uint32_t path = 0; path < 4096u; ++path)
            equal += series[kind][path] == kinks[kind];
        if (equal == 0u || check_case(engine, n, 2u, 0u, kinks[kind],
                                      0.01, 0) != 0)
            goto done;
    }
    float max_plus = plus[0];
    for (uint32_t path = 1u; path < 4096u; ++path)
        if (plus[path] > max_plus)
            max_plus = plus[path];
    const float outside = nextafterf(max_plus, INFINITY);
    uint32_t inside = 0u;
    for (uint32_t path = 0; path < 4096u; ++path)
        inside += outside >= minus[path] && outside <= plus[path];
    if (inside != 0u || check_case(engine, n, 2u, 0u, outside, 0.01, 0) != 0)
        goto done;

    double expected_sum = 0.0;
    const double dt = market->maturity / (double)n;
    for (uint32_t fixing = 1u; fixing <= n; ++fixing)
        expected_sum += 100.0 * exp((market->rate - market->dividend) *
                                    dt * (double)fixing);
    strike = (float)(expected_sum / (double)n);
    input.family.strikes = &strike;
    if (asian_full_risk_gamma_triple_request_prepare(engine, carrier, &input,
            triple) != 0)
        goto done;
    uint32_t direct_mask = 0u;
    for (uint32_t bump = 0; bump < 3u; ++bump)
        direct_mask |= (triple->request[bump].parity.flags &
            ASIAN_GENUINE_MSFR_DIRECT_CALL) ? 1u : 2u;
    if (direct_mask != 3u)
        goto done;

    if (check_case(engine, 2u, 2u, 254u, 100.0f, 0.01, 0) != 0)
        goto done;
    result = 0;
done:
    free(minus); free(plus); free(base); free(triple); free(request);
    free(carrier);
    return result;
}

int main(int argc, char **argv)
{
    uint32_t first = 2u, last = 256u;
    int verbose = 0;
    if (argc == 3 && strcmp(argv[1], "--n") == 0) {
        first = last = (uint32_t)strtoul(argv[2], NULL, 10);
        verbose = 1;
    } else if (argc == 2 && strcmp(argv[1], "--accuracy") == 0) {
        first = 1u;
        last = 0u;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [--n N]\n", argv[0]);
        return 2;
    }
    asian_affine_family_engine_t engine __attribute__((aligned(64)));
    memset(&engine, 0, sizeof(engine));
    if (asian_affine_family_engine_create(&engine) != ASIAN_AFFINE_FAMILY_OK)
        return 1;
    if (first > last) {
        const int status = accuracy_rows(&engine);
        asian_affine_family_engine_destroy(&engine);
        return status == 0 ? 0 : 1;
    }
    int failed = invalid_input_checks(&engine);
    static const float strikes[3] = {80.0f, 100.0f, 125.0f};
    static const double fractions[3] = {0.005, 0.01, 0.02};
    for (uint32_t n = first; failed == 0 && n <= last; ++n) {
        const uint32_t completed = n < 256u && n % 5u == 0u ?
            (12u < 256u - n ? 12u : 256u - n) : 0u;
        if (check_case(&engine, n, n % 4u, completed,
                       strikes[n % 3u], fractions[n % 3u], verbose) != 0)
            failed = 1;
    }
    const uint32_t boundary[] = {2u, 16u, 64u, 128u, 255u, 256u};
    for (size_t ni = 0; failed == 0 &&
         ni < sizeof(boundary) / sizeof(boundary[0]); ++ni) {
        const uint32_t n = boundary[ni];
        for (uint32_t market = 0; failed == 0 && market < 4u; ++market)
            for (uint32_t side = 0; failed == 0 && side < 2u; ++side)
                for (uint32_t bump = 0; failed == 0 && bump < 3u; ++bump) {
                    const uint32_t completed = n <= 244u &&
                        ((market + side + bump) & 1u) ? 12u : 0u;
                    const float strike = side == 0u ? 125.0f : 80.0f;
                    if (check_case(&engine, n, market, completed, strike,
                                   fractions[bump], 0) != 0)
                        failed = 1;
                }
    }
    if (failed == 0 && targeted_kink_and_history_checks(&engine) != 0)
        failed = 1;
    asian_affine_family_engine_destroy(&engine);
    if (failed != 0)
        return 1;
    puts("asian_full_risk_gamma_correctness PASS N=2..256 markets=4 "
         "sides=call_put seasoning=YES bumps=0.005,0.01,0.02 "
         "parent_fields_exact=YES scalar_gamma_exact=YES "
         "bumped_kinks=minus,base,plus no_window=PASS "
         "otm_side_change=PASS history_dominates=PASS "
         "carrier_prepare_leaf_calls=0 request_prepare_leaf_calls=0 "
         "phase1_leaves_per_valuation=1 guards=PASS immutable=PASS");
    return 0;
}
