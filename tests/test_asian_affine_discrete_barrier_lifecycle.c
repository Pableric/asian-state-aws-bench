#define _POSIX_C_SOURCE 200112L

#include "private/asian_affine_discrete_barrier_lifecycle_diag.h"
#include "private/asian_genuine_discrete_barrier_diag.h"
#include "private/asian_genuine_permute.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GENERIC_PLAN_MAGIC = 0x54525351u, JOE_KUO_RECORD_BYTES = 132 };

struct asian_meta_qsort_control_plan {
    uint32_t magic;
    uint8_t donor_region[ASIAN_META_DIRECTIONS];
    uint8_t reserved[60];
    fragment_map_t maps[ASIAN_META_DIRECTIONS];
};

extern const unsigned char asian_arithmetic_joe_kuo_256_records[];
extern uint64_t asian_affine_barrier_test_leaf_invocations;

typedef struct { double rate, dividend, sigma, maturity; } market_t;
static const market_t markets[4] = {
    {-0.02, 0.01, 0.05, 0.25},
    { 0.00, 0.00, 0.40, 0.25},
    { 0.03, 0.00, 0.20, 1.00},
    { 0.03, 0.01, 0.05, 5.00},
};

static void *a64(size_t bytes)
{
    void *pointer = NULL;
    return posix_memalign(&pointer, 64u, bytes) == 0 ? pointer : NULL;
}

static int bit_equal_double(double a, double b)
{
    uint64_t aa, bb;
    memcpy(&aa, &a, 8u); memcpy(&bb, &b, 8u);
    return aa == bb;
}

static uint32_t sobol_word(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray = index ^ (index >> 1), word = 0u;
    for (uint32_t bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u) word ^= directions[bit];
    return word;
}

static void direction_row(uint32_t dimension, uint32_t out[32])
{
    const unsigned char *record = asian_arithmetic_joe_kuo_256_records +
        (size_t)dimension * JOE_KUO_RECORD_BYTES;
    uint32_t count;
    memcpy(&count, record, 4u);
    if (count != 32u) abort();
    memcpy(out, record + 4u, 128u);
}

static uint32_t generic_source(const fragment_map_t *map, uint32_t path)
{
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    const uint32_t pattern = map->select[packet][2u + half];
    return (uint32_t)map->select[packet][half] * 16u +
           map->patterns[pattern][lane];
}

static uint32_t affine_source(const asian_meta_dim_affine_ctx_t *map,
                              uint32_t path)
{
    const uint32_t packet = path >> 5;
    const uint32_t half = (path >> 4) & 1u;
    const uint32_t lane = path & 15u;
    uint32_t control = map->base_control[lane] ^ map->sel2[packet][1];
    if (half != 0u) control ^= map->half_delta[lane];
    return ((uint32_t)(map->sel2[packet][0] ^ half) << 4) | control;
}

static int route_integrity(const asian_meta_affine_plan_t *affine,
                           const struct asian_meta_qsort_control_plan *generic)
{
    uint32_t d1[32], target[32];
    direction_row(0u, d1);
    for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
         ++dimension) {
        direction_row(dimension, target);
        uint8_t *seen = calloc(ASIAN_META_PATHS, 1u);
        if (seen == NULL) return -1;
        if (affine->donor_region[dimension] !=
            generic->donor_region[dimension]) { free(seen); return -1; }
        for (uint32_t path = 0; path < ASIAN_META_PATHS; ++path) {
            const uint32_t gs = generic_source(&generic->maps[dimension], path);
            const uint32_t as = affine_source(&affine->contexts[dimension], path);
            if (gs != as || gs >= ASIAN_META_PATHS || seen[gs] != 0u) {
                free(seen); return -1;
            }
            seen[gs] = 1u;
            const uint32_t donor_index =
                8192u + generic->donor_region[dimension] * ASIAN_META_PATHS + gs;
            if (sobol_word(donor_index, d1) !=
                sobol_word(8192u + path, target)) { free(seen); return -1; }
        }
        free(seen);
    }
    return 0;
}

static asian_affine_barrier_market_input_t market_input(const market_t *market,
                                                         uint32_t n)
{
    const asian_affine_barrier_market_input_t out = {
        market->rate, market->dividend, market->sigma, market->maturity, n};
    return out;
}

static asian_affine_barrier_request_input_t request_input(
    const market_t *market, uint32_t n, float strike, float barrier,
    enum asian_affine_barrier_product product,
    enum asian_affine_barrier_direction direction,
    enum asian_affine_barrier_side side,
    enum asian_affine_barrier_initial_state state)
{
    const asian_affine_barrier_request_input_t out = {
        .s0 = 100.0, .strike = strike, .barrier = barrier,
        .rate = market->rate, .dividend_yield = market->dividend,
        .sigma = market->sigma, .maturity = market->maturity,
        .monitoring_count = n, .product = product, .direction = direction,
        .side = side, .initial_state = state,
    };
    return out;
}

static void generic_routes(
    const struct asian_meta_qsort_control_plan *plan,
    const asian_affine_barrier_carrier_t *carrier, uint32_t n,
    asian_genuine_route_t routes[ASIAN_META_DIRECTIONS])
{
    for (uint32_t dimension = 0; dimension < n; ++dimension) {
        routes[dimension].x_base = NULL;
        routes[dimension].growth_base = carrier->growth +
            (size_t)plan->donor_region[dimension] * ASIAN_META_PATHS;
        routes[dimension].map = &plan->maps[dimension];
        routes[dimension].weight_bits = 0u;
        routes[dimension].fixing_index = dimension;
    }
}

static float fmul(float a, float b) { volatile float x = a * b; return x; }
static float fadd(float a, float b) { volatile float x = a + b; return x; }
static float fsub(float a, float b) { volatile float x = a - b; return x; }

static double scalar_price(const asian_genuine_route_t *routes, uint32_t n,
                           float s0, float barrier, float strike,
                           enum asian_affine_barrier_product product,
                           enum asian_affine_barrier_direction direction,
                           enum asian_affine_barrier_side side,
                           enum asian_affine_barrier_initial_state state,
                           double scale)
{
    float lo[16] = {0}, hi[16] = {0};
    for (uint32_t path = 0; path < ASIAN_AFFINE_BARRIER_PATHS; ++path) {
        float s = s0;
        int alive = state == ASIAN_AFFINE_BARRIER_KNOWN_ALIVE;
        for (uint32_t dimension = 0; dimension < n; ++dimension) {
            const uint32_t source = generic_source(routes[dimension].map, path);
            s = fmul(s, routes[dimension].growth_base[source]);
            if (product == ASIAN_AFFINE_BARRIER_KNOCK_OUT) {
                const int survives = direction == ASIAN_AFFINE_BARRIER_DOWN ?
                    isfinite(s) && s > barrier : isfinite(s) && s < barrier;
                alive = alive && survives;
            }
        }
        float payoff = side == ASIAN_AFFINE_BARRIER_CALL ?
            fsub(s, strike) : fsub(strike, s);
        if (payoff < 0.0f || !alive) payoff = 0.0f;
        float *acc = (path & 16u) == 0u ? lo : hi;
        acc[path & 15u] = fadd(acc[path & 15u], payoff);
    }
    float lanes[16];
    for (uint32_t lane = 0; lane < 16u; ++lane)
        lanes[lane] = fadd(lo[lane], hi[lane]);
    float q0[4], q2[4];
    for (uint32_t lane = 0; lane < 4u; ++lane) {
        q0[lane] = fadd(lanes[lane], lanes[4u + lane]);
        q2[lane] = fadd(lanes[8u + lane], lanes[12u + lane]);
        q0[lane] = fadd(q0[lane], q2[lane]);
    }
    q0[0] = fadd(q0[0], q0[2]);
    q0[1] = fadd(q0[1], q0[3]);
    q0[0] = fadd(q0[0], q0[1]);
    return (double)q0[0] * scale;
}

static double historical_leaf(const asian_barrier_context_t *context,
                              enum asian_affine_barrier_product product,
                              enum asian_affine_barrier_direction direction,
                              enum asian_affine_barrier_side side)
{
    if (product == ASIAN_AFFINE_BARRIER_VANILLA) {
        if (direction == ASIAN_AFFINE_BARRIER_DOWN)
            return side == ASIAN_AFFINE_BARRIER_CALL ?
                asian_barrier_vanilla_call_interleaved_diag(context) :
                asian_barrier_vanilla_put_interleaved_diag(context);
        return side == ASIAN_AFFINE_BARRIER_CALL ?
            asian_barrier_vanilla_call_grouped_diag(context) :
            asian_barrier_vanilla_put_grouped_diag(context);
    }
    if (direction == ASIAN_AFFINE_BARRIER_DOWN)
        return side == ASIAN_AFFINE_BARRIER_CALL ?
            asian_barrier_down_call_self_interleaved_diag(context) :
            asian_barrier_down_put_self_interleaved_diag(context);
    return side == ASIAN_AFFINE_BARRIER_CALL ?
        asian_barrier_up_call_self_grouped_diag(context) :
        asian_barrier_up_put_self_grouped_diag(context);
}

static int compare_case(
    asian_affine_barrier_engine_t *engine,
    const struct asian_meta_qsort_control_plan *generic,
    asian_affine_barrier_carrier_t *carrier,
    const asian_affine_barrier_request_input_t *input)
{
    asian_affine_barrier_request_t *request = a64(sizeof(*request));
    asian_genuine_route_t *routes = a64(sizeof(*routes) * ASIAN_META_DIRECTIONS);
    asian_barrier_growth_route_t *compact =
        a64(sizeof(*compact) * ASIAN_META_DIRECTIONS);
    asian_barrier_context_t old_context;
    asian_affine_barrier_output_t output, repeated;
    int result = -1;
    if (request == NULL || routes == NULL || compact == NULL) goto done;
    generic_routes(generic, carrier, input->monitoring_count, routes);
    const double discount = exp(-input->rate * input->maturity);
    if (asian_barrier_prepare_compact(routes, input->monitoring_count,
            (float)input->s0, (float)input->barrier, (float)input->strike,
            discount, NULL, compact, &old_context) != 0 ||
        asian_affine_barrier_request_prepare(
            engine, carrier, input, request) != 0)
        goto done;
    unsigned char carrier_copy[sizeof(*carrier)];
    unsigned char request_copy[sizeof(*request)];
    memcpy(carrier_copy, carrier, sizeof(*carrier));
    memcpy(request_copy, request, sizeof(*request));
    if (asian_affine_barrier_prepared_price(request, &output) != 0 ||
        asian_affine_barrier_prepared_price(request, &repeated) != 0 ||
        memcmp(&output, &repeated, sizeof(output)) != 0 ||
        memcmp(carrier_copy, carrier, sizeof(*carrier)) != 0 ||
        memcmp(request_copy, request, sizeof(*request)) != 0)
        goto done;
    const double scalar = scalar_price(routes, input->monitoring_count,
        (float)input->s0, (float)input->barrier, (float)input->strike,
        input->product, input->direction, input->side, input->initial_state,
        discount / ASIAN_AFFINE_BARRIER_PATHS);
    if (!bit_equal_double(output.price, scalar)) goto done;
    if (input->initial_state == ASIAN_AFFINE_BARRIER_KNOWN_ALIVE) {
        const double historical = historical_leaf(&old_context, input->product,
            input->direction, input->side);
        if (!bit_equal_double(output.price, historical)) goto done;
    } else if (!bit_equal_double(output.price,
                                 input->product == ASIAN_AFFINE_BARRIER_KNOCK_OUT ?
                                 0.0 : scalar)) {
        goto done;
    }
    result = 0;
done:
    free(compact); free(routes); free(request);
    return result;
}

static int lifecycle_leaf_counts(asian_affine_barrier_engine_t *engine)
{
    const asian_affine_barrier_market_input_t market =
        market_input(&markets[2], 64u);
    asian_affine_barrier_carrier_t *carrier = a64(sizeof(*carrier));
    asian_affine_barrier_request_t *request = a64(sizeof(*request));
    asian_affine_barrier_output_t output;
    asian_affine_barrier_request_input_t input = request_input(
        &markets[2], 64u, 100.0f, 95.0f,
        ASIAN_AFFINE_BARRIER_KNOCK_OUT, ASIAN_AFFINE_BARRIER_DOWN,
        ASIAN_AFFINE_BARRIER_CALL, ASIAN_AFFINE_BARRIER_KNOWN_ALIVE);
    if (carrier == NULL || request == NULL) return -1;
    asian_affine_barrier_test_leaf_invocations = 0u;
    if (asian_affine_barrier_market_prepare(engine, &market, carrier) != 0 ||
        asian_affine_barrier_test_leaf_invocations != 0u) return -1;
    if (asian_affine_barrier_request_prepare(engine, carrier, &input, request) !=
        0 || asian_affine_barrier_test_leaf_invocations != 0u) return -1;
    if (asian_affine_barrier_prepared_price(request, &output) != 0 ||
        asian_affine_barrier_test_leaf_invocations != 1u) return -1;
    asian_affine_barrier_test_leaf_invocations = 0u;
    if (asian_affine_barrier_reuse_total(engine, carrier, &input, &output) != 0 ||
        asian_affine_barrier_test_leaf_invocations != 1u) return -1;
    asian_affine_barrier_test_leaf_invocations = 0u;
    if (asian_affine_barrier_fresh_total(engine, &input, &output) != 0 ||
        asian_affine_barrier_test_leaf_invocations != 1u) return -1;
    free(request); free(carrier);
    return 0;
}

static int edge_cases(asian_affine_barrier_engine_t *engine,
                      const struct asian_meta_qsort_control_plan *generic)
{
    asian_affine_barrier_carrier_t *carrier = a64(sizeof(*carrier));
    if (carrier == NULL) return -1;
    const asian_affine_barrier_market_input_t market =
        market_input(&markets[2], 64u);
    if (asian_affine_barrier_market_prepare(engine, &market, carrier) != 0)
        return -1;
    for (uint32_t direction = 0; direction < 2u; ++direction) {
        const float unreachable = direction == 0u ? FLT_MIN : FLT_MAX;
        const float immediate = direction == 0u ? FLT_MAX : FLT_MIN;
        for (uint32_t side = 0; side < 2u; ++side) {
            asian_affine_barrier_request_input_t vanilla = request_input(
                &markets[2], 64u, 100.0f, unreachable,
                ASIAN_AFFINE_BARRIER_VANILLA,
                (enum asian_affine_barrier_direction)direction,
                (enum asian_affine_barrier_side)side,
                ASIAN_AFFINE_BARRIER_KNOWN_ALIVE);
            asian_affine_barrier_request_input_t knockout = vanilla;
            knockout.product = ASIAN_AFFINE_BARRIER_KNOCK_OUT;
            if (compare_case(engine, generic, carrier, &vanilla) != 0 ||
                compare_case(engine, generic, carrier, &knockout) != 0)
                return -1;
            asian_affine_barrier_request_t vr, kr;
            asian_affine_barrier_output_t vo, ko;
            if (asian_affine_barrier_request_prepare(
                    engine, carrier, &vanilla, &vr) != 0 ||
                asian_affine_barrier_request_prepare(
                    engine, carrier, &knockout, &kr) != 0 ||
                asian_affine_barrier_prepared_price(&vr, &vo) != 0 ||
                asian_affine_barrier_prepared_price(&kr, &ko) != 0 ||
                !bit_equal_double(vo.price, ko.price) ||
                !bit_equal_double(
                    asian_affine_barrier_knock_in_from_parity(vo.price, ko.price),
                    0.0)) return -1;
            knockout.barrier = immediate;
            if (compare_case(engine, generic, carrier, &knockout) != 0)
                return -1;
            knockout.barrier = unreachable;
            knockout.initial_state =
                ASIAN_AFFINE_BARRIER_ALREADY_KNOCKED_OUT;
            if (compare_case(engine, generic, carrier, &knockout) != 0)
                return -1;
        }
    }
    asian_affine_barrier_request_input_t equality = request_input(
        &markets[2], 64u, 100.0f, 100.0f * carrier->growth[0],
        ASIAN_AFFINE_BARRIER_KNOCK_OUT, ASIAN_AFFINE_BARRIER_DOWN,
        ASIAN_AFFINE_BARRIER_CALL, ASIAN_AFFINE_BARRIER_KNOWN_ALIVE);
    if (compare_case(engine, generic, carrier, &equality) != 0) return -1;

    asian_genuine_route_t routes[ASIAN_META_DIRECTIONS];
    generic_routes(generic, carrier, 64u, routes);
    float final_barrier = 0.0f;
    int found = 0;
    for (uint32_t path = 0; path < ASIAN_AFFINE_BARRIER_PATHS && !found;
         ++path) {
        float states[64], s = 100.0f;
        for (uint32_t dimension = 0; dimension < 64u; ++dimension) {
            s = fmul(s, routes[dimension].growth_base[
                generic_source(routes[dimension].map, path)]);
            states[dimension] = s;
        }
        found = 1;
        for (uint32_t dimension = 0; dimension + 1u < 64u; ++dimension)
            if (!(states[dimension] > states[63])) found = 0;
        if (found) final_barrier = states[63];
    }
    if (!found) return -1;
    equality.barrier = final_barrier;
    if (compare_case(engine, generic, carrier, &equality) != 0) return -1;

    /* S0 is deliberately not a monitoring observation. */
    equality.barrier = 101.0;
    if (compare_case(engine, generic, carrier, &equality) != 0) return -1;

    /* Exact terminal payoff kink under an unreachable barrier. */
    float terminal = 100.0f;
    for (uint32_t dimension = 0; dimension < 64u; ++dimension)
        terminal = fmul(terminal, routes[dimension].growth_base[
            generic_source(routes[dimension].map, 7u)]);
    equality.strike = terminal;
    equality.barrier = FLT_MIN;
    if (compare_case(engine, generic, carrier, &equality) != 0) return -1;

    asian_affine_barrier_request_input_t parity_vanilla = equality;
    parity_vanilla.product = ASIAN_AFFINE_BARRIER_VANILLA;
    parity_vanilla.strike = 100.0;
    parity_vanilla.barrier = 95.0;
    asian_affine_barrier_request_input_t parity_out = parity_vanilla;
    parity_out.product = ASIAN_AFFINE_BARRIER_KNOCK_OUT;
    asian_affine_barrier_request_t parity_request;
    asian_affine_barrier_output_t vanilla_output, out_output;
    if (asian_affine_barrier_request_prepare(engine, carrier, &parity_vanilla,
            &parity_request) != 0 ||
        asian_affine_barrier_prepared_price(&parity_request,
            &vanilla_output) != 0 ||
        asian_affine_barrier_request_prepare(engine, carrier, &parity_out,
            &parity_request) != 0 ||
        asian_affine_barrier_prepared_price(&parity_request, &out_output) != 0)
        return -1;
    const double knock_in = asian_affine_barrier_knock_in_from_parity(
        vanilla_output.price, out_output.price);
    const double scalar_in = scalar_price(routes, 64u, 100.0f, 95.0f,
        100.0f, ASIAN_AFFINE_BARRIER_VANILLA, ASIAN_AFFINE_BARRIER_DOWN,
        ASIAN_AFFINE_BARRIER_CALL, ASIAN_AFFINE_BARRIER_KNOWN_ALIVE,
        exp(-markets[2].rate * markets[2].maturity) /
            ASIAN_AFFINE_BARRIER_PATHS) -
        scalar_price(routes, 64u, 100.0f, 95.0f, 100.0f,
        ASIAN_AFFINE_BARRIER_KNOCK_OUT, ASIAN_AFFINE_BARRIER_DOWN,
        ASIAN_AFFINE_BARRIER_CALL, ASIAN_AFFINE_BARRIER_KNOWN_ALIVE,
        exp(-markets[2].rate * markets[2].maturity) /
            ASIAN_AFFINE_BARRIER_PATHS);
    if (!bit_equal_double(knock_in, scalar_in)) return -1;

    /* Ordered comparisons must kill a NaN lane. */
    const float saved = carrier->growth[0];
    carrier->growth[0] = NAN;
    equality.strike = 100.0;
    equality.barrier = 95.0;
    if (compare_case(engine, generic, carrier, &equality) != 0) return -1;
    carrier->growth[0] = saved;
    free(carrier);
    return 0;
}

static int guard_zones(asian_affine_barrier_engine_t *engine)
{
    const size_t carrier_bytes = sizeof(asian_affine_barrier_carrier_t);
    const size_t request_bytes = sizeof(asian_affine_barrier_request_t);
    const size_t output_bytes = sizeof(asian_affine_barrier_output_t);
    unsigned char *carrier_storage = a64(carrier_bytes + 128u);
    unsigned char *request_storage = a64(request_bytes + 128u);
    unsigned char *output_storage = a64(output_bytes + 128u);
    if (carrier_storage == NULL || request_storage == NULL ||
        output_storage == NULL) return -1;
    memset(carrier_storage, 0xa5, carrier_bytes + 128u);
    memset(request_storage, 0xa5, request_bytes + 128u);
    memset(output_storage, 0xa5, output_bytes + 128u);
    asian_affine_barrier_carrier_t *carrier =
        (asian_affine_barrier_carrier_t *)(carrier_storage + 64u);
    asian_affine_barrier_request_t *request =
        (asian_affine_barrier_request_t *)(request_storage + 64u);
    asian_affine_barrier_output_t *output =
        (asian_affine_barrier_output_t *)(output_storage + 64u);
    const asian_affine_barrier_market_input_t market =
        market_input(&markets[2], 256u);
    const asian_affine_barrier_request_input_t input = request_input(
        &markets[2], 256u, 100.0f, 95.0f,
        ASIAN_AFFINE_BARRIER_KNOCK_OUT, ASIAN_AFFINE_BARRIER_DOWN,
        ASIAN_AFFINE_BARRIER_PUT, ASIAN_AFFINE_BARRIER_KNOWN_ALIVE);
    int result = asian_affine_barrier_market_prepare(engine, &market, carrier) |
        asian_affine_barrier_request_prepare(engine, carrier, &input, request) |
        asian_affine_barrier_prepared_price(request, output);
    for (size_t i = 0; i < 64u; ++i)
        if (carrier_storage[i] != 0xa5u ||
            carrier_storage[64u + carrier_bytes + i] != 0xa5u ||
            request_storage[i] != 0xa5u ||
            request_storage[64u + request_bytes + i] != 0xa5u ||
            output_storage[i] != 0xa5u ||
            output_storage[64u + output_bytes + i] != 0xa5u)
            result = -1;
    free(output_storage); free(request_storage); free(carrier_storage);
    return result == 0 ? 0 : -1;
}

int main(void)
{
    asian_affine_barrier_engine_t engine __attribute__((aligned(64)));
    asian_meta_qsort_control_plan_t *generic_opaque = NULL;
    if (asian_affine_barrier_engine_create(&engine) != 0 ||
        asian_meta_qsort_control_plan_create(&generic_opaque) != 0)
        return 2;
    const struct asian_meta_qsort_control_plan *generic =
        (const struct asian_meta_qsort_control_plan *)generic_opaque;
    if (generic->magic != GENERIC_PLAN_MAGIC ||
        route_integrity(engine.affine_plan, generic) != 0) {
        fprintf(stderr, "route integrity failed\n"); return 2;
    }
    asian_affine_barrier_carrier_t *carrier = a64(sizeof(*carrier));
    if (carrier == NULL) return 2;

    for (uint32_t n = 2u; n <= 256u; ++n) {
        const market_t *market = &markets[n & 3u];
        const asian_affine_barrier_market_input_t mi = market_input(market, n);
        if (asian_affine_barrier_market_prepare(&engine, &mi, carrier) != 0)
            return 2;
        const enum asian_affine_barrier_direction direction =
            (n & 1u) ? ASIAN_AFFINE_BARRIER_UP : ASIAN_AFFINE_BARRIER_DOWN;
        const enum asian_affine_barrier_side side =
            (n & 2u) ? ASIAN_AFFINE_BARRIER_PUT : ASIAN_AFFINE_BARRIER_CALL;
        const float strikes[3] = {80.0f, 100.0f, 120.0f};
        const float barrier = direction == ASIAN_AFFINE_BARRIER_DOWN ?
            95.0f : 110.0f;
        const asian_affine_barrier_request_input_t input = request_input(
            market, n, strikes[n % 3u], barrier,
            ASIAN_AFFINE_BARRIER_KNOCK_OUT, direction, side,
            ASIAN_AFFINE_BARRIER_KNOWN_ALIVE);
        if (compare_case(&engine, generic, carrier, &input) != 0) {
            fprintf(stderr, "N sweep failed N=%u\n", n); return 2;
        }
    }

    static const uint32_t principal_n[] = {2u, 64u, 256u};
    for (uint32_t ni = 0; ni < 3u; ++ni)
        for (uint32_t mi = 0; mi < 4u; ++mi) {
            const uint32_t n = principal_n[ni];
            const market_t *market = &markets[mi];
            const asian_affine_barrier_market_input_t in = market_input(market,n);
            if (asian_affine_barrier_market_prepare(&engine, &in, carrier) != 0)
                return 2;
            for (uint32_t direction = 0; direction < 2u; ++direction)
                for (uint32_t side = 0; side < 2u; ++side)
                    for (uint32_t strike = 80u; strike <= 120u; strike += 20u) {
                        const asian_affine_barrier_request_input_t request =
                            request_input(market, n, (float)strike,
                                direction == 0u ? 95.0f : 110.0f,
                                ASIAN_AFFINE_BARRIER_KNOCK_OUT,
                                (enum asian_affine_barrier_direction)direction,
                                (enum asian_affine_barrier_side)side,
                                ASIAN_AFFINE_BARRIER_KNOWN_ALIVE);
                        if (compare_case(&engine, generic, carrier, &request) != 0)
                            return 2;
                    }
        }
    if (edge_cases(&engine, generic) != 0 || guard_zones(&engine) != 0 ||
        lifecycle_leaf_counts(&engine) != 0) return 2;

    free(carrier);
    asian_meta_qsort_control_plan_destroy(generic_opaque);
    asian_affine_barrier_engine_destroy(&engine);
    puts("affine_discrete_barrier_correctness PASS "
         "direct_d1=YES affine_d2_dn=YES paths=4096 N=2..256 "
         "historical_identity=PASS scalar_binary32=PASS parity=PASS "
         "leaf_invocations=1");
    return 0;
}
