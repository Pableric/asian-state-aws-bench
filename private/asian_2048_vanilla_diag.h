#ifndef ASIAN_2048_VANILLA_DIAG_H
#define ASIAN_2048_VANILLA_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_affine_route_family_diag.h"

enum {
    ASIAN_2048_PATHS = 2048,
    ASIAN_2048_PACKETS = 64,
    ASIAN_2048_DONOR_VIEWS = 4,
    ASIAN_2048_DONOR_VALUES = 8192,
    ASIAN_2048_GROWTH_BYTES = 32768,
    ASIAN_2048_DIM_CONTEXT_BYTES = 320,
    ASIAN_2048_PLAN_HEADER_BYTES = 64,
    ASIAN_2048_PLAN_BYTES = 81984,
    ASIAN_2048_SELECTOR_BYTES = 32768,
};

#define ASIAN_2048_PLAN_MAGIC UINT32_C(0x50324841)
#define ASIAN_2048_REQUEST_MAGIC UINT32_C(0x52524841)

typedef struct __attribute__((aligned(64))) {
    uint8_t sel2[ASIAN_2048_PACKETS][2];
    uint32_t delta;
    uint32_t reserved0;
    uint8_t reserved1[56];
    uint32_t base_control[16];
    uint32_t half_delta[16];
} asian_2048_dim_context_t;

_Static_assert(offsetof(asian_2048_dim_context_t, base_control) == 192,
               "2048 base-control offset");
_Static_assert(offsetof(asian_2048_dim_context_t, half_delta) == 256,
               "2048 half-delta offset");
_Static_assert(sizeof(asian_2048_dim_context_t) ==
               ASIAN_2048_DIM_CONTEXT_BYTES, "2048 context size");

/* The hot plan is exactly 81,984 bytes: a 64-byte header plus contexts. */
typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t dimension_count;
    uint32_t descriptor_bytes;
    uint32_t packet_steps;
    uint32_t selector_bytes;
    uint32_t donor_view_counts[ASIAN_2048_DONOR_VIEWS];
    uint8_t reserved[24];
    asian_2048_dim_context_t contexts[ASIAN_META_DIRECTIONS];
} asian_2048_compact_plan_t;

_Static_assert(offsetof(asian_2048_compact_plan_t, contexts) == 64,
               "compact plan header");
_Static_assert(sizeof(asian_2048_compact_plan_t) == ASIAN_2048_PLAN_BYTES,
               "compact plan bytes");

typedef struct __attribute__((aligned(32))) {
    const float *x_base;
    const float *growth_base;
    const asian_2048_dim_context_t *map;
    uint32_t weight_bits;
    uint32_t fixing_index;
} asian_2048_route_t;

_Static_assert(sizeof(asian_2048_route_t) == 32, "2048 route ABI");

typedef struct __attribute__((aligned(64))) {
    const float *d1_growth;
    const asian_2048_route_t *routes_d2;
    float *unused_q;
    uint32_t fixing_count;
    float s0;
    uint32_t magic;
    uint8_t reserved[28];
} asian_2048_growth_context_t;

_Static_assert(sizeof(asian_2048_growth_context_t) == 64,
               "2048 growth context ABI");

typedef struct __attribute__((aligned(64))) {
    uint8_t reserved0[24];
    float inv_total;
    float initial_q;
    float discount;
    uint8_t reserved1[28];
} asian_2048_payoff_context_t;

_Static_assert(sizeof(asian_2048_payoff_context_t) == 64,
               "2048 payoff context ABI");

typedef struct {
    double call_price;
    double put_price;
} asian_2048_output_t;

typedef struct __attribute__((aligned(64))) {
    asian_affine_family_engine_t parent;
    asian_2048_compact_plan_t *plan;
    uint32_t magic;
    uint8_t reserved[52];
} asian_2048_engine_t;

typedef struct __attribute__((aligned(64))) {
    uint8_t guard_before[64];
    float growth[ASIAN_2048_DONOR_VALUES];
    uint8_t guard_after[64];
    asian_genuine_arithmetic_fused_source_exp_context_t fused;
    asian_affine_family_carrier_input_t market;
    uint32_t magic;
    uint8_t reserved[12];
} asian_2048_carrier_t;

typedef struct __attribute__((aligned(64))) {
    asian_2048_route_t routes[ASIAN_META_DIRECTIONS];
    asian_2048_growth_context_t growth;
    asian_2048_payoff_context_t payoff;
    asian_genuine_strip_strike_t strike;
    uint32_t magic;
    uint8_t reserved[60];
} asian_2048_request_t;

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_route_t routes[ASIAN_META_DIRECTIONS];
    asian_meta_growth_only_context_t growth;
    asian_2048_payoff_context_t payoff;
    asian_genuine_strip_strike_t strike;
    uint32_t magic;
    uint8_t reserved[60];
} asian_4096_vanilla_request_t;

typedef struct asian_2048_generic_plan asian_2048_generic_plan_t;

int asian_2048_engine_create(asian_2048_engine_t *engine);
void asian_2048_engine_destroy(asian_2048_engine_t *engine);
int asian_2048_generic_plan_create(asian_2048_generic_plan_t **out);
void asian_2048_generic_plan_destroy(asian_2048_generic_plan_t *plan);
void asian_2048_carrier_initialize(asian_2048_carrier_t *carrier);
int asian_2048_carrier_prepare(const asian_2048_engine_t *engine,
    const asian_affine_family_carrier_input_t *input,
    asian_2048_carrier_t *carrier);
int asian_2048_request_prepare(const asian_2048_engine_t *engine,
    const asian_2048_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_2048_request_t *request);
int asian_2048_prepared_price(asian_2048_request_t *request,
                              asian_2048_output_t *output);
int asian_4096_vanilla_request_prepare(const asian_2048_engine_t *engine,
    const asian_2048_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_4096_vanilla_request_t *request);
int asian_4096_vanilla_prepared_price(
    const asian_4096_vanilla_request_t *request, asian_2048_output_t *output);

void asian_2048_affine_price_1_diag(const asian_2048_growth_context_t *,
    const asian_2048_payoff_context_t *,
    const asian_genuine_strip_strike_t *, asian_2048_output_t *);

/* Test-only independent qsort/pattern-provider oracle. */
int asian_2048_generic_request_prepare(const asian_2048_generic_plan_t *plan,
    const asian_2048_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_2048_request_t *request);
void asian_2048_generic_price_1_diag(
    const asian_genuine_arithmetic_growth_only_context_t *,
    const asian_2048_payoff_context_t *,
    const asian_genuine_strip_strike_t *, asian_2048_output_t *);

uint8_t asian_2048_descriptor_donor_view(uint32_t dimension);
int asian_2048_structural_check(void);

#endif
