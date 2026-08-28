#define _POSIX_C_SOURCE 200112L
#include "private/autocall_single_asset_three_date_d1_native_diag.h"
#include "private/asian_genuine_fixed_block_source_diag.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int same_double(double a,double b)
{
    uint64_t x,y;memcpy(&x,&a,8);memcpy(&y,&b,8);return x==y;
}

static int supported_paths(uint32_t n)
{
    return n>=64u&&n<=4096u&&(n&(n-1u))==0u;
}

static int finite_market(const autocall_3date_market_input_t *m)
{
    return m&&isfinite(m->rate)&&isfinite(m->dividend_yield)&&
        isfinite(m->sigma)&&m->sigma>0.0&&
        isfinite(m->maturity)&&m->maturity>0.0;
}

static int finite_request(const autocall_3date_request_input_t *in)
{
    if (!in||!isfinite(in->s0)||in->s0<=0.0||
        !isfinite(in->notional)||in->notional<=0.0||
        !isfinite(in->protection_barrier)||in->protection_barrier<=0.0||
        !isfinite(in->rate)||!isfinite(in->dividend_yield)||
        !isfinite(in->sigma)||in->sigma<=0.0||
        !isfinite(in->maturity)||in->maturity<=0.0||
        !isfinite(in->terminal_payment_time)||
        in->terminal_payment_time<in->maturity) return 0;
    for (unsigned d=0;d<3u;++d) {
        const double t=in->maturity*(double)(d+1u)/3.0;
        if (!isfinite(in->call_barrier[d])||in->call_barrier[d]<=0.0||
            !isfinite(in->coupon_barrier[d])||in->coupon_barrier[d]<=0.0||
            !isfinite(in->coupon_cashflow[d])||in->coupon_cashflow[d]<0.0||
            !isfinite(in->call_redemption[d])||in->call_redemption[d]<=0.0||
            !isfinite(in->coupon_payment_time[d])||
            in->coupon_payment_time[d]<t||
            !isfinite(in->call_payment_time[d])||
            in->call_payment_time[d]<t) return 0;
    }
    return 1;
}

int autocall_d1_native_engine_create(autocall_d1_native_engine_t *engine)
{
    if (!engine||((uintptr_t)engine&63u)) return AUTOCALL_D1_NATIVE_INVALID;
    memset(engine,0,sizeof(*engine));
    if (asian_meta_affine_plan_create(&engine->affine_plan)!=0)
        return AUTOCALL_D1_NATIVE_INVALID;
    if (posix_memalign((void **)&engine->workspace,64,sizeof(*engine->workspace))) {
        asian_meta_affine_plan_destroy(engine->affine_plan);return AUTOCALL_D1_NATIVE_INVALID;
    }
    memset(engine->workspace,0,sizeof(*engine->workspace));
    if (engine->affine_plan->donor_region[1] != 1u) {
        free(engine->workspace);asian_meta_affine_plan_destroy(engine->affine_plan);
        memset(engine,0,sizeof(*engine));return AUTOCALL_D1_NATIVE_INVALID;
    }
    engine->signed_z=asian_genuine_fixed_block_signed_z;
    const uint32_t lo=UINT32_C(0xc075e22f),hi=UINT32_C(0x4075e233);
    memcpy(&engine->signed_z_min,&lo,4);memcpy(&engine->signed_z_max,&hi,4);
    engine->next_generation=1;engine->magic=AUTOCALL_D1_NATIVE_ENGINE_MAGIC;
    return AUTOCALL_D1_NATIVE_OK;
}

void autocall_d1_native_engine_destroy(autocall_d1_native_engine_t *engine)
{
    if (engine) { free(engine->workspace);asian_meta_affine_plan_destroy(
        engine->affine_plan);memset(engine,0,sizeof(*engine)); }
}

int autocall_d1_native_market_prepare(autocall_d1_native_engine_t *engine,
    const autocall_3date_market_input_t *input,double volatility_bump,
    autocall_d1_native_market_t *out)
{
    if (!engine||engine->magic!=AUTOCALL_D1_NATIVE_ENGINE_MAGIC||!out||
        ((uintptr_t)out&63u)||!finite_market(input)) return AUTOCALL_D1_NATIVE_INVALID;
    autocall_3date_greek_legs_t vol;
    if (autocall_3date_prepare_volatility_legs(input->sigma,volatility_bump,&vol))
        return AUTOCALL_D1_NATIVE_UNSUPPORTED;
    autocall_d1_native_market_t result;memset(&result,0,sizeof(result));
    result.input=*input;result.volatility_legs=vol;
    const float sigma[3]={vol.minus,vol.zero,vol.plus};
    const double dt=input->maturity/3.0;
    for (unsigned i=0;i<3u;++i) {
        const double s=(double)sigma[i];
        const double mu=(input->rate-input->dividend_yield-.5*s*s)*dt;
        const double a=s*sqrt(dt);
        if (!isfinite(mu)||!isfinite(a)||a<=0.0||
            a*(double)(2.0f*engine->signed_z_min)<-87.0||
            a*(double)(2.0f*engine->signed_z_max)>88.0)
            return AUTOCALL_D1_NATIVE_DOMAIN;
        result.leg[i].sigma=sigma[i];result.leg[i].mu=(float)mu;
        result.leg[i].a=(float)a;result.leg[i].inverse_a=1.0f/(float)a;
        result.leg[i].sigma_exact=s;
    }
    result.generation=engine->next_generation++;
    if (!result.generation) result.generation=engine->next_generation++;
    result.requested_volatility_bump=volatility_bump;
    result.magic=AUTOCALL_D1_NATIVE_MARKET_MAGIC;
    result.status=AUTOCALL_D1_NATIVE_OK;*out=result;return AUTOCALL_D1_NATIVE_OK;
}

static int make_leg(const autocall_3date_request_input_t *in,double s0,
    const autocall_d1_native_market_leg_t *market,
    const float discounted_coupon[3],const float discounted_call[3],
    float terminal,autocall_d1_native_leg_record_t *out)
{
    const double mu=(double)market->mu,a=(double)market->a;
    double call[3],coupon[3];
    for (unsigned d=0;d<3u;++d) {
        call[d]=(log(in->call_barrier[d]/s0)-(double)(d+1u)*mu)/a;
        coupon[d]=(log(in->coupon_barrier[d]/s0)-(double)(d+1u)*mu)/a;
    }
    const double pcall=.5*erfc(call[0]*0.7071067811865475244);
    const double pcoupon=.5*erfc(coupon[0]*0.7071067811865475244);
    const double prior=.5*erfc(-call[0]*0.7071067811865475244);
    const double d1pv=(double)discounted_coupon[0]*pcoupon+
                      (double)discounted_call[0]*pcall;
    const double downside=(double)terminal*exp(3.0*mu+.5*a*a);
    autocall_d1_native_leg_record_t r;memset(&r,0,sizeof(r));
    r.call2_intercept=(float)call[1];r.coupon2_intercept=(float)coupon[1];
    r.call3_intercept=(float)call[2];r.coupon3_intercept=(float)coupon[2];
    r.protection_intercept=(float)((log(in->protection_barrier/s0)-3.0*mu)/a);
    r.date1_pv=(float)d1pv;r.prior_threshold=(float)call[0];
    r.prior_survival_cdf=(float)prior;r.residual_a=market->a;
    r.downside_scale=(float)downside;
    const float *v=(const float *)(const void *)&r;
    for (unsigned i=0;i<10u;++i) if (!isfinite(v[i])) return -1;
    *out=r;return 0;
}

int autocall_d1_native_request_prepare(const autocall_d1_native_engine_t *engine,
    const autocall_d1_native_market_t *market,
    const autocall_3date_request_input_t *input,double spot_fraction,
    uint32_t path_count,autocall_d1_native_request_t *out)
{
    if (!engine||engine->magic!=AUTOCALL_D1_NATIVE_ENGINE_MAGIC||
        !engine->affine_plan||!market||market->magic!=AUTOCALL_D1_NATIVE_MARKET_MAGIC||
        !out||((uintptr_t)out&63u)||!finite_request(input)||!supported_paths(path_count)||
        !same_double(input->rate,market->input.rate)||
        !same_double(input->dividend_yield,market->input.dividend_yield)||
        !same_double(input->sigma,market->input.sigma)||
        !same_double(input->maturity,market->input.maturity))
        return AUTOCALL_D1_NATIVE_INVALID;
    autocall_3date_greek_legs_t spot;
    if (autocall_3date_prepare_spot_legs(input->s0,spot_fraction,&spot))
        return AUTOCALL_D1_NATIVE_UNSUPPORTED;
    autocall_d1_native_request_t r;memset(&r,0,sizeof(r));
    r.spot_legs=spot;r.volatility_legs=market->volatility_legs;
    float dc[3],dr[3];
    for (unsigned d=0;d<3u;++d) {
        dc[d]=(float)(input->coupon_cashflow[d]*
            exp(-input->rate*input->coupon_payment_time[d]));
        dr[d]=(float)(input->call_redemption[d]*
            exp(-input->rate*input->call_payment_time[d]));
        if (!isfinite(dc[d])||dc[d]<0.0f||!isfinite(dr[d])||dr[d]<=0.0f)
            return AUTOCALL_D1_NATIVE_DOMAIN;
    }
    const float terminal=(float)(input->notional*
        exp(-input->rate*input->terminal_payment_time));
    if (!isfinite(terminal)||terminal<=0.0f) return AUTOCALL_D1_NATIVE_DOMAIN;
    r.leaf.direct_d1=engine->signed_z;
    r.leaf.d2_donor=engine->signed_z+ASIAN_META_PATHS;
    r.leaf.d2_map=&engine->affine_plan->contexts[1];
    r.leaf.end_bytes=path_count*4u;r.leaf.path_count=path_count;
    r.leaf.inverse_paths=1.0/(double)path_count;
    r.leaf.discounted_coupon2=dc[1];r.leaf.discounted_call2=dr[1];
    r.leaf.discounted_coupon3=dc[2];r.leaf.discounted_call3=dr[2];
    r.leaf.discounted_terminal_notional=terminal;
    r.leaf.magic=AUTOCALL_D1_NATIVE_REQUEST_MAGIC;
    const double s0[5]={spot.minus,spot.zero,spot.plus,spot.zero,spot.zero};
    const unsigned mi[5]={1,1,1,0,2};
    for (unsigned i=0;i<5u;++i)
        if (make_leg(input,s0[i],&market->leg[mi[i]],dc,dr,terminal,
                     &r.leaf.leg[i])) return AUTOCALL_D1_NATIVE_DOMAIN;
    r.market=market;r.market_generation=market->generation;
    r.magic=AUTOCALL_D1_NATIVE_REQUEST_MAGIC;r.status=AUTOCALL_D1_NATIVE_OK;
    *out=r;return AUTOCALL_D1_NATIVE_OK;
}

static double combine(const autocall_3date_greek_coefficients_t *c,
                      double m,double z,double p)
{ return c->minus*m+c->zero*z+c->plus*p; }

int autocall_d1_native_prepared_price(const autocall_d1_native_request_t *r,
    autocall_d1_native_output_t *out)
{
    if (!r||!out||r->magic!=AUTOCALL_D1_NATIVE_REQUEST_MAGIC||
        r->leaf.magic!=AUTOCALL_D1_NATIVE_REQUEST_MAGIC||!r->market||
        r->market->magic!=AUTOCALL_D1_NATIVE_MARKET_MAGIC||
        r->market_generation!=r->market->generation)
        return AUTOCALL_D1_NATIVE_INVALID;
    autocall_d1_native_output_t o;memset(&o,0,sizeof(o));
    autocall_d1_native_price5_leaf(&r->leaf,o.leg_price);
    o.price=o.leg_price[1];
    o.delta_per_spot_unit=combine(&r->spot_legs.first,
        o.leg_price[0],o.leg_price[1],o.leg_price[2]);
    o.gamma_per_spot_unit_squared=combine(&r->spot_legs.second,
        o.leg_price[0],o.leg_price[1],o.leg_price[2]);
    o.vega_per_unit_sigma=combine(&r->volatility_legs.first,
        o.leg_price[3],o.leg_price[1],o.leg_price[4]);
    o.vega_per_one_vol_point=.01*o.vega_per_unit_sigma;
    o.spot_minus=r->spot_legs.minus;o.spot_zero=r->spot_legs.zero;
    o.spot_plus=r->spot_legs.plus;o.sigma_minus=r->volatility_legs.minus;
    o.sigma_zero=r->volatility_legs.zero;o.sigma_plus=r->volatility_legs.plus;
    o.spot_dm=r->spot_legs.dm;o.spot_dp=r->spot_legs.dp;
    o.volatility_vm=r->volatility_legs.dm;o.volatility_vp=r->volatility_legs.dp;
    o.market_generation=r->market_generation;o.path_count=r->leaf.path_count;
    o.magic=AUTOCALL_D1_NATIVE_OUTPUT_MAGIC;o.status=AUTOCALL_D1_NATIVE_OK;
    o.identity=UINT32_C(0x50415353);*out=o;return AUTOCALL_D1_NATIVE_OK;
}

int autocall_d1_native_reused_total(autocall_d1_native_engine_t *engine,
    const autocall_d1_native_market_t *market,
    const autocall_3date_request_input_t *input,double spot_fraction,
    uint32_t path_count,autocall_d1_native_output_t *out)
{
    if (!engine||!engine->workspace) return AUTOCALL_D1_NATIVE_INVALID;
    int s=autocall_d1_native_request_prepare(engine,market,input,spot_fraction,
        path_count,&engine->workspace->request);
    return s?s:autocall_d1_native_prepared_price(&engine->workspace->request,out);
}

int autocall_d1_native_fresh_total(autocall_d1_native_engine_t *engine,
    const autocall_3date_request_input_t *input,double spot_fraction,
    double volatility_bump,uint32_t path_count,autocall_d1_native_output_t *out)
{
    if (!engine||!engine->workspace||!input) return AUTOCALL_D1_NATIVE_INVALID;
    const autocall_3date_market_input_t m={input->rate,input->dividend_yield,
        input->sigma,input->maturity};
    int s=autocall_d1_native_market_prepare(engine,&m,volatility_bump,
        &engine->workspace->market);
    if (!s) s=autocall_d1_native_request_prepare(engine,&engine->workspace->market,
        input,spot_fraction,path_count,&engine->workspace->request);
    return s?s:autocall_d1_native_prepared_price(&engine->workspace->request,out);
}
