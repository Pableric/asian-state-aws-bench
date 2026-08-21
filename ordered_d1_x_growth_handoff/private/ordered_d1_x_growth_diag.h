#ifndef ORDERED_D1_X_GROWTH_DIAG_H
#define ORDERED_D1_X_GROWTH_DIAG_H

#include <stddef.h>
#include <stdint.h>

#define ORDERED_D1_DIAG_ROWS 128u
#define ORDERED_D1_DIAG_LANES 16u
#define ORDERED_D1_DIAG_CLASSES 2048u
#define ORDERED_D1_DIAG_LOGICAL_CLASSES 4096u
#define ORDERED_D1_DIAG_MAX_POINTS (1u << 20)
#define ORDERED_D1_DIAG_MAX_PACKETS (ORDERED_D1_DIAG_MAX_POINTS / 32u)
#define ORDERED_D1_DIAG_MAX_ALPHA 0.20f
#define ORDERED_D1_DIAG_MIN_DRIFT (-0.25f)
#define ORDERED_D1_DIAG_MAX_DRIFT 0.25f

enum ordered_d1_diag_prepare_flags {
    ORDERED_D1_DIAG_PREPARE_X2 = 1u << 0,
    ORDERED_D1_DIAG_PREPARE_X3 = 1u << 1,
    ORDERED_D1_DIAG_PREPARE_GROWTH3 = 1u << 2,
    ORDERED_D1_DIAG_PREPARE_GROWTH3_FULL = 1u << 3,
    ORDERED_D1_DIAG_PREPARE_GROWTH2 = 1u << 4,
    ORDERED_D1_DIAG_PREPARE_ALL = 0x1fu,
};

typedef struct __attribute__((aligned(64))) ordered_d1_diag_context {
    uint32_t initial[32];
    uint32_t jumps[32];
    float drift;
    float diffusion;
    float exp_drift;
    uint32_t max_packets;
    uint32_t flags;
    uint32_t reserved[11];
    float x2[3][ORDERED_D1_DIAG_CLASSES];
    float x3[4][ORDERED_D1_DIAG_CLASSES];
    float growth3[4][ORDERED_D1_DIAG_CLASSES];
    float growth3_full[4][ORDERED_D1_DIAG_LOGICAL_CLASSES];
    float growth2[3][ORDERED_D1_DIAG_CLASSES];
    float weights[256];
} ordered_d1_diag_context_t;

typedef struct __attribute__((aligned(64))) {
    float s_a[16];
    float s_b[16];
    float q_a[16];
    float q_b[16];
    float l_a[16];
    float l_b[16];
} ordered_d1_diag_state_t;

int ordered_d1_diag_prepare(
    ordered_d1_diag_context_t *ctx,
    float drift,
    float diffusion,
    uint64_t max_points,
    uint32_t flags,
    uint32_t fixing_count);

void ordered_d1_x_only_diag(
    uint64_t packets, const ordered_d1_diag_context_t *ctx, float *x_out);
void ordered_d1_growth_local_diag(
    uint64_t packets, const ordered_d1_diag_context_t *ctx, float *growth_out);
void ordered_d1_x_growth_local_diag(
    uint64_t packets, const ordered_d1_diag_context_t *ctx,
    float *x_out, float *growth_out);
void ordered_d1_x_growth_compact56_diag(
    uint64_t packets, const ordered_d1_diag_context_t *ctx,
    float *x_out, float *growth_out);
void ordered_d1_x_quadratic_diag(
    uint64_t packets, const ordered_d1_diag_context_t *ctx, float *x_out);
void ordered_d1_growth_local_full_diag(
    uint64_t packets, const ordered_d1_diag_context_t *ctx, float *growth_out);
void ordered_d1_exp_p8_array_diag(
    uint64_t vectors, const float *x, float scale, float *growth_out);
void ordered_d1_x_growth_fused_consumer_diag(
    uint32_t steps, const ordered_d1_diag_context_t *ctx,
    const ordered_d1_diag_state_t *initial, ordered_d1_diag_state_t *result);
void ordered_d1_x_sumx_stress_diag(
    uint32_t steps, const ordered_d1_diag_context_t *ctx, float sum_out[32]);

double ordered_d1_price_growth_local_diag(
    uint64_t packets, const ordered_d1_diag_context_t *ctx,
    const float payoff_scale_beta[2]);

/* Standard entry shim for timing the private register-output packet ABI. */
void ordered_d1_x_growth_packet_probe_call_diag(
    const ordered_d1_diag_context_t *ctx);
void ordered_d1_x_growth_packet_ordinary_probe_call_diag(
    const ordered_d1_diag_context_t *ctx);

#endif
