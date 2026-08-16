#define _GNU_SOURCE
#include "asian_pricer.h"
#include "asian_kernels.h"
#include "asian_reference.h"
#include <math.h>
#include <string.h>
#include <time.h>

static double now_seconds(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec+ts.tv_nsec*1e-9; }

static int valid(const asian_price_request_t *r) {
    return r && r->s0>0 && r->k>0 && r->sigma>=0 && r->t>0 && r->num_blocks &&
        (r->type==ASIAN_CALL || r->type==ASIAN_PUT) && r->mode>=ASIAN_MODE_REFERENCE && r->mode<=ASIAN_MODE_COEFFICIENT_PAIR;
}

static int deterministic_price(const asian_price_request_t *r, asian_price_result_t *out) {
    if (r->sigma != 0.0f) return 0;
    double sum=0.0;
    for(int i=1;i<=32;++i) sum += (double)r->s0*exp((double)r->r*(double)r->t*i/32.0);
    const double avg=sum/32.0;
    const double intrinsic=r->type==ASIAN_CALL?avg-r->k:r->k-avg;
    out->samples=r->num_blocks*(uint64_t)ASIAN_SOBOL_BLOCK_SIZE;
    out->price=exp(-(double)r->r*r->t)*fmax(intrinsic,0.0);
    out->payoff_sum=out->price*(double)out->samples;
    return 1;
}

const char* asian_mode_name(asian_pricing_mode_t m) {
    switch(m){case ASIAN_MODE_REFERENCE:return "reference";case ASIAN_MODE_FINAL_Z:return "final-z";
    case ASIAN_MODE_RANK1:return "rank1";case ASIAN_MODE_COEFFICIENT_PAIR:return "coefficient-pair";default:return "invalid";}
}
int asian_mode_is_experimental(asian_pricing_mode_t m){ return m!=ASIAN_MODE_REFERENCE; }

int price_asian_scalar_mode(const asian_price_request_t *req, asian_pricing_mode_t mode, asian_price_result_t *out) {
    if(!valid(req)||!out||mode<0||mode>ASIAN_MODE_COEFFICIENT_PAIR) return -1;
    memset(out,0,sizeof(*out));
    if(deterministic_price(req,out)) return 0;
    const double start=now_seconds();
    out->payoff_sum=asian_scalar_payoff_sum(req,asian_w_for_mode(mode));
    out->kernel_seconds=now_seconds()-start;
    out->samples=req->num_blocks*(uint64_t)ASIAN_SOBOL_BLOCK_SIZE;
    out->price=out->payoff_sum/(double)out->samples;
    return 0;
}

int price_asian(const asian_price_request_t *req, asian_price_result_t *out) {
    if(!valid(req)||!out) return -1;
    memset(out,0,sizeof(*out));
    if(deterministic_price(req,out)) return 0;
    if(req->mode==ASIAN_MODE_REFERENCE) return price_asian_scalar_mode(req,ASIAN_MODE_REFERENCE,out);
    const double start=now_seconds();
    if(req->mode==ASIAN_MODE_FINAL_Z) out->payoff_sum=asian_kernel_final_z(req);
    else if(req->mode==ASIAN_MODE_RANK1) out->payoff_sum=asian_kernel_rank1(req);
    else out->payoff_sum=asian_kernel_pair(req);
    out->kernel_seconds=now_seconds()-start;
    out->samples=req->num_blocks*(uint64_t)ASIAN_SOBOL_BLOCK_SIZE;
    out->price=out->payoff_sum/(double)out->samples;
    return 0;
}

static double cdf(double x){return .5*erfc(-x/M_SQRT2);}
double asian_black_scholes_price(const asian_price_request_t *r) {
    if(!valid(r)) return NAN;
    const double df=exp(-r->r*r->t), vol=r->sigma*sqrt(r->t);
    if(vol==0){double f=r->s0*exp(r->r*r->t),p=r->type==ASIAN_CALL?f-r->k:r->k-f;return df*fmax(p,0);}
    const double d1=(log(r->s0/r->k)+(r->r+.5*r->sigma*r->sigma)*r->t)/vol,d2=d1-vol;
    return r->type==ASIAN_CALL?r->s0*cdf(d1)-r->k*df*cdf(d2):r->k*df*cdf(-d2)-r->s0*cdf(-d1);
}
