#define _POSIX_C_SOURCE 200112L
#include "private/autocall_single_asset_three_date_d1_native_diag.h"
#include "tests/autocall_single_asset_three_date_greek_cases.h"
#include "tests/autocall_single_asset_three_date_d1_native_selection.h"

#include <immintrin.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void autocall_d1_native_test_leaf_calls_reset(void);
uint64_t autocall_d1_native_test_leaf_calls(void);

static float fadd(float a,float b){volatile float x=a+b;return x;}
static float fmul(float a,float b){volatile float x=a*b;return x;}

static float exp_exact(float x)
{
    const float log2e=0x1.715476p+0f,hi=0x1.63p-1f,lo=-0x1.bd0106p-13f;
    const float p[9]={0x1p+0f,0x1.fffff2p-1f,0x1.fffff8p-2f,
      0x1.55577ep-3f,0x1.5556cep-5f,0x1.10bb1p-7f,0x1.6bbc76p-10f,
      0x1.b39fbcp-13f,0x1.b188e2p-16f};
    const float n=nearbyintf(fmul(x,log2e));
    float r=fmaf(-n,hi,x);r=fmaf(-n,lo,r);float y=p[8];
    for(int i=7;i>=0;--i)y=fmaf(r,y,p[i]);
    return scalbnf(y,(int)n);
}

static float rcp14(float x)
{
    __m128 z=_mm_setzero_ps(),v=_mm_set_ss(x);
    v=_mm_rcp14_ss(z,v);return _mm_cvtss_f32(v);
}

static float phi_exact(float input)
{
    static const float P[4]={0x1p-1f,0x1.8ab7e8p+0f,
      0x1.2a53c6p+1f,-0x1.de73dap-5f};
    static const float Q[4]={0x1p+0f,0x1.2ef076p+3f,
      0x1.08af3p+5f,0x1.635354p+5f};
    const int negative=signbit(input);float x=fabsf(input);
    const int tail=x>=8.0f;if(x>8.0f)x=8.0f;
    const float u=fmul(x,0x1p-3f);
    float n=P[3];for(int i=2;i>=0;--i)n=fmaf(u,n,P[i]);
    float d=Q[3];for(int i=2;i>=0;--i)d=fmaf(u,d,Q[i]);
    float r=rcp14(d);r=fmul(r,fadd(2.0f,-fmul(d,r)));
    const float earg=fmul(-.5f,fmul(x,x));
    const float en=nearbyintf(fmul(earg,0x1.715476p+0f));
    float er=fmaf(-en,0x1.62e43p-1f,earg);
    const float ep[7]={0x1p+0f,0x1.fffff2p-1f,0x1.fffff8p-2f,
      0x1.55577ep-3f,0x1.5556cep-5f,0x1.10bb1p-7f,0x1.6bbc76p-10f};
    float ey=ep[6];for(int i=5;i>=0;--i)ey=fmaf(er,ey,ep[i]);
    const float cdf_exp=scalbnf(ey,(int)en);
    float t=fmul(fmul(n,r),cdf_exp);
    float out=negative?t:fadd(1.0f,-t);
    if(tail) out=negative?0.0f:1.0f;
    return out;
}

static uint32_t source(const asian_meta_dim_affine_ctx_t *m,uint32_t path)
{
    const uint32_t packet=path>>5,half=(path>>4)&1u,lane=path&15u;
    uint32_t control=m->base_control[lane]^m->sel2[packet][1];
    if(half)control^=m->half_delta[lane];
    return ((uint32_t)(m->sel2[packet][0]^half)<<4)|control;
}

static void scalar_prices(const autocall_d1_native_leaf_context_t *c,double out[5])
{
    float lo[5][16]={{0}},hi[5][16]={{0}};
    for(uint32_t path=0;path<c->path_count;++path) {
        const float z3=c->direct_d1[path];
        const float z2=c->d2_donor[source(c->d2_map,path)];
        const float sum=fadd(z2,z3);
        float residual[3];
        residual[0]=exp_exact(fmul(c->leg[1].residual_a,sum));
        residual[1]=exp_exact(fmul(c->leg[3].residual_a,sum));
        residual[2]=exp_exact(fmul(c->leg[4].residual_a,sum));
        for(unsigned l=0;l<5;++l) {
            const autocall_d1_native_leg_record_t *r=&c->leg[l];
            float pv=0,prior=r->prior_threshold,pcdf=r->prior_survival_cdf;
            float q=phi_exact(fadd(r->coupon2_intercept,-z2));
            float p=fmaxf(0.0f,fadd(pcdf,-q));pv=fmaf(c->discounted_coupon2,p,pv);
            float call=fadd(r->call2_intercept,-z2);prior=fminf(prior,call);
            q=phi_exact(call);p=fmaxf(0.0f,fadd(pcdf,-q));
            pv=fmaf(c->discounted_call2,p,pv);pcdf=fminf(pcdf,q);
            q=phi_exact(fadd(r->coupon3_intercept,-sum));
            p=fmaxf(0.0f,fadd(pcdf,-q));pv=fmaf(c->discounted_coupon3,p,pv);
            call=fadd(r->call3_intercept,-sum);prior=fminf(prior,call);
            q=phi_exact(call);p=fmaxf(0.0f,fadd(pcdf,-q));
            pv=fmaf(c->discounted_call3,p,pv);pcdf=fminf(pcdf,q);
            float prot=fadd(r->protection_intercept,-sum);prior=fminf(prior,prot);
            q=phi_exact(prot);p=fmaxf(0.0f,fadd(pcdf,-q));
            pv=fmaf(c->discounted_terminal_notional,p,pv);
            q=phi_exact(fadd(prior,-r->residual_a));
            const float e=residual[l<3?0:l-2];
            pv=fmaf(r->downside_scale,fmul(e,q),pv);
            float *acc=(path&16u)?hi[l]:lo[l];
            acc[path&15u]=fadd(acc[path&15u],pv);
        }
    }
    for(unsigned l=0;l<5;++l) {
        float lane[16],a[4],b[4];
        for(unsigned i=0;i<16;++i)lane[i]=fadd(lo[l][i],hi[l][i]);
        for(unsigned i=0;i<4;++i){a[i]=fadd(lane[i],lane[i+4]);
            b[i]=fadd(lane[i+8],lane[i+12]);a[i]=fadd(a[i],b[i]);}
        a[0]=fadd(a[0],a[2]);a[1]=fadd(a[1],a[3]);a[0]=fadd(a[0],a[1]);
        out[l]=(double)a[0]*c->inverse_paths+(double)c->leg[l].date1_pv;
    }
}

static void replay_prices(const autocall_d1_native_leaf_context_t *c,double out[5])
{
    for(unsigned l=0;l<5;++l)out[l]=0.0;
    for(uint32_t path=0;path<c->path_count;++path) {
        const double z3=c->direct_d1[path];
        const double z2=c->d2_donor[source(c->d2_map,path)];
        const double sum=z2+z3;
        const double residual[3]={exp((double)c->leg[1].residual_a*sum),
            exp((double)c->leg[3].residual_a*sum),
            exp((double)c->leg[4].residual_a*sum)};
        for(unsigned l=0;l<5;++l) {
            const autocall_d1_native_leg_record_t *r=&c->leg[l];
            double pv=0.0,prior=r->prior_threshold,pcdf=r->prior_survival_cdf;
            double q=.5*erfc(-((double)r->coupon2_intercept-z2)*
                              0.7071067811865475244);
            double p=fmax(0.0,pcdf-q);pv+=(double)c->discounted_coupon2*p;
            double call=(double)r->call2_intercept-z2;prior=fmin(prior,call);
            q=.5*erfc(-call*0.7071067811865475244);p=fmax(0.0,pcdf-q);
            pv+=(double)c->discounted_call2*p;pcdf=fmin(pcdf,q);
            q=.5*erfc(-((double)r->coupon3_intercept-sum)*
                       0.7071067811865475244);
            p=fmax(0.0,pcdf-q);pv+=(double)c->discounted_coupon3*p;
            call=(double)r->call3_intercept-sum;prior=fmin(prior,call);
            q=.5*erfc(-call*0.7071067811865475244);p=fmax(0.0,pcdf-q);
            pv+=(double)c->discounted_call3*p;pcdf=fmin(pcdf,q);
            double prot=(double)r->protection_intercept-sum;
            prior=fmin(prior,prot);q=.5*erfc(-prot*0.7071067811865475244);
            p=fmax(0.0,pcdf-q);pv+=(double)c->discounted_terminal_notional*p;
            q=.5*erfc(-(prior-(double)r->residual_a)*0.7071067811865475244);
            pv+=(double)r->downside_scale*residual[l<3?0:l-2]*q;
            out[l]+=pv;
        }
    }
    for(unsigned l=0;l<5;++l)
        out[l]=out[l]*c->inverse_paths+(double)c->leg[l].date1_pv;
}

static autocall_3date_request_input_t request_from(
    const autocall_frozen_case_t *c)
{
    autocall_3date_request_input_t r;memset(&r,0,sizeof(r));
    r.s0=c->s0;r.rate=c->rate;r.dividend_yield=c->dividend;r.sigma=c->sigma;
    r.maturity=c->maturity;r.notional=c->notional;r.protection_barrier=c->protection;
    for(unsigned d=0;d<3;++d){const double t=c->maturity*(d+1.0)/3.0;
        r.call_barrier[d]=c->call_barrier[d];r.coupon_barrier[d]=c->coupon_barrier[d];
        r.coupon_cashflow[d]=c->coupon[d];r.call_redemption[d]=c->redemption[d];
        r.coupon_payment_time[d]=t+c->payment_lag_fraction*c->maturity;
        r.call_payment_time[d]=r.coupon_payment_time[d];}
    r.terminal_payment_time=c->maturity*(1.0+c->payment_lag_fraction);return r;
}

static int bits_equal(double a,double b)
{uint64_t x,y;memcpy(&x,&a,8);memcpy(&y,&b,8);return x==y;}

static double combine3(const autocall_3date_greek_coefficients_t *c,
                       double m,double z,double p)
{ return c->minus*m+c->zero*z+c->plus*p; }

int main(void)
{
    {
        const double score[7]={90.0,70.0,50.0,30.0,20.0,10.0,1.0};
        const uint32_t global_mask=(UINT32_C(1)<<3)|(UINT32_C(1)<<4)|
                                   (UINT32_C(1)<<5);
        const uint32_t self_mask=global_mask|(UINT32_C(1)<<6);
        const int selected=autocall_d1_native_select_best_global(
            global_mask,score,sizeof(score)/sizeof(score[0]));
        if((self_mask&(UINT32_C(1)<<6))==0u||
           (global_mask&(UINT32_C(1)<<6))!=0u||selected!=5)return 1;
        printf("D1_NATIVE_SELECTION_CONTROL PASS selected_index=%d "
               "selected_paths=2048 terminal_self=PASS terminal_global=NO\n",
               selected);
    }
    autocall_d1_native_engine_t engine __attribute__((aligned(64)));
    autocall_d1_native_test_leaf_calls_reset();
    if(autocall_d1_native_engine_create(&engine))return 1;
    if(autocall_d1_native_test_leaf_calls()!=0) return 1;
    const uint32_t counts[7]={64,128,256,512,1024,2048,4096};
    const double bumps[3]={.005,.01,.02};
    unsigned rows=0;double max_arithmetic_bp=0.0;
    for(unsigned ci=0;ci<AUTOCALL_GREEK_CASES;++ci) {
      const autocall_frozen_case_t fc=autocall_greek_case(ci);
      const autocall_3date_request_input_t in=request_from(&fc);
      const autocall_3date_market_input_t m={in.rate,in.dividend_yield,in.sigma,in.maturity};
      for(unsigned bi=0;bi<3;++bi) {
        autocall_d1_native_market_t market __attribute__((aligned(64)));
        if(autocall_d1_native_market_prepare(&engine,&m,bumps[bi],&market))return 1;
        if(autocall_d1_native_test_leaf_calls()!=0) return 1;
        for(unsigned pi=0;pi<7;++pi) {
            autocall_d1_native_request_t request __attribute__((aligned(64)));
            autocall_d1_native_output_t output __attribute__((aligned(64)));
            double scalar[5],replay[5];
            if(autocall_d1_native_request_prepare(&engine,&market,&in,bumps[bi],
                counts[pi],&request))return 1;
            if(autocall_d1_native_test_leaf_calls()!=0) return 1;
            autocall_d1_native_request_t request_before=request;
            autocall_d1_native_test_leaf_calls_reset();
            if(autocall_d1_native_prepared_price(&request,&output)||
               autocall_d1_native_test_leaf_calls()!=1||
               memcmp(&request,&request_before,sizeof(request)))return 1;
            autocall_d1_native_test_leaf_calls_reset();
            scalar_prices(&request.leaf,scalar);replay_prices(&request.leaf,replay);
            for(unsigned l=0;l<5;++l)if(!bits_equal(scalar[l],output.leg_price[l])){
                fprintf(stderr,"identity case=%s bump=%g paths=%u leg=%u scalar=%a native=%a\n",
                    fc.name,bumps[bi],counts[pi],l,scalar[l],output.leg_price[l]);return 1;}
            const double rd=combine3(&request.spot_legs.first,replay[0],replay[1],replay[2]);
            const double rg=combine3(&request.spot_legs.second,replay[0],replay[1],replay[2]);
            const double rv=combine3(&request.volatility_legs.first,replay[3],replay[1],replay[4]);
            const double arithmetic[3]={fabs(output.delta_per_spot_unit-rd)*
                .01*in.s0/in.notional*1e4,
                .5*fabs(output.gamma_per_spot_unit_squared-rg)*
                (.01*in.s0)*(.01*in.s0)/in.notional*1e4,
                fabs(output.vega_per_unit_sigma-rv)*.01/in.notional*1e4};
            for(unsigned g=0;g<3;++g)if(arithmetic[g]>max_arithmetic_bp)
                max_arithmetic_bp=arithmetic[g];
            if(max_arithmetic_bp>.25){fprintf(stderr,
                "arithmetic case=%s bump=%g paths=%u bp=%.12g\n",
                fc.name,bumps[bi],counts[pi],max_arithmetic_bp);return 1;}
            if(bi==1u)printf("D1_NATIVE_FIXED case=%s paths=%u price=%a delta=%a "
                "gamma=%a vega=%a leg_minus=%a leg_plus=%a identity=PASS\n",
                fc.name,counts[pi],output.price,output.delta_per_spot_unit,
                output.gamma_per_spot_unit_squared,output.vega_per_unit_sigma,
                output.leg_price[0],output.leg_price[2]);
            ++rows;
        }
      }
    }
    {
        const autocall_frozen_case_t fc=autocall_greek_case(1);
        const autocall_3date_request_input_t in=request_from(&fc);
        const autocall_3date_market_input_t m={in.rate,in.dividend_yield,
            in.sigma,in.maturity};
        autocall_d1_native_market_t market __attribute__((aligned(64)));
        autocall_d1_native_request_t request __attribute__((aligned(64)));
        autocall_d1_native_output_t output __attribute__((aligned(64)));
        autocall_d1_native_test_leaf_calls_reset();
        if(autocall_d1_native_market_prepare(&engine,&m,.01,&market)||
           autocall_d1_native_test_leaf_calls()!=0)return 1;
        autocall_d1_native_market_t market_before=market;
        if(autocall_d1_native_request_prepare(&engine,&market,&in,.01,512,&request)||
           autocall_d1_native_test_leaf_calls()!=0)return 1;
        autocall_d1_native_request_t request_before=request;
        if(autocall_d1_native_prepared_price(&request,&output)||
           autocall_d1_native_test_leaf_calls()!=1||
           memcmp(&request,&request_before,sizeof(request)))return 1;
        autocall_d1_native_test_leaf_calls_reset();
        if(autocall_d1_native_reused_total(&engine,&market,&in,.01,512,&output)||
           autocall_d1_native_test_leaf_calls()!=1||
           memcmp(&market,&market_before,sizeof(market)))return 1;
        autocall_d1_native_test_leaf_calls_reset();
        if(autocall_d1_native_fresh_total(&engine,&in,.01,.01,512,&output)||
           autocall_d1_native_test_leaf_calls()!=1)return 1;
        const uint32_t invalid_counts[]={0,32,96,8192};
        for(unsigned i=0;i<sizeof(invalid_counts)/sizeof(invalid_counts[0]);++i)
            if(autocall_d1_native_request_prepare(&engine,&market,&in,.01,
               invalid_counts[i],&request)!=AUTOCALL_D1_NATIVE_INVALID)return 1;
        if(autocall_d1_native_market_prepare(&engine,&m,NAN,&market)!=
           AUTOCALL_D1_NATIVE_UNSUPPORTED)return 1;
        autocall_3date_market_input_t bad=m;bad.sigma=NAN;
        if(autocall_d1_native_market_prepare(&engine,&bad,.01,&market)!=
           AUTOCALL_D1_NATIVE_INVALID)return 1;
        autocall_3date_request_input_t bad_request=in;bad_request.s0=NAN;
        if(autocall_d1_native_request_prepare(&engine,&market_before,&bad_request,
           .01,512,&request)!=AUTOCALL_D1_NATIVE_INVALID)return 1;
    }
    printf("D1_NATIVE_SCALAR_IDENTITY PASS rows=%u max_native_replay_bp=%.12g\n",
           rows,max_arithmetic_bp);
    printf("D1_NATIVE_LIFECYCLE_COUNTS PASS engine=0 market=0 request=0 "
           "prepared=1 reused=1 fresh=1\n");
    for(unsigned pi=0;pi<7;++pi) {
        unsigned char line[256]={0};unsigned donor_lines=0;
        const asian_meta_dim_affine_ctx_t *map=&engine.affine_plan->contexts[1];
        for(uint32_t p=0;p<counts[pi];++p)line[source(map,p)>>4]=1;
        for(unsigned i=0;i<256;++i)donor_lines+=line[i];
        const unsigned direct_lines=counts[pi]/16u;
        const unsigned selector_lines=((counts[pi]/32u)*2u+63u)/64u;
        const unsigned route_lines=selector_lines+2u;
        printf("D1_NATIVE_HOT_LINES paths=%u direct_source_lines=%u "
               "d2_source_lines=%u route_control_lines=%u source_bytes=%u "
               "route_control_bytes=%u\n",counts[pi],direct_lines,donor_lines,
               route_lines,64u*(direct_lines+donor_lines),64u*route_lines);
    }
    autocall_d1_native_engine_destroy(&engine);return 0;
}
