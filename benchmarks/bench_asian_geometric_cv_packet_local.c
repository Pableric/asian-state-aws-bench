#define _GNU_SOURCE
#pragma GCC diagnostic ignored "-Wunused-function"
#define main asian_geometric_cv_packet_local_embedded_correctness_main
#include "../tests/test_asian_geometric_cv_packet_local.c"
#undef main

#include <cpuid.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

enum { WARMUP_QUARTETS=16, MEASURED_QUARTETS=51 };
enum candidate { MATERIALIZED_SQL=0, PACKET_LOCAL_QG=1 };
enum workload { PRICE=0, PRICE_DELTA=1 };
enum cache_mode { CANDIDATE_WARM=0, HISTORICAL_32K_RMW=1 };
typedef struct { uint64_t wall,tsc; } duration_t;

static fixture_t b;
static asian_genuine_fixed_block_source_context_t source __attribute__((aligned(64)));
static asian_genuine_strip_context_t *strip;
static asian_genuine_strip_output_t *output;
static uint32_t *pressure;
static uint32_t active_n,active_k;
static enum workload active_workload;
static volatile uint64_t sink;

static uint64_t wall_now(void)
{
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC_RAW,&t)!=0)abort();
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}

static uint64_t tsc_begin(void){_mm_lfence();return __rdtsc();}
static uint64_t tsc_end(void)
{
    unsigned auxiliary;const uint64_t t=__rdtscp(&auxiliary);_mm_lfence();return t;
}

static int compare_double(const void *a,const void *bvalue)
{
    const double x=*(const double *)a,y=*(const double *)bvalue;
    return(x>y)-(x<y);
}

static double median(double *values,uint32_t count)
{
    qsort(values,count,sizeof(*values),compare_double);
    return count&1u?values[count/2u]:0.5*(values[count/2u-1u]+values[count/2u]);
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

static void prepare_source(uint32_t n)
{
    const asian_genuine_fixed_block_source_request_t request=source_request(&markets[2],n);
    if(asian_genuine_fixed_block_source_prepare(&source,&request)!=
       ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK)abort();
}

static void frontend(void)
{
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(&source,b.x);
    asian_vector_exp_range_reduced_array_diag(b.x,b.growth);
    asian_vector_exp_range_reduced_array_diag(b.x+PATHS,b.growth+PATHS);
}

static void consume(const float *q,const float *g)
{
    const int status=active_workload==PRICE?
      asian_genuine_strip_price_diag(q,g,strip,ASIAN_GENUINE_STRIP_GEOMETRIC_CV,
        4u,output):
      asian_genuine_strip_price_delta_diag(q,g,strip,
        ASIAN_GENUINE_STRIP_GEOMETRIC_CV,4u,output);
    if(status!=0)abort();
}

__attribute__((noinline)) static void run_pipeline(enum candidate candidate)
{
    frontend();
    if(candidate==MATERIALIZED_SQL){
        asian_genuine_sql_dual_control_diag(b.routes,active_n,b.baseline);
        asian_genuine_strip_l_to_g_diag(b.baseline->l,strip,b.baseline_g);
        consume(b.baseline->q,b.baseline_g);
    }else{
        asian_geometric_cv_packet_local_qg_diag(b.context);
        consume(b.q,b.g);
    }
}

static uint64_t output_hash(void)
{
    const size_t bytes=active_workload==PRICE?16u:32u;
    uint64_t h=UINT64_C(1469598103934665603);
    for(uint32_t k=0;k<active_k;++k)
        h=hash_bytes(h,&output->values[k],bytes);
    return h;
}

static void historical_pressure(void)
{
    for(uint32_t i=0;i<8192u;i+=16u)pressure[i]=pressure[i]*1664525u+1013904223u;
    sink+=pressure[(sink>>3u)&8191u];
}

static void condition(enum candidate candidate,enum cache_mode cache)
{
    if(candidate==MATERIALIZED_SQL)initial_state(b.baseline);
    run_pipeline(candidate);
    sink+=output_hash();
    if(candidate==MATERIALIZED_SQL)initial_state(b.baseline);
    memset(output,0,sizeof(*output));
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
    active_n=n;prepare_source(n);
    if(prepare_shared_routes(&b,n,n)!=0)return-1;
    frontend();
    float fixed[32];
    if(asian_genuine_strip_fixed_strikes(32u,fixed)!=0||
       asian_genuine_strip_prepare(strip,100.0,.03,0.0,.20,1.0,n,0,0,0,
         fixed,32u)!=0)return-1;
    return asian_geometric_cv_packet_local_prepare(b.context,b.routes,n,100.0f,
      b.x,32768u,b.growth,32768u,strip,b.q,16384u,b.g,16384u);
}

static int prepare_cell(uint32_t k,enum workload workload,uint64_t expected[2])
{
    float fixed[32];active_k=k;active_workload=workload;
    if(asian_genuine_strip_fixed_strikes(k,fixed)!=0||
       asian_genuine_strip_prepare(strip,100.0,.03,0.0,.20,1.0,active_n,0,0,0,
         fixed,k)!=0||
       asian_geometric_cv_packet_local_prepare(b.context,b.routes,active_n,100.0f,
         b.x,32768u,b.growth,32768u,strip,b.q,16384u,b.g,16384u)!=0)return-1;
    for(uint32_t c=0;c<2u;++c){
        if(c==MATERIALIZED_SQL)initial_state(b.baseline);
        memset(output,0,sizeof(*output));run_pipeline((enum candidate)c);
        expected[c]=output_hash();
    }
    return expected[0]==expected[1]?0:-1;
}

static void quartet(uint32_t index,enum cache_mode cache,const uint64_t expected[2],
                    double wall[2],double tsc[2])
{
    const enum candidate abba[4]={MATERIALIZED_SQL,PACKET_LOCAL_QG,
      PACKET_LOCAL_QG,MATERIALIZED_SQL};
    const enum candidate baab[4]={PACKET_LOCAL_QG,MATERIALIZED_SQL,
      MATERIALIZED_SQL,PACKET_LOCAL_QG};
    const enum candidate *order=index&1u?baab:abba;
    double wall_sum[2]={0.0,0.0},tsc_sum[2]={0.0,0.0};
    for(uint32_t observation=0;observation<4u;++observation){
        const enum candidate c=order[observation];
        const duration_t d=observe(c,cache,expected[c]);
        wall_sum[c]+=(double)d.wall;tsc_sum[c]+=(double)d.tsc;
    }
    /* Established protocol: mean the two observations inside the quartet. */
    wall[0]=0.5*wall_sum[0];wall[1]=0.5*wall_sum[1];
    tsc[0]=0.5*tsc_sum[0];tsc[1]=0.5*tsc_sum[1];
}

static int time_cell(enum cache_mode cache,const uint64_t expected[2],
                     double *wall_speedup,double *tsc_speedup)
{
    double dummy_wall[2],dummy_tsc[2];
    for(uint32_t q=0;q<WARMUP_QUARTETS;++q)
        quartet(q,cache,expected,dummy_wall,dummy_tsc);
    double base_wall[MEASURED_QUARTETS],candidate_wall[MEASURED_QUARTETS];
    double base_tsc[MEASURED_QUARTETS],candidate_tsc[MEASURED_QUARTETS];
    for(uint32_t q=0;q<MEASURED_QUARTETS;++q){
        double wall[2],tsc[2];quartet(q,cache,expected,wall,tsc);
        base_wall[q]=wall[0];candidate_wall[q]=wall[1];
        base_tsc[q]=tsc[0];candidate_tsc[q]=tsc[1];
    }
    *wall_speedup=median(base_wall,MEASURED_QUARTETS)/
      median(candidate_wall,MEASURED_QUARTETS);
    *tsc_speedup=median(base_tsc,MEASURED_QUARTETS)/
      median(candidate_tsc,MEASURED_QUARTETS);
    return 0;
}

static int benchmark(void)
{
    static const uint32_t ns[]={16u,32u,64u,128u,256u};
    static const uint32_t ks[]={1u,4u,8u,16u,32u};
    puts("N  worst_wall  worst_tsc  median_wall  median_tsc  decision");
    for(uint32_t ni=0;ni<5u;++ni){
        if(prepare_n(ns[ni])!=0)return-1;
        double walls[20],tscs[20];uint32_t cell=0;int exact=1;
        for(uint32_t ki=0;ki<5u;++ki)for(uint32_t workload=0;workload<2u;++workload){
            uint64_t expected[2];
            if(prepare_cell(ks[ki],(enum workload)workload,expected)!=0){exact=0;return-1;}
            for(uint32_t cache=0;cache<2u;++cache){
                if(time_cell((enum cache_mode)cache,expected,&walls[cell],&tscs[cell])!=0)
                    return-1;
                if(walls[cell]<0.99||tscs[cell]<0.99)
                    fprintf(stderr,"regression N=%u K=%u workload=%s cache=%s wall=%.6f tsc=%.6f\n",
                      ns[ni],ks[ki],workload?"price_delta":"price",
                      cache?"historical_32KiB_rmw":"candidate_warm",
                      walls[cell],tscs[cell]);
                ++cell;
            }
        }
        double worst_wall=walls[0],worst_tsc=tscs[0];
        for(uint32_t i=1;i<20u;++i){if(walls[i]<worst_wall)worst_wall=walls[i];
            if(tscs[i]<worst_tsc)worst_tsc=tscs[i];}
        const double median_wall=median(walls,20u),median_tsc=median(tscs,20u);
        const int select=exact&&worst_wall>=0.99&&worst_tsc>=0.99&&
          median_wall>1.01&&median_tsc>1.01;
        printf("%u  %.6f  %.6f  %.6f  %.6f  %s\n",ns[ni],worst_wall,worst_tsc,
          median_wall,median_tsc,select?"PACKET_LOCAL_QG":"MATERIALIZED_SQL");
    }
    return 0;
}

int main(int argc,char **argv)
{
    int cpu=-1,check_only=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--check-only"))check_only=1;
        else if(!strcmp(argv[i],"--cpu")&&i+1<argc)cpu=atoi(argv[++i]);
        else{fprintf(stderr,"usage: %s --cpu CPU [--check-only]\n",argv[0]);return 2;}
    }
    if(cpu<0||pin_cpu(cpu)!=0){fprintf(stderr,"CPU pinning failed\n");return 2;}
    if(!sapphire_rapids()){fprintf(stderr,"family-6/model-143 Sapphire Rapids required\n");return 2;}
    if(check_only)return asian_geometric_cv_packet_local_embedded_correctness_main(1,argv);
    if(fixture_init(&b)!=0)return 2;
    strip=a64(sizeof(*strip));output=a64(sizeof(*output));pressure=a64(32768u);
    if(!strip||!output||!pressure)return 2;
    for(uint32_t i=0;i<8192u;++i)pressure[i]=i*UINT32_C(2654435761);
    const int status=benchmark();
    free(pressure);free(output);free(strip);fixture_release(&b);
    if(sink==UINT64_MAX)fprintf(stderr,"sink=%llu\n",(unsigned long long)sink);
    return status==0?0:1;
}
