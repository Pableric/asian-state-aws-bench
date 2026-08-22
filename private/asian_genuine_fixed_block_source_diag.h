#ifndef ASIAN_GENUINE_FIXED_BLOCK_SOURCE_DIAG_H
#define ASIAN_GENUINE_FIXED_BLOCK_SOURCE_DIAG_H

#include <stddef.h>
#include <stdint.h>

#define ASIAN_GENUINE_FIXED_BLOCK_SOURCE_MAGIC UINT32_C(0x53424641)
#define ASIAN_GENUINE_FIXED_BLOCK_EXACT_MAGIC UINT32_C(0x58424641)
#define ASIAN_GENUINE_FIXED_BLOCK_ABI_VERSION UINT16_C(1)

enum {
    ASIAN_GENUINE_FIXED_BLOCK_PATHS = 4096,
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_VALUES = 8192,
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES = 8192 * 4,
    ASIAN_GENUINE_FIXED_BLOCK_PACKETS = 256,
    ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX = 8192,
};

enum asian_genuine_fixed_block_source_status {
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK = 0,
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_INVALID = -1,
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BLOCK_UNSUPPORTED = -2,
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_FEATURE_UNSUPPORTED = -3,
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_FIXINGS_UNSUPPORTED = -4,
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_SIGMA_ZERO_UNSUPPORTED = -5,
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_QUALIFIED_DOMAIN = -6,
    ASIAN_GENUINE_FIXED_BLOCK_SOURCE_TABLE_INVALID = -7,
};

enum asian_genuine_fixed_block_source_flags {
    ASIAN_GENUINE_FIXED_BLOCK_REQUEST_CONTINUATION = UINT32_C(1) << 0,
    ASIAN_GENUINE_FIXED_BLOCK_REQUEST_SCRAMBLE = UINT32_C(1) << 1,
};

typedef struct {
    uint64_t target_start_index;
    uint32_t path_count;
    uint32_t block_count;
    uint32_t block_ordinal;
    uint32_t fixing_count;
    uint32_t flags;
    uint32_t digital_shift;
    double s0;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    const float *signed_z;
    size_t signed_z_bytes;
} asian_genuine_fixed_block_source_request_t;

/* One cache line.  The ranked leaf performs no validation or dispatch. */
typedef struct __attribute__((aligned(64))) {
    const float *signed_z;
    float drift;
    float diffusion;
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved0;
    uint8_t reserved[40];
} asian_genuine_fixed_block_source_context_t;

_Static_assert(sizeof(asian_genuine_fixed_block_source_context_t) == 64,
               "fixed-block source hot context");

typedef struct __attribute__((aligned(64))) {
    const float *prepared_x;
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved0;
    uint8_t reserved[48];
} asian_genuine_fixed_block_exact_x_context_t;

_Static_assert(sizeof(asian_genuine_fixed_block_exact_x_context_t) == 64,
               "fixed-block exact-x hot context");

extern const float asian_genuine_fixed_block_signed_z[];
extern const unsigned char asian_genuine_fixed_block_signed_z_end[];
extern const unsigned char asian_genuine_fixed_block_signed_z_sha256[32];

int asian_genuine_fixed_block_source_prepare(
    asian_genuine_fixed_block_source_context_t *out,
    const asian_genuine_fixed_block_source_request_t *request);

int asian_genuine_fixed_block_exact_x_prepare(
    asian_genuine_fixed_block_exact_x_context_t *out,
    const asian_genuine_fixed_block_source_context_t *source,
    float *prepared_x, size_t prepared_x_bytes);

void asian_genuine_fixed_block_signed_z_one_fma_source_diag(
    const asian_genuine_fixed_block_source_context_t *, float *x_out);

void asian_genuine_fixed_block_prepared_exact_x_lookup_diag(
    const asian_genuine_fixed_block_exact_x_context_t *, float *x_out);

#endif
