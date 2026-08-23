#ifndef ASIAN_ARITHMETIC_PRICER_H
#define ASIAN_ARITHMETIC_PRICER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ASIAN_ARITHMETIC_PATHS = 4096,
    ASIAN_ARITHMETIC_MIN_FIXINGS = 2,
    ASIAN_ARITHMETIC_MAX_FIXINGS = 256,
    ASIAN_ARITHMETIC_MAX_STRIKES = 32,
};

typedef enum {
    ASIAN_ARITHMETIC_PRICE = 0,
    ASIAN_ARITHMETIC_PRICE_DELTA = 1,
} asian_arithmetic_workload_t;

typedef enum {
    ASIAN_ARITHMETIC_SELECTED_STAGE1 = 1,
    ASIAN_ARITHMETIC_SELECTED_IMMEDIATE = 2,
} asian_arithmetic_selected_path_t;

typedef enum {
    ASIAN_ARITHMETIC_OK = 0,
    ASIAN_ARITHMETIC_INVALID_ARGUMENT = -1,
    ASIAN_ARITHMETIC_UNSUPPORTED_CONTRACT = -2,
    ASIAN_ARITHMETIC_PREPARATION_FAILED = -3,
    ASIAN_ARITHMETIC_EXECUTION_FAILED = -4,
} asian_arithmetic_status_t;

typedef struct {
    double s0;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    uint32_t future_fixings;
    uint32_t completed_fixings;
    double completed_arithmetic_sum;
    double completed_log_sum;
    const float *strikes;
    uint32_t strike_count;
    asian_arithmetic_workload_t workload;
} asian_arithmetic_request_t;

typedef struct {
    double call_price;
    double put_price;
    double call_delta;
    double put_delta;
} asian_arithmetic_value_t;

typedef struct {
    uint32_t strike_count;
    asian_arithmetic_selected_path_t selected_path;
    asian_arithmetic_value_t values[ASIAN_ARITHMETIC_MAX_STRIKES];
} asian_arithmetic_result_t;

/* Immutable and thread-safe after construction; it must outlive its contracts. */
typedef struct asian_arithmetic_route_plan asian_arithmetic_route_plan_t;

/*
 * A prepared contract owns mutable path workspace.  Use one prepared contract
 * per concurrent caller.  Repeated calls are allocation-free.
 */
typedef struct asian_arithmetic_prepared asian_arithmetic_prepared_t;

int asian_arithmetic_route_plan_create(asian_arithmetic_route_plan_t **out);
void asian_arithmetic_route_plan_destroy(asian_arithmetic_route_plan_t *plan);

int asian_arithmetic_prepare(
    const asian_arithmetic_route_plan_t *plan,
    const asian_arithmetic_request_t *request,
    asian_arithmetic_prepared_t **out);

int asian_arithmetic_price_prepared(
    asian_arithmetic_prepared_t *prepared,
    asian_arithmetic_result_t *out);

void asian_arithmetic_prepared_destroy(asian_arithmetic_prepared_t *prepared);

#ifdef __cplusplus
}
#endif

#endif
