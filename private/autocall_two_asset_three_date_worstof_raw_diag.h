#ifndef AUTOCALL_TWO_ASSET_THREE_DATE_WORSTOF_RAW_DIAG_H
#define AUTOCALL_TWO_ASSET_THREE_DATE_WORSTOF_RAW_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_genuine_arithmetic_fused_source_exp_diag.h"
#include "asian_meta_direction_affine_route.h"

enum {
    AUTOCALL_WORSTOF_PATHS = 4096,
    AUTOCALL_WORSTOF_DONOR_VALUES = 8192,
    AUTOCALL_WORSTOF_DATES = 3,
    AUTOCALL_WORSTOF_DIMENSIONS = 6,
};

enum autocall_worstof_status {
    AUTOCALL_WORSTOF_OK = 0,
    AUTOCALL_WORSTOF_INVALID = -1,
    AUTOCALL_WORSTOF_UNSUPPORTED = -2,
};

#define AUTOCALL_WORSTOF_ENGINE_MAGIC UINT32_C(0x45573241)
#define AUTOCALL_WORSTOF_PREPARED_MARKET_MAGIC UINT32_C(0x4d503241)
#define AUTOCALL_WORSTOF_INLINE_MARKET_MAGIC UINT32_C(0x4d493241)
#define AUTOCALL_WORSTOF_PREPARED_REQUEST_MAGIC UINT32_C(0x52503241)
#define AUTOCALL_WORSTOF_INLINE_REQUEST_MAGIC UINT32_C(0x52493241)
#define AUTOCALL_WORSTOF_OUTPUT_MAGIC UINT32_C(0x4f573241)
#define AUTOCALL_WORSTOF_B_PRODUCER_MAGIC UINT32_C(0x42503241)

typedef struct {
    double rate;
    double maturity;
    double dividend_a;
    double sigma_a;
    double dividend_b;
    double sigma_b;
    double rho;
} autocall_worstof_market_input_t;

typedef struct {
    double spot_a;
    double spot_b;
    double notional;
    double call_barrier[AUTOCALL_WORSTOF_DATES];
    double coupon_barrier[AUTOCALL_WORSTOF_DATES];
    double coupon_cashflow[AUTOCALL_WORSTOF_DATES];
    double call_redemption[AUTOCALL_WORSTOF_DATES];
    double protection_barrier;
    double coupon_payment_time[AUTOCALL_WORSTOF_DATES];
    double call_payment_time[AUTOCALL_WORSTOF_DATES];
    double terminal_payment_time;
} autocall_worstof_contract_input_t;

typedef struct {
    float coupon_a;
    float coupon_b;
    float call_a;
    float call_b;
    float discounted_coupon;
    float discounted_call;
    uint32_t reserved[2];
} autocall_worstof_date_record_t;

_Static_assert(sizeof(autocall_worstof_date_record_t) == 32,
               "one natural worst-of date record");

typedef struct __attribute__((aligned(64))) {
    const float *asset_a_d1;
    const asian_meta_affine_route_t *asset_a_d3;
    const asian_meta_affine_route_t *asset_a_d5;
    const float *asset_b_dates;
    autocall_worstof_date_record_t date[AUTOCALL_WORSTOF_DATES];
    float spot_a;
    float spot_b;
    float inverse_spot_a;
    float inverse_spot_b;
    float protection_a;
    float protection_b;
    float discounted_terminal;
    uint32_t reserved0;
    double inverse_paths;
    uint32_t magic;
    uint8_t reserved[84];
} autocall_worstof_prepared_leaf_context_t;

_Static_assert(offsetof(autocall_worstof_prepared_leaf_context_t, date) == 32,
               "prepared leaf date offset");
_Static_assert(offsetof(autocall_worstof_prepared_leaf_context_t, spot_a) == 128,
               "prepared leaf scalar offset");
_Static_assert(sizeof(autocall_worstof_prepared_leaf_context_t) == 256,
               "prepared leaf context is four lines");

typedef struct __attribute__((aligned(64))) {
    const float *direct_d1;
    const asian_meta_affine_route_t *routes_d2;
    autocall_worstof_date_record_t date[AUTOCALL_WORSTOF_DATES];
    float spot_a;
    float spot_b;
    float inverse_spot_a;
    float inverse_spot_b;
    float protection_a;
    float protection_b;
    float discounted_terminal;
    float drift_a;
    float diffusion_a;
    float drift_b;
    float diffusion_b;
    float rho;
    float cholesky;
    uint32_t reserved0;
    double inverse_paths;
    uint32_t magic;
    uint8_t reserved[68];
} autocall_worstof_inline_leaf_context_t;

_Static_assert(offsetof(autocall_worstof_inline_leaf_context_t, date) == 16,
               "inline leaf date offset");
_Static_assert(offsetof(autocall_worstof_inline_leaf_context_t, spot_a) == 112,
               "inline leaf scalar offset");
_Static_assert(sizeof(autocall_worstof_inline_leaf_context_t) == 256,
               "inline leaf context is four lines");

typedef struct __attribute__((aligned(64))) {
    float asset_a_growth[AUTOCALL_WORSTOF_DONOR_VALUES];
    float asset_b_growth[AUTOCALL_WORSTOF_DATES][AUTOCALL_WORSTOF_PATHS];
    asian_genuine_arithmetic_fused_source_exp_context_t asset_a_fused;
    autocall_worstof_market_input_t input;
    float drift_a;
    float diffusion_a;
    float drift_b;
    float diffusion_b;
    float rho;
    float cholesky;
    uint64_t generation;
    uint32_t magic;
    uint8_t reserved[28];
} autocall_worstof_prepared_market_t;

_Static_assert(sizeof(autocall_worstof_prepared_market_t) == 82112,
               "81920-byte payload plus three metadata lines");

typedef struct __attribute__((aligned(64))) {
    autocall_worstof_market_input_t input;
    float drift_a;
    float diffusion_a;
    float drift_b;
    float diffusion_b;
    float rho;
    float cholesky;
    uint64_t generation;
    uint32_t magic;
    uint8_t reserved[28];
} autocall_worstof_inline_market_t;

_Static_assert(sizeof(autocall_worstof_inline_market_t) == 128,
               "inline market occupies two lines");

typedef struct __attribute__((aligned(64))) {
    autocall_worstof_prepared_leaf_context_t leaf;
    asian_meta_affine_route_t routes[AUTOCALL_WORSTOF_DIMENSIONS];
    const autocall_worstof_prepared_market_t *market;
    uint64_t market_generation;
    uint32_t magic;
    uint8_t reserved[44];
} autocall_worstof_prepared_request_t;

typedef struct __attribute__((aligned(64))) {
    autocall_worstof_inline_leaf_context_t leaf;
    asian_meta_affine_route_t routes[AUTOCALL_WORSTOF_DIMENSIONS];
    const autocall_worstof_inline_market_t *market;
    uint64_t market_generation;
    uint32_t magic;
    uint8_t reserved[44];
} autocall_worstof_inline_request_t;

_Static_assert(sizeof(autocall_worstof_prepared_request_t) == 512,
               "prepared request is eight lines");
_Static_assert(sizeof(autocall_worstof_inline_request_t) == 512,
               "inline request is eight lines");

typedef struct {
    double price;
    uint64_t market_generation;
    uint32_t magic;
    uint32_t identity;
} autocall_worstof_output_t;

typedef struct __attribute__((aligned(64))) {
    const float *direct_d1;
    const asian_meta_affine_route_t *routes_d2;
    float *output;
    float drift_b;
    float diffusion_b;
    float rho;
    float cholesky;
    uint32_t magic;
    uint8_t reserved[20];
} autocall_worstof_b_producer_context_t;

_Static_assert(sizeof(autocall_worstof_b_producer_context_t) == 64,
               "asset-B producer context is one line");

typedef union __attribute__((aligned(64))) {
    autocall_worstof_prepared_market_t prepared;
    autocall_worstof_inline_market_t inline_market;
} autocall_worstof_market_workspace_t;

typedef union __attribute__((aligned(64))) {
    autocall_worstof_prepared_request_t prepared;
    autocall_worstof_inline_request_t inline_request;
} autocall_worstof_request_workspace_t;

typedef struct __attribute__((aligned(64))) {
    autocall_worstof_market_workspace_t market;
    autocall_worstof_request_workspace_t request;
    asian_meta_affine_route_t source_routes[AUTOCALL_WORSTOF_DIMENSIONS];
} autocall_worstof_workspace_t;

_Static_assert(sizeof(autocall_worstof_workspace_t) == 82816,
               "maximum private market/request/source-route workspace");

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_plan_t *affine_plan;
    autocall_worstof_workspace_t *workspace;
    uint64_t next_generation;
    float signed_z_min;
    float signed_z_max;
    uint32_t magic;
    uint8_t reserved[28];
} autocall_worstof_engine_t;

_Static_assert(sizeof(autocall_worstof_engine_t) == 64,
               "engine handle is one line");

int autocall_worstof_engine_create(autocall_worstof_engine_t *engine);
void autocall_worstof_engine_destroy(autocall_worstof_engine_t *engine);
int autocall_worstof_prepared_market_prepare(
    autocall_worstof_engine_t *, const autocall_worstof_market_input_t *,
    autocall_worstof_prepared_market_t *);
int autocall_worstof_inline_market_prepare(
    autocall_worstof_engine_t *, const autocall_worstof_market_input_t *,
    autocall_worstof_inline_market_t *);
int autocall_worstof_prepared_request_prepare(
    const autocall_worstof_engine_t *,
    const autocall_worstof_prepared_market_t *,
    const autocall_worstof_contract_input_t *,
    autocall_worstof_prepared_request_t *);
int autocall_worstof_inline_request_prepare(
    const autocall_worstof_engine_t *,
    const autocall_worstof_inline_market_t *,
    const autocall_worstof_contract_input_t *,
    autocall_worstof_inline_request_t *);
int autocall_worstof_prepared_price(
    const autocall_worstof_prepared_request_t *, autocall_worstof_output_t *);
int autocall_worstof_inline_price(
    const autocall_worstof_inline_request_t *, autocall_worstof_output_t *);
int autocall_worstof_prepared_reuse_total(
    autocall_worstof_engine_t *, const autocall_worstof_prepared_market_t *,
    const autocall_worstof_contract_input_t *, autocall_worstof_output_t *);
int autocall_worstof_inline_reuse_total(
    autocall_worstof_engine_t *, const autocall_worstof_inline_market_t *,
    const autocall_worstof_contract_input_t *, autocall_worstof_output_t *);
int autocall_worstof_prepared_fresh_total(
    autocall_worstof_engine_t *, const autocall_worstof_market_input_t *,
    const autocall_worstof_contract_input_t *, autocall_worstof_output_t *);
int autocall_worstof_inline_fresh_total(
    autocall_worstof_engine_t *, const autocall_worstof_market_input_t *,
    const autocall_worstof_contract_input_t *, autocall_worstof_output_t *);

void autocall_worstof_prepare_asset_b_growth(
    const autocall_worstof_b_producer_context_t *);
double autocall_worstof_prepared_leaf(
    const autocall_worstof_prepared_leaf_context_t *);
double autocall_worstof_inline_leaf(
    const autocall_worstof_inline_leaf_context_t *);

#endif
