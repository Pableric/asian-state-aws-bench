#define _GNU_SOURCE

#include "private/autocall_two_asset_three_date_worstof_raw_diag.h"
#include "private/autocall_single_asset_three_date_raw_diag.h"
#include "tests/autocall_single_asset_three_date_cases.h"
#include "tests/autocall_two_asset_three_date_worstof_cases.h"

#include <cpuid.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { OBSERVATIONS = 101, WARMUPS = 16, PRESSURE_BYTES = 32768 };
enum candidate { PREPARED_CARRIER = 0, INLINE_CORRELATION = 1 };
enum metric { MARKET_PREPARE, REQUEST_PREPARE, PREPARED_PRICE, REUSE_TOTAL,
              FRESH_TOTAL, METRIC_COUNT };
enum cache_mode { CANDIDATE_WARM, PRESSURE_32K };

typedef struct { uint64_t wall, tsc; } sample_t;

typedef struct {
    autocall_worstof_engine_t engine __attribute__((aligned(64)));
    autocall_worstof_market_input_t market_input[2];
    autocall_worstof_contract_input_t contract[2];
    autocall_worstof_prepared_market_t *prepared_market[2];
    autocall_worstof_prepared_market_t *prepared_scratch[2];
    autocall_worstof_inline_market_t *inline_market[2];
    autocall_worstof_inline_market_t *inline_scratch[2];
    autocall_worstof_prepared_request_t *prepared_request[2];
    autocall_worstof_prepared_request_t *prepared_request_scratch[2];
    autocall_worstof_inline_request_t *inline_request[2];
    autocall_worstof_inline_request_t *inline_request_scratch[2];
    double expected[2];
    unsigned char *pressure;
    asian_meta_qsort_control_plan_t *generic_oracle;
} fixture_t;

static volatile double sink;

static void *a64(size_t bytes) {
    void *p = NULL;
    return posix_memalign(&p, 64u, bytes) == 0 ? p : NULL;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec*UINT64_C(1000000000)+(uint64_t)ts.tv_nsec;
}

static inline uint64_t tsc_begin(void) {
    unsigned lo, hi;
    __asm__ volatile("lfence\n\trdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t tsc_end(void) {
    unsigned lo, hi, aux;
    __asm__ volatile("rdtscp\n\tlfence" : "=a"(lo), "=d"(hi), "=c"(aux)
                     :: "memory");
    return ((uint64_t)hi << 32) | lo;
}

static int compare_u64(const void *a, const void *b) {
    const uint64_t aa = *(const uint64_t *)a;
    const uint64_t bb = *(const uint64_t *)b;
    return aa > bb ? 1 : aa < bb ? -1 : 0;
}

static uint64_t median(uint64_t values[OBSERVATIONS]) {
    qsort(values, OBSERVATIONS, sizeof(values[0]), compare_u64);
    return values[OBSERVATIONS/2];
}

static uint64_t median5(uint64_t values[5]) {
    qsort(values, 5, sizeof(values[0]), compare_u64);
    return values[2];
}

static int same_double(double a, double b) {
    uint64_t aa;
    uint64_t bb;
    memcpy(&aa, &a, sizeof(aa));
    memcpy(&bb, &b, sizeof(bb));
    return aa == bb;
}

static int pin_cpu(unsigned cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set);
}

static int sapphire_rapids(void) {
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d))
        return 0;
    const unsigned base_family = (a >> 8) & 15u;
    const unsigned family = base_family +
        (base_family == 15u ? (a >> 20) & 255u : 0u);
    const unsigned model = ((a >> 4) & 15u) | (((a >> 16) & 15u) << 4);
    return family == 6u && model == 143u;
}

static void variant(const autocall_worstof_case_t *c,
                    autocall_worstof_market_input_t *m,
                    autocall_worstof_contract_input_t *contract) {
    *m = c->market;
    *contract = c->contract;
    m->rate += 0.001;
    m->sigma_a *= 1.03;
    m->sigma_b *= 0.97;
    m->rho += m->rho < 0.85 ? 0.02 : -0.02;
    contract->spot_a *= 1.02;
    contract->spot_b *= 0.98;
}

static int fixture_create(fixture_t *f,
                          const autocall_worstof_case_t *case0) {
    memset(f, 0, sizeof(*f));
    if (autocall_worstof_engine_create(&f->engine) != 0 ||
        asian_meta_qsort_control_plan_create(&f->generic_oracle) != 0)
        return -1;
    f->market_input[0] = case0->market;
    f->contract[0] = case0->contract;
    variant(case0, &f->market_input[1], &f->contract[1]);
    f->pressure = a64(PRESSURE_BYTES);
    if (f->pressure == NULL)
        return -1;
    memset(f->pressure, 1, PRESSURE_BYTES);
    for (unsigned i = 0; i < 2; ++i) {
#define ALLOC(member) do { f->member[i] = a64(sizeof(*f->member[i])); \
    if (f->member[i] == NULL) return -1; } while (0)
        ALLOC(prepared_market);
        ALLOC(prepared_scratch);
        ALLOC(inline_market);
        ALLOC(inline_scratch);
        ALLOC(prepared_request);
        ALLOC(prepared_request_scratch);
        ALLOC(inline_request);
        ALLOC(inline_request_scratch);
#undef ALLOC
        autocall_worstof_output_t po;
        autocall_worstof_output_t io;
        if (autocall_worstof_prepared_market_prepare(
                &f->engine, &f->market_input[i], f->prepared_market[i]) != 0 ||
            autocall_worstof_inline_market_prepare(
                &f->engine, &f->market_input[i], f->inline_market[i]) != 0 ||
            autocall_worstof_prepared_request_prepare(
                &f->engine, f->prepared_market[i], &f->contract[i],
                f->prepared_request[i]) != 0 ||
            autocall_worstof_inline_request_prepare(
                &f->engine, f->inline_market[i], &f->contract[i],
                f->inline_request[i]) != 0 ||
            autocall_worstof_prepared_price(f->prepared_request[i], &po) != 0 ||
            autocall_worstof_inline_price(f->inline_request[i], &io) != 0 ||
            !same_double(po.price, io.price))
            return -1;
        f->expected[i] = po.price;
    }
    return 0;
}

static void fixture_destroy(fixture_t *f) {
    for (unsigned i = 0; i < 2; ++i) {
        free(f->inline_request_scratch[i]);
        free(f->inline_request[i]);
        free(f->prepared_request_scratch[i]);
        free(f->prepared_request[i]);
        free(f->inline_scratch[i]);
        free(f->inline_market[i]);
        free(f->prepared_scratch[i]);
        free(f->prepared_market[i]);
    }
    free(f->pressure);
    asian_meta_qsort_control_plan_destroy(f->generic_oracle);
    autocall_worstof_engine_destroy(&f->engine);
}

static void pressure_32k(fixture_t *f) {
    for (unsigned i = 0; i < PRESSURE_BYTES; i += 64)
        f->pressure[i] = (unsigned char)(f->pressure[i] + 1u);
    __asm__ volatile("" :: "r"(f->pressure) : "memory");
}

static void touch_lines(const void *pointer, size_t bytes,
                        volatile unsigned char *value) {
    const unsigned char *p = pointer;
    for (size_t i = 0; i < bytes; i += 64u)
        *value ^= p[i];
}

static void touch_map(const asian_meta_affine_route_t *route,
                      volatile unsigned char *value) {
    const unsigned char *map = (const unsigned char *)(const void *)route->map;
    for (unsigned i = 0; i < 256u; i += 64u)
        *value ^= map[i];
    *value ^= map[320];
    *value ^= map[384];
}

static void candidate_warm(fixture_t *f, enum candidate candidate,
                           enum metric metric, unsigned which) {
    volatile unsigned char value = 0;
    if (candidate == PREPARED_CARRIER) {
        const autocall_worstof_prepared_market_t *market =
            metric == MARKET_PREPARE ? f->prepared_scratch[which] :
                                       f->prepared_market[which];
        const autocall_worstof_prepared_request_t *request =
            metric == REQUEST_PREPARE ? f->prepared_request_scratch[which] :
                                        f->prepared_request[which];
        if (metric == MARKET_PREPARE) {
            touch_lines(&f->market_input[which],
                        sizeof(f->market_input[which]), &value);
            touch_lines(asian_genuine_fixed_block_signed_z, 32768u, &value);
            for (unsigned d = 1; d < 6; ++d)
                touch_map(f->prepared_request[which]->routes + d, &value);
            touch_lines(market, sizeof(*market), &value);
        } else if (metric == REQUEST_PREPARE) {
            value ^= *(const unsigned char *)(const void *)&market->generation;
            touch_lines(&f->contract[which], sizeof(f->contract[which]), &value);
            touch_lines(f->engine.affine_plan, 320u, &value);
            touch_lines(request, sizeof(*request), &value);
        } else if (metric == PREPARED_PRICE) {
            touch_lines(market->asset_a_growth, 32768u, &value);
            touch_lines(market->asset_b_growth, 49152u, &value);
            touch_lines(request, sizeof(*request), &value);
            touch_map(f->prepared_request[which]->routes + 2, &value);
            touch_map(f->prepared_request[which]->routes + 4, &value);
            value ^= *(const unsigned char *)(const void *)&market->generation;
        } else if (metric == REUSE_TOTAL) {
            touch_lines(market->asset_a_growth, 32768u, &value);
            touch_lines(market->asset_b_growth, 49152u, &value);
            touch_lines(&f->contract[which], sizeof(f->contract[which]), &value);
            touch_lines(f->engine.affine_plan, 320u, &value);
            touch_lines(&f->engine.workspace->request.prepared,
                        sizeof(f->engine.workspace->request.prepared), &value);
            touch_map(f->prepared_request[which]->routes + 2, &value);
            touch_map(f->prepared_request[which]->routes + 4, &value);
            value ^= *(const unsigned char *)(const void *)&market->generation;
        } else {
            touch_lines(&f->market_input[which], sizeof(f->market_input[which]),
                        &value);
            touch_lines(&f->contract[which], sizeof(f->contract[which]), &value);
            touch_lines(asian_genuine_fixed_block_signed_z, 32768u, &value);
            for (unsigned d = 1; d < 6; ++d)
                touch_map(f->prepared_request[which]->routes + d, &value);
            touch_lines(&f->engine.workspace->market.prepared,
                        sizeof(f->engine.workspace->market.prepared), &value);
            touch_lines(&f->engine.workspace->request.prepared,
                        sizeof(f->engine.workspace->request.prepared), &value);
        }
    } else {
        const autocall_worstof_inline_market_t *market =
            metric == MARKET_PREPARE ? f->inline_scratch[which] :
                                       f->inline_market[which];
        const autocall_worstof_inline_request_t *request =
            metric == REQUEST_PREPARE ? f->inline_request_scratch[which] :
                                        f->inline_request[which];
        if (metric == MARKET_PREPARE) {
            touch_lines(&f->market_input[which],
                        sizeof(f->market_input[which]), &value);
            touch_lines(market, sizeof(*market), &value);
        } else if (metric == REQUEST_PREPARE) {
            value ^= *(const unsigned char *)(const void *)&market->generation;
            touch_lines(&f->contract[which], sizeof(f->contract[which]), &value);
            touch_lines(f->engine.affine_plan, 320u, &value);
            touch_lines(request, sizeof(*request), &value);
        } else if (metric == PREPARED_PRICE) {
            touch_lines(asian_genuine_fixed_block_signed_z, 32768u, &value);
            for (unsigned d = 1; d < 6; ++d)
                touch_map(f->inline_request[which]->routes + d, &value);
            touch_lines(request, sizeof(*request), &value);
            value ^= *(const unsigned char *)(const void *)&market->generation;
            touch_lines(asian_genuine_arithmetic_fused_exp_constants, 48u,
                        &value);
        } else if (metric == REUSE_TOTAL) {
            touch_lines(asian_genuine_fixed_block_signed_z, 32768u, &value);
            touch_lines(&f->contract[which], sizeof(f->contract[which]), &value);
            touch_lines(f->engine.affine_plan, 320u, &value);
            touch_lines(&f->engine.workspace->request.inline_request,
                        sizeof(f->engine.workspace->request.inline_request),
                        &value);
            for (unsigned d = 1; d < 6; ++d)
                touch_map(f->inline_request[which]->routes + d, &value);
            value ^= *(const unsigned char *)(const void *)&market->generation;
            touch_lines(asian_genuine_arithmetic_fused_exp_constants, 48u,
                        &value);
        } else {
            touch_lines(&f->market_input[which], sizeof(f->market_input[which]),
                        &value);
            touch_lines(&f->contract[which], sizeof(f->contract[which]), &value);
            touch_lines(asian_genuine_fixed_block_signed_z, 32768u, &value);
            for (unsigned d = 1; d < 6; ++d)
                touch_map(f->inline_request[which]->routes + d, &value);
            touch_lines(&f->engine.workspace->market.inline_market,
                        sizeof(f->engine.workspace->market.inline_market),
                        &value);
            touch_lines(&f->engine.workspace->request.inline_request,
                        sizeof(f->engine.workspace->request.inline_request),
                        &value);
            touch_lines(asian_genuine_arithmetic_fused_exp_constants, 48u,
                        &value);
        }
    }
    __asm__ volatile("" :: "r"(value) : "memory");
}

static int operation(fixture_t *f, enum candidate candidate,
                     enum metric metric, unsigned which,
                     autocall_worstof_output_t *output) {
    if (candidate == PREPARED_CARRIER) {
        if (metric == MARKET_PREPARE)
            return autocall_worstof_prepared_market_prepare(
                &f->engine, &f->market_input[which], f->prepared_scratch[which]);
        if (metric == REQUEST_PREPARE)
            return autocall_worstof_prepared_request_prepare(
                &f->engine, f->prepared_market[which], &f->contract[which],
                f->prepared_request_scratch[which]);
        if (metric == PREPARED_PRICE)
            return autocall_worstof_prepared_price(
                f->prepared_request[which], output);
        if (metric == REUSE_TOTAL)
            return autocall_worstof_prepared_reuse_total(
                &f->engine, f->prepared_market[which], &f->contract[which],
                output);
        return autocall_worstof_prepared_fresh_total(
            &f->engine, &f->market_input[which], &f->contract[which], output);
    }
    if (metric == MARKET_PREPARE)
        return autocall_worstof_inline_market_prepare(
            &f->engine, &f->market_input[which], f->inline_scratch[which]);
    if (metric == REQUEST_PREPARE)
        return autocall_worstof_inline_request_prepare(
            &f->engine, f->inline_market[which], &f->contract[which],
            f->inline_request_scratch[which]);
    if (metric == PREPARED_PRICE)
        return autocall_worstof_inline_price(f->inline_request[which], output);
    if (metric == REUSE_TOTAL)
        return autocall_worstof_inline_reuse_total(
            &f->engine, f->inline_market[which], &f->contract[which], output);
    return autocall_worstof_inline_fresh_total(
        &f->engine, &f->market_input[which], &f->contract[which], output);
}

static sample_t observe(fixture_t *f, enum candidate candidate,
                        enum metric metric, unsigned which,
                        enum cache_mode cache) {
    if (cache == PRESSURE_32K)
        pressure_32k(f);
    else
        candidate_warm(f, candidate, metric, which);
    autocall_worstof_output_t output = {0};
    const uint64_t w0 = now_ns();
    const uint64_t t0 = tsc_begin();
    const int status = operation(f, candidate, metric, which, &output);
    const uint64_t t1 = tsc_end();
    const uint64_t w1 = now_ns();
    if (status != 0)
        abort();
    if (metric >= PREPARED_PRICE) {
        if (!same_double(output.price, f->expected[which]))
            abort();
        sink += output.price;
    } else if (metric == MARKET_PREPARE) {
        autocall_worstof_output_t check;
        int check_status;
        if (candidate == PREPARED_CARRIER) {
            check_status = autocall_worstof_prepared_request_prepare(
                &f->engine, f->prepared_scratch[which], &f->contract[which],
                f->prepared_request_scratch[which]);
            if (check_status == 0)
                check_status = autocall_worstof_prepared_price(
                    f->prepared_request_scratch[which], &check);
        } else {
            check_status = autocall_worstof_inline_request_prepare(
                &f->engine, f->inline_scratch[which], &f->contract[which],
                f->inline_request_scratch[which]);
            if (check_status == 0)
                check_status = autocall_worstof_inline_price(
                    f->inline_request_scratch[which], &check);
        }
        if (check_status != 0 || !same_double(check.price, f->expected[which]))
            abort();
    } else {
        autocall_worstof_output_t check;
        int check_status = candidate == PREPARED_CARRIER ?
            autocall_worstof_prepared_price(
                f->prepared_request_scratch[which], &check) :
            autocall_worstof_inline_price(
                f->inline_request_scratch[which], &check);
        if (check_status != 0 || !same_double(check.price, f->expected[which]))
            abort();
    }
    const sample_t sample = {w1-w0, t1-t0};
    return sample;
}

static void measure_pair(fixture_t *f, enum metric metric,
                         enum cache_mode cache, sample_t result[2]) {
    for (unsigned warm = 0; warm < WARMUPS; ++warm) {
        const unsigned first = warm & 1u;
        const unsigned order[4] = {first, first^1u, first^1u, first};
        for (unsigned j = 0; j < 4; ++j)
            (void)observe(f, (enum candidate)order[j], metric,
                          (warm+j)&1u, cache);
    }
    uint64_t wall[2][OBSERVATIONS];
    uint64_t tsc[2][OBSERVATIONS];
    for (unsigned group = 0; group < OBSERVATIONS; ++group) {
        const unsigned first = group & 1u;
        const unsigned order[4] = {first, first^1u, first^1u, first};
        sample_t pair[2][2];
        unsigned count[2] = {0, 0};
        for (unsigned j = 0; j < 4; ++j) {
            const unsigned candidate = order[j];
            pair[candidate][count[candidate]++] = observe(
                f, (enum candidate)candidate, metric, (group+j)&1u, cache);
        }
        for (unsigned candidate = 0; candidate < 2; ++candidate) {
            wall[candidate][group] =
                (pair[candidate][0].wall+pair[candidate][1].wall)/2u;
            tsc[candidate][group] =
                (pair[candidate][0].tsc+pair[candidate][1].tsc)/2u;
        }
    }
    for (unsigned candidate = 0; candidate < 2; ++candidate) {
        result[candidate].wall = median(wall[candidate]);
        result[candidate].tsc = median(tsc[candidate]);
    }
}

static int check_only(void) {
    const autocall_worstof_case_t *cases = autocall_worstof_cases();
    for (unsigned ordinal = 0; ordinal < 5u; ++ordinal) {
        const int index = autocall_worstof_native_case_index(ordinal);
        fixture_t f;
        if (index < 0 || fixture_create(&f, &cases[index]) != 0)
            return 1;
        for (unsigned i = 0; i < 2; ++i) {
            autocall_worstof_output_t p;
            autocall_worstof_output_t q;
            if (autocall_worstof_prepared_price(
                    f.prepared_request[i], &p) != 0 ||
                autocall_worstof_inline_price(f.inline_request[i], &q) != 0 ||
                !same_double(p.price, q.price) ||
                !same_double(p.price, f.expected[i]))
                return 1;
        }
        fixture_destroy(&f);
    }
    puts("benchmark_correctness PASS identity=PASS frozen_native_cases=5");
    return 0;
}

static autocall_3date_request_input_t parent_input(void) {
    const autocall_frozen_case_t *c = &autocall_frozen_cases[1];
    autocall_3date_request_input_t out = {0};
    out.s0 = c->s0;
    out.rate = c->rate;
    out.dividend_yield = c->dividend;
    out.sigma = c->sigma;
    out.maturity = c->maturity;
    out.notional = c->notional;
    out.protection_barrier = c->protection;
    out.terminal_payment_time = c->maturity*(1.0+c->payment_lag_fraction);
    for (unsigned d = 0; d < 3; ++d) {
        out.call_barrier[d] = c->call_barrier[d];
        out.coupon_barrier[d] = c->coupon_barrier[d];
        out.coupon_cashflow[d] = c->coupon[d];
        out.call_redemption[d] = c->redemption[d];
        const double observation = c->maturity*(d+1.0)/3.0;
        out.coupon_payment_time[d] = observation +
            c->payment_lag_fraction*c->maturity;
        out.call_payment_time[d] = out.coupon_payment_time[d];
    }
    return out;
}

static int parent_single_asset_context(void) {
    autocall_3date_engine_t engine __attribute__((aligned(64)));
    autocall_3date_carrier_t *carrier = a64(sizeof(*carrier));
    autocall_3date_request_t *request = a64(sizeof(*request));
    if (carrier == NULL || request == NULL ||
        autocall_3date_engine_create(&engine) != 0)
        return -1;
    const autocall_3date_request_input_t input = parent_input();
    const autocall_3date_market_input_t market = {
        input.rate, input.dividend_yield, input.sigma, input.maturity};
    autocall_3date_output_t expected;
    if (autocall_3date_market_prepare(&engine, &market, carrier) != 0 ||
        autocall_3date_request_prepare(&engine, carrier, &input, request) != 0 ||
        autocall_3date_prepared_price(request, &expected) != 0)
        return -1;
    for (unsigned warm = 0; warm < WARMUPS; ++warm) {
        autocall_3date_output_t out;
        (void)autocall_3date_prepared_price(request, &out);
        (void)autocall_3date_fresh_total(&engine, &input, &out);
    }
    uint64_t prepared_wall[OBSERVATIONS], prepared_tsc[OBSERVATIONS];
    uint64_t fresh_wall[OBSERVATIONS], fresh_tsc[OBSERVATIONS];
    for (unsigned observation = 0; observation < OBSERVATIONS; ++observation) {
        volatile unsigned char warm_value = 0;
        touch_lines(carrier->growth, 32768u, &warm_value);
        touch_lines(request, sizeof(*request), &warm_value);
        __asm__ volatile("" :: "r"(warm_value) : "memory");
        autocall_3date_output_t out;
        uint64_t w0 = now_ns(), t0 = tsc_begin();
        if (autocall_3date_prepared_price(request, &out) != 0)
            return -1;
        uint64_t t1 = tsc_end(), w1 = now_ns();
        if (!same_double(out.price, expected.price))
            return -1;
        prepared_wall[observation] = w1-w0;
        prepared_tsc[observation] = t1-t0;
        touch_lines(asian_genuine_fixed_block_signed_z, 32768u, &warm_value);
        w0 = now_ns(); t0 = tsc_begin();
        if (autocall_3date_fresh_total(&engine, &input, &out) != 0)
            return -1;
        t1 = tsc_end(); w1 = now_ns();
        if (!same_double(out.price, expected.price))
            return -1;
        fresh_wall[observation] = w1-w0;
        fresh_tsc[observation] = t1-t0;
        sink += out.price;
    }
    printf("PARENT_SINGLE_ASSET_CONTEXT lifecycle=prepared cache=candidate_warm "
           "wall_ns=%llu tsc_ticks=%llu identity=PASS\n",
           (unsigned long long)median(prepared_wall),
           (unsigned long long)median(prepared_tsc));
    printf("PARENT_SINGLE_ASSET_CONTEXT lifecycle=fresh cache=candidate_warm "
           "wall_ns=%llu tsc_ticks=%llu identity=PASS\n",
           (unsigned long long)median(fresh_wall),
           (unsigned long long)median(fresh_tsc));
    autocall_3date_engine_destroy(&engine);
    free(request);
    free(carrier);
    return 0;
}

static int timing(void) {
    uint64_t plan_wall[5], plan_tsc[5], engine_wall[5], engine_tsc[5];
    for (unsigned i = 0; i < 5; ++i) {
        asian_meta_affine_plan_t *plan = NULL;
        uint64_t w0 = now_ns(), t0 = tsc_begin();
        if (asian_meta_affine_plan_create(&plan) != 0)
            return 1;
        uint64_t t1 = tsc_end(), w1 = now_ns();
        plan_wall[i] = w1-w0;
        plan_tsc[i] = t1-t0;
        asian_meta_affine_plan_destroy(plan);
        autocall_worstof_engine_t engine __attribute__((aligned(64)));
        w0 = now_ns(); t0 = tsc_begin();
        if (autocall_worstof_engine_create(&engine) != 0)
            return 1;
        t1 = tsc_end(); w1 = now_ns();
        engine_wall[i] = w1-w0;
        engine_tsc[i] = t1-t0;
        autocall_worstof_engine_destroy(&engine);
    }
    printf("initialization metric wall_ns tsc_ticks\n");
    printf("initialization full_affine_plan_create %llu %llu\n",
           (unsigned long long)median5(plan_wall),
           (unsigned long long)median5(plan_tsc));
    printf("initialization shared_engine_create %llu %llu\n",
           (unsigned long long)median5(engine_wall),
           (unsigned long long)median5(engine_tsc));
    puts("MEMORY candidate=PREPARED_CORRELATED_CARRIER payload_bytes=81920 "
         "candidate_warm_bytes=83328 residency=L2_STREAMED_NOT_L1");
    puts("MEMORY candidate=INLINE_CORRELATED_EVOLUTION payload_bytes=0 "
         "signed_z_bytes=32768 route_control_bytes=1920 "
         "candidate_warm_bytes=35392 residency=BELOW_40_KiB");
    if (parent_single_asset_context() != 0)
        return 1;

    static const char *metric_name[METRIC_COUNT] = {
        "market_prepare", "request_prepare", "prepared", "reused", "fresh"};
    static const char *candidate_name[2] = {
        "PREPARED_CORRELATED_CARRIER", "INLINE_CORRELATED_EVOLUTION"};
    static const char *cache_name[2] = {"candidate_warm", "pressure_32KiB"};
    sample_t all[5][2][METRIC_COUNT][2];
    const autocall_worstof_case_t *cases = autocall_worstof_cases();
    puts("contract candidate lifecycle cache wall_ns tsc_ticks "
         "valuations_per_sec path_observation_updates_per_sec identity");
    for (unsigned ordinal = 0; ordinal < 5; ++ordinal) {
        const int index = autocall_worstof_native_case_index(ordinal);
        fixture_t f;
        if (fixture_create(&f, &cases[index]) != 0)
            return 1;
        for (unsigned cache = 0; cache < 2; ++cache) {
            for (unsigned metric = 0; metric < METRIC_COUNT; ++metric) {
                sample_t result[2];
                measure_pair(&f, (enum metric)metric,
                             (enum cache_mode)cache, result);
                for (unsigned candidate = 0; candidate < 2; ++candidate) {
                    all[ordinal][cache][metric][candidate] = result[candidate];
                    const double vps = 1e9/(double)result[candidate].wall;
                    printf("%s %s %s %s %llu %llu %.6f %.6f PASS\n",
                           cases[index].name, candidate_name[candidate],
                           metric_name[metric], cache_name[cache],
                           (unsigned long long)result[candidate].wall,
                           (unsigned long long)result[candidate].tsc,
                           vps, vps*4096.0*3.0);
                }
            }
        }
        fixture_destroy(&f);
    }

    int candidate_pass[2] = {1, 1};
    double prepared_median[2] = {0, 0};
    double prepared_tsc_median[2] = {0, 0};
    double fresh_median[2] = {0, 0};
    double fresh_tsc_median[2] = {0, 0};
    for (unsigned candidate = 0; candidate < 2; ++candidate) {
        uint64_t prepared_wall[5], prepared_tsc[5], fresh_wall[5], fresh_tsc[5];
        uint64_t pressure_ratio_prepared[5], pressure_ratio_reuse[5];
        uint64_t pressure_ratio_fresh[5];
        uint64_t prepared_worst = 0, fresh_worst = 0;
        for (unsigned i = 0; i < 5; ++i) {
            prepared_wall[i] = all[i][0][PREPARED_PRICE][candidate].wall;
            prepared_tsc[i] = all[i][0][PREPARED_PRICE][candidate].tsc;
            fresh_wall[i] = all[i][0][FRESH_TOTAL][candidate].wall;
            fresh_tsc[i] = all[i][0][FRESH_TOTAL][candidate].tsc;
            if (prepared_wall[i] > prepared_worst) prepared_worst = prepared_wall[i];
            if (fresh_wall[i] > fresh_worst) fresh_worst = fresh_wall[i];
            pressure_ratio_prepared[i] = (uint64_t)(1000000.0*
                all[i][1][PREPARED_PRICE][candidate].wall/
                all[i][0][PREPARED_PRICE][candidate].wall);
            pressure_ratio_reuse[i] = (uint64_t)(1000000.0*
                all[i][1][REUSE_TOTAL][candidate].wall/
                all[i][0][REUSE_TOTAL][candidate].wall);
            pressure_ratio_fresh[i] = (uint64_t)(1000000.0*
                all[i][1][FRESH_TOTAL][candidate].wall/
                all[i][0][FRESH_TOTAL][candidate].wall);
        }
        const uint64_t prepared_med = median5(prepared_wall);
        const uint64_t prepared_tsc_med = median5(prepared_tsc);
        const uint64_t fresh_med = median5(fresh_wall);
        const uint64_t fresh_tsc_med = median5(fresh_tsc);
        const double pressure_prepared = median5(pressure_ratio_prepared)/1e6;
        const double pressure_reuse = median5(pressure_ratio_reuse)/1e6;
        const double pressure_fresh = median5(pressure_ratio_fresh)/1e6;
        prepared_median[candidate] = prepared_med;
        prepared_tsc_median[candidate] = prepared_tsc_med;
        fresh_median[candidate] = fresh_med;
        fresh_tsc_median[candidate] = fresh_tsc_med;
        candidate_pass[candidate] = prepared_med <= 12000u &&
            prepared_worst <= 15000u && fresh_med <= 20000u &&
            fresh_worst <= 25000u && pressure_prepared <= 1.25 &&
            pressure_reuse <= 1.25 && pressure_fresh <= 1.25;
        printf("PERFORMANCE_SUMMARY candidate=%s prepared_median_ns=%llu "
               "prepared_worst_ns=%llu prepared_median_tsc=%llu "
               "fresh_median_ns=%llu fresh_worst_ns=%llu fresh_median_tsc=%llu "
               "pressure_prepared_ratio=%.6f pressure_reuse_ratio=%.6f "
               "pressure_fresh_ratio=%.6f decision=%s\n",
               candidate_name[candidate], (unsigned long long)prepared_med,
               (unsigned long long)prepared_worst,
               (unsigned long long)prepared_tsc_med,
               (unsigned long long)fresh_med,
               (unsigned long long)fresh_worst,
               (unsigned long long)fresh_tsc_med, pressure_prepared,
               pressure_reuse, pressure_fresh,
               candidate_pass[candidate] ? "PASS" : "FAIL");
    }
    const int same_ranking =
        ((prepared_median[0] < prepared_median[1]) ==
         (prepared_tsc_median[0] < prepared_tsc_median[1])) &&
        (fresh_median[0] < fresh_median[1]) ==
        (fresh_tsc_median[0] < fresh_tsc_median[1]);
    const unsigned prepared_winner =
        prepared_median[0] < prepared_median[1] ? 0u : 1u;
    const unsigned fresh_winner = fresh_median[0] < fresh_median[1] ? 0u : 1u;
    printf("selection wall_tsc_same_architecture=%s accuracy=FAIL "
           "fixed_4096_panel=NOT_QUALIFIED prepared_winner=%s fresh_winner=%s "
           "split_winner=%s eligible_prepared=%s eligible_inline=%s\n",
           same_ranking ? "YES" : "NO", candidate_name[prepared_winner],
           candidate_name[fresh_winner],
           prepared_winner != fresh_winner ? "YES" : "NO",
           candidate_pass[0] ? "YES" : "NO",
           candidate_pass[1] ? "YES" : "NO");
    puts("TWO_ASSET_WORSTOF_ARCHITECTURE_RESEARCH_REQUIRED");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--check") == 0)
        return check_only();
    if (argc == 4 && strcmp(argv[1], "--native-check") == 0 &&
        strcmp(argv[2], "--cpu") == 0) {
        const unsigned cpu = (unsigned)strtoul(argv[3], NULL, 10);
        if (cpu != 0u || !sapphire_rapids()) {
            fprintf(stderr, "CPU 0 on family-6/model-143 Sapphire Rapids required\n");
            return 2;
        }
        return pin_cpu(cpu) == 0 ? check_only() : 2;
    }
    if (argc == 4 && strcmp(argv[1], "--timing") == 0 &&
        strcmp(argv[2], "--cpu") == 0) {
        const unsigned cpu = (unsigned)strtoul(argv[3], NULL, 10);
        if (cpu != 0u || !sapphire_rapids() || pin_cpu(cpu) != 0) {
            fprintf(stderr, "CPU 0 on family-6/model-143 Sapphire Rapids required\n");
            return 2;
        }
        return timing();
    }
    fprintf(stderr, "usage: %s --check | --native-check --cpu 0 | --timing --cpu 0\n",
            argv[0]);
    return 2;
}
