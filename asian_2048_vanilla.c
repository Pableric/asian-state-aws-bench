#define _POSIX_C_SOURCE 200112L

#include "private/asian_2048_vanilla_diag.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    ASIAN_2048_ENGINE_MAGIC = 0x45324841u,
    ASIAN_2048_GENERIC_MAGIC = 0x47324841u,
    JOE_KUO_RECORD_BYTES = 33 * 4,
    ASIAN_2048_CARRIER_MAGIC = 0x43324841u,
};

extern const unsigned char asian_arithmetic_joe_kuo_256_records[];

typedef struct {
    uint32_t word;
    uint16_t index;
} word_index_t;

struct asian_2048_generic_plan {
    uint32_t magic;
    uint8_t donor_view[ASIAN_META_DIRECTIONS];
    uint8_t reserved[60];
    fragment_map_t maps[ASIAN_META_DIRECTIONS];
};

static int pair_compare(const void *left, const void *right)
{
    const uint32_t a = ((const word_index_t *)left)->word;
    const uint32_t b = ((const word_index_t *)right)->word;
    return (a > b) - (a < b);
}

static int same_double(double a, double b)
{
    uint64_t x, y;
    memcpy(&x, &a, sizeof(x));
    memcpy(&y, &b, sizeof(y));
    return x == y;
}

static uint32_t sobol_word(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray = index ^ (index >> 1);
    uint32_t word = 0u;
    for (uint32_t bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u)
            word ^= directions[bit];
    return word;
}

static int load_row(uint32_t dimension, uint32_t directions[32])
{
    if (dimension >= ASIAN_META_DIRECTIONS)
        return -1;
    const unsigned char *record = asian_arithmetic_joe_kuo_256_records +
        (size_t)dimension * JOE_KUO_RECORD_BYTES;
    uint32_t count;
    memcpy(&count, record, sizeof(count));
    if (count != 32u)
        return -1;
    memcpy(directions, record + sizeof(count), 32u * sizeof(*directions));
    return 0;
}

uint8_t asian_2048_descriptor_donor_view(uint32_t dimension)
{
    if (dimension >= ASIAN_META_DIRECTIONS)
        return UINT8_MAX;
    const asian_meta_direction_descriptor_t *descriptor =
        &asian_meta_direction_descriptors[dimension];
    return (uint8_t)(descriptor->donor_region * 2u +
                     (descriptor->base >> 11));
}

static int context_build(const asian_meta_direction_descriptor_t *descriptor,
                         asian_2048_dim_context_t *out)
{
    if (descriptor == NULL || out == NULL ||
        descriptor->magic != ASIAN_META_DESCRIPTOR_MAGIC ||
        descriptor->abi_version != ASIAN_META_DESCRIPTOR_ABI_VERSION ||
        descriptor->donor_region > 1u || descriptor->base >= 4096u ||
        ((uintptr_t)out & 63u) != 0u)
        return -1;
    for (uint32_t bit = 0; bit < 11u; ++bit)
        if (descriptor->column[bit] >= 2048u)
            return -1;

    memset(out, 0, sizeof(*out));
    for (uint32_t lane = 0; lane < 16u; ++lane) {
        uint32_t control = 0u;
        for (uint32_t bit = 0; bit < 4u; ++bit)
            if ((lane & (1u << bit)) != 0u)
                control ^= descriptor->column[bit];
        out->base_control[lane] = control;
        out->half_delta[lane] = descriptor->column[4] & 15u;
    }
    out->delta = descriptor->column[4] & 15u;
    out->reserved0 = (uint32_t)(descriptor->donor_region * 2u +
                               (descriptor->base >> 11));

    uint16_t jump[6];
    uint16_t cumulative = 0u;
    for (uint32_t bit = 0; bit < 6u; ++bit) {
        cumulative ^= descriptor->column[5u + bit];
        jump[bit] = cumulative;
    }
    uint16_t state = descriptor->base & 2047u;
    for (uint32_t packet = 0; packet < ASIAN_2048_PACKETS; ++packet) {
        out->sel2[packet][0] = (uint8_t)(state >> 4);
        out->sel2[packet][1] = (uint8_t)(state & 15u);
        if (packet + 1u < ASIAN_2048_PACKETS)
            state ^= jump[__builtin_ctz(packet + 1u)];
    }
    return 0;
}

int asian_2048_engine_create(asian_2048_engine_t *engine)
{
    if (engine == NULL || ((uintptr_t)engine & 63u) != 0u)
        return -1;
    memset(engine, 0, sizeof(*engine));
    if (asian_affine_family_engine_create(&engine->parent) != 0)
        return -1;
    if (posix_memalign((void **)&engine->plan, 64u,
                       sizeof(*engine->plan)) != 0) {
        asian_affine_family_engine_destroy(&engine->parent);
        return -1;
    }
    memset(engine->plan, 0, sizeof(*engine->plan));
    for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
         ++dimension) {
        if (context_build(&asian_meta_direction_descriptors[dimension],
                          &engine->plan->contexts[dimension]) != 0) {
            free(engine->plan);
            engine->plan = NULL;
            asian_affine_family_engine_destroy(&engine->parent);
            return -1;
        }
        const uint32_t view =
            engine->plan->contexts[dimension].reserved0;
        if (view >= ASIAN_2048_DONOR_VIEWS) {
            free(engine->plan);
            engine->plan = NULL;
            asian_affine_family_engine_destroy(&engine->parent);
            return -1;
        }
        ++engine->plan->donor_view_counts[view];
    }
    static const uint32_t expected[4] = {64u, 66u, 59u, 67u};
    if (memcmp(engine->plan->donor_view_counts, expected,
               sizeof(expected)) != 0) {
        free(engine->plan);
        engine->plan = NULL;
        asian_affine_family_engine_destroy(&engine->parent);
        return -1;
    }
    engine->plan->abi_version = 1u;
    engine->plan->dimension_count = ASIAN_META_DIRECTIONS;
    engine->plan->descriptor_bytes = ASIAN_META_DESCRIPTOR_SET_BYTES;
    engine->plan->packet_steps = ASIAN_2048_PACKETS;
    engine->plan->selector_bytes = ASIAN_2048_SELECTOR_BYTES;
    engine->plan->magic = ASIAN_2048_PLAN_MAGIC;
    engine->magic = ASIAN_2048_ENGINE_MAGIC;
    return 0;
}

void asian_2048_engine_destroy(asian_2048_engine_t *engine)
{
    if (engine != NULL) {
        if (engine->plan != NULL) {
            engine->plan->magic = 0u;
            free(engine->plan);
        }
        asian_affine_family_engine_destroy(&engine->parent);
        memset(engine, 0, sizeof(*engine));
    }
}

static int build_generic_map(const uint16_t source[ASIAN_2048_PATHS],
                             fragment_map_t *map)
{
    memset(map, 0, sizeof(*map));
    uint32_t pattern_count = 0u;
    for (uint32_t packet = 0; packet < ASIAN_2048_PACKETS; ++packet) {
        for (uint32_t half = 0; half < 2u; ++half) {
            const uint32_t base = packet * 32u + half * 16u;
            const uint32_t line = source[base] / 16u;
            uint32_t controls[16];
            for (uint32_t lane = 0; lane < 16u; ++lane) {
                if (source[base + lane] / 16u != line)
                    return -1;
                controls[lane] = source[base + lane] & 15u;
            }
            uint32_t pattern;
            for (pattern = 0; pattern < pattern_count; ++pattern)
                if (memcmp(map->patterns[pattern], controls,
                           sizeof(controls)) == 0)
                    break;
            if (pattern == pattern_count) {
                if (pattern_count == FRAG_MAX_PATTERNS)
                    return -1;
                memcpy(map->patterns[pattern_count++], controls,
                       sizeof(controls));
            }
            map->select[packet][half] = (uint8_t)line;
            map->select[packet][2u + half] = (uint8_t)pattern;
        }
    }
    map->pattern_count = pattern_count;
    return 0;
}

int asian_2048_generic_plan_create(asian_2048_generic_plan_t **out)
{
    if (out == NULL)
        return -1;
    *out = NULL;
    asian_2048_generic_plan_t *plan = NULL;
    if (posix_memalign((void **)&plan, 64u, sizeof(*plan)) != 0)
        return -1;
    memset(plan, 0, sizeof(*plan));

    word_index_t *donor = calloc(ASIAN_2048_DONOR_VIEWS * ASIAN_2048_PATHS,
                                 sizeof(*donor));
    uint16_t *indices = malloc(ASIAN_2048_PATHS * sizeof(*indices));
    if (donor == NULL || indices == NULL) {
        free(indices); free(donor); free(plan); return -1;
    }
    uint32_t directions[32];
    if (load_row(0u, directions) != 0) {
        free(indices); free(donor); free(plan); return -1;
    }
    for (uint32_t view = 0; view < ASIAN_2048_DONOR_VIEWS; ++view) {
        word_index_t *table = donor + view * ASIAN_2048_PATHS;
        for (uint32_t path = 0; path < ASIAN_2048_PATHS; ++path) {
            table[path].word = sobol_word(8192u + view * ASIAN_2048_PATHS +
                                          path, directions);
            table[path].index = (uint16_t)path;
        }
        qsort(table, ASIAN_2048_PATHS, sizeof(*table), pair_compare);
        for (uint32_t path = 1; path < ASIAN_2048_PATHS; ++path)
            if (table[path - 1u].word == table[path].word) {
                free(indices); free(donor); free(plan); return -1;
            }
    }

    for (uint32_t dimension = 0; dimension < ASIAN_META_DIRECTIONS;
         ++dimension) {
        if (load_row(dimension, directions) != 0) {
            free(indices); free(donor); free(plan); return -1;
        }
        uint32_t matches = 0u, selected = 0u;
        for (uint32_t view = 0; view < ASIAN_2048_DONOR_VIEWS; ++view) {
            word_index_t *table = donor + view * ASIAN_2048_PATHS;
            uint8_t seen[ASIAN_2048_PATHS] = {0};
            int valid = 1;
            for (uint32_t path = 0; path < ASIAN_2048_PATHS; ++path) {
                const word_index_t key = {
                    sobol_word(8192u + path, directions), 0u
                };
                word_index_t *found = bsearch(&key, table, ASIAN_2048_PATHS,
                                               sizeof(*table), pair_compare);
                if (found == NULL || seen[found->index]) {
                    valid = 0;
                    break;
                }
                seen[found->index] = 1u;
                indices[path] = found->index;
            }
            if (valid) {
                ++matches;
                selected = view;
                if (build_generic_map(indices, &plan->maps[dimension]) != 0) {
                    free(indices); free(donor); free(plan); return -1;
                }
            }
        }
        if (matches != 1u) {
            free(indices); free(donor); free(plan); return -1;
        }
        plan->donor_view[dimension] = (uint8_t)selected;
        plan->maps[dimension].dimension = dimension + 1u;
    }
    free(indices);
    free(donor);
    plan->magic = ASIAN_2048_GENERIC_MAGIC;
    *out = plan;
    return 0;
}

void asian_2048_generic_plan_destroy(asian_2048_generic_plan_t *plan)
{
    if (plan != NULL) {
        plan->magic = 0u;
        free(plan);
    }
}

void asian_2048_carrier_initialize(asian_2048_carrier_t *carrier)
{
    if (carrier != NULL) {
        memset(carrier, 0, sizeof(*carrier));
        memset(carrier->guard_before, 0xa5, sizeof(carrier->guard_before));
        memset(carrier->guard_after, 0x5a, sizeof(carrier->guard_after));
    }
}

int asian_2048_carrier_prepare(const asian_2048_engine_t *engine,
    const asian_affine_family_carrier_input_t *input,
    asian_2048_carrier_t *carrier)
{
    if (engine == NULL || engine->magic != ASIAN_2048_ENGINE_MAGIC ||
        input == NULL || carrier == NULL ||
        !isfinite(input->rate) || !isfinite(input->dividend_yield) ||
        !isfinite(input->sigma) || input->sigma <= 0.0 ||
        !isfinite(input->maturity) || input->maturity <= 0.0 ||
        input->future_fixings < 2u || input->future_fixings > 256u)
        return -1;
    const double dt = input->maturity / (double)input->future_fixings;
    const float drift = (float)((input->rate - input->dividend_yield -
        0.5 * input->sigma * input->sigma) * dt);
    const float diffusion = (float)(input->sigma * sqrt(dt));
    const float lo = fmaf(diffusion, engine->parent.signed_z_min, drift);
    const float hi = fmaf(diffusion, engine->parent.signed_z_max, drift);
    if (!isfinite(drift) || !isfinite(diffusion) || diffusion <= 0.0f ||
        !isfinite(lo) || !isfinite(hi) || lo < -87.0f || hi > 88.0f)
        return -1;
    memset(&carrier->fused, 0, sizeof(carrier->fused));
    carrier->fused.signed_z = asian_genuine_fixed_block_signed_z;
    carrier->fused.growth_out = carrier->growth;
    carrier->fused.drift = drift;
    carrier->fused.diffusion = diffusion;
    carrier->fused.fixing_count = input->future_fixings;
    carrier->fused.path_count = 4096u;
    carrier->fused.region_count = 2u;
    carrier->fused.values_per_region = 4096u;
    carrier->fused.first_index = 8192u;
    carrier->fused.total_values = ASIAN_2048_DONOR_VALUES;
    carrier->fused.magic = ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC;
    carrier->fused.abi_version =
        ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION;
    carrier->market = *input;
    carrier->magic = ASIAN_2048_CARRIER_MAGIC;
    asian_genuine_arithmetic_fused_source_exp_diag(&carrier->fused);
    return 0;
}

static int request_valid(const asian_2048_carrier_t *carrier,
    const asian_affine_family_request_input_t *input)
{
    return carrier != NULL && input != NULL &&
        carrier->magic == ASIAN_2048_CARRIER_MAGIC &&
        input->future_fixings >= 2u && input->future_fixings <= 256u &&
        input->completed_fixings == 0u &&
        input->initial_arithmetic_sum == 0.0 && input->past_log_sum == 0.0 &&
        input->strike_count == 1u && input->strikes != NULL &&
        input->workload == ASIAN_AFFINE_FAMILY_PRICE &&
        isfinite(input->s0) && input->s0 > 0.0 &&
        isfinite(input->strikes[0]) && input->strikes[0] > 0.0f &&
        same_double(input->rate, carrier->market.rate) &&
        same_double(input->dividend_yield, carrier->market.dividend_yield) &&
        same_double(input->sigma, carrier->market.sigma) &&
        same_double(input->maturity, carrier->market.maturity) &&
        input->future_fixings == carrier->market.future_fixings;
}

static void prepare_payoff(const asian_affine_family_request_input_t *input,
    asian_2048_payoff_context_t *payoff,
    asian_genuine_strip_strike_t *strike);

static int finish_request(const asian_2048_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_2048_request_t *request)
{
    const uint32_t n = input->future_fixings;
    request->growth.d1_growth = request->routes[0].growth_base;
    request->growth.routes_d2 = request->routes + 1;
    request->growth.unused_q = NULL;
    request->growth.fixing_count = n;
    request->growth.s0 = (float)input->s0;
    request->growth.magic = ASIAN_2048_REQUEST_MAGIC;
    prepare_payoff(input, &request->payoff, &request->strike);
    request->magic = ASIAN_2048_REQUEST_MAGIC;
    (void)carrier;
    return 0;
}

static void prepare_payoff(const asian_affine_family_request_input_t *input,
    asian_2048_payoff_context_t *payoff,
    asian_genuine_strip_strike_t *strike)
{
    const uint32_t n = input->future_fixings;
    const double dt = input->maturity / (double)n;
    const double carry = input->rate - input->dividend_yield;
    double expected_sum = 0.0;
    for (uint32_t fixing = 1; fixing <= n; ++fixing)
        expected_sum += input->s0 * exp(carry * dt * (double)fixing);
    const double expected = expected_sum / (double)n;
    const double discount = exp(-input->rate * input->maturity);
    const double parity = discount * (expected - input->strikes[0]);
    const int direct_call = input->strikes[0] >= expected;
    payoff->inv_total = (float)(1.0 / (double)n);
    payoff->initial_q = 0.0f;
    payoff->discount = (float)discount;
    strike->strike = input->strikes[0];
    strike->direct_sign = direct_call ? 1.0f : -1.0f;
    strike->call_price_adjust = direct_call ? 0.0 : parity;
    strike->put_price_adjust = direct_call ? -parity : 0.0;
}

int asian_2048_request_prepare(const asian_2048_engine_t *engine,
    const asian_2048_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_2048_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_2048_ENGINE_MAGIC ||
        engine->plan == NULL || engine->plan->magic != ASIAN_2048_PLAN_MAGIC ||
        request == NULL || ((uintptr_t)request & 63u) != 0u ||
        !request_valid(carrier, input))
        return -1;
    const uint32_t n = input->future_fixings;
    for (uint32_t fixing = 0; fixing < n; ++fixing) {
        const asian_2048_dim_context_t *map =
            &engine->plan->contexts[fixing];
        const uint32_t view = map->reserved0;
        request->routes[fixing].x_base = NULL;
        request->routes[fixing].growth_base =
            carrier->growth + (size_t)view * ASIAN_2048_PATHS;
        request->routes[fixing].map = map;
        const float weight = (float)(n - fixing) / (float)n;
        memcpy(&request->routes[fixing].weight_bits, &weight, sizeof(weight));
        request->routes[fixing].fixing_index = fixing;
    }
    return finish_request(carrier, input, request);
}

int asian_2048_generic_request_prepare(const asian_2048_generic_plan_t *plan,
    const asian_2048_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_2048_request_t *request)
{
    if (plan == NULL || plan->magic != ASIAN_2048_GENERIC_MAGIC ||
        request == NULL || ((uintptr_t)request & 63u) != 0u ||
        !request_valid(carrier, input))
        return -1;
    const uint32_t n = input->future_fixings;
    for (uint32_t fixing = 0; fixing < n; ++fixing) {
        const uint32_t view = plan->donor_view[fixing];
        request->routes[fixing].x_base = NULL;
        request->routes[fixing].growth_base =
            carrier->growth + (size_t)view * ASIAN_2048_PATHS;
        request->routes[fixing].map =
            (const asian_2048_dim_context_t *)&plan->maps[fixing];
        request->routes[fixing].fixing_index = fixing;
    }
    return finish_request(carrier, input, request);
}

int asian_4096_vanilla_request_prepare(const asian_2048_engine_t *engine,
    const asian_2048_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_4096_vanilla_request_t *request)
{
    if (engine == NULL || engine->magic != ASIAN_2048_ENGINE_MAGIC ||
        engine->parent.affine_plan == NULL || request == NULL ||
        ((uintptr_t)request & 63u) != 0u || !request_valid(carrier, input) ||
        asian_meta_affine_routes_bind(engine->parent.affine_plan, NULL,
            carrier->growth, input->future_fixings, request->routes) != 0)
        return -1;
    request->growth.d1_growth = request->routes[0].growth_base;
    request->growth.routes_d2 = request->routes + 1;
    request->growth.q_out = NULL;
    request->growth.fixing_count = input->future_fixings;
    request->growth.s0 = (float)input->s0;
    request->growth.magic = ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC;
    request->growth.abi_version =
        ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION;
    prepare_payoff(input, &request->payoff, &request->strike);
    request->magic = ASIAN_2048_REQUEST_MAGIC ^ UINT32_C(1);
    return 0;
}

__attribute__((noinline, used))
int asian_4096_vanilla_prepared_price(
    const asian_4096_vanilla_request_t *request, asian_2048_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != (ASIAN_2048_REQUEST_MAGIC ^ UINT32_C(1)))
        return -1;
    asian_meta_arithmetic_growth_only_price_1_diag(
        &request->growth,
        (const asian_genuine_strip_context_t *)&request->payoff,
        &request->strike, (asian_genuine_strip_value_t *)output);
    return 0;
}

__attribute__((noinline, used))
int asian_2048_prepared_price(asian_2048_request_t *request,
                              asian_2048_output_t *output)
{
    if (request == NULL || output == NULL ||
        request->magic != ASIAN_2048_REQUEST_MAGIC)
        return -1;
    asian_2048_affine_price_1_diag(&request->growth, &request->payoff,
                                   &request->strike, output);
    return 0;
}

int asian_2048_structural_check(void)
{
    asian_2048_engine_t engine __attribute__((aligned(64)));
    asian_2048_generic_plan_t *generic = NULL;
    if (asian_2048_engine_create(&engine) != 0 ||
        asian_2048_generic_plan_create(&generic) != 0) {
        asian_2048_engine_destroy(&engine);
        return -1;
    }
    const uint32_t expected[4] = {64u, 66u, 59u, 67u};
    int status = memcmp(engine.plan->donor_view_counts, expected,
                        sizeof(expected)) == 0 ? 0 : -1;
    uint32_t d1[32], directions[32];
    if (status == 0 && load_row(0u, d1) != 0)
        status = -1;
    for (uint32_t dimension = 0;
         status == 0 && dimension < ASIAN_META_DIRECTIONS; ++dimension) {
        if (load_row(dimension, directions) != 0) {
            status = -1;
            break;
        }
        word_index_t words[ASIAN_2048_PATHS];
        const asian_meta_direction_descriptor_t *descriptor =
            &asian_meta_direction_descriptors[dimension];
        const uint32_t view = asian_2048_descriptor_donor_view(dimension);
        if (view >= ASIAN_2048_DONOR_VIEWS ||
            generic->donor_view[dimension] != view ||
            engine.plan->contexts[dimension].reserved0 != view) {
            status = -1;
            break;
        }
        for (uint32_t path = 0; path < ASIAN_2048_PATHS; ++path) {
            const uint32_t target = sobol_word(8192u + path, directions);
            if (target == 0u || target == UINT32_MAX) {
                status = -1;
                break;
            }
            words[path].word = target;
            words[path].index = (uint16_t)path;

            uint32_t affine_index = descriptor->base & 2047u;
            for (uint32_t bit = 0; bit < 11u; ++bit)
                if ((path & (1u << bit)) != 0u)
                    affine_index ^= descriptor->column[bit];
            const uint32_t packet = path / 32u;
            const uint32_t half = (path & 31u) / 16u;
            const uint32_t lane = path & 15u;
            const fragment_map_t *map = &generic->maps[dimension];
            const uint32_t pattern = map->select[packet][2u + half];
            const uint32_t generic_index =
                (uint32_t)map->select[packet][half] * 16u +
                map->patterns[pattern][lane];
            const uint32_t absolute = 8192u + view * ASIAN_2048_PATHS;
            if (affine_index >= ASIAN_2048_PATHS ||
                generic_index != affine_index ||
                sobol_word(absolute + affine_index, d1) != target) {
                status = -1;
                break;
            }
        }
        qsort(words, ASIAN_2048_PATHS, sizeof(words[0]), pair_compare);
        for (uint32_t path = 1; status == 0 && path < ASIAN_2048_PATHS;
             ++path)
            if (words[path - 1u].word == words[path].word)
                status = -1;
    }
    asian_2048_generic_plan_destroy(generic);
    asian_2048_engine_destroy(&engine);
    return status;
}
