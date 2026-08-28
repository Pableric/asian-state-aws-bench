#ifndef AUTOCALL_SINGLE_ASSET_THREE_DATE_RAW_DIAG_H
#define AUTOCALL_SINGLE_ASSET_THREE_DATE_RAW_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_genuine_arithmetic_fused_source_exp_diag.h"
#include "asian_meta_direction_affine_route.h"

enum {
    AUTOCALL_3DATE_PATHS = 4096,
    AUTOCALL_3DATE_DONOR_VALUES = 8192,
    AUTOCALL_3DATE_DATES = 3,
};

enum autocall_3date_status {
    AUTOCALL_3DATE_OK = 0,
    AUTOCALL_3DATE_INVALID = -1,
    AUTOCALL_3DATE_UNSUPPORTED = -2,
};

#define AUTOCALL_3DATE_ENGINE_MAGIC UINT32_C(0x45434133)
#define AUTOCALL_3DATE_CARRIER_MAGIC UINT32_C(0x43434133)
#define AUTOCALL_3DATE_REQUEST_MAGIC UINT32_C(0x52434133)
#define AUTOCALL_3DATE_OUTPUT_MAGIC UINT32_C(0x4f434133)

typedef struct {
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
} autocall_3date_market_input_t;

typedef struct {
    double s0;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    double notional;
    double protection_barrier;
    double call_barrier[AUTOCALL_3DATE_DATES];
    double coupon_barrier[AUTOCALL_3DATE_DATES];
    double coupon_cashflow[AUTOCALL_3DATE_DATES];
    double call_redemption[AUTOCALL_3DATE_DATES];
    double coupon_payment_time[AUTOCALL_3DATE_DATES];
    double call_payment_time[AUTOCALL_3DATE_DATES];
    double terminal_payment_time;
} autocall_3date_request_input_t;

typedef struct {
    float coupon_barrier;
    float call_barrier;
    float discounted_coupon;
    float discounted_call_redemption;
} autocall_3date_date_record_t;

_Static_assert(sizeof(autocall_3date_date_record_t) == 16,
               "one natural four-float date record");

/* The three date records are contained inside this 128-byte leaf context. */
typedef struct __attribute__((aligned(64))) {
    const float *d1_growth;
    const asian_meta_affine_route_t *routes_d2;
    autocall_3date_date_record_t date[AUTOCALL_3DATE_DATES];
    float initial_spot;
    float inverse_initial_spot;
    float protection_barrier;
    float discounted_terminal_notional;
    double inverse_paths;
    uint32_t magic;
    uint8_t reserved[36];
} autocall_3date_leaf_context_t;

_Static_assert(offsetof(autocall_3date_leaf_context_t, date) == 16,
               "date records are in the leaf context");
_Static_assert(offsetof(autocall_3date_leaf_context_t, initial_spot) == 64,
               "natural second-line leaf scalars");
_Static_assert(sizeof(autocall_3date_leaf_context_t) == 128,
               "natural 128-byte leaf context");

typedef struct __attribute__((aligned(64))) {
    float growth[AUTOCALL_3DATE_DONOR_VALUES];
    asian_genuine_arithmetic_fused_source_exp_context_t fused;
    autocall_3date_market_input_t market;
    uint64_t generation;
    uint32_t magic;
    uint8_t reserved[20];
} autocall_3date_carrier_t;

_Static_assert(sizeof(autocall_3date_carrier_t) == 32896,
               "canonical growth carrier plus natural identity metadata");

typedef struct __attribute__((aligned(64))) {
    autocall_3date_leaf_context_t context;
    asian_meta_affine_route_t routes[AUTOCALL_3DATE_DATES];
    const autocall_3date_carrier_t *carrier;
    uint64_t carrier_generation;
    uint32_t magic;
    uint8_t reserved[12];
} autocall_3date_request_t;

_Static_assert(offsetof(autocall_3date_request_t, routes) == 128,
               "three route records follow the leaf context");
_Static_assert(offsetof(autocall_3date_request_t, carrier) == 224,
               "32-byte request identity tail");
_Static_assert(sizeof(autocall_3date_request_t) == 256,
               "natural aligned request remains 256 bytes");

typedef struct {
    double price;
    uint64_t carrier_generation;
    uint32_t magic;
    uint32_t reserved;
} autocall_3date_output_t;

typedef struct __attribute__((aligned(64))) autocall_3date_workspace {
    autocall_3date_carrier_t carrier;
    autocall_3date_request_t request;
} autocall_3date_workspace_t;

_Static_assert(sizeof(autocall_3date_workspace_t) == 33152,
               "persistent carrier and request workspace");

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_plan_t *affine_plan;
    autocall_3date_workspace_t *workspace;
    uint64_t next_generation;
    float signed_z_min;
    float signed_z_max;
    uint32_t magic;
    uint8_t reserved[28];
} autocall_3date_engine_t;

_Static_assert(sizeof(autocall_3date_engine_t) == 64,
               "one-line private engine handle");

int autocall_3date_engine_create(autocall_3date_engine_t *engine);
void autocall_3date_engine_destroy(autocall_3date_engine_t *engine);
int autocall_3date_market_prepare(autocall_3date_engine_t *engine,
    const autocall_3date_market_input_t *input,
    autocall_3date_carrier_t *carrier);
int autocall_3date_request_prepare(const autocall_3date_engine_t *engine,
    const autocall_3date_carrier_t *carrier,
    const autocall_3date_request_input_t *input,
    autocall_3date_request_t *request);
int autocall_3date_prepared_price(const autocall_3date_request_t *request,
    autocall_3date_output_t *output);
int autocall_3date_reuse_total(autocall_3date_engine_t *engine,
    const autocall_3date_carrier_t *carrier,
    const autocall_3date_request_input_t *input,
    autocall_3date_output_t *output);
int autocall_3date_fresh_total(autocall_3date_engine_t *engine,
    const autocall_3date_request_input_t *input,
    autocall_3date_output_t *output);

double autocall_3date_affine_price_leaf(
    const autocall_3date_leaf_context_t *context);
double autocall_3date_generic_price_leaf_test(
    const autocall_3date_leaf_context_t *context);

#endif
