#ifndef ASIAN_N64_GEOCV_COMPARISON_DIAG_H
#define ASIAN_N64_GEOCV_COMPARISON_DIAG_H

#include <stdint.h>

#include "asian_geocv_affine_lifecycle_diag.h"
#include "asian_genuine_arithmetic_fused_source_exp_diag.h"
#include "asian_genuine_arithmetic_growth_only_strip_adapter.h"
#include "asian_genuine_permute.h"

enum {
    ASIAN_N64_COMPARISON_FIXINGS = 64,
    ASIAN_N64_COMPARISON_PATHS = 4096,
    ASIAN_N64_COMPARISON_DONOR_VALUES = 8192,
};

enum asian_n64_comparison_candidate {
    ASIAN_N64_ARITHMETIC_AFFINE = 0,
    ASIAN_N64_GEOCV_GENERIC_IMMEDIATE = 1,
    ASIAN_N64_GEOCV_AFFINE_IMMEDIATE = 2,
};

#define ASIAN_N64_COMPARISON_ENGINE_MAGIC UINT32_C(0x45434e36)
#define ASIAN_N64_COMPARISON_ARITH_CARRIER_MAGIC UINT32_C(0x41434e36)
#define ASIAN_N64_COMPARISON_ARITH_REQUEST_MAGIC UINT32_C(0x41524e36)
#define ASIAN_N64_COMPARISON_GEOCV_REQUEST_MAGIC UINT32_C(0x47524e36)
#define ASIAN_N64_GENERIC_IMMEDIATE_MAGIC UINT32_C(0x47494e36)

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
} asian_n64_generic_immediate_context_t;

_Static_assert(sizeof(asian_n64_generic_immediate_context_t) == 64,
               "generic immediate context ABI");

typedef struct __attribute__((aligned(64))) {
    asian_geocv_affine_engine_t affine;
    asian_meta_qsort_control_plan_t *generic_plan;
    uint32_t magic;
    uint8_t reserved[52];
} asian_n64_comparison_engine_t;

typedef struct __attribute__((aligned(64))) {
    float growth[ASIAN_N64_COMPARISON_DONOR_VALUES];
    asian_genuine_arithmetic_fused_source_exp_context_t fused;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    uint32_t magic;
    uint8_t reserved[28];
} asian_n64_arithmetic_carrier_t;

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_route_t routes[ASIAN_N64_COMPARISON_FIXINGS];
    asian_meta_growth_only_context_t growth;
    asian_genuine_strip_context_t strip;
    const asian_n64_arithmetic_carrier_t *carrier;
    uint32_t magic;
    uint8_t reserved[52];
} asian_n64_arithmetic_request_t;

typedef union __attribute__((aligned(64))) {
    asian_genuine_route_t generic[ASIAN_N64_COMPARISON_FIXINGS];
    asian_meta_affine_route_t affine[ASIAN_N64_COMPARISON_FIXINGS];
} asian_n64_geocv_routes_t;

typedef struct __attribute__((aligned(64))) {
    asian_n64_geocv_routes_t routes;
    asian_genuine_strip_context_t strip;
    union {
        asian_n64_generic_immediate_context_t generic;
        asian_geometric_cv_immediate_context_t affine;
    } immediate;
    const asian_geocv_affine_carrier_t *carrier;
    uint32_t candidate;
    uint32_t magic;
    uint8_t reserved[48];
} asian_n64_geocv_request_t;

int asian_n64_comparison_engine_create(asian_n64_comparison_engine_t *engine);
void asian_n64_comparison_engine_destroy(asian_n64_comparison_engine_t *engine);

int asian_n64_arithmetic_carrier_prepare(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_carrier_input_t *input,
    asian_n64_arithmetic_carrier_t *carrier);
int asian_n64_arithmetic_request_prepare(
    const asian_n64_comparison_engine_t *engine,
    const asian_n64_arithmetic_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    asian_n64_arithmetic_request_t *request);
int asian_n64_arithmetic_prepared_price(
    const asian_n64_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output);
int asian_n64_arithmetic_fresh_total(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_request_input_t *input,
    asian_n64_arithmetic_carrier_t *carrier,
    asian_n64_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output);
int asian_n64_arithmetic_reuse_total(
    const asian_n64_comparison_engine_t *engine,
    const asian_n64_arithmetic_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    asian_n64_arithmetic_request_t *request,
    asian_genuine_strip_output_t *output);

int asian_n64_geocv_request_prepare(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    enum asian_n64_comparison_candidate candidate,
    asian_n64_geocv_request_t *request);
int asian_n64_geocv_prepared_price(
    const asian_n64_geocv_request_t *request,
    asian_genuine_strip_output_t *output);
int asian_n64_geocv_fresh_total(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_request_input_t *input,
    enum asian_n64_comparison_candidate candidate,
    asian_geocv_affine_carrier_t *carrier,
    asian_n64_geocv_request_t *request,
    asian_genuine_strip_output_t *output);
int asian_n64_geocv_reuse_total(
    const asian_n64_comparison_engine_t *engine,
    const asian_geocv_affine_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    enum asian_n64_comparison_candidate candidate,
    asian_n64_geocv_request_t *request,
    asian_genuine_strip_output_t *output);

void asian_n64_generic_geocv_immediate_invoke_price_1(
    const asian_n64_generic_immediate_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_output_t *);

#endif
