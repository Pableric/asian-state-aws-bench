#ifndef AUTOCALL_SINGLE_ASSET_THREE_DATE_D1_NATIVE_DIAG_H
#define AUTOCALL_SINGLE_ASSET_THREE_DATE_D1_NATIVE_DIAG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define _Static_assert static_assert
#endif
#include "private/autocall_single_asset_three_date_raw_diag.h"
#include "private/autocall_single_asset_three_date_greeks_diag.h"
#ifdef __cplusplus
#undef _Static_assert
#endif

#ifdef __cplusplus
#define AUTOCALL_D1_NATIVE_STATIC_ASSERT static_assert
#else
#define AUTOCALL_D1_NATIVE_STATIC_ASSERT _Static_assert
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AUTOCALL_D1_NATIVE_LEGS = 5,
    AUTOCALL_D1_NATIVE_MIN_PATHS = 64,
    AUTOCALL_D1_NATIVE_MAX_PATHS = 4096,
};

enum autocall_d1_native_status {
    AUTOCALL_D1_NATIVE_OK = 0,
    AUTOCALL_D1_NATIVE_INVALID = -1,
    AUTOCALL_D1_NATIVE_UNSUPPORTED = -2,
    AUTOCALL_D1_NATIVE_DOMAIN = -3,
};

#define AUTOCALL_D1_NATIVE_ENGINE_MAGIC UINT32_C(0x454e3144)
#define AUTOCALL_D1_NATIVE_MARKET_MAGIC UINT32_C(0x4d4e3144)
#define AUTOCALL_D1_NATIVE_REQUEST_MAGIC UINT32_C(0x524e3144)
#define AUTOCALL_D1_NATIVE_OUTPUT_MAGIC UINT32_C(0x4f4e3144)

typedef struct {
    float call2_intercept;
    float coupon2_intercept;
    float call3_intercept;
    float coupon3_intercept;
    float protection_intercept;
    float date1_pv;
    float prior_threshold;
    float prior_survival_cdf;
    float residual_a;
    float downside_scale;
    uint32_t reserved[2];
} autocall_d1_native_leg_record_t;

AUTOCALL_D1_NATIVE_STATIC_ASSERT(sizeof(autocall_d1_native_leg_record_t) == 48,
               "five compact 48-byte conditional leg records");

typedef struct __attribute__((aligned(64))) {
    const float *direct_d1;
    const float *d2_donor;
    const asian_meta_dim_affine_ctx_t *d2_map;
    uint32_t end_bytes;
    uint32_t path_count;
    double inverse_paths;
    float discounted_coupon2;
    float discounted_call2;
    float discounted_coupon3;
    float discounted_call3;
    float discounted_terminal_notional;
    uint32_t magic;
    autocall_d1_native_leg_record_t leg[AUTOCALL_D1_NATIVE_LEGS];
    uint8_t reserved[16];
} autocall_d1_native_leaf_context_t;

AUTOCALL_D1_NATIVE_STATIC_ASSERT(offsetof(autocall_d1_native_leaf_context_t, leg) == 64,
               "conditional legs start on the second cache line");
AUTOCALL_D1_NATIVE_STATIC_ASSERT(sizeof(autocall_d1_native_leaf_context_t) == 320,
               "natural five-leg leaf context is five cache lines");

typedef struct {
    float sigma;
    float mu;
    float a;
    float inverse_a;
    double sigma_exact;
    uint8_t reserved[8];
} autocall_d1_native_market_leg_t;

AUTOCALL_D1_NATIVE_STATIC_ASSERT(sizeof(autocall_d1_native_market_leg_t) == 32,
               "one compact prepared market leg");

typedef struct __attribute__((aligned(64))) {
    autocall_3date_market_input_t input;
    autocall_3date_greek_legs_t volatility_legs;
    autocall_d1_native_market_leg_t leg[3];
    uint64_t generation;
    double requested_volatility_bump;
    uint32_t magic;
    uint32_t status;
    uint8_t reserved[24];
} autocall_d1_native_market_t;

AUTOCALL_D1_NATIVE_STATIC_ASSERT(sizeof(autocall_d1_native_market_t) == 256,
               "prepared conditional market is four lines");

typedef struct __attribute__((aligned(64))) {
    autocall_d1_native_leaf_context_t leaf;
    autocall_3date_greek_legs_t spot_legs;
    autocall_3date_greek_legs_t volatility_legs;
    const autocall_d1_native_market_t *market;
    uint64_t market_generation;
    uint32_t magic;
    uint32_t status;
    uint8_t reserved[8];
} autocall_d1_native_request_t;

AUTOCALL_D1_NATIVE_STATIC_ASSERT(sizeof(autocall_d1_native_request_t) == 512,
               "complete natural prepared request is eight lines");

typedef struct __attribute__((aligned(64))) {
    double price;
    double delta_per_spot_unit;
    double gamma_per_spot_unit_squared;
    double vega_per_unit_sigma;
    double vega_per_one_vol_point;
    double leg_price[AUTOCALL_D1_NATIVE_LEGS];
    float spot_minus, spot_zero, spot_plus;
    float sigma_minus, sigma_zero, sigma_plus;
    double spot_dm, spot_dp, volatility_vm, volatility_vp;
    uint64_t market_generation;
    uint32_t path_count;
    uint32_t magic;
    int32_t status;
    uint32_t identity;
    uint8_t reserved[16];
} autocall_d1_native_output_t;

AUTOCALL_D1_NATIVE_STATIC_ASSERT(sizeof(autocall_d1_native_output_t) == 192,
               "conditional risk output is three lines");

typedef struct __attribute__((aligned(64))) {
    autocall_d1_native_market_t market;
    autocall_d1_native_request_t request;
    autocall_d1_native_output_t output;
} autocall_d1_native_workspace_t;

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_plan_t *affine_plan;
    autocall_d1_native_workspace_t *workspace;
    const float *signed_z;
    uint64_t next_generation;
    float signed_z_min;
    float signed_z_max;
    uint32_t magic;
    uint8_t reserved[20];
} autocall_d1_native_engine_t;

AUTOCALL_D1_NATIVE_STATIC_ASSERT(sizeof(autocall_d1_native_engine_t) == 64,
               "private engine handle is one line");

int autocall_d1_native_engine_create(autocall_d1_native_engine_t *);
void autocall_d1_native_engine_destroy(autocall_d1_native_engine_t *);
int autocall_d1_native_market_prepare(autocall_d1_native_engine_t *,
    const autocall_3date_market_input_t *, double volatility_bump,
    autocall_d1_native_market_t *);
int autocall_d1_native_request_prepare(const autocall_d1_native_engine_t *,
    const autocall_d1_native_market_t *,
    const autocall_3date_request_input_t *, double spot_fraction,
    uint32_t path_count, autocall_d1_native_request_t *);
int autocall_d1_native_prepared_price(const autocall_d1_native_request_t *,
    autocall_d1_native_output_t *);
int autocall_d1_native_reused_total(autocall_d1_native_engine_t *,
    const autocall_d1_native_market_t *,
    const autocall_3date_request_input_t *, double spot_fraction,
    uint32_t path_count, autocall_d1_native_output_t *);
int autocall_d1_native_fresh_total(autocall_d1_native_engine_t *,
    const autocall_3date_request_input_t *, double spot_fraction,
    double volatility_bump, uint32_t path_count,
    autocall_d1_native_output_t *);

void autocall_d1_native_price5_leaf(
    const autocall_d1_native_leaf_context_t *, double prices[5]);

#ifdef __cplusplus
}
#endif
#endif
