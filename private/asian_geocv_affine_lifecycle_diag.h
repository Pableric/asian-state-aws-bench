#ifndef ASIAN_GEOCV_AFFINE_LIFECYCLE_DIAG_H
#define ASIAN_GEOCV_AFFINE_LIFECYCLE_DIAG_H

#include <stdint.h>

#include "asian_geometric_cv_immediate_diag.h"
#include "asian_genuine_fixed_block_source_diag.h"

enum {
    ASIAN_GEOCV_AFFINE_N64 = 64,
    ASIAN_GEOCV_AFFINE_PATHS = 4096,
    ASIAN_GEOCV_AFFINE_DONOR_VALUES = 8192,
};

#define ASIAN_GEOCV_AFFINE_ENGINE_MAGIC UINT32_C(0x45434741)
#define ASIAN_GEOCV_AFFINE_CARRIER_MAGIC UINT32_C(0x43434741)
#define ASIAN_GEOCV_AFFINE_REQUEST_MAGIC UINT32_C(0x52434741)

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_plan_t *plan;
    float signed_z_min;
    float signed_z_max;
    uint32_t magic;
    uint8_t reserved[36];
} asian_geocv_affine_engine_t;

typedef struct {
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
} asian_geocv_affine_carrier_input_t;

typedef struct __attribute__((aligned(64))) {
    float x[ASIAN_GEOCV_AFFINE_DONOR_VALUES];
    float growth[ASIAN_GEOCV_AFFINE_DONOR_VALUES];
    asian_genuine_fixed_block_source_context_t source;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    uint32_t magic;
    uint8_t reserved[28];
} asian_geocv_affine_carrier_t;

typedef struct {
    double s0;
    double strike;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
} asian_geocv_affine_request_input_t;

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_route_t routes[ASIAN_GEOCV_AFFINE_N64];
    asian_genuine_strip_context_t strip;
    asian_geometric_cv_immediate_context_t immediate;
    const asian_geocv_affine_carrier_t *carrier;
    uint32_t magic;
    uint8_t reserved[52];
} asian_geocv_affine_request_t;

int asian_geocv_affine_engine_create(asian_geocv_affine_engine_t *engine);
void asian_geocv_affine_engine_destroy(asian_geocv_affine_engine_t *engine);

int asian_geocv_affine_carrier_prepare(
    const asian_geocv_affine_engine_t *engine,
    const asian_geocv_affine_carrier_input_t *input,
    asian_geocv_affine_carrier_t *carrier);
int asian_geocv_affine_request_prepare(
    const asian_geocv_affine_engine_t *engine,
    const asian_geocv_affine_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    asian_geocv_affine_request_t *request);
int asian_geocv_affine_prepared_price(
    const asian_geocv_affine_request_t *request,
    asian_genuine_strip_output_t *output);
int asian_geocv_affine_fresh_total(
    const asian_geocv_affine_engine_t *engine,
    const asian_geocv_affine_request_input_t *input,
    asian_geocv_affine_carrier_t *carrier,
    asian_geocv_affine_request_t *request,
    asian_genuine_strip_output_t *output);
int asian_geocv_affine_reuse_total(
    const asian_geocv_affine_engine_t *engine,
    const asian_geocv_affine_carrier_t *carrier,
    const asian_geocv_affine_request_input_t *input,
    asian_geocv_affine_request_t *request,
    asian_genuine_strip_output_t *output);

#endif
