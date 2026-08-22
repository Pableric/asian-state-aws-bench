#define _POSIX_C_SOURCE 200112L
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_genuine_fixed_block_source_diag.h"
#include "private/asian_genuine_multistrike_full_risk_diag.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GUARD = 16 };

static void *a64(size_t bytes)
{
    void *p=NULL;if(posix_memalign(&p,64,bytes)!=0)return NULL;
    memset(p,0,bytes);return p;
}

static uint64_t hash_bytes(const void *data,size_t bytes)
{
    const unsigned char *p=data;uint64_t h=UINT64_C(1469598103934665603);
    while(bytes--){h^=*p++;h*=UINT64_C(1099511628211);}return h;
}

static asian_genuine_fixed_block_source_request_t request_for(
    uint32_t n,double rate,double q,double sigma,double maturity,const float *z)
{
    asian_genuine_fixed_block_source_request_t r;
    memset(&r,0,sizeof(r));r.target_start_index=8192u;r.path_count=4096u;
    r.block_count=1u;r.fixing_count=n;r.s0=100.0;r.rate=rate;
    r.dividend_yield=q;r.sigma=sigma;r.maturity=maturity;r.signed_z=z;
    r.signed_z_bytes=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES;return r;
}

static uint32_t ordered_bits(float x)
{
    uint32_t bits;memcpy(&bits,&x,4);return (bits&UINT32_C(0x80000000))?
        ~bits:bits^UINT32_C(0x80000000);
}

static int check_invalid(void)
{
    asian_genuine_fixed_block_source_context_t *ctx=a64(sizeof(*ctx));
    float *copy=a64(ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES+64u);
    if(!ctx||!copy)return -1;
    memcpy(copy,asian_genuine_fixed_block_signed_z,
           ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    asian_genuine_fixed_block_source_request_t r=request_for(16,.03,0,.2,1,
        asian_genuine_fixed_block_signed_z);
#define REJECT(expr,code) do{asian_genuine_fixed_block_source_request_t q=r;expr; \
 if(asian_genuine_fixed_block_source_prepare(ctx,&q)!=(code))return -1;}while(0)
    REJECT(q.target_start_index=12288u,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BLOCK_UNSUPPORTED);
    REJECT(q.path_count=4095u,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BLOCK_UNSUPPORTED);
    REJECT(q.block_count=2u,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BLOCK_UNSUPPORTED);
    REJECT(q.block_ordinal=1u,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BLOCK_UNSUPPORTED);
    REJECT(q.flags=ASIAN_GENUINE_FIXED_BLOCK_REQUEST_CONTINUATION,
           ASIAN_GENUINE_FIXED_BLOCK_SOURCE_FEATURE_UNSUPPORTED);
    REJECT(q.flags=ASIAN_GENUINE_FIXED_BLOCK_REQUEST_SCRAMBLE,
           ASIAN_GENUINE_FIXED_BLOCK_SOURCE_FEATURE_UNSUPPORTED);
    REJECT(q.digital_shift=1u,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_FEATURE_UNSUPPORTED);
    REJECT(q.fixing_count=1u,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_FIXINGS_UNSUPPORTED);
    REJECT(q.fixing_count=257u,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_FIXINGS_UNSUPPORTED);
    REJECT(q.sigma=0.0,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_SIGMA_ZERO_UNSUPPORTED);
    REJECT(q.signed_z=(const float *)((const unsigned char *)copy+4u),
           ASIAN_GENUINE_FIXED_BLOCK_SOURCE_TABLE_INVALID);
    copy[17]=nextafterf(copy[17],INFINITY);r.signed_z=copy;
    if(asian_genuine_fixed_block_source_prepare(ctx,&r)!=
       ASIAN_GENUINE_FIXED_BLOCK_SOURCE_TABLE_INVALID)return -1;
#undef REJECT
    free(copy);free(ctx);return 0;
}

static int check_domain_delegation(void)
{
    static const double s0s[]={DBL_MIN,1e-38,100.0,1e38,DBL_MAX};
    static const double sigmas[]={0.05,0.20,0.40,1.0,10.0};
    asian_genuine_fixed_block_source_context_t *ctx=a64(sizeof(*ctx));
    asian_genuine_msfr_basis_controls_t *qualified=a64(sizeof(*qualified));
    if(!ctx||!qualified)return -1;
    for(uint32_t n=2;n<=256u;++n)for(unsigned si=0;si<5u;++si)
      for(unsigned vi=0;vi<5u;++vi){
        asian_genuine_fixed_block_source_request_t r=request_for(n,.03,0,
          sigmas[vi],1.0,asian_genuine_fixed_block_signed_z);r.s0=s0s[si];
        const int q=asian_genuine_msfr_prepare_basis_controls(qualified,r.s0,
          r.rate,r.dividend_yield,r.sigma,r.maturity,n);
        const int got=asian_genuine_fixed_block_source_prepare(ctx,&r);
        if((q==ASIAN_GENUINE_MSFR_OK)!=(got==ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK)){
          fprintf(stderr,"domain delegation mismatch N=%u s0=%g sigma=%g q=%d got=%d\n",
                  n,r.s0,r.sigma,q,got);return -1;}
      }
    free(qualified);free(ctx);return 0;
}

int main(int argc,char **argv)
{
    const char *report=NULL;for(int i=1;i<argc;++i)
      if(!strcmp(argv[i],"--report")&&++i<argc)report=argv[i];else return 2;
    FILE *csv=report?fopen(report,"w"):stdout;if(!csv)return 2;
    fputs("N,contract,rate,q,sigma,maturity,max_abs_x3_difference,max_ulp_x3_difference,signed_mean_x3_difference,positive_count,negative_count,worst_payload,formerly_hard_tail\n",csv);
    static const double contracts[4][4]={
      {-.02,.01,.05,.25},{0,0,.40,.25},{.03,0,.20,1},{.03,.01,.05,5}};
    ordered_d1_diag_context_t *producer=a64(sizeof(*producer));
    asian_genuine_fixed_block_source_context_t *ctx=a64(sizeof(*ctx));
    asian_genuine_fixed_block_exact_x_context_t *exact_ctx=a64(sizeof(*exact_ctx));
    float *storage=a64(4u*(ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES+128u));
    if(!producer||!ctx||!exact_ctx||!storage)return 2;
    float *one=storage+GUARD,*exact=storage+8192u+2u*GUARD;
    float *x3=storage+2u*(8192u+2u*GUARD),*prepared=storage+3u*(8192u+2u*GUARD);
    for(size_t i=0;i<4u*(8192u+2u*GUARD);++i)storage[i]=-12345.25f;
    const uint64_t table_hash=hash_bytes(asian_genuine_fixed_block_signed_z,
      ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    for(uint32_t n=2;n<=256u;++n)for(unsigned contract=0;contract<4u;++contract){
      asian_genuine_fixed_block_source_request_t r=request_for(n,contracts[contract][0],
        contracts[contract][1],contracts[contract][2],contracts[contract][3],
        asian_genuine_fixed_block_signed_z);
      if(asian_genuine_fixed_block_source_prepare(ctx,&r)||
         asian_genuine_fixed_block_exact_x_prepare(exact_ctx,ctx,prepared,
           ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES))return 1;
      asian_genuine_fixed_block_signed_z_one_fma_source_diag(ctx,one);
      asian_genuine_fixed_block_prepared_exact_x_lookup_diag(exact_ctx,exact);
      if(memcmp(one,exact,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES)||
         memcmp(one,prepared,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES))return 1;
      const uint32_t producer_n=asian_genuine_msfr_producer_fixing_count(n);
      if(ordered_d1_diag_prepare(producer,ctx->drift,ctx->diffusion,8192u,
           ORDERED_D1_DIAG_PREPARE_X3,producer_n))return 1;
      ordered_d1_x_only_diag(256u,producer,x3);
      double max_abs=0,sum=0;uint32_t max_ulp=0,worst=0,pos=0,neg=0;
      for(uint32_t i=0;i<8192u;++i){
        const double d=(double)one[i]-x3[i];const double ad=fabs(d);
        uint32_t a=ordered_bits(one[i]),b=ordered_bits(x3[i]);
        const uint32_t ulp=a>b?a-b:b-a;
        if(ad>max_abs){max_abs=ad;worst=i;}if(ulp>max_ulp)max_ulp=ulp;
        sum+=d;if(d>0)++pos;else if(d<0)++neg;
      }
      int hard=0;static const uint32_t hard_positions[64]={170,2560,5631,8021,1535,3925,4266,6656,1877,3583,4608,6314,512,2218,5973,7679,853,2559,5632,7338,1536,3242,4949,6655,1194,3584,4607,6997,511,2901,5290,7680,341,3071,5120,7850,1024,3754,4437,7167,1706,3072,5119,6485,1023,2389,5802,7168,682,2048,6143,7509,2047,3413,4778,6144,1365,4095,4096,6826,0,2730,5461,8191};
      for(unsigned h=0;h<64u;++h)if(worst==hard_positions[h])hard=1;
      fprintf(csv,"%u,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%u,%.17g,%u,%u,%u,%s\n",
        n,contract,r.rate,r.dividend_yield,r.sigma,r.maturity,max_abs,max_ulp,
        sum/8192.0,pos,neg,worst,hard?"yes":"no");
    }
    if(report&&fclose(csv))return 2;
    if(hash_bytes(asian_genuine_fixed_block_signed_z,
       ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES)!=table_hash||check_invalid()||
       check_domain_delegation())return 1;
    puts("asian_genuine_fixed_block_source=PASS N=2..256 contracts=4 exact_x_bit_identity=yes domain_delegation=yes invalid_rejection=yes");
    free(storage);free(exact_ctx);free(ctx);free(producer);return 0;
}
