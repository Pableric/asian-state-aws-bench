#define main asian_genuine_msfr_qualified_vector_main
#include "test_asian_genuine_multistrike_full_risk_vector.c"
#undef main

#include "private/asian_genuine_fixed_block_source_diag.h"

static int fixed_downstream_n(uint32_t n)
{
    fixture_t f;
    if (prepare_fixture(&f,n) != 0) return -1;
    float *x3_x=a64(2u*PATHS*sizeof(float));
    float *x3_growth=a64(2u*PATHS*sizeof(float));
    asian_genuine_msfr_basis_t *x3_basis=a64(sizeof(*x3_basis));
    asian_genuine_fixed_block_source_context_t *source=a64(sizeof(*source));
    if(!x3_x||!x3_growth||!x3_basis||!source)return -1;
    memcpy(x3_x,f.x,2u*PATHS*sizeof(float));
    memcpy(x3_growth,f.growth,2u*PATHS*sizeof(float));
    asian_genuine_msfr_basis_forward_diag(f.basis_context,f.basis);
    memcpy(x3_basis,f.basis,sizeof(*x3_basis));

    asian_genuine_fixed_block_source_request_t request;
    memset(&request,0,sizeof(request));request.target_start_index=8192u;
    request.path_count=PATHS;request.block_count=1u;request.fixing_count=n;
    request.s0=100.0;request.rate=.03;request.dividend_yield=0;
    request.sigma=.20;request.maturity=1.0;
    request.signed_z=asian_genuine_fixed_block_signed_z;
    request.signed_z_bytes=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES;
    if(asian_genuine_fixed_block_source_prepare(source,&request))return -1;
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(source,f.x);
    asian_vector_exp_range_reduced_array_diag(f.x,f.growth);
    asian_vector_exp_range_reduced_array_diag(f.x+PATHS,f.growth+PATHS);
    f.immutable_hash=fixture_hash(&f);
    if(compare_basis(&f)!=0)return -1;

    double max_x=0,max_growth=0,max_basis[8]={0};
    for(uint32_t i=0;i<2u*PATHS;++i){
        const double xd=fabs((double)f.x[i]-x3_x[i]);
        const double gd=fabs((double)f.growth[i]-x3_growth[i]);
        if(xd>max_x)max_x=xd;
        if(gd>max_growth)max_growth=gd;
    }
    for(uint32_t field=0;field<8u;++field)for(uint32_t path=0;path<PATHS;++path){
        const double d=fabs((double)f.basis->values[field][path]-
                            x3_basis->values[field][path]);
        if(d>max_basis[field])max_basis[field]=d;
    }

    /* Materialize no ranked trace.  This same-float oracle validates every
       chronological S/Q/L update; compare_basis links its final state and all
       basis fields back to the ranked AVX-512 producer. */
    double max_s_error=0,max_q_error=0,max_l_error=0;
    for(uint32_t path=0;path<PATHS;++path){
        float fs=100.0f,fq=0.0f,fl=0.0f;double ds=100.0,dq=0.0,dl=0.0;
        for(uint32_t k=0;k<n;++k){
            const uint32_t source_index=route_source(&f.routes[k],path);
            float weight;memcpy(&weight,&f.routes[k].weight_bits,4);
            const float x=f.routes[k].x_base[source_index];
            const float growth=f.routes[k].growth_base[source_index];
            fs*=growth;fq+=fs;fl=fmaf(weight,x,fl);
            const uint32_t word=sobol(8192u+path,f.directions[k]);
            const double u=((double)word+.5)*0x1p-32;
            const double dx=(.03-.5*.20*.20)/n+.20*sqrt(1.0/n)*inverse_normal(u);
            ds*=exp(dx);dq+=ds;dl+=(double)(n-k)/(double)n*dx;
            const double se=fabs((double)fs-ds),qe=fabs((double)fq-dq);
            const double le=fabs((double)fl-dl);
            if(se>max_s_error)max_s_error=se;
            if(qe>max_q_error)max_q_error=qe;
            if(le>max_l_error)max_l_error=le;
        }
    }
    printf("fixed_downstream N=%u max_raw_x3_x_difference=%.9g "
      "max_raw_x3_growth_difference=%.9g max_step_S_error=%.9g "
      "max_step_Q_error=%.9g max_step_L_error=%.9g",
      n,max_x,max_growth,max_s_error,max_q_error,max_l_error);
    for(uint32_t field=0;field<8u;++field)
        printf(" max_raw_x3_basis_field%u_difference=%.9g",field,max_basis[field]);
    putchar('\n');

    int status=float64_validation(&f);
    const uint32_t ks[]={1u,4u,8u,16u,32u};
    for(unsigned i=0;status==0&&i<5u;++i)status=validate_consumers(&f,ks[i]);
    if(status==0)status=validate_kink_strikes(&f);
    if(status==0&&fixture_hash(&f)!=f.immutable_hash)status=-1;
    free(source);free(x3_basis);free(x3_growth);free(x3_x);release_fixture(&f);
    return status;
}

int main(int argc,char **argv)
{
    const char *report=NULL;
    for(int i=1;i<argc;++i)if(!strcmp(argv[i],"--report")&&++i<argc)
        report=argv[i];else return 2;
    if(report&&freopen(report,"w",stdout)==NULL)return 2;
    const uint32_t ns[]={16u,32u,64u,128u,256u};
    for(unsigned i=0;i<5u;++i)if(fixed_downstream_n(ns[i]))return 1;
    puts("asian_genuine_fixed_block_source_downstream=PASS principal_N=16,32,64,128,256 calls_puts=yes arithmetic_cv=yes price_delta_vega_rho=yes K=1,4,8,16,32");
    return 0;
}
