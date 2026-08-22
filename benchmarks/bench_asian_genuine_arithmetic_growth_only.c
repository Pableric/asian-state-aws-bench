#define _GNU_SOURCE
#pragma GCC diagnostic ignored "-Wunused-function"
#define main asian_growth_only_embedded_correctness_main
#include "../tests/test_asian_genuine_arithmetic_growth_only.c"
#undef main

#include <cpuid.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

enum {
    WARMUP_QUARTETS = 16,
    MEASURED_QUARTETS = 51,
    CONFIRM_QUARTETS = 201,
    CONFIRM_BATCHES = 5,
    QUARTET_OBSERVATIONS = 4,
};

enum candidate { CANDIDATE_CURRENT = 0, CANDIDATE_FUSED = 1 };
enum workload { WORKLOAD_PRICE = 0, WORKLOAD_PRICE_DELTA = 1 };
enum cache_mode { CACHE_CANDIDATE_WARM = 0, CACHE_HISTORICAL_RMW = 1 };
enum timing_mode {
    TIMING_FULL = 0,
    TIMING_CROSSOVER = 1,
    TIMING_CONFIRM = 2,
};

typedef struct { uint64_t tsc, wall; } duration_t;

static fixture_t bench_fixture;
static asian_genuine_fixed_block_source_context_t bench_source
  __attribute__((aligned(64)));
static asian_genuine_arithmetic_growth_only_context_t bench_immediate_context[2]
  __attribute__((aligned(64)));
static asian_genuine_strip_context_t *bench_strip;
static asian_genuine_strip_output_t *bench_output;
static uint32_t *bench_pressure;
static uint32_t bench_n;
static uint32_t bench_k;
static enum workload bench_workload;
static asian_genuine_arithmetic_growth_only_immediate_leaf_t bench_immediate_leaf;
static asian_genuine_arithmetic_growth_only_context_t *bench_active_context;
static asian_genuine_arithmetic_growth_only_context_t *bench_active_immediate;
static float *bench_active_q;
static volatile uint64_t bench_sink;

void asian_genuine_arithmetic_downstream_layout_anchor(void);

static uint64_t wall_now(void)
{
    struct timespec value;
    if(clock_gettime(CLOCK_MONOTONIC_RAW,&value)!=0)abort();
    return (uint64_t)value.tv_sec*UINT64_C(1000000000)+(uint64_t)value.tv_nsec;
}

static uint64_t tsc_begin(void)
{
    _mm_lfence();
    return __rdtsc();
}

static uint64_t tsc_end(void)
{
    unsigned auxiliary;
    const uint64_t value=__rdtscp(&auxiliary);
    _mm_lfence();
    return value;
}

static int compare_double(const void *left,const void *right)
{
    const double a=*(const double *)left,b=*(const double *)right;
    return(a>b)-(a<b);
}

static double median51(double values[MEASURED_QUARTETS])
{
    qsort(values,MEASURED_QUARTETS,sizeof(values[0]),compare_double);
    return values[MEASURED_QUARTETS/2u];
}

static double median201(double values[CONFIRM_QUARTETS])
{
    qsort(values,CONFIRM_QUARTETS,sizeof(values[0]),compare_double);
    return values[CONFIRM_QUARTETS/2u];
}

static double median5(double values[CONFIRM_BATCHES])
{
    qsort(values,CONFIRM_BATCHES,sizeof(values[0]),compare_double);
    return values[CONFIRM_BATCHES/2u];
}

static double equal_cell_median10(double values[10])
{
    qsort(values,10u,sizeof(values[0]),compare_double);
    return 0.5*(values[4]+values[5]);
}

static double equal_cell_median12(double values[12])
{
    qsort(values,12u,sizeof(values[0]),compare_double);
    return 0.5*(values[5]+values[6]);
}

static int pin_cpu(int cpu)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu,&set);
    return sched_setaffinity(0,sizeof(set),&set);
}

static int is_sapphire_rapids(void)
{
    unsigned a,b,c,d;
    char vendor[13];
    __cpuid(0,a,b,c,d);
    memcpy(vendor,&b,4u);memcpy(vendor+4,&d,4u);memcpy(vendor+8,&c,4u);
    vendor[12]=0;
    if(strcmp(vendor,"GenuineIntel")!=0)return 0;
    __cpuid(1,a,b,c,d);
    const unsigned family=((a>>8)&15u)+((a>>20)&255u);
    const unsigned model=((a>>4)&15u)|((a>>12)&240u);
    return family==6u&&model==0x8fu;
}

static uint64_t touch(const void *memory,size_t bytes)
{
    const unsigned char *data=memory;
    uint64_t value=0;
    for(size_t offset=0;offset<bytes;offset+=64u)value+=data[offset];
    return value;
}

static int uses_immediate(uint32_t k,enum workload workload)
{
    if(k==1u)return workload==WORKLOAD_PRICE_DELTA;
    return k==2u||k==4u;
}

static asian_genuine_arithmetic_growth_only_immediate_leaf_t
select_immediate(uint32_t k,enum workload workload)
{
    if(k==1u)return workload==WORKLOAD_PRICE?
      asian_genuine_arithmetic_growth_only_price_1_diag:
      asian_genuine_arithmetic_growth_only_price_delta_1_diag;
    if(k==2u)return workload==WORKLOAD_PRICE?
      asian_genuine_arithmetic_growth_only_price_2_diag:
      asian_genuine_arithmetic_growth_only_price_delta_2_diag;
    return workload==WORKLOAD_PRICE?
      asian_genuine_arithmetic_growth_only_price_4_diag:
      asian_genuine_arithmetic_growth_only_price_delta_4_diag;
}

static float *candidate_growth(enum candidate candidate)
{
    return candidate==CANDIDATE_CURRENT?
      bench_fixture.growth:bench_fixture.growth_fused;
}

static float *candidate_q(enum candidate candidate)
{
    return candidate==CANDIDATE_CURRENT?
      bench_fixture.q:bench_fixture.q_fused;
}

static asian_genuine_arithmetic_growth_only_context_t *
candidate_context(enum candidate candidate)
{
    return candidate==CANDIDATE_CURRENT?
      bench_fixture.context:bench_fixture.fused_context;
}

__attribute__((noinline)) static void run_current_frontend(void)
{
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(
      &bench_source,bench_fixture.x);
    asian_vector_exp_range_reduced_array_diag(
      bench_fixture.x,bench_fixture.growth);
    asian_vector_exp_range_reduced_array_diag(
      bench_fixture.x+PATHS,bench_fixture.growth+PATHS);
}

__attribute__((noinline)) static void run_fused_frontend(void)
{
    asian_genuine_arithmetic_fused_source_exp_diag(
      bench_fixture.fused_source_context);
}

static void activate_candidate(enum candidate candidate)
{
    bench_active_context=candidate_context(candidate);
    bench_active_immediate=&bench_immediate_context[candidate];
    bench_active_q=candidate_q(candidate);
}

__attribute__((noinline)) static void run_stage1_consumer(void)
{
    if(bench_k==1u){
        asian_genuine_strip_arithmetic_price_1_diag(
          bench_active_q,bench_fixture.g_unused,bench_strip,bench_strip->strikes,
          bench_output->values);
    }else if(bench_workload==WORKLOAD_PRICE){
        (void)asian_genuine_strip_price_diag(bench_active_q,bench_fixture.g_unused,
          bench_strip,ASIAN_GENUINE_STRIP_ARITHMETIC,4u,bench_output);
    }else{
        (void)asian_genuine_strip_price_delta_diag(bench_active_q,
          bench_fixture.g_unused,
          bench_strip,ASIAN_GENUINE_STRIP_ARITHMETIC,4u,bench_output);
    }
}

__attribute__((noinline)) static void run_downstream(void)
{
    if(uses_immediate(bench_k,bench_workload)){
        bench_immediate_leaf(bench_active_immediate,bench_strip,
          bench_strip->strikes,bench_output->values);
    }else{
        asian_genuine_arithmetic_growth_only_q_diag(bench_active_context);
        run_stage1_consumer();
    }
}

__attribute__((noinline)) static void run_complete_current(void)
{
    run_current_frontend();
    run_downstream();
}

__attribute__((noinline)) static void run_complete_fused(void)
{
    run_fused_frontend();
    run_downstream();
}

static uint64_t output_hash(void)
{
    uint64_t value=UINT64_C(1469598103934665603);
    const size_t bytes=bench_workload==WORKLOAD_PRICE?16u:32u;
    for(uint32_t i=0;i<bench_k;++i)
        value=hash_bytes(value,&bench_output->values[i],bytes);
    return value;
}

static void snapshot_output(asian_genuine_strip_value_t snapshot[32])
{
    const size_t bytes=bench_workload==WORKLOAD_PRICE?16u:32u;
    memset(snapshot,0,32u*sizeof(snapshot[0]));
    for(uint32_t i=0;i<bench_k;++i)
        memcpy(&snapshot[i],&bench_output->values[i],bytes);
}

static int output_matches(const asian_genuine_strip_value_t expected[32])
{
    const size_t bytes=bench_workload==WORKLOAD_PRICE?16u:32u;
    for(uint32_t i=0;i<bench_k;++i)
        if(memcmp(&expected[i],&bench_output->values[i],bytes)!=0)return 0;
    return 1;
}

static void reset_outside_timing(enum candidate candidate)
{
    activate_candidate(candidate);
    memset(bench_output,0,sizeof(*bench_output));
    if(!uses_immediate(bench_k,bench_workload))
        memset(candidate_q(candidate),0xa5,PATHS*sizeof(float));
}

static void condition_outside_timing(enum candidate candidate,
                                     enum cache_mode cache)
{
    const asian_genuine_route_t *routes=candidate==CANDIDATE_CURRENT?
      bench_fixture.poisoned_routes:bench_fixture.poisoned_fused_routes;
    uint64_t value=touch(asian_genuine_fixed_block_signed_z,32768u)+
      touch(routes,(size_t)bench_n*sizeof(*routes))+
      touch(bench_fixture.maps,(size_t)bench_n*sizeof(*bench_fixture.maps))+
      touch(bench_strip,sizeof(*bench_strip));
    if(candidate==CANDIDATE_CURRENT)value+=touch(bench_fixture.x,32768u);
    value+=touch(candidate_growth(candidate),32768u);
    if(!uses_immediate(bench_k,bench_workload))
        value+=touch(candidate_q(candidate),PATHS*sizeof(float));
    else
        value+=touch(&bench_immediate_context[candidate],
                     sizeof(bench_immediate_context[candidate]));
    if(cache==CACHE_HISTORICAL_RMW){
        for(uint32_t i=0;i<8192u;++i){
            bench_pressure[i]+=i+3u;
            value+=bench_pressure[i];
        }
    }
    bench_sink+=value;
}

static duration_t measure_complete(enum candidate candidate,
                                   enum cache_mode cache,uint64_t *hash)
{
    void (*const runner)(void)=candidate==CANDIDATE_CURRENT?
      run_complete_current:run_complete_fused;
    reset_outside_timing(candidate);
    condition_outside_timing(candidate,cache);
    const int cpu_before=sched_getcpu();
    const uint64_t wall0=wall_now(),tsc0=tsc_begin();
    runner();
    const uint64_t tsc1=tsc_end(),wall1=wall_now();
    const int cpu_after=sched_getcpu();
    if(cpu_before<0||cpu_before!=cpu_after)abort();
    *hash=output_hash();
    bench_sink+=*hash;
    return(duration_t){tsc1-tsc0,wall1-wall0};
}

static duration_t measure_frontend(enum candidate candidate,
                                   enum cache_mode cache,uint64_t *hash)
{
    void (*const runner)(void)=candidate==CANDIDATE_CURRENT?
      run_current_frontend:run_fused_frontend;
    condition_outside_timing(candidate,cache);
    const int cpu_before=sched_getcpu();
    const uint64_t wall0=wall_now(),tsc0=tsc_begin();
    runner();
    const uint64_t tsc1=tsc_end(),wall1=wall_now();
    const int cpu_after=sched_getcpu();
    if(cpu_before<0||cpu_before!=cpu_after)abort();
    *hash=hash_bytes(UINT64_C(1469598103934665603),candidate_growth(candidate),
                     2u*PATHS*sizeof(float));
    bench_sink+=*hash;
    return(duration_t){tsc1-tsc0,wall1-wall0};
}

static int prepare_cell(uint32_t n,uint32_t k,enum workload workload)
{
    static const float strikes[32]={
      101.25f,88.75f,113.5f,97.0f,76.0f,124.0f,92.5f,108.0f,
      81.0f,118.0f,99.5f,105.0f,72.0f,128.0f,95.0f,111.0f,
      84.0f,121.0f,90.0f,115.0f,78.0f,126.0f,103.0f,100.0f,
      87.0f,117.0f,93.0f,109.0f,74.0f,130.0f,98.0f,106.0f};
    uint32_t padded=0;
    const asian_genuine_fixed_block_source_request_t request=
      source_request(&markets[2],n);
    bench_n=n;
    bench_k=k;
    bench_workload=workload;
    bench_immediate_leaf=select_immediate(k,workload);
    if(asian_genuine_fixed_block_source_prepare(&bench_source,&request)!=
         ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK||
       asian_genuine_arithmetic_fused_source_exp_prepare(
         bench_fixture.fused_source_context,&request,bench_fixture.growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_OK)return-1;
    run_current_frontend();
    run_fused_frontend();
    if(memcmp(bench_fixture.growth,bench_fixture.growth_fused,
         2u*PATHS*sizeof(float))!=0)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(bench_fixture.context,
         bench_fixture.poisoned_routes,n,100.0f,bench_fixture.growth,
         2u*PATHS*sizeof(float),bench_fixture.q,PATHS*sizeof(float))!=0||
       asian_genuine_arithmetic_growth_only_prepare(bench_fixture.fused_context,
         bench_fixture.poisoned_fused_routes,n,100.0f,
         bench_fixture.growth_fused,2u*PATHS*sizeof(float),
         bench_fixture.q_fused,PATHS*sizeof(float))!=0||
       asian_genuine_arithmetic_growth_only_strip_prepare_padded(bench_strip,
         100.0,markets[2].rate,markets[2].dividend,markets[2].sigma,
         markets[2].maturity,n,0u,0.0,0.0,strikes,k,&padded)!=0||
       padded!=(k==1u?1u:k<=4u?4u:32u))return-1;
    bench_immediate_context[CANDIDATE_CURRENT]=*bench_fixture.context;
    bench_immediate_context[CANDIDATE_FUSED]=*bench_fixture.fused_context;
    bench_immediate_context[CANDIDATE_CURRENT].q_out=(float *)(uintptr_t)1u;
    bench_immediate_context[CANDIDATE_FUSED].q_out=(float *)(uintptr_t)1u;
    return 0;
}

static int compare_current_cell(void)
{
    asian_genuine_strip_value_t expected[32];
    reset_outside_timing(CANDIDATE_CURRENT);
    run_complete_current();
    snapshot_output(expected);
    reset_outside_timing(CANDIDATE_FUSED);
    run_complete_fused();
    if(!output_matches(expected))return-1;
    reset_outside_timing(CANDIDATE_FUSED);
    run_complete_fused();
    return output_matches(expected)&&
      bench_immediate_context[CANDIDATE_CURRENT].q_out==(float *)(uintptr_t)1u&&
      bench_immediate_context[CANDIDATE_FUSED].q_out==(float *)(uintptr_t)1u?0:-1;
}

static int bounded_correctness(void)
{
    static const uint32_t ns[]={2u,16u,256u};
    static const uint32_t ks[]={1u,2u,4u,32u};
    for(uint32_t ni=0;ni<3u;++ni)
        for(uint32_t ki=0;ki<4u;++ki)
            for(uint32_t workload=0;workload<2u;++workload)
                if(prepare_cell(ns[ni],ks[ki],(enum workload)workload)!=0||
                   compare_current_cell()!=0)return-1;
    puts("bounded_native_correctness PASS");
    return 0;
}

static const char *decision(double wall_speedup,double tsc_speedup)
{
    if(wall_speedup<0.99||tsc_speedup<0.99)return"LOSE";
    if(wall_speedup>1.01&&tsc_speedup>1.01)return"WIN";
    return"TIE";
}

static int time_complete_cell(uint32_t n,uint32_t k,enum workload workload,
                              enum cache_mode cache,int *n_ok,int *n_win)
{
    const char *workload_name=workload==WORKLOAD_PRICE?"price":"price_delta";
    const char *cache_name=cache==CACHE_CANDIDATE_WARM?
      "candidate_warm":"historical_32KiB_rmw";
    if(prepare_cell(n,k,workload)!=0||compare_current_cell()!=0)return-1;
    double current_wall[MEASURED_QUARTETS],fused_wall[MEASURED_QUARTETS];
    double current_tsc[MEASURED_QUARTETS],fused_tsc[MEASURED_QUARTETS];
    uint64_t expected_hash=0;
    asian_genuine_strip_value_t expected_output[32];
    int have_output=0;
    for(uint32_t quartet=0;quartet<WARMUP_QUARTETS+MEASURED_QUARTETS;++quartet){
        static const enum candidate abba[4]={CANDIDATE_CURRENT,CANDIDATE_FUSED,
          CANDIDATE_FUSED,CANDIDATE_CURRENT};
        static const enum candidate baab[4]={CANDIDATE_FUSED,CANDIDATE_CURRENT,
          CANDIDATE_CURRENT,CANDIDATE_FUSED};
        const enum candidate *order=(quartet&1u)?baab:abba;
        uint64_t sum_wall[2]={0,0},sum_tsc[2]={0,0};
        for(uint32_t observation=0;observation<QUARTET_OBSERVATIONS;++observation){
            uint64_t hash;
            const enum candidate candidate=order[observation];
            const duration_t duration=measure_complete(candidate,cache,&hash);
            if(!have_output){
                expected_hash=hash;snapshot_output(expected_output);have_output=1;
            }else if(hash!=expected_hash||!output_matches(expected_output))return-1;
            sum_wall[candidate]+=duration.wall;
            sum_tsc[candidate]+=duration.tsc;
        }
        if(quartet>=WARMUP_QUARTETS){
            const uint32_t sample=quartet-WARMUP_QUARTETS;
            current_wall[sample]=(double)sum_wall[CANDIDATE_CURRENT]*0.5;
            fused_wall[sample]=(double)sum_wall[CANDIDATE_FUSED]*0.5;
            current_tsc[sample]=(double)sum_tsc[CANDIDATE_CURRENT]*0.5;
            fused_tsc[sample]=(double)sum_tsc[CANDIDATE_FUSED]*0.5;
        }
    }
    const double cw=median51(current_wall),fw=median51(fused_wall);
    const double ct=median51(current_tsc),ft=median51(fused_tsc);
    const double wall_speedup=cw/fw,tsc_speedup=ct/ft;
    const char *result=decision(wall_speedup,tsc_speedup);
    if(strcmp(result,"LOSE")==0)*n_ok=0;
    if(strcmp(result,"WIN")==0)*n_win=1;
    printf("%u\t%u\t%s\t%s\t%.2f\t%.2f\t%.6f\t%.2f\t%.2f\t%.6f\tEXACT\t%s\n",
      n,k,workload_name,cache_name,cw,fw,wall_speedup,ct,ft,tsc_speedup,result);
    return 0;
}

static int measure_crossover_cell(uint32_t n,uint32_t k,
                                  enum workload workload,
                                  enum cache_mode cache,
                                  double *wall_speedup,double *tsc_speedup)
{
    if(prepare_cell(n,k,workload)!=0||compare_current_cell()!=0)return-1;
    double current_wall[MEASURED_QUARTETS],fused_wall[MEASURED_QUARTETS];
    double current_tsc[MEASURED_QUARTETS],fused_tsc[MEASURED_QUARTETS];
    uint64_t expected_hash=0;
    asian_genuine_strip_value_t expected_output[32];
    int have_output=0;
    for(uint32_t quartet=0;quartet<WARMUP_QUARTETS+MEASURED_QUARTETS;++quartet){
        static const enum candidate abba[4]={CANDIDATE_CURRENT,CANDIDATE_FUSED,
          CANDIDATE_FUSED,CANDIDATE_CURRENT};
        static const enum candidate baab[4]={CANDIDATE_FUSED,CANDIDATE_CURRENT,
          CANDIDATE_CURRENT,CANDIDATE_FUSED};
        const enum candidate *order=(quartet&1u)?baab:abba;
        uint64_t sum_wall[2]={0,0},sum_tsc[2]={0,0};
        for(uint32_t observation=0;observation<QUARTET_OBSERVATIONS;++observation){
            uint64_t hash;
            const enum candidate candidate=order[observation];
            const duration_t duration=measure_complete(candidate,cache,&hash);
            if(!have_output){
                expected_hash=hash;snapshot_output(expected_output);have_output=1;
            }else if(hash!=expected_hash||!output_matches(expected_output))return-1;
            sum_wall[candidate]+=duration.wall;
            sum_tsc[candidate]+=duration.tsc;
        }
        if(quartet>=WARMUP_QUARTETS){
            const uint32_t sample=quartet-WARMUP_QUARTETS;
            current_wall[sample]=(double)sum_wall[CANDIDATE_CURRENT]*0.5;
            fused_wall[sample]=(double)sum_wall[CANDIDATE_FUSED]*0.5;
            current_tsc[sample]=(double)sum_tsc[CANDIDATE_CURRENT]*0.5;
            fused_tsc[sample]=(double)sum_tsc[CANDIDATE_FUSED]*0.5;
        }
    }
    *wall_speedup=median51(current_wall)/median51(fused_wall);
    *tsc_speedup=median51(current_tsc)/median51(fused_tsc);
    return 0;
}

static int run_crossover(void)
{
    static const uint32_t ns[]={128u,144u,160u,176u,192u,208u,224u,240u,256u};
    static const uint32_t ks[]={1u,4u,32u};
    puts("N\tworst_wall\tworst_tsc\tmedian_wall\tmedian_tsc\tdecision");
    for(uint32_t ni=0;ni<sizeof(ns)/sizeof(ns[0]);++ni){
        double wall[12]={0},tsc[12]={0};
        const uint32_t repetitions=ns[ni]==256u?2u:1u;
        for(uint32_t repetition=0;repetition<repetitions;++repetition){
            uint32_t cell=0;
            for(uint32_t ki=0;ki<sizeof(ks)/sizeof(ks[0]);++ki)
                for(uint32_t workload=0;workload<2u;++workload)
                    for(uint32_t cache=0;cache<2u;++cache,++cell){
                        double observed_wall,observed_tsc;
                        if(measure_crossover_cell(ns[ni],ks[ki],
                             (enum workload)workload,(enum cache_mode)cache,
                             &observed_wall,&observed_tsc)!=0){
                            fprintf(stderr,
                              "crossover identity failure N=%u K=%u workload=%u cache=%u repeat=%u\n",
                              ns[ni],ks[ki],workload,cache,repetition);return-1;
                        }
                        if(repetition==0u||observed_wall<wall[cell])
                            wall[cell]=observed_wall;
                        if(repetition==0u||observed_tsc<tsc[cell])
                            tsc[cell]=observed_tsc;
                    }
        }
        double worst_wall=wall[0],worst_tsc=tsc[0];
        int has_win=0;
        for(uint32_t cell=0;cell<12u;++cell){
            if(wall[cell]<worst_wall)worst_wall=wall[cell];
            if(tsc[cell]<worst_tsc)worst_tsc=tsc[cell];
            if(wall[cell]>1.01&&tsc[cell]>1.01)has_win=1;
        }
        double wall_for_median[12],tsc_for_median[12];
        memcpy(wall_for_median,wall,sizeof(wall));
        memcpy(tsc_for_median,tsc,sizeof(tsc));
        const char *result=worst_wall<0.99||worst_tsc<0.99?
          "CURRENT":has_win?"FUSED":"CURRENT";
        printf("%u\t%.6f\t%.6f\t%.6f\t%.6f\t%s\n",ns[ni],
          worst_wall,worst_tsc,equal_cell_median12(wall_for_median),
          equal_cell_median12(tsc_for_median),result);
    }
    return 0;
}

static int measure_confirm_batch(uint32_t n,uint32_t k,
                                 enum workload workload,
                                 enum cache_mode cache,
                                 double *wall_speedup,double *tsc_speedup)
{
    if(prepare_cell(n,k,workload)!=0||compare_current_cell()!=0)return-1;
    double current_wall[CONFIRM_QUARTETS],fused_wall[CONFIRM_QUARTETS];
    double current_tsc[CONFIRM_QUARTETS],fused_tsc[CONFIRM_QUARTETS];
    uint64_t expected_hash=0;
    asian_genuine_strip_value_t expected_output[32];
    int have_output=0;
    for(uint32_t quartet=0;quartet<WARMUP_QUARTETS+CONFIRM_QUARTETS;++quartet){
        static const enum candidate abba[4]={CANDIDATE_CURRENT,CANDIDATE_FUSED,
          CANDIDATE_FUSED,CANDIDATE_CURRENT};
        static const enum candidate baab[4]={CANDIDATE_FUSED,CANDIDATE_CURRENT,
          CANDIDATE_CURRENT,CANDIDATE_FUSED};
        const enum candidate *order=(quartet&1u)?baab:abba;
        uint64_t sum_wall[2]={0,0},sum_tsc[2]={0,0};
        for(uint32_t observation=0;observation<QUARTET_OBSERVATIONS;++observation){
            uint64_t hash;
            const enum candidate candidate=order[observation];
            const duration_t duration=measure_complete(candidate,cache,&hash);
            if(!have_output){
                expected_hash=hash;snapshot_output(expected_output);have_output=1;
            }else if(hash!=expected_hash||!output_matches(expected_output))return-1;
            sum_wall[candidate]+=duration.wall;
            sum_tsc[candidate]+=duration.tsc;
        }
        if(quartet>=WARMUP_QUARTETS){
            const uint32_t sample=quartet-WARMUP_QUARTETS;
            current_wall[sample]=(double)sum_wall[CANDIDATE_CURRENT]*0.5;
            fused_wall[sample]=(double)sum_wall[CANDIDATE_FUSED]*0.5;
            current_tsc[sample]=(double)sum_tsc[CANDIDATE_CURRENT]*0.5;
            fused_tsc[sample]=(double)sum_tsc[CANDIDATE_FUSED]*0.5;
        }
    }
    *wall_speedup=median201(current_wall)/median201(fused_wall);
    *tsc_speedup=median201(current_tsc)/median201(fused_tsc);
    return 0;
}

static int run_confirm(void)
{
    static const uint32_t ns[]={176u,192u,208u,224u,240u,256u};
    static const uint32_t ks[]={1u,4u,32u};
    puts("N\tK\tworkload\tcache\tscout_worst\tconfirm_wall_median\tconfirm_tsc_median\tconfirm_wall_min\tconfirm_wall_max\tbatches_below_0.99\tdecision");
    for(uint32_t ni=0;ni<sizeof(ns)/sizeof(ns[0]);++ni){
        uint32_t selected_k=0;
        enum workload selected_workload=WORKLOAD_PRICE;
        enum cache_mode selected_cache=CACHE_CANDIDATE_WARM;
        double scout_worst=0;
        int have_selection=0;
        for(uint32_t ki=0;ki<sizeof(ks)/sizeof(ks[0]);++ki)
            for(uint32_t workload=0;workload<2u;++workload)
                for(uint32_t cache=0;cache<2u;++cache){
                    double wall_speedup,tsc_speedup;
                    if(measure_crossover_cell(ns[ni],ks[ki],
                         (enum workload)workload,(enum cache_mode)cache,
                         &wall_speedup,&tsc_speedup)!=0){
                        fprintf(stderr,
                          "confirm scout identity failure N=%u K=%u workload=%u cache=%u\n",
                          ns[ni],ks[ki],workload,cache);return-1;
                    }
                    const double score=wall_speedup<tsc_speedup?
                      wall_speedup:tsc_speedup;
                    if(!have_selection||score<scout_worst){
                        selected_k=ks[ki];
                        selected_workload=(enum workload)workload;
                        selected_cache=(enum cache_mode)cache;
                        scout_worst=score;
                        have_selection=1;
                    }
                }

        double wall[CONFIRM_BATCHES],tsc[CONFIRM_BATCHES];
        double wall_min=0,wall_max=0;
        uint32_t batches_below=0;
        for(uint32_t batch=0;batch<CONFIRM_BATCHES;++batch){
            if(measure_confirm_batch(ns[ni],selected_k,selected_workload,
                 selected_cache,&wall[batch],&tsc[batch])!=0){
                fprintf(stderr,
                  "confirm identity failure N=%u K=%u workload=%u cache=%u batch=%u\n",
                  ns[ni],selected_k,selected_workload,selected_cache,batch);
                return-1;
            }
            if(batch==0u||wall[batch]<wall_min)wall_min=wall[batch];
            if(batch==0u||wall[batch]>wall_max)wall_max=wall[batch];
            if(wall[batch]<0.99&&tsc[batch]<0.99)++batches_below;
        }
        const double wall_median=median5(wall),tsc_median=median5(tsc);
        const char *result=wall_median<0.99&&tsc_median<0.99&&
          batches_below>=4u?"CONFIRMED_REGRESSION":
          wall_median>=0.99&&tsc_median>=0.99?
          "NO_MATERIAL_REGRESSION":"UNSTABLE";
        const char *workload_name=selected_workload==WORKLOAD_PRICE?
          "price":"price_delta";
        const char *cache_name=selected_cache==CACHE_CANDIDATE_WARM?
          "candidate_warm":"historical_32KiB_rmw";
        printf("%u\t%u\t%s\t%s\t%.6f\t%.6f\t%.6f\t%.6f\t%.6f\t%u\t%s\n",
          ns[ni],selected_k,workload_name,cache_name,scout_worst,
          wall_median,tsc_median,wall_min,wall_max,batches_below,result);
    }
    return 0;
}

static int time_frontend_cell(uint32_t n,enum cache_mode cache,
                              double *wall_speedup,double *tsc_speedup)
{
    if(prepare_cell(n,1u,WORKLOAD_PRICE)!=0)return-1;
    double current_wall[MEASURED_QUARTETS],fused_wall[MEASURED_QUARTETS];
    double current_tsc[MEASURED_QUARTETS],fused_tsc[MEASURED_QUARTETS];
    uint64_t expected_hash=0;
    int have_hash=0;
    for(uint32_t quartet=0;quartet<WARMUP_QUARTETS+MEASURED_QUARTETS;++quartet){
        static const enum candidate abba[4]={CANDIDATE_CURRENT,CANDIDATE_FUSED,
          CANDIDATE_FUSED,CANDIDATE_CURRENT};
        static const enum candidate baab[4]={CANDIDATE_FUSED,CANDIDATE_CURRENT,
          CANDIDATE_CURRENT,CANDIDATE_FUSED};
        const enum candidate *order=(quartet&1u)?baab:abba;
        uint64_t sum_wall[2]={0,0},sum_tsc[2]={0,0};
        for(uint32_t observation=0;observation<4u;++observation){
            uint64_t hash;
            const enum candidate candidate=order[observation];
            const duration_t duration=measure_frontend(candidate,cache,&hash);
            if(!have_hash){expected_hash=hash;have_hash=1;}
            else if(hash!=expected_hash)return-1;
            sum_wall[candidate]+=duration.wall;
            sum_tsc[candidate]+=duration.tsc;
        }
        if(quartet>=WARMUP_QUARTETS){
            const uint32_t sample=quartet-WARMUP_QUARTETS;
            current_wall[sample]=(double)sum_wall[CANDIDATE_CURRENT]*0.5;
            fused_wall[sample]=(double)sum_wall[CANDIDATE_FUSED]*0.5;
            current_tsc[sample]=(double)sum_tsc[CANDIDATE_CURRENT]*0.5;
            fused_tsc[sample]=(double)sum_tsc[CANDIDATE_FUSED]*0.5;
        }
    }
    *wall_speedup=median51(current_wall)/median51(fused_wall);
    *tsc_speedup=median51(current_tsc)/median51(fused_tsc);
    return 0;
}

int main(int argc,char **argv)
{
    int cpu=-1,check_only=0;
    enum timing_mode mode=TIMING_FULL;
    for(int i=1;i<argc;++i){
        if(strcmp(argv[i],"--check-only")==0)check_only=1;
        else if(strcmp(argv[i],"--cpu")==0&&i+1<argc)cpu=atoi(argv[++i]);
        else if(strcmp(argv[i],"--mode=crossover")==0)mode=TIMING_CROSSOVER;
        else if(strcmp(argv[i],"--mode=confirm")==0)mode=TIMING_CONFIRM;
        else if(strcmp(argv[i],"--mode=full")==0)mode=TIMING_FULL;
        else{
            fprintf(stderr,
              "usage: %s --cpu CPU [--check-only] [--mode=full|crossover|confirm]\n",
              argv[0]);
            return 2;
        }
    }
    if(cpu<0){fprintf(stderr,"--cpu is required\n");return 2;}
    if(pin_cpu(cpu)!=0){perror("sched_setaffinity");return 2;}
    if(!is_sapphire_rapids()){
        fprintf(stderr,"verified Sapphire Rapids CPU required\n");return 2;
    }
    asian_genuine_arithmetic_downstream_layout_anchor();
    if(fixture_init(&bench_fixture)!=0){fprintf(stderr,"fixture failed\n");return 2;}
    bench_strip=a64(sizeof(*bench_strip));
    bench_output=a64(sizeof(*bench_output));
    bench_pressure=a64(8192u*sizeof(*bench_pressure));
    if(!bench_strip||!bench_output||!bench_pressure){
        fprintf(stderr,"allocation failed\n");return 2;
    }
    for(uint32_t i=0;i<8192u;++i)bench_pressure[i]=i+1u;
    if(check_only){
        if(bounded_correctness()!=0){
            fprintf(stderr,"bounded correctness failed\n");return 1;
        }
        return 0;
    }
    if(mode==TIMING_CROSSOVER)return run_crossover();
    if(mode==TIMING_CONFIRM)return run_confirm();
    if(bounded_correctness()!=0){
        fprintf(stderr,"bounded correctness failed\n");return 1;
    }

    static const uint32_t ns[]={16u,32u,64u,128u,256u};
    static const uint32_t ks[]={1u,2u,4u,32u};
    int n_ok[5]={1,1,1,1,1},n_win[5]={0,0,0,0,0};
    puts("N\tK\tworkload\tcache\tcurrent_wall_ns\tfused_wall_ns\twall_speedup\tcurrent_tsc\tfused_tsc\ttsc_speedup\toutput_identity\tdecision");
    for(uint32_t ni=0;ni<5u;++ni)
        for(uint32_t ki=0;ki<4u;++ki)
            for(uint32_t workload=0;workload<2u;++workload)
                for(uint32_t cache=0;cache<2u;++cache)
                    if(time_complete_cell(ns[ni],ks[ki],(enum workload)workload,
                         (enum cache_mode)cache,&n_ok[ni],&n_win[ni])!=0){
                        fprintf(stderr,"timing identity failure N=%u K=%u workload=%u cache=%u\n",
                          ns[ni],ks[ki],workload,cache);return 1;
                    }

    double front_wall[10],front_tsc[10];
    uint32_t front_cell=0;
    for(uint32_t ni=0;ni<5u;++ni)
        for(uint32_t cache=0;cache<2u;++cache,++front_cell)
            if(time_frontend_cell(ns[ni],(enum cache_mode)cache,
                 &front_wall[front_cell],&front_tsc[front_cell])!=0){
                fprintf(stderr,"front-end timing identity failure N=%u cache=%u\n",
                  ns[ni],cache);return 1;
            }
    printf("front_end_speedup_wall=%.6f\n",equal_cell_median10(front_wall));
    printf("front_end_speedup_tsc=%.6f\n",equal_cell_median10(front_tsc));
    for(uint32_t ni=0;ni<5u;++ni)
        printf("N=%-3u %s\n",ns[ni],n_ok[ni]&&n_win[ni]?"FUSED":"CURRENT");
    bench_sink+=touch(bench_output,sizeof(*bench_output));
    return bench_sink==UINT64_MAX?1:0;
}
