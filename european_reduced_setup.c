#include "private/european_reduced_setup.h"
#include "private/gaussian_dynamic_range_coeff_values.h"
#include "european_exp_table_64.h"
#include "european_reduced_exp_slots.h"

#include <immintrin.h>
#include <stddef.h>

static const double reduced_exp_coeff[9] = {
    1.00000000361, 0.999999559932, 0.499999873009,
    0.166670788605, 0.0416673696717, 0.00832308095835,
    0.0013875434074, 0.0002077216867, 2.58406812172e-05
};

#define SETUP_TARGET __attribute__((target("avx512f,avx2,fma")))

SETUP_TARGET static __m512d exp_poly8_pd(__m512d x) {
    __m512d y = _mm512_set1_pd(reduced_exp_coeff[8]);
    for (int coefficient = 7; coefficient >= 0; --coefficient) {
        y = _mm512_fmadd_pd(y, x, _mm512_set1_pd(reduced_exp_coeff[coefficient]));
    }
    return y;
}

SETUP_TARGET static __m256i exp_bucket8(__m256 zmid, float alpha) {
    const __m256 x = _mm256_mul_ps(_mm256_set1_ps(alpha), zmid);
    const __m256 scaled = _mm256_mul_ps(
        _mm256_sub_ps(x, _mm256_set1_ps(EUROPEAN_EXP_DOMAIN_LO)),
        _mm256_set1_ps(EUROPEAN_EXP_BUCKET_SCALE));
    __m256i bucket = _mm256_cvttps_epi32(scaled);
    bucket = _mm256_max_epi32(bucket, _mm256_setzero_si256());
    return _mm256_min_epi32(bucket, _mm256_set1_epi32(EUROPEAN_EXP_BUCKETS - 1));
}

SETUP_TARGET static __m512d gather_bucket_pd(const float* table, __m256i bucket) {
    return _mm512_cvtps_pd(_mm256_i32gather_ps(table, bucket, 4));
}

SETUP_TARGET static __m512d baseline_for_half(
    size_t half,
    size_t pair,
    __m512d alpha,
    __m512d alpha2,
    float alpha_f
) {
    const __m256 zmid = _mm256_load_ps(
        &european_reduced_pair_zmid[half][pair]);
    const __m256i bucket = exp_bucket8(zmid, alpha_f);
    const __m512d c0 = gather_bucket_pd(european_exp64_c0, bucket);
    const __m512d c1 = gather_bucket_pd(european_exp64_c1, bucket);
    const __m512d c2 = gather_bucket_pd(european_exp64_c2, bucket);
    const __m512d mean_z = _mm512_load_pd(
        &european_reduced_pair_slot_mean_z[half][pair]);
    const __m512d mean_z2 = _mm512_load_pd(
        &european_reduced_pair_slot_mean_z2[half][pair]);
    __m512d baseline = _mm512_fmadd_pd(
        c1, _mm512_mul_pd(alpha, mean_z), c0);
    return _mm512_fmadd_pd(
        c2, _mm512_mul_pd(alpha2, mean_z2), baseline);
}

SETUP_TARGET EUROPEAN_SETUP_INTERNAL
void european_build_reduced_tail_schedule(
    const float params[4],
    float* schedule
) {
    const __m512d alpha = _mm512_set1_pd((double)params[0]);
    const __m512d alpha2 = _mm512_mul_pd(alpha, alpha);
    const __m512d alpha3 = _mm512_mul_pd(alpha2, alpha);
    const __m512d payoff_scale = _mm512_set1_pd((double)params[1]);
    const __m512d payoff_beta = _mm512_set1_pd((double)params[2]);
    const __m512d half = _mm512_set1_pd(0.5);
    const __m512d one = _mm512_set1_pd(1.0);
    const __m512d sixth = _mm512_set1_pd(1.0 / 6.0);

    for (size_t base = 0; base < EUROPEAN_REDUCED_EXP_TAIL_STRIDE; base += 8) {
        const __m512d lo = _mm512_cvtps_pd(
            _mm256_load_ps(european_reduced_tail_lo + base));
        const __m512d hi = _mm512_cvtps_pd(
            _mm256_load_ps(european_reduced_tail_hi + base));
        const __m512d mid = _mm512_mul_pd(
            half, _mm512_mul_pd(alpha, _mm512_add_pd(lo, hi)));
        const __m512d mid2 = _mm512_mul_pd(mid, mid);
        const __m512d e_mid = exp_poly8_pd(mid);
        const __m512d c3 = _mm512_mul_pd(e_mid, sixth);
        const __m512d c2 = _mm512_mul_pd(
            e_mid, _mm512_mul_pd(half, _mm512_sub_pd(one, mid)));
        const __m512d c1 = _mm512_mul_pd(
            e_mid,
            _mm512_add_pd(_mm512_sub_pd(one, mid), _mm512_mul_pd(half, mid2)));
        const __m512d c0 = _mm512_mul_pd(
            e_mid,
            _mm512_sub_pd(
                _mm512_add_pd(
                    _mm512_sub_pd(one, mid),
                    _mm512_mul_pd(half, mid2)),
                _mm512_mul_pd(_mm512_mul_pd(mid2, mid), sixth)));

        _mm256_store_ps(
            schedule + base,
            _mm512_cvtpd_ps(_mm512_mul_pd(payoff_scale, _mm512_mul_pd(c3, alpha3))));
        _mm256_store_ps(
            schedule + EUROPEAN_REDUCED_EXP_TAIL_STRIDE + base,
            _mm512_cvtpd_ps(_mm512_mul_pd(payoff_scale, _mm512_mul_pd(c2, alpha2))));
        _mm256_store_ps(
            schedule + 2 * EUROPEAN_REDUCED_EXP_TAIL_STRIDE + base,
            _mm512_cvtpd_ps(_mm512_mul_pd(payoff_scale, _mm512_mul_pd(c1, alpha))));
        _mm256_store_ps(
            schedule + 3 * EUROPEAN_REDUCED_EXP_TAIL_STRIDE + base,
            _mm512_cvtpd_ps(_mm512_fmadd_pd(payoff_scale, c0, payoff_beta)));
    }
}

SETUP_TARGET EUROPEAN_SETUP_INTERNAL
void european_build_composed_normal_schedule(
    const float params[4],
    float* combined_c0,
    float* combined_c1
) {
    const double alpha_scalar = (double)params[0];
    const __m512d alpha = _mm512_set1_pd(alpha_scalar);
    const __m512d alpha2 = _mm512_mul_pd(alpha, alpha);
    const __m512d payoff_scale = _mm512_set1_pd((double)params[1]);
    const __m512d payoff_beta = _mm512_set1_pd((double)params[2]);
    const __m512d half = _mm512_set1_pd(0.5);
    double weighted[9] __attribute__((aligned(64)));
    double power = 1.0;
    for (size_t coefficient = 0; coefficient < 9; ++coefficient) {
        weighted[coefficient] = reduced_exp_coeff[coefficient] * power;
        power *= alpha_scalar;
    }

    for (size_t pair = 0; pair < 128; pair += 8) {
        __m512d mean_y = _mm512_setzero_pd();
        __m512d mean_zy = _mm512_setzero_pd();
        for (size_t coefficient = 0; coefficient < 9; ++coefficient) {
            const __m512d weight = _mm512_set1_pd(weighted[coefficient]);
            mean_y = _mm512_fmadd_pd(
                weight,
                _mm512_load_pd(&european_reduced_pair_moment[coefficient][pair]),
                mean_y);
            mean_zy = _mm512_fmadd_pd(
                weight,
                _mm512_load_pd(&european_reduced_pair_moment[coefficient + 1][pair]),
                mean_zy);
        }
        const __m512d mean_z =
            _mm512_load_pd(&european_reduced_pair_moment[1][pair]);
        const __m512d variance = _mm512_fnmadd_pd(
            mean_z,
            mean_z,
            _mm512_load_pd(&european_reduced_pair_moment[2][pair]));
        const __m512d numerator =
            _mm512_fnmadd_pd(mean_z, mean_y, mean_zy);
        const __m512d slope_z = _mm512_div_pd(numerator, variance);
        const __m512d baseline = _mm512_mul_pd(
            half,
            _mm512_add_pd(
                baseline_for_half(0, pair, alpha, alpha2, params[0]),
                baseline_for_half(1, pair, alpha, alpha2, params[0])));
        const __m512d intercept_z =
            _mm512_fnmadd_pd(slope_z, mean_z, baseline);
        const __m256 payoff_slope = _mm512_cvtpd_ps(
            _mm512_mul_pd(payoff_scale, slope_z));
        const __m256 payoff_intercept = _mm512_cvtpd_ps(
            _mm512_fmadd_pd(payoff_scale, intercept_z, payoff_beta));
        float slopes[8] __attribute__((aligned(32)));
        float intercepts[8] __attribute__((aligned(32)));
        _mm256_store_ps(slopes, payoff_slope);
        _mm256_store_ps(intercepts, payoff_intercept);

        for (size_t lane = 0; lane < 8; ++lane) {
            const size_t output_pair = pair + lane;
            const __m512 slope = _mm512_set1_ps(slopes[lane]);
            const __m512 intercept = _mm512_set1_ps(intercepts[lane]);
            const __m512 g0 = _mm512_load_ps(&gauss_dynamic_c0[output_pair][0]);
            const __m512 g1 = _mm512_load_ps(&gauss_dynamic_c1[output_pair][0]);
            _mm512_store_ps(
                combined_c1 + output_pair * 32,
                _mm512_mul_ps(slope, g1));
            _mm512_store_ps(
                combined_c0 + output_pair * 32,
                _mm512_fmadd_ps(slope, g0, intercept));
        }
    }
}

#ifdef EUROPEAN_SETUP_TEST_REFERENCE
static double exp_poly8_scalar(double x) {
    double value = reduced_exp_coeff[8];
    for (int coefficient = 7; coefficient >= 0; --coefficient) {
        value = value * x + reduced_exp_coeff[coefficient];
    }
    return value;
}

static int exp_bucket_scalar(float x) {
    int bucket = (int)((x - EUROPEAN_EXP_DOMAIN_LO) * EUROPEAN_EXP_BUCKET_SCALE);
    if (bucket < 0) {
        return 0;
    }
    if (bucket >= EUROPEAN_EXP_BUCKETS) {
        return EUROPEAN_EXP_BUCKETS - 1;
    }
    return bucket;
}

void european_build_reduced_tail_schedule_scalar(
    const float params[4],
    float* schedule
) {
    const double alpha = (double)params[0];
    const double alpha2 = alpha * alpha;
    const double alpha3 = alpha2 * alpha;
    for (size_t slot = 0; slot < EUROPEAN_REDUCED_EXP_TAIL_STRIDE; ++slot) {
        const double mid = 0.5 * alpha * (
            (double)european_reduced_tail_lo[slot] +
            (double)european_reduced_tail_hi[slot]);
        const double mid2 = mid * mid;
        const double e_mid = exp_poly8_scalar(mid);
        const double c3 = e_mid / 6.0;
        const double c2 = e_mid * (0.5 - 0.5 * mid);
        const double c1 = e_mid * (1.0 - mid + 0.5 * mid2);
        const double c0 = e_mid * (
            1.0 - mid + 0.5 * mid2 - mid2 * mid / 6.0);
        schedule[slot] = (float)((double)params[1] * c3 * alpha3);
        schedule[EUROPEAN_REDUCED_EXP_TAIL_STRIDE + slot] =
            (float)((double)params[1] * c2 * alpha2);
        schedule[2 * EUROPEAN_REDUCED_EXP_TAIL_STRIDE + slot] =
            (float)((double)params[1] * c1 * alpha);
        schedule[3 * EUROPEAN_REDUCED_EXP_TAIL_STRIDE + slot] =
            (float)((double)params[1] * c0 + (double)params[2]);
    }
}

SETUP_TARGET void european_build_composed_normal_schedule_scalar(
    const float params[4],
    float* combined_c0,
    float* combined_c1
) {
    const double alpha = (double)params[0];
    const double alpha2 = alpha * alpha;
    double weighted[9];
    double power = 1.0;
    for (size_t coefficient = 0; coefficient < 9; ++coefficient) {
        weighted[coefficient] = reduced_exp_coeff[coefficient] * power;
        power *= alpha;
    }
    for (size_t pair = 0; pair < 128; ++pair) {
        double mean_y = 0.0;
        double mean_zy = 0.0;
        for (size_t coefficient = 0; coefficient < 9; ++coefficient) {
            mean_y += weighted[coefficient] *
                european_reduced_pair_moment[coefficient][pair];
            mean_zy += weighted[coefficient] *
                european_reduced_pair_moment[coefficient + 1][pair];
        }
        const double mean_z = european_reduced_pair_moment[1][pair];
        const double variance =
            european_reduced_pair_moment[2][pair] - mean_z * mean_z;
        const double slope_z = (mean_zy - mean_z * mean_y) / variance;
        double baseline = 0.0;
        for (size_t half_index = 0; half_index < 2; ++half_index) {
            const int bucket = exp_bucket_scalar(
                params[0] * european_reduced_pair_zmid[half_index][pair]);
            const double slot_mean_z =
                european_reduced_pair_slot_mean_z[half_index][pair];
            const double slot_mean_z2 =
                european_reduced_pair_slot_mean_z2[half_index][pair];
            baseline += 0.5 * (
                (double)european_exp64_c0[bucket] +
                (double)european_exp64_c1[bucket] * alpha * slot_mean_z +
                (double)european_exp64_c2[bucket] * alpha2 * slot_mean_z2);
        }
        const float payoff_slope = (float)((double)params[1] * slope_z);
        const float payoff_intercept = (float)(
            (double)params[1] * (baseline - slope_z * mean_z) +
            (double)params[2]);
        const __m512 slope = _mm512_set1_ps(payoff_slope);
        const __m512 intercept = _mm512_set1_ps(payoff_intercept);
        const __m512 g0 = _mm512_load_ps(&gauss_dynamic_c0[pair][0]);
        const __m512 g1 = _mm512_load_ps(&gauss_dynamic_c1[pair][0]);
        _mm512_store_ps(
            combined_c1 + pair * 32, _mm512_mul_ps(slope, g1));
        _mm512_store_ps(
            combined_c0 + pair * 32,
            _mm512_fmadd_ps(slope, g0, intercept));
    }
}
#endif
