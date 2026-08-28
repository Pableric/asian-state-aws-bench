#include "private/autocall_two_asset_three_date_worstof_raw_diag.h"
#include "private/asian_genuine_permute.h"

#include <immintrin.h>
#include <stdint.h>

static inline void generic_growth(
    const asian_genuine_route_t *route, unsigned packet,
    __m512 *low, __m512 *high) {
    const fragment_map_t *map = route->map;
    const float *low_line = route->growth_base +
        (size_t)map->select[packet][0] * 16u;
    const float *high_line = route->growth_base +
        (size_t)map->select[packet][1] * 16u;
    const __m512i low_control = _mm512_load_si512(
        (const void *)map->patterns[map->select[packet][2]]);
    const __m512i high_control = _mm512_load_si512(
        (const void *)map->patterns[map->select[packet][3]]);
    *low = _mm512_permutexvar_ps(low_control, _mm512_load_ps(low_line));
    *high = _mm512_permutexvar_ps(high_control, _mm512_load_ps(high_line));
}

static inline void event(
    const autocall_worstof_date_record_t *date,
    __m512 *spot_a_low, __m512 *spot_a_high,
    __m512 *spot_b_low, __m512 *spot_b_high,
    __m512 *pv_low, __m512 *pv_high,
    __mmask16 *alive_low, __mmask16 *alive_high) {
    const __mmask16 coupon_low = *alive_low &
        _mm512_cmp_ps_mask(*spot_a_low, _mm512_set1_ps(date->coupon_a),
                           _CMP_GE_OQ) &
        _mm512_cmp_ps_mask(*spot_b_low, _mm512_set1_ps(date->coupon_b),
                           _CMP_GE_OQ);
    const __mmask16 coupon_high = *alive_high &
        _mm512_cmp_ps_mask(*spot_a_high, _mm512_set1_ps(date->coupon_a),
                           _CMP_GE_OQ) &
        _mm512_cmp_ps_mask(*spot_b_high, _mm512_set1_ps(date->coupon_b),
                           _CMP_GE_OQ);
    *pv_low = _mm512_mask_add_ps(*pv_low, coupon_low, *pv_low,
                                 _mm512_set1_ps(date->discounted_coupon));
    *pv_high = _mm512_mask_add_ps(*pv_high, coupon_high, *pv_high,
                                  _mm512_set1_ps(date->discounted_coupon));
    const __mmask16 call_low = *alive_low &
        _mm512_cmp_ps_mask(*spot_a_low, _mm512_set1_ps(date->call_a),
                           _CMP_GE_OQ) &
        _mm512_cmp_ps_mask(*spot_b_low, _mm512_set1_ps(date->call_b),
                           _CMP_GE_OQ);
    const __mmask16 call_high = *alive_high &
        _mm512_cmp_ps_mask(*spot_a_high, _mm512_set1_ps(date->call_a),
                           _CMP_GE_OQ) &
        _mm512_cmp_ps_mask(*spot_b_high, _mm512_set1_ps(date->call_b),
                           _CMP_GE_OQ);
    *pv_low = _mm512_mask_add_ps(*pv_low, call_low, *pv_low,
                                 _mm512_set1_ps(date->discounted_call));
    *pv_high = _mm512_mask_add_ps(*pv_high, call_high, *pv_high,
                                  _mm512_set1_ps(date->discounted_call));
    *alive_low &= (__mmask16)~call_low;
    *alive_high &= (__mmask16)~call_high;
}

double autocall_worstof_generic_vector_price_test(
    const autocall_worstof_prepared_leaf_context_t *context) {
    const asian_genuine_route_t *route_d3 =
        (const asian_genuine_route_t *)(const void *)context->asset_a_d3;
    const asian_genuine_route_t *route_d5 =
        (const asian_genuine_route_t *)(const void *)context->asset_a_d5;
    __m512 total_low = _mm512_setzero_ps();
    __m512 total_high = _mm512_setzero_ps();
    const __m512 initial_a = _mm512_set1_ps(context->spot_a);
    const __m512 initial_b = _mm512_set1_ps(context->spot_b);
    for (unsigned packet = 0; packet < 128u; ++packet) {
        const size_t offset = (size_t)packet * 32u;
        __m512 sa_low = initial_a;
        __m512 sa_high = initial_a;
        __m512 sb_low = initial_b;
        __m512 sb_high = initial_b;
        __m512 pv_low = _mm512_setzero_ps();
        __m512 pv_high = _mm512_setzero_ps();
        __mmask16 alive_low = UINT16_MAX;
        __mmask16 alive_high = UINT16_MAX;
        __m512 ga_low = _mm512_load_ps(context->asset_a_d1 + offset);
        __m512 ga_high = _mm512_load_ps(context->asset_a_d1 + offset + 16u);
        __m512 gb_low = _mm512_load_ps(context->asset_b_dates + offset);
        __m512 gb_high = _mm512_load_ps(context->asset_b_dates + offset + 16u);
        sa_low = _mm512_mul_ps(sa_low, ga_low);
        sa_high = _mm512_mul_ps(sa_high, ga_high);
        sb_low = _mm512_mul_ps(sb_low, gb_low);
        sb_high = _mm512_mul_ps(sb_high, gb_high);
        event(&context->date[0], &sa_low, &sa_high, &sb_low, &sb_high,
              &pv_low, &pv_high, &alive_low, &alive_high);

        generic_growth(route_d3, packet, &ga_low, &ga_high);
        gb_low = _mm512_load_ps(context->asset_b_dates + 4096u + offset);
        gb_high = _mm512_load_ps(context->asset_b_dates + 4096u + offset + 16u);
        sa_low = _mm512_mul_ps(sa_low, ga_low);
        sa_high = _mm512_mul_ps(sa_high, ga_high);
        sb_low = _mm512_mul_ps(sb_low, gb_low);
        sb_high = _mm512_mul_ps(sb_high, gb_high);
        event(&context->date[1], &sa_low, &sa_high, &sb_low, &sb_high,
              &pv_low, &pv_high, &alive_low, &alive_high);

        generic_growth(route_d5, packet, &ga_low, &ga_high);
        gb_low = _mm512_load_ps(context->asset_b_dates + 8192u + offset);
        gb_high = _mm512_load_ps(context->asset_b_dates + 8192u + offset + 16u);
        sa_low = _mm512_mul_ps(sa_low, ga_low);
        sa_high = _mm512_mul_ps(sa_high, ga_high);
        sb_low = _mm512_mul_ps(sb_low, gb_low);
        sb_high = _mm512_mul_ps(sb_high, gb_high);
        event(&context->date[2], &sa_low, &sa_high, &sb_low, &sb_high,
              &pv_low, &pv_high, &alive_low, &alive_high);

        const __mmask16 protected_low = alive_low &
            _mm512_cmp_ps_mask(sa_low,
                _mm512_set1_ps(context->protection_a), _CMP_GE_OQ) &
            _mm512_cmp_ps_mask(sb_low,
                _mm512_set1_ps(context->protection_b), _CMP_GE_OQ);
        const __mmask16 protected_high = alive_high &
            _mm512_cmp_ps_mask(sa_high,
                _mm512_set1_ps(context->protection_a), _CMP_GE_OQ) &
            _mm512_cmp_ps_mask(sb_high,
                _mm512_set1_ps(context->protection_b), _CMP_GE_OQ);
        pv_low = _mm512_mask_add_ps(pv_low, protected_low, pv_low,
                                    _mm512_set1_ps(context->discounted_terminal));
        pv_high = _mm512_mask_add_ps(pv_high, protected_high, pv_high,
                                     _mm512_set1_ps(context->discounted_terminal));
        const __mmask16 downside_low = alive_low & (__mmask16)~protected_low;
        const __mmask16 downside_high = alive_high & (__mmask16)~protected_high;
        __m512 worst_low = _mm512_min_ps(
            _mm512_mul_ps(sa_low, _mm512_set1_ps(context->inverse_spot_a)),
            _mm512_mul_ps(sb_low, _mm512_set1_ps(context->inverse_spot_b)));
        __m512 worst_high = _mm512_min_ps(
            _mm512_mul_ps(sa_high, _mm512_set1_ps(context->inverse_spot_a)),
            _mm512_mul_ps(sb_high, _mm512_set1_ps(context->inverse_spot_b)));
        worst_low = _mm512_mul_ps(
            worst_low, _mm512_set1_ps(context->discounted_terminal));
        worst_high = _mm512_mul_ps(
            worst_high, _mm512_set1_ps(context->discounted_terminal));
        pv_low = _mm512_mask_add_ps(pv_low, downside_low, pv_low, worst_low);
        pv_high = _mm512_mask_add_ps(pv_high, downside_high, pv_high, worst_high);
        total_low = _mm512_add_ps(total_low, pv_low);
        total_high = _mm512_add_ps(total_high, pv_high);
    }
    const __m512 total = _mm512_add_ps(total_low, total_high);
    __m128 q0 = _mm512_castps512_ps128(total);
    const __m128 q1 = _mm512_extractf32x4_ps(total, 1);
    __m128 q2 = _mm512_extractf32x4_ps(total, 2);
    const __m128 q3 = _mm512_extractf32x4_ps(total, 3);
    q0 = _mm_add_ps(q0, q1);
    q2 = _mm_add_ps(q2, q3);
    q0 = _mm_add_ps(q0, q2);
    __m128 high = _mm_movehl_ps(q0, q0);
    q0 = _mm_add_ps(q0, high);
    high = _mm_shuffle_ps(q0, q0, 1);
    q0 = _mm_add_ss(q0, high);
    return (double)_mm_cvtss_f32(q0) * context->inverse_paths;
}
