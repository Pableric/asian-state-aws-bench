#define _POSIX_C_SOURCE 200112L
#include "asian_genuine_aad_phase1_reference.h"
#include "private/asian_genuine_aad_phase1_diag.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *a64(size_t n){void*p=0;if(posix_memalign(&p,64,n))return 0;memset(p,0,n);return p;}
static int close64(double a,double b,uint32_t n){double s=fmax(1.0,fmax(fabs(a),fabs(b)));return fabs(a-b)<=64.0*DBL_EPSILON*n*s;}

static int check_n(uint32_t n,double rate,double q,double sigma)
{
    const double s0=100.0,t=1.25,dt=t/n;
    double x[256];
    for(uint32_t k=0;k<n;++k){double z=sin((k+1.0)*1.23456789)+cos((k+3.0)*0.3456789);x[k]=(rate-q-0.5*sigma*sigma)*dt+sigma*sqrt(dt)*z;}
    asian_aad_ref_basis_t f,s,g;
    asian_aad_ref_targeted(x,n,s0,rate,q,sigma,t,&f);
    asian_aad_ref_suffix(x,n,s0,rate,q,sigma,t,&s);
    asian_aad_ref_generic(x,n,s0,rate,q,sigma,t,&g);
    const double *fp=&f.a,*sp=&s.a,*gp=&g.a;
    for(size_t i=0;i<sizeof(f)/sizeof(double);++i)if(!close64(fp[i],sp[i],n)||!close64(fp[i],gp[i],n)){
        fprintf(stderr,"identity N=%u field=%zu forward=%.17g suffix=%.17g generic=%.17g\n",n,i,fp[i],sp[i],gp[i]);return -1;
    }
    return 0;
}

static int check_controls(void)
{
    asian_genuine_aad_phase1_controls_t*c=a64(sizeof(*c));if(!c)return-1;
    if(asian_genuine_aad_phase1_prepare_controls(c,100,105,.03,.01,.2,1,64))return-1;
    const double eps=1e-5;
    asian_genuine_aad_phase1_controls_t *up=a64(sizeof(*up)),*dn=a64(sizeof(*dn));if(!up||!dn)return-1;
    if(asian_genuine_aad_phase1_prepare_controls(up,100,105,.03,.01,.2*(1+eps),1,64)||
       asian_genuine_aad_phase1_prepare_controls(dn,100,105,.03,.01,.2*(1-eps),1,64))return-1;
    double cv=(up->geometric_call.price-dn->geometric_call.price)/(.4*eps);
    double pv=(up->geometric_put.price-dn->geometric_put.price)/(.4*eps);
    if(fabs(cv-c->geometric_call.vega)>1e-7||fabs(pv-c->geometric_put.vega)>1e-7){fprintf(stderr,"geometric vega analytic mismatch\n");return-1;}
    if(asian_genuine_aad_phase1_prepare_controls(c,100,105,.03,.01,0,1,64))return-1;
    if(c->geometric_call.vega!=0||c->geometric_put.vega!=0)return-1;
    free(dn);free(up);free(c);return 0;
}

static int check_crn(void)
{
    enum{N=32};double z[N];for(int k=0;k<N;++k)z[k]=sin(k*0.73)+cos(k*0.19);
    const double s0=100,k=130,r=.03,q=.01,sigma=.2,t=1;
    double x[N],dt=t/N;for(int i=0;i<N;++i)x[i]=(r-q-.5*sigma*sigma)*dt+sigma*sqrt(dt)*z[i];
    asian_aad_ref_basis_t b;asian_aad_ref_suffix(x,N,s0,r,q,sigma,t,&b);
    asian_aad_ref_value_t zero={0},v;asian_aad_ref_payoff(&b,k,r,t,0,0,&zero,&v);
    const double es[]={1e-2,3e-3,1e-3,3e-4};double last=1e300;
    for(size_t i=0;i<4;++i){double h=sigma*es[i];double up=asian_aad_ref_price_from_z(z,N,s0,k,r,q,sigma+h,t,0,0,&zero);double dn=asian_aad_ref_price_from_z(z,N,s0,k,r,q,sigma-h,t,0,0,&zero);double err=fabs((up-dn)/(2*h)-v.vega);if(i&&err>last*1.05){fprintf(stderr,"CRN convergence %.17g %.17g\n",last,err);return-1;}last=err;}
    return 0;
}

static int check_preparation_domain(void)
{
    asian_genuine_aad_phase1_controls_t *controls=a64(sizeof(*controls));
    if(!controls)return-1;
    const uint32_t rejected[]={0,1,257};
    for(size_t i=0;i<sizeof(rejected)/sizeof(rejected[0]);++i){
        const uint32_t n=rejected[i];
        if(asian_genuine_aad_phase1_producer_fixing_count(n)!=0||
           asian_genuine_aad_phase1_prepare_controls(controls,100,105,.03,.01,.2,1,n)!=
             ASIAN_GENUINE_AAD_PHASE1_FIXING_COUNT_UNSUPPORTED||
           asian_genuine_aad_phase1_prepare_context(NULL,NULL,NULL,NULL,
             100,105,.03,.01,.2,1,n)!=
             ASIAN_GENUINE_AAD_PHASE1_FIXING_COUNT_UNSUPPORTED){
            fprintf(stderr,"preparation domain N=%u not rejected explicitly\n",n);
            free(controls);return-1;
        }
    }
    if(asian_genuine_aad_phase1_producer_fixing_count(2)!=16||
       asian_genuine_aad_phase1_producer_fixing_count(17)!=16||
       asian_genuine_aad_phase1_producer_fixing_count(32)!=32){
        free(controls);return-1;
    }
    free(controls);return 0;
}

int main(void)
{
    for(uint32_t n=ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS;
        n<=ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS;++n)
        if(check_n(n,.03,.01,.2))return 2;
    if(check_controls()||check_crn()||check_preparation_domain())return 2;
    puts("asian_genuine_aad_phase1 mathematical_identities=PASS runtime_N_2_256=PASS preparation_domain=PASS sigma_zero_reference=PASS crn_convergence=PASS");
    return 0;
}
