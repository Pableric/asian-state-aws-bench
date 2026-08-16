#define _GNU_SOURCE

#include "european_pricer.h"
#include "private/sobol.h"
#include "private/gaussian_first_patch_values_2048.h"
#include "private/gaussian_linear_coeff_values_2048.h"
#include "private/gaussian_range_schedule_2048.h"
#include "private/gaussian_center_shared_coeff_values_2048.h"
#include "private/gaussian_dynamic_range_coeff_values.h"
#include "private/gaussian_scheduled_coeff_values_2048.h"
#include "private/gaussian_split_tail_2048.h"
#include "private/gaussian_tail_coeff_values_2048.h"
#include "private/european_reduced_setup.h"
#include "private/european_ordered_setup.h"
#include "european_exp_table_64.h"
#include "european_reduced_exp_slots.h"
#include "european_direct_center_4096.h"

#include <errno.h>
#include <immintrin.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct european_prepared_contract {
    european_price_request_t req;
    int reduced;
    float params[4] __attribute__((aligned(16)));
    float regular_exp_schedule[3 * EUROPEAN_EXP_ZMM_SLOTS] __attribute__((aligned(64)));
    float reduced_tail_schedule[
        EUROPEAN_REDUCED_EXP_TAIL_STREAMS * EUROPEAN_REDUCED_EXP_TAIL_STRIDE
    ] __attribute__((aligned(64)));
    float combined_c0[128 * 32] __attribute__((aligned(64)));
    float combined_c1[128 * 32] __attribute__((aligned(64)));
    uint32_t ordered_initial_states[32] __attribute__((aligned(64)));
    european_ordered_d1_tail_context_t ordered_tail;
};

static inline double now_seconds(void) {
#ifdef EUROPEAN_ENABLE_INTERNAL_TIMING
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1.0e-9;
#else
    return 0.0;
#endif
}

extern double price_european_sequence(
    float* unused_output,
    uint64_t number_of_iterations,
    const uint32_t* direction_vectors,
    const float* gauss_c0,
    const float* gauss_c1,
    const float* gauss_c2,
    const uint32_t* gauss_fold_mask,
    const float* gauss_tail_c0,
    const float* gauss_tail_c1,
    const float* gauss_tail_c2,
    const float* gauss_tail_c3,
    const float* gauss_tail_c4,
    const float* gauss_tail_c5,
    const float* gauss_range2047_lut,
    const uint32_t* gauss_range2047_mask,
    const float* gauss_linear_c0,
    const float* gauss_linear_c1,
    const float* gauss_first_patch_zmm,
    const float* pricing_params,
    const float* exp_schedule
);

extern double price_european_sequence_reduced_fma(
    float* unused_output,
    uint64_t number_of_iterations,
    const uint32_t* direction_vectors,
    const float* gauss_c0,
    const float* gauss_c1,
    const float* gauss_c2,
    const uint32_t* gauss_fold_mask,
    const float* gauss_tail_c0,
    const float* gauss_tail_c1,
    const float* gauss_tail_c2,
    const float* gauss_tail_c3,
    const float* gauss_tail_c4,
    const float* gauss_tail_c5,
    const float* gauss_range2047_lut,
    const uint32_t* gauss_range2047_mask,
    const float* gauss_linear_c0,
    const float* gauss_linear_c1,
    const float* gauss_first_patch_zmm,
    const float* pricing_params,
    const float* reduced_exp_schedule
);

extern double price_european_sequence_ordered_d1(
    uint64_t number_of_packets,
    const uint32_t* initial_states,
    const uint32_t* packet_jump_words,
    const float* combined_c0,
    const float* combined_c1,
    const european_ordered_d1_tail_context_t* tail
);

extern double price_european_sequence_center_shared(
    float* unused_output,
    uint64_t number_of_iterations,
    const uint32_t* direction_vectors,
    const float* gauss_c0,
    const float* gauss_c1,
    const float* gauss_c2,
    const uint32_t* gauss_fold_mask,
    const float* gauss_tail_c0,
    const float* gauss_tail_c1,
    const float* gauss_tail_c2,
    const float* gauss_tail_c3,
    const float* gauss_tail_c4,
    const float* gauss_tail_c5,
    const float* gauss_range2047_lut,
    const uint32_t* gauss_range2047_mask,
    const float* gauss_linear_c0,
    const float* gauss_linear_c1,
    const float* gauss_first_patch_zmm,
    const float* pricing_params,
    const float* exp_schedule,
    const float* gauss_center_shared_c0,
    const float* gauss_center_shared_c1
);

extern double price_european_sequence_dynamic_ranges(
    float* unused_output,
    uint64_t number_of_iterations,
    const uint32_t* direction_vectors,
    const float* gauss_c0,
    const float* gauss_c1,
    const float* gauss_c2,
    const uint32_t* gauss_fold_mask,
    const float* gauss_tail_c0,
    const float* gauss_tail_c1,
    const float* gauss_tail_c2,
    const float* gauss_tail_c3,
    const float* gauss_tail_c4,
    const float* gauss_tail_c5,
    const float* gauss_range2047_lut,
    const uint32_t* gauss_range2047_mask,
    const float* gauss_dynamic_c0,
    const float* gauss_dynamic_c1,
    const float* unused_first_patch,
    const float* pricing_params,
    const float* exp_schedule
);

extern double price_european_sequence_direct(
    float* unused_output,
    uint64_t number_of_iterations,
    const uint32_t* direction_vectors,
    const float* gauss_c0,
    const float* gauss_c1,
    const float* gauss_c2,
    const uint32_t* gauss_fold_mask,
    const float* gauss_tail_c0,
    const float* gauss_tail_c1,
    const float* gauss_tail_c2,
    const float* gauss_tail_c3,
    const float* gauss_tail_c4,
    const float* gauss_tail_c5,
    const float* gauss_range2047_lut,
    const uint32_t* gauss_range2047_mask,
    const float* gauss_linear_c0,
    const float* gauss_linear_c1,
    const float* gauss_first_patch_zmm,
    const float* pricing_params,
    const float* direct_schedule
);

extern double price_european_sequence_hybrid(
    float* unused_output,
    uint64_t number_of_iterations,
    const uint32_t* direction_vectors,
    const float* gauss_c0,
    const float* gauss_c1,
    const float* gauss_c2,
    const uint32_t* gauss_fold_mask,
    const float* gauss_tail_c0,
    const float* gauss_tail_c1,
    const float* gauss_tail_c2,
    const float* gauss_tail_c3,
    const float* gauss_tail_c4,
    const float* gauss_tail_c5,
    const float* gauss_range2047_lut,
    const uint32_t* gauss_range2047_mask,
    const float* gauss_linear_c0,
    const float* gauss_linear_c1,
    const float* gauss_first_patch_zmm,
    const float* pricing_params,
    const float* exp_schedule,
    const float* patch_payoff,
    const float* contaminated_direct_schedule,
    const float* gauss_range2047_endpoint_lut
);

// Must match private/sobol.c.
static const uint32_t european_sobol_words[64] __attribute__((aligned(64))) = {
    0x80000000u, 0x40000000u, 0x20000000u, 0x10000000u,
    0x08000000u, 0x04000000u, 0x02000000u, 0x01000000u,
    0x00800000u, 0x00400000u, 0x00200000u, 0x00100000u,
    0x00080000u, 0x00040000u, 0x00020000u, 0x00010000u,
    0x00008000u, 0x00004000u, 0x00002000u, 0x00001000u,
    0x00000800u, 0x00000400u, 0x00000200u, 0x00000100u,
    0x00000080u, 0x00000040u, 0x00000020u, 0x00000010u,
    0x00000008u, 0x00000004u, 0x00000002u, 0x00000001u,

    0x00000000u, 0x01800000u, 0x00c00000u, 0x01400000u,
    0x00600000u, 0x01e00000u, 0x00a00000u, 0x01200000u,
    0x00300000u, 0x01b00000u, 0x00f00000u, 0x01700000u,
    0x00500000u, 0x01d00000u, 0x00900000u, 0x01100000u,
    0x00100000u, 0x01900000u, 0x00d00000u, 0x01500000u,
    0x00700000u, 0x01f00000u, 0x00b00000u, 0x01300000u,
    0x00200000u, 0x01a00000u, 0x00e00000u, 0x01600000u,
    0x00400000u, 0x01c00000u, 0x00800000u, 0x01000000u,
};

/* J[k] advances every lane by 32 canonical D1 indices. */
static const uint32_t european_ordered_d1_jump_words[32]
__attribute__((aligned(64))) = {
    0x0c000000u, 0x0a000000u, 0x09000000u, 0x08800000u,
    0x08400000u, 0x08200000u, 0x08100000u, 0x08080000u,
    0x08040000u, 0x08020000u, 0x08010000u, 0x08008000u,
    0x08004000u, 0x08002000u, 0x08001000u, 0x08000800u,
    0x08000400u, 0x08000200u, 0x08000100u, 0x08000080u,
    0x08000040u, 0x08000020u, 0x08000010u, 0x08000008u,
    0x08000004u, 0x08000002u, 0x08000001u, 0u,
    0u, 0u, 0u, 0u,
};

static int valid_request(const european_price_request_t* req) {
    if (!req) {
        return 0;
    }
    if (!(req->s0 > 0.0f) || !(req->k > 0.0f) || !(req->sigma >= 0.0f) ||
        !(req->t > 0.0f) || req->num_blocks == 0) {
        return 0;
    }
    if (req->type != EUROPEAN_CALL && req->type != EUROPEAN_PUT) {
        return 0;
    }
    if (req->mode != EUROPEAN_MODE_BUFFER_REFERENCE &&
        req->mode != EUROPEAN_MODE_GAUSSIAN_EXP &&
        req->mode != EUROPEAN_MODE_DIRECT_PAYOFF &&
        req->mode != EUROPEAN_MODE_HYBRID &&
        req->mode != EUROPEAN_MODE_HYBRID_DIRECT_TAIL &&
        req->mode != EUROPEAN_MODE_GAUSSIAN_SPLIT_TAIL &&
        req->mode != EUROPEAN_MODE_GAUSSIAN_CENTER_SHARED &&
        req->mode != EUROPEAN_MODE_GAUSSIAN_DYNAMIC_RANGES &&
        req->mode != EUROPEAN_MODE_GAUSSIAN_EXP_REDUCED_FMA &&
        req->mode != EUROPEAN_MODE_ORDERED_D1_MIN_FMA) {
        return 0;
    }
    return 1;
}

static double normal_cdf(double x) {
    return 0.5 * erfc(-x / sqrt(2.0));
}

static double normal_quantile(double p) {
    static const double a[] = {
        -3.969683028665376e+01, 2.209460984245205e+02,
        -2.759285104469687e+02, 1.383577518672690e+02,
        -3.066479806614716e+01, 2.506628277459239e+00
    };
    static const double b[] = {
        -5.447609879822406e+01, 1.615858368580409e+02,
        -1.556989798598866e+02, 6.680131188771972e+01,
        -1.328068155288572e+01
    };
    static const double c[] = {
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
         4.374664141464968e+00,  2.938163982698783e+00
    };
    static const double d[] = {
        7.784695709041462e-03, 3.224671290700398e-01,
        2.445134137142996e+00, 3.754408661907416e+00
    };
    const double plow = 0.02425;
    const double phigh = 1.0 - plow;

    if (p <= 0.0) {
        return -INFINITY;
    }
    if (p >= 1.0) {
        return INFINITY;
    }
    if (p < plow) {
        const double q = sqrt(-2.0 * log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
               ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }
    if (p > phigh) {
        const double q = sqrt(-2.0 * log(1.0 - p));
        return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
                ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }

    const double q = p - 0.5;
    const double r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
           (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
}

double black_scholes_price(const european_price_request_t* req) {
    if (!valid_request(req)) {
        return NAN;
    }

    const double s0 = (double)req->s0;
    const double k = (double)req->k;
    const double r = (double)req->r;
    const double sigma = (double)req->sigma;
    const double t = (double)req->t;
    const double df = exp(-r * t);

    if (sigma == 0.0) {
        const double fwd = s0 * exp(r * t);
        const double payoff = req->type == EUROPEAN_CALL ? fmax(fwd - k, 0.0) : fmax(k - fwd, 0.0);
        return df * payoff;
    }

    const double vol = sigma * sqrt(t);
    const double d1 = (log(s0 / k) + (r + 0.5 * sigma * sigma) * t) / vol;
    const double d2 = d1 - vol;

    if (req->type == EUROPEAN_CALL) {
        return s0 * normal_cdf(d1) - k * df * normal_cdf(d2);
    }
    return k * df * normal_cdf(-d2) - s0 * normal_cdf(-d1);
}

static void* aligned_alloc64(size_t nbytes) {
    void* p = NULL;
    const int rc = posix_memalign(&p, 64, nbytes);
    if (rc != 0) {
        errno = rc;
        return NULL;
    }
    return p;
}

static void build_pricing_params(const european_price_request_t* req, float params[4]) {
    const float s0 = req->s0;
    const float k = req->k;
    const float r = req->r;
    const float sigma = req->sigma;
    const float t = req->t;
    const float mu = (r - 0.5f * sigma * sigma) * t;
    const float vol = sigma * sqrtf(t);
    const float df = expf(-r * t);
    const float exp_mu = expf(mu);

    params[0] = vol;
    if (req->type == EUROPEAN_CALL) {
        params[1] = df * s0 * exp_mu;
        params[2] = -df * k;
    } else {
        params[1] = -df * s0 * exp_mu;
        params[2] = df * k;
    }

    const float polynomial_payoff_at_zero = fmaxf(params[1] + params[2], 0.0f);
    const float exact_endpoint_payoff = req->type == EUROPEAN_CALL ? 0.0f : df * k;
    params[3] = exact_endpoint_payoff - polynomial_payoff_at_zero;
}

static int exp_bucket_for_x(double x) {
    int bucket = (int)((x - (double)EUROPEAN_EXP_DOMAIN_LO) * (double)EUROPEAN_EXP_BUCKET_SCALE);
    if (bucket < 0) {
        return 0;
    }
    if (bucket >= EUROPEAN_EXP_BUCKETS) {
        return EUROPEAN_EXP_BUCKETS - 1;
    }
    return bucket;
}

static void fill_exp_schedule(float* dst, const float* zmid, double alpha) {
    for (size_t i = 0; i < EUROPEAN_EXP_ZMM_SLOTS; ++i) {
        const int bucket = exp_bucket_for_x(alpha * (double)zmid[i]);
        dst[i] = european_exp64_c2[bucket];
        dst[EUROPEAN_EXP_ZMM_SLOTS + i] = european_exp64_c1[bucket];
        dst[2 * EUROPEAN_EXP_ZMM_SLOTS + i] = european_exp64_c0[bucket];
    }
}

static int exp_bucket_for_xf(float x) {
    int bucket = (int)((x - EUROPEAN_EXP_DOMAIN_LO) * EUROPEAN_EXP_BUCKET_SCALE);
    if (bucket < 0) {
        return 0;
    }
    if (bucket >= EUROPEAN_EXP_BUCKETS) {
        return EUROPEAN_EXP_BUCKETS - 1;
    }
    return bucket;
}

static void fill_payoff_exp_schedule(
    float* dst,
    const float* zmid,
    float alpha,
    float payoff_scale,
    float payoff_beta
) {
    /* Pablo please work your magic here. */
    for (size_t i = 0; i < EUROPEAN_EXP_ZMM_SLOTS; ++i) {
        const int bucket = exp_bucket_for_xf(alpha * zmid[i]);
        dst[i] = payoff_scale * european_exp64_c2[bucket];
        dst[EUROPEAN_EXP_ZMM_SLOTS + i] = payoff_scale * european_exp64_c1[bucket];
        dst[2 * EUROPEAN_EXP_ZMM_SLOTS + i] =
            payoff_scale * european_exp64_c0[bucket] + payoff_beta;
    }
}

static int build_exp_schedules(const european_price_request_t* req, float* schedule) {
    const double alpha = (double)req->sigma * sqrt((double)req->t);
    if (alpha * (double)EUROPEAN_EXP_MAX_FINITE_Z > (double)EUROPEAN_EXP_DOMAIN_HI) {
        return -4;
    }

    fill_exp_schedule(schedule, european_exp_regular_zmid, alpha);
    fill_exp_schedule(schedule + 3 * EUROPEAN_EXP_ZMM_SLOTS, european_exp_first0_zmid, alpha);
    fill_exp_schedule(schedule + 6 * EUROPEAN_EXP_ZMM_SLOTS, european_exp_first1_zmid, alpha);
    return 0;
}

static int build_regular_exp_schedule(const european_price_request_t* req, const float params[4], float* schedule) {
    const float alpha = req->sigma * sqrtf(req->t);
    if (alpha * EUROPEAN_EXP_MAX_FINITE_Z > EUROPEAN_EXP_DOMAIN_HI) {
        return -4;
    }

    fill_payoff_exp_schedule(schedule, european_exp_regular_zmid, alpha, params[1], params[2]);
    return 0;
}

static double exp_poly8(double x) {
    double y = 2.58406812172e-05;
    y = y * x + 0.0002077216867;
    y = y * x + 0.0013875434074;
    y = y * x + 0.00832308095835;
    y = y * x + 0.0416673696717;
    y = y * x + 0.166670788605;
    y = y * x + 0.499999873009;
    y = y * x + 0.999999559932;
    y = y * x + 1.00000000361;
    return y;
}

static double polynomial_payoff_from_z(double z, const float params[4]) {
    const double x = (double)params[0] * z;
    const double payoff = (double)params[1] * exp_poly8(x) + (double)params[2];
    return payoff > 0.0 ? payoff : 0.0;
}

static void build_hybrid_patch_payoff(float* patch_payoff, const float params[4]) {
    for (size_t i = 0; i < GAUSS_FIRST_PATCH_VALUES; ++i) {
        const float zf = gauss_first_patch_zmm[i];
        const double z = isfinite(zf) ? (double)zf : 0.0;
        patch_payoff[i] = (float)polynomial_payoff_from_z(z, params);
    }
}

static __m512d direct_exp_poly_pd(__m512d x) __attribute__((target("avx512f")));
static __m512d direct_exp_poly_pd(__m512d x) {
    __m512d y = _mm512_set1_pd(2.58406812172e-05);
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(0.0002077216867));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(0.0013875434074));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(0.00832308095835));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(0.0416673696717));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(0.166670788605));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(0.499999873009));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(0.999999559932));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(1.00000000361));
    return y;
}

static __m512d direct_exp_poly_d1_pd(__m512d x) __attribute__((target("avx512f")));
static __m512d direct_exp_poly_d1_pd(__m512d x) {
    __m512d y = _mm512_set1_pd(8.0 * 2.58406812172e-05);
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(7.0 * 0.0002077216867));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(6.0 * 0.0013875434074));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(5.0 * 0.00832308095835));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(4.0 * 0.0416673696717));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(3.0 * 0.166670788605));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(2.0 * 0.499999873009));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(0.999999559932));
    return y;
}

static __m512d direct_exp_poly_d2_pd(__m512d x) __attribute__((target("avx512f")));
static __m512d direct_exp_poly_d2_pd(__m512d x) {
    __m512d y = _mm512_set1_pd(56.0 * 2.58406812172e-05);
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(42.0 * 0.0002077216867));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(30.0 * 0.0013875434074));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(20.0 * 0.00832308095835));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(12.0 * 0.0416673696717));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(6.0 * 0.166670788605));
    y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(2.0 * 0.499999873009));
    return y;
}

static __m512d direct_discounted_poly_payoff_pd(
    __m512d raw_x,
    __m512d g0,
    __m512d g1,
    __m512d scale,
    __m512d beta
) __attribute__((target("avx512f")));
static __m512d direct_discounted_poly_payoff_pd(
    __m512d raw_x,
    __m512d g0,
    __m512d g1,
    __m512d scale,
    __m512d beta
) {
    const __m512d z = _mm512_fmadd_pd(g1, raw_x, g0);
    const __m512d v = _mm512_fmadd_pd(scale, direct_exp_poly_pd(z), beta);
    return _mm512_max_pd(v, _mm512_setzero_pd());
}

static void build_direct_payoff_schedule(float* schedule, const european_price_request_t* req) __attribute__((target("avx512f")));
static void build_direct_payoff_schedule(float* schedule, const european_price_request_t* req) {
    const double alpha = (double)req->sigma * sqrt((double)req->t);
    const double mu = ((double)req->r - 0.5 * (double)req->sigma * (double)req->sigma) * (double)req->t;
    const double df = exp(-(double)req->r * (double)req->t);
    const double signed_scale = (req->type == EUROPEAN_CALL ? 1.0 : -1.0) * df * (double)req->s0 * exp(mu);
    const double beta = req->type == EUROPEAN_CALL ? -df * (double)req->k : df * (double)req->k;
    const double h = 0.5 / 4096.0;

    const __m512d alpha_v = _mm512_set1_pd(alpha);
    const __m512d scale_v = _mm512_set1_pd(signed_scale);
    const __m512d beta_v = _mm512_set1_pd(beta);
    const __m512d h_v = _mm512_set1_pd(h);
    const __m512d two_h_v = _mm512_set1_pd(2.0 * h);
    const __m512d two_h2_v = _mm512_set1_pd(2.0 * h * h);
    const __m512d half_scale_v = _mm512_set1_pd(0.5 * signed_scale);
    const __m512d zero_v = _mm512_setzero_pd();

    for (size_t i = 0; i < 4096; i += 8) {
        const __m512d m = _mm512_cvtps_pd(_mm256_load_ps(european_direct_raw_center_4096 + i));
        const __m512d g0 = _mm512_mul_pd(alpha_v, _mm512_cvtps_pd(_mm256_load_ps(gauss_linear_c0 + i)));
        const __m512d g1 = _mm512_mul_pd(alpha_v, _mm512_cvtps_pd(_mm256_load_ps(gauss_linear_c1 + i)));
        const __m512d z = _mm512_fmadd_pd(g1, m, g0);
        const __m512d gh = _mm512_mul_pd(g1, h_v);
        const __m512d vl = _mm512_fmadd_pd(scale_v, direct_exp_poly_pd(_mm512_sub_pd(z, gh)), beta_v);
        const __m512d vm = _mm512_fmadd_pd(scale_v, direct_exp_poly_pd(z), beta_v);
        const __m512d vr = _mm512_fmadd_pd(scale_v, direct_exp_poly_pd(_mm512_add_pd(z, gh)), beta_v);

        const __mmask8 all_zero =
            _mm512_cmp_pd_mask(vl, zero_v, _CMP_LE_OQ) &
            _mm512_cmp_pd_mask(vm, zero_v, _CMP_LE_OQ) &
            _mm512_cmp_pd_mask(vr, zero_v, _CMP_LE_OQ);
        const __mmask8 all_positive =
            _mm512_cmp_pd_mask(vl, zero_v, _CMP_GT_OQ) &
            _mm512_cmp_pd_mask(vm, zero_v, _CMP_GT_OQ) &
            _mm512_cmp_pd_mask(vr, zero_v, _CMP_GT_OQ);

        if (all_zero == 0xff) {
            const __m256 zero_ps = _mm256_setzero_ps();
            _mm256_store_ps(schedule + i, zero_ps);
            _mm256_store_ps(schedule + 4096 + i, zero_ps);
            _mm256_store_ps(schedule + 8192 + i, zero_ps);
            _mm256_store_ps(schedule + 12288 + i, _mm512_cvtpd_ps(m));
            continue;
        }

        if (all_positive == 0xff) {
            const __m512d p2 = _mm512_mul_pd(
                _mm512_mul_pd(half_scale_v, direct_exp_poly_d2_pd(z)),
                _mm512_mul_pd(g1, g1)
            );
            const __m512d p1 = _mm512_mul_pd(_mm512_mul_pd(scale_v, direct_exp_poly_d1_pd(z)), g1);

            _mm256_store_ps(schedule + i, _mm512_cvtpd_ps(p2));
            _mm256_store_ps(schedule + 4096 + i, _mm512_cvtpd_ps(p1));
            _mm256_store_ps(schedule + 8192 + i, _mm512_cvtpd_ps(vm));
            _mm256_store_ps(schedule + 12288 + i, _mm512_cvtpd_ps(m));
            continue;
        }

        const __m512d yl = direct_discounted_poly_payoff_pd(_mm512_sub_pd(m, h_v), g0, g1, scale_v, beta_v);
        const __m512d ym = direct_discounted_poly_payoff_pd(m, g0, g1, scale_v, beta_v);
        const __m512d yr = direct_discounted_poly_payoff_pd(_mm512_add_pd(m, h_v), g0, g1, scale_v, beta_v);

        __m512d p2 = _mm512_div_pd(
            _mm512_sub_pd(_mm512_add_pd(yr, yl), _mm512_add_pd(ym, ym)),
            two_h2_v
        );
        __m512d p1 = _mm512_div_pd(_mm512_sub_pd(yr, yl), two_h_v);
        __m512d p0 = ym;

        const __m512d smooth_p2 = _mm512_mul_pd(
            _mm512_mul_pd(half_scale_v, direct_exp_poly_d2_pd(z)),
            _mm512_mul_pd(g1, g1)
        );
        const __m512d smooth_p1 = _mm512_mul_pd(_mm512_mul_pd(scale_v, direct_exp_poly_d1_pd(z)), g1);

        p2 = _mm512_mask_blend_pd(all_positive, p2, smooth_p2);
        p1 = _mm512_mask_blend_pd(all_positive, p1, smooth_p1);
        p0 = _mm512_mask_blend_pd(all_positive, p0, vm);

        p2 = _mm512_mask_blend_pd(all_zero, p2, zero_v);
        p1 = _mm512_mask_blend_pd(all_zero, p1, zero_v);
        p0 = _mm512_mask_blend_pd(all_zero, p0, zero_v);

        _mm256_store_ps(schedule + i, _mm512_cvtpd_ps(p2));
        _mm256_store_ps(schedule + 4096 + i, _mm512_cvtpd_ps(p1));
        _mm256_store_ps(schedule + 8192 + i, _mm512_cvtpd_ps(p0));
        _mm256_store_ps(schedule + 12288 + i, _mm512_cvtpd_ps(m));
    }
}

enum {
    EUROPEAN_CONTAMINATED_PAIRS_PER_CHUNK = 7,
    EUROPEAN_CONTAMINATED_LANES_PER_CHUNK = EUROPEAN_CONTAMINATED_PAIRS_PER_CHUNK * 32,
    EUROPEAN_CONTAMINATED_LANES = 2 * EUROPEAN_CONTAMINATED_LANES_PER_CHUNK,
    EUROPEAN_DIRECT_TAIL_DEGREE = 11,
    EUROPEAN_COMPACT_DIRECT_STREAMS = EUROPEAN_DIRECT_TAIL_DEGREE + 2
};

static double direct_tail_payoff_at_raw(
    double raw_x,
    const european_price_request_t* req,
    double mu,
    double vol,
    double df
) {
    const double u = raw_x - 1.0;
    const double z = normal_quantile(u);
    const double st = (double)req->s0 * exp(mu + vol * z);
    if (req->type == EUROPEAN_CALL) {
        return df * fmax(st - (double)req->k, 0.0);
    }
    return df * fmax((double)req->k - st, 0.0);
}

static uint32_t sobol_raw_word(uint64_t index) {
    uint64_t gray = index ^ (index >> 1);
    uint32_t x = 0;
    size_t bit = 0;
    while (gray != 0 && bit < 64) {
        if ((gray & 1u) != 0) {
            x ^= european_sobol_words[bit];
        }
        gray >>= 1;
        ++bit;
    }
    return x;
}

static void build_ordered_d1_initial_states(uint32_t states[32]) {
    for (uint64_t lane = 0; lane < 32; ++lane) {
        states[lane] = sobol_raw_word(
            (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE + lane);
    }
}

static int valid_ordered_points_request(
    const european_price_request_t* req,
    uint64_t num_points
) {
    if (!req || req->mode != EUROPEAN_MODE_ORDERED_D1_MIN_FMA ||
        num_points < 32u || (num_points & 31u) != 0u ||
        num_points > (UINT64_C(1) << 32) - EUROPEAN_SOBOL_BLOCK_SIZE) {
        return 0;
    }
    const float alpha = req->sigma * sqrtf(req->t);
    if (alpha > EUROPEAN_ORDERED_D1_MAX_ALPHA) {
        return 0;
    }
    european_price_request_t validation = *req;
    validation.num_blocks = 1;
    return valid_request(&validation);
}

static int price_ordered_d1_points_unchecked(
    const european_price_request_t* req,
    uint64_t num_points,
    european_price_result_t* out
) {
    if (req->sigma == 0.0f) {
        const double df = exp(-(double)req->r * (double)req->t);
        const double st = (double)req->s0 *
            exp((double)req->r * (double)req->t);
        const double payoff = req->type == EUROPEAN_CALL ?
            df * fmax(st - (double)req->k, 0.0) :
            df * fmax((double)req->k - st, 0.0);
        out->payoff_sum = payoff * (double)num_points;
        out->samples = num_points;
        out->price = payoff;
        return 0;
    }

    float params[4] __attribute__((aligned(16)));
    float combined_c0[EUROPEAN_ORDERED_D1_COEFF_FLOATS]
        __attribute__((aligned(64)));
    float combined_c1[EUROPEAN_ORDERED_D1_COEFF_FLOATS]
        __attribute__((aligned(64)));
    uint32_t initial_states[32] __attribute__((aligned(64)));
    european_ordered_d1_tail_context_t tail;

    const double setup_start = now_seconds();
    build_pricing_params(req, params);
    build_ordered_d1_initial_states(initial_states);
    european_build_ordered_d1_schedule(
        params, combined_c0, combined_c1, &tail, gauss_range2047_lut);
    const double setup_end = now_seconds();

    const double kernel_start = now_seconds();
    const double payoff_sum = price_european_sequence_ordered_d1(
        num_points / 32u,
        initial_states,
        european_ordered_d1_jump_words,
        combined_c0,
        combined_c1,
        &tail);
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = num_points;
    out->price = payoff_sum / (double)num_points;
    out->coeff_setup_seconds = setup_end - setup_start;
    out->kernel_seconds = kernel_end - kernel_start;
    return 0;
}

static int price_ordered_d1_prepared_points_unchecked(
    const european_prepared_contract_t* prepared,
    uint64_t num_points,
    european_price_result_t* out
) {
    const european_price_request_t* req = &prepared->req;
    if (req->sigma == 0.0f) {
        const double df = exp(-(double)req->r * (double)req->t);
        const double st = (double)req->s0 *
            exp((double)req->r * (double)req->t);
        const double payoff = req->type == EUROPEAN_CALL ?
            df * fmax(st - (double)req->k, 0.0) :
            df * fmax((double)req->k - st, 0.0);
        out->payoff_sum = payoff * (double)num_points;
        out->samples = num_points;
        out->price = payoff;
        return 0;
    }

    const double kernel_start = now_seconds();
    const double payoff_sum = price_european_sequence_ordered_d1(
        num_points / 32u,
        prepared->ordered_initial_states,
        european_ordered_d1_jump_words,
        prepared->combined_c0,
        prepared->combined_c1,
        &prepared->ordered_tail);
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = num_points;
    out->price = payoff_sum / (double)num_points;
    out->coeff_setup_seconds = 0.0;
    out->kernel_seconds = kernel_end - kernel_start;
    return 0;
}

static double raw_center_for_sobol_index(uint64_t index) {
    const uint32_t shifted = sobol_raw_word(index) >> 9;
    const uint32_t cell = shifted >> 11;
    return 1.0 + ((double)cell + 0.5) / 4096.0;
}

static uint32_t logical_for_mem_8192(uint32_t mem_idx) {
    const uint32_t internal_half = mem_idx / 4096u;
    const uint32_t pos = mem_idx % 4096u;
    const uint32_t step = pos / 32u;
    const uint32_t lane32 = pos % 32u;
    const uint32_t lane = lane32 < 16u ? lane32 : lane32 - 16u;

    if (internal_half == 0u) {
        if (lane32 < 16u) {
            return step + lane * 256u;
        }
        return 4096u + step + lane * 256u;
    }

    if (lane32 < 16u) {
        return (4096u - 1u - step) - lane * 256u;
    }
    return (8192u - 1u - step) - lane * 256u;
}

static void build_contaminated_direct_schedule(float* compact, const european_price_request_t* req) __attribute__((target("avx512f")));
static void build_contaminated_direct_schedule(float* compact, const european_price_request_t* req) {
    static const size_t pair_zmm_start[EUROPEAN_CONTAMINATED_PAIRS_PER_CHUNK] = {
        0, 84, 126, 170, 212, 242, 254
    };
    const double s0 = (double)req->s0;
    const double k = (double)req->k;
    const double r = (double)req->r;
    const double sigma = (double)req->sigma;
    const double t = (double)req->t;
    const double mu = (r - 0.5 * sigma * sigma) * t;
    const double vol = sigma * sqrt(t);
    const double df = exp(-r * t);
    const double h = 0.5 / 4096.0;
    const size_t n = EUROPEAN_DIRECT_TAIL_DEGREE + 1;

    (void)s0;
    (void)k;

    for (size_t parity = 0; parity < 2; ++parity) {
        for (size_t pair = 0; pair < EUROPEAN_CONTAMINATED_PAIRS_PER_CHUNK; ++pair) {
            for (size_t lane = 0; lane < 32; ++lane) {
                const size_t src_lane = pair_zmm_start[pair] * 16 + lane;
                const size_t lane_dst =
                    parity * EUROPEAN_CONTAMINATED_LANES_PER_CHUNK +
                    pair * 32 +
                    lane;
                const uint32_t mem_idx = (uint32_t)(parity * 4096u + src_lane);
                const uint32_t logical_idx = logical_for_mem_8192(mem_idx);
                const uint64_t sobol_index =
                    (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE +
                    (uint64_t)logical_idx;
                const double m = raw_center_for_sobol_index(sobol_index);
                double values[12];
                double coeff[12];

                for (size_t j = 0; j < n; ++j) {
                    const double theta = M_PI * ((double)j + 0.5) / (double)n;
                    values[j] = direct_tail_payoff_at_raw(m + h * cos(theta), req, mu, vol, df);
                }

                for (size_t kidx = 0; kidx < n; ++kidx) {
                    double sum = 0.0;
                    for (size_t j = 0; j < n; ++j) {
                        const double theta = M_PI * ((double)j + 0.5) / (double)n;
                        sum += values[j] * cos((double)kidx * theta);
                    }
                    coeff[kidx] = (kidx == 0 ? 1.0 : 2.0) * sum / (double)n;
                }

                for (size_t kidx = 0; kidx <= EUROPEAN_DIRECT_TAIL_DEGREE; ++kidx) {
                    const size_t stream = EUROPEAN_DIRECT_TAIL_DEGREE - kidx;
                    compact[stream * EUROPEAN_CONTAMINATED_LANES + lane_dst] = (float)coeff[kidx];
                }
                compact[(EUROPEAN_DIRECT_TAIL_DEGREE + 1) * EUROPEAN_CONTAMINATED_LANES + lane_dst] = (float)m;
            }
        }
    }
}

static int price_fused_gaussian_exp(const european_price_request_t* req, european_price_result_t* out) {
    const uint64_t samples = req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE;
    float params[4] __attribute__((aligned(16)));
    float exp_schedule[3 * EUROPEAN_EXP_ZMM_SLOTS] __attribute__((aligned(64)));

    const double setup_start = now_seconds();
    build_pricing_params(req, params);
    const int schedule_rc = build_regular_exp_schedule(req, params, exp_schedule);
    const double setup_end = now_seconds();
    if (schedule_rc != 0) {
        return schedule_rc;
    }

    const double kernel_start = now_seconds();
    const double payoff_sum = price_european_sequence(
        NULL,
        req->num_blocks * 2 + 1,
        european_sobol_words,
        gauss_sched_c0,
        gauss_sched_c1,
        gauss_sched_c2,
        gauss_fold_mask_2048,
        gauss_tail_c0,
        gauss_tail_c1,
        gauss_tail_c2,
        gauss_tail_c3,
        gauss_tail_c4,
        gauss_tail_c5,
        gauss_range2047_lut,
        gauss_range2047_mask,
        gauss_linear_c0,
        gauss_linear_c1,
        gauss_first_patch_zmm,
        params,
        exp_schedule
    );
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = samples;
    out->price = payoff_sum / (double)samples;
    out->coeff_setup_seconds = setup_end - setup_start;
    out->kernel_seconds = kernel_end - kernel_start;

    return 0;
}

static int price_fused_gaussian_exp_reduced_fma(
    const european_price_request_t* req,
    european_price_result_t* out
) {
    const float alpha = req->sigma * sqrtf(req->t);
    if (!(alpha > 0.0f) || alpha > EUROPEAN_REDUCED_EXP_MAX_ALPHA) {
        return price_fused_gaussian_exp(req, out);
    }

    const uint64_t samples = req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE;
    float params[4] __attribute__((aligned(16)));
    float exp_schedule[
        EUROPEAN_REDUCED_EXP_TAIL_STREAMS * EUROPEAN_REDUCED_EXP_TAIL_STRIDE
    ] __attribute__((aligned(64)));
    float combined_c0[128 * 32] __attribute__((aligned(64)));
    float combined_c1[128 * 32] __attribute__((aligned(64)));

    const double setup_start = now_seconds();
    build_pricing_params(req, params);
    european_build_reduced_tail_schedule(params, exp_schedule);
    european_build_composed_normal_schedule(params, combined_c0, combined_c1);
    const double setup_end = now_seconds();

    const double kernel_start = now_seconds();
    const double payoff_sum = price_european_sequence_reduced_fma(
        NULL,
        req->num_blocks * 2 + 1,
        european_sobol_words,
        gauss_sched_c0,
        gauss_sched_c1,
        gauss_sched_c2,
        gauss_fold_mask_2048,
        gauss_tail_c0,
        gauss_tail_c1,
        gauss_tail_c2,
        gauss_tail_c3,
        gauss_tail_c4,
        gauss_tail_c5,
        gauss_range2047_lut,
        gauss_range2047_mask,
        combined_c0,
        combined_c1,
        gauss_first_patch_zmm,
        params,
        exp_schedule
    );
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = samples;
    out->price = payoff_sum / (double)samples;
    out->coeff_setup_seconds = setup_end - setup_start;
    out->kernel_seconds = kernel_end - kernel_start;
    return 0;
}

static int price_fused_gaussian_center_shared(const european_price_request_t* req, european_price_result_t* out) {
    const uint64_t samples = req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE;
    float params[4] __attribute__((aligned(16)));
    float exp_schedule[EUROPEAN_EXP_SCHEDULE_FLOATS] __attribute__((aligned(64)));

    const double setup_start = now_seconds();
    build_pricing_params(req, params);
    const int schedule_rc = build_exp_schedules(req, exp_schedule);
    const double setup_end = now_seconds();
    if (schedule_rc != 0) {
        return schedule_rc;
    }

    const double kernel_start = now_seconds();
    const double payoff_sum = price_european_sequence_center_shared(
        NULL,
        req->num_blocks * 2 + 1,
        european_sobol_words,
        gauss_sched_c0,
        gauss_sched_c1,
        gauss_sched_c2,
        gauss_fold_mask_2048,
        gauss_tail_c0,
        gauss_tail_c1,
        gauss_tail_c2,
        gauss_tail_c3,
        gauss_tail_c4,
        gauss_tail_c5,
        gauss_range2047_lut,
        gauss_range2047_mask,
        gauss_linear_c0,
        gauss_linear_c1,
        gauss_first_patch_zmm,
        params,
        exp_schedule,
        gauss_center_shared_c0,
        gauss_center_shared_c1
    );
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = samples;
    out->price = payoff_sum / (double)samples;
    out->coeff_setup_seconds = setup_end - setup_start;
    out->kernel_seconds = kernel_end - kernel_start;

    return 0;
}

static int price_fused_gaussian_dynamic_ranges(const european_price_request_t* req, european_price_result_t* out) {
    const uint64_t samples = req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE;
    float params[4] __attribute__((aligned(16)));
    float exp_schedule[3 * EUROPEAN_EXP_ZMM_SLOTS] __attribute__((aligned(64)));

    const double setup_start = now_seconds();
    build_pricing_params(req, params);
    const int schedule_rc = build_regular_exp_schedule(req, params, exp_schedule);
    const double setup_end = now_seconds();
    if (schedule_rc != 0) {
        return schedule_rc;
    }

    const double kernel_start = now_seconds();
    const double payoff_sum = price_european_sequence_dynamic_ranges(
        NULL,
        req->num_blocks * 2 + 1,
        european_sobol_words,
        gauss_sched_c0,
        gauss_sched_c1,
        gauss_sched_c2,
        gauss_fold_mask_2048,
        gauss_tail_c0,
        gauss_tail_c1,
        gauss_tail_c2,
        gauss_tail_c3,
        gauss_tail_c4,
        gauss_tail_c5,
        gauss_range2047_lut,
        gauss_range2047_mask,
        &gauss_dynamic_c0[0][0],
        &gauss_dynamic_c1[0][0],
        gauss_first_patch_zmm,
        params,
        exp_schedule
    );
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = samples;
    out->price = payoff_sum / (double)samples;
    out->coeff_setup_seconds = setup_end - setup_start;
    out->kernel_seconds = kernel_end - kernel_start;
    return 0;
}

static int price_fused_gaussian_split_tail(const european_price_request_t* req, european_price_result_t* out) {
    /*
     * The split-tail tables are generated and compiled as a separate mode so
     * the kernel can be benchmarked independently. Until the AVX-512 override
     * is wired, use the compact shifted Gaussian kernel as the correctness
     * baseline for this mode.
     */
    (void)gauss_split_tail_raw_ranges;
    (void)gauss_split_tail_pattern;
    (void)gauss_split_tail_lane_desc;
    (void)gauss_split_tail_fallback_mask;
    (void)gauss_split_tail_coeff;
    return price_fused_gaussian_exp(req, out);
}

static int price_fused_hybrid(const european_price_request_t* req, european_price_result_t* out, int direct_tail) {
    const uint64_t samples = req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE;
    float params[4] __attribute__((aligned(16)));
    float exp_schedule[EUROPEAN_EXP_SCHEDULE_FLOATS] __attribute__((aligned(64)));
    float patch_payoff[GAUSS_FIRST_PATCH_VALUES] __attribute__((aligned(64)));
    float contaminated_direct_schedule[EUROPEAN_COMPACT_DIRECT_STREAMS * EUROPEAN_CONTAMINATED_LANES] __attribute__((aligned(64)));

    const double setup_start = now_seconds();
    build_pricing_params(req, params);
    const int schedule_rc = build_exp_schedules(req, exp_schedule);
    if (schedule_rc != 0) {
        return schedule_rc;
    }
    build_hybrid_patch_payoff(patch_payoff, params);
    if (direct_tail) {
        build_contaminated_direct_schedule(contaminated_direct_schedule, req);
    }
    const double setup_end = now_seconds();

    const double kernel_start = now_seconds();
    const double payoff_sum = price_european_sequence_hybrid(
        NULL,
        req->num_blocks * 2 - 1,
        european_sobol_words,
        gauss_sched_c0,
        gauss_sched_c1,
        gauss_sched_c2,
        gauss_fold_mask_2048,
        gauss_tail_c0,
        gauss_tail_c1,
        gauss_tail_c2,
        gauss_tail_c3,
        gauss_tail_c4,
        gauss_tail_c5,
        gauss_range2047_lut,
        gauss_range2047_mask,
        gauss_linear_c0,
        gauss_linear_c1,
        gauss_first_patch_zmm,
        params,
        exp_schedule,
        patch_payoff,
        direct_tail ? contaminated_direct_schedule : NULL,
        gauss_range2047_lut
    );
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = samples;
    out->price = payoff_sum / (double)samples;
    out->coeff_setup_seconds = setup_end - setup_start;
    out->kernel_seconds = kernel_end - kernel_start;
    return 0;
}

static int price_fused_direct_payoff(const european_price_request_t* req, european_price_result_t* out) {
    const uint64_t samples = req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE;
    float params[4] __attribute__((aligned(16)));
    float direct_schedule[4 * 4096] __attribute__((aligned(64)));

    const double setup_start = now_seconds();
    build_pricing_params(req, params);
    build_direct_payoff_schedule(direct_schedule, req);
    const double setup_end = now_seconds();

    const double kernel_start = now_seconds();
    const double payoff_sum = price_european_sequence_direct(
        NULL,
        req->num_blocks * 2 - 1,
        european_sobol_words,
        gauss_sched_c0,
        gauss_sched_c1,
        gauss_sched_c2,
        gauss_fold_mask_2048,
        gauss_tail_c0,
        gauss_tail_c1,
        gauss_tail_c2,
        gauss_tail_c3,
        gauss_tail_c4,
        gauss_tail_c5,
        gauss_range2047_lut,
        gauss_range2047_mask,
        gauss_linear_c0,
        gauss_linear_c1,
        gauss_first_patch_zmm,
        params,
        direct_schedule
    );
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = samples;
    out->price = payoff_sum / (double)samples;
    out->coeff_setup_seconds = setup_end - setup_start;
    out->kernel_seconds = kernel_end - kernel_start;
    return 0;
}

static int price_buffer_reference(const european_price_request_t* req, european_price_result_t* out) {
    const uint64_t samples = req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE;
    const size_t bytes = (size_t)samples * sizeof(float);
    float* z = (float*)aligned_alloc64(bytes);
    if (!z) {
        return -2;
    }

    const double kernel_start = now_seconds();
    const int gen_rc = generate_sobol(z, req->num_blocks);
    if (gen_rc != 0) {
        free(z);
        return -3;
    }

    const double s0 = (double)req->s0;
    const double k = (double)req->k;
    const double r = (double)req->r;
    const double sigma = (double)req->sigma;
    const double t = (double)req->t;
    const double mu = (r - 0.5 * sigma * sigma) * t;
    const double vol = sigma * sqrt(t);
    const double df = exp(-r * t);

    double payoff_sum = 0.0;
    for (uint64_t i = 0; i < samples; ++i) {
        const double zi = (double)z[i];
        double payoff = 0.0;
        if (isfinite(zi)) {
            const double st = s0 * exp(mu + vol * zi);
            payoff = req->type == EUROPEAN_CALL ? fmax(st - k, 0.0) : fmax(k - st, 0.0);
        } else if (zi > 0.0) {
            payoff = req->type == EUROPEAN_CALL ? INFINITY : 0.0;
        } else {
            payoff = req->type == EUROPEAN_CALL ? 0.0 : k;
        }
        payoff_sum += df * payoff;
    }
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = samples;
    out->price = payoff_sum / (double)samples;
    out->coeff_setup_seconds = 0.0;
    out->kernel_seconds = kernel_end - kernel_start;

    free(z);
    return 0;
}

int european_prepare(
    const european_price_request_t* req,
    european_prepared_contract_t** prepared_out
) {
    if (!prepared_out) {
        return -1;
    }
    *prepared_out = NULL;
    if (!valid_request(req) ||
        (req->mode != EUROPEAN_MODE_GAUSSIAN_EXP_REDUCED_FMA &&
         req->mode != EUROPEAN_MODE_ORDERED_D1_MIN_FMA)) {
        return -1;
    }
    if (req->mode == EUROPEAN_MODE_ORDERED_D1_MIN_FMA) {
        const uint64_t max_points =
            (UINT64_C(1) << 32) - EUROPEAN_SOBOL_BLOCK_SIZE;
        if (req->num_blocks > max_points / EUROPEAN_SOBOL_BLOCK_SIZE) {
            return -1;
        }
        if (!valid_ordered_points_request(
                req,
                req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE)) {
            return -1;
        }
    }

    const size_t allocation_size =
        (sizeof(european_prepared_contract_t) + 63u) & ~(size_t)63u;
    european_prepared_contract_t* prepared = aligned_alloc(64u, allocation_size);
    if (!prepared) {
        return -2;
    }

    prepared->req = *req;
    build_pricing_params(req, prepared->params);
    if (req->mode == EUROPEAN_MODE_ORDERED_D1_MIN_FMA) {
        build_ordered_d1_initial_states(prepared->ordered_initial_states);
        if (req->sigma != 0.0f) {
            european_build_ordered_d1_schedule(
                prepared->params,
                prepared->combined_c0,
                prepared->combined_c1,
                &prepared->ordered_tail,
                gauss_range2047_lut);
        }
        *prepared_out = prepared;
        return 0;
    }

    const float alpha = req->sigma * sqrtf(req->t);
    prepared->reduced = alpha > 0.0f && alpha <= EUROPEAN_REDUCED_EXP_MAX_ALPHA;
    if (prepared->reduced) {
        european_build_reduced_tail_schedule(
            prepared->params, prepared->reduced_tail_schedule);
        european_build_composed_normal_schedule(
            prepared->params,
            prepared->combined_c0,
            prepared->combined_c1);
    } else {
        const int rc = build_regular_exp_schedule(
            req, prepared->params, prepared->regular_exp_schedule);
        if (rc != 0) {
            free(prepared);
            return rc;
        }
    }

    *prepared_out = prepared;
    return 0;
}

int european_price_prepared(
    const european_prepared_contract_t* prepared,
    european_price_result_t* out
) {
    if (!prepared || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    const european_price_request_t* req = &prepared->req;
    const uint64_t samples =
        req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE;
    if (req->mode == EUROPEAN_MODE_ORDERED_D1_MIN_FMA) {
        return price_ordered_d1_prepared_points_unchecked(
            prepared, samples, out);
    }
    const double kernel_start = now_seconds();
    double payoff_sum;
    if (prepared->reduced) {
        payoff_sum = price_european_sequence_reduced_fma(
            NULL,
            req->num_blocks * 2 + 1,
            european_sobol_words,
            gauss_sched_c0,
            gauss_sched_c1,
            gauss_sched_c2,
            gauss_fold_mask_2048,
            gauss_tail_c0,
            gauss_tail_c1,
            gauss_tail_c2,
            gauss_tail_c3,
            gauss_tail_c4,
            gauss_tail_c5,
            gauss_range2047_lut,
            gauss_range2047_mask,
            prepared->combined_c0,
            prepared->combined_c1,
            gauss_first_patch_zmm,
            prepared->params,
            prepared->reduced_tail_schedule);
    } else {
        payoff_sum = price_european_sequence(
            NULL,
            req->num_blocks * 2 + 1,
            european_sobol_words,
            gauss_sched_c0,
            gauss_sched_c1,
            gauss_sched_c2,
            gauss_fold_mask_2048,
            gauss_tail_c0,
            gauss_tail_c1,
            gauss_tail_c2,
            gauss_tail_c3,
            gauss_tail_c4,
            gauss_tail_c5,
            gauss_range2047_lut,
            gauss_range2047_mask,
            gauss_linear_c0,
            gauss_linear_c1,
            gauss_first_patch_zmm,
            prepared->params,
            prepared->regular_exp_schedule);
    }
    const double kernel_end = now_seconds();

    out->payoff_sum = payoff_sum;
    out->samples = samples;
    out->price = payoff_sum / (double)samples;
    out->coeff_setup_seconds = 0.0;
    out->kernel_seconds = kernel_end - kernel_start;
    return 0;
}

int european_price_prepared_points(
    const european_prepared_contract_t* prepared,
    uint64_t num_points,
    european_price_result_t* out
) {
    if (!prepared || !out ||
        prepared->req.mode != EUROPEAN_MODE_ORDERED_D1_MIN_FMA ||
        !valid_ordered_points_request(&prepared->req, num_points)) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    return price_ordered_d1_prepared_points_unchecked(
        prepared, num_points, out);
}

void european_prepared_destroy(european_prepared_contract_t* prepared) {
    free(prepared);
}

int price_european_points(
    const european_price_request_t* req,
    uint64_t num_points,
    european_price_result_t* out
) {
    if (!out || !valid_ordered_points_request(req, num_points)) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    return price_ordered_d1_points_unchecked(req, num_points, out);
}

int price_european(const european_price_request_t* req, european_price_result_t* out) {
    if (!valid_request(req) || !out) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    if (req->mode == EUROPEAN_MODE_ORDERED_D1_MIN_FMA) {
        const uint64_t max_points =
            (UINT64_C(1) << 32) - EUROPEAN_SOBOL_BLOCK_SIZE;
        if (req->num_blocks > max_points / EUROPEAN_SOBOL_BLOCK_SIZE) {
            return -1;
        }
        if (!valid_ordered_points_request(
                req,
                req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE)) {
            return -1;
        }
        return price_ordered_d1_points_unchecked(
            req,
            req->num_blocks * (uint64_t)EUROPEAN_SOBOL_BLOCK_SIZE,
            out);
    }

    if (req->mode == EUROPEAN_MODE_GAUSSIAN_EXP) {
        return price_fused_gaussian_exp(req, out);
    }
    if (req->mode == EUROPEAN_MODE_GAUSSIAN_EXP_REDUCED_FMA) {
        return price_fused_gaussian_exp_reduced_fma(req, out);
    }
    if (req->mode == EUROPEAN_MODE_GAUSSIAN_SPLIT_TAIL) {
        return price_fused_gaussian_split_tail(req, out);
    }
    if (req->mode == EUROPEAN_MODE_GAUSSIAN_CENTER_SHARED) {
        return price_fused_gaussian_center_shared(req, out);
    }
    if (req->mode == EUROPEAN_MODE_GAUSSIAN_DYNAMIC_RANGES) {
        return price_fused_gaussian_dynamic_ranges(req, out);
    }
    if (req->mode == EUROPEAN_MODE_HYBRID) {
        return price_fused_hybrid(req, out, 0);
    }
    if (req->mode == EUROPEAN_MODE_HYBRID_DIRECT_TAIL) {
        return price_fused_hybrid(req, out, 1);
    }
    if (req->mode == EUROPEAN_MODE_DIRECT_PAYOFF) {
        return price_fused_direct_payoff(req, out);
    }

    return price_buffer_reference(req, out);
}
