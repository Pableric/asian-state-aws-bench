#include "asian_arithmetic_pricer.h"
#include "private/asian_arithmetic_pricer_test.h"
#include "private/asian_genuine_arithmetic_growth_only_strip_adapter.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static asian_arithmetic_selected_path_t expected_path(
    uint32_t k, asian_arithmetic_workload_t workload)
{
    if ((k == 1u && workload == ASIAN_ARITHMETIC_PRICE) ||
        (k == 3u && workload == ASIAN_ARITHMETIC_PRICE_DELTA) || k >= 5u)
        return ASIAN_ARITHMETIC_SELECTED_STAGE1;
    return ASIAN_ARITHMETIC_SELECTED_IMMEDIATE;
}

static void make_strikes(float strikes[32], uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        const int offset = (int)((i * 11u + 3u) % count) - (int)(count / 2u);
        strikes[i] = 100.0f + (float)offset * 0.75f;
    }
}

static int run_case(const asian_arithmetic_route_plan_t *plan,
                    uint32_t n, uint32_t k,
                    asian_arithmetic_workload_t workload,
                    uint32_t completed)
{
    float strikes[32];
    make_strikes(strikes, k);
    asian_arithmetic_request_t request = {
        .s0 = 100.0,
        .rate = 0.03,
        .dividend_yield = 0.0,
        .sigma = 0.20,
        .maturity = 1.0,
        .future_fixings = n,
        .completed_fixings = completed,
        .completed_arithmetic_sum = completed == 0u ? 0.0 : 99.25 * completed,
        .completed_log_sum = completed == 0u ? 0.0 : 4.59 * completed,
        .strikes = strikes,
        .strike_count = k,
        .workload = workload,
    };
    asian_arithmetic_prepared_t *prepared = NULL;
    if (asian_arithmetic_prepare(plan, &request, &prepared) !=
        ASIAN_ARITHMETIC_OK)
        return -1;

    asian_arithmetic_result_t production;
    if (asian_arithmetic_price_prepared(prepared, &production) !=
        ASIAN_ARITHMETIC_OK || production.strike_count != k ||
        production.selected_path != expected_path(k, workload)) {
        asian_arithmetic_prepared_destroy(prepared);
        return -1;
    }

    asian_genuine_arithmetic_fused_source_exp_context_t *fused;
    asian_genuine_arithmetic_growth_only_context_t *growth;
    asian_genuine_strip_context_t *strip;
    float *q;
    if (asian_arithmetic_test_views(prepared, &fused, &growth, &strip, &q) != 0) {
        asian_arithmetic_prepared_destroy(prepared);
        return -1;
    }
    asian_genuine_strip_output_t reference __attribute__((aligned(64)));
    memset(&reference, 0, sizeof(reference));
    asian_genuine_arithmetic_fused_source_exp_diag(fused);
    asian_genuine_arithmetic_growth_only_q_diag(growth);
    if (asian_genuine_arithmetic_growth_only_strip_consume_padded(
          q, q, strip, k, workload == ASIAN_ARITHMETIC_PRICE_DELTA,
          &reference) != 0) {
        asian_arithmetic_prepared_destroy(prepared);
        return -1;
    }
    for (uint32_t i = 0; i < k; ++i) {
        if (memcmp(&production.values[i], &reference.values[i],
                   sizeof(production.values[i])) != 0) {
            fprintf(stderr, "identity N=%u K=%u workload=%u strike=%u\n",
                    n, k, (unsigned)workload, i);
            fprintf(stderr, "production %.17g %.17g %.17g %.17g\n",
                    production.values[i].call_price,
                    production.values[i].put_price,
                    production.values[i].call_delta,
                    production.values[i].put_delta);
            fprintf(stderr, "reference  %.17g %.17g %.17g %.17g\n",
                    reference.values[i].call_price,
                    reference.values[i].put_price,
                    reference.values[i].call_delta,
                    reference.values[i].put_delta);
            asian_arithmetic_prepared_destroy(prepared);
            return -1;
        }
    }

    asian_arithmetic_result_t repeated;
    if (asian_arithmetic_price_prepared(prepared, &repeated) !=
          ASIAN_ARITHMETIC_OK ||
        memcmp(&production, &repeated, sizeof(production)) != 0) {
        asian_arithmetic_prepared_destroy(prepared);
        return -1;
    }
    asian_arithmetic_prepared_destroy(prepared);
    return 0;
}

static int negative_tests(const asian_arithmetic_route_plan_t *plan)
{
    float strike = 100.0f;
    asian_arithmetic_request_t request = {
        .s0 = 100.0, .rate = 0.03, .sigma = 0.20, .maturity = 1.0,
        .future_fixings = 16u, .strikes = &strike, .strike_count = 1u,
        .workload = ASIAN_ARITHMETIC_PRICE,
    };
    asian_arithmetic_prepared_t *prepared = NULL;
    if (asian_arithmetic_prepare(NULL, &request, &prepared) ==
        ASIAN_ARITHMETIC_OK)
        return -1;
    request.future_fixings = 1u;
    if (asian_arithmetic_prepare(plan, &request, &prepared) ==
        ASIAN_ARITHMETIC_OK)
        return -1;
    request.future_fixings = 257u;
    if (asian_arithmetic_prepare(plan, &request, &prepared) ==
        ASIAN_ARITHMETIC_OK)
        return -1;
    request.future_fixings = 16u;
    request.sigma = 0.0;
    if (asian_arithmetic_prepare(plan, &request, &prepared) ==
        ASIAN_ARITHMETIC_OK)
        return -1;
    request.sigma = 0.20;
    request.strike_count = 33u;
    if (asian_arithmetic_prepare(plan, &request, &prepared) ==
        ASIAN_ARITHMETIC_OK)
        return -1;
    return 0;
}

int main(void)
{
    asian_arithmetic_route_plan_t *plan = NULL;
    if (asian_arithmetic_route_plan_create(&plan) != ASIAN_ARITHMETIC_OK ||
        plan == NULL || negative_tests(plan) != 0) {
        fprintf(stderr, "production preparation failed\n");
        return 1;
    }

    static const uint32_t n_values[] = {2u,16u,32u,64u,128u,256u};
    for (uint32_t ni = 0; ni < sizeof(n_values)/sizeof(n_values[0]); ++ni) {
        for (uint32_t k = 1u; k <= 32u; ++k) {
            for (uint32_t workload = 0; workload < 2u; ++workload) {
                if (run_case(plan, n_values[ni], k,
                      (asian_arithmetic_workload_t)workload, 0u) != 0) {
                    asian_arithmetic_route_plan_destroy(plan);
                    return 1;
                }
            }
        }
    }
    static const uint32_t completed[] = {1u,4u,8u};
    for (uint32_t i = 0; i < sizeof(completed)/sizeof(completed[0]); ++i)
        for (uint32_t workload = 0; workload < 2u; ++workload)
            if (run_case(plan, 16u, 4u,
                  (asian_arithmetic_workload_t)workload, completed[i]) != 0) {
                asian_arithmetic_route_plan_destroy(plan);
                return 1;
            }

    asian_arithmetic_route_plan_destroy(plan);
    puts("asian_arithmetic_public_api PASS N=2/16/32/64/128/256 K=1..32");
    return 0;
}
