#define _GNU_SOURCE

#include "asian_arithmetic_pricer.h"
#include "private/asian_arithmetic_pricer_test.h"
#include "private/asian_genuine_arithmetic_growth_only_strip_adapter.h"

#include <immintrin.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { SAMPLES = 51, WARMUPS = 16 };

typedef struct {
    asian_arithmetic_prepared_t *prepared;
    asian_genuine_arithmetic_fused_source_exp_context_t *fused;
    asian_genuine_arithmetic_growth_only_context_t *growth;
    asian_genuine_strip_context_t *strip;
    float *q;
    uint32_t k;
    asian_arithmetic_workload_t workload;
    asian_arithmetic_selected_path_t path;
} cell_t;

typedef struct { uint64_t tsc; uint64_t wall; } timing_t;

static volatile double sink;

static uint64_t wall_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static uint64_t tsc_start(void)
{
    _mm_mfence(); _mm_lfence();
    return __rdtsc();
}

static uint64_t tsc_stop(void)
{
    unsigned aux;
    const uint64_t value = __rdtscp(&aux);
    _mm_lfence(); _mm_mfence();
    return value;
}

static int pin_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

static int compare_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void copy_result(const asian_genuine_strip_output_t *source,
                        uint32_t k, asian_arithmetic_selected_path_t path,
                        asian_arithmetic_result_t *out)
{
    memset(out, 0, sizeof(*out));
    out->strike_count = k;
    out->selected_path = path;
    for (uint32_t i = 0; i < k; ++i) {
        out->values[i].call_price = source->values[i].call_price;
        out->values[i].put_price = source->values[i].put_price;
        out->values[i].call_delta = source->values[i].call_delta;
        out->values[i].put_delta = source->values[i].put_delta;
    }
}

static int direct_price(cell_t *cell, asian_arithmetic_result_t *out)
{
    asian_genuine_strip_output_t internal __attribute__((aligned(64)));
    memset(&internal, 0, sizeof(internal));
    asian_genuine_arithmetic_fused_source_exp_diag(cell->fused);
    if (cell->path == ASIAN_ARITHMETIC_SELECTED_IMMEDIATE) {
        if (asian_genuine_arithmetic_growth_only_immediate_consume(
              cell->growth, cell->strip, cell->k,
              cell->workload == ASIAN_ARITHMETIC_PRICE_DELTA, 1,
              &internal) !=
            ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_USED)
            return -1;
    } else {
        asian_genuine_arithmetic_growth_only_q_diag(cell->growth);
        if (asian_genuine_arithmetic_growth_only_strip_consume_padded(
              cell->q, cell->q, cell->strip, cell->k,
              cell->workload == ASIAN_ARITHMETIC_PRICE_DELTA,
              &internal) != 0)
            return -1;
    }
    copy_result(&internal, cell->k, cell->path, out);
    return 0;
}

static timing_t observe(cell_t *cell, int production, uint32_t repetitions)
{
    asian_arithmetic_result_t output;
    const uint64_t wall0 = wall_ns();
    const uint64_t tsc0 = tsc_start();
    for (uint32_t i = 0; i < repetitions; ++i) {
        const int status = production ?
            asian_arithmetic_price_prepared(cell->prepared, &output) :
            direct_price(cell, &output);
        if (status != 0)
            abort();
        sink += output.values[i % cell->k].call_price;
    }
    const uint64_t tsc1 = tsc_stop();
    const uint64_t wall1 = wall_ns();
    timing_t out = {tsc1 - tsc0, wall1 - wall0};
    return out;
}

static int prepare_cell(const asian_arithmetic_route_plan_t *plan,
                        uint32_t n, uint32_t k,
                        asian_arithmetic_workload_t workload, cell_t *cell)
{
    float strikes[32];
    for (uint32_t i = 0; i < k; ++i)
        strikes[i] = 88.0f + (float)((i * 13u + 5u) % k) * 0.8f;
    asian_arithmetic_request_t request = {
        .s0=100.0, .rate=0.03, .dividend_yield=0.0, .sigma=0.20,
        .maturity=1.0, .future_fixings=n, .strikes=strikes,
        .strike_count=k, .workload=workload,
    };
    memset(cell, 0, sizeof(*cell));
    if (asian_arithmetic_prepare(plan, &request, &cell->prepared) !=
          ASIAN_ARITHMETIC_OK)
        return -1;
    if (asian_arithmetic_test_views(cell->prepared, &cell->fused,
          &cell->growth, &cell->strip, &cell->q) != 0)
        return -1;
    asian_arithmetic_result_t result;
    if (asian_arithmetic_price_prepared(cell->prepared, &result) !=
          ASIAN_ARITHMETIC_OK)
        return -1;
    cell->k = k;
    cell->workload = workload;
    cell->path = result.selected_path;
    asian_arithmetic_result_t direct;
    if (direct_price(cell, &direct) != 0 ||
        memcmp(&result, &direct, sizeof(result)) != 0)
        return -1;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "--cpu") != 0) {
        fprintf(stderr, "usage: %s --cpu CPU\n", argv[0]);
        return 2;
    }
    const int cpu = atoi(argv[2]);
    if (cpu < 0 || pin_cpu(cpu) != 0 ||
        !__builtin_cpu_supports("avx512f") ||
        !__builtin_cpu_supports("avx512bw")) {
        fprintf(stderr, "native AVX-512 CPU required\n");
        return 2;
    }

    asian_arithmetic_route_plan_t *plan = NULL;
    if (asian_arithmetic_route_plan_create(&plan) != ASIAN_ARITHMETIC_OK)
        return 2;
    static const uint32_t n_values[] = {16u,32u,64u,128u,256u};
    static const uint32_t k_values[] = {1u,2u,3u,4u,32u};
    puts("N\tworst_wall\tworst_tsc\tmedian_wall\tmedian_tsc\tdecision");
    int failed = 0;
    for (uint32_t ni = 0; ni < 5u; ++ni) {
        double cell_wall[10], cell_tsc[10];
        uint32_t cell_index = 0;
        for (uint32_t ki = 0; ki < 5u; ++ki) {
            for (uint32_t workload = 0; workload < 2u; ++workload) {
                cell_t cell;
                if (prepare_cell(plan, n_values[ni], k_values[ki],
                      (asian_arithmetic_workload_t)workload, &cell) != 0)
                    return 2;
                const uint32_t repetitions = 2048u / n_values[ni];
                for (uint32_t warm = 0; warm < WARMUPS; ++warm) {
                    (void)observe(&cell, warm & 1u, repetitions);
                    (void)observe(&cell, !(warm & 1u), repetitions);
                }
                double wall_ratios[SAMPLES], tsc_ratios[SAMPLES];
                for (uint32_t sample = 0; sample < SAMPLES; ++sample) {
                    timing_t d0, d1, p0, p1;
                    if ((sample & 1u) == 0u) {
                        d0=observe(&cell,0,repetitions);p0=observe(&cell,1,repetitions);
                        p1=observe(&cell,1,repetitions);d1=observe(&cell,0,repetitions);
                    } else {
                        p0=observe(&cell,1,repetitions);d0=observe(&cell,0,repetitions);
                        d1=observe(&cell,0,repetitions);p1=observe(&cell,1,repetitions);
                    }
                    wall_ratios[sample]=((double)d0.wall+(double)d1.wall)/
                                        ((double)p0.wall+(double)p1.wall);
                    tsc_ratios[sample]=((double)d0.tsc+(double)d1.tsc)/
                                       ((double)p0.tsc+(double)p1.tsc);
                }
                qsort(wall_ratios,SAMPLES,sizeof(double),compare_double);
                qsort(tsc_ratios,SAMPLES,sizeof(double),compare_double);
                cell_wall[cell_index]=wall_ratios[SAMPLES/2];
                cell_tsc[cell_index]=tsc_ratios[SAMPLES/2];
                ++cell_index;
                asian_arithmetic_prepared_destroy(cell.prepared);
            }
        }
        qsort(cell_wall,10u,sizeof(double),compare_double);
        qsort(cell_tsc,10u,sizeof(double),compare_double);
        const double median_wall=(cell_wall[4]+cell_wall[5])*0.5;
        const double median_tsc=(cell_tsc[4]+cell_tsc[5])*0.5;
        const int pass=cell_wall[0]>=0.99&&cell_tsc[0]>=0.99;
        printf("%u\t%.6f\t%.6f\t%.6f\t%.6f\t%s\n",
               n_values[ni],cell_wall[0],cell_tsc[0],median_wall,median_tsc,
               pass?"PASS":"FAIL");
        failed |= !pass;
    }
    asian_arithmetic_route_plan_destroy(plan);
    return failed ? 1 : 0;
}
