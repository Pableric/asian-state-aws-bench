#define _GNU_SOURCE
#pragma GCC diagnostic ignored "-Wunused-function"
#define ASIAN_GEOMETRIC_CV_IMMEDIATE_EMBEDDED 1
#include "../tests/test_asian_geometric_cv_immediate.c"
#undef ASIAN_GEOMETRIC_CV_IMMEDIATE_EMBEDDED

#include <cpuid.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

enum { WARMUP_QUARTETS=16,MEASURED_QUARTETS=51 };
enum candidate { CURRENT=0,IMMEDIATE=1 };
enum workload { PRICE=0,PRICE_DELTA=1 };
enum cache_mode { CANDIDATE_WARM=0,HISTORICAL_32K_RMW=1 };
typedef struct {uint64_t wall,tsc;} duration_t;
typedef void (*pipeline_t)(void);

static fixture_t b;
static asian_genuine_fixed_block_source_context_t source __attribute__((aligned(64)));
static asian_genuine_strip_context_t *bench_strip;
static asian_geometric_cv_immediate_context_t *bench_immediate;
static asian_genuine_strip_output_t *bench_output;
static uint32_t *pressure;
static uint32_t active_n,active_k;
static enum workload active_workload;
static pipeline_t candidate_pipeline;
static volatile uint64_t sink;

static uint64_t wall_now(void)
{
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC_RAW,&t)!=0)abort();
    return(uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}

static uint64_t tsc_begin(void){_mm_lfence();return __rdtsc();}
static uint64_t tsc_end(void)
{
    unsigned aux;const uint64_t value=__rdtscp(&aux);_mm_lfence();return value;
}

static int compare_double(const void *a,const void *bvalue)
{
    const double x=*(const double *)a,y=*(const double *)bvalue;
    return(x>y)-(x<y);
}

static double median(double *values,uint32_t count)
{
    qsort(values,count,sizeof(*values),compare_double);
    return count&1u?values[count/2u]:.5*(values[count/2u-1u]+values[count/2u]);
}

static int pin_cpu(int cpu)
{
    cpu_set_t set;CPU_ZERO(&set);CPU_SET(cpu,&set);
    return sched_setaffinity(0,sizeof(set),&set);
}

static int sapphire_rapids(void)
{
    unsigned a,bx,c,d;char vendor[13];
    __cpuid(0,a,bx,c,d);memcpy(vendor,&bx,4u);memcpy(vendor+4,&d,4u);
    memcpy(vendor+8,&c,4u);vendor[12]=0;
    if(strcmp(vendor,"GenuineIntel")!=0)return 0;
    __cpuid(1,a,bx,c,d);
    const unsigned family=((a>>8u)&15u)+((a>>20u)&255u);
    const unsigned model=((a>>4u)&15u)|((a>>12u)&240u);
    return family==6u&&model==143u;
}

__attribute__((noinline,used)) void asian_geometric_cv_immediate_hot_frontend(void)
{
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(&source,b.x);
    asian_vector_exp_range_reduced_array_diag(b.x,b.growth);
    asian_vector_exp_range_reduced_array_diag(b.x+PATHS,b.growth+PATHS);
}

#define HOT_ROOT(mode,width) \
__attribute__((noinline,used)) \
void asian_geometric_cv_immediate_hot_##mode##_##width(void) \
{ \
    asian_geometric_cv_immediate_hot_frontend(); \
    asian_geometric_cv_immediate_invoke_##mode##_##width(bench_immediate, \
      bench_strip,bench_strip->strikes,bench_output); \
}

HOT_ROOT(price,1)
HOT_ROOT(price_delta,1)
HOT_ROOT(price,2)
HOT_ROOT(price_delta,2)
HOT_ROOT(price,4)
HOT_ROOT(price_delta,4)
#undef HOT_ROOT

static void consume_current(const float *q,const float *g)
{
    const int status=active_workload==PRICE?
      asian_genuine_strip_price_diag(q,g,bench_strip,
        ASIAN_GENUINE_STRIP_GEOMETRIC_CV,4u,bench_output):
      asian_genuine_strip_price_delta_diag(q,g,bench_strip,
        ASIAN_GENUINE_STRIP_GEOMETRIC_CV,4u,bench_output);
    if(status!=0)abort();
}

__attribute__((noinline)) static void run_current(void)
{
    asian_geometric_cv_immediate_hot_frontend();
    if(active_n<=64u){
        asian_geometric_cv_packet_local_qg_diag(b.context);
        consume_current(b.q,b.g);
    }else{
        asian_genuine_sql_dual_control_diag(b.routes,active_n,b.baseline);
        asian_genuine_strip_l_to_g_diag(b.baseline->l,bench_strip,b.baseline_g);
        consume_current(b.baseline->q,b.baseline_g);
    }
}

static uint64_t output_hash(void)
{
    const size_t bytes=active_workload==PRICE?16u:32u;
    uint64_t h=UINT64_C(1469598103934665603);
    for(uint32_t k=0;k<active_k;++k)
        h=hash_bytes(h,&bench_output->values[k],bytes);
    return h;
}

static void historical_pressure(void)
{
    for(uint32_t i=0;i<8192u;i+=16u)
        pressure[i]=pressure[i]*UINT32_C(1664525)+UINT32_C(1013904223);
    sink+=pressure[(sink>>3u)&8191u];
}

static void reset_current(void)
{
    if(active_n>64u)initial_state(b.baseline);
}

static void run_pipeline(enum candidate candidate)
{
    if(candidate==CURRENT)run_current();else candidate_pipeline();
}

static void condition(enum candidate candidate,enum cache_mode cache)
{
    if(candidate==CURRENT)reset_current();
    run_pipeline(candidate);sink+=output_hash();
    if(candidate==CURRENT)reset_current();
    memset(bench_output,0,sizeof(*bench_output));
    if(cache==HISTORICAL_32K_RMW)historical_pressure();
}

static duration_t observe(enum candidate candidate,enum cache_mode cache,
                          uint64_t expected)
{
    condition(candidate,cache);
    const uint64_t w0=wall_now(),t0=tsc_begin();
    run_pipeline(candidate);
    const uint64_t t1=tsc_end(),w1=wall_now();
    const uint64_t got=output_hash();sink+=got;
    if(got!=expected)abort();
    return(duration_t){w1-w0,t1-t0};
}

static int prepare_n(uint32_t n)
{
    active_n=n;
    const asian_genuine_fixed_block_source_request_t request=source_request(&markets[2],n);
    if(asian_genuine_fixed_block_source_prepare(&source,&request)!=
         ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK||prepare_shared_routes(&b,n,n)!=0)
        return-1;
    asian_geometric_cv_immediate_hot_frontend();
    return 0;
}

static pipeline_t select_candidate(uint32_t k,enum workload workload)
{
    const uint32_t width=k==1u?1u:k==2u?2u:4u;
    if(workload==PRICE){
        if(width==1u)return asian_geometric_cv_immediate_hot_price_1;
        if(width==2u)return asian_geometric_cv_immediate_hot_price_2;
        return asian_geometric_cv_immediate_hot_price_4;
    }
    if(width==1u)return asian_geometric_cv_immediate_hot_price_delta_1;
    if(width==2u)return asian_geometric_cv_immediate_hot_price_delta_2;
    return asian_geometric_cv_immediate_hot_price_delta_4;
}

static uint32_t shape_bit(uint32_t k,enum workload workload)
{
    const uint32_t width=k==1u?1u:k==2u?2u:4u;
    const uint32_t base=width==1u?0u:width==2u?2u:4u;
    return base+(workload==PRICE_DELTA);
}

static int prepare_cell(uint32_t k,enum workload workload,uint64_t expected[2])
{
    float fixed[32];uint32_t padded=0;
    active_k=k;active_workload=workload;candidate_pipeline=select_candidate(k,workload);
    if(asian_genuine_strip_fixed_strikes(32u,fixed)!=0||
       asian_geometric_cv_packet_local_strip_prepare_padded(bench_strip,100.0,
         .03,0.0,.20,1.0,active_n,0u,0.0,0.0,fixed,k,&padded)!=0||
       padded!=(k==1u?1u:4u)||
       asian_geometric_cv_packet_local_prepare(b.context,b.routes,active_n,
         100.0f,b.x,32768u,b.growth,32768u,bench_strip,
         b.q,16384u,b.g,16384u)!=0||
       asian_geometric_cv_immediate_prepare(bench_immediate,b.context,
         bench_strip)!=ASIAN_GEOMETRIC_CV_IMMEDIATE_OK)return-1;
    for(uint32_t candidate=0;candidate<2u;++candidate){
        if(candidate==CURRENT)reset_current();
        memset(bench_output,0,sizeof(*bench_output));
        run_pipeline((enum candidate)candidate);
        expected[candidate]=output_hash();
    }
    return expected[CURRENT]==expected[IMMEDIATE]?0:-1;
}

static void quartet(uint32_t index,enum cache_mode cache,
    const uint64_t expected[2],double wall[2],double tsc[2])
{
    const enum candidate abba[4]={CURRENT,IMMEDIATE,IMMEDIATE,CURRENT};
    const enum candidate baab[4]={IMMEDIATE,CURRENT,CURRENT,IMMEDIATE};
    const enum candidate *order=index&1u?baab:abba;
    double wall_sum[2]={0.0,0.0},tsc_sum[2]={0.0,0.0};
    for(uint32_t observation=0;observation<4u;++observation){
        const enum candidate candidate=order[observation];
        const duration_t d=observe(candidate,cache,expected[candidate]);
        wall_sum[candidate]+=(double)d.wall;tsc_sum[candidate]+=(double)d.tsc;
    }
    wall[CURRENT]=.5*wall_sum[CURRENT];wall[IMMEDIATE]=.5*wall_sum[IMMEDIATE];
    tsc[CURRENT]=.5*tsc_sum[CURRENT];tsc[IMMEDIATE]=.5*tsc_sum[IMMEDIATE];
}

static void time_cell(enum cache_mode cache,const uint64_t expected[2],
                      double *wall_speedup,double *tsc_speedup)
{
    double dummy_wall[2],dummy_tsc[2];
    for(uint32_t q=0;q<WARMUP_QUARTETS;++q)
        quartet(q,cache,expected,dummy_wall,dummy_tsc);
    double current_wall[MEASURED_QUARTETS],immediate_wall[MEASURED_QUARTETS];
    double current_tsc[MEASURED_QUARTETS],immediate_tsc[MEASURED_QUARTETS];
    for(uint32_t q=0;q<MEASURED_QUARTETS;++q){
        double wall[2],tsc[2];quartet(q,cache,expected,wall,tsc);
        current_wall[q]=wall[CURRENT];immediate_wall[q]=wall[IMMEDIATE];
        current_tsc[q]=tsc[CURRENT];immediate_tsc[q]=tsc[IMMEDIATE];
    }
    *wall_speedup=median(current_wall,MEASURED_QUARTETS)/
                  median(immediate_wall,MEASURED_QUARTETS);
    *tsc_speedup=median(current_tsc,MEASURED_QUARTETS)/
                 median(immediate_tsc,MEASURED_QUARTETS);
}

static int benchmark(uint32_t accepted_mask)
{
    static const uint32_t ns[]={16u,32u,64u,128u,256u};
    static const uint32_t ks[]={1u,2u,3u,4u};
    puts("N  K  workload  warm_wall  warm_tsc  pressure_wall  pressure_tsc  decision");
    for(uint32_t ni=0;ni<5u;++ni){
        if(prepare_n(ns[ni])!=0)return-1;
        for(uint32_t ki=0;ki<4u;++ki)for(uint32_t workload=0;workload<2u;++workload){
            const uint32_t bit=shape_bit(ks[ki],(enum workload)workload);
            if(!(accepted_mask&(1u<<bit))){
                printf("%u  %u  %s  NA  NA  NA  NA  REJECTED_STRUCTURE\n",
                  ns[ni],ks[ki],workload?"price_delta":"price");continue;
            }
            uint64_t expected[2];double warm_wall,warm_tsc,pressure_wall,pressure_tsc;
            if(prepare_cell(ks[ki],(enum workload)workload,expected)!=0){
                fprintf(stderr,"output identity N=%u K=%u workload=%s failed\n",
                  ns[ni],ks[ki],workload?"price_delta":"price");return-1;}
            time_cell(CANDIDATE_WARM,expected,&warm_wall,&warm_tsc);
            time_cell(HISTORICAL_32K_RMW,expected,&pressure_wall,&pressure_tsc);
            const int nonregress=warm_wall>=.99&&warm_tsc>=.99&&
              pressure_wall>=.99&&pressure_tsc>=.99;
            const int one_win=(warm_wall>1.01&&warm_tsc>1.01)||
                              (pressure_wall>1.01&&pressure_tsc>1.01);
            printf("%u  %u  %s  %.6f  %.6f  %.6f  %.6f  %s\n",ns[ni],ks[ki],
              workload?"price_delta":"price",warm_wall,warm_tsc,
              pressure_wall,pressure_tsc,nonregress&&one_win?"IMMEDIATE":"CURRENT");
        }
    }
    return 0;
}

int main(int argc,char **argv)
{
    int cpu=-1,check_only=0;uint32_t accepted_mask=0u;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--check-only"))check_only=1;
        else if(!strcmp(argv[i],"--cpu")&&i+1<argc)cpu=atoi(argv[++i]);
        else if(!strcmp(argv[i],"--accepted-mask")&&i+1<argc)
            accepted_mask=(uint32_t)strtoul(argv[++i],NULL,0);
        else{fprintf(stderr,"usage: %s --cpu CPU [--check-only|--accepted-mask MASK]\n",
               argv[0]);return 2;}
    }
    if(cpu<0||pin_cpu(cpu)!=0){fprintf(stderr,"CPU pinning failed\n");return 2;}
    if(!sapphire_rapids()){
        fprintf(stderr,"family-6/model-143 Sapphire Rapids required\n");return 2;}
    if(check_only)return asian_geometric_cv_immediate_embedded_correctness_main(1,argv);
    if((accepted_mask&~63u)!=0u){fprintf(stderr,"invalid audit mask\n");return 2;}
    if(fixture_init(&b)!=0)return 2;
    bench_strip=a64(sizeof(*bench_strip));bench_immediate=a64(sizeof(*bench_immediate));
    bench_output=a64(sizeof(*bench_output));pressure=a64(32768u);
    if(!bench_strip||!bench_immediate||!bench_output||!pressure)return 2;
    for(uint32_t i=0;i<8192u;++i)pressure[i]=i*UINT32_C(2654435761);
    const int status=benchmark(accepted_mask);
    free(pressure);free(bench_output);free(bench_immediate);free(bench_strip);
    fixture_release(&b);
    if(sink==UINT64_MAX)fprintf(stderr,"sink=%llu\n",(unsigned long long)sink);
    return status==0?0:1;
}
