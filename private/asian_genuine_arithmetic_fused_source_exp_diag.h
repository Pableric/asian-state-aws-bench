#ifndef ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_DIAG_H
#define ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_genuine_fixed_block_source_diag.h"

#define ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC UINT32_C(0x58464141)
#define ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION UINT16_C(1)

enum {
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_PATHS = 4096,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_REGIONS = 2,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_VALUES_PER_REGION = 4096,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_TOTAL_VALUES = 8192,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES = 8192 * 4,
};

enum asian_genuine_arithmetic_fused_source_exp_status {
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_OK = 0,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_INVALID = -1,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_SOURCE_REJECTED = -2,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_TABLE_INVALID = -3,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ALIGNMENT = -4,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ALIAS = -5,
    ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_DOMAIN = -6,
};

/*
 * One private cache line.  The ranked fused leaf reads bytes 0..23 only.
 * Region/count identity, magic, version, and reserved fields are cold
 * preparation metadata and are not referenced by its fixed hot loop.
 */
typedef struct __attribute__((aligned(64))) {
    const float *signed_z;
    float *growth_out;
    float drift;
    float diffusion;
    uint32_t fixing_count;
    uint32_t path_count;
    uint32_t region_count;
    uint32_t values_per_region;
    uint32_t first_index;
    uint32_t total_values;
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved0;
    uint8_t reserved[8];
} asian_genuine_arithmetic_fused_source_exp_context_t;

_Static_assert(offsetof(asian_genuine_arithmetic_fused_source_exp_context_t,
                        signed_z) == 0, "fused signed-z ABI");
_Static_assert(offsetof(asian_genuine_arithmetic_fused_source_exp_context_t,
                        growth_out) == 8, "fused growth ABI");
_Static_assert(offsetof(asian_genuine_arithmetic_fused_source_exp_context_t,
                        drift) == 16, "fused drift ABI");
_Static_assert(offsetof(asian_genuine_arithmetic_fused_source_exp_context_t,
                        diffusion) == 20, "fused diffusion ABI");
_Static_assert(offsetof(asian_genuine_arithmetic_fused_source_exp_context_t,
                        fixing_count) == 24, "fused metadata ABI");
_Static_assert(offsetof(asian_genuine_arithmetic_fused_source_exp_context_t,
                        magic) == 48, "fused magic ABI");
_Static_assert(sizeof(asian_genuine_arithmetic_fused_source_exp_context_t) == 64,
               "fused source/exp context must be one cache line");

int asian_genuine_arithmetic_fused_source_exp_prepare(
    asian_genuine_arithmetic_fused_source_exp_context_t *out,
    const asian_genuine_fixed_block_source_request_t *request,
    float *growth_out, size_t growth_out_bytes);

void asian_genuine_arithmetic_fused_source_exp_diag(
    const asian_genuine_arithmetic_fused_source_exp_context_t *context);

/* Linked-audit anchor: log2(e), ln2-hi, ln2-lo, then p0..p8. */
extern const uint32_t asian_genuine_arithmetic_fused_exp_constants[12];

#endif
