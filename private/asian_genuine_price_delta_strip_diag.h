#ifndef ASIAN_GENUINE_PRICE_DELTA_STRIP_DIAG_H
#define ASIAN_GENUINE_PRICE_DELTA_STRIP_DIAG_H

#include <stddef.h>
#include <stdint.h>

#define ASIAN_GENUINE_STRIP_MAGIC UINT32_C(0x53504741) /* "AGPS" */
#define ASIAN_GENUINE_STRIP_ABI_VERSION UINT16_C(1)

enum {
    ASIAN_GENUINE_STRIP_PATHS = 4096,
    ASIAN_GENUINE_STRIP_MAX_STRIKES = 32,
    ASIAN_GENUINE_STRIP_TILE4 = 4,
    ASIAN_GENUINE_STRIP_TILE8 = 8,
};

enum asian_genuine_strip_estimator {
    ASIAN_GENUINE_STRIP_ARITHMETIC = 0,
    ASIAN_GENUINE_STRIP_GEOMETRIC_CV = 1,
};

enum asian_genuine_strip_flags {
    ASIAN_GENUINE_STRIP_DIRECT_CALL = UINT32_C(1) << 0,
    ASIAN_GENUINE_STRIP_CALL_ITM = UINT32_C(1) << 1,
    ASIAN_GENUINE_STRIP_CALL_ATM = UINT32_C(1) << 2,
    ASIAN_GENUINE_STRIP_CALL_OTM = UINT32_C(1) << 3,
    ASIAN_GENUINE_STRIP_NEAREST_ATM = UINT32_C(1) << 4,
};

typedef struct {
    float strike;
    float direct_sign;
    double geometric_price_exact_direct;
    double geometric_delta_exact_direct;
    double call_price_adjust;
    double put_price_adjust;
    double call_delta_adjust;
    double put_delta_adjust;
    uint32_t strike_bits;
    uint32_t flags;
} asian_genuine_strip_strike_t;

_Static_assert(sizeof(asian_genuine_strip_strike_t) == 64,
               "one cache line per prepared strike");

typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t strike_count;
    uint32_t future_fixings;
    uint32_t completed_fixings;
    uint32_t total_fixings;
    uint32_t reserved0;
    float inv_total;
    float initial_q;
    float discount;
    float delta_q_scale;
    float delta_g_scale;
    float log_base;
    float expected_arithmetic;
    float expected_arithmetic_delta;
    float exp_input_min;
    float exp_input_max;
    uint8_t reserved1[64];
    asian_genuine_strip_strike_t strikes[ASIAN_GENUINE_STRIP_MAX_STRIKES];
} asian_genuine_strip_context_t;

_Static_assert(offsetof(asian_genuine_strip_context_t, inv_total) == 24,
               "strip hot constants offset");
_Static_assert(offsetof(asian_genuine_strip_context_t, log_base) == 44,
               "strip log base offset");
_Static_assert(offsetof(asian_genuine_strip_context_t, strikes) == 128,
               "strip strike records offset");
_Static_assert(sizeof(asian_genuine_strip_context_t) == 2176,
               "strip context size");

typedef struct {
    double call_price;
    double put_price;
    double call_delta;
    double put_delta;
} asian_genuine_strip_value_t;

typedef struct __attribute__((aligned(64))) {
    asian_genuine_strip_value_t values[ASIAN_GENUINE_STRIP_MAX_STRIKES];
} asian_genuine_strip_output_t;

int asian_genuine_strip_fixed_strikes(uint32_t strike_count,
                                      float strikes[ASIAN_GENUINE_STRIP_MAX_STRIKES]);

int asian_genuine_strip_prepare(
    asian_genuine_strip_context_t *out,
    double s0, double rate, double dividend_yield, double sigma, double maturity,
    uint32_t future_fixings, uint32_t completed_fixings,
    double initial_q, double past_log_sum,
    const float *strikes, uint32_t strike_count);

int asian_genuine_strip_exp_preflight(
    const asian_genuine_strip_context_t *context,
    const float l[ASIAN_GENUINE_STRIP_PATHS],
    float *observed_min, float *observed_max);

void asian_genuine_strip_l_to_g_diag(
    const float l[ASIAN_GENUINE_STRIP_PATHS],
    const asian_genuine_strip_context_t *context,
    float g[ASIAN_GENUINE_STRIP_PATHS]);

int asian_genuine_strip_price_diag(
    const float q_future[ASIAN_GENUINE_STRIP_PATHS],
    const float g[ASIAN_GENUINE_STRIP_PATHS],
    const asian_genuine_strip_context_t *context,
    enum asian_genuine_strip_estimator estimator, uint32_t tile,
    asian_genuine_strip_output_t *output);

int asian_genuine_strip_price_delta_diag(
    const float q_future[ASIAN_GENUINE_STRIP_PATHS],
    const float g[ASIAN_GENUINE_STRIP_PATHS],
    const asian_genuine_strip_context_t *context,
    enum asian_genuine_strip_estimator estimator, uint32_t tile,
    asian_genuine_strip_output_t *output);

/* Ranked fixed-trip leaves. Dispatch and validation stay outside them. */
void asian_genuine_strip_arithmetic_price_1_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_arithmetic_price_4_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_arithmetic_price_8_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_cv_price_1_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_cv_price_4_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_cv_price_8_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_arithmetic_price_delta_1_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_arithmetic_price_delta_4_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_arithmetic_price_delta_8_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_cv_price_delta_1_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_cv_price_delta_4_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);
void asian_genuine_strip_cv_price_delta_8_diag(const float *, const float *,
    const asian_genuine_strip_context_t *, const asian_genuine_strip_strike_t *,
    asian_genuine_strip_value_t *);

#endif
