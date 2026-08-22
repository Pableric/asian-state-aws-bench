#ifndef ASIAN_GENUINE_AAD_PHASE1_DIAG_H
#define ASIAN_GENUINE_AAD_PHASE1_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_genuine_permute.h"

/*
 * Private Phase-1 mathematical contract.  Fixings are zero based.
 *
 *   dt   = T / N
 *   x[k] = (r - q - 0.5*sigma*sigma)*dt
 *          + sigma*sqrt(dt)*z[k]
 *   S[k] = S[k-1] * exp(x[k])
 *   A    = sum(S[k]) / N
 *   L    = sum(((N-k)/N) * x[k])
 *   G    = S0 * exp(L)
 *   c    = r - q + 0.5*sigma*sigma
 *   B    = T*(N+1)/(2*N)
 *
 * Contracted suffix reverse (unscaled suffix):
 *
 *   suffix = rho_sum = x_dot = 0
 *   for k=N-1..0:
 *       suffix  += S[k]
 *       rho_sum += suffix
 *       x_dot    = fma(suffix, x[k], x_dot)
 *
 *   A_delta = A/S0
 *   A_rho   = rho_sum*dt/N
 *   A_vega  = (x_dot/N - c*A_rho)/sigma
 *
 * Geometric basis (there is no geometric reverse sweep):
 *
 *   G_delta = G/S0
 *   G_rho   = G*B
 *   G_vega  = G*(L-c*B)/sigma
 */

#define ASIAN_GENUINE_AAD_PHASE1_CONTROL_MAGIC UINT32_C(0x31444141)
#define ASIAN_GENUINE_AAD_PHASE1_ABI_VERSION UINT16_C(1)

enum {
    ASIAN_GENUINE_AAD_PHASE1_PATHS = 4096,
    ASIAN_GENUINE_AAD_PHASE1_PACKETS = 128,
    ASIAN_GENUINE_AAD_PHASE1_PACKET_PATHS = 32,
    ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS = 2,
    ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS = 256,
    ASIAN_GENUINE_AAD_PHASE1_TAPE_FLOATS = 32 * 256,
    ASIAN_GENUINE_AAD_PHASE1_TAPE_BYTES = 32 * 256 * 4,
};

enum asian_genuine_aad_phase1_status {
    ASIAN_GENUINE_AAD_PHASE1_OK = 0,
    ASIAN_GENUINE_AAD_PHASE1_INVALID = -1,
    ASIAN_GENUINE_AAD_PHASE1_SIGMA_ZERO_UNSUPPORTED = -2,
    ASIAN_GENUINE_AAD_PHASE1_PRODUCER_DOMAIN = -3,
    ASIAN_GENUINE_AAD_PHASE1_EXP_DOMAIN = -4,
    ASIAN_GENUINE_AAD_PHASE1_FIXING_COUNT_UNSUPPORTED = -5,
};

enum asian_genuine_aad_phase1_side {
    ASIAN_GENUINE_AAD_PHASE1_CALL = 0,
    ASIAN_GENUINE_AAD_PHASE1_PUT = 1,
};

enum asian_genuine_aad_phase1_basis_field {
    ASIAN_GENUINE_AAD_PHASE1_A = 0,
    ASIAN_GENUINE_AAD_PHASE1_A_DELTA = 1,
    ASIAN_GENUINE_AAD_PHASE1_A_VEGA = 2,
    ASIAN_GENUINE_AAD_PHASE1_A_RHO = 3,
    ASIAN_GENUINE_AAD_PHASE1_G = 4,
    ASIAN_GENUINE_AAD_PHASE1_G_DELTA = 5,
    ASIAN_GENUINE_AAD_PHASE1_G_VEGA = 6,
    ASIAN_GENUINE_AAD_PHASE1_G_RHO = 7,
    ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS = 8,
};

typedef struct {
    double price;
    double delta;
    double vega;
    double rho;
} asian_genuine_aad_phase1_value_t;

/* Separate cold allocation: weights and exact analytic controls are not inline
 * in the recurring hot context. */
typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t reserved0;
    uint32_t fixing_count;
    uint32_t reserved1;
    float maturity;
    float geometric_b;
    float discount;
    float log_s0;
    float forward_weights[ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS];
    asian_genuine_aad_phase1_value_t geometric_call;
    asian_genuine_aad_phase1_value_t geometric_put;
    uint8_t reserved3[32];
} asian_genuine_aad_phase1_controls_t;

_Static_assert(offsetof(asian_genuine_aad_phase1_controls_t, forward_weights) == 32,
               "forward weights ABI");
_Static_assert(offsetof(asian_genuine_aad_phase1_controls_t, log_s0) == 28,
               "qualified geometric exp input ABI");
_Static_assert(offsetof(asian_genuine_aad_phase1_controls_t, geometric_call) == 1056,
               "geometric call ABI");
_Static_assert(sizeof(asian_genuine_aad_phase1_controls_t) == 1152,
               "cold controls size");

/* Exactly one cache line.  routes, tape, maps and controls are separately
 * allocated.  Every supported kernel consumes routes[0] as direct D1, then
 * at least one routed fixing from routes[1..N-1]. */
typedef struct __attribute__((aligned(64))) {
    const asian_genuine_route_t *routes;
    float *s_tape;
    const asian_genuine_aad_phase1_controls_t *controls;
    uint32_t fixing_count;
    uint32_t route_count;
    float s0;
    float strike;
    float inv_n;
    float dt_over_n;
    float c;
    float inv_sigma;
    float inv_s0;
    float discount;
} asian_genuine_aad_phase1_context_t;

_Static_assert(sizeof(asian_genuine_aad_phase1_context_t) == 64,
               "hot context must be one cache line");
_Static_assert(offsetof(asian_genuine_aad_phase1_context_t, s0) == 32,
               "hot scalar ABI");

/* Test-only packet trace.  Ranked leaves never materialize this object. */
typedef struct __attribute__((aligned(64))) {
    float final_s[32];
    float q[32];
    float l[32];
    float basis[ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS][32];
} asian_genuine_aad_phase1_packet_trace_t;

uint32_t asian_genuine_aad_phase1_producer_fixing_count(uint32_t actual_n);

int asian_genuine_aad_phase1_prepare_controls(
    asian_genuine_aad_phase1_controls_t *out,
    double s0, double strike, double rate, double dividend_yield,
    double sigma, double maturity, uint32_t fixing_count);

int asian_genuine_aad_phase1_prepare_context(
    asian_genuine_aad_phase1_context_t *out,
    const asian_genuine_route_t *routes, float *s_tape,
    const asian_genuine_aad_phase1_controls_t *controls,
    double s0, double strike, double rate, double dividend_yield,
    double sigma, double maturity, uint32_t fixing_count);

/* Ranked N=2..256 leaves. */
#define ASIAN_AAD_DECLARE_RANKED(mode, estimator, side) \
void asian_genuine_aad_phase1_##mode##_##estimator##_##side##_diag( \
    const asian_genuine_aad_phase1_context_t *, \
    asian_genuine_aad_phase1_value_t *)

ASIAN_AAD_DECLARE_RANKED(forward, arithmetic, call);
ASIAN_AAD_DECLARE_RANKED(forward, arithmetic, put);
ASIAN_AAD_DECLARE_RANKED(forward, cv, call);
ASIAN_AAD_DECLARE_RANKED(forward, cv, put);
ASIAN_AAD_DECLARE_RANKED(suffix, arithmetic, call);
ASIAN_AAD_DECLARE_RANKED(suffix, arithmetic, put);
ASIAN_AAD_DECLARE_RANKED(suffix, cv, call);
ASIAN_AAD_DECLARE_RANKED(suffix, cv, put);

#undef ASIAN_AAD_DECLARE_RANKED

/* Test-only probes and the single unranked generic basis kernel. */
void asian_genuine_aad_phase1_forward_probe_diag(
    const asian_genuine_aad_phase1_context_t *, uint32_t packet,
    asian_genuine_aad_phase1_packet_trace_t *);
void asian_genuine_aad_phase1_suffix_probe_diag(
    const asian_genuine_aad_phase1_context_t *, uint32_t packet,
    asian_genuine_aad_phase1_packet_trace_t *);
void asian_genuine_aad_phase1_generic_basis_diag(
    const asian_genuine_aad_phase1_context_t *,
    float basis[ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS]
               [ASIAN_GENUINE_AAD_PHASE1_PATHS]);
void asian_genuine_aad_phase1_consume_basis_diag(
    const asian_genuine_aad_phase1_context_t *,
    const float basis[ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS]
                     [ASIAN_GENUINE_AAD_PHASE1_PATHS],
    enum asian_genuine_aad_phase1_side side, int geometric_control,
    asian_genuine_aad_phase1_value_t *);

#endif
