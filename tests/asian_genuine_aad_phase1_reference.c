#include "asian_genuine_aad_phase1_reference.h"

#include <math.h>
#include <string.h>

static void geometric_finish(asian_aad_ref_basis_t *b, uint32_t n, double s0,
                             double rate, double dividend_yield, double sigma,
                             double maturity)
{
    const double c=rate-dividend_yield+0.5*sigma*sigma;
    const double big_b=maturity*(n+1.0)/(2.0*n);
    b->g=s0*exp(b->l);
    b->g_delta=b->g/s0;
    b->g_rho=b->g*big_b;
    b->g_vega=b->g*(b->l-c*big_b)/sigma;
}

void asian_aad_ref_primal(const double*x,uint32_t n,double s0,double*s,
                          asian_aad_ref_basis_t*out)
{
    double q=0,l=0,current=s0;
    for(uint32_t k=0;k<n;++k){
        current*=exp(x[k]);s[k]=current;q+=current;
        l+=(double)(n-k)/(double)n*x[k];
    }
    memset(out,0,sizeof(*out));out->a=q/n;out->a_delta=out->a/s0;out->l=l;
}

void asian_aad_ref_targeted(const double*x,uint32_t n,double s0,double rate,
                            double dividend,double sigma,double maturity,
                            asian_aad_ref_basis_t*out)
{
    double s[256],cumulative=0,rw=0,xw=0;
    asian_aad_ref_primal(x,n,s0,s,out);
    for(uint32_t k=0;k<n;++k){cumulative+=x[k];rw+=(k+1.0)*s[k];xw+=s[k]*cumulative;}
    const double dt=maturity/n,c=rate-dividend+0.5*sigma*sigma;
    out->a_rho=dt*rw/n;
    out->a_vega=(xw/n-c*out->a_rho)/sigma;
    geometric_finish(out,n,s0,rate,dividend,sigma,maturity);
}

void asian_aad_ref_suffix(const double*x,uint32_t n,double s0,double rate,
                          double dividend,double sigma,double maturity,
                          asian_aad_ref_basis_t*out)
{
    double s[256],suffix=0,rho_sum=0,x_dot=0;
    asian_aad_ref_primal(x,n,s0,s,out);
    for(uint32_t k=n;k--!=0;){suffix+=s[k];rho_sum+=suffix;x_dot=fma(suffix,x[k],x_dot);}
    const double dt=maturity/n,c=rate-dividend+0.5*sigma*sigma;
    out->a_rho=rho_sum*dt/n;
    out->a_vega=(x_dot/n-c*out->a_rho)/sigma;
    geometric_finish(out,n,s0,rate,dividend,sigma,maturity);
}

void asian_aad_ref_generic(const double*x,uint32_t n,double s0,double rate,
                           double dividend,double sigma,double maturity,
                           asian_aad_ref_basis_t*out)
{
    double s[256],growth[256],adj_s=0,rho=0,vega=0;
    asian_aad_ref_primal(x,n,s0,s,out);
    for(uint32_t k=0;k<n;++k)growth[k]=exp(x[k]);
    const double dt=maturity/n,c=rate-dividend+0.5*sigma*sigma;
    for(uint32_t k=n;k--!=0;){
        const double previous=k?s[k-1]:s0;
        adj_s+=1.0/n;
        const double adj_growth=adj_s*previous;
        const double adj_x=adj_growth*growth[k];
        rho+=adj_x*dt;
        vega+=adj_x*(x[k]-c*dt)/sigma;
        adj_s*=growth[k];
    }
    out->a_rho=rho;out->a_vega=vega;
    geometric_finish(out,n,s0,rate,dividend,sigma,maturity);
}

void asian_aad_ref_payoff(const asian_aad_ref_basis_t*b,double strike,
                          double rate,double maturity,int put,int cv,
                          const asian_aad_ref_value_t*exact,
                          asian_aad_ref_value_t*out)
{
    const double p=put?-1.0:1.0,d=exp(-rate*maturity);
    const double ma=p*(b->a-strike),mg=p*(b->g-strike);
    const double ia=ma>0?p:0,ig=mg>0?p:0;
    const double pa=ma>0?ma:0,pg=mg>0?mg:0;
    out->price=d*(pa-(cv?pg:0));
    out->delta=d*(ia*b->a_delta-(cv?ig*b->g_delta:0));
    out->vega=d*(ia*b->a_vega-(cv?ig*b->g_vega:0));
    out->rho=d*(ia*b->a_rho-(cv?ig*b->g_rho:0))-
             maturity*d*(pa-(cv?pg:0));
    if(cv){out->price+=exact->price;out->delta+=exact->delta;
           out->vega+=exact->vega;out->rho+=exact->rho;}
}

double asian_aad_ref_price_from_z(const double*z,uint32_t n,double s0,
                                  double strike,double rate,double dividend,
                                  double sigma,double maturity,int put,int cv,
                                  const asian_aad_ref_value_t*exact)
{
    double x[256];const double dt=maturity/n;
    for(uint32_t k=0;k<n;++k)x[k]=(rate-dividend-0.5*sigma*sigma)*dt+
        sigma*sqrt(dt)*z[k];
    asian_aad_ref_basis_t b;asian_aad_ref_value_t v;
    asian_aad_ref_suffix(x,n,s0,rate,dividend,sigma,maturity,&b);
    asian_aad_ref_payoff(&b,strike,rate,maturity,put,cv,exact,&v);
    return v.price;
}
