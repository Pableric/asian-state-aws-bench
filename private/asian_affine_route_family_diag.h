#ifndef ASIAN_AFFINE_ROUTE_FAMILY_DIAG_H
#define ASIAN_AFFINE_ROUTE_FAMILY_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_geometric_cv_immediate_diag.h"
#include "asian_genuine_arithmetic_fused_source_exp_diag.h"
#include "asian_genuine_arithmetic_growth_only_strip_adapter.h"

enum {
    ASIAN_AFFINE_FAMILY_PATHS = 4096,
    ASIAN_AFFINE_FAMILY_DONOR_VALUES = 8192,
    ASIAN_AFFINE_FAMILY_MAX_FIXINGS = 256,
    ASIAN_AFFINE_FAMILY_MAX_STRIKES = 32,
};

enum asian_affine_family_status {
    ASIAN_AFFINE_FAMILY_OK = 0,
    ASIAN_AFFINE_FAMILY_INVALID = -1,
    ASIAN_AFFINE_FAMILY_UNSUPPORTED = -2,
    ASIAN_AFFINE_FAMILY_POLICY_UNFROZEN = -3,
    ASIAN_AFFINE_FAMILY_ORACLE_REQUIRED = -4,
};

enum asian_affine_family_provider {
    ASIAN_AFFINE_FAMILY_GENERIC = 0,
    ASIAN_AFFINE_FAMILY_AFFINE = 1,
    ASIAN_AFFINE_FAMILY_AUTO = 2,
};

enum asian_affine_family_workload {
    ASIAN_AFFINE_FAMILY_PRICE = 0,
    ASIAN_AFFINE_FAMILY_PRICE_DELTA = 1,
};

enum asian_affine_family_leaf {
    ASIAN_AFFINE_FAMILY_LEAF1 = 1,
    ASIAN_AFFINE_FAMILY_LEAF2 = 2,
    ASIAN_AFFINE_FAMILY_LEAF4 = 4,
    ASIAN_AFFINE_FAMILY_MATERIALIZED = 32,
};

#define ASIAN_AFFINE_FAMILY_ENGINE_MAGIC UINT32_C(0x45464641)
#define ASIAN_AFFINE_FAMILY_ORACLE_MAGIC UINT32_C(0x4f464641)
#define ASIAN_AFFINE_FAMILY_GROWTH_MAGIC UINT32_C(0x47464641)
#define ASIAN_AFFINE_FAMILY_XGROWTH_MAGIC UINT32_C(0x58464641)
#define ASIAN_AFFINE_FAMILY_ARITH_REQUEST_MAGIC UINT32_C(0x41464641)
#define ASIAN_AFFINE_FAMILY_GEOCV_REQUEST_MAGIC UINT32_C(0x43464641)

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_plan_t *affine_plan;
    float signed_z_min;
    float signed_z_max;
    uint32_t magic;
    uint8_t reserved[36];
} asian_affine_family_engine_t;

typedef struct __attribute__((aligned(64))) {
    asian_meta_qsort_control_plan_t *generic_plan;
    uint32_t magic;
    uint8_t reserved[52];
} asian_affine_family_generic_oracle_t;

typedef struct {
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    uint32_t future_fixings;
} asian_affine_family_carrier_input_t;

typedef struct __attribute__((aligned(64))) {
    float growth[ASIAN_AFFINE_FAMILY_DONOR_VALUES];
    asian_genuine_arithmetic_fused_source_exp_context_t fused;
    asian_affine_family_carrier_input_t market;
    uint32_t magic;
    uint8_t reserved[12];
} asian_affine_family_growth_carrier_t;

typedef struct __attribute__((aligned(64))) {
    float x[ASIAN_AFFINE_FAMILY_DONOR_VALUES];
    float growth[ASIAN_AFFINE_FAMILY_DONOR_VALUES];
    asian_genuine_fixed_block_source_context_t source;
    asian_affine_family_carrier_input_t market;
    uint32_t magic;
    uint8_t reserved[12];
} asian_affine_family_xgrowth_carrier_t;

typedef struct {
    double s0;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    uint32_t future_fixings;
    uint32_t completed_fixings;
    double initial_arithmetic_sum;
    double past_log_sum;
    const float *strikes;
    uint32_t strike_count;
    enum asian_affine_family_workload workload;
} asian_affine_family_request_input_t;

typedef union __attribute__((aligned(64))) {
    asian_genuine_route_t generic[ASIAN_AFFINE_FAMILY_MAX_FIXINGS];
    asian_meta_affine_route_t affine[ASIAN_AFFINE_FAMILY_MAX_FIXINGS];
} asian_affine_family_routes_t;

typedef union __attribute__((aligned(64))) {
    asian_genuine_arithmetic_growth_only_context_t generic;
    asian_meta_growth_only_context_t affine;
} asian_affine_family_arithmetic_context_t;

typedef struct __attribute__((aligned(64))) {
    asian_affine_family_routes_t routes;
    asian_affine_family_arithmetic_context_t growth;
    asian_genuine_strip_context_t strip;
    float q[ASIAN_AFFINE_FAMILY_PATHS];
    const float *carrier_growth;
    enum asian_affine_family_provider provider;
    enum asian_affine_family_leaf leaf;
    uint32_t strike_count;
    enum asian_affine_family_workload workload;
    uint32_t magic;
} asian_affine_family_arithmetic_request_t;

typedef struct __attribute__((aligned(64))) {
    const float *d1_x;
    const float *d1_growth;
    const asian_genuine_route_t *routes_d2;
    uint32_t fixing_count;
    float s0;
    uint32_t d1_weight_bits;
    uint32_t terminal_log_base_bits;
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved0;
    uint8_t reserved1[16];
} asian_affine_family_generic_immediate_context_t;

_Static_assert(sizeof(asian_affine_family_generic_immediate_context_t) == 64,
               "generic immediate context ABI");

typedef struct __attribute__((aligned(64))) {
    const float *d1_x;
    const float *d1_growth;
    const asian_genuine_route_t *routes_d2;
    float *q_out;
    float *g_out;
    uint32_t fixing_count;
    float s0;
    uint32_t d1_weight_bits;
    uint32_t terminal_log_base_bits;
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved;
} asian_affine_family_generic_packet_context_t;

_Static_assert(sizeof(asian_affine_family_generic_packet_context_t) == 64,
               "generic packet context ABI");

typedef union __attribute__((aligned(64))) {
    asian_affine_family_generic_immediate_context_t generic;
    asian_geometric_cv_immediate_context_t affine;
} asian_affine_family_geocv_immediate_context_t;

typedef union __attribute__((aligned(64))) {
    asian_affine_family_generic_packet_context_t generic;
    asian_geometric_cv_packet_local_context_t affine;
} asian_affine_family_geocv_packet_context_t;

typedef struct __attribute__((aligned(64))) {
    asian_affine_family_routes_t routes;
    asian_genuine_strip_context_t strip;
    asian_affine_family_geocv_immediate_context_t immediate;
    asian_affine_family_geocv_packet_context_t packet;
    float q[ASIAN_AFFINE_FAMILY_PATHS];
    float g[ASIAN_AFFINE_FAMILY_PATHS];
    const asian_affine_family_xgrowth_carrier_t *carrier;
    enum asian_affine_family_provider provider;
    enum asian_affine_family_leaf leaf;
    uint32_t strike_count;
    enum asian_affine_family_workload workload;
    uint32_t magic;
} asian_affine_family_geocv_request_t;

int asian_affine_family_engine_create(asian_affine_family_engine_t *engine);
void asian_affine_family_engine_destroy(asian_affine_family_engine_t *engine);
int asian_affine_family_generic_oracle_create(
    asian_affine_family_generic_oracle_t *oracle);
void asian_affine_family_generic_oracle_destroy(
    asian_affine_family_generic_oracle_t *oracle);

int asian_affine_family_growth_carrier_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_carrier_input_t *input,
    asian_affine_family_growth_carrier_t *carrier);
int asian_affine_family_xgrowth_carrier_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_carrier_input_t *input,
    asian_affine_family_xgrowth_carrier_t *carrier);

int asian_affine_family_arithmetic_request_prepare_growth(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_generic_oracle_t *oracle,
    const asian_affine_family_growth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_affine_family_provider provider,
    asian_affine_family_arithmetic_request_t *request);
int asian_affine_family_arithmetic_request_prepare_xgrowth(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_generic_oracle_t *oracle,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_affine_family_provider provider,
    asian_affine_family_arithmetic_request_t *request);
int asian_affine_family_arithmetic_prepared_price(
    asian_affine_family_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output);

int asian_affine_family_geocv_request_prepare(
    const asian_affine_family_engine_t *engine,
    const asian_affine_family_generic_oracle_t *oracle,
    const asian_affine_family_xgrowth_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_affine_family_provider provider,
    asian_affine_family_geocv_request_t *request);
int asian_affine_family_geocv_prepared_price(
    asian_affine_family_geocv_request_t *request,
    asian_genuine_strip_output_t *output);

void asian_affine_family_generic_packet_qg_diag(
    const asian_affine_family_generic_packet_context_t *context);

#define ASIAN_AFFINE_FAMILY_GENERIC_INVOKE_DECL(mode, width) \
void asian_affine_family_generic_geocv_invoke_##mode##_##width( \
    const asian_affine_family_generic_immediate_context_t *, \
    const asian_genuine_strip_context_t *, \
    const asian_genuine_strip_strike_t *, asian_genuine_strip_output_t *)

ASIAN_AFFINE_FAMILY_GENERIC_INVOKE_DECL(price, 1);
ASIAN_AFFINE_FAMILY_GENERIC_INVOKE_DECL(price_delta, 1);
ASIAN_AFFINE_FAMILY_GENERIC_INVOKE_DECL(price, 2);
ASIAN_AFFINE_FAMILY_GENERIC_INVOKE_DECL(price_delta, 2);
ASIAN_AFFINE_FAMILY_GENERIC_INVOKE_DECL(price, 4);
ASIAN_AFFINE_FAMILY_GENERIC_INVOKE_DECL(price_delta, 4);
#undef ASIAN_AFFINE_FAMILY_GENERIC_INVOKE_DECL

#endif
