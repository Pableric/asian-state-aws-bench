#define main asian_meta_parent_growth_only_test_main
#include "test_asian_genuine_arithmetic_growth_only.c"
#undef main

#include "private/asian_meta_direction_affine_route.h"

static uint32_t meta_affine_source(
    const asian_meta_dim_affine_ctx_t *context, uint32_t path)
{
    const uint32_t packet = path >> 5;
    const uint32_t lane = path & 15u;
    const uint32_t half = (path >> 4) & 1u;
    uint32_t line = context->sel2[packet][0];
    uint32_t control = context->sel2[packet][1] ^
        context->base_control[lane];
    if (half != 0u) {
        line ^= 1u;
        control ^= context->half_delta[lane];
    }
    return line * 16u + control;
}

static int meta_descriptor_and_provider_gate(fixture_t *fixture,
                                              asian_meta_affine_plan_t *plan)
{
    uint32_t donor_counts[2] = {0u, 0u};
    const uint32_t full_expected[12] =
        {2048u,1024u,512u,256u,128u,64u,32u,16u,8u,4u,2u,1u};
    float *growth_out = a64(PATHS * sizeof(*growth_out));
    float *x_out = a64(PATHS * sizeof(*x_out));
    if (growth_out == NULL || x_out == NULL)
        return -1;

    for (uint32_t i = 0; i < 2u * PATHS; ++i) {
        const uint32_t growth_bits = UINT32_C(0x3f000000) ^
            (i * UINT32_C(0x00010101));
        const uint32_t x_bits = UINT32_C(0x41000000) ^
            (i * UINT32_C(0x00008081));
        memcpy(&fixture->growth[i], &growth_bits, sizeof(growth_bits));
        memcpy(&fixture->x[i], &x_bits, sizeof(x_bits));
    }

    for (uint32_t dimension = 0; dimension < MAX_N; ++dimension) {
        const asian_meta_direction_descriptor_t *descriptor =
            &asian_meta_direction_descriptors[dimension];
        const uint16_t *oracle = fixture->oracle_source +
            (size_t)dimension * PATHS;
        const uint32_t donor = oracle[0] >> 12;
        uint8_t seen[PATHS] = {0};
        uint32_t counts[12] = {0};
        uint16_t full_jump[12];
        uint16_t cumulative = 0u;

        if (descriptor->base != (oracle[0] & 4095u) ||
            descriptor->donor_region != donor ||
            descriptor->magic != ASIAN_META_DESCRIPTOR_MAGIC ||
            descriptor->abi_version != ASIAN_META_DESCRIPTOR_ABI_VERSION ||
            plan->donor_region[dimension] != donor)
            return -1;
        ++donor_counts[donor];

        for (uint32_t bit = 0; bit < 12u; ++bit) {
            const uint16_t regenerated =
                (oracle[1u << bit] & 4095u) ^ descriptor->base;
            if (descriptor->column[bit] != regenerated)
                return -1;
            cumulative ^= descriptor->column[bit];
            full_jump[bit] = cumulative;
        }

        uint16_t state = descriptor->base;
        for (uint32_t path = 0; path < PATHS; ++path) {
            if ((oracle[path] >> 12) != donor ||
                state != (oracle[path] & 4095u) || seen[state] != 0u ||
                meta_affine_source(&plan->contexts[dimension], path) != state)
                return -1;
            seen[state] = 1u;
            if (path + 1u < PATHS) {
                const uint32_t jump = __builtin_ctz(path + 1u);
                ++counts[jump];
                state ^= full_jump[jump];
            }
        }
        if (memcmp(counts, full_expected, sizeof(counts)) != 0)
            return -1;

        const float *growth_donor = fixture->growth + (size_t)donor * PATHS;
        const float *x_donor = fixture->x + (size_t)donor * PATHS;
        asian_meta_affine_dual_provider_diag(growth_donor, x_donor,
            &plan->contexts[dimension], growth_out, x_out);
        for (uint32_t path = 0; path < PATHS; ++path) {
            const uint32_t source = oracle[path] & 4095u;
            if (memcmp(&growth_out[path], &growth_donor[source], 4u) != 0 ||
                memcmp(&x_out[path], &x_donor[source], 4u) != 0)
                return -1;
        }
    }
    free(x_out);
    free(growth_out);
    return donor_counts[0] != 0u && donor_counts[1] != 0u ? 0 : -1;
}

static void meta_call_immediate(
    uint32_t count, int delta, const void *growth,
    const asian_genuine_strip_context_t *strip,
    asian_genuine_strip_value_t *output, int meta)
{
    if (!meta) {
        if (count == 1u && !delta)
            asian_genuine_arithmetic_growth_only_price_1_diag(growth, strip,
                strip->strikes, output);
        else if (count == 1u)
            asian_genuine_arithmetic_growth_only_price_delta_1_diag(growth,
                strip, strip->strikes, output);
        else if (count == 2u && !delta)
            asian_genuine_arithmetic_growth_only_price_2_diag(growth, strip,
                strip->strikes, output);
        else if (count == 2u)
            asian_genuine_arithmetic_growth_only_price_delta_2_diag(growth,
                strip, strip->strikes, output);
        else if (!delta)
            asian_genuine_arithmetic_growth_only_price_4_diag(growth, strip,
                strip->strikes, output);
        else
            asian_genuine_arithmetic_growth_only_price_delta_4_diag(growth,
                strip, strip->strikes, output);
    } else {
        if (count == 1u && !delta)
            asian_meta_arithmetic_growth_only_price_1_diag(growth, strip,
                strip->strikes, output);
        else if (count == 1u)
            asian_meta_arithmetic_growth_only_price_delta_1_diag(growth, strip,
                strip->strikes, output);
        else if (count == 2u && !delta)
            asian_meta_arithmetic_growth_only_price_2_diag(growth, strip,
                strip->strikes, output);
        else if (count == 2u)
            asian_meta_arithmetic_growth_only_price_delta_2_diag(growth, strip,
                strip->strikes, output);
        else if (!delta)
            asian_meta_arithmetic_growth_only_price_4_diag(growth, strip,
                strip->strikes, output);
        else
            asian_meta_arithmetic_growth_only_price_delta_4_diag(growth, strip,
                strip->strikes, output);
    }
}

static int meta_candidate_case(fixture_t *fixture,
                               const asian_meta_affine_plan_t *plan,
                               const market_t *market, uint32_t n,
                               int immediate_gate)
{
    asian_meta_affine_route_t *routes =
        a64(MAX_N * sizeof(*routes));
    asian_genuine_state_t *state = a64(sizeof(*state));
    float *q = a64(PATHS * sizeof(*q));
    if (routes == NULL || state == NULL || q == NULL ||
        asian_meta_affine_routes_bind(plan, fixture->x, fixture->growth,
            n, routes) != 0)
        return -1;

    /* The frozen generic S/Q/L oracle prepared its diagnostic weights at
       MAX_N; mirror those bits exactly for this trace-only comparison. */
    for (uint32_t fixing = 0; fixing < n; ++fixing)
        routes[fixing].weight_bits = fixture->routes[fixing].weight_bits;

    for (uint32_t path = 0; path < PATHS; ++path)
        state->s[path] = 100.0f;
    asian_meta_affine_sql_dual_control_diag(routes, n, state);
    if (memcmp(state, fixture->dual, sizeof(*state)) != 0)
        return -1;

    asian_meta_growth_only_context_t context __attribute__((aligned(64)));
    memset(&context, 0, sizeof(context));
    context.d1_growth = fixture->growth;
    context.routes_d2 = routes + 1;
    context.q_out = q;
    context.fixing_count = n;
    context.s0 = 100.0f;
    context.magic = ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC;
    context.abi_version = ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION;
    asian_meta_arithmetic_growth_only_q_diag(&context);
    if (memcmp(q, fixture->q, PATHS * sizeof(*q)) != 0)
        return -1;

    if (immediate_gate) {
        float strikes[4] = {73.25f, 96.5f, 100.0f, 131.75f};
        for (uint32_t count = 1; count <= 4u; ++count) {
            asian_genuine_strip_context_t *strip = a64(sizeof(*strip));
            asian_genuine_strip_value_t generic[4] = {{0}};
            asian_genuine_strip_value_t affine[4] = {{0}};
            uint32_t padded = 0u;
            if (strip == NULL ||
                asian_genuine_arithmetic_growth_only_strip_prepare_padded(
                    strip, 100.0, market->rate, market->dividend,
                    market->sigma, market->maturity, n, 0u, 0.0, 0.0,
                    strikes, count, &padded) != 0)
                return -1;
            const uint32_t tile = count == 1u ? 1u : count == 2u ? 2u : 4u;
            for (int delta = 0; delta <= 1; ++delta) {
                memset(generic, 0, sizeof(generic));
                memset(affine, 0, sizeof(affine));
                meta_call_immediate(tile, delta, fixture->context, strip,
                                    generic, 0);
                meta_call_immediate(tile, delta, &context, strip, affine, 1);
                if (memcmp(generic, affine, sizeof(generic)) != 0)
                    return -1;
            }
            free(strip);
        }
    }
    free(q);
    free(state);
    free(routes);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 1) {
        fprintf(stderr, "usage: %s\n", argv[0]);
        return 2;
    }
    fixture_t fixture;
    asian_meta_affine_plan_t *plan = NULL;
    if (fixture_init(&fixture) != 0 || asian_meta_affine_plan_create(&plan) != 0 ||
        meta_descriptor_and_provider_gate(&fixture, plan) != 0) {
        fprintf(stderr, "meta descriptor/provider gate failed\n");
        return 1;
    }
    if (negative_tests(&fixture) != 0 || fused_preparation_tests(&fixture) != 0 ||
        fused_growth_matrix(&fixture) != 0) {
        fprintf(stderr, "parent preparation gate failed\n");
        return 1;
    }
    for (uint32_t n = 2u; n <= 256u; ++n) {
        const uint32_t market = n % (sizeof(markets) / sizeof(markets[0]));
        const int immediate = n == 16u || n == 32u || n == 64u ||
                              n == 128u || n == 256u;
        if (run_case(&fixture, n, market, n == 2u || n == 256u) != 0 ||
            meta_candidate_case(&fixture, plan, &markets[market], n,
                                immediate) != 0) {
            fprintf(stderr, "meta candidate failure market=%u N=%u\n", market, n);
            return 1;
        }
    }
    for (uint32_t market = 0; market < sizeof(markets) / sizeof(markets[0]);
         ++market) {
        if (run_case(&fixture, 16u, market, market == 0u) != 0 ||
            meta_candidate_case(&fixture, plan, &markets[market], 16u, 1) != 0) {
            fprintf(stderr, "meta boundary failure market=%u N=16\n", market);
            return 1;
        }
    }
    if (consumer_only_one_fixing(&fixture) != 0) {
        fprintf(stderr, "one-fixing consumer gate failed\n");
        return 1;
    }
    asian_meta_affine_plan_destroy(plan);
    fixture_release(&fixture);
    puts("meta_direction_affine_route PASS");
    return 0;
}
