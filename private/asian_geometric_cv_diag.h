#ifndef ASIAN_GEOMETRIC_CV_DIAG_H
#define ASIAN_GEOMETRIC_CV_DIAG_H

#include <stddef.h>
#include <stdint.h>

#define ASIAN_GEOMETRIC_CV_MAGIC UINT32_C(0x56434741) /* "AGCV" */
#define ASIAN_GEOMETRIC_CV_ABI_VERSION UINT16_C(1)

enum { ASIAN_GEOMETRIC_CALL=0, ASIAN_GEOMETRIC_PUT=1 };

typedef struct __attribute__((aligned(64))) {
    uint32_t magic;
    uint16_t abi_version;
    uint16_t option_type;
    uint32_t future_fixings;
    uint32_t completed_fixings;
    uint32_t total_fixings;
    float inv_total;
    float strike;
    float discount;
    float log_s0;
    float payoff_sign;
    float initial_l;
    float exp_input_min;
    float exp_input_max;
    double geometric_exact;
    double log_mean;
    double log_variance;
    double initial_q;
    double past_log_sum;
    uint8_t reserved[32];
} asian_geometric_cv_context_t;

_Static_assert(offsetof(asian_geometric_cv_context_t, inv_total) == 20,
               "geometric payoff float constants");
_Static_assert(offsetof(asian_geometric_cv_context_t, geometric_exact) == 56,
               "geometric exact offset");
_Static_assert(sizeof(asian_geometric_cv_context_t) == 128,
               "geometric context size");

int asian_geometric_cv_prepare(
    asian_geometric_cv_context_t *out,
    double s0, double strike, double rate, double dividend_yield,
    double sigma, double maturity,
    uint32_t future_fixings, uint32_t completed_fixings,
    double initial_q, double past_log_sum, int option_type);

int asian_geometric_cv_exp_preflight(
    const asian_geometric_cv_context_t *context,
    const float *l, size_t count,
    float *observed_min, float *observed_max);

void asian_vector_exp_range_reduced_array_diag(
    const float input[4096], float output[4096]);
double asian_arithmetic_payoff_reduce_diag(
    const float q[4096], const asian_geometric_cv_context_t *context);
double asian_geometric_payoff_reduce_diag(
    const float l[4096], const asian_geometric_cv_context_t *context);
double asian_geometric_cv_payoff_reduce_diag(
    const float q[4096], const float l[4096],
    const asian_geometric_cv_context_t *context);

#endif
