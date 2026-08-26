#define _GNU_SOURCE

#include "private/asian_variable_sobol_block_count_diag.h"

#include <cpuid.h>
#include <immintrin.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { WARMUPS=16, SAMPLES=101, WORKLOADS=4, LIFECYCLES=3, CACHES=2 };
enum workload { W_PRICE, W_DELTA, W_GEOCV, W_FULL_RISK };
enum lifecycle { L_PREPARED, L_REUSE, L_FRESH };
enum cache_mode { C_WARM, C_PRESSURE };

typedef struct { double wall, tsc; } timing_t;
typedef struct { double rate, dividend, sigma, maturity; } market_t;

static const market_t markets[2] = {
    {0.03,0.0,0.20,1.0}, {-0.01,0.015,0.35,0.75}
};
static const char *workload_names[] = {
    "ARITHMETIC_PRICE","ARITHMETIC_PRICE_DELTA","GEOCV_PRICE","FULL_RISK"
};
static const char *lifecycle_names[] = {"prepared","reused_carrier","fresh"};
static const char *cache_names[] = {"candidate_warm","pressure_32KiB"};

typedef struct {
    asian_variable_engine_t *engine;
    asian_variable_carrier_t *carrier;
    asian_variable_carrier_t *scratch_carrier;
    asian_variable_strip_request_t *strip;
    asian_variable_strip_request_t *scratch_strip;
    asian_variable_full_risk_request_t *full;
    asian_variable_full_risk_request_t *scratch_full;
    asian_variable_output_t *output;
    uint32_t pressure[8192];
    uint32_t n;
    uint32_t blocks;
    enum workload workload;
} fixture_t;

static fixture_t fixture;
static volatile uint64_t sink;

static void *a64(size_t bytes)
{
    void *out = NULL;
    if (posix_memalign(&out,64u,bytes)!=0) return NULL;
    memset(out,0,bytes); return out;
}

static uint64_t wall_now(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC_RAW,&value)!=0) abort();
    return (uint64_t)value.tv_sec*UINT64_C(1000000000)+(uint64_t)value.tv_nsec;
}

static inline uint64_t tsc_begin(void)
{
    _mm_lfence(); uint64_t value=__rdtsc(); _mm_lfence(); return value;
}

static inline uint64_t tsc_end(void)
{
    unsigned aux; uint64_t value=__rdtscp(&aux); _mm_lfence(); return value;
}

static int compare_double(const void *a,const void *b)
{
    const double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}

static double median(double values[SAMPLES])
{
    qsort(values,SAMPLES,sizeof(values[0]),compare_double);
    return values[SAMPLES/2];
}

static void warm_bytes(const void *memory,size_t bytes)
{
    const volatile unsigned char *p=memory; uint64_t value=sink;
    for(size_t i=0;i<bytes;i+=64u) value+=p[i];
    sink=value;
}

static void pressure(void)
{
    uint64_t value=sink;
    for(uint32_t i=0;i<8192u;i+=16u){fixture.pressure[i]+=i+1u;value+=fixture.pressure[i];}
    sink=value;
}

static int is_spr(void)
{
    unsigned eax,ebx,ecx,edx;
    if(!__get_cpuid(1u,&eax,&ebx,&ecx,&edx))return 0;
    const unsigned family=((eax>>8)&15u)+((eax>>20)&255u);
    const unsigned model=((eax>>4)&15u)|((eax>>12)&240u);
    return family==6u&&model==143u;
}

static int pin_cpu(int cpu)
{
    cpu_set_t set;CPU_ZERO(&set);CPU_SET(cpu,&set);
    return sched_setaffinity(0,sizeof(set),&set);
}

static enum asian_variable_carrier_capability capability(enum workload w)
{
    return (w==W_GEOCV||w==W_FULL_RISK)?ASIAN_VARIABLE_X_GROWTH:
        ASIAN_VARIABLE_GROWTH_ONLY;
}

static asian_affine_family_carrier_input_t carrier_input(uint32_t n,
                                                          uint32_t variant)
{
    const market_t *m=&markets[variant];
    return (asian_affine_family_carrier_input_t){
        m->rate,m->dividend,m->sigma,m->maturity,n
    };
}

static asian_affine_family_request_input_t request_input(uint32_t n,
                                                          uint32_t variant,
                                                          enum workload w,
                                                          int reuse)
{
    const market_t *m=reuse?&markets[0]:&markets[variant];
    static float strikes[2]={100.0f,110.0f};
    return (asian_affine_family_request_input_t){
        .s0=variant?117.0:100.0,.rate=m->rate,.dividend_yield=m->dividend,
        .sigma=m->sigma,.maturity=m->maturity,.future_fixings=n,
        .completed_fixings=0,.initial_arithmetic_sum=0.0,.past_log_sum=0.0,
        .strikes=&strikes[variant],.strike_count=1,
        .workload=w==W_DELTA?ASIAN_AFFINE_FAMILY_PRICE_DELTA:
                              ASIAN_AFFINE_FAMILY_PRICE,
    };
}

static int prepare_request(const asian_variable_carrier_t *carrier,
                           const asian_affine_family_request_input_t *input,
                           void *request)
{
    if(fixture.workload==W_FULL_RISK)
        return asian_variable_full_risk_k1_request_prepare(fixture.engine,
            carrier,input,request);
    const enum asian_variable_family family=fixture.workload==W_GEOCV?
        ASIAN_VARIABLE_GEOCV:ASIAN_VARIABLE_ARITHMETIC;
    return asian_variable_strip_request_prepare(fixture.engine,carrier,input,
        family,ASIAN_AFFINE_FAMILY_AFFINE,request);
}

static void warm_request(const void *memory)
{
    if(fixture.workload==W_FULL_RISK){
        const asian_variable_full_risk_request_t *request=memory;
        warm_bytes(request,64u);
        warm_bytes(&request->controls,sizeof(request->controls));
        warm_bytes(&request->parity,sizeof(request->parity));
        for(uint32_t block=0;block<fixture.blocks;++block){
            const asian_affine_family_full_risk_k1_request_t *part=
                &request->block[block];
            warm_bytes(part->routes,fixture.n*sizeof(part->routes[0]));
            warm_bytes(&part->controls,sizeof(part->controls));
            warm_bytes(&part->context,sizeof(part->context));
            warm_bytes(&part->parity,sizeof(part->parity));
        }
    }else{
        const asian_variable_strip_request_t *request=memory;
        warm_bytes(request,64u);
        for(uint32_t block=0;block<fixture.blocks;++block){
            if(fixture.workload==W_GEOCV){
                const asian_affine_family_geocv_request_t *part=
                    &request->block[block].geocv;
                warm_bytes(&part->routes,fixture.n*sizeof(part->routes.affine[0]));
                warm_bytes(&part->strip,sizeof(part->strip));
                warm_bytes(&part->immediate,sizeof(part->immediate));
                warm_bytes(&part->packet,sizeof(part->packet));
            }else{
                const asian_affine_family_arithmetic_request_t *part=
                    &request->block[block].arithmetic;
                warm_bytes(&part->routes,fixture.n*sizeof(part->routes.affine[0]));
                warm_bytes(&part->growth,sizeof(part->growth));
                warm_bytes(&part->strip,sizeof(part->strip));
            }
        }
    }
}

static void *prepared_request(void)
{
    return fixture.workload==W_FULL_RISK?(void *)fixture.full:
                                          (void *)fixture.strip;
}

static void *scratch_request(void)
{
    return fixture.workload==W_FULL_RISK?(void *)fixture.scratch_full:
                                          (void *)fixture.scratch_strip;
}

static int fixture_create(asian_variable_engine_t *engine,uint32_t n,
                          uint32_t blocks,enum workload workload)
{
    memset(&fixture,0,sizeof(fixture));
    fixture.engine=engine;fixture.n=n;fixture.blocks=blocks;fixture.workload=workload;
    fixture.strip=a64(sizeof(*fixture.strip));
    fixture.scratch_strip=a64(sizeof(*fixture.scratch_strip));
    fixture.full=a64(sizeof(*fixture.full));
    fixture.scratch_full=a64(sizeof(*fixture.scratch_full));
    fixture.output=a64(sizeof(*fixture.output));
    if(!fixture.strip||!fixture.scratch_strip||!fixture.full||
       !fixture.scratch_full||!fixture.output||
       asian_variable_carrier_create(blocks,capability(workload),
           &fixture.carrier)!=0||
       asian_variable_carrier_create(blocks,capability(workload),
           &fixture.scratch_carrier)!=0)return -1;
    const asian_affine_family_carrier_input_t market=carrier_input(n,0u);
    const asian_affine_family_request_input_t input=request_input(n,0u,workload,0);
    if(asian_variable_carrier_prepare(engine,blocks,&market,fixture.carrier)!=0||
       prepare_request(fixture.carrier,&input,prepared_request())!=0)return -1;
    return 0;
}

static void fixture_destroy(void)
{
    asian_variable_carrier_destroy(fixture.scratch_carrier);
    asian_variable_carrier_destroy(fixture.carrier);
    free(fixture.output);free(fixture.scratch_full);free(fixture.full);
    free(fixture.scratch_strip);free(fixture.strip);memset(&fixture,0,sizeof(fixture));
}

static void condition(enum lifecycle lifecycle,enum cache_mode cache)
{
    if(cache==C_PRESSURE){pressure();return;}
    for(uint32_t block=0;block<fixture.blocks;++block){
        warm_bytes(fixture.engine->affine_plan[block],ASIAN_META_PLAN_HEADER_BYTES);
        warm_bytes(fixture.engine->affine_plan[block]->contexts,
                   fixture.n*sizeof(fixture.engine->affine_plan[block]->contexts[0]));
    }
    if(lifecycle==L_FRESH)
        warm_bytes(asian_variable_signed_z_bank,
            (size_t)asian_variable_required_donor_regions(fixture.blocks)*
            ASIAN_VARIABLE_PATHS_PER_BLOCK*sizeof(float));
    else
        warm_bytes(fixture.carrier,asian_variable_carrier_bytes(
            fixture.blocks,capability(fixture.workload)));
    warm_request(lifecycle==L_PREPARED?prepared_request():scratch_request());
}

static timing_t observe(enum lifecycle lifecycle,enum cache_mode cache,
                        uint32_t variant)
{
    const int reuse=lifecycle==L_REUSE;
    const asian_affine_family_request_input_t input=request_input(
        fixture.n,variant,fixture.workload,reuse);
    const asian_affine_family_carrier_input_t market=carrier_input(
        fixture.n,variant);
    asian_variable_carrier_t *carrier=lifecycle==L_FRESH?
        fixture.scratch_carrier:fixture.carrier;
    void *request=lifecycle==L_PREPARED?prepared_request():scratch_request();
    condition(lifecycle,cache);
    int status=0;
    const uint64_t wall0=wall_now(),tsc0=tsc_begin();
    if(lifecycle==L_FRESH)
        status=asian_variable_carrier_prepare(fixture.engine,fixture.blocks,
            &market,carrier);
    if(status==0&&lifecycle!=L_PREPARED)
        status=prepare_request(carrier,&input,request);
    if(status==0)
        status=asian_variable_sobol_price(request,fixture.blocks,fixture.output);
    const uint64_t tsc1=tsc_end(),wall1=wall_now();
    if(status!=0)abort();
    const unsigned char *bytes=(const unsigned char *)fixture.output;
    sink^=bytes[(variant*17u)%sizeof(*fixture.output)];
    return (timing_t){(double)(wall1-wall0),(double)(tsc1-tsc0)};
}

static timing_t measure(enum lifecycle lifecycle,enum cache_mode cache)
{
    double walls[SAMPLES],ticks[SAMPLES];
    for(uint32_t sample=0;sample<WARMUPS+SAMPLES;++sample){
        const timing_t a=observe(lifecycle,cache,sample&1u);
        const timing_t b=observe(lifecycle,cache,(sample+1u)&1u);
        if(sample>=WARMUPS){walls[sample-WARMUPS]=0.5*(a.wall+b.wall);
                           ticks[sample-WARMUPS]=0.5*(a.tsc+b.tsc);}
    }
    return (timing_t){median(walls),median(ticks)};
}

int main(int argc,char **argv)
{
    int cpu=-1;
    if(argc==3&&strcmp(argv[1],"--cpu")==0)cpu=atoi(argv[2]);
    if(cpu!=0||!is_spr()||pin_cpu(cpu)!=0){
        fputs("Sapphire Rapids CPU 0 is required\n",stderr);return 2;
    }
    asian_variable_engine_t *engine=a64(sizeof(*engine));
    if(!engine||asian_variable_engine_create(engine,0)!=0)return 1;
    printf("memory signed_z_bank=491520 expanded_plans=1840128 "
           "block_metadata=16384 descriptor=8192 W_provenance=128 "
           "max_x_growth_carrier=983104 approximate_private_footprint=3339456\n");
    puts("N block_count paths workload lifecycle cache wall_ns tsc_ticks "
         "valuations_per_second path_fixing_updates_per_second identity");
    static const uint32_t ns[]={64,256},blocks[]={1,2,4,8,16};
    timing_t results[2][5][WORKLOADS][LIFECYCLES][CACHES];
    memset(results,0,sizeof(results));
    for(uint32_t ni=0;ni<2u;++ni)for(uint32_t bi=0;bi<5u;++bi)
      for(uint32_t w=0;w<WORKLOADS;++w){
        if(fixture_create(engine,ns[ni],blocks[bi],(enum workload)w)!=0)return 1;
        for(uint32_t life=0;life<LIFECYCLES;++life)for(uint32_t cache=0;cache<CACHES;++cache){
            const timing_t t=measure((enum lifecycle)life,(enum cache_mode)cache);
            results[ni][bi][w][life][cache]=t;
            const double vps=1e9/t.wall;
            const double updates=vps*4096.0*(double)blocks[bi]*(double)ns[ni];
            printf("%u %u %u %s %s %s %.0f %.0f %.3f %.3f PASS\n",
                ns[ni],blocks[bi],4096u*blocks[bi],workload_names[w],
                lifecycle_names[life],cache_names[cache],t.wall,t.tsc,vps,updates);
        }
        fixture_destroy();
      }
    puts("scaling N workload lifecycle cache block_count wall_vs_block1 tsc_vs_block1");
    for(uint32_t ni=0;ni<2u;++ni)for(uint32_t w=0;w<WORKLOADS;++w)
      for(uint32_t life=0;life<LIFECYCLES;++life)for(uint32_t cache=0;cache<CACHES;++cache)
        for(uint32_t bi=0;bi<5u;++bi)
          printf("scaling %u %s %s %s %u %.6f %.6f\n",ns[ni],workload_names[w],
            lifecycle_names[life],cache_names[cache],blocks[bi],
            results[ni][bi][w][life][cache].wall/results[ni][0][w][life][cache].wall,
            results[ni][bi][w][life][cache].tsc/results[ni][0][w][life][cache].tsc);
    puts("N64_GEOCV_ERROR_LATENCY paths wall_ns call_abs_error put_abs_error");
    const double call_error[]={0.0011165799170977,0.0007287324935442,
        0.0000638352304941,0.0002133013278499,0.0001289233967427};
    const double put_error[]={0.0011448474823911,0.0007570000588376,
        0.0000921027957875,0.0002415688931433,0.0001006558314471};
    for(uint32_t bi=0;bi<5u;++bi)
        printf("N64_GEOCV_ERROR_LATENCY %u %.0f %.10g %.10g\n",4096u*blocks[bi],
            results[0][bi][W_GEOCV][L_PREPARED][C_WARM].wall,
            call_error[bi],put_error[bi]);
    asian_variable_engine_destroy(engine);free(engine);
    return sink==UINT64_MAX?1:0;
}
