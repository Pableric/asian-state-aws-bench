#define _GNU_SOURCE

#include "private/autocall_single_asset_three_date_raw_diag.h"
#include "private/asian_genuine_permute.h"
#include "tests/autocall_single_asset_three_date_cases.h"

#include <cpuid.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { OBSERVATIONS=101, WARMUPS=16, PRESSURE_BYTES=32768 };

typedef struct { uint64_t wall, tsc; } sample_t;
typedef struct {
    autocall_3date_engine_t engine __attribute__((aligned(64)));
    autocall_3date_carrier_t *carrier[2];
    autocall_3date_carrier_t *scratch_carrier[2];
    autocall_3date_request_t *request[2];
    autocall_3date_request_t *scratch_request[2];
    autocall_3date_request_input_t input[2];
    double expected[2];
    unsigned char *pressure;
    asian_meta_qsort_control_plan_t *generic_oracle;
} fixture_t;

struct asian_meta_qsort_control_plan {
    uint32_t magic;
    uint8_t donor_region[ASIAN_META_DIRECTIONS];
    uint8_t reserved[60];
    fragment_map_t maps[ASIAN_META_DIRECTIONS];
};

static volatile double sink;
enum metric { M_MARKET, M_REQUEST, M_PREPARED, M_REUSE, M_FRESH };

static void *a64(size_t bytes)
{
    void *pointer=NULL;
    return posix_memalign(&pointer,64u,bytes)==0 ? pointer : NULL;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW,&ts);
    return (uint64_t)ts.tv_sec*UINT64_C(1000000000)+(uint64_t)ts.tv_nsec;
}

static inline uint64_t tsc_begin(void)
{
    unsigned lo,hi;
    __asm__ volatile("lfence\n\trdtsc" : "=a"(lo),"=d"(hi) :: "memory");
    return ((uint64_t)hi<<32)|lo;
}

static inline uint64_t tsc_end(void)
{
    unsigned lo,hi,aux;
    __asm__ volatile("rdtscp\n\tlfence" : "=a"(lo),"=d"(hi),"=c"(aux)
                     :: "memory");
    return ((uint64_t)hi<<32)|lo;
}

static int cmp_u64(const void *a,const void *b)
{
    const uint64_t aa=*(const uint64_t *)a,bb=*(const uint64_t *)b;
    return aa>bb ? 1 : aa<bb ? -1 : 0;
}

static uint64_t median(uint64_t values[OBSERVATIONS])
{
    qsort(values,OBSERVATIONS,sizeof(*values),cmp_u64);
    return values[OBSERVATIONS/2];
}

static uint64_t median5(uint64_t values[5])
{
    qsort(values,5,sizeof(*values),cmp_u64);
    return values[2];
}

static int bit_equal(double a,double b)
{
    uint64_t aa,bb; memcpy(&aa,&a,8); memcpy(&bb,&b,8); return aa==bb;
}

static autocall_3date_request_input_t make_input(
    const autocall_frozen_case_t *c)
{
    autocall_3date_request_input_t out={0};
    out.s0=c->s0; out.rate=c->rate; out.dividend_yield=c->dividend;
    out.sigma=c->sigma; out.maturity=c->maturity; out.notional=c->notional;
    out.protection_barrier=c->protection;
    out.terminal_payment_time=c->maturity*(1.0+c->payment_lag_fraction);
    for (unsigned d=0;d<3;++d) {
        out.call_barrier[d]=c->call_barrier[d];
        out.coupon_barrier[d]=c->coupon_barrier[d];
        out.coupon_cashflow[d]=c->coupon[d];
        out.call_redemption[d]=c->redemption[d];
        const double obs=c->maturity*(d+1.0)/3.0;
        out.coupon_payment_time[d]=obs+c->payment_lag_fraction*c->maturity;
        out.call_payment_time[d]=out.coupon_payment_time[d];
    }
    return out;
}

static autocall_3date_market_input_t market_of(
    const autocall_3date_request_input_t *input)
{
    const autocall_3date_market_input_t out={input->rate,input->dividend_yield,
        input->sigma,input->maturity};
    return out;
}

static void bind_generic(const struct asian_meta_qsort_control_plan *plan,
    const autocall_3date_carrier_t *carrier, asian_genuine_route_t route[3])
{
    for (unsigned d=0;d<3;++d) {
        route[d].x_base=NULL;
        route[d].growth_base=carrier->growth+
            (size_t)plan->donor_region[d]*ASIAN_META_PATHS;
        route[d].map=&plan->maps[d];
        route[d].weight_bits=0; route[d].fixing_index=d;
    }
}

static int pin_cpu(unsigned cpu)
{
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu,&set);
    return sched_setaffinity(0,sizeof(set),&set);
}

static int sapphire_rapids(void)
{
    unsigned a,b,c,d;
    if (!__get_cpuid(1,&a,&b,&c,&d)) return 0;
    const unsigned family=((a>>8)&15u)+(((a>>8)&15u)==15u ? (a>>20)&255u:0u);
    const unsigned model=((a>>4)&15u)|(((a>>16)&15u)<<4);
    return family==6u && model==143u;
}

static int fixture_create(fixture_t *f)
{
    memset(f,0,sizeof(*f));
    if (autocall_3date_engine_create(&f->engine)!=0) return -1;
    if (asian_meta_qsort_control_plan_create(&f->generic_oracle)!=0) return -1;
    f->input[0]=make_input(&autocall_frozen_cases[1]);
    f->input[1]=make_input(&autocall_frozen_cases[4]);
    f->pressure=a64(PRESSURE_BYTES);
    for (unsigned i=0;i<2;++i) {
        f->carrier[i]=a64(sizeof(*f->carrier[i]));
        f->scratch_carrier[i]=a64(sizeof(*f->scratch_carrier[i]));
        f->request[i]=a64(sizeof(*f->request[i]));
        f->scratch_request[i]=a64(sizeof(*f->scratch_request[i]));
        if (!f->carrier[i] || !f->scratch_carrier[i] || !f->request[i] ||
            !f->scratch_request[i] || !f->pressure) return -1;
        memset(f->scratch_carrier[i],0,sizeof(*f->scratch_carrier[i]));
        memset(f->scratch_request[i],0,sizeof(*f->scratch_request[i]));
        autocall_3date_market_input_t market=market_of(&f->input[i]);
        autocall_3date_output_t output;
        if (autocall_3date_market_prepare(&f->engine,&market,f->carrier[i])!=0 ||
            autocall_3date_request_prepare(&f->engine,f->carrier[i],
                                           &f->input[i],f->request[i])!=0 ||
            autocall_3date_prepared_price(f->request[i],&output)!=0) return -1;
        f->expected[i]=output.price;
        asian_genuine_route_t generic_route[3] __attribute__((aligned(64)));
        bind_generic((const struct asian_meta_qsort_control_plan *)
                     f->generic_oracle,f->carrier[i],generic_route);
        autocall_3date_leaf_context_t generic_context=f->request[i]->context;
        generic_context.d1_growth=generic_route[0].growth_base;
        generic_context.routes_d2=(const asian_meta_affine_route_t *)
            (const void *)(generic_route+1);
        if (!bit_equal(output.price,
                       autocall_3date_generic_price_leaf_test(&generic_context)))
            return -1;
    }
    memset(f->pressure,1,PRESSURE_BYTES);
    return 0;
}

static void fixture_destroy(fixture_t *f)
{
    for (unsigned i=0;i<2;++i) {
        free(f->scratch_request[i]); free(f->request[i]);
        free(f->scratch_carrier[i]); free(f->carrier[i]);
    }
    free(f->pressure); autocall_3date_engine_destroy(&f->engine);
    asian_meta_qsort_control_plan_destroy(f->generic_oracle);
}

static void pressure_32k(fixture_t *f)
{
    for (unsigned i=0;i<PRESSURE_BYTES;i+=64)
        f->pressure[i]=(unsigned char)(f->pressure[i]+1u);
    __asm__ volatile(""::"r"(f->pressure):"memory");
}

static void warm_route(const asian_meta_affine_route_t *route)
{
    volatile unsigned char value=0;
    const unsigned char *map=(const unsigned char *)(const void *)route->map;
    for (unsigned i=0;i<256u;i+=64u) value^=map[i];
    value^=map[320]; value^=map[384];
    __asm__ volatile(""::"r"(value):"memory");
}

static void candidate_warm(fixture_t *f,enum metric metric,unsigned which)
{
    volatile unsigned char value=0;
    const autocall_3date_carrier_t *carrier = metric==M_FRESH ?
        &f->engine.workspace->carrier : f->carrier[which];
    const autocall_3date_request_t *prepared = metric==M_PREPARED ?
        f->request[which] : &f->engine.workspace->request;
    if (metric==M_MARKET) carrier=f->scratch_carrier[which];
    if (metric==M_REQUEST) prepared=f->scratch_request[which];
    const unsigned char *request=(const unsigned char *)(const void *)
        prepared;
    const unsigned char *identity=(const unsigned char *)(const void *)
        &carrier->generation;
    value^=*identity;
    if (metric==M_REQUEST) {
        for (unsigned i=0;i<sizeof(*prepared);i+=64u) value^=request[i];
        value^=*(const volatile unsigned char *)(const void *)
            f->engine.affine_plan;
        __asm__ volatile(""::"r"(value):"memory");
        return;
    }
    const unsigned char *growth=(const unsigned char *)(const void *)
        carrier->growth;
    for (unsigned i=0;i<32768u;i+=64u) value^=growth[i];
    if (metric==M_MARKET) {
        __asm__ volatile(""::"r"(value):"memory");
        return;
    }
    for (unsigned i=0;i<sizeof(*prepared);i+=64u) value^=request[i];
    warm_route(f->request[which]->routes+1);
    warm_route(f->request[which]->routes+2);
    __asm__ volatile(""::"r"(value):"memory");
}

static int operation(fixture_t *f,enum metric metric,unsigned which,
                     autocall_3date_output_t *output)
{
    if (metric==M_MARKET) {
        autocall_3date_market_input_t market=market_of(&f->input[which]);
        return autocall_3date_market_prepare(&f->engine,&market,
                                             f->scratch_carrier[which]);
    }
    if (metric==M_REQUEST)
        return autocall_3date_request_prepare(&f->engine,f->carrier[which],
            &f->input[which],f->scratch_request[which]);
    if (metric==M_PREPARED)
        return autocall_3date_prepared_price(f->request[which],output);
    if (metric==M_REUSE)
        return autocall_3date_reuse_total(&f->engine,f->carrier[which],
                                          &f->input[which],output);
    return autocall_3date_fresh_total(&f->engine,&f->input[which],output);
}

static sample_t observe(fixture_t *f,enum metric metric,unsigned which,
                        int pressure)
{
    if (pressure) pressure_32k(f); else candidate_warm(f,metric,which);
    autocall_3date_output_t output={0};
    const uint64_t w0=now_ns(),t0=tsc_begin();
    const int status=operation(f,metric,which,&output);
    const uint64_t t1=tsc_end(),w1=now_ns();
    if (status!=0) abort();
    if (metric>=M_PREPARED) {
        if (!bit_equal(output.price,f->expected[which])) abort();
        sink+=output.price;
    }
    const sample_t sample={w1-w0,t1-t0}; return sample;
}

static sample_t bracket(void)
{
    const uint64_t w0=now_ns(),t0=tsc_begin();
    __asm__ volatile("":::"memory");
    const uint64_t t1=tsc_end(),w1=now_ns();
    const sample_t sample={w1-w0,t1-t0}; return sample;
}

static void measure(fixture_t *f,enum metric metric,int pressure,
                    sample_t result[2])
{
    for (unsigned warm=0;warm<WARMUPS;++warm) {
        const unsigned order[4]={warm&1u,(warm&1u)^1u,(warm&1u)^1u,warm&1u};
        for (unsigned j=0;j<4;++j) (void)observe(f,metric,order[j],pressure);
    }
    uint64_t wall[2][OBSERVATIONS],tsc[2][OBSERVATIONS];
    for (unsigned group=0;group<OBSERVATIONS;++group) {
        const unsigned first=group&1u;
        const unsigned order[4]={first,first^1u,first^1u,first};
        sample_t pair[2][2]; unsigned count[2]={0,0};
        for (unsigned j=0;j<4;++j) {
            const unsigned which=order[j];
            pair[which][count[which]++]=observe(f,metric,which,pressure);
        }
        for (unsigned which=0;which<2;++which) {
            wall[which][group]=(pair[which][0].wall+pair[which][1].wall)/2u;
            tsc[which][group]=(pair[which][0].tsc+pair[which][1].tsc)/2u;
        }
    }
    for (unsigned i=0;i<2;++i) {
        result[i].wall=median(wall[i]); result[i].tsc=median(tsc[i]);
    }
}

static int check_only(void)
{
    fixture_t f;
    if (fixture_create(&f)!=0) return 1;
    for (unsigned i=0;i<2;++i) {
        autocall_3date_output_t a,b,c;
        if (autocall_3date_prepared_price(f.request[i],&a)!=0 ||
            autocall_3date_reuse_total(&f.engine,f.carrier[i],&f.input[i],&b)!=0 ||
            autocall_3date_fresh_total(&f.engine,&f.input[i],&c)!=0 ||
            !bit_equal(a.price,f.expected[i]) || !bit_equal(b.price,f.expected[i]) ||
            !bit_equal(c.price,f.expected[i])) return 1;
    }
    fixture_destroy(&f);
    puts("benchmark_correctness PASS identity=PASS");
    return 0;
}

static int timing(void)
{
    uint64_t plan_wall[5],plan_tsc[5],engine_wall[5],engine_tsc[5];
    for (unsigned i=0;i<5;++i) {
        asian_meta_affine_plan_t *plan=NULL;
        uint64_t w0=now_ns(),t0=tsc_begin();
        if (asian_meta_affine_plan_create(&plan)!=0) return 1;
        uint64_t t1=tsc_end(),w1=now_ns();
        plan_wall[i]=w1-w0; plan_tsc[i]=t1-t0;
        asian_meta_affine_plan_destroy(plan);

        autocall_3date_engine_t temporary __attribute__((aligned(64)));
        w0=now_ns(); t0=tsc_begin();
        if (autocall_3date_engine_create(&temporary)!=0) return 1;
        t1=tsc_end(); w1=now_ns();
        engine_wall[i]=w1-w0; engine_tsc[i]=t1-t0;
        autocall_3date_engine_destroy(&temporary);
    }
    printf("initialization metric wall_ns tsc_ticks\n");
    printf("initialization full_affine_plan_create %llu %llu\n",
        (unsigned long long)median5(plan_wall),
        (unsigned long long)median5(plan_tsc));
    printf("initialization engine_create %llu %llu\n",
        (unsigned long long)median5(engine_wall),
        (unsigned long long)median5(engine_tsc));
    puts("initialization three_route_plan_create NOT_AVAILABLE full_plan_reused=YES");
    puts("initialization generic_qsort_oracle constructed=YES outside_lifecycle=YES");
    fixture_t f;
    if (fixture_create(&f)!=0) return 1;
    uint64_t bw[OBSERVATIONS],bt[OBSERVATIONS];
    for (unsigned i=0;i<OBSERVATIONS;++i) { sample_t s=bracket(); bw[i]=s.wall;bt[i]=s.tsc; }
    printf("metric contract cache wall_ns tsc_ticks valuations_per_sec_per_core "
           "path_observation_updates_per_sec_per_core identity\n");
    printf("timing_bracket ALL none %llu %llu 0 0 PASS\n",
           (unsigned long long)median(bw),(unsigned long long)median(bt));
    static const char *metric_name[]={"market_prepare","request_prepare",
        "prepared_price","reused_total","fresh_total"};
    static const char *case_name[]={"A_STEPDOWN","B_MATURITY_DOWNSIDE"};
    sample_t headline[2][3];
    for (unsigned cache=0;cache<2;++cache) for (unsigned metric=0;metric<5;++metric) {
        sample_t result[2]; measure(&f,(enum metric)metric,(int)cache,result);
        for (unsigned which=0;which<2;++which) {
            const double vps=1e9/(double)result[which].wall;
            const double updates=vps*4096.0*3.0;
            printf("%s %s %s %llu %llu %.6f %.6f PASS\n",metric_name[metric],
                case_name[which],cache?"pressure_32KiB":"candidate_warm",
                (unsigned long long)result[which].wall,
                (unsigned long long)result[which].tsc,vps,updates);
            if (!cache && metric>=M_PREPARED)
                headline[which][metric-M_PREPARED]=result[which];
        }
    }
    for (unsigned which=0;which<2;++which) {
        printf("AUTOCALL_3DATE_PREPARED contract=%s wall_ns=%llu identity=PASS\n",
            case_name[which],(unsigned long long)headline[which][0].wall);
        printf("AUTOCALL_3DATE_REUSED contract=%s wall_ns=%llu identity=PASS\n",
            case_name[which],(unsigned long long)headline[which][1].wall);
        printf("AUTOCALL_3DATE_FRESH contract=%s wall_ns=%llu identity=PASS\n",
            case_name[which],(unsigned long long)headline[which][2].wall);
    }
    const uint64_t worst=headline[0][2].wall>headline[1][2].wall ?
        headline[0][2].wall:headline[1][2].wall;
    puts(worst<=9000 ? "RAW_AUTOCALL_ARCHITECTURE_READY" :
         worst<=12000 ? "RAW_AUTOCALL_LIFECYCLE_INVESTIGATION" :
                       "RAW_AUTOCALL_ARCHITECTURE_MISSED");
    fixture_destroy(&f);
    return sink==0.123 ? 1:0;
}

int main(int argc,char **argv)
{
    if (argc==2 && strcmp(argv[1],"--check")==0) return check_only();
    if (argc==4 && strcmp(argv[1],"--native-check")==0 &&
        strcmp(argv[2],"--cpu")==0) {
        const unsigned cpu=(unsigned)strtoul(argv[3],NULL,10);
        if (cpu!=0 || !sapphire_rapids() || pin_cpu(cpu)!=0) return 2;
        return check_only();
    }
    if (argc==4 && strcmp(argv[1],"--timing")==0 &&
        strcmp(argv[2],"--cpu")==0) {
        const unsigned cpu=(unsigned)strtoul(argv[3],NULL,10);
        if (cpu!=0 || !sapphire_rapids() || pin_cpu(cpu)!=0) return 2;
        return timing();
    }
    fprintf(stderr,"usage: %s --check | --native-check --cpu 0 | --timing --cpu 0\n",argv[0]);
    return 2;
}
