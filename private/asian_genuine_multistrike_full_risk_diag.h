#ifndef ASIAN_GENUINE_MULTISTRIKE_FULL_RISK_DIAG_H
#define ASIAN_GENUINE_MULTISTRIKE_FULL_RISK_DIAG_H

#include <stddef.h>
#include <stdint.h>

#include "asian_genuine_permute.h"

#define ASIAN_GENUINE_MSFR_BASIS_MAGIC UINT32_C(0x4246534d) /* MSFB */
#define ASIAN_GENUINE_MSFR_STRIKE_MAGIC UINT32_C(0x5346534d) /* MSFS */
#define ASIAN_GENUINE_MSFR_ACCUM_MAGIC UINT32_C(0x4146534d) /* MSFA */
#define ASIAN_GENUINE_MSFR_ABI_VERSION UINT16_C(1)

enum {
    ASIAN_GENUINE_MSFR_PATHS = 4096,
    ASIAN_GENUINE_MSFR_PACKET_PATHS = 32,
    ASIAN_GENUINE_MSFR_MIN_FIXINGS = 2,
    ASIAN_GENUINE_MSFR_MAX_FIXINGS = 256,
    ASIAN_GENUINE_MSFR_MAX_STRIKES = 32,
    ASIAN_GENUINE_MSFR_PADDED_STRIKES = 36,
    ASIAN_GENUINE_MSFR_BASIS_FIELDS = 8,
    ASIAN_GENUINE_MSFR_RISK_FIELDS = 4,
    ASIAN_GENUINE_MSFR_BASIS_BYTES = 8 * 4096 * 4,
};

enum asian_genuine_msfr_status {
    ASIAN_GENUINE_MSFR_OK = 0,
    ASIAN_GENUINE_MSFR_INVALID = -1,
    ASIAN_GENUINE_MSFR_SIGMA_ZERO_UNSUPPORTED = -2,
    ASIAN_GENUINE_MSFR_PRODUCER_DOMAIN = -3,
    ASIAN_GENUINE_MSFR_EXP_DOMAIN = -4,
    ASIAN_GENUINE_MSFR_FIXING_COUNT_UNSUPPORTED = -5,
    ASIAN_GENUINE_MSFR_STRIKE_COUNT_UNSUPPORTED = -6,
    ASIAN_GENUINE_MSFR_ACCUMULATOR_MISMATCH = -7,
};

enum asian_genuine_msfr_basis_field {
    ASIAN_GENUINE_MSFR_A = 0,
    ASIAN_GENUINE_MSFR_A_DELTA = 1,
    ASIAN_GENUINE_MSFR_A_VEGA = 2,
    ASIAN_GENUINE_MSFR_A_RHO = 3,
    ASIAN_GENUINE_MSFR_G = 4,
    ASIAN_GENUINE_MSFR_G_DELTA = 5,
    ASIAN_GENUINE_MSFR_G_VEGA = 6,
    ASIAN_GENUINE_MSFR_G_RHO = 7,
};

enum asian_genuine_msfr_risk_field {
    ASIAN_GENUINE_MSFR_PRICE = 0,
    ASIAN_GENUINE_MSFR_DELTA = 1,
    ASIAN_GENUINE_MSFR_VEGA = 2,
    ASIAN_GENUINE_MSFR_RHO = 3,
};

enum asian_genuine_msfr_estimator {
    ASIAN_GENUINE_MSFR_ARITHMETIC = 0,
    ASIAN_GENUINE_MSFR_GEOMETRIC_CV = 1,
};

enum asian_genuine_msfr_strike_flags {
    ASIAN_GENUINE_MSFR_DIRECT_CALL = UINT32_C(1) << 0,
    ASIAN_GENUINE_MSFR_CALL_ITM = UINT32_C(1) << 1,
    ASIAN_GENUINE_MSFR_CALL_ATM = UINT32_C(1) << 2,
    ASIAN_GENUINE_MSFR_CALL_OTM = UINT32_C(1) << 3,
    ASIAN_GENUINE_MSFR_PADDING = UINT32_C(1) << 4,
};

typedef struct __attribute__((aligned(64))) {
    float values[ASIAN_GENUINE_MSFR_BASIS_FIELDS]
                [ASIAN_GENUINE_MSFR_PATHS];
} asian_genuine_msfr_basis_t;

_Static_assert(sizeof(asian_genuine_msfr_basis_t) ==
               ASIAN_GENUINE_MSFR_BASIS_BYTES, "128-KiB basis ABI");

/* Cold basis constants.  The weight offset intentionally matches Phase 1. */
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
    float forward_weights[ASIAN_GENUINE_MSFR_MAX_FIXINGS];
    uint8_t reserved2[32];
} asian_genuine_msfr_basis_controls_t;

_Static_assert(offsetof(asian_genuine_msfr_basis_controls_t,
                        forward_weights) == 32, "basis weights ABI");
_Static_assert(sizeof(asian_genuine_msfr_basis_controls_t) == 1088,
               "basis cold controls ABI");

/* One cache line.  Routes, maps, source payloads, basis and controls are cold. */
typedef struct __attribute__((aligned(64))) {
    const asian_genuine_route_t *routes;
    const asian_genuine_msfr_basis_controls_t *controls;
    uint32_t fixing_count;
    uint32_t route_count;
    float s0;
    float inv_n;
    float dt_over_n;
    float c;
    float inv_sigma;
    float inv_s0;
    uint8_t reserved[16];
} asian_genuine_msfr_basis_context_t;

_Static_assert(sizeof(asian_genuine_msfr_basis_context_t) == 64,
               "basis hot context must be one cache line");

typedef struct __attribute__((aligned(64))) {
    float strike;
    float direct_sign;
    uint32_t strike_bits;
    uint32_t flags;
    double geometric_direct[ASIAN_GENUINE_MSFR_RISK_FIELDS];
    double call_adjust[ASIAN_GENUINE_MSFR_RISK_FIELDS];
    double put_adjust[ASIAN_GENUINE_MSFR_RISK_FIELDS];
    uint8_t reserved[16];
} asian_genuine_msfr_strike_t;

_Static_assert(sizeof(asian_genuine_msfr_strike_t) == 128,
               "cold prepared strike ABI");

typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t strike_count;
    uint32_t fixing_count;
    uint32_t reserved0;
    double s0;
    double rate;
    double dividend_yield;
    double sigma;
    double maturity;
    double discount;
    double expected_arithmetic;
    double expected_arithmetic_delta;
    double expected_arithmetic_vega;
    double expected_arithmetic_rho;
    uint32_t padded_count_tile2;
    uint32_t padded_count_tile4;
    uint8_t reserved1[16];
    asian_genuine_msfr_strike_t strikes[ASIAN_GENUINE_MSFR_PADDED_STRIKES];
} asian_genuine_msfr_strike_controls_t;

_Static_assert(offsetof(asian_genuine_msfr_strike_controls_t, strikes) == 128,
               "cold strike records ABI");
_Static_assert(sizeof(asian_genuine_msfr_strike_controls_t) == 4736,
               "cold strike controls ABI");

typedef struct __attribute__((aligned(64))) {
    const asian_genuine_msfr_strike_controls_t *controls;
    uint32_t strike_count;
    uint32_t fixing_count;
    float discount;
    float maturity;
    uint32_t padded_count_tile2;
    uint32_t padded_count_tile4;
    uint8_t reserved[32];
} asian_genuine_msfr_consumer_context_t;

_Static_assert(sizeof(asian_genuine_msfr_consumer_context_t) == 64,
               "consumer hot context must be one cache line");

typedef struct {
    double price;
    double delta;
    double vega;
    double rho;
} asian_genuine_msfr_value_t;

typedef struct {
    asian_genuine_msfr_value_t call;
    asian_genuine_msfr_value_t put;
} asian_genuine_msfr_two_sided_value_t;

typedef struct __attribute__((aligned(64))) {
    asian_genuine_msfr_two_sided_value_t
        values[ASIAN_GENUINE_MSFR_MAX_STRIKES];
} asian_genuine_msfr_output_t;

typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t estimator;
    uint32_t strike_count;
    uint32_t reserved0;
    uint64_t completed_path_count;
    uint64_t completed_block_count;
    uint32_t controls_magic;
    uint32_t reserved1;
    uint64_t controls_identity;
    uint8_t reserved2[16];
    double direct_sums[ASIAN_GENUINE_MSFR_PADDED_STRIKES]
                      [ASIAN_GENUINE_MSFR_RISK_FIELDS];
} asian_genuine_msfr_accumulator_t;

_Static_assert(offsetof(asian_genuine_msfr_accumulator_t, direct_sums) == 64,
               "streaming accumulator sums ABI");
_Static_assert(sizeof(asian_genuine_msfr_accumulator_t) == 1216,
               "streaming accumulator ABI");

/* Test-only packet trace; ranked leaves never materialize it. */
typedef struct __attribute__((aligned(64))) {
    float final_s[ASIAN_GENUINE_MSFR_PACKET_PATHS];
    float q[ASIAN_GENUINE_MSFR_PACKET_PATHS];
    float l[ASIAN_GENUINE_MSFR_PACKET_PATHS];
    float basis[ASIAN_GENUINE_MSFR_BASIS_FIELDS]
               [ASIAN_GENUINE_MSFR_PACKET_PATHS];
} asian_genuine_msfr_packet_trace_t;

uint32_t asian_genuine_msfr_producer_fixing_count(uint32_t fixing_count);

int asian_genuine_msfr_prepare_basis_controls(
    asian_genuine_msfr_basis_controls_t *out,
    double s0, double rate, double dividend_yield, double sigma,
    double maturity, uint32_t fixing_count);

int asian_genuine_msfr_prepare_basis_context(
    asian_genuine_msfr_basis_context_t *out,
    const asian_genuine_route_t *routes,
    const asian_genuine_msfr_basis_controls_t *controls,
    double s0, double rate, double dividend_yield, double sigma,
    double maturity, uint32_t fixing_count);

int asian_genuine_msfr_prepare_strikes(
    asian_genuine_msfr_strike_controls_t *out,
    double s0, double rate, double dividend_yield, double sigma,
    double maturity, uint32_t fixing_count,
    const float *strikes, uint32_t strike_count);

int asian_genuine_msfr_prepare_consumer_context(
    asian_genuine_msfr_consumer_context_t *out,
    const asian_genuine_msfr_strike_controls_t *controls);

int asian_genuine_msfr_accumulator_init(
    asian_genuine_msfr_accumulator_t *out,
    const asian_genuine_msfr_consumer_context_t *context,
    enum asian_genuine_msfr_estimator estimator);

int asian_genuine_msfr_consume_block(
    const asian_genuine_msfr_basis_t *basis,
    const asian_genuine_msfr_consumer_context_t *context,
    enum asian_genuine_msfr_estimator estimator, uint32_t tile,
    asian_genuine_msfr_accumulator_t *accumulator);

int asian_genuine_msfr_finalize(
    const asian_genuine_msfr_consumer_context_t *context,
    const asian_genuine_msfr_accumulator_t *accumulator,
    asian_genuine_msfr_output_t *output);

/* Ranked leaves.  Dispatch, validation, exact controls and parity stay out. */
void asian_genuine_msfr_basis_forward_diag(
    const asian_genuine_msfr_basis_context_t *, asian_genuine_msfr_basis_t *);
void asian_genuine_msfr_dimension_major_basis_diag(
    const float *dimension_major_x, const float *dimension_major_growth,
    const asian_genuine_msfr_basis_context_t *, asian_genuine_msfr_basis_t *);
void asian_genuine_msfr_arithmetic_tile2_diag(
    const asian_genuine_msfr_basis_t *,
    const asian_genuine_msfr_consumer_context_t *,
    const asian_genuine_msfr_strike_t *, double (*)[4]);
void asian_genuine_msfr_arithmetic_tile4_diag(
    const asian_genuine_msfr_basis_t *,
    const asian_genuine_msfr_consumer_context_t *,
    const asian_genuine_msfr_strike_t *, double (*)[4]);
void asian_genuine_msfr_cv_tile2_diag(
    const asian_genuine_msfr_basis_t *,
    const asian_genuine_msfr_consumer_context_t *,
    const asian_genuine_msfr_strike_t *, double (*)[4]);
void asian_genuine_msfr_cv_tile4_diag(
    const asian_genuine_msfr_basis_t *,
    const asian_genuine_msfr_consumer_context_t *,
    const asian_genuine_msfr_strike_t *, double (*)[4]);

/* Test-only scalar float32 probes/oracles. */
void asian_genuine_msfr_forward_probe_diag(
    const asian_genuine_msfr_basis_context_t *, uint32_t packet,
    asian_genuine_msfr_packet_trace_t *);
int asian_genuine_msfr_scalar_consume_block(
    const asian_genuine_msfr_basis_t *,
    const asian_genuine_msfr_consumer_context_t *,
    enum asian_genuine_msfr_estimator,
    asian_genuine_msfr_accumulator_t *);

#endif
