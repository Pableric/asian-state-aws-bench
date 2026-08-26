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
typedef struct { timing_t parent, block; } b1_pair_t;
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

enum b1_lifecycle {
    B1_MARKET_PREPARE,
    B1_REQUEST_PREPARE,
    B1_PREPARED_PRICE,
    B1_REUSED_TOTAL,
    B1_FRESH_TOTAL,
    B1_LIFECYCLE_COUNT,
};

typedef struct {
    asian_affine_family_engine_t *engine;
    asian_affine_family_growth_carrier_t *growth;
    asian_affine_family_growth_carrier_t *scratch_growth;
    asian_affine_family_xgrowth_carrier_t *xgrowth;
    asian_affine_family_xgrowth_carrier_t *scratch_xgrowth;
    asian_affine_family_arithmetic_request_t *arithmetic;
    asian_affine_family_arithmetic_request_t *scratch_arithmetic;
    asian_affine_family_geocv_request_t *geocv;
    asian_affine_family_geocv_request_t *scratch_geocv;
    asian_genuine_strip_output_t *output;
    enum workload workload;
} b1_parent_fixture_t;

static b1_parent_fixture_t b1_parent;

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

static int b1_parent_create(enum workload workload)
{
    memset(&b1_parent,0,sizeof(b1_parent));
    b1_parent.workload=workload;
    b1_parent.engine=a64(sizeof(*b1_parent.engine));
    b1_parent.output=a64(sizeof(*b1_parent.output));
    if(!b1_parent.engine||!b1_parent.output||
       asian_affine_family_engine_create(b1_parent.engine)!=0)return -1;
    const asian_affine_family_carrier_input_t market=carrier_input(64u,0u);
    const asian_affine_family_request_input_t input=request_input(
        64u,0u,workload,0);
    if(workload==W_PRICE){
        b1_parent.growth=a64(sizeof(*b1_parent.growth));
        b1_parent.scratch_growth=a64(sizeof(*b1_parent.scratch_growth));
        b1_parent.arithmetic=a64(sizeof(*b1_parent.arithmetic));
        b1_parent.scratch_arithmetic=a64(sizeof(*b1_parent.scratch_arithmetic));
        if(!b1_parent.growth||!b1_parent.scratch_growth||
           !b1_parent.arithmetic||!b1_parent.scratch_arithmetic||
           asian_affine_family_growth_carrier_prepare(b1_parent.engine,&market,
               b1_parent.growth)!=0||
           asian_affine_family_arithmetic_request_prepare_growth(
               b1_parent.engine,NULL,b1_parent.growth,&input,
               ASIAN_AFFINE_FAMILY_AFFINE,b1_parent.arithmetic)!=0)return -1;
    }else{
        b1_parent.xgrowth=a64(sizeof(*b1_parent.xgrowth));
        b1_parent.scratch_xgrowth=a64(sizeof(*b1_parent.scratch_xgrowth));
        b1_parent.geocv=a64(sizeof(*b1_parent.geocv));
        b1_parent.scratch_geocv=a64(sizeof(*b1_parent.scratch_geocv));
        if(!b1_parent.xgrowth||!b1_parent.scratch_xgrowth||!b1_parent.geocv||
           !b1_parent.scratch_geocv||
           asian_affine_family_xgrowth_carrier_prepare(b1_parent.engine,&market,
               b1_parent.xgrowth)!=0||
           asian_affine_family_geocv_request_prepare(b1_parent.engine,NULL,
               b1_parent.xgrowth,&input,ASIAN_AFFINE_FAMILY_AFFINE,
               b1_parent.geocv)!=0)return -1;
    }
    return 0;
}

static void b1_parent_destroy(void)
{
    if(b1_parent.engine)asian_affine_family_engine_destroy(b1_parent.engine);
    free(b1_parent.output);free(b1_parent.scratch_geocv);free(b1_parent.geocv);
    free(b1_parent.scratch_arithmetic);free(b1_parent.arithmetic);
    free(b1_parent.scratch_xgrowth);free(b1_parent.xgrowth);
    free(b1_parent.scratch_growth);free(b1_parent.growth);
    free(b1_parent.engine);memset(&b1_parent,0,sizeof(b1_parent));
}

static int b1_parent_prepare_request(uint32_t variant,int reuse)
{
    const asian_affine_family_request_input_t input=request_input(
        64u,variant,b1_parent.workload,reuse);
    if(b1_parent.workload==W_PRICE)
        return asian_affine_family_arithmetic_request_prepare_growth(
            b1_parent.engine,NULL,b1_parent.growth,&input,
            ASIAN_AFFINE_FAMILY_AFFINE,b1_parent.scratch_arithmetic);
    return asian_affine_family_geocv_request_prepare(b1_parent.engine,NULL,
        b1_parent.xgrowth,&input,ASIAN_AFFINE_FAMILY_AFFINE,
        b1_parent.scratch_geocv);
}

static int b1_parent_price(int scratch)
{
    if(b1_parent.workload==W_PRICE)
        return asian_affine_family_arithmetic_prepared_price(
            scratch?b1_parent.scratch_arithmetic:b1_parent.arithmetic,
            b1_parent.output);
    return asian_affine_family_geocv_prepared_price(
        scratch?b1_parent.scratch_geocv:b1_parent.geocv,b1_parent.output);
}

static int b1_parent_invoke(enum b1_lifecycle lifecycle,uint32_t variant)
{
    const asian_affine_family_carrier_input_t market=carrier_input(64u,variant);
    const asian_affine_family_request_input_t fresh=request_input(
        64u,variant,b1_parent.workload,0);
    if(lifecycle==B1_MARKET_PREPARE){
        return b1_parent.workload==W_PRICE?
            asian_affine_family_growth_carrier_prepare(b1_parent.engine,&market,
                b1_parent.scratch_growth):
            asian_affine_family_xgrowth_carrier_prepare(b1_parent.engine,&market,
                b1_parent.scratch_xgrowth);
    }
    if(lifecycle==B1_REQUEST_PREPARE)return b1_parent_prepare_request(variant,1);
    if(lifecycle==B1_PREPARED_PRICE)return b1_parent_price(0);
    if(lifecycle==B1_REUSED_TOTAL){
        const int status=b1_parent_prepare_request(variant,1);
        return status==0?b1_parent_price(1):status;
    }
    int status;
    if(b1_parent.workload==W_PRICE){
        status=asian_affine_family_growth_carrier_prepare(b1_parent.engine,
            &market,b1_parent.scratch_growth);
        if(status==0)status=asian_affine_family_arithmetic_request_prepare_growth(
            b1_parent.engine,NULL,b1_parent.scratch_growth,&fresh,
            ASIAN_AFFINE_FAMILY_AFFINE,b1_parent.scratch_arithmetic);
    }else{
        status=asian_affine_family_xgrowth_carrier_prepare(b1_parent.engine,
            &market,b1_parent.scratch_xgrowth);
        if(status==0)status=asian_affine_family_geocv_request_prepare(
            b1_parent.engine,NULL,b1_parent.scratch_xgrowth,&fresh,
            ASIAN_AFFINE_FAMILY_AFFINE,b1_parent.scratch_geocv);
    }
    return status==0?b1_parent_price(1):status;
}

static int b1_block_invoke(enum b1_lifecycle lifecycle,uint32_t variant)
{
    const int reuse=lifecycle==B1_REQUEST_PREPARE||lifecycle==B1_REUSED_TOTAL;
    const asian_affine_family_request_input_t input=request_input(
        64u,variant,fixture.workload,reuse);
    const asian_affine_family_carrier_input_t market=carrier_input(64u,variant);
    if(lifecycle==B1_MARKET_PREPARE)
        return asian_variable_carrier_prepare(fixture.engine,1u,&market,
            fixture.scratch_carrier);
    if(lifecycle==B1_REQUEST_PREPARE)
        return prepare_request(fixture.carrier,&input,scratch_request());
    if(lifecycle==B1_PREPARED_PRICE)
        return asian_variable_sobol_price(prepared_request(),1u,fixture.output);
    if(lifecycle==B1_REUSED_TOTAL){
        int status=prepare_request(fixture.carrier,&input,scratch_request());
        return status==0?asian_variable_sobol_price(scratch_request(),1u,
            fixture.output):status;
    }
    int status=asian_variable_carrier_prepare(fixture.engine,1u,&market,
        fixture.scratch_carrier);
    if(status==0)status=prepare_request(fixture.scratch_carrier,&input,
        scratch_request());
    return status==0?asian_variable_sobol_price(scratch_request(),1u,
        fixture.output):status;
}

static int b1_counted_price(void *request,uint64_t *leaf_invocations)
{
    ++*leaf_invocations;
    return asian_variable_sobol_price(request,1u,fixture.output);
}

static int b1_block_invoke_audited(enum b1_lifecycle lifecycle,
                                   uint32_t variant,
                                   uint64_t *leaf_invocations)
{
    const int reuse=lifecycle==B1_REQUEST_PREPARE||lifecycle==B1_REUSED_TOTAL;
    const asian_affine_family_request_input_t input=request_input(
        64u,variant,fixture.workload,reuse);
    const asian_affine_family_carrier_input_t market=carrier_input(64u,variant);
    if(lifecycle==B1_MARKET_PREPARE)
        return asian_variable_carrier_prepare(fixture.engine,1u,&market,
            fixture.scratch_carrier);
    if(lifecycle==B1_REQUEST_PREPARE)
        return prepare_request(fixture.carrier,&input,scratch_request());
    if(lifecycle==B1_PREPARED_PRICE)
        return b1_counted_price(prepared_request(),leaf_invocations);
    if(lifecycle==B1_REUSED_TOTAL){
        int status=prepare_request(fixture.carrier,&input,scratch_request());
        return status==0?b1_counted_price(scratch_request(),leaf_invocations):
                         status;
    }
    int status=asian_variable_carrier_prepare(fixture.engine,1u,&market,
        fixture.scratch_carrier);
    if(status==0)status=prepare_request(fixture.scratch_carrier,&input,
        scratch_request());
    return status==0?b1_counted_price(scratch_request(),leaf_invocations):
                     status;
}

static size_t b1_output_write_footprint(void)
{
    asian_variable_output_t *first=a64(sizeof(*first));
    if(first==NULL)return 0u;
    memset(fixture.output,0xa5,sizeof(*fixture.output));
    if(asian_variable_sobol_price(prepared_request(),1u,fixture.output)!=0){
        free(first);return 0u;
    }
    *first=*fixture.output;
    memset(fixture.output,0x5a,sizeof(*fixture.output));
    if(asian_variable_sobol_price(prepared_request(),1u,fixture.output)!=0){
        free(first);return 0u;
    }
    const unsigned char *a=(const unsigned char *)(const void *)first;
    const unsigned char *b=(const unsigned char *)(const void *)fixture.output;
    size_t changed=0u;
    for(size_t i=0;i<sizeof(*first);++i)
        if(a[i]!=0xa5u||b[i]!=0x5au)++changed;
    free(first);return changed;
}

static void warm_parent_request(int scratch)
{
    if(b1_parent.workload==W_PRICE){
        const asian_affine_family_arithmetic_request_t *request=scratch?
            b1_parent.scratch_arithmetic:b1_parent.arithmetic;
        warm_bytes(request->routes.affine,64u*sizeof(request->routes.affine[0]));
        warm_bytes(&request->growth,sizeof(request->growth));
        warm_bytes(&request->strip,sizeof(request->strip));
    }else{
        const asian_affine_family_geocv_request_t *request=scratch?
            b1_parent.scratch_geocv:b1_parent.geocv;
        warm_bytes(request->routes.affine,64u*sizeof(request->routes.affine[0]));
        warm_bytes(&request->strip,sizeof(request->strip));
        warm_bytes(&request->immediate,sizeof(request->immediate));
        warm_bytes(&request->packet,sizeof(request->packet));
    }
}

static void b1_condition(int parent,enum b1_lifecycle lifecycle,
                         enum cache_mode cache)
{
    if(cache==C_PRESSURE){pressure();return;}
    if(lifecycle==B1_MARKET_PREPARE||lifecycle==B1_FRESH_TOTAL){
        warm_bytes(asian_variable_signed_z_bank,
            2u*ASIAN_VARIABLE_PATHS_PER_BLOCK*sizeof(float));
        if(parent)
            warm_bytes(&b1_parent.engine->signed_z_min,
                sizeof(b1_parent.engine->signed_z_min)+
                sizeof(b1_parent.engine->signed_z_max)+
                sizeof(b1_parent.engine->magic));
        else
            warm_bytes(&fixture.engine->signed_z_min,
                sizeof(fixture.engine->signed_z_min)+
                sizeof(fixture.engine->signed_z_max)+
                sizeof(fixture.engine->magic));
        return;
    }
    if(parent){
        warm_bytes(b1_parent.engine->affine_plan,ASIAN_META_PLAN_HEADER_BYTES);
        if(lifecycle==B1_REQUEST_PREPARE){
            if(b1_parent.workload==W_PRICE)
                warm_bytes(&b1_parent.growth->market,
                    sizeof(b1_parent.growth->market)+sizeof(uint32_t));
            else
                warm_bytes(&b1_parent.xgrowth->market,
                    sizeof(b1_parent.xgrowth->market)+sizeof(uint32_t));
        }else{
            warm_bytes(b1_parent.engine->affine_plan->contexts,
                64u*sizeof(b1_parent.engine->affine_plan->contexts[0]));
            warm_bytes(b1_parent.workload==W_PRICE?(const void *)b1_parent.growth:
                (const void *)b1_parent.xgrowth,b1_parent.workload==W_PRICE?
                sizeof(*b1_parent.growth):sizeof(*b1_parent.xgrowth));
            warm_parent_request(lifecycle!=B1_PREPARED_PRICE);
        }
    }else{
        warm_bytes(fixture.engine->affine_plan[0],ASIAN_META_PLAN_HEADER_BYTES);
        if(lifecycle==B1_REQUEST_PREPARE){
            warm_bytes(fixture.carrier,64u);
        }else{
            warm_bytes(fixture.engine->affine_plan[0]->contexts,
                64u*sizeof(fixture.engine->affine_plan[0]->contexts[0]));
            warm_bytes(fixture.carrier,asian_variable_carrier_bytes(1u,
                capability(fixture.workload)));
            warm_request(lifecycle==B1_PREPARED_PRICE?prepared_request():
                scratch_request());
        }
    }
}

static timing_t b1_observe(int parent,enum b1_lifecycle lifecycle,
                           enum cache_mode cache,uint32_t variant)
{
    b1_condition(parent,lifecycle,cache);
    const uint64_t wall0=wall_now(),tsc0=tsc_begin();
    const int status=parent?b1_parent_invoke(lifecycle,variant):
                            b1_block_invoke(lifecycle,variant);
    const uint64_t tsc1=tsc_end(),wall1=wall_now();
    if(status!=0)abort();
    if(parent){const unsigned char *p=(const unsigned char *)b1_parent.output;
               sink^=p[(variant*17u)%sizeof(*b1_parent.output)];}
    else{const unsigned char *p=(const unsigned char *)fixture.output;
         sink^=p[(variant*17u)%sizeof(*fixture.output)];}
    return (timing_t){(double)(wall1-wall0),(double)(tsc1-tsc0)};
}

static b1_pair_t b1_measure_pair(enum b1_lifecycle lifecycle,
                                 enum cache_mode cache)
{
    double pw[SAMPLES],pt[SAMPLES],bw[SAMPLES],bt[SAMPLES];
    for(uint32_t quartet=0;quartet<WARMUPS+SAMPLES;++quartet){
        timing_t ps={0},bs={0};const int parent_first=(quartet&1u)==0u;
        for(uint32_t observation=0;observation<4u;++observation){
            const int parent=(observation==0u||observation==3u)?parent_first:
                                                                    !parent_first;
            const timing_t value=b1_observe(parent,lifecycle,cache,
                (quartet+observation)&1u);
            if(parent){ps.wall+=value.wall;ps.tsc+=value.tsc;}
            else{bs.wall+=value.wall;bs.tsc+=value.tsc;}
        }
        if(quartet>=WARMUPS){const uint32_t s=quartet-WARMUPS;
            pw[s]=0.5*ps.wall;pt[s]=0.5*ps.tsc;
            bw[s]=0.5*bs.wall;bt[s]=0.5*bs.tsc;}
    }
    return (b1_pair_t){{median(pw),median(pt)},{median(bw),median(bt)}};
}

static int b1_identity(void)
{
    const size_t values=ASIAN_AFFINE_FAMILY_DONOR_VALUES*sizeof(float);
    if(fixture.workload==W_PRICE){
        if(memcmp(b1_parent.growth->growth,fixture.carrier->growth,values)!=0)
            return -1;
    }else if(memcmp(b1_parent.xgrowth->x,fixture.carrier->x,values)!=0||
              memcmp(b1_parent.xgrowth->growth,fixture.carrier->growth,
                     values)!=0)return -1;
    asian_genuine_strip_output_t expected __attribute__((aligned(64)));
    static const enum b1_lifecycle priced[]={
        B1_PREPARED_PRICE,B1_REUSED_TOTAL,B1_FRESH_TOTAL};
    for(uint32_t life=0;life<3u;++life){
        const uint32_t variants=priced[life]==B1_PREPARED_PRICE?1u:2u;
        for(uint32_t variant=0;variant<variants;++variant){
            if(b1_parent_invoke(priced[life],variant)!=0)return -1;
            expected=*b1_parent.output;
            if(b1_block_invoke(priced[life],variant)!=0||
               memcmp(&expected,&fixture.output->value.strip,
                      sizeof(expected))!=0)return -1;
        }
    }
    return 0;
}

static int benchmark_b1_parent_comparison(asian_variable_engine_t *engine)
{
    static const enum workload families[]={W_PRICE,W_GEOCV};
    static const char *family_names[]={"ARITHMETIC","GEOCV"};
    static const char *life_names[]={"market_prepare","request_prepare",
        "prepared_price","reused_total","fresh_total"};
    puts("B1_PARENT_COMPARISON family cache lifecycle parent_wall_ns "
         "block_wall_ns parent_tsc_ticks block_tsc_ticks block/parent_wall "
         "block/parent_tsc identity");
    for(uint32_t family=0;family<2u;++family){
        if(fixture_create(engine,64u,1u,families[family])!=0||
           b1_parent_create(families[family])!=0||b1_identity()!=0)return -1;
        const asian_affine_family_request_input_t input=request_input(
            64u,0u,families[family],0);
        asian_variable_b1_request_footprint_t footprint;
        if(asian_variable_b1_request_footprint(engine,fixture.carrier,&input,
                family==0u?ASIAN_VARIABLE_ARITHMETIC:ASIAN_VARIABLE_GEOCV,
                &footprint)!=0)return -1;
        printf("B1_REQUEST_FOOTPRINT family=%s N=64 "
               "logical_unique_read_bytes=%zu persistent_write_bytes=%zu "
               "selected_block_write_bytes=%zu route_prefix_write_bytes=%zu "
               "route_suffix_write_bytes=%zu unused_block_write_bytes=%zu "
               "request_capacity_bytes=%zu\n",family_names[family],
               footprint.logical_unique_bytes_read,
               footprint.persistent_bytes_written,
               footprint.selected_block_bytes_written,
               footprint.route_prefix_bytes_written,
               footprint.route_suffix_bytes_written,
               footprint.unused_block_bytes_written,
               footprint.request_capacity_bytes);
        const size_t output_bytes=b1_output_write_footprint();
        if(output_bytes==0u)return -1;
        printf("B1_OUTPUT_FOOTPRINT family=%s output_capacity_bytes=%zu "
               "output_bytes_written=%zu whole_capacity_clear=%s\n",
               family_names[family],sizeof(*fixture.output),output_bytes,
               output_bytes==sizeof(*fixture.output)?"YES":"NO");
        for(uint32_t life=0;life<B1_LIFECYCLE_COUNT;++life){
            uint64_t leaves=0u;
            if(b1_block_invoke_audited((enum b1_lifecycle)life,0u,&leaves)!=0||
               leaves!=(life<2u?0u:1u))return -1;
            printf("B1_LIFECYCLE_AUDIT family=%s stage=%s "
                   "pricing_leaf_invocations=%llu expected=%u PASS\n",
                   family_names[family],life_names[life],
                   (unsigned long long)leaves,life<2u?0u:1u);
            for(uint32_t cache=0;cache<CACHES;++cache){
                const b1_pair_t pair=b1_measure_pair((enum b1_lifecycle)life,
                    (enum cache_mode)cache);
                printf("B1_PARENT_COMPARISON %s %s %s %.0f %.0f %.0f %.0f "
                       "%.6f %.6f PASS\n",family_names[family],
                       cache_names[cache],life_names[life],pair.parent.wall,
                       pair.block.wall,pair.parent.tsc,pair.block.tsc,
                       pair.block.wall/pair.parent.wall,
                       pair.block.tsc/pair.parent.tsc);
            }
        }
        b1_parent_destroy();fixture_destroy();
    }
    return 0;
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
    if(benchmark_b1_parent_comparison(engine)!=0)return 1;
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
