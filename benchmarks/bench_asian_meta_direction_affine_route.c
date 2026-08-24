#define main asian_meta_parent_lifecycle_main
#include "bench_asian_n64_lifecycle.c"
#undef main

#include "private/asian_meta_direction_affine_route.h"
#include "private/asian_arithmetic_pricer_test.h"

enum { META_GRID = 5, META_CELLS_PER_N = 20, BUILDER_SAMPLES = 5 };

static asian_meta_affine_plan_t *meta_plan;
static unsigned char pressure_bytes[32768] __attribute__((aligned(64)));
static volatile uint64_t meta_sink;

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_route_t routes[N64];
    asian_meta_growth_only_context_t growth_context;
    asian_genuine_strip_context_t strip;
    float q[ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_PATHS];
    const clean_carrier_t *carrier;
    uint32_t strike_count;
    asian_arithmetic_selected_path_t selected_path;
    uint32_t magic;
    uint8_t reserved[40];
} meta_clean_request_t;

typedef struct __attribute__((aligned(64))) {
    asian_arithmetic_prepared_t *prepared;
    asian_genuine_arithmetic_fused_source_exp_context_t *fused;
    asian_genuine_arithmetic_growth_only_context_t *generic_growth;
    asian_genuine_strip_context_t *strip;
    float *generic_q;
    asian_meta_affine_route_t routes[ASIAN_META_DIRECTIONS];
    asian_meta_growth_only_context_t affine_growth;
    float affine_q[ASIAN_META_PATHS];
    uint32_t n;
    uint32_t k;
    asian_arithmetic_workload_t workload;
    asian_arithmetic_selected_path_t selected_path;
} pricing_cell_t;

static void pressure_32k(void)
{
    uint64_t value = meta_sink;
    for (uint32_t offset = 0; offset < sizeof(pressure_bytes); offset += 64u) {
        pressure_bytes[offset] = (unsigned char)(pressure_bytes[offset] + 1u);
        value += pressure_bytes[offset];
    }
    meta_sink = value;
}

static asian_arithmetic_selected_path_t meta_selected_path(
    uint32_t k, asian_arithmetic_workload_t workload)
{
    if ((k == 1u && workload == ASIAN_ARITHMETIC_PRICE) ||
        (k == 3u && workload == ASIAN_ARITHMETIC_PRICE_DELTA) || k >= 5u)
        return ASIAN_ARITHMETIC_SELECTED_STAGE1;
    return ASIAN_ARITHMETIC_SELECTED_IMMEDIATE;
}

static int meta_immediate_consume(
    const asian_meta_growth_only_context_t *growth,
    const asian_genuine_strip_context_t *strip, uint32_t requested_count,
    int price_delta, asian_genuine_strip_output_t *output)
{
    memset(output, 0, sizeof(*output));
    if (requested_count == 1u) {
        if (price_delta)
            asian_meta_arithmetic_growth_only_price_delta_1_diag(growth, strip,
                strip->strikes, output->values);
        else
            asian_meta_arithmetic_growth_only_price_1_diag(growth, strip,
                strip->strikes, output->values);
    } else if (requested_count == 2u) {
        if (price_delta)
            asian_meta_arithmetic_growth_only_price_delta_2_diag(growth, strip,
                strip->strikes, output->values);
        else
            asian_meta_arithmetic_growth_only_price_2_diag(growth, strip,
                strip->strikes, output->values);
    } else if (requested_count <= 4u) {
        if (price_delta)
            asian_meta_arithmetic_growth_only_price_delta_4_diag(growth, strip,
                strip->strikes, output->values);
        else
            asian_meta_arithmetic_growth_only_price_4_diag(growth, strip,
                strip->strikes, output->values);
    } else {
        return -1;
    }
    memset(output->values + requested_count, 0,
        (ASIAN_GENUINE_STRIP_MAX_STRIKES - requested_count) *
        sizeof(output->values[0]));
    return 0;
}

static int meta_request_prepare(const clean_carrier_t *carrier,
                                const asian_arithmetic_request_t *request,
                                meta_clean_request_t *out)
{
    if (carrier == NULL || request == NULL || out == NULL ||
        !valid_clean_request(carrier, request) || meta_plan == NULL ||
        meta_plan->magic != ASIAN_META_PLAN_MAGIC ||
        asian_meta_affine_routes_bind(meta_plan, NULL, carrier->growth, N64,
                                      out->routes) != 0)
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;
    memset(&out->growth_context, 0, sizeof(out->growth_context));
    out->growth_context.d1_growth = carrier->growth;
    out->growth_context.routes_d2 = out->routes + 1;
    out->growth_context.q_out = out->q;
    out->growth_context.fixing_count = N64;
    out->growth_context.s0 = (float)request->s0;
    out->growth_context.magic = ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC;
    out->growth_context.abi_version =
        ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION;
    uint32_t padded = 0u;
    if (asian_genuine_arithmetic_growth_only_strip_prepare_padded(
            &out->strip, request->s0, request->rate,
            request->dividend_yield, request->sigma, request->maturity,
            N64, 0u, 0.0, 0.0, request->strikes, 1u, &padded) != 0 ||
        padded != 1u)
        return ASIAN_ARITHMETIC_UNSUPPORTED_CONTRACT;
    out->carrier = carrier;
    out->strike_count = 1u;
    out->selected_path = ASIAN_ARITHMETIC_SELECTED_STAGE1;
    out->magic = REQUEST_MAGIC;
    return ASIAN_ARITHMETIC_OK;
}

static int meta_prepared_price(meta_clean_request_t *request,
                               asian_arithmetic_result_t *out)
{
    if (request == NULL || out == NULL || request->magic != REQUEST_MAGIC ||
        request->carrier == NULL || request->carrier->magic != CARRIER_MAGIC)
        return ASIAN_ARITHMETIC_INVALID_ARGUMENT;
    asian_genuine_strip_output_t internal __attribute__((aligned(64)));
    memset(&internal, 0, sizeof(internal));
    asian_meta_arithmetic_growth_only_q_diag(&request->growth_context);
    asian_genuine_strip_arithmetic_price_1_diag(request->q, request->q,
        &request->strip, request->strip.strikes, internal.values);
    memset(out, 0, sizeof(*out));
    out->strike_count = 1u;
    out->selected_path = ASIAN_ARITHMETIC_SELECTED_STAGE1;
    out->values[0].call_price = internal.values[0].call_price;
    out->values[0].put_price = internal.values[0].put_price;
    out->values[0].call_delta = internal.values[0].call_delta;
    out->values[0].put_delta = internal.values[0].put_delta;
    return ASIAN_ARITHMETIC_OK;
}

static int meta_fresh_total(const asian_arithmetic_request_t *request,
                            clean_carrier_t *carrier,
                            meta_clean_request_t *prepared,
                            asian_arithmetic_result_t *out)
{
    const carrier_input_t input = carrier_from_request(request);
    int status = asian_n64_clean_carrier_prepare(&engine, &input, carrier);
    if (status == ASIAN_ARITHMETIC_OK)
        status = meta_request_prepare(carrier, request, prepared);
    if (status == ASIAN_ARITHMETIC_OK)
        status = meta_prepared_price(prepared, out);
    return status;
}

static int meta_reuse_total(const clean_carrier_t *carrier,
                            const asian_arithmetic_request_t *request,
                            meta_clean_request_t *prepared,
                            asian_arithmetic_result_t *out)
{
    int status = meta_request_prepare(carrier, request, prepared);
    if (status == ASIAN_ARITHMETIC_OK)
        status = meta_prepared_price(prepared, out);
    return status;
}

static int prepare_pricing_cell(uint32_t n, uint32_t k,
                                asian_arithmetic_workload_t workload,
                                pricing_cell_t *cell)
{
    float strikes[32];
    for (uint32_t i = 0; i < k; ++i)
        strikes[i] = 83.0f + (float)((i * 13u + 5u) % 32u) * 1.25f;
    asian_arithmetic_request_t request = {
        .s0=100.0, .rate=0.03, .dividend_yield=0.0, .sigma=0.20,
        .maturity=1.0, .future_fixings=n, .strikes=strikes,
        .strike_count=k, .workload=workload,
    };
    memset(cell, 0, sizeof(*cell));
    if (asian_arithmetic_prepare(production_plan, &request, &cell->prepared) !=
          ASIAN_ARITHMETIC_OK ||
        asian_arithmetic_test_views(cell->prepared, &cell->fused,
          &cell->generic_growth, &cell->strip, &cell->generic_q) != 0 ||
        asian_meta_affine_routes_bind(meta_plan, NULL,
          cell->generic_growth->d1_growth, n, cell->routes) != 0)
        return -1;
    cell->affine_growth.d1_growth = cell->generic_growth->d1_growth;
    cell->affine_growth.routes_d2 = cell->routes + 1;
    cell->affine_growth.q_out = cell->affine_q;
    cell->affine_growth.fixing_count = n;
    cell->affine_growth.s0 = 100.0f;
    cell->affine_growth.magic = ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC;
    cell->affine_growth.abi_version =
        ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION;
    cell->n = n;
    cell->k = k;
    cell->workload = workload;
    cell->selected_path = meta_selected_path(k, workload);
    return 0;
}

static int run_pricing_cell(pricing_cell_t *cell, int affine,
                            asian_genuine_strip_output_t *out)
{
    const int price_delta = cell->workload == ASIAN_ARITHMETIC_PRICE_DELTA;
    memset(out, 0, sizeof(*out));
    asian_genuine_arithmetic_fused_source_exp_diag(cell->fused);
    if (cell->selected_path == ASIAN_ARITHMETIC_SELECTED_IMMEDIATE) {
        if (affine)
            return meta_immediate_consume(&cell->affine_growth, cell->strip,
                                          cell->k, price_delta, out);
        return asian_genuine_arithmetic_growth_only_immediate_consume(
            cell->generic_growth, cell->strip, cell->k, price_delta, 1, out) ==
            ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_USED ? 0 : -1;
    }
    if (affine) {
        asian_meta_arithmetic_growth_only_q_diag(&cell->affine_growth);
        return asian_genuine_arithmetic_growth_only_strip_consume_padded(
            cell->affine_q, cell->affine_q, cell->strip, cell->k,
            price_delta, out);
    }
    asian_genuine_arithmetic_growth_only_q_diag(cell->generic_growth);
    return asian_genuine_arithmetic_growth_only_strip_consume_padded(
        cell->generic_q, cell->generic_q, cell->strip, cell->k,
        price_delta, out);
}

static timing_t observe_pricing(pricing_cell_t *cell, int affine, int pressure)
{
    asian_genuine_strip_output_t output __attribute__((aligned(64)));
    if (pressure)
        pressure_32k();
    const uint64_t wall0 = wall_now();
    const uint64_t tsc0 = tsc_begin();
    const int status = run_pricing_cell(cell, affine, &output);
    const uint64_t tsc1 = tsc_end();
    const uint64_t wall1 = wall_now();
    if (status != 0)
        abort();
    meta_sink += hash_bytes(UINT64_C(1469598103934665603), &output,
                            sizeof(output));
    return (timing_t){wall1 - wall0, tsc1 - tsc0};
}

static double median_values(double *values, uint32_t count)
{
    qsort(values, count, sizeof(*values), compare_double);
    if ((count & 1u) != 0u)
        return values[count / 2u];
    return 0.5 * (values[count / 2u - 1u] + values[count / 2u]);
}

static int measure_builder_pair(timing_t *qsort_result,
                                timing_t *meta_result)
{
    double qw[BUILDER_SAMPLES], qt[BUILDER_SAMPLES];
    double mw[BUILDER_SAMPLES], mt[BUILDER_SAMPLES];
    for (uint32_t sample = 0; sample < BUILDER_SAMPLES; ++sample) {
        for (uint32_t phase = 0; phase < 2u; ++phase) {
            const int meta = ((sample + phase) & 1u) != 0u;
            const uint64_t wall0 = wall_now();
            const uint64_t tsc0 = tsc_begin();
            void *created = NULL;
            const int status = meta ?
                asian_meta_affine_plan_create((asian_meta_affine_plan_t **)&created) :
                asian_meta_qsort_control_plan_create(
                    (asian_meta_qsort_control_plan_t **)&created);
            const uint64_t tsc1 = tsc_end();
            const uint64_t wall1 = wall_now();
            if (status != 0 || created == NULL)
                return -1;
            if (meta) {
                mw[sample] = (double)(wall1 - wall0);
                mt[sample] = (double)(tsc1 - tsc0);
                asian_meta_affine_plan_destroy(created);
            } else {
                qw[sample] = (double)(wall1 - wall0);
                qt[sample] = (double)(tsc1 - tsc0);
                asian_meta_qsort_control_plan_destroy(created);
            }
        }
    }
    qsort_result->wall = (uint64_t)median_values(qw, BUILDER_SAMPLES);
    qsort_result->tsc = (uint64_t)median_values(qt, BUILDER_SAMPLES);
    meta_result->wall = (uint64_t)median_values(mw, BUILDER_SAMPLES);
    meta_result->tsc = (uint64_t)median_values(mt, BUILDER_SAMPLES);
    return 0;
}

static int measure_pricing_grid(void)
{
    static const uint32_t ns[META_GRID] = {16u,32u,64u,128u,256u};
    static const uint32_t ks[META_GRID] = {1u,2u,3u,4u,32u};
    puts("N  worst_wall  worst_tsc  median_wall  median_tsc  decision");
    for (uint32_t ni = 0; ni < META_GRID; ++ni) {
        double cells_wall[META_CELLS_PER_N];
        double cells_tsc[META_CELLS_PER_N];
        uint32_t cell_index = 0u;
        for (uint32_t ki = 0; ki < META_GRID; ++ki) {
            for (uint32_t workload = 0; workload < 2u; ++workload) {
                pricing_cell_t *cell = aligned_zero(sizeof(*cell));
                if (cell == NULL || prepare_pricing_cell(ns[ni], ks[ki],
                      (asian_arithmetic_workload_t)workload, cell) != 0)
                    return -1;
                asian_genuine_strip_output_t generic __attribute__((aligned(64)));
                asian_genuine_strip_output_t affine __attribute__((aligned(64)));
                if (run_pricing_cell(cell, 0, &generic) != 0 ||
                    run_pricing_cell(cell, 1, &affine) != 0 ||
                    memcmp(&generic, &affine, sizeof(generic)) != 0)
                    return -1;
                for (uint32_t cache = 0; cache < 2u; ++cache) {
                    double wall_ratio[SAMPLES], tsc_ratio[SAMPLES];
                    for (uint32_t quartet = 0; quartet < WARMUPS + SAMPLES;
                         ++quartet) {
                        uint64_t gw = 0u, gt = 0u, aw = 0u, at = 0u;
                        for (uint32_t observation = 0; observation < 4u;
                             ++observation) {
                            const int affine_first = (quartet & 1u) != 0u;
                            const int use_affine = observation == 0u ||
                                observation == 3u ? affine_first : !affine_first;
                            const timing_t value = observe_pricing(
                                cell, use_affine, cache != 0u);
                            if (use_affine) { aw += value.wall; at += value.tsc; }
                            else { gw += value.wall; gt += value.tsc; }
                        }
                        if (quartet >= WARMUPS) {
                            const uint32_t sample = quartet - WARMUPS;
                            wall_ratio[sample] = (double)gw / (double)aw;
                            tsc_ratio[sample] = (double)gt / (double)at;
                        }
                    }
                    cells_wall[cell_index] = median_values(wall_ratio, SAMPLES);
                    cells_tsc[cell_index] = median_values(tsc_ratio, SAMPLES);
                    ++cell_index;
                }
                asian_arithmetic_prepared_destroy(cell->prepared);
                free(cell);
            }
        }
        double wall_sorted[META_CELLS_PER_N], tsc_sorted[META_CELLS_PER_N];
        memcpy(wall_sorted, cells_wall, sizeof(wall_sorted));
        memcpy(tsc_sorted, cells_tsc, sizeof(tsc_sorted));
        const double median_wall = median_values(wall_sorted,
                                                 META_CELLS_PER_N);
        const double median_tsc = median_values(tsc_sorted,
                                                META_CELLS_PER_N);
        double worst_wall = cells_wall[0], worst_tsc = cells_tsc[0];
        int every = 1;
        for (uint32_t cell = 0; cell < META_CELLS_PER_N; ++cell) {
            if (cells_wall[cell] < worst_wall) worst_wall = cells_wall[cell];
            if (cells_tsc[cell] < worst_tsc) worst_tsc = cells_tsc[cell];
            if (cells_wall[cell] < 0.99 || cells_tsc[cell] < 0.99)
                every = 0;
        }
        const int affine_route = every && median_wall > 1.01 &&
                                 median_tsc > 1.01;
        printf("%u  %.6f  %.6f  %.6f  %.6f  %s\n", ns[ni], worst_wall,
               worst_tsc, median_wall, median_tsc,
               affine_route ? "AFFINE_ROUTE" : "GENERIC_ROUTE");
    }
    return 0;
}

enum meta_lifecycle_op { LIFE_GENERIC_FRESH, LIFE_AFFINE_FRESH,
                         LIFE_GENERIC_REUSE, LIFE_AFFINE_REUSE };
static clean_carrier_t meta_life_carrier __attribute__((aligned(64)));
static clean_request_t meta_generic_request __attribute__((aligned(64)));
static meta_clean_request_t meta_affine_request __attribute__((aligned(64)));
static asian_arithmetic_result_t meta_life_result;

static timing_t observe_lifecycle(enum meta_lifecycle_op operation,
                                  uint32_t input)
{
    const asian_arithmetic_request_t *request = operation >= LIFE_GENERIC_REUSE ?
        reuse_request(input) : fresh_request(input);
    const uint64_t wall0 = wall_now();
    const uint64_t tsc0 = tsc_begin();
    int status;
    if (operation == LIFE_GENERIC_FRESH)
        status = asian_n64_clean_fresh_total(&engine, request,
            &meta_life_carrier, &meta_generic_request, &meta_life_result);
    else if (operation == LIFE_AFFINE_FRESH)
        status = meta_fresh_total(request, &meta_life_carrier,
            &meta_affine_request, &meta_life_result);
    else if (operation == LIFE_GENERIC_REUSE)
        status = asian_n64_clean_reuse_total(&engine, &workspace->carrier_a,
            request, &meta_generic_request, &meta_life_result);
    else
        status = meta_reuse_total(&workspace->carrier_a, request,
            &meta_affine_request, &meta_life_result);
    const uint64_t tsc1 = tsc_end();
    const uint64_t wall1 = wall_now();
    if (status != 0)
        abort();
    meta_sink += hash_bytes(UINT64_C(1469598103934665603), &meta_life_result,
                            sizeof(meta_life_result));
    return (timing_t){wall1 - wall0, tsc1 - tsc0};
}

static void measure_lifecycle_pair(enum meta_lifecycle_op generic_op,
                                   enum meta_lifecycle_op affine_op,
                                   timing_t *generic, timing_t *affine)
{
    double gw[SAMPLES], gt[SAMPLES], aw[SAMPLES], at[SAMPLES];
    for (uint32_t quartet = 0; quartet < WARMUPS + SAMPLES; ++quartet) {
        uint64_t gws = 0u, gts = 0u, aws = 0u, ats = 0u;
        for (uint32_t observation = 0; observation < 4u; ++observation) {
            const int affine_first = (quartet & 1u) != 0u;
            const int use_affine = observation == 0u || observation == 3u ?
                                   affine_first : !affine_first;
            const timing_t value = observe_lifecycle(
                use_affine ? affine_op : generic_op, quartet & 1u);
            if (use_affine) { aws += value.wall; ats += value.tsc; }
            else { gws += value.wall; gts += value.tsc; }
        }
        if (quartet >= WARMUPS) {
            const uint32_t sample = quartet - WARMUPS;
            gw[sample] = 0.5 * (double)gws;
            gt[sample] = 0.5 * (double)gts;
            aw[sample] = 0.5 * (double)aws;
            at[sample] = 0.5 * (double)ats;
        }
    }
    generic->wall = (uint64_t)median_values(gw, SAMPLES);
    generic->tsc = (uint64_t)median_values(gt, SAMPLES);
    affine->wall = (uint64_t)median_values(aw, SAMPLES);
    affine->tsc = (uint64_t)median_values(at, SAMPLES);
}

static int meta_correctness(void)
{
    for (uint32_t input = 0; input < 2u; ++input) {
        asian_arithmetic_result_t generic, affine;
        const asian_arithmetic_request_t *fresh = fresh_request(input);
        if (asian_n64_clean_fresh_total(&engine, fresh, &meta_life_carrier,
              &meta_generic_request, &generic) != 0 ||
            meta_fresh_total(fresh, &meta_life_carrier, &meta_affine_request,
              &affine) != 0 || memcmp(&generic, &affine, sizeof(generic)) != 0)
            return -1;
        const asian_arithmetic_request_t *reuse = reuse_request(input);
        if (asian_n64_clean_reuse_total(&engine, &workspace->carrier_a, reuse,
              &meta_generic_request, &generic) != 0 ||
            meta_reuse_total(&workspace->carrier_a, reuse,
              &meta_affine_request, &affine) != 0 ||
            memcmp(&generic, &affine, sizeof(generic)) != 0)
            return -1;
    }
    return 0;
}

static int run_meta_timing(void)
{
    timing_t qsort_builder, meta_builder;
    if (measure_builder_pair(&qsort_builder, &meta_builder) != 0)
        return -1;
    puts("plan                         wall_ns      tsc_ticks    speedup");
    printf("%-28s %12" PRIu64 " %14" PRIu64 " %10.3f\n",
           "current_qsort_builder", qsort_builder.wall, qsort_builder.tsc, 1.0);
    printf("%-28s %12" PRIu64 " %14" PRIu64 " %10.3f\n",
           "meta_direction_builder", meta_builder.wall, meta_builder.tsc,
           (double)qsort_builder.wall / (double)meta_builder.wall);
    if (measure_pricing_grid() != 0)
        return -1;
    timing_t generic_fresh, affine_fresh, generic_reuse, affine_reuse;
    measure_lifecycle_pair(LIFE_GENERIC_FRESH, LIFE_AFFINE_FRESH,
                           &generic_fresh, &affine_fresh);
    measure_lifecycle_pair(LIFE_GENERIC_REUSE, LIFE_AFFINE_REUSE,
                           &generic_reuse, &affine_reuse);
    puts("metric                              wall_ns     tsc_ticks");
    print_metric("generic_clean_fresh_total", generic_fresh);
    print_metric("affine_clean_fresh_total", affine_fresh);
    print_metric("generic_clean_reuse_total", generic_reuse);
    print_metric("affine_clean_reuse_total", affine_reuse);
    return 0;
}

int main(int argc, char **argv)
{
    int timing = 0, native = 0, cpu = -1;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--check") == 0) {
            timing = 0;
        } else if (strcmp(argv[i], "--timing") == 0) {
            timing = 1; native = 1;
        } else if (strcmp(argv[i], "--native-check") == 0) {
            timing = 0; native = 1;
        } else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
            cpu = atoi(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s --check | --native-check --cpu 0 | "
                            "--timing --cpu 0\n", argv[0]);
            return 2;
        }
    }
    if (native && (cpu != 0 || pin_cpu(cpu) != 0 || !is_sapphire_rapids())) {
        fprintf(stderr, "Sapphire Rapids CPU 0 is required\n");
        return 2;
    }
    if (initialize_engine() != 0 || prepare_persistent_cells() != 0 ||
        asian_meta_affine_plan_create(&meta_plan) != 0) {
        fprintf(stderr, "engine initialization failed\n");
        return 2;
    }
    if (bounded_correctness() != 0 || meta_correctness() != 0) {
        fprintf(stderr, "meta correctness failed\n");
        return 1;
    }
    if (!timing) {
        puts("bounded_native_correctness PASS");
    } else if (run_meta_timing() != 0) {
        return 1;
    }
    asian_meta_affine_plan_destroy(meta_plan);
    meta_plan = NULL;
    release_engine();
    return meta_sink == UINT64_MAX ? 1 : 0;
}
