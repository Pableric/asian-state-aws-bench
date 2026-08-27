#ifndef ASIAN_AFFINE_DISCRETE_BARRIER_LIFECYCLE_DIAG_H
#define ASIAN_AFFINE_DISCRETE_BARRIER_LIFECYCLE_DIAG_H

#include <stdint.h>

#include "asian_genuine_arithmetic_fused_source_exp_diag.h"
#include "asian_meta_direction_affine_route.h"

enum {
    ASIAN_AFFINE_BARRIER_PATHS = 4096,
    ASIAN_AFFINE_BARRIER_DONOR_VALUES = 8192,
    ASIAN_AFFINE_BARRIER_MIN_MONITORING = 2,
    ASIAN_AFFINE_BARRIER_MAX_MONITORING = 256,
};

enum asian_affine_barrier_product {
    ASIAN_AFFINE_BARRIER_VANILLA = 0,
    ASIAN_AFFINE_BARRIER_KNOCK_OUT = 1,
};

enum asian_affine_barrier_direction {
    ASIAN_AFFINE_BARRIER_DOWN = 0,
    ASIAN_AFFINE_BARRIER_UP = 1,
};

enum asian_affine_barrier_side {
    ASIAN_AFFINE_BARRIER_CALL = 0,
    ASIAN_AFFINE_BARRIER_PUT = 1,
};

enum asian_affine_barrier_initial_state {
    ASIAN_AFFINE_BARRIER_KNOWN_ALIVE = 0,
    ASIAN_AFFINE_BARRIER_ALREADY_KNOCKED_OUT = 1,
};

enum asian_affine_barrier_status {
    ASIAN_AFFINE_BARRIER_OK = 0,
    ASIAN_AFFINE_BARRIER_INVALID = -1,
    ASIAN_AFFINE_BARRIER_UNSUPPORTED = -2,
};

#define ASIAN_AFFINE_BARRIER_ENGINE_MAGIC UINT32_C(0x45424241)
#define ASIAN_AFFINE_BARRIER_CARRIER_MAGIC UINT32_C(0x43424241)
#define ASIAN_AFFINE_BARRIER_REQUEST_MAGIC UINT32_C(0x52424241)
#define ASIAN_AFFINE_BARRIER_OUTPUT_MAGIC UINT32_C(0x4f424241)

typedef struct {
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    uint32_t monitoring_count;
} asian_affine_barrier_market_input_t;

typedef struct {
    double s0;
    double strike;
    double barrier;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    uint32_t monitoring_count;
    enum asian_affine_barrier_product product;
    enum asian_affine_barrier_direction direction;
    enum asian_affine_barrier_side side;
    enum asian_affine_barrier_initial_state initial_state;
} asian_affine_barrier_request_input_t;

/* D1 is deliberately separate. routes_d2 never contains D1. */
typedef struct __attribute__((aligned(64))) {
    const asian_meta_affine_route_t *routes_d2;
    const float *d1_growth;
    uint32_t route_count;
    uint32_t monitoring_count;
    float initial_spot;
    float barrier;
    float strike;
    uint16_t initial_alive_mask;
    uint16_t reserved0;
    double payoff_scale;
    uint32_t magic;
    uint8_t reserved1[12];
} asian_affine_barrier_context_t;

_Static_assert(sizeof(asian_affine_barrier_context_t) == 64,
               "barrier hot context is one cache line");

typedef struct __attribute__((aligned(64))) {
    float growth[ASIAN_AFFINE_BARRIER_DONOR_VALUES];
    asian_genuine_arithmetic_fused_source_exp_context_t fused;
    asian_affine_barrier_market_input_t market;
    uint32_t magic;
    uint8_t reserved[12];
} asian_affine_barrier_carrier_t;

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_route_t routes[ASIAN_AFFINE_BARRIER_MAX_MONITORING];
    asian_affine_barrier_context_t context;
    const asian_affine_barrier_carrier_t *carrier;
    enum asian_affine_barrier_product product;
    enum asian_affine_barrier_direction direction;
    enum asian_affine_barrier_side side;
    uint32_t magic;
    uint8_t reserved[36];
} asian_affine_barrier_request_t;

typedef struct {
    double price;
    uint32_t monitoring_count;
    enum asian_affine_barrier_product product;
    enum asian_affine_barrier_direction direction;
    enum asian_affine_barrier_side side;
    uint32_t magic;
    uint32_t reserved;
} asian_affine_barrier_output_t;

typedef struct asian_affine_barrier_workspace asian_affine_barrier_workspace_t;

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_plan_t *affine_plan;
    asian_affine_barrier_workspace_t *workspace;
    float signed_z_min;
    float signed_z_max;
    uint32_t magic;
    uint8_t reserved[28];
} asian_affine_barrier_engine_t;

int asian_affine_barrier_engine_create(asian_affine_barrier_engine_t *engine);
void asian_affine_barrier_engine_destroy(asian_affine_barrier_engine_t *engine);

int asian_affine_barrier_market_prepare(
    const asian_affine_barrier_engine_t *engine,
    const asian_affine_barrier_market_input_t *input,
    asian_affine_barrier_carrier_t *carrier);
int asian_affine_barrier_request_prepare(
    const asian_affine_barrier_engine_t *engine,
    const asian_affine_barrier_carrier_t *carrier,
    const asian_affine_barrier_request_input_t *input,
    asian_affine_barrier_request_t *request);
int asian_affine_barrier_prepared_price(
    const asian_affine_barrier_request_t *request,
    asian_affine_barrier_output_t *output);
int asian_affine_barrier_reuse_total(
    asian_affine_barrier_engine_t *engine,
    const asian_affine_barrier_carrier_t *carrier,
    const asian_affine_barrier_request_input_t *input,
    asian_affine_barrier_output_t *output);
int asian_affine_barrier_fresh_total(
    asian_affine_barrier_engine_t *engine,
    const asian_affine_barrier_request_input_t *input,
    asian_affine_barrier_output_t *output);

double asian_affine_barrier_knock_in_from_parity(
    double matched_vanilla, double knock_out);

double asian_affine_barrier_vanilla_call_interleaved_diag(
    const asian_affine_barrier_context_t *);
double asian_affine_barrier_vanilla_put_interleaved_diag(
    const asian_affine_barrier_context_t *);
double asian_affine_barrier_down_call_self_interleaved_diag(
    const asian_affine_barrier_context_t *);
double asian_affine_barrier_down_put_self_interleaved_diag(
    const asian_affine_barrier_context_t *);
double asian_affine_barrier_vanilla_call_grouped_diag(
    const asian_affine_barrier_context_t *);
double asian_affine_barrier_vanilla_put_grouped_diag(
    const asian_affine_barrier_context_t *);
double asian_affine_barrier_up_call_self_grouped_diag(
    const asian_affine_barrier_context_t *);
double asian_affine_barrier_up_put_self_grouped_diag(
    const asian_affine_barrier_context_t *);

#endif
