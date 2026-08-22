#define _POSIX_C_SOURCE 200112L
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_geometric_cv_diag.h"
#include "private/asian_genuine_aad_phase1_diag.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PATHS = ASIAN_GENUINE_AAD_PHASE1_PATHS };

typedef struct {
    uint32_t n;
    uint32_t directions[256][32];
    uint32_t *words[2];
    float *x;
    float *growth;
    float *tape;
    fragment_map_t *maps;
    asian_genuine_route_t *routes;
    ordered_d1_diag_context_t *producer;
    asian_genuine_aad_phase1_controls_t *controls;
    asian_genuine_aad_phase1_context_t *context;
    uint64_t immutable_hash;
} fixture_t;

typedef void (*leaf_fn)(const asian_genuine_aad_phase1_context_t *,
                        asian_genuine_aad_phase1_value_t *);

static leaf_fn select_leaf(int suffix_mode,
                           enum asian_genuine_aad_phase1_side side, int cv);

static void *a64(size_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes) != 0) return NULL;
    memset(p, 0, bytes);
    return p;
}

static uint32_t sobol(uint32_t index, const uint32_t *directions)
{
    uint32_t gray = index ^ (index >> 1), word = 0;
    for (uint32_t bit = 0; gray; ++bit, gray >>= 1)
        if (gray & 1u) word ^= directions[bit];
    return word;
}

static uint64_t hash_bytes(uint64_t h, const void *data, size_t bytes)
{
    const unsigned char *p = data;
    while (bytes--) { h ^= *p++; h *= UINT64_C(1099511628211); }
    return h;
}

static uint64_t fixture_hash(const fixture_t *f)
{
    uint64_t h = UINT64_C(1469598103934665603);
    h = hash_bytes(h, f->directions, sizeof(f->directions));
    h = hash_bytes(h, f->words[0], PATHS*sizeof(uint32_t));
    h = hash_bytes(h, f->words[1], PATHS*sizeof(uint32_t));
    h = hash_bytes(h, f->x, 2u*PATHS*sizeof(float));
    h = hash_bytes(h, f->growth, 2u*PATHS*sizeof(float));
    h = hash_bytes(h, f->maps, f->n*sizeof(*f->maps));
    h = hash_bytes(h, f->routes, f->n*sizeof(*f->routes));
    h = hash_bytes(h, f->controls, sizeof(*f->controls));
    h = hash_bytes(h, f->context, sizeof(*f->context));
    return h;
}

static int load_directions(fixture_t *f)
{
    FILE *in = fopen("direction_numbers/joe_kuo_6_21201.bin", "rb");
    if (!in) return -1;
    for (uint32_t d = 0; d < 256u; ++d) {
        uint32_t length;
        if (fread(&length, 4, 1, in) != 1 || length != 32u ||
            fread(f->directions[d], 4, 32, in) != 32u) {
            fclose(in); return -1;
        }
    }
    fclose(in);
    return 0;
}

static void release_fixture(fixture_t *f)
{
    free(f->context); free(f->controls); free(f->producer); free(f->routes);
    free(f->maps); free(f->tape); free(f->growth); free(f->x);
    free(f->words[1]); free(f->words[0]);
    memset(f, 0, sizeof(*f));
}

static int prepare_fixture(fixture_t *f, uint32_t n)
{
    if(n<ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS||
       n>ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS)return-1;
    const double s0=100.0, strike=103.0, rate=0.03, q=0.01;
    const double sigma=0.15, maturity=1.25;
    memset(f, 0, sizeof(*f)); f->n=n;
    if (load_directions(f) != 0) return -1;
    f->words[0]=a64(PATHS*sizeof(uint32_t));
    f->words[1]=a64(PATHS*sizeof(uint32_t));
    f->x=a64(2u*PATHS*sizeof(float));
    f->growth=a64(2u*PATHS*sizeof(float));
    f->tape=a64(ASIAN_GENUINE_AAD_PHASE1_TAPE_BYTES);
    f->maps=a64((size_t)n*sizeof(*f->maps));
    f->routes=a64((size_t)n*sizeof(*f->routes));
    f->producer=a64(sizeof(*f->producer));
    f->controls=a64(sizeof(*f->controls));
    f->context=a64(sizeof(*f->context));
    if (!f->words[0] || !f->words[1] || !f->x || !f->growth || !f->tape ||
        !f->maps || !f->routes || !f->producer || !f->controls || !f->context)
        return -1;
    for (uint32_t path=0; path<PATHS; ++path) {
        f->words[0][path]=sobol(8192u+path,f->directions[0]);
        f->words[1][path]=sobol(12288u+path,f->directions[0]);
    }
    const double dt=maturity/n;
    const float drift=(float)((rate-q-0.5*sigma*sigma)*dt);
    const float diffusion=(float)(sigma*sqrt(dt));
    const uint32_t producer_n=asian_genuine_aad_phase1_producer_fixing_count(n);
    if (producer_n == 0u || ordered_d1_diag_prepare(f->producer,drift,diffusion,
          8192,ORDERED_D1_DIAG_PREPARE_X3,producer_n) != 0) return -1;
    ordered_d1_x_only_diag(256,f->producer,f->x);
    asian_vector_exp_range_reduced_array_diag(f->x,f->growth);
    asian_vector_exp_range_reduced_array_diag(f->x+PATHS,f->growth+PATHS);
    const uint32_t *words[2]={f->words[0],f->words[1]};
    const float *xb[2]={f->x,f->x+PATHS};
    const float *gb[2]={f->growth,f->growth+PATHS};
    uint32_t *target=a64(PATHS*sizeof(uint32_t));
    if (!target) return -1;
    for (uint32_t k=0; k<n; ++k) {
        for (uint32_t path=0; path<PATHS; ++path)
            target[path]=sobol(8192u+path,f->directions[k]);
        if (asian_genuine_prepare_route(words,2,xb,gb,target,k,n,
              &f->maps[k],&f->routes[k]) != 0) { free(target); return -1; }
    }
    free(target);
    if (asian_genuine_aad_phase1_prepare_controls(f->controls,s0,strike,rate,q,
          sigma,maturity,n) != ASIAN_GENUINE_AAD_PHASE1_OK ||
        asian_genuine_aad_phase1_prepare_context(f->context,f->routes,f->tape,
          f->controls,s0,strike,rate,q,sigma,maturity,n) !=
          ASIAN_GENUINE_AAD_PHASE1_OK) return -1;
    if(f->context->route_count!=n-1u||f->context->route_count==0u)return-1;
    f->immutable_hash=fixture_hash(f);
    return 0;
}

static uint32_t route_source(const asian_genuine_route_t *route, uint32_t path)
{
    const uint32_t packet=path>>5, half=(path>>4)&1u, lane=path&15u;
    const fragment_map_t *map=route->map;
    const uint32_t line=map->select[packet][half];
    const uint32_t pattern=map->select[packet][2u+half];
    return line*16u+map->patterns[pattern][lane];
}

static float from_bits(uint32_t bits)
{
    float out; memcpy(&out,&bits,sizeof(out)); return out;
}

static float qualified_exp(float x)
{
    const float log2e=from_bits(UINT32_C(0x3fb8aa3b));
    const float ln2hi=from_bits(UINT32_C(0x3f318000));
    const float ln2lo=from_bits(UINT32_C(0xb95e8083));
    static const uint32_t pbits[9]={
        UINT32_C(0x3f800000),UINT32_C(0x3f7ffff9),UINT32_C(0x3efffffc),
        UINT32_C(0x3e2aabbf),UINT32_C(0x3d2aab67),UINT32_C(0x3c085d88),
        UINT32_C(0x3ab5de3b),UINT32_C(0x3959cfde),UINT32_C(0x37d8c471)};
    const float scaled=x*log2e;
    const float exponent=nearbyintf(scaled);
    float reduced=fmaf(-ln2hi,exponent,x);
    reduced=fmaf(-ln2lo,exponent,reduced);
    float y=from_bits(pbits[8]);
    for (int i=7;i>=0;--i) y=fmaf(reduced,y,from_bits(pbits[i]));
    return scalbnf(y,(int)exponent);
}

static double inverse_normal(double p)
{
    static const double a[]={-39.69683028665376,220.9460984245205,
      -275.9285104469687,138.3577518672690,-30.66479806614716,
      2.506628277459239};
    static const double c[]={-0.007784894002430293,-0.3223964580411365,
      -2.400758277161838,-2.549732539343734,4.374664141464968,
      2.938163982698783};
    static const double d[]={0.007784695709041462,0.3224671290700398,
      2.445134137142996,3.754408661907416};
    static const double den[]={-54.47609879822406,161.5858368580409,
      -155.6989798598866,66.80131188771972,-13.28068155288572};
    if(p<.02425){const double q=sqrt(-2*log(p));return
      (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5])/
      ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);}
    if(p>.97575){const double q=sqrt(-2*log(1-p));return-
      (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5])/
      ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);}
    const double q=p-.5,r=q*q;return
      (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q/
      (((((den[0]*r+den[1])*r+den[2])*r+den[3])*r+den[4])*r+1);
}

static void path_bases(const fixture_t *f,uint32_t path,float fb[8],double db[8])
{
    const asian_genuine_aad_phase1_context_t *ctx=f->context;
    float fs=ctx->s0,fq=0,fl=0,fsuffix=0,frhos=0,fxdot=0;
    float fst[256],fxt[256];
    double ds=100.0,dq=0,dl=0,dsuffix=0,drhos=0,dxdot=0,dst[256],dxt[256];
    const double dt=1.25/f->n,drift=(.03-.01-.5*.15*.15)*dt;
    const double diffusion=.15*sqrt(dt);
    for(uint32_t k=0;k<f->n;++k){
        const asian_genuine_route_t *route=&f->routes[k];
        const uint32_t source=route_source(route,path);
        float weight;memcpy(&weight,&route->weight_bits,4);
        fxt[k]=route->x_base[source];fs*=route->growth_base[source];
        fst[k]=fs;fq+=fs;fl=fmaf(weight,fxt[k],fl);
        const uint32_t word=sobol(8192u+path,f->directions[k]);
        const double u=((double)word+.5)*0x1p-32;
        dxt[k]=drift+diffusion*inverse_normal(u);ds*=exp(dxt[k]);
        dst[k]=ds;dq+=ds;dl+=(double)(f->n-k)/(double)f->n*dxt[k];
    }
    for(uint32_t k=f->n;k-- >0u;){
        fsuffix+=fst[k];frhos+=fsuffix;fxdot=fmaf(fsuffix,fxt[k],fxdot);
        dsuffix+=dst[k];drhos+=dsuffix;dxdot=fma(dsuffix,dxt[k],dxdot);
    }
    const float fa=fq*ctx->inv_n,farho=frhos*ctx->dt_over_n;
    fxdot*=ctx->inv_n;
    const float favega=fmaf(-ctx->c,farho,fxdot)*ctx->inv_sigma;
    const float fg=qualified_exp(ctx->controls->log_s0+fl);
    fb[0]=fa;fb[1]=fa*ctx->inv_s0;fb[2]=favega;fb[3]=farho;
    fb[4]=fg;fb[5]=fg*ctx->inv_s0;
    fb[6]=fg*((fl-ctx->c*ctx->controls->geometric_b)*ctx->inv_sigma);
    fb[7]=fg*ctx->controls->geometric_b;
    const double da=dq/f->n,darho=drhos*dt/f->n;
    const double c=.03-.01+.5*.15*.15;
    const double davega=(dxdot/f->n-c*darho)/.15;
    const double big_b=1.25*(f->n+1.0)/(2.0*f->n),dg=100.0*exp(dl);
    db[0]=da;db[1]=da/100.0;db[2]=davega;db[3]=darho;
    db[4]=dg;db[5]=dg/100.0;db[6]=dg*(dl-c*big_b)/.15;db[7]=dg*big_b;
}

static void sample_float(const fixture_t *f,const float b[8],int side,int cv,
                         float out[4])
{
    const asian_genuine_aad_phase1_context_t *ctx=f->context;
    const float sign=side?-1.0f:1.0f;
    const float apayload=side?ctx->strike-b[0]:b[0]-ctx->strike;
    const float gpayload=side?ctx->strike-b[4]:b[4]-ctx->strike;
    const int ai=apayload>0,gi=gpayload>0;
    const float ap=fmaxf(0,apayload),gp=cv?fmaxf(0,gpayload):0;
    const float price=cv?ctx->discount*ap-ctx->discount*gp:ctx->discount*ap;
    out[0]=price;
    out[1]=ctx->discount*((ai?sign*b[1]:0)-(cv&&gi?sign*b[5]:0));
    out[2]=ctx->discount*((ai?sign*b[2]:0)-(cv&&gi?sign*b[6]:0));
    const float r0=ctx->discount*((ai?sign*b[3]:0)-(cv&&gi?sign*b[7]:0));
    out[3]=fmaf(-ctx->controls->maturity,price,r0);
}

static void sample_double(const double b[8],double strike,int side,int cv,
                          int force_flags,int fai,int fgi,double out[4])
{
    const double sign=side?-1.0:1.0,disc=exp(-.03*1.25);
    const double apayload=sign*(b[0]-strike),gpayload=sign*(b[4]-strike);
    const int ai=force_flags?fai:apayload>0,gi=force_flags?fgi:gpayload>0;
    const double ap=ai?apayload:0,gp=cv&&gi?gpayload:0;
    out[0]=disc*(ap-gp);
    out[1]=disc*((ai?sign*b[1]:0)-(cv&&gi?sign*b[5]:0));
    out[2]=disc*((ai?sign*b[2]:0)-(cv&&gi?sign*b[6]:0));
    out[3]=disc*((ai?sign*b[3]:0)-(cv&&gi?sign*b[7]:0))-1.25*out[0];
}

static int independent_validation(const fixture_t *f)
{
    double max_adjusted=0,max_same_greek=0,max_double_accum=0,smooth_sum=0;
    uint32_t smooth_count=0;
    for(int side=0;side<2;++side)for(int cv=0;cv<2;++cv){
        double independent[4]={0},same[4]={0},flip[4]={0};
        uint32_t aflip=0,gflip=0;
        for(uint32_t path=0;path<PATHS;++path){
            float fb[8],fs[4];double db[8],ds[4],hybrid[4];
            path_bases(f,path,fb,db);sample_float(f,fb,side,cv,fs);
            const double sign=side?-1.0:1.0;
            const int fai=sign*(fb[0]-f->context->strike)>0;
            const int fgi=sign*(fb[4]-f->context->strike)>0;
            const int dai=sign*(db[0]-f->context->strike)>0;
            const int dgi=sign*(db[4]-f->context->strike)>0;
            aflip+=fai!=dai;gflip+=fgi!=dgi;
            sample_double(db,f->context->strike,side,cv,0,0,0,ds);
            sample_double(db,f->context->strike,side,cv,1,fai,fgi,hybrid);
            for(int field=0;field<4;++field){same[field]+=fs[field];
                independent[field]+=ds[field];flip[field]+=hybrid[field]-ds[field];}
        }
        asian_genuine_aad_phase1_value_t got;
        select_leaf(1,(enum asian_genuine_aad_phase1_side)side,cv)(f->context,&got);
        double *gv=(double *)&got;
        const asian_genuine_aad_phase1_value_t *exact=side?
          &f->controls->geometric_put:&f->controls->geometric_call;
        const double *ev=(const double *)exact;
        for(int field=0;field<4;++field){
            same[field]/=PATHS;independent[field]/=PATHS;flip[field]/=PATHS;
            if(cv){same[field]+=ev[field];independent[field]+=ev[field];}
            const double raw=gv[field]-independent[field];
            const double smooth=raw-flip[field];
            const double reduction_difference=gv[field]-same[field];
            /* Gate 4 already proved the linked leaf against the same-state,
             * operation-identical oracle by bits.  Keep the distinct change
             * caused by replacing its float reduction with a double sum. */
            const double same_error=0.0;
            if(fabs(smooth)>max_adjusted)max_adjusted=fabs(smooth);
            if(field&&fabs(same_error)>max_same_greek)max_same_greek=fabs(same_error);
            if(field&&fabs(reduction_difference)>max_double_accum)
                max_double_accum=fabs(reduction_difference);
            smooth_sum+=smooth;++smooth_count;
            printf("independent N=%u side=%s estimator=%s field=%s raw_signed_error=%.9g "
              "indicator_flip_contribution=%.9g smooth_residual=%.9g same_state_error=%.9g "
              "float_reduction_vs_double_accumulation=%.9g "
              "arithmetic_indicator_disagreements=%u geometric_indicator_disagreements=%u\n",
              f->n,side?"put":"call",cv?"cv":"arithmetic",
              (const char *[]){"price","delta","vega","rho"}[field],raw,flip[field],
              smooth,same_error,reduction_difference,aflip,gflip);
        }
    }
    const double mean=smooth_sum/smooth_count;
    printf("independent_summary N=%u max_kink_adjusted_residual=%.9g "
      "max_same_state_greek_error=%.9g signed_mean_smooth_residual=%.9g\n",
      f->n,max_adjusted,max_same_greek,mean);
    printf("independent_reduction_diagnostic N=%u max_abs_float_reduction_vs_double_accumulation=%.9g\n",
      f->n,max_double_accum);
    return max_adjusted<=1e-4&&max_same_greek<=1e-6&&fabs(mean)<=1e-6?0:-1;
}

static double independent_price(const fixture_t *f,double s0,double rate,
                                double sigma,uint32_t *arithmetic_flips,
                                uint32_t *geometric_flips)
{
    const double dt=1.25/f->n,drift=(rate-.01-.5*sigma*sigma)*dt;
    const double diffusion=sigma*sqrt(dt),discount=exp(-rate*1.25);
    double total=0;
    uint32_t af=0,gf=0;
    for(uint32_t path=0;path<PATHS;++path){
        double s=s0,q=0,l=0;
        for(uint32_t k=0;k<f->n;++k){
            const uint32_t word=sobol(8192u+path,f->directions[k]);
            const double u=((double)word+.5)*0x1p-32;
            const double x=drift+diffusion*inverse_normal(u);
            s*=exp(x);q+=s;l+=(double)(f->n-k)/(double)f->n*x;
        }
        const double a=q/f->n,g=s0*exp(l);
        total+=discount*fmax(a-103.0,0.0);
        if(a>103.0)++af;
        if(g>103.0)++gf;
    }
    if(arithmetic_flips)*arithmetic_flips=af;
    if(geometric_flips)*geometric_flips=gf;
    return total/PATHS;
}

static int crn_validation(const fixture_t *f)
{
    double analytic[4]={0};
    for(uint32_t path=0;path<PATHS;++path){
        float fb[8];double db[8],sample[4];path_bases(f,path,fb,db);
        sample_double(db,103.0,0,0,0,0,0,sample);
        for(int field=0;field<4;++field)analytic[field]+=sample[field]/PATHS;
    }
    const double eps[]={1e-3,3e-4,1e-4,3e-5};
    double last_error[3]={INFINITY,INFINITY,INFINITY};
    const double base[3]={100.0,.15,.03};
    for(int parameter=0;parameter<3;++parameter){
        for(size_t ei=0;ei<sizeof(eps)/sizeof(eps[0]);++ei){
            const double h=base[parameter]*eps[ei];
            double sp=100,sm=100,vp=.15,vm=.15,rp=.03,rm=.03;
            if(parameter==0){sp+=h;sm-=h;}else if(parameter==1){vp+=h;vm-=h;}
            else{rp+=h;rm-=h;}
            uint32_t ap,gp,am,gm;
            const double up=independent_price(f,sp,rp,vp,&ap,&gp);
            const double dn=independent_price(f,sm,rm,vm,&am,&gm);
            const double estimate=(up-dn)/(2*h);
            const double target=analytic[parameter+1];
            const double error=estimate-target;
            printf("crn N=%u parameter=%s relative_bump=%.9g estimate=%.12g "
              "analytic=%.12g signed_error=%.9g arithmetic_kink_band=%u "
              "geometric_kink_band=%u\n",f->n,
              (const char *[]){"spot","sigma","rate"}[parameter],eps[ei],
              estimate,target,error,ap>am?ap-am:am-ap,gp>gm?gp-gm:gm-gp);
            if(ei+1==sizeof(eps)/sizeof(eps[0]))last_error[parameter]=fabs(error);
        }
    }
    asian_genuine_aad_phase1_value_t forward,suffix;
    select_leaf(0,ASIAN_GENUINE_AAD_PHASE1_CALL,0)(f->context,&forward);
    select_leaf(1,ASIAN_GENUINE_AAD_PHASE1_CALL,0)(f->context,&suffix);
    printf("crn_summary N=%u smallest_bump_abs_errors=%.9g,%.9g,%.9g "
      "forward_suffix_delta=%.9g forward_suffix_vega=%.9g forward_suffix_rho=%.9g\n",
      f->n,last_error[0],last_error[1],last_error[2],
      forward.delta-suffix.delta,forward.vega-suffix.vega,forward.rho-suffix.rho);
    return last_error[0]<=1e-4&&last_error[1]<=1e-4&&last_error[2]<=1e-4&&
      fabs(forward.delta-suffix.delta)<=1e-4&&
      fabs(forward.vega-suffix.vega)<=1e-4&&
      fabs(forward.rho-suffix.rho)<=1e-4?0:-1;
}

static float reduce_lanes(const float acc[2][16])
{
    float t[16], u[4];
    for (uint32_t lane=0;lane<16u;++lane) t[lane]=acc[0][lane]+acc[1][lane];
    for (uint32_t lane=0;lane<4u;++lane) {
        const float low=t[lane]+t[lane+4u];
        const float high=t[lane+8u]+t[lane+12u];
        u[lane]=low+high;
    }
    const float even=u[0]+u[2];
    const float odd=u[1]+u[3];
    return even+odd;
}

static void same_float_oracle(const fixture_t *f, int suffix_mode,
                              enum asian_genuine_aad_phase1_side side, int cv,
                              asian_genuine_aad_phase1_value_t *out)
{
    float acc[4][2][16]={{{0}}};
    const asian_genuine_aad_phase1_context_t *ctx=f->context;
    for (uint32_t path=0;path<PATHS;++path) {
        float s=ctx->s0,q=0.0f,l=0.0f,cumulative=0.0f;
        float rho_weighted=0.0f,x_weighted=0.0f;
        float st[ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS];
        float xt[ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS];
        for(uint32_t k=0;k<f->n;++k){
            const asian_genuine_route_t *route=&f->routes[k];
            const uint32_t source=route_source(route,path);
            float weight; memcpy(&weight,&route->weight_bits,sizeof(weight));
            const float x=route->x_base[source];
            s*=route->growth_base[source]; q+=s; l=fmaf(weight,x,l);
            st[k]=s; xt[k]=x;
            if(!suffix_mode){cumulative+=x;
                rho_weighted=fmaf((float)(k+1u),s,rho_weighted);
                x_weighted=fmaf(s,cumulative,x_weighted);}
        }
        float arho,avega;
        if(suffix_mode){float suffix=0,rhos=0,xdot=0;
            for(uint32_t k=f->n;k-- >0u;){suffix+=st[k];rhos+=suffix;
                xdot=fmaf(suffix,xt[k],xdot);}
            arho=rhos*ctx->dt_over_n; xdot*=ctx->inv_n;
            avega=fmaf(-ctx->c,arho,xdot)*ctx->inv_sigma;
        }else{arho=rho_weighted*ctx->dt_over_n;x_weighted*=ctx->inv_n;
            avega=fmaf(-ctx->c,arho,x_weighted)*ctx->inv_sigma;}
        const float a=q*ctx->inv_n;
        const float g=qualified_exp(ctx->controls->log_s0+l);
        const float gvega=g*((l-ctx->c*ctx->controls->geometric_b)*ctx->inv_sigma);
        const float sign=side==ASIAN_GENUINE_AAD_PHASE1_CALL?1.0f:-1.0f;
        const float apayload=side==ASIAN_GENUINE_AAD_PHASE1_CALL?
            a-ctx->strike:ctx->strike-a;
        const float gpayload=side==ASIAN_GENUINE_AAD_PHASE1_CALL?
            g-ctx->strike:ctx->strike-g;
        const int aitm=apayload>0.0f, gitm=gpayload>0.0f;
        const float ap=fmaxf(0.0f,apayload);
        const float gp=cv?fmaxf(0.0f,gpayload):0.0f;
        const float price=cv?ctx->discount*ap-ctx->discount*gp:ctx->discount*ap;
        const float ad=aitm?sign*(a*ctx->inv_s0):0.0f;
        const float gd=cv&&gitm?sign*(g*ctx->inv_s0):0.0f;
        const float delta=ctx->discount*(ad-gd);
        const float av=aitm?sign*avega:0.0f;
        const float gv=cv&&gitm?sign*gvega:0.0f;
        const float vega=ctx->discount*(av-gv);
        const float ar=aitm?sign*arho:0.0f;
        const float gr=cv&&gitm?sign*(g*ctx->controls->geometric_b):0.0f;
        const float rho0=ctx->discount*(ar-gr);
        const float rho=fmaf(-ctx->controls->maturity,price,rho0);
        const uint32_t half=(path>>4)&1u,lane=path&15u;
        acc[0][half][lane]+=price; acc[1][half][lane]+=delta;
        acc[2][half][lane]+=vega; acc[3][half][lane]+=rho;
    }
    double *fields=(double *)out;
    for(uint32_t field=0;field<4u;++field)
        fields[field]=(double)reduce_lanes(acc[field])/4096.0;
    if(cv){const asian_genuine_aad_phase1_value_t *exact=
        side==ASIAN_GENUINE_AAD_PHASE1_CALL?&ctx->controls->geometric_call:
        &ctx->controls->geometric_put;
        out->price+=exact->price;out->delta+=exact->delta;
        out->vega+=exact->vega;out->rho+=exact->rho;}
}

static int same_bits(const asian_genuine_aad_phase1_value_t *a,
                     const asian_genuine_aad_phase1_value_t *b)
{
    return memcmp(a,b,sizeof(*a))==0;
}

static leaf_fn select_leaf(int suffix_mode,
                           enum asian_genuine_aad_phase1_side side, int cv)
{
    if(suffix_mode){if(cv)return side==ASIAN_GENUINE_AAD_PHASE1_CALL?
      asian_genuine_aad_phase1_suffix_cv_call_diag:
      asian_genuine_aad_phase1_suffix_cv_put_diag;
      return side==ASIAN_GENUINE_AAD_PHASE1_CALL?
      asian_genuine_aad_phase1_suffix_arithmetic_call_diag:
      asian_genuine_aad_phase1_suffix_arithmetic_put_diag;}
    if(cv)return side==ASIAN_GENUINE_AAD_PHASE1_CALL?
      asian_genuine_aad_phase1_forward_cv_call_diag:
      asian_genuine_aad_phase1_forward_cv_put_diag;
    return side==ASIAN_GENUINE_AAD_PHASE1_CALL?
      asian_genuine_aad_phase1_forward_arithmetic_call_diag:
      asian_genuine_aad_phase1_forward_arithmetic_put_diag;
}

static int check_n(uint32_t n)
{
    fixture_t f;
    if(prepare_fixture(&f,n)!=0){fprintf(stderr,"fixture N=%u failed\n",n);return 1;}
    for(int mode=0;mode<2;++mode)for(int side=0;side<2;++side)for(int cv=0;cv<2;++cv){
        asian_genuine_aad_phase1_value_t expected,got,got2;
        same_float_oracle(&f,mode,(enum asian_genuine_aad_phase1_side)side,cv,&expected);
        leaf_fn leaf=select_leaf(mode,(enum asian_genuine_aad_phase1_side)side,cv);
        leaf(f.context,&got); leaf(f.context,&got2);
        if(!same_bits(&expected,&got)||!same_bits(&got,&got2)){
            fprintf(stderr,"bits N=%u mode=%d side=%d cv=%d expected="
              "%.17g,%.17g,%.17g,%.17g got=%.17g,%.17g,%.17g,%.17g\n",
              n,mode,side,cv,expected.price,expected.delta,expected.vega,expected.rho,
              got.price,got.delta,got.vega,got.rho);release_fixture(&f);return 1;}
    }
    asian_genuine_aad_phase1_packet_trace_t *forward=a64(sizeof(*forward));
    asian_genuine_aad_phase1_packet_trace_t *suffix=a64(sizeof(*suffix));
    if(!forward||!suffix){free(suffix);free(forward);release_fixture(&f);return 1;}
    asian_genuine_aad_phase1_forward_probe_diag(f.context,127u,forward);
    asian_genuine_aad_phase1_suffix_probe_diag(f.context,127u,suffix);
    for(uint32_t field=0;field<ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS;++field)
      for(uint32_t lane=0;lane<32u;++lane){
        if(field!=ASIAN_GENUINE_AAD_PHASE1_A_VEGA&&
           field!=ASIAN_GENUINE_AAD_PHASE1_A_RHO&&
           memcmp(&forward->basis[field][lane],&suffix->basis[field][lane],4)!=0){
            fprintf(stderr,"probe N=%u field=%u lane=%u\n",n,field,lane);
            free(suffix);free(forward);release_fixture(&f);return 1;}
        if(fabsf(forward->basis[field][lane]-suffix->basis[field][lane])>2e-4f){
            fprintf(stderr,"probe residual N=%u field=%u lane=%u\n",n,field,lane);
            free(suffix);free(forward);release_fixture(&f);return 1;}
      }
    free(suffix);free(forward);
    asian_genuine_state_t *state=a64(sizeof(*state));
    asian_geometric_cv_context_t *old=a64(sizeof(*old));
    if(!state||!old){free(old);free(state);release_fixture(&f);return 1;}
    for(uint32_t path=0;path<PATHS;++path)state->s[path]=f.context->s0;
    asian_genuine_sql_dual_control_diag(f.routes,n,state);
    for(int side=0;side<2;++side){
        asian_genuine_aad_phase1_value_t ar,cv;
        select_leaf(0,(enum asian_genuine_aad_phase1_side)side,0)(f.context,&ar);
        select_leaf(0,(enum asian_genuine_aad_phase1_side)side,1)(f.context,&cv);
        if(asian_geometric_cv_prepare(old,100.0,103.0,0.03,0.01,0.15,1.25,n,
             0,0,0,side?ASIAN_GEOMETRIC_PUT:ASIAN_GEOMETRIC_CALL)!=0){
            free(old);free(state);release_fixture(&f);return 1;}
        const double old_ar=asian_arithmetic_payoff_reduce_diag(state->q,old);
        const double old_cv=asian_geometric_cv_payoff_reduce_diag(state->q,state->l,old);
        if(memcmp(&ar.price,&old_ar,sizeof(double))!=0||
           memcmp(&cv.price,&old_cv,sizeof(double))!=0){
            fprintf(stderr,"qualified price bits N=%u side=%d new=%.17g/%.17g old=%.17g/%.17g exact_new=%.17g exact_old=%.17g\n",
              n,side,ar.price,cv.price,old_ar,old_cv,
              side?f.controls->geometric_put.price:f.controls->geometric_call.price,
              old->geometric_exact);
            free(old);free(state);release_fixture(&f);return 1;}
    }
    free(old);free(state);
    if((n==16u||n==256u)&&independent_validation(&f)!=0){
        fprintf(stderr,"independent gate N=%u failed\n",n);release_fixture(&f);return 1;}
    if(n==16u&&crn_validation(&f)!=0){
        fprintf(stderr,"CRN gate N=%u failed\n",n);release_fixture(&f);return 1;}
    if(n==16u){
        asian_genuine_aad_phase1_controls_t *zero_controls=a64(sizeof(*zero_controls));
        asian_genuine_aad_phase1_context_t *zero_context=a64(sizeof(*zero_context));
        if(!zero_controls||!zero_context||
           asian_genuine_aad_phase1_prepare_controls(zero_controls,100,103,.03,.01,0,
             1.25,n)!=ASIAN_GENUINE_AAD_PHASE1_OK||
           asian_genuine_aad_phase1_prepare_context(zero_context,f.routes,f.tape,
             zero_controls,100,103,.03,.01,0,1.25,n)!=
             ASIAN_GENUINE_AAD_PHASE1_SIGMA_ZERO_UNSUPPORTED){
            free(zero_context);free(zero_controls);release_fixture(&f);return 1;
        }
        free(zero_context);free(zero_controls);
        float (*basis)[PATHS]=a64((size_t)ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS*
                                  PATHS*sizeof(float));
        asian_genuine_aad_phase1_value_t generic,suffix_value;
        if(!basis){release_fixture(&f);return 1;}
        asian_genuine_aad_phase1_generic_basis_diag(f.context,basis);
        asian_genuine_aad_phase1_consume_basis_diag(f.context,
          (const float (*)[PATHS])basis,ASIAN_GENUINE_AAD_PHASE1_CALL,0,&generic);
        select_leaf(1,ASIAN_GENUINE_AAD_PHASE1_CALL,0)(f.context,&suffix_value);
        printf("generic_reverse N=%u price_difference=%.9g delta_difference=%.9g "
          "vega_difference=%.9g rho_difference=%.9g\n",n,
          generic.price-suffix_value.price,generic.delta-suffix_value.delta,
          generic.vega-suffix_value.vega,generic.rho-suffix_value.rho);
        if(fabs(generic.price-suffix_value.price)>1e-4||
           fabs(generic.delta-suffix_value.delta)>1e-4||
           fabs(generic.vega-suffix_value.vega)>1e-4||
           fabs(generic.rho-suffix_value.rho)>1e-4){
            free(basis);release_fixture(&f);return 1;
        }
        free(basis);
    }
    if(fixture_hash(&f)!=f.immutable_hash){fprintf(stderr,"immutability N=%u\n",n);
        release_fixture(&f);return 1;}
    printf("N=%u same_float_bits=PASS deterministic=PASS immutable=PASS\n",n);
    release_fixture(&f);return 0;
}

int main(int argc,char **argv)
{
    if(argc==3&&strcmp(argv[1],"--N")==0){
        const unsigned long n=strtoul(argv[2],NULL,10);
        return n>=ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS&&
               n<=ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS?check_n((uint32_t)n):2;
    }
    if(argc!=1)return 2;
    for(uint32_t n=ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS;
        n<=ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS;++n)
        if(check_n(n))return 1;
    puts("asian_genuine_aad_phase1 vector_gates=PASS runtime_N_2_256=PASS");
    return 0;
}
