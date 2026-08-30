#include "private/autocall_two_asset_three_date_mvn_f64_math.h"

#include <algorithm>
#include <cmath>
#include <limits>

#if defined(__AVX512F__)
#include <immintrin.h>
#endif

namespace mvn_f64_math {
namespace {

constexpr double INV_SQRT_2PI=0x1.9884533d43651p-2;
constexpr double PHI2_NODE20[20]={
    -0x1.fc7b5a0c71ce1p-1,-0x1.ed8dba7bd769fp-1,-0x1.d31064173fd92p-1,
    -0x1.ada0bd5efd6e7p-1,-0x1.7e1f37346a54ep-1,-0x1.45a8d3fa710dbp-1,
    -0x1.05905c13f7ff7p-1,-0x1.7eaccf15652c4p-2,-0x1.d281636928bc0p-3,
    -0x1.3973df98b86b0p-4,0x1.3973df98b86b0p-4,0x1.d281636928bc0p-3,
    0x1.7eaccf15652c4p-2,0x1.05905c13f7ff7p-1,0x1.45a8d3fa710dbp-1,
    0x1.7e1f37346a54ep-1,0x1.ada0bd5efd6e7p-1,0x1.d31064173fd92p-1,
    0x1.ed8dba7bd769fp-1,0x1.fc7b5a0c71ce1p-1};
constexpr double PHI2_WEIGHT20[20]={
    0x1.209680274e74ep-6,0x1.4c9b5ea53b638p-5,0x1.00b467df7e461p-4,
    0x1.5519fe196e247p-4,0x1.a1817a317a834p-4,0x1.e41ff31573b56p-4,
    0x1.0db2c5db26e08p-3,0x1.230348f34a542p-3,0x1.31819b52c59a4p-3,
    0x1.38d6c490a3380p-3,0x1.38d6c490a3380p-3,0x1.31819b52c59a4p-3,
    0x1.230348f34a542p-3,0x1.0db2c5db26e08p-3,0x1.e41ff31573b56p-4,
    0x1.a1817a317a834p-4,0x1.5519fe196e247p-4,0x1.00b467df7e461p-4,
    0x1.4c9b5ea53b638p-5,0x1.209680274e74ep-6};

template <unsigned N>
static double polevl(double x,const double (&coefficient)[N]) {
    double value=coefficient[0];
    for (unsigned i=1;i<N;++i)
        value=std::fma(value,x,coefficient[i]);
    return value;
}

/*
 * Graeme West's compact Hart rational normal tail, coefficients converted
 * once to hexadecimal binary64.  Its single central rational materially
 * lowers register pressure versus evaluating separate erf/erfc families.
 */
static double compact_tail(double y) {
    const double exponential=exp_scalar(-0.5*y*y);
    static constexpr double N[7]={
        0x1.20ded0b57fbddp-5,0x1.66989be8ea720p-1,
        0x1.97eeff2a86f23p+2,0x1.0f4d8cbb02431p+5,
        0x1.c05131ca58d3cp+6,0x1.ba6d5c7a28cf2p+7,
        0x1.b869ea974c7e5p+7};
    static constexpr double D[8]={
        0x1.6a09e667f3bcdp-4,0x1.c173673887d1ap+0,
        0x1.0106df11bd49dp+4,0x1.5b1f78433a59ap+6,
        0x1.2890729ba7820p+8,0x1.3eaab47fa1777p+9,
        0x1.8ce9cb298974ap+9,0x1.b869ea974c7e5p+8};
    return exponential*polevl(y,N)/polevl(y,D);
}

} // namespace

double exp_scalar(double x) {
    if (x>0x1.62e42fefa39efp+9)
        return std::numeric_limits<double>::infinity();
    if (x<-0x1.74385446d71c3p+9)
        return 0.0;
    static constexpr double P[3]={
        0x1.089cdd5e44be8p-13,0x1.f06d10cca2c7ep-6,0x1p+0};
    static constexpr double Q[4]={
        0x1.92eb6bc365fa0p-19,0x1.4ae39b508b6c0p-9,
        0x1.d17099887e074p-3,0x1p+1};
    const double dn=std::floor(std::fma(x,0x1.71547652b82fep+0,0.5));
    double r=std::fma(-dn,0x1.6300000000000p-1,x);
    r=std::fma(-dn,-0x1.bd0105c610ca8p-13,r);
    const double z=r*r;
    const double px=r*polevl(z,P);
    const double y=std::fma(2.0,px/(polevl(z,Q)-px),1.0);
    return std::ldexp(y,static_cast<int>(dn));
}

double cdf_scalar(double x) {
    if (std::isnan(x))
        return x;
    if (x==std::numeric_limits<double>::infinity())
        return 1.0;
    if (x==-std::numeric_limits<double>::infinity())
        return 0.0;
    if (x>=9.0)
        return 1.0;
    if (x<=-9.0)
        return 0.0;
    const double tail=compact_tail(std::fabs(x));
    return x<0.0 ? tail : 1.0-tail;
}

double density_scalar(double x) {
    return INV_SQRT_2PI*exp_scalar(-0.5*x*x);
}

double phi2_scalar_order20(const Phi2Batch8Input *input,unsigned lane) {
    double sum=0.0;
    for (unsigned segment=0;segment<8u;++segment) {
        const double lower=input->segment_lower[segment][lane];
        const double upper=input->segment_upper[segment][lane];
        const double middle=0.5*(lower+upper);
        const double half=0.5*(upper-lower);
        double local=0.0;
        for (unsigned i=0;i<20u;++i) {
            const double x=std::fma(half,PHI2_NODE20[i],middle);
            const double lo=std::fma(-input->rho[lane],x,input->lower1[lane])*
                input->inverse_residual[lane];
            const double hi=std::fma(-input->rho[lane],x,input->upper1[lane])*
                input->inverse_residual[lane];
            const double interval=std::max(0.0,cdf_scalar(hi)-cdf_scalar(lo));
            local=std::fma(PHI2_WEIGHT20[i],density_scalar(x)*interval,local);
        }
        sum=std::fma(half,local,sum);
    }
    return std::clamp(sum,0.0,1.0);
}

double phi2_angle_scalar_order40(const Phi2AngleBatch8Input *input,
                                 unsigned lane) {
    double sum=0.0;
    for (unsigned i=0;i<40u;++i) {
        const double numerator=std::fma(-input->two_hk[lane],input->sine[i],
                                        input->h2_plus_k2[lane]);
        const double exponent=numerator*
            input->negative_inverse_two_cosine_squared[i];
        const double exponential=exponent<-700.0 ? 0.0 : exp_scalar(exponent);
        sum=std::fma(input->scaled_weight[i],exponential,sum);
    }
    return std::clamp(std::fma(cdf_scalar(input->h[lane]),
        cdf_scalar(input->k[lane]),sum),0.0,1.0);
}

#if defined(__AVX512F__)
namespace {

static inline __m512d polevl3(__m512d x,double a,double b,double c) {
    __m512d value=_mm512_set1_pd(a);
    value=_mm512_fmadd_pd(value,x,_mm512_set1_pd(b));
    return _mm512_fmadd_pd(value,x,_mm512_set1_pd(c));
}

static inline __m512d polevl4(__m512d x,double a,double b,double c,double d) {
    __m512d value=_mm512_set1_pd(a);
    value=_mm512_fmadd_pd(value,x,_mm512_set1_pd(b));
    value=_mm512_fmadd_pd(value,x,_mm512_set1_pd(c));
    return _mm512_fmadd_pd(value,x,_mm512_set1_pd(d));
}

static inline __m512d exp_vector(__m512d x) {
    const __m512d log2e=_mm512_set1_pd(0x1.71547652b82fep+0);
    const __m512d half=_mm512_set1_pd(0.5);
    __m512d dn=_mm512_fmadd_pd(x,log2e,half);
    dn=_mm512_roundscale_pd(dn,_MM_FROUND_TO_NEG_INF|_MM_FROUND_NO_EXC);
    __m512d r=_mm512_fmadd_pd(_mm512_sub_pd(_mm512_setzero_pd(),dn),
                              _mm512_set1_pd(0x1.6300000000000p-1),x);
    r=_mm512_fmadd_pd(_mm512_sub_pd(_mm512_setzero_pd(),dn),
                      _mm512_set1_pd(-0x1.bd0105c610ca8p-13),r);
    const __m512d z=_mm512_mul_pd(r,r);
    const __m512d p=_mm512_mul_pd(r,polevl3(z,0x1.089cdd5e44be8p-13,
        0x1.f06d10cca2c7ep-6,0x1p+0));
    const __m512d q=polevl4(z,0x1.92eb6bc365fa0p-19,
        0x1.4ae39b508b6c0p-9,0x1.d17099887e074p-3,0x1p+1);
    const __m512d y=_mm512_fmadd_pd(_mm512_set1_pd(2.0),
        _mm512_div_pd(p,_mm512_sub_pd(q,p)),_mm512_set1_pd(1.0));
    const __m512i exponent=_mm512_slli_epi64(_mm512_cvttpd_epi64(dn),52);
    const __m512i one=_mm512_set1_epi64(0x3ff0000000000000ULL);
    return _mm512_mul_pd(y,_mm512_castsi512_pd(_mm512_add_epi64(one,exponent)));
}

static inline __m512d cdf_vector(__m512d x) {
    static constexpr double N[7]={0x1.20ded0b57fbddp-5,
        0x1.66989be8ea720p-1,0x1.97eeff2a86f23p+2,0x1.0f4d8cbb02431p+5,
        0x1.c05131ca58d3cp+6,0x1.ba6d5c7a28cf2p+7,0x1.b869ea974c7e5p+7};
    static constexpr double D[8]={0x1.6a09e667f3bcdp-4,
        0x1.c173673887d1ap+0,0x1.0106df11bd49dp+4,0x1.5b1f78433a59ap+6,
        0x1.2890729ba7820p+8,0x1.3eaab47fa1777p+9,0x1.8ce9cb298974ap+9,
        0x1.b869ea974c7e5p+8};
    const __m512i sign_mask=_mm512_set1_epi64(0x7fffffffffffffffULL);
    const __m512d y=_mm512_castsi512_pd(_mm512_and_epi64(
        _mm512_castpd_si512(x),sign_mask));
    const __mmask8 negative=_mm512_cmp_pd_mask(x,_mm512_setzero_pd(),_CMP_LT_OQ);
    const __m512d exponential=exp_vector(_mm512_mul_pd(_mm512_set1_pd(-0.5),
                                                        _mm512_mul_pd(y,y)));
    __m512d numerator=_mm512_set1_pd(N[0]);
    __m512d denominator=_mm512_set1_pd(D[0]);
    for (unsigned i=1;i<7u;++i) {
        numerator=_mm512_fmadd_pd(numerator,y,_mm512_set1_pd(N[i]));
        denominator=_mm512_fmadd_pd(denominator,y,_mm512_set1_pd(D[i]));
    }
    denominator=_mm512_fmadd_pd(denominator,y,_mm512_set1_pd(D[7]));
    const __m512d rational=_mm512_div_pd(_mm512_mul_pd(exponential,numerator),
                                         denominator);
    const __m512d tail=rational;
    __m512d result=_mm512_mask_blend_pd(negative,
        _mm512_sub_pd(_mm512_set1_pd(1.0),tail),tail);
    const __mmask8 far=_mm512_cmp_pd_mask(
        y,_mm512_set1_pd(9.0),_CMP_GE_OQ);
    result=_mm512_mask_mov_pd(result,far,_mm512_set1_pd(1.0));
    result=_mm512_mask_mov_pd(result,far&negative,_mm512_setzero_pd());
    return result;
}

} // namespace

__attribute__((noinline))
void probe_batch8(const double *input,double *cdf,double *density,
                  double *exponential) {
    const __m512d x=_mm512_loadu_pd(input);
    _mm512_storeu_pd(cdf,cdf_vector(x));
    const __m512d p=_mm512_mul_pd(_mm512_set1_pd(INV_SQRT_2PI),
        exp_vector(_mm512_mul_pd(_mm512_set1_pd(-0.5),_mm512_mul_pd(x,x))));
    _mm512_storeu_pd(density,p);
    _mm512_storeu_pd(exponential,exp_vector(x));
}


__attribute__((noinline))
void phi2_batch8_order20(const Phi2Batch8Input *input,double *output) {
    const __m512d rho=_mm512_load_pd(input->rho);
    const __m512d inverse_residual=_mm512_load_pd(input->inverse_residual);
    const __m512d lower1=_mm512_load_pd(input->lower1);
    const __m512d upper1=_mm512_load_pd(input->upper1);
    __m512d sum=_mm512_setzero_pd();
    for (unsigned segment=0;segment<8u;++segment) {
        const __m512d lower=_mm512_load_pd(input->segment_lower[segment]);
        const __m512d upper=_mm512_load_pd(input->segment_upper[segment]);
        const __m512d middle=_mm512_mul_pd(_mm512_set1_pd(0.5),
                                           _mm512_add_pd(lower,upper));
        const __m512d half=_mm512_mul_pd(_mm512_set1_pd(0.5),
                                         _mm512_sub_pd(upper,lower));
        __m512d local=_mm512_setzero_pd();
        for (unsigned i=0;i<20u;++i) {
            const __m512d x=_mm512_fmadd_pd(half,
                _mm512_set1_pd(PHI2_NODE20[i]),middle);
            const __m512d lo=_mm512_mul_pd(_mm512_fnmadd_pd(rho,x,lower1),
                                          inverse_residual);
            const __m512d hi=_mm512_mul_pd(_mm512_fnmadd_pd(rho,x,upper1),
                                          inverse_residual);
            const __m512d interval=_mm512_max_pd(_mm512_setzero_pd(),
                _mm512_sub_pd(cdf_vector(hi),cdf_vector(lo)));
            local=_mm512_fmadd_pd(_mm512_set1_pd(PHI2_WEIGHT20[i]),
                _mm512_mul_pd(_mm512_mul_pd(_mm512_set1_pd(INV_SQRT_2PI),
                    exp_vector(_mm512_mul_pd(_mm512_set1_pd(-0.5),
                        _mm512_mul_pd(x,x)))),interval),local);
        }
        sum=_mm512_fmadd_pd(half,local,sum);
    }
    const __m512d result=_mm512_min_pd(_mm512_set1_pd(1.0),
                                       _mm512_max_pd(_mm512_setzero_pd(),sum));
    _mm512_storeu_pd(output,result);
}
#else
void probe_batch8(const double *input,double *cdf,double *density,
                  double *exponential) {
    for (unsigned i=0;i<8u;++i) {
        cdf[i]=cdf_scalar(input[i]);
        density[i]=density_scalar(input[i]);
        exponential[i]=exp_scalar(input[i]);
    }
}

void phi2_batch8_order20(const Phi2Batch8Input *input,double *output) {
    for (unsigned i=0;i<8u;++i)
        output[i]=phi2_scalar_order20(input,i);
}
#endif

} // namespace mvn_f64_math
