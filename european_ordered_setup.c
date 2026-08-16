#include "private/european_ordered_setup.h"
#include "private/european_ordered_d1_coeffs.h"

#include <immintrin.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

static const double ordered_exp_coeff[9] = {
    1.00000000361, 0.999999559932, 0.499999873009,
    0.166670788605, 0.0416673696717, 0.00832308095835,
    0.0013875434074, 0.0002077216867, 2.58406812172e-05
};

#define ORDERED_TARGET __attribute__((target("avx512f,fma")))

_Static_assert(offsetof(european_ordered_d1_tail_context_t, fast_c0) == 64,
               "ordered tail fast_c0 ABI");
_Static_assert(offsetof(european_ordered_d1_tail_context_t, fast_c1) == 320,
               "ordered tail fast_c1 ABI");
_Static_assert(offsetof(european_ordered_d1_tail_context_t, cubic_c0) == 576,
               "ordered tail cubic_c0 ABI");
_Static_assert(offsetof(european_ordered_d1_tail_context_t, cubic_c1) == 768,
               "ordered tail cubic_c1 ABI");
_Static_assert(offsetof(european_ordered_d1_tail_context_t, cubic_c2) == 960,
               "ordered tail cubic_c2 ABI");
_Static_assert(offsetof(european_ordered_d1_tail_context_t, cubic_c3) == 1152,
               "ordered tail cubic_c3 ABI");

static double ordered_exp_poly8(double x) {
    double value = ordered_exp_coeff[8];
    for (int coefficient = 7; coefficient >= 0; --coefficient) {
        value = value * x + ordered_exp_coeff[coefficient];
    }
    return value;
}

ORDERED_TARGET
void european_build_ordered_d1_schedule(
    const float params[4],
    float* combined_c0,
    float* combined_c1,
    european_ordered_d1_tail_context_t* tail,
    const float* range2047_lut
) {
    const double alpha = (double)params[0];
    double weighted[9];
    double power = 1.0;
    for (size_t coefficient = 0; coefficient < 9; ++coefficient) {
        weighted[coefficient] = ordered_exp_coeff[coefficient] * power;
        power *= alpha;
    }

    for (size_t row = 0; row < EUROPEAN_ORDERED_D1_ROWS; ++row) {
        float payoff_slope[16] __attribute__((aligned(64)));
        float payoff_intercept[16] __attribute__((aligned(64)));
        for (size_t lane = 0; lane < EUROPEAN_ORDERED_D1_LANES; ++lane) {
            double mean_y = 0.0;
            double mean_zy = 0.0;
            for (size_t coefficient = 0; coefficient < 9; ++coefficient) {
                mean_y += weighted[coefficient] *
                    european_ordered_d1_moment[coefficient][row][lane];
                mean_zy += weighted[coefficient] *
                    european_ordered_d1_moment[coefficient + 1][row][lane];
            }
            const double mean_z = european_ordered_d1_moment[1][row][lane];
            const double variance =
                european_ordered_d1_moment[2][row][lane] - mean_z * mean_z;
            const double slope_z = (mean_zy - mean_z * mean_y) / variance;
            const double intercept_z = mean_y - slope_z * mean_z;
            payoff_slope[lane] = (float)((double)params[1] * slope_z);
            payoff_intercept[lane] = (float)(
                (double)params[1] * intercept_z + (double)params[2]);
        }
        const __m512 slope = _mm512_load_ps(payoff_slope);
        const __m512 intercept = _mm512_load_ps(payoff_intercept);
        const __m512 g0 = _mm512_load_ps(european_ordered_d1_gauss_c0[row]);
        const __m512 g1 = _mm512_load_ps(european_ordered_d1_gauss_c1[row]);
        _mm512_store_ps(
            combined_c1 + row * EUROPEAN_ORDERED_D1_LANES,
            _mm512_mul_ps(slope, g1));
        _mm512_store_ps(
            combined_c0 + row * EUROPEAN_ORDERED_D1_LANES,
            _mm512_fmadd_ps(slope, g0, intercept));
    }

    memcpy(tail->params, params, sizeof(tail->params));
    tail->range2047_lut = range2047_lut;
    memset(tail->reserved, 0, sizeof(tail->reserved));
    for (size_t lane = 0; lane < EUROPEAN_ORDERED_D1_HARD_POINTS; ++lane) {
        const size_t source = european_ordered_d1_hard_coeff_index[lane];
        tail->fast_c0[lane] = combined_c0[source];
        tail->fast_c1[lane] = combined_c1[source];
    }

    const double alpha2 = alpha * alpha;
    const double alpha3 = alpha2 * alpha;
    for (size_t lane = 0; lane < EUROPEAN_ORDERED_D1_CUBIC_FLOATS; ++lane) {
        const double lo = (double)european_ordered_d1_hard_z_lo[lane];
        const double hi = (double)european_ordered_d1_hard_z_hi[lane];
        const double mid = 0.5 * alpha * (lo + hi);
        const double mid2 = mid * mid;
        const double e_mid = ordered_exp_poly8(mid);
        tail->cubic_c3[lane] = (float)(
            (double)params[1] * e_mid * alpha3 / 6.0);
        tail->cubic_c2[lane] = (float)(
            (double)params[1] * e_mid * (0.5 - 0.5 * mid) * alpha2);
        tail->cubic_c1[lane] = (float)(
            (double)params[1] * e_mid *
            (1.0 - mid + 0.5 * mid2) * alpha);
        tail->cubic_c0[lane] = (float)(
            (double)params[1] * e_mid *
            (1.0 - mid + 0.5 * mid2 - mid2 * mid / 6.0) +
            (double)params[2]);
    }
}
