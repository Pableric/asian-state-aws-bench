#define main asian_accuracy_parent_growth_only_test_main
#include "test_asian_genuine_arithmetic_growth_only.c"
#undef main

#include "private/asian_quantlib_accuracy_cases.h"

typedef struct {
    double plain_call;
    double plain_put;
    double geocv_call;
    double geocv_put;
} ours_value_t;

static void print_published_contract(const asian_accuracy_published_case_t *c)
{
    printf("id=%s first=%u n=%u basis=%u first_tick=%u step_tick=11 "
           "s0=%a strike=%a q=%a r=%a sigma=%a published=%a",
           c->id, c->first_twelfths, c->fixings,
           asian_accuracy_schedule_basis(c->fixings),
           asian_accuracy_schedule_first_tick(c), c->s0, c->strike,
           c->dividend_yield, c->rate, c->sigma, c->published_price);
}

static int prepare_arbitrary_strip(
    asian_genuine_strip_context_t *strip,
    double s0, double strike, double rate, double dividend, double sigma,
    double maturity, uint32_t future, uint32_t completed,
    double initial_q, double past_log_sum)
{
    const float strike_f = (float)strike;
    uint32_t padded = 0;
    if (asian_genuine_arithmetic_growth_only_strip_prepare_padded(
            strip, s0, rate, dividend, sigma, maturity, future, completed,
            initial_q, past_log_sum, &strike_f, 1u, &padded) != 0 ||
        padded != 1u)
        return -1;

    asian_geometric_cv_context_t geometric __attribute__((aligned(64)));
    const int direct_call =
        (strip->strikes[0].flags & ASIAN_GENUINE_STRIP_DIRECT_CALL) != 0u;
    if (asian_geometric_cv_prepare(&geometric, s0, strike, rate, dividend,
            sigma, maturity, future, completed, initial_q, past_log_sum,
            direct_call ? ASIAN_GEOMETRIC_CALL : ASIAN_GEOMETRIC_PUT) != 0)
        return -1;
    strip->strikes[0].geometric_price_exact_direct = geometric.geometric_exact;
    return 0;
}

static int run_ours_case(fixture_t *fixture,
                         double s0, double strike,
                         double rate, double dividend, double sigma,
                         double maturity, uint32_t future,
                         uint32_t completed, double initial_q,
                         double past_log_sum, ours_value_t *value)
{
    if (future == 0u || future > MAX_N || completed > MAX_N - future)
        return -1;

    const market_t market = {rate, dividend, sigma, maturity};
    asian_genuine_fixed_block_source_context_t source
        __attribute__((aligned(64)));
    if (future == 1u) {
        source_context(&source, &market, future);
        asian_genuine_fixed_block_signed_z_one_fma_source_diag(&source,
                                                               fixture->x);
        asian_vector_exp_range_reduced_array_diag(fixture->x,
                                                   fixture->growth);
        asian_vector_exp_range_reduced_array_diag(fixture->x + PATHS,
                                                   fixture->growth + PATHS);
    } else if (produce_frontends(fixture, &market, future, &source) != 0) {
        return -1;
    }

    asian_genuine_route_t *routes = a64(MAX_N * sizeof(*routes));
    float *geometric_paths = a64(PATHS * sizeof(*geometric_paths));
    asian_genuine_strip_context_t *strip = a64(sizeof(*strip));
    asian_genuine_strip_output_t *plain = a64(sizeof(*plain));
    asian_genuine_strip_output_t *materialized = a64(sizeof(*materialized));
    asian_genuine_strip_output_t *geocv = a64(sizeof(*geocv));
    if (routes == NULL || geometric_paths == NULL || strip == NULL ||
        plain == NULL || materialized == NULL || geocv == NULL) {
        free(geocv); free(materialized); free(plain); free(strip);
        free(geometric_paths); free(routes);
        return -1;
    }
    memcpy(routes, fixture->routes, MAX_N * sizeof(*routes));
    const uint32_t total = future + completed;
    for (uint32_t fixing = 0; fixing < future; ++fixing) {
        const float weight = (float)(future - fixing) / (float)total;
        memcpy(&routes[fixing].weight_bits, &weight, sizeof(weight));
    }

    memset(fixture->dual, 0, sizeof(*fixture->dual));
    for (uint32_t path = 0; path < PATHS; ++path)
        fixture->dual->s[path] = (float)s0;
    asian_genuine_sql_dual_control_diag(routes, future, fixture->dual);

    if (future == 1u) {
        for (uint32_t path = 0; path < PATHS; ++path)
            fixture->q[path] = rounded_mul((float)s0, fixture->growth[path]);
    } else {
        if (asian_genuine_arithmetic_growth_only_prepare(
                fixture->context, routes, future, (float)s0, fixture->growth,
                2u * PATHS * sizeof(float), fixture->q,
                PATHS * sizeof(float)) !=
            ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_OK) {
            free(geocv); free(materialized); free(plain); free(strip);
            free(geometric_paths); free(routes);
            return -1;
        }
        asian_genuine_arithmetic_growth_only_q_diag(fixture->context);
    }
    if (memcmp(fixture->q, fixture->dual->q,
               PATHS * sizeof(float)) != 0) {
        fprintf(stderr, "materialized Q mismatch future=%u completed=%u\n",
                future, completed);
        free(geocv); free(materialized); free(plain); free(strip);
        free(geometric_paths); free(routes);
        return -1;
    }

    if (prepare_arbitrary_strip(strip, s0, strike, rate, dividend, sigma,
            maturity, future, completed, initial_q, past_log_sum) != 0 ||
        asian_genuine_strip_exp_preflight(strip, fixture->dual->l,
                                          NULL, NULL) != 0) {
        free(geocv); free(materialized); free(plain); free(strip);
        free(geometric_paths); free(routes);
        return -1;
    }
    asian_genuine_strip_l_to_g_diag(fixture->dual->l, strip, geometric_paths);
    if (asian_genuine_arithmetic_growth_only_strip_consume_padded(
            fixture->q, fixture->g_unused, strip, 1u, 0, plain) != 0 ||
        asian_genuine_arithmetic_growth_only_strip_consume_padded(
            fixture->dual->q, fixture->g_unused, strip, 1u, 0,
            materialized) != 0 ||
        memcmp(plain, materialized, sizeof(*plain)) != 0 ||
        asian_genuine_strip_price_diag(
            fixture->dual->q, geometric_paths, strip,
            ASIAN_GENUINE_STRIP_GEOMETRIC_CV, 4u, geocv) != 0) {
        fprintf(stderr, "payoff bridge mismatch future=%u completed=%u\n",
                future, completed);
        free(geocv); free(materialized); free(plain); free(strip);
        free(geometric_paths); free(routes);
        return -1;
    }

    value->plain_call = plain->values[0].call_price;
    value->plain_put = plain->values[0].put_price;
    value->geocv_call = geocv->values[0].call_price;
    value->geocv_put = geocv->values[0].put_price;
    free(geocv); free(materialized); free(plain); free(strip);
    free(geometric_paths); free(routes);
    return 0;
}

static int analytic_stage(void)
{
    asian_geometric_cv_context_t call __attribute__((aligned(64)));
    asian_geometric_cv_context_t put __attribute__((aligned(64)));
    if (asian_geometric_cv_prepare(&call, 100.0, 100.0, 0.03, 0.0,
            0.20, 1.0, 5u, 0u, 0.0, 0.0,
            ASIAN_GEOMETRIC_CALL) != 0 ||
        asian_geometric_cv_prepare(&put, 100.0, 100.0, 0.03, 0.0,
            0.20, 1.0, 5u, 0u, 0.0, 0.0,
            ASIAN_GEOMETRIC_PUT) != 0 ||
        memcmp(&call.log_mean, &put.log_mean, sizeof(double)) != 0 ||
        memcmp(&call.log_variance, &put.log_variance, sizeof(double)) != 0)
        return -1;
    printf("OUR_ANALYTIC option=call basis=5 first_tick=1 step_tick=1 n=5 "
           "s0=%a strike=%a q=%a r=%a sigma=%a price=%a mean=%a variance=%a\n",
           100.0, 100.0, 0.0, 0.03, 0.20, call.geometric_exact,
           call.log_mean, call.log_variance);
    printf("OUR_ANALYTIC option=put basis=5 first_tick=1 step_tick=1 n=5 "
           "s0=%a strike=%a q=%a r=%a sigma=%a price=%a mean=%a variance=%a\n",
           100.0, 100.0, 0.0, 0.03, 0.20, put.geometric_exact,
           put.log_mean, put.log_variance);
    return 0;
}

static int arithmetic_stage(void)
{
    fixture_t fixture;
    if (fixture_init(&fixture) != 0) {
        fprintf(stderr, "accuracy fixture initialization failed\n");
        return -1;
    }
    for (uint32_t i = 0; i < ASIAN_ACCURACY_PUBLISHED_CASES; ++i) {
        const asian_accuracy_published_case_t *c =
            &asian_accuracy_published_cases[i];
        printf("OUR_PUBLISHED ");
        print_published_contract(c);
        if (!asian_accuracy_schedule_supported(c)) {
            printf(" status=SKIPPED reason=%s\n",
                   asian_accuracy_schedule_reason(c));
            continue;
        }
        uint32_t future;
        uint32_t completed;
        double maturity;
        double initial_q;
        double past_log_sum;
        if (c->first_twelfths == 0u) {
            future = c->fixings - 1u;
            completed = 1u;
            maturity = 11.0 / 12.0;
            initial_q = c->s0;
            past_log_sum = log(c->s0);
        } else {
            future = c->fixings;
            completed = 0u;
            maturity = 1.0;
            initial_q = 0.0;
            past_log_sum = 0.0;
        }
        ours_value_t value;
        if (run_ours_case(&fixture, c->s0, c->strike, c->rate,
                c->dividend_yield, c->sigma, maturity, future, completed,
                initial_q, past_log_sum, &value) != 0) {
            fprintf(stderr, "ours published case failed id=%s\n", c->id);
            fixture_release(&fixture);
            return -1;
        }
        printf(" status=SUPPORTED future=%u completed=%u maturity=%a "
               "plain_call=%a plain_put=%a geocv_call=%a geocv_put=%a "
               "q_identity=YES\n",
               future, completed, maturity, value.plain_call,
               value.plain_put, value.geocv_call, value.geocv_put);
    }

    ours_value_t n64;
    if (run_ours_case(&fixture, 100.0, 100.0, 0.03, 0.0, 0.20,
            1.0, 64u, 0u, 0.0, 0.0, &n64) != 0) {
        fprintf(stderr, "ours N64 case failed\n");
        fixture_release(&fixture);
        return -1;
    }
    printf("OUR_N64 basis=64 first_tick=1 step_tick=1 n=64 "
           "s0=%a strike=%a q=%a r=%a sigma=%a "
           "plain_call=%a plain_put=%a geocv_call=%a geocv_put=%a "
           "q_identity=YES\n",
           100.0, 100.0, 0.0, 0.03, 0.20,
           n64.plain_call, n64.plain_put,
           n64.geocv_call, n64.geocv_put);
    fixture_release(&fixture);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "--stage") != 0) {
        fprintf(stderr, "usage: %s --stage analytic|arithmetic\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "analytic") == 0)
        return analytic_stage() == 0 ? 0 : 1;
    if (strcmp(argv[2], "arithmetic") == 0)
        return arithmetic_stage() == 0 ? 0 : 1;
    fprintf(stderr, "unknown stage: %s\n", argv[2]);
    return 2;
}
