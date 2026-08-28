#define _POSIX_C_SOURCE 200112L

#include "private/autocall_single_asset_three_date_raw_diag.h"
#include "private/autocall_single_asset_three_date_greeks_diag.h"
#include "tests/autocall_single_asset_three_date_greek_cases.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t autocall_3date_test_leaf_invocations;

typedef autocall_3date_greek_coefficients_t coefficients_t;
typedef autocall_3date_greek_legs_t legs_t;

static const double spot_bumps[3] = {0.005,0.010,0.020};
static const double vol_bumps[3] = {0.005,0.010,0.020};

static void *a64(size_t bytes)
{
    void *out=NULL;
    return posix_memalign(&out,64u,bytes)==0 ? out : NULL;
}

static legs_t legs(double center,double bump,int relative)
{
    legs_t out;
    const int status=relative ? autocall_3date_prepare_spot_legs(center,bump,&out) :
        autocall_3date_prepare_volatility_legs(center,bump,&out);
    if (status!=AUTOCALL_3DATE_GREEK_OK) abort();
    return out;
}

static double combine(coefficients_t c,double minus,double zero,double plus)
{
    return c.minus*minus+c.zero*zero+c.plus*plus;
}

static autocall_3date_request_input_t request_input(
    const autocall_frozen_case_t *c)
{
    autocall_3date_request_input_t out;
    memset(&out,0,sizeof(out));
    out.s0=c->s0;out.rate=c->rate;out.dividend_yield=c->dividend;
    out.sigma=c->sigma;out.maturity=c->maturity;out.notional=c->notional;
    out.protection_barrier=c->protection;
    out.terminal_payment_time=c->maturity*(1.0+c->payment_lag_fraction);
    for (unsigned d=0;d<3;++d) {
        out.call_barrier[d]=c->call_barrier[d];
        out.coupon_barrier[d]=c->coupon_barrier[d];
        out.coupon_cashflow[d]=c->coupon[d];
        out.call_redemption[d]=c->redemption[d];
        const double observation=c->maturity*(d+1.0)/3.0;
        out.coupon_payment_time[d]=observation+
            c->payment_lag_fraction*c->maturity;
        out.call_payment_time[d]=out.coupon_payment_time[d];
    }
    return out;
}

static uint32_t source(const asian_meta_affine_route_t *route,uint32_t path)
{
    const asian_meta_dim_affine_ctx_t *map=route->map;
    const uint32_t packet=path>>5,half=(path>>4)&1u,lane=path&15u;
    uint32_t line=map->sel2[packet][0];
    uint32_t control=map->sel2[packet][1]^map->base_control[lane];
    if (half) { line^=1u;control^=map->half_delta[lane]; }
    return line*16u+(control&15u);
}

static double replay(const autocall_3date_request_t *request)
{
    double total=0.0;
    const autocall_3date_leaf_context_t *ctx=&request->context;
    for (uint32_t path=0;path<4096u;++path) {
        double spot=(double)ctx->initial_spot,pv=0.0;
        int alive=1;
        for (unsigned d=0;d<3;++d) {
            const asian_meta_affine_route_t *route=&request->routes[d];
            const uint32_t offset=d==0u ? path : source(route,path);
            spot*=(double)route->growth_base[offset];
            if (alive && spot>=(double)ctx->date[d].coupon_barrier)
                pv+=(double)ctx->date[d].discounted_coupon;
            if (alive && spot>=(double)ctx->date[d].call_barrier) {
                pv+=(double)ctx->date[d].discounted_call_redemption;
                alive=0;
            }
        }
        if (alive) pv+=(double)ctx->discounted_terminal_notional*
            (spot>=(double)ctx->protection_barrier ? 1.0 :
             spot*(double)ctx->inverse_initial_spot);
        total+=pv;
    }
    return total/4096.0;
}

static int request_price(autocall_3date_engine_t *engine,
    const autocall_frozen_case_t *fixture,autocall_3date_carrier_t *carrier,
    autocall_3date_request_t *request,double *price,double *replay_price)
{
    const autocall_3date_request_input_t input=request_input(fixture);
    autocall_3date_output_t output;
    if (autocall_3date_request_prepare(engine,carrier,&input,request)!=0 ||
        autocall_3date_prepared_price(request,&output)!=0) return -1;
    *price=output.price;*replay_price=replay(request);
    return isfinite(*price)&&isfinite(*replay_price)?0:-1;
}

static int prepare_and_price(autocall_3date_engine_t *engine,
    const autocall_frozen_case_t *fixture,autocall_3date_carrier_t *carrier,
    autocall_3date_request_t *request,double *price,double *replay_price)
{
    const autocall_3date_market_input_t market={fixture->rate,fixture->dividend,
        fixture->sigma,fixture->maturity};
    return autocall_3date_market_prepare(engine,&market,carrier)!=0 ? -1 :
        request_price(engine,fixture,carrier,request,price,replay_price);
}

int main(void)
{
    struct {
        unsigned char before[64];
        legs_t value;
        unsigned char after[64];
    } guarded;
    memset(&guarded,0xa5,sizeof(guarded));
    if (autocall_3date_prepare_spot_legs(117.3,0.01,&guarded.value)!=0 ||
        memcmp(guarded.before,(unsigned char[64]){[0 ... 63]=0xa5},64u)!=0 ||
        memcmp(guarded.after,(unsigned char[64]){[0 ... 63]=0xa5},64u)!=0)
        return 1;
    const double fm=(double)guarded.value.minus*guarded.value.minus;
    const double f0=(double)guarded.value.zero*guarded.value.zero;
    const double fp=(double)guarded.value.plus*guarded.value.plus;
    if (fabs(combine(guarded.value.first,fm,f0,fp)-
             2.0*(double)guarded.value.zero)>1e-10 ||
        fabs(combine(guarded.value.second,fm,f0,fp)-2.0)>1e-10)
        return 1;
    legs_t rejected;
    memset(&rejected,0x5a,sizeof(rejected));
    const legs_t rejected_copy=rejected;
    if (autocall_3date_prepare_spot_legs(100.0,0.0075,&rejected)!=
            AUTOCALL_3DATE_GREEK_UNSUPPORTED_BUMP ||
        autocall_3date_prepare_spot_legs(NAN,0.01,&rejected)!=
            AUTOCALL_3DATE_GREEK_INVALID ||
        autocall_3date_prepare_volatility_legs(0.004,0.005,&rejected)!=
            AUTOCALL_3DATE_GREEK_DOMAIN ||
        autocall_3date_prepare_volatility_legs(0.2,0.0,&rejected)!=
            AUTOCALL_3DATE_GREEK_UNSUPPORTED_BUMP ||
        memcmp(&rejected,&rejected_copy,sizeof(rejected))!=0)
        return 1;
    puts("BUMP_SEMANTICS PASS supported=0.005,0.010,0.020 "
         "nonuniform_coefficients=YES invalid_rejection_stable=YES guards=PASS");
    autocall_3date_engine_t engine __attribute__((aligned(64)));
    autocall_3date_carrier_t *base=a64(sizeof(*base));
    autocall_3date_carrier_t *scratch=a64(sizeof(*scratch));
    autocall_3date_request_t *request=a64(sizeof(*request));
    if (!base||!scratch||!request||autocall_3date_engine_create(&engine)!=0)
        return 1;
    for (unsigned ci=0;ci<AUTOCALL_GREEK_CASES;++ci) {
        const autocall_frozen_case_t original=autocall_greek_case(ci);
        autocall_frozen_case_t c=original;
        c.s0=(float)c.s0;c.sigma=(float)c.sigma;
        const uint64_t before=autocall_3date_test_leaf_invocations;
        double zero,zero_replay;
        if (prepare_and_price(&engine,&c,base,request,&zero,&zero_replay)!=0)
            return 1;
        for (unsigned b=0;b<3;++b) {
            const legs_t spot=legs(c.s0,spot_bumps[b],1);
            const legs_t vol=legs(c.sigma,vol_bumps[b],0);
            double sm,sr,sp,spr,vm,vr,vp,vpr;
            c.s0=spot.minus;c.sigma=vol.zero;
            if (request_price(&engine,&c,base,request,&sm,&sr)!=0) return 1;
            c.s0=spot.plus;
            if (request_price(&engine,&c,base,request,&sp,&spr)!=0) return 1;
            c.s0=spot.zero;c.sigma=vol.minus;
            if (prepare_and_price(&engine,&c,scratch,request,&vm,&vr)!=0) return 1;
            c.sigma=vol.plus;
            if (prepare_and_price(&engine,&c,scratch,request,&vp,&vpr)!=0) return 1;
            const double delta=combine(spot.first,sm,zero,sp);
            const double gamma=combine(spot.second,sm,zero,sp);
            const double vega=combine(vol.first,vm,zero,vp);
            const double rd=combine(spot.first,sr,zero_replay,spr);
            const double rg=combine(spot.second,sr,zero_replay,spr);
            const double rv=combine(vol.first,vr,zero_replay,vpr);
            printf("FIXED_BLOCK panel=%s case=%s bump=%u price=%.17g delta=%.17g "
                "gamma=%.17g vega_per_unit_sigma=%.17g vega_per_vol_point=%.17g "
                "replay_price=%.17g replay_delta=%.17g "
                "replay_gamma=%.17g replay_vega=%.17g Sminus=%a S0=%a Splus=%a "
                "sigma_minus=%a sigma0=%a sigma_plus=%a dm=%.17g dp=%.17g "
                "vm=%.17g vp=%.17g\n",
                ci<AUTOCALL_GREEK_CORE_CASES?"CORE":"STRESS",original.name,b,
                zero,delta,gamma,vega,.01*vega,zero_replay,rd,rg,rv,
                spot.minus,spot.zero,spot.plus,vol.minus,vol.zero,vol.plus,
                spot.dm,spot.dp,vol.dm,vol.dp);
            c=original;c.s0=(float)c.s0;c.sigma=(float)c.sigma;
        }
        if (autocall_3date_test_leaf_invocations-before!=13u) return 1;
    }
    puts("FIXED_BLOCK_ENGINE PASS leaf_calls_per_case=13 base_logically_once=YES "
         "spot_carrier_shared=YES volatility_signed_z_shared=YES");
    autocall_3date_engine_destroy(&engine);free(request);free(scratch);free(base);
    return 0;
}
