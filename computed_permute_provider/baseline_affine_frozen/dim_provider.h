#ifndef DIM_PROVIDER_H
#define DIM_PROVIDER_H

#include <stddef.h>
#include <stdint.h>

enum {
    DIM_PROVIDER_BLOCK_VALUES = 4096,
    DIM_PROVIDER_PACKET_VALUES = 32,
    DIM_PROVIDER_LANES = 16,
    DIM_PROVIDER_BLOCK_PACKETS = 128,
    DIM_PROVIDER_MAX_PATTERNS = 16,
    DIM_PROVIDER_PATTERNS_OFFSET = 576,
    DIM_PROVIDER_GENERIC_BYTES = 1600,
    DIM_PROVIDER_AFFINE_BYTES = 448
};

typedef struct __attribute__((aligned(64))) {
    uint8_t select[DIM_PROVIDER_BLOCK_PACKETS][4];
    uint32_t pattern_count;
    uint32_t reserved0;
    uint8_t reserved1[56];
    uint32_t patterns[DIM_PROVIDER_MAX_PATTERNS][DIM_PROVIDER_LANES];
} dim_generic_ctx_t;

_Static_assert(offsetof(dim_generic_ctx_t, patterns) == DIM_PROVIDER_PATTERNS_OFFSET,
               "patterns at 576");
_Static_assert(sizeof(dim_generic_ctx_t) == DIM_PROVIDER_GENERIC_BYTES, "generic 1600");

typedef struct __attribute__((aligned(64))) {
    uint8_t sel2[DIM_PROVIDER_BLOCK_PACKETS][2];
    uint32_t delta;
    uint32_t reserved0;
    uint8_t reserved1[56];
    uint32_t base_control[DIM_PROVIDER_LANES];
    uint32_t half_delta[DIM_PROVIDER_LANES];
} dim_affine_ctx_t;

_Static_assert(offsetof(dim_affine_ctx_t, base_control) == 320, "base at 320");
_Static_assert(offsetof(dim_affine_ctx_t, half_delta) == 384, "delta zmm at 384");
_Static_assert(sizeof(dim_affine_ctx_t) == DIM_PROVIDER_AFFINE_BYTES, "affine 448");

typedef dim_affine_ctx_t dim_res2xor_ctx_t;

enum {
    DIM_PROVIDER_PRED_OK = 0,
    DIM_PROVIDER_PRED_PERM = 1,
    DIM_PROVIDER_PRED_LINE = 2,
    DIM_PROVIDER_PRED_NPAT = 3,
    DIM_PROVIDER_PRED_XOR_FAMILY = 4,
    DIM_PROVIDER_PRED_TWO_MASK = 5,
    DIM_PROVIDER_PRED_SEQ_GROUP = 6,
    DIM_PROVIDER_PRED_CONTROL = 7,
    DIM_PROVIDER_PRED_LINE_RECON = 8
};

typedef struct {
    int first_fail;
    int sequential_groups;
    int gf2_pair_groups;
    uint32_t npat;
    uint32_t delta;
    uint32_t dest_c_changes;
    uint32_t fail_packet;
} dim_provider_diag_t;

int dim_provider_prepare_generic(const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                                 dim_generic_ctx_t *out);
int dim_provider_prepare_affine(const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                                dim_affine_ctx_t *out);
int dim_provider_prepare_res2xor(const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                                 dim_res2xor_ctx_t *out);
int dim_provider_diagnose(const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                          dim_provider_diag_t *out);

void dim_provider_oracle_packet(const uint32_t *d1,
                                const uint16_t source[DIM_PROVIDER_BLOCK_VALUES],
                                uint32_t packet,
                                uint32_t out[DIM_PROVIDER_PACKET_VALUES]);

#endif
