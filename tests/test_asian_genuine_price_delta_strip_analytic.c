#define _POSIX_C_SOURCE 200112L
#include "private/asian_genuine_price_delta_strip_diag.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void *a64(size_t bytes)
{
    void *p=0;
    if(posix_memalign(&p,64,bytes))return 0;
    return p;
}

static long double normal_cdf(long double x)
{
    return 0.5L*erfcl(-x*0.707106781186547524400844362104849039L);
}

static void exact_geometric(long double s0,long double strike,long double rate,
    long double dividend,long double sigma,long double maturity,uint32_t future,
    uint32_t completed,long double past_log_sum,int put,long double*price,
    long double*delta)
{
    const long double f=future,m=future+completed,dt=maturity/f;
    const long double mean=past_log_sum/m+(f/m)*logl(s0)+
        (rate-dividend-0.5L*sigma*sigma)*dt*f*(f+1)/2/m;
    const long double variance=sigma*sigma*dt*f*(f+1)*(2*f+1)/6/(m*m);
    const long double discount=expl(-rate*maturity),dmean=f/(m*s0);
    if(variance==0){
        const long double g=expl(mean);
        *price=discount*(put?fmaxl(strike-g,0):fmaxl(g-strike,0));
        *delta=put?(g<strike?-discount*g*dmean:0):(g>strike?discount*g*dmean:0);
        return;
    }
    const long double root=sqrtl(variance),forward=expl(mean+0.5L*variance);
    const long double d2=(mean-logl(strike))/root,d1=d2+root;
    if(put){
        *price=discount*(strike*normal_cdf(-d2)-forward*normal_cdf(-d1));
        *delta=-discount*forward*normal_cdf(-d1)*dmean;
    }else{
        *price=discount*(forward*normal_cdf(d1)-strike*normal_cdf(d2));
        *delta=discount*forward*normal_cdf(d1)*dmean;
    }
}

static long double central_delta(long double s0,long double strike,long double rate,
    long double dividend,long double sigma,long double maturity,uint32_t future,
    uint32_t completed,long double past_log_sum,int put)
{
    const long double h=s0*1e-4L;
    long double p2p,p1p,p1m,p2m,unused;
    exact_geometric(s0+2*h,strike,rate,dividend,sigma,maturity,future,completed,
        past_log_sum,put,&p2p,&unused);
    exact_geometric(s0+h,strike,rate,dividend,sigma,maturity,future,completed,
        past_log_sum,put,&p1p,&unused);
    exact_geometric(s0-h,strike,rate,dividend,sigma,maturity,future,completed,
        past_log_sum,put,&p1m,&unused);
    exact_geometric(s0-2*h,strike,rate,dividend,sigma,maturity,future,completed,
        past_log_sum,put,&p2m,&unused);
    return (-p2p+8*p1p-8*p1m+p2m)/(12*h);
}

static int one(double rate,double sigma,uint32_t future,uint32_t completed)
{
    float strikes[32];asian_genuine_strip_fixed_strikes(32,strikes);
    asian_genuine_strip_context_t*ctx=a64(sizeof*ctx);if(!ctx)return -1;
    const double past=completed?completed*log(95.0):0;
    const double q0=completed?completed*95.0:0;
    if(asian_genuine_strip_prepare(ctx,100,rate,0,sigma,1,future,completed,
        q0,past,strikes,32))return -1;
    for(uint32_t k=0;k<32;++k){
        for(int put=0;put<2;++put){
            long double price,delta;
            exact_geometric(100,strikes[k],rate,0,sigma,1,future,completed,past,
                put,&price,&delta);
            const long double fd=central_delta(100,strikes[k],rate,0,sigma,1,
                future,completed,past,put);
            const int exact_kink=sigma==0.0 && fabsl(price)<1e-12L &&
                fabsl(fd)>0.1L;
            if(!exact_kink && fabsl(fd-delta)>2e-9L){
                fprintf(stderr,"geometric central delta rate=%.9g sigma=%.9g f=%u c=%u K=%.9g put=%d analytic=%.17Lg finite=%.17Lg\n",rate,sigma,future,completed,strikes[k],put,delta,fd);return -1;
            }
            const int direct_call=ctx->strikes[k].flags&ASIAN_GENUINE_STRIP_DIRECT_CALL;
            if((!put&&direct_call)||(put&&!direct_call)){
                if(fabsl((long double)ctx->strikes[k].geometric_price_exact_direct-price)>2e-11L||
                   fabsl((long double)ctx->strikes[k].geometric_delta_exact_direct-delta)>2e-11L){
                    fprintf(stderr,"prepared geometric mismatch K=%.9g put=%d\n",strikes[k],put);return -1;
                }
            }
        }
        long double call,put,cd,pd;
        exact_geometric(100,strikes[k],rate,0,sigma,1,future,completed,past,0,&call,&cd);
        exact_geometric(100,strikes[k],rate,0,sigma,1,future,completed,past,1,&put,&pd);
        if(sigma!=0.0 && !(isfinite((double)(call-put))&&isfinite((double)(cd-pd))))return -1;
    }
    free(ctx);return 0;
}

int main(void)
{
    static const double rates[]={-.03,0,.08};
    static const double sigmas[]={0,.2,.6};
    for(size_t r=0;r<sizeof rates/sizeof rates[0];++r)
        for(size_t s=0;s<sizeof sigmas/sizeof sigmas[0];++s){
            if(one(rates[r],sigmas[s],1,0)||one(rates[r],sigmas[s],17,3)||
               one(rates[r],sigmas[s],256,0))return 2;
        }
    puts("asian_genuine_price_delta_strip analytic_geometric_prices=PASS "
         "analytic_geometric_deltas=PASS high_accuracy_central_difference=PASS "
         "sigma_zero_exact_kink=INAPPLICABLE");
    return 0;
}
