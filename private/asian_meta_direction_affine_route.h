#ifndef ASIAN_META_DIRECTION_AFFINE_ROUTE_H
#define ASIAN_META_DIRECTION_AFFINE_ROUTE_H

#include <stddef.h>
#include <stdint.h>

#include "asian_genuine_arithmetic_growth_only_diag.h"

enum {
    ASIAN_META_DIRECTIONS = 256,
    ASIAN_META_COLUMNS = 12,
    ASIAN_META_PATHS = 4096,
    ASIAN_META_PACKETS = 128,
    ASIAN_META_DESCRIPTOR_BYTES = 32,
    ASIAN_META_DESCRIPTOR_SET_BYTES = 8192,
    ASIAN_META_AFFINE_CONTEXT_BYTES = 448,
    ASIAN_META_PLAN_HEADER_BYTES = 320,
};

#define ASIAN_META_DESCRIPTOR_MAGIC UINT32_C(0x31444641)
#define ASIAN_META_DESCRIPTOR_ABI_VERSION UINT8_C(1)
#define ASIAN_META_PLAN_MAGIC UINT32_C(0x314c504d)

/*
 * A complete affine map from natural path bits to a 12-bit D1 donor index.
 * The descriptor object is immutable and exactly 8 KiB for D1...D256.
 */
typedef struct __attribute__((aligned(32))) {
    uint16_t base;
    uint8_t donor_region;
    uint8_t abi_version;
    uint16_t column[ASIAN_META_COLUMNS];
    uint32_t magic;
} asian_meta_direction_descriptor_t;

_Static_assert(sizeof(asian_meta_direction_descriptor_t) ==
               ASIAN_META_DESCRIPTOR_BYTES, "32-byte meta descriptor");

/* Audited private affine-provider ABI imported without layout changes. */
typedef struct __attribute__((aligned(64))) {
    uint8_t sel2[ASIAN_META_PACKETS][2];
    uint32_t delta;
    uint32_t reserved0;
    uint8_t reserved1[56];
    uint32_t base_control[16];
    uint32_t half_delta[16];
} asian_meta_dim_affine_ctx_t;

_Static_assert(offsetof(asian_meta_dim_affine_ctx_t, base_control) == 320,
               "affine base-control ABI");
_Static_assert(offsetof(asian_meta_dim_affine_ctx_t, half_delta) == 384,
               "affine half-delta ABI");
_Static_assert(sizeof(asian_meta_dim_affine_ctx_t) ==
               ASIAN_META_AFFINE_CONTEXT_BYTES, "448-byte affine context");

typedef struct __attribute__((aligned(32))) {
    const float *x_base;
    const float *growth_base;
    const asian_meta_dim_affine_ctx_t *map;
    uint32_t weight_bits;
    uint32_t fixing_index;
} asian_meta_affine_route_t;

_Static_assert(sizeof(asian_meta_affine_route_t) == 32,
               "affine hot route size");

typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t dimension_count;
    uint32_t descriptor_bytes;
    uint32_t packet_steps;
    uint8_t donor_region[ASIAN_META_DIRECTIONS];
    uint8_t reserved[48];
    asian_meta_dim_affine_ctx_t contexts[ASIAN_META_DIRECTIONS];
} asian_meta_affine_plan_t;

_Static_assert(offsetof(asian_meta_affine_plan_t, contexts) ==
               ASIAN_META_PLAN_HEADER_BYTES, "aligned plan context offset");

/* Same hot-context byte ABI as the current growth-only implementation. */
typedef struct __attribute__((aligned(64))) {
    const float *d1_growth;
    const asian_meta_affine_route_t *routes_d2;
    float *q_out;
    uint32_t fixing_count;
    float s0;
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved0;
    uint8_t reserved[24];
} asian_meta_growth_only_context_t;

typedef struct asian_meta_qsort_control_plan asian_meta_qsort_control_plan_t;

_Static_assert(sizeof(asian_meta_growth_only_context_t) == 64,
               "meta growth context is one line");

extern const asian_meta_direction_descriptor_t
    asian_meta_direction_descriptors[ASIAN_META_DIRECTIONS];

int asian_meta_affine_context_build(
    const asian_meta_direction_descriptor_t *descriptor,
    asian_meta_dim_affine_ctx_t *out);
int asian_meta_affine_plan_create(asian_meta_affine_plan_t **out);
void asian_meta_affine_plan_destroy(asian_meta_affine_plan_t *plan);
int asian_meta_qsort_control_plan_create(asian_meta_qsort_control_plan_t **out);
void asian_meta_qsort_control_plan_destroy(
    asian_meta_qsort_control_plan_t *plan);
int asian_meta_affine_routes_bind(
    const asian_meta_affine_plan_t *plan,
    const float *x_donors, const float *growth_donors,
    uint32_t fixing_count, asian_meta_affine_route_t *routes);

void asian_meta_arithmetic_growth_only_q_diag(
    const asian_meta_growth_only_context_t *context);
void asian_meta_arithmetic_growth_only_price_1_diag(
    const asian_meta_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_meta_arithmetic_growth_only_price_delta_1_diag(
    const asian_meta_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_meta_arithmetic_growth_only_price_2_diag(
    const asian_meta_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_meta_arithmetic_growth_only_price_delta_2_diag(
    const asian_meta_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_meta_arithmetic_growth_only_price_4_diag(
    const asian_meta_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);
void asian_meta_arithmetic_growth_only_price_delta_4_diag(
    const asian_meta_growth_only_context_t *,
    const asian_genuine_strip_context_t *,
    const asian_genuine_strip_strike_t *, asian_genuine_strip_value_t *);

/* Test-only provider/trace leaves; not used by production dispatch. */
void asian_meta_affine_dual_provider_diag(
    const float *growth, const float *x,
    const asian_meta_dim_affine_ctx_t *map,
    float *growth_out, float *x_out);
void asian_meta_affine_sql_dual_control_diag(
    const asian_meta_affine_route_t *routes,
    uint32_t fixing_count, asian_genuine_state_t *state);

#endif
