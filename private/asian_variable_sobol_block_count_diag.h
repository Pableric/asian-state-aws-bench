#ifndef ASIAN_VARIABLE_SOBOL_BLOCK_COUNT_DIAG_H
#define ASIAN_VARIABLE_SOBOL_BLOCK_COUNT_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_affine_route_family_diag.h"

enum {
    ASIAN_VARIABLE_PATHS_PER_BLOCK = 4096,
    ASIAN_VARIABLE_MAX_BLOCKS = 16,
    ASIAN_VARIABLE_FIRST_INDEX = 8192,
    ASIAN_VARIABLE_FIRST_DONOR_BLOCK = 2,
    ASIAN_VARIABLE_LAST_DONOR_BLOCK = 31,
    ASIAN_VARIABLE_DONOR_REGIONS = 30,
    ASIAN_VARIABLE_SIGNED_Z_BYTES = 30 * 4096 * 4,
    ASIAN_VARIABLE_BLOCK_METADATA_BYTES = 16 * 256 * 4,
    ASIAN_VARIABLE_W_PROVENANCE_BYTES = 128,
};

#define ASIAN_VARIABLE_W_MAGIC UINT32_C(0x42575641)
#define ASIAN_VARIABLE_ENGINE_MAGIC UINT32_C(0x45425641)
#define ASIAN_VARIABLE_CARRIER_MAGIC UINT32_C(0x43425641)
#define ASIAN_VARIABLE_STRIP_REQUEST_MAGIC UINT32_C(0x53525641)
#define ASIAN_VARIABLE_FULL_RISK_REQUEST_MAGIC UINT32_C(0x46525641)
#define ASIAN_VARIABLE_MSFR_REQUEST_MAGIC UINT32_C(0x4d525641)

typedef struct {
    uint16_t base;
    uint8_t donor_block;
    uint8_t reserved;
} asian_variable_block_meta_t;

_Static_assert(sizeof(asian_variable_block_meta_t) == 4,
               "four-byte block metadata record");

typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t column_count;
    uint32_t first_donor_block;
    uint32_t last_donor_block;
    uint32_t physical_permutation;
    uint32_t special_w[17];
    uint8_t joe_kuo_sha256[32];
    uint8_t reserved[8];
} asian_variable_w_provenance_t;

_Static_assert(sizeof(asian_variable_w_provenance_t) ==
               ASIAN_VARIABLE_W_PROVENANCE_BYTES,
               "128-byte transformed-W provenance");

extern const asian_variable_w_provenance_t asian_variable_w_provenance;
extern const asian_variable_block_meta_t
    asian_variable_block_metadata[ASIAN_VARIABLE_MAX_BLOCKS]
                                 [ASIAN_META_DIRECTIONS];
extern const uint32_t asian_variable_donor_masks[ASIAN_VARIABLE_MAX_BLOCKS];

extern const float asian_variable_signed_z_bank[];
extern const unsigned char asian_variable_signed_z_bank_end[];
extern const unsigned char asian_variable_signed_z_bank_sha256[32];

typedef struct __attribute__((aligned(64))) asian_variable_generic_plan {
    uint32_t magic;
    uint8_t donor_region[ASIAN_META_DIRECTIONS];
    uint8_t reserved[60];
    fragment_map_t maps[ASIAN_META_DIRECTIONS];
} asian_variable_generic_plan_t;

_Static_assert(offsetof(asian_variable_generic_plan_t, maps) == 320,
               "generic map header remains 320 bytes");

typedef struct __attribute__((aligned(64))) {
    asian_meta_affine_plan_t *affine_plan[ASIAN_VARIABLE_MAX_BLOCKS];
    asian_variable_generic_plan_t *generic_plan[ASIAN_VARIABLE_MAX_BLOCKS];
    float signed_z_min;
    float signed_z_max;
    uint32_t magic;
    uint32_t generic_enabled;
    uint8_t reserved[48];
} asian_variable_engine_t;

enum asian_variable_carrier_capability {
    ASIAN_VARIABLE_GROWTH_ONLY = 1,
    ASIAN_VARIABLE_X_GROWTH = 2,
};

typedef struct __attribute__((aligned(64))) {
    float *x;
    float *growth;
    asian_affine_family_carrier_input_t market;
    uint32_t magic;
    uint8_t block_capacity;
    uint8_t prepared_block_count;
    uint8_t donor_region_count;
    uint8_t capability;
} asian_variable_carrier_t;

_Static_assert(sizeof(asian_variable_carrier_t) == 64,
               "one-line variable carrier header");

enum asian_variable_family {
    ASIAN_VARIABLE_ARITHMETIC = 1,
    ASIAN_VARIABLE_GEOCV = 2,
    ASIAN_VARIABLE_FULL_RISK = 3,
};

typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint8_t block_count;
    uint8_t family;
    uint8_t provider;
    uint8_t reserved0;
    uint32_t strike_count;
    uint32_t reserved1;
    union {
        asian_affine_family_arithmetic_request_t arithmetic;
        asian_affine_family_geocv_request_t geocv;
    } block[ASIAN_VARIABLE_MAX_BLOCKS];
} asian_variable_strip_request_t;

typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint8_t block_count;
    uint8_t family;
    uint8_t direct_call;
    uint8_t reserved0;
    uint32_t strike_count;
    uint32_t reserved1;
    asian_genuine_aad_phase1_controls_t controls;
    asian_genuine_msfr_strike_t parity;
    asian_affine_family_full_risk_k1_request_t block[ASIAN_VARIABLE_MAX_BLOCKS];
} asian_variable_full_risk_request_t;

typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint8_t block_count;
    uint8_t family;
    uint8_t estimator;
    uint8_t reserved0;
    uint32_t strike_count;
    uint32_t reserved1;
    asian_genuine_msfr_basis_controls_t basis_controls;
    asian_genuine_msfr_strike_controls_t strike_controls;
    asian_genuine_msfr_consumer_context_t consumer;
    asian_genuine_aad_phase1_controls_t phase1_controls;
    asian_genuine_route_t routes[ASIAN_VARIABLE_MAX_BLOCKS]
                                 [ASIAN_META_DIRECTIONS];
    asian_genuine_msfr_basis_context_t basis_context[ASIAN_VARIABLE_MAX_BLOCKS];
    asian_genuine_aad_phase1_context_t phase1_context[ASIAN_VARIABLE_MAX_BLOCKS];
    asian_genuine_msfr_basis_t basis;
} asian_variable_msfr_request_t;

typedef struct __attribute__((aligned(64))) {
    uint32_t family;
    uint32_t strike_count;
    union {
        asian_genuine_strip_output_t strip;
        asian_affine_family_full_risk_k1_output_t full_risk;
        asian_genuine_msfr_output_t multi_strike_full_risk;
    } value;
} asian_variable_output_t;

int asian_variable_block_count_valid(uint32_t block_count);
uint32_t asian_variable_required_donor_regions(uint32_t block_count);
size_t asian_variable_carrier_bytes(uint32_t block_count,
                                    enum asian_variable_carrier_capability cap);

int asian_variable_engine_create(asian_variable_engine_t *engine,
                                 int include_generic_oracle);
void asian_variable_engine_destroy(asian_variable_engine_t *engine);

int asian_variable_carrier_create(uint32_t block_capacity,
                                  enum asian_variable_carrier_capability cap,
                                  asian_variable_carrier_t **out);
void asian_variable_carrier_destroy(asian_variable_carrier_t *carrier);
int asian_variable_carrier_prepare(const asian_variable_engine_t *engine,
                                   uint32_t block_count,
                                   const asian_affine_family_carrier_input_t *input,
                                   asian_variable_carrier_t *carrier);

int asian_variable_strip_request_prepare(
    const asian_variable_engine_t *engine,
    const asian_variable_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_variable_family family,
    enum asian_affine_family_provider provider,
    asian_variable_strip_request_t *request);

int asian_variable_full_risk_k1_request_prepare(
    const asian_variable_engine_t *engine,
    const asian_variable_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    asian_variable_full_risk_request_t *request);

int asian_variable_msfr_request_prepare(
    const asian_variable_engine_t *engine,
    const asian_variable_carrier_t *carrier,
    const asian_affine_family_request_input_t *input,
    enum asian_genuine_msfr_estimator estimator,
    asian_variable_msfr_request_t *request);

int asian_variable_sobol_price(const void *prepared_request,
                               uint32_t block_count,
                               asian_variable_output_t *output);

/* Test/audit helpers; never called from a pricing lifecycle. */
int asian_variable_structural_check(void);
uint64_t asian_variable_phase1_invocations(void);
void asian_variable_phase1_invocations_reset(void);

#endif
