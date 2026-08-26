#define _POSIX_C_SOURCE 200112L

#include "private/asian_2048_vanilla_diag.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double rate, dividend, sigma, maturity; } market_t;

static const market_t markets[4] = {
    {-0.02, 0.01, 0.05, 0.25},
    { 0.00, 0.00, 0.40, 0.25},
    { 0.03, 0.00, 0.20, 1.00},
    { 0.03, 0.01, 0.05, 5.00},
};

static uint64_t leaf_calls;

void __real_asian_2048_affine_price_1_diag(
    const asian_2048_growth_context_t *, const asian_2048_payoff_context_t *,
    const asian_genuine_strip_strike_t *, asian_2048_output_t *);

void __wrap_asian_2048_affine_price_1_diag(
    const asian_2048_growth_context_t *growth,
    const asian_2048_payoff_context_t *payoff,
    const asian_genuine_strip_strike_t *strike, asian_2048_output_t *output)
{
    ++leaf_calls;
    __real_asian_2048_affine_price_1_diag(growth, payoff, strike, output);
}

static void *a64(size_t bytes)
{
    void *pointer = NULL;
    if (posix_memalign(&pointer, 64u, bytes) != 0)
        return NULL;
    memset(pointer, 0, bytes);
    return pointer;
}

static int carrier_guards_ok(const asian_2048_carrier_t *carrier)
{
    for (uint32_t byte = 0; byte < 64u; ++byte)
        if (carrier->guard_before[byte] != 0xa5u ||
            carrier->guard_after[byte] != 0x5au)
            return 0;
    return 1;
}

static asian_affine_family_carrier_input_t carrier_input(
    const market_t *market, uint32_t n)
{
    return (asian_affine_family_carrier_input_t){
        market->rate, market->dividend, market->sigma, market->maturity, n
    };
}

static asian_affine_family_request_input_t request_input(
    const market_t *market, uint32_t n, const float *strike)
{
    return (asian_affine_family_request_input_t){
        .s0 = 100.0,
        .rate = market->rate,
        .dividend_yield = market->dividend,
        .sigma = market->sigma,
        .maturity = market->maturity,
        .future_fixings = n,
        .completed_fixings = 0u,
        .initial_arithmetic_sum = 0.0,
        .past_log_sum = 0.0,
        .strikes = strike,
        .strike_count = 1u,
        .workload = ASIAN_AFFINE_FAMILY_PRICE,
    };
}

static int one_case(asian_2048_engine_t *engine,
                    asian_2048_generic_plan_t *generic,
                    const market_t *market, uint32_t n, float strike)
{
    const int snapshot_required = market == &markets[2] && strike == 100.0f &&
        (n == 2u || n == 64u || n == 256u);
    asian_2048_carrier_t *carrier = a64(sizeof(*carrier));
    asian_2048_carrier_t *carrier_snapshot = snapshot_required ?
        a64(sizeof(*carrier_snapshot)) : NULL;
    asian_affine_family_growth_carrier_t *parent = a64(sizeof(*parent));
    asian_2048_request_t *affine = a64(sizeof(*affine));
    asian_2048_request_t *oracle = a64(sizeof(*oracle));
    asian_2048_request_t *snapshot = a64(sizeof(*snapshot));
    unsigned char *output_storage = a64(64u + sizeof(asian_2048_output_t) + 64u);
    asian_2048_output_t *affine_output = output_storage == NULL ? NULL :
        (asian_2048_output_t *)(output_storage + 64u);
    asian_2048_output_t generic_output, repeated;
    int result = -1;
    if (carrier == NULL || (snapshot_required && carrier_snapshot == NULL) ||
        parent == NULL ||
        affine == NULL || oracle == NULL || snapshot == NULL ||
        output_storage == NULL)
        goto done;
    asian_2048_carrier_initialize(carrier);
    memset(output_storage, 0xa5, 64u + sizeof(*affine_output) + 64u);
    const asian_affine_family_carrier_input_t ci = carrier_input(market, n);
    const asian_affine_family_request_input_t ri =
        request_input(market, n, &strike);
    if (asian_2048_carrier_prepare(engine, &ci, carrier) != 0 ||
        asian_affine_family_growth_carrier_prepare(&engine->parent, &ci,
                                                    parent) != 0 ||
        memcmp(carrier->growth, parent->growth,
               ASIAN_2048_GROWTH_BYTES) != 0 ||
        !carrier_guards_ok(carrier) ||
        asian_2048_request_prepare(engine, carrier, &ri, affine) != 0 ||
        asian_2048_generic_request_prepare(generic, carrier, &ri, oracle) != 0)
        goto done;
    if (snapshot_required)
        memcpy(carrier_snapshot, carrier, sizeof(*carrier_snapshot));
    memcpy(snapshot, affine, sizeof(*snapshot));
    leaf_calls = 0u;
    if (asian_2048_prepared_price(affine, affine_output) != 0 ||
        leaf_calls != 1u ||
        memcmp(snapshot, affine, sizeof(*snapshot)) != 0)
        goto done;
    asian_2048_generic_price_1_diag(
        (const asian_genuine_arithmetic_growth_only_context_t *)&oracle->growth,
        &oracle->payoff, &oracle->strike, &generic_output);
    if (memcmp(affine_output, &generic_output, sizeof(generic_output)) != 0)
        goto done;
    leaf_calls = 0u;
    if (asian_2048_prepared_price(affine, &repeated) != 0 ||
        leaf_calls != 1u ||
        memcmp(affine_output, &repeated, sizeof(repeated)) != 0 ||
        !carrier_guards_ok(carrier) ||
        (snapshot_required &&
         memcmp(carrier_snapshot, carrier, sizeof(*carrier)) != 0) ||
        memcmp(snapshot, affine, sizeof(*snapshot)) != 0)
        goto done;
    for (uint32_t byte = 0; byte < 64u; ++byte)
        if (output_storage[byte] != 0xa5u ||
            output_storage[64u + sizeof(*affine_output) + byte] != 0xa5u)
            goto done;
    result = 0;
done:
    free(output_storage); free(snapshot); free(oracle); free(affine);
    free(parent); free(carrier_snapshot); free(carrier);
    return result;
}

static int pricing_matrix(asian_2048_engine_t *engine,
                          asian_2048_generic_plan_t *generic)
{
    for (uint32_t n = 2u; n <= 256u; ++n) {
        for (uint32_t market = 0; market < 4u; ++market) {
            static const float strikes[3] = {60.0f, 100.0f, 160.0f};
            for (uint32_t k = 0; k < 3u; ++k)
                if (one_case(engine, generic, &markets[market], n,
                             strikes[k]) != 0) {
                    fprintf(stderr, "pricing mismatch N=%u market=%u K=%g\n",
                            n, market, (double)strikes[k]);
                    return -1;
                }
        }
    }
    const market_t *market = &markets[2];
    const uint32_t ns[3] = {2u, 64u, 256u};
    for (uint32_t i = 0; i < 3u; ++i) {
        const uint32_t n = ns[i];
        const double dt = market->maturity / (double)n;
        double expected = 0.0;
        for (uint32_t fixing = 1; fixing <= n; ++fixing)
            expected += 100.0 * exp((market->rate - market->dividend) *
                                    dt * fixing);
        const float kink = (float)(expected / n);
        const float strikes[3] = {
            nextafterf(kink, -INFINITY), kink, nextafterf(kink, INFINITY)
        };
        for (uint32_t k = 0; k < 3u; ++k)
            if (one_case(engine, generic, market, n, strikes[k]) != 0)
                return -1;
    }
    return 0;
}

static int accuracy(asian_2048_engine_t *engine,
                    asian_2048_generic_plan_t *generic)
{
    (void)generic;
    const market_t *market = &markets[2];
    const uint32_t n = 64u;
    const float strike = 100.0f;
    asian_2048_carrier_t *carrier = a64(sizeof(*carrier));
    asian_2048_request_t *request = a64(sizeof(*request));
    asian_affine_family_growth_carrier_t *parent_carrier = a64(sizeof(*parent_carrier));
    asian_affine_family_arithmetic_request_t *parent_request = a64(sizeof(*parent_request));
    asian_genuine_strip_output_t *parent_output = a64(sizeof(*parent_output));
    asian_4096_vanilla_request_t *compact_request = a64(sizeof(*compact_request));
    asian_2048_output_t compact_output;
    asian_2048_output_t candidate;
    int result = -1;
    if (carrier == NULL || request == NULL || parent_carrier == NULL ||
        parent_request == NULL || parent_output == NULL || compact_request == NULL)
        goto done;
    const asian_affine_family_carrier_input_t ci = carrier_input(market, n);
    const asian_affine_family_request_input_t ri = request_input(market, n, &strike);
    if (asian_2048_carrier_prepare(engine, &ci, carrier) != 0 ||
        asian_2048_request_prepare(engine, carrier, &ri, request) != 0 ||
        asian_2048_prepared_price(request, &candidate) != 0 ||
        asian_affine_family_growth_carrier_prepare(&engine->parent, &ci,
                                                    parent_carrier) != 0 ||
        asian_affine_family_arithmetic_request_prepare_growth(
            &engine->parent, NULL, parent_carrier, &ri,
            ASIAN_AFFINE_FAMILY_AFFINE, parent_request) != 0 ||
        asian_affine_family_arithmetic_prepared_price(parent_request,
                                                       parent_output) != 0 ||
        asian_4096_vanilla_request_prepare(engine, carrier, &ri,
                                            compact_request) != 0 ||
        asian_4096_vanilla_prepared_price(compact_request, &compact_output) != 0 ||
        memcmp(&compact_output, &parent_output->values[0],
               sizeof(compact_output)) != 0)
        goto done;
    const double references[2] = {5.34876366, 3.85536588};
    const double values2048[2] = {candidate.call_price, candidate.put_price};
    const double values4096[2] = {parent_output->values[0].call_price,
                                 parent_output->values[0].put_price};
    const char *sides[2] = {"call", "put"};
    puts("side reference plain_2048 plain_2048_signed_error "
         "plain_2048_abs_error plain_4096 plain_4096_signed_error "
         "plain_4096_abs_error");
    for (uint32_t side = 0; side < 2u; ++side) {
        const double e2 = values2048[side] - references[side];
        const double e4 = values4096[side] - references[side];
        printf("%s %.8f %.17g %.17g %.17g %.17g %.17g %.17g\n",
               sides[side], references[side], values2048[side], e2, fabs(e2),
               values4096[side], e4, fabs(e4));
    }
    result = 0;
done:
    free(compact_request); free(parent_output); free(parent_request);
    free(parent_carrier);
    free(request); free(carrier);
    return result;
}

int main(void)
{
    asian_2048_engine_t *engine = a64(sizeof(*engine));
    asian_2048_compact_plan_t *plan_snapshot = a64(sizeof(*plan_snapshot));
    asian_2048_generic_plan_t *generic = NULL;
    if (engine == NULL || plan_snapshot == NULL ||
        asian_2048_structural_check() != 0 ||
        asian_2048_engine_create(engine) != 0 ||
        asian_2048_generic_plan_create(&generic) != 0) {
        fprintf(stderr, "asian_2048_vanilla initialization FAIL\n");
        free(plan_snapshot); free(engine);
        return 1;
    }
    memcpy(plan_snapshot, engine->plan, sizeof(*plan_snapshot));
    if (pricing_matrix(engine, generic) != 0 || accuracy(engine, generic) != 0 ||
        memcmp(plan_snapshot, engine->plan, sizeof(*plan_snapshot)) != 0) {
        fprintf(stderr, "asian_2048_vanilla correctness FAIL\n");
        asian_2048_generic_plan_destroy(generic);
        if (engine != NULL) asian_2048_engine_destroy(engine);
        free(plan_snapshot);
        free(engine);
        return 1;
    }
    asian_2048_generic_plan_destroy(generic);
    asian_2048_engine_destroy(engine);
    free(plan_snapshot);
    free(engine);
    puts("asian_2048_vanilla correctness PASS paths=2048 packets=64 "
         "D1_D256=PASS generic_affine_exact=PASS guards=PASS repeated=PASS");
    return 0;
}
