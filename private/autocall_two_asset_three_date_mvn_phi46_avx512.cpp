#include "private/autocall_two_asset_three_date_mvn_f64_math.h"

#include <immintrin.h>
#include <initializer_list>

namespace {

constexpr double INV_SQRT_2PI=0x1.9884533d43651p-2;

__attribute__((always_inline)) static inline __m512d expv(__m512d x) {
    const __m512d dn=_mm512_roundscale_pd(
        _mm512_fmadd_pd(x,_mm512_set1_pd(0x1.71547652b82fep+0),
                        _mm512_set1_pd(0.5)),
        _MM_FROUND_TO_NEG_INF|_MM_FROUND_NO_EXC);
    __m512d r=_mm512_fnmadd_pd(dn,_mm512_set1_pd(0x1.6300000000000p-1),x);
    r=_mm512_fnmadd_pd(dn,_mm512_set1_pd(-0x1.bd0105c610ca8p-13),r);
    const __m512d z=_mm512_mul_pd(r,r);
    __m512d p=_mm512_fmadd_pd(_mm512_set1_pd(0x1.089cdd5e44be8p-13),z,
                              _mm512_set1_pd(0x1.f06d10cca2c7ep-6));
    p=_mm512_mul_pd(r,_mm512_fmadd_pd(p,z,_mm512_set1_pd(1.0)));
    __m512d q=_mm512_fmadd_pd(_mm512_set1_pd(0x1.92eb6bc365fa0p-19),z,
                              _mm512_set1_pd(0x1.4ae39b508b6c0p-9));
    q=_mm512_fmadd_pd(q,z,_mm512_set1_pd(0x1.d17099887e074p-3));
    q=_mm512_fmadd_pd(q,z,_mm512_set1_pd(2.0));
    const __m512d y=_mm512_fmadd_pd(_mm512_set1_pd(2.0),
        _mm512_div_pd(p,_mm512_sub_pd(q,p)),_mm512_set1_pd(1.0));
    const __m512i exponent=_mm512_slli_epi64(_mm512_cvttpd_epi64(dn),52);
    return _mm512_mul_pd(y,_mm512_castsi512_pd(_mm512_add_epi64(
        _mm512_set1_epi64(0x3ff0000000000000ULL),exponent)));
}

__attribute__((always_inline)) static inline __m512d cdfv(__m512d x) {
    const __m512i sign_mask=_mm512_set1_epi64(0x7fffffffffffffffULL);
    const __m512d y=_mm512_castsi512_pd(_mm512_and_epi64(
        _mm512_castpd_si512(x),sign_mask));
    const __mmask8 negative=_mm512_cmp_pd_mask(x,_mm512_setzero_pd(),_CMP_LT_OQ);
    const __m512d exponential=expv(_mm512_mul_pd(_mm512_set1_pd(-0.5),
                                                  _mm512_mul_pd(y,y)));
    __m512d numerator=_mm512_set1_pd(0x1.20ded0b57fbddp-5);
    for (double c : {0x1.66989be8ea720p-1,0x1.97eeff2a86f23p+2,
                     0x1.0f4d8cbb02431p+5,0x1.c05131ca58d3cp+6,
                     0x1.ba6d5c7a28cf2p+7,0x1.b869ea974c7e5p+7})
        numerator=_mm512_fmadd_pd(numerator,y,_mm512_set1_pd(c));
    __m512d denominator=_mm512_set1_pd(0x1.6a09e667f3bcdp-4);
    for (double c : {0x1.c173673887d1ap+0,0x1.0106df11bd49dp+4,
                     0x1.5b1f78433a59ap+6,0x1.2890729ba7820p+8,
                     0x1.3eaab47fa1777p+9,0x1.8ce9cb298974ap+9,
                     0x1.b869ea974c7e5p+8})
        denominator=_mm512_fmadd_pd(denominator,y,_mm512_set1_pd(c));
    const __m512d tail=_mm512_div_pd(_mm512_mul_pd(exponential,numerator),
                                     denominator);
    __m512d result=_mm512_mask_blend_pd(negative,
        _mm512_sub_pd(_mm512_set1_pd(1.0),tail),tail);
    const __mmask8 far=_mm512_cmp_pd_mask(y,_mm512_set1_pd(9.0),_CMP_GE_OQ);
    result=_mm512_mask_mov_pd(result,far,_mm512_set1_pd(1.0));
    return _mm512_mask_mov_pd(result,far&negative,_mm512_setzero_pd());
}

__attribute__((always_inline)) static inline __m512d pdfv(__m512d x) {
    return _mm512_mul_pd(_mm512_set1_pd(INV_SQRT_2PI),
        expv(_mm512_mul_pd(_mm512_set1_pd(-0.5),_mm512_mul_pd(x,x))));
}

__attribute__((always_inline)) static inline __m512d conditional_bvn(
    const mvn_f64_math::Phi46TargetBatch8 &t,__m512d x0,__m512d x1,
    const double *node,const double *weight) {
    const __m512d mean0=_mm512_fmadd_pd(_mm512_load_pd(t.gain01),x1,
        _mm512_mul_pd(_mm512_load_pd(t.gain00),x0));
    const __m512d mean1=_mm512_fmadd_pd(_mm512_load_pd(t.gain11),x1,
        _mm512_mul_pd(_mm512_load_pd(t.gain10),x0));
    __m512d lower0=_mm512_mul_pd(
        _mm512_sub_pd(_mm512_load_pd(t.lower0),mean0),
        _mm512_load_pd(t.inverse_sd0));
    __m512d upper0=_mm512_mul_pd(
        _mm512_sub_pd(_mm512_load_pd(t.upper0),mean0),
        _mm512_load_pd(t.inverse_sd0));
    const __m512d lower1=_mm512_mul_pd(
        _mm512_sub_pd(_mm512_load_pd(t.lower1),mean1),
        _mm512_load_pd(t.inverse_sd1));
    const __m512d upper1=_mm512_mul_pd(
        _mm512_sub_pd(_mm512_load_pd(t.upper1),mean1),
        _mm512_load_pd(t.inverse_sd1));
    lower0=_mm512_max_pd(lower0,_mm512_set1_pd(-9.0));
    upper0=_mm512_min_pd(upper0,_mm512_set1_pd(9.0));
    const __m512d middle=_mm512_mul_pd(_mm512_set1_pd(0.5),
        _mm512_add_pd(lower0,upper0));
    const __m512d half=_mm512_mul_pd(_mm512_set1_pd(0.5),
        _mm512_sub_pd(upper0,lower0));
    const __m512d rho=_mm512_load_pd(t.rho);
    const __m512d inverse_residual=_mm512_load_pd(t.inverse_residual);
    __m512d sum=_mm512_setzero_pd();
    for (unsigned k=0;k<40u;++k) {
        const __m512d y=_mm512_fmadd_pd(half,_mm512_set1_pd(node[k]),middle);
        const __m512d lo=_mm512_mul_pd(_mm512_fnmadd_pd(rho,y,lower1),
                                       inverse_residual);
        const __m512d hi=_mm512_mul_pd(_mm512_fnmadd_pd(rho,y,upper1),
                                       inverse_residual);
        const __m512d interval=_mm512_max_pd(_mm512_setzero_pd(),
            _mm512_sub_pd(cdfv(hi),cdfv(lo)));
        sum=_mm512_fmadd_pd(_mm512_set1_pd(weight[k]),
                             _mm512_mul_pd(pdfv(y),interval),sum);
    }
    return _mm512_min_pd(_mm512_set1_pd(1.0),_mm512_max_pd(
        _mm512_setzero_pd(),_mm512_mul_pd(half,sum)));
}

template <bool TWO_TARGETS>
__attribute__((always_inline)) static inline __m512d tensor_candidate(
    const mvn_f64_math::Phi46Batch8Input *input) {
    const __m512d l0=_mm512_load_pd(input->outer_lower0);
    const __m512d u0=_mm512_load_pd(input->outer_upper0);
    const __m512d middle0=_mm512_mul_pd(_mm512_set1_pd(0.5),
                                        _mm512_add_pd(l0,u0));
    const __m512d half0=_mm512_mul_pd(_mm512_set1_pd(0.5),
                                      _mm512_sub_pd(u0,l0));
    const __m512d rho=_mm512_load_pd(input->outer_rho);
    const __m512d residual=_mm512_load_pd(input->outer_residual);
    const __m512d inverse_residual=_mm512_load_pd(input->outer_inverse_residual);
    const __m512d l1=_mm512_load_pd(input->outer_lower1);
    const __m512d u1=_mm512_load_pd(input->outer_upper1);
    __m512d total=_mm512_setzero_pd();
    for (unsigned i=0;i<40u;++i) {
        const __m512d x0=_mm512_fmadd_pd(half0,
            _mm512_set1_pd(input->node[i]),middle0);
        const __m512d vl=_mm512_max_pd(_mm512_set1_pd(-9.0),
            _mm512_mul_pd(_mm512_fnmadd_pd(rho,x0,l1),inverse_residual));
        const __m512d vu=_mm512_min_pd(_mm512_set1_pd(9.0),
            _mm512_mul_pd(_mm512_fnmadd_pd(rho,x0,u1),inverse_residual));
        const __m512d vmiddle=_mm512_mul_pd(_mm512_set1_pd(0.5),
                                            _mm512_add_pd(vl,vu));
        const __m512d vhalf=_mm512_mul_pd(_mm512_set1_pd(0.5),
                                          _mm512_sub_pd(vu,vl));
        __m512d inner=_mm512_setzero_pd();
        for (unsigned j=0;j<40u;++j) {
            const __m512d v=_mm512_fmadd_pd(vhalf,
                _mm512_set1_pd(input->node[j]),vmiddle);
            const __m512d x1=_mm512_fmadd_pd(residual,v,
                                             _mm512_mul_pd(rho,x0));
            __m512d probability=conditional_bvn(input->target[0],x0,x1,
                                                  input->node,input->weight);
            if constexpr (TWO_TARGETS)
                probability=_mm512_mul_pd(probability,conditional_bvn(
                    input->target[1],x0,x1,input->node,input->weight));
            inner=_mm512_fmadd_pd(_mm512_set1_pd(input->weight[j]),
                _mm512_mul_pd(pdfv(v),probability),inner);
        }
        total=_mm512_fmadd_pd(_mm512_set1_pd(input->weight[i]),
            _mm512_mul_pd(pdfv(x0),_mm512_mul_pd(vhalf,inner)),total);
    }
    return _mm512_min_pd(_mm512_set1_pd(1.0),_mm512_max_pd(
        _mm512_setzero_pd(),_mm512_mul_pd(half0,total)));
}

} // namespace

extern "C" __attribute__((noinline)) void mvn_phi4_batch8_tensor40_candidate(
    const mvn_f64_math::Phi46Batch8Input *input,double *output) {
    _mm512_storeu_pd(output,tensor_candidate<false>(input));
}

extern "C" __attribute__((noinline)) void mvn_phi6_batch8_bridge40_candidate(
    const mvn_f64_math::Phi46Batch8Input *input,double *output) {
    _mm512_storeu_pd(output,tensor_candidate<true>(input));
}
