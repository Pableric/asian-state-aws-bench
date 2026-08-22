#define _GNU_SOURCE

#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_geometric_cv_diag.h"
#include "private/asian_genuine_fixed_block_source_diag.h"
#include "private/asian_genuine_multistrike_full_risk_diag.h"
#include "private/asian_genuine_price_delta_strip_diag.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#ifndef FIXED_E2E_GIT_COMMIT
#define FIXED_E2E_GIT_COMMIT "unknown"
#endif

enum {
    PATHS = 4096,
    WARMUP_QUARTETS = 16,
    MEASURED_QUARTETS = 201,
    SOURCE_CANDIDATES = 3,
    STAGES = 4,
    N_COUNT = 5,
    WORKLOAD_COUNT = 7,
    MAX_GLOBAL_CELLS = N_COUNT * WORKLOAD_COUNT * 2,
};

enum source_candidate {
    SOURCE_X3 = 0,
    SOURCE_FIXED = 1,
    SOURCE_EXACT = 2,
};

enum workload_kind {
    WORKLOAD_PRICE = 0,
    WORKLOAD_PRICE_DELTA = 1,
    WORKLOAD_FULL_RISK = 2,
};

enum pipeline_stage {
    STAGE_SOURCE = 0,
    STAGE_VECTOR_EXP = 1,
    STAGE_EVOLUTION = 2,
    STAGE_CONSUMER = 3,
};

typedef struct {
    const char *name;
    enum workload_kind kind;
    uint32_t strikes;
    uint32_t fields;
} workload_spec_t;

static const uint32_t native_n[N_COUNT] = {16u, 32u, 64u, 128u, 256u};
static const workload_spec_t workloads[WORKLOAD_COUNT] = {
    {"single_strike_price", WORKLOAD_PRICE, 1u, 1u},
    {"single_strike_price_delta", WORKLOAD_PRICE_DELTA, 1u, 2u},
    {"single_strike_full_risk", WORKLOAD_FULL_RISK, 1u, 4u},
    {"multi_strike_full_risk_K4", WORKLOAD_FULL_RISK, 4u, 4u},
    {"multi_strike_full_risk_K8", WORKLOAD_FULL_RISK, 8u, 4u},
    {"multi_strike_full_risk_K16", WORKLOAD_FULL_RISK, 16u, 4u},
    {"multi_strike_full_risk_K32", WORKLOAD_FULL_RISK, 32u, 4u},
};

static const char *const candidate_names[SOURCE_CANDIDATES] = {
    "qualified_x3_source_baseline",
    "prepared_fixed_block_source_consumption",
    "prepared_exact_x_lookup_ceiling",
};

static const char *const stage_names[STAGES] = {
    "source", "vector_exp", "route_evolution", "consumer"
};

typedef struct {
    uint64_t tsc;
    uint64_t wall;
} duration_t;

typedef struct {
    duration_t candidate[2][MEASURED_QUARTETS];
} pair_complete_t;

typedef struct {
    duration_t candidate[2][STAGES][MEASURED_QUARTETS];
} pair_profile_t;

typedef struct {
    uint32_t source;
    uint32_t vector_exp;
    uint32_t evolution;
    uint32_t sql_dual_control;
    uint32_t basis_forward;
    uint32_t consumer_api;
    uint32_t l_to_g;
    uint32_t strip_leaf;
    uint32_t accumulator_init;
    uint32_t consume_block;
    uint32_t finalize;
    uint32_t tile4_leaf;
} invocation_counts_t;

typedef struct {
    uint32_t n;
    uint32_t k;
    uint32_t directions[256][32];
    uint32_t *words[2];
    uint32_t *pressure;
    float *x;
    float *growth;
    float *g;
    float *exact_x;
    fragment_map_t *maps;
    asian_genuine_route_t *routes;
    ordered_d1_diag_context_t *x3;
    asian_genuine_fixed_block_source_context_t *fixed;
    asian_genuine_fixed_block_exact_x_context_t *exact;
    asian_genuine_msfr_basis_controls_t *basis_controls;
    asian_genuine_msfr_basis_context_t *basis_context;
    asian_genuine_msfr_basis_t *basis;
    asian_genuine_msfr_strike_controls_t *strike_controls;
    asian_genuine_msfr_consumer_context_t *consumer_context;
    asian_genuine_msfr_accumulator_t *accumulator;
    asian_genuine_msfr_output_t *output;
    asian_genuine_state_t *state;
    asian_genuine_strip_context_t *strip_context;
    asian_genuine_strip_output_t *strip_output;
} fixture_t;

typedef struct {
    uint32_t n;
    uint32_t k;
    int estimator;
    int cache_mode;
    uint64_t tsc_median;
    uint64_t wall_median;
} historical_row_t;

typedef struct {
    int requested;
    int loaded;
    int compatible;
    char path[4096];
    char sha256[65];
    char commit[65];
    char binary_sha256[65];
    char measurement_date[64];
    char cpu_model[256];
    char kernel_release[128];
    char reason[160];
    historical_row_t rows[256];
    uint32_t row_count;
} historical_reference_t;

typedef struct {
    FILE *file;
    char *temporary;
} atomic_json_t;

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    unsigned char block[64];
    size_t used;
} sha256_t;

static fixture_t fixture;
static invocation_counts_t *active_counts;
static volatile double sink;
static uint32_t active_warmup_quartets = WARMUP_QUARTETS;
static uint32_t active_measured_quartets = MEASURED_QUARTETS;

static void *aligned64(size_t bytes)
{
    void *pointer = NULL;
    if (posix_memalign(&pointer, 64u, bytes) != 0) return NULL;
    memset(pointer, 0, bytes);
    return pointer;
}

static uint32_t sobol_word(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray = index ^ (index >> 1), word = 0;
    for (uint32_t bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u) word ^= directions[bit];
    return word;
}

static uint64_t tsc_begin(void)
{
    _mm_lfence();
    return __rdtsc();
}

static uint64_t tsc_end(void)
{
    unsigned auxiliary;
    const uint64_t value = __rdtscp(&auxiliary);
    _mm_lfence();
    return value;
}

static uint64_t wall_now(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value) != 0) abort();
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static int compare_u64(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

static uint32_t percentile_rank(uint32_t percentile)
{
    return (active_measured_quartets - 1u) * percentile / 100u;
}

static uint64_t quantile_u64(const uint64_t input[MEASURED_QUARTETS],
                             uint32_t percentile)
{
    uint64_t copy[MEASURED_QUARTETS];
    memcpy(copy, input, active_measured_quartets * sizeof(copy[0]));
    qsort(copy, active_measured_quartets, sizeof(copy[0]), compare_u64);
    return copy[percentile_rank(percentile)];
}

static double quantile_double(const double input[MEASURED_QUARTETS],
                              uint32_t percentile)
{
    double copy[MEASURED_QUARTETS];
    memcpy(copy, input, active_measured_quartets * sizeof(copy[0]));
    qsort(copy, active_measured_quartets, sizeof(copy[0]), compare_double);
    return copy[percentile_rank(percentile)];
}

static uint32_t rotate_right(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void sha256_block(sha256_t *hash, const unsigned char input[64])
{
    static const uint32_t constants[64] = {
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
      0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
      0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
      0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t words[64];
    for (uint32_t i = 0; i < 16u; ++i)
        words[i] = (uint32_t)input[4u*i] << 24 |
                   (uint32_t)input[4u*i+1u] << 16 |
                   (uint32_t)input[4u*i+2u] << 8 |
                   (uint32_t)input[4u*i+3u];
    for (uint32_t i = 16u; i < 64u; ++i) {
        const uint32_t a = rotate_right(words[i-15u],7)^rotate_right(words[i-15u],18)^(words[i-15u]>>3);
        const uint32_t b = rotate_right(words[i-2u],17)^rotate_right(words[i-2u],19)^(words[i-2u]>>10);
        words[i] = words[i-16u] + a + words[i-7u] + b;
    }
    uint32_t a=hash->state[0],b=hash->state[1],c=hash->state[2],d=hash->state[3];
    uint32_t e=hash->state[4],f=hash->state[5],g=hash->state[6],h=hash->state[7];
    for (uint32_t i=0;i<64u;++i) {
        const uint32_t s1=rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25);
        const uint32_t choose=(e&f)^(~e&g);
        const uint32_t t1=h+s1+choose+constants[i]+words[i];
        const uint32_t s0=rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22);
        const uint32_t majority=(a&b)^(a&c)^(b&c);
        const uint32_t t2=s0+majority;
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    hash->state[0]+=a;hash->state[1]+=b;hash->state[2]+=c;hash->state[3]+=d;
    hash->state[4]+=e;hash->state[5]+=f;hash->state[6]+=g;hash->state[7]+=h;
}

static void sha256_init(sha256_t *hash)
{
    static const uint32_t initial[8] = {
      0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
      0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    memset(hash,0,sizeof(*hash));
    memcpy(hash->state,initial,sizeof(initial));
}

static void sha256_add(sha256_t *hash, const void *data, size_t bytes)
{
    const unsigned char *input = data;
    hash->bits += (uint64_t)bytes * 8u;
    while (bytes != 0u) {
        size_t take = 64u - hash->used;
        if (take > bytes) take = bytes;
        memcpy(hash->block + hash->used,input,take);
        hash->used += take;input += take;bytes -= take;
        if (hash->used == 64u) {sha256_block(hash,hash->block);hash->used=0u;}
    }
}

static void sha256_finish(sha256_t *hash, char output[65])
{
    hash->block[hash->used++] = 0x80u;
    if (hash->used > 56u) {
        memset(hash->block+hash->used,0,64u-hash->used);
        sha256_block(hash,hash->block);hash->used=0u;
    }
    memset(hash->block+hash->used,0,56u-hash->used);
    for (uint32_t i=0;i<8u;++i)hash->block[63u-i]=(unsigned char)(hash->bits>>(8u*i));
    sha256_block(hash,hash->block);
    for (uint32_t i=0;i<8u;++i)sprintf(output+8u*i,"%08"PRIx32,hash->state[i]);
    output[64]=0;
}

static int file_sha256(const char *path, char output[65])
{
    FILE *file=fopen(path,"rb");if(file==NULL)return -1;
    sha256_t hash;sha256_init(&hash);unsigned char buffer[16384];size_t got;
    while((got=fread(buffer,1,sizeof(buffer),file))!=0u)sha256_add(&hash,buffer,got);
    const int failed=ferror(file);fclose(file);if(failed)return -1;
    sha256_finish(&hash,output);return 0;
}

static int binary_sha256(char output[65])
{
    char path[4096];const ssize_t count=readlink("/proc/self/exe",path,sizeof(path)-1u);
    if(count<0)return -1;
    path[count]=0;
    return file_sha256(path,output);
}

static int pin_first_cpu(void)
{
    cpu_set_t available,pinned;CPU_ZERO(&available);
    if(sched_getaffinity(0,sizeof(available),&available)!=0)return -1;
    int cpu=0;while(cpu<CPU_SETSIZE&&!CPU_ISSET(cpu,&available))++cpu;
    if(cpu==CPU_SETSIZE)return -1;
    CPU_ZERO(&pinned);CPU_SET(cpu,&pinned);
    return sched_setaffinity(0,sizeof(pinned),&pinned)==0?cpu:-1;
}

static int read_cpu_model(char output[256])
{
    FILE *file=fopen("/proc/cpuinfo","r");if(file==NULL)return -1;
    char line[512];output[0]=0;
    while(fgets(line,sizeof(line),file)!=NULL)if(strncmp(line,"model name",10u)==0){
        char *colon=strchr(line,':');if(colon!=NULL){++colon;while(*colon==' '||*colon=='\t')++colon;
            size_t length=strcspn(colon,"\r\n");if(length>255u)length=255u;
            memcpy(output,colon,length);output[length]=0;break;}}
    fclose(file);return output[0]==0?-1:0;
}

static int load_directions(void)
{
    FILE *file=fopen("direction_numbers/joe_kuo_6_21201.bin","rb");
    if(file==NULL)return -1;
    for(uint32_t dimension=0;dimension<256u;++dimension){uint32_t length;
        if(fread(&length,4u,1u,file)!=1u||length!=32u){fclose(file);return -1;}
        if(fread(fixture.directions[dimension],4u,32u,file)!=32u){fclose(file);return -1;}}
    fclose(file);return 0;
}

static void initialize_state(void)
{
    memset(fixture.state,0,sizeof(*fixture.state));
    for(uint32_t path=0;path<PATHS;++path)fixture.state->s[path]=100.0f;
}

static void release_fixture(void)
{
    free(fixture.strip_output);free(fixture.strip_context);free(fixture.state);
    free(fixture.output);free(fixture.accumulator);free(fixture.consumer_context);
    free(fixture.strike_controls);free(fixture.basis);free(fixture.basis_context);
    free(fixture.basis_controls);free(fixture.exact);free(fixture.fixed);free(fixture.x3);
    free(fixture.routes);free(fixture.maps);free(fixture.exact_x);free(fixture.g);
    free(fixture.growth);free(fixture.x);free(fixture.pressure);
    free(fixture.words[1]);free(fixture.words[0]);memset(&fixture,0,sizeof(fixture));
}

static int prepare_fixture(uint32_t n,uint32_t k)
{
    memset(&fixture,0,sizeof(fixture));fixture.n=n;fixture.k=k;
    if(load_directions()!=0)return -1;
    fixture.words[0]=aligned64(PATHS*4u);fixture.words[1]=aligned64(PATHS*4u);
    fixture.pressure=aligned64(8192u*4u);fixture.x=aligned64(8192u*4u);
    fixture.growth=aligned64(8192u*4u);fixture.g=aligned64(PATHS*4u);
    fixture.exact_x=aligned64(8192u*4u);fixture.maps=aligned64((size_t)n*sizeof(*fixture.maps));
    fixture.routes=aligned64((size_t)n*sizeof(*fixture.routes));
    fixture.x3=aligned64(sizeof(*fixture.x3));fixture.fixed=aligned64(sizeof(*fixture.fixed));
    fixture.exact=aligned64(sizeof(*fixture.exact));
    fixture.basis_controls=aligned64(sizeof(*fixture.basis_controls));
    fixture.basis_context=aligned64(sizeof(*fixture.basis_context));
    fixture.basis=aligned64(sizeof(*fixture.basis));
    fixture.strike_controls=aligned64(sizeof(*fixture.strike_controls));
    fixture.consumer_context=aligned64(sizeof(*fixture.consumer_context));
    fixture.accumulator=aligned64(sizeof(*fixture.accumulator));
    fixture.output=aligned64(sizeof(*fixture.output));fixture.state=aligned64(sizeof(*fixture.state));
    fixture.strip_context=aligned64(sizeof(*fixture.strip_context));
    fixture.strip_output=aligned64(sizeof(*fixture.strip_output));
    if(fixture.words[0]==NULL||fixture.words[1]==NULL||fixture.pressure==NULL||
       fixture.x==NULL||fixture.growth==NULL||fixture.g==NULL||fixture.exact_x==NULL||
       fixture.maps==NULL||fixture.routes==NULL||fixture.x3==NULL||fixture.fixed==NULL||
       fixture.exact==NULL||fixture.basis_controls==NULL||fixture.basis_context==NULL||
       fixture.basis==NULL||fixture.strike_controls==NULL||fixture.consumer_context==NULL||
       fixture.accumulator==NULL||fixture.output==NULL||fixture.state==NULL||
       fixture.strip_context==NULL||fixture.strip_output==NULL)return -1;
    for(uint32_t i=0;i<8192u;++i)fixture.pressure[i]=i;
    for(uint32_t path=0;path<PATHS;++path){
        fixture.words[0][path]=sobol_word(8192u+path,fixture.directions[0]);
        fixture.words[1][path]=sobol_word(12288u+path,fixture.directions[0]);
    }
    const double dt=1.0/(double)n;
    const float drift=(float)((.03-.5*.20*.20)*dt);
    const float diffusion=(float)(.20*sqrt(dt));
    const uint32_t producer_n=asian_genuine_msfr_producer_fixing_count(n);
    if(producer_n==0u||ordered_d1_diag_prepare(fixture.x3,drift,diffusion,8192u,
       ORDERED_D1_DIAG_PREPARE_X3,producer_n)!=0)return -1;
    asian_genuine_fixed_block_source_request_t request;memset(&request,0,sizeof(request));
    request.target_start_index=8192u;request.path_count=PATHS;request.block_count=1u;
    request.fixing_count=n;request.s0=100.0;request.rate=.03;request.dividend_yield=0.0;
    request.sigma=.20;request.maturity=1.0;request.signed_z=asian_genuine_fixed_block_signed_z;
    request.signed_z_bytes=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES;
    if(asian_genuine_fixed_block_source_prepare(fixture.fixed,&request)!=0||
       asian_genuine_fixed_block_exact_x_prepare(fixture.exact,fixture.fixed,
       fixture.exact_x,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES)!=0)return -1;
    ordered_d1_x_only_diag(256u,fixture.x3,fixture.x);
    asian_vector_exp_range_reduced_array_diag(fixture.x,fixture.growth);
    asian_vector_exp_range_reduced_array_diag(fixture.x+PATHS,fixture.growth+PATHS);
    const uint32_t *source_words[2]={fixture.words[0],fixture.words[1]};
    const float *source_x[2]={fixture.x,fixture.x+PATHS};
    const float *source_growth[2]={fixture.growth,fixture.growth+PATHS};
    uint32_t *target=aligned64(PATHS*4u);if(target==NULL)return -1;
    for(uint32_t dimension=0;dimension<n;++dimension){
        for(uint32_t path=0;path<PATHS;++path)
            target[path]=sobol_word(8192u+path,fixture.directions[dimension]);
        if(asian_genuine_prepare_route(source_words,2u,source_x,source_growth,target,
           dimension,n,&fixture.maps[dimension],&fixture.routes[dimension])!=0){free(target);return -1;}
    }
    free(target);float strikes[32];if(asian_genuine_strip_fixed_strikes(k,strikes)!=0)return -1;
    if(asian_genuine_msfr_prepare_basis_controls(fixture.basis_controls,100,.03,0,.20,1,n)!=0||
       asian_genuine_msfr_prepare_basis_context(fixture.basis_context,fixture.routes,
       fixture.basis_controls,100,.03,0,.20,1,n)!=0||
       asian_genuine_msfr_prepare_strikes(fixture.strike_controls,100,.03,0,.20,1,n,strikes,k)!=0||
       asian_genuine_msfr_prepare_consumer_context(fixture.consumer_context,
       fixture.strike_controls)!=0||
       asian_genuine_strip_prepare(fixture.strip_context,100,.03,0,.20,1,n,0,0,0,strikes,k)!=0)
        return -1;
    return 0;
}

static void reset_pipeline(void)
{
    memset(fixture.output,0,sizeof(*fixture.output));
    memset(fixture.strip_output,0,sizeof(*fixture.strip_output));
    memset(fixture.basis,0,sizeof(*fixture.basis));
    memset(fixture.accumulator,0,sizeof(*fixture.accumulator));
    initialize_state();
}

__attribute__((noinline)) void asian_fixed_e2e_source_x3(void)
{
    if(active_counts!=NULL)++active_counts->source;
    ordered_d1_x_only_diag(256u,fixture.x3,fixture.x);
}

__attribute__((noinline)) void asian_fixed_e2e_source_fixed(void)
{
    if(active_counts!=NULL)++active_counts->source;
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(fixture.fixed,fixture.x);
}

__attribute__((noinline)) void asian_fixed_e2e_source_exact(void)
{
    if(active_counts!=NULL)++active_counts->source;
    asian_genuine_fixed_block_prepared_exact_x_lookup_diag(fixture.exact,fixture.x);
}

__attribute__((noinline)) void asian_fixed_e2e_vector_exp(void)
{
    if(active_counts!=NULL)active_counts->vector_exp+=2u;
    asian_vector_exp_range_reduced_array_diag(fixture.x,fixture.growth);
    asian_vector_exp_range_reduced_array_diag(fixture.x+PATHS,fixture.growth+PATHS);
}

__attribute__((noinline)) void asian_fixed_e2e_route_evolution(enum workload_kind workload)
{
    if(active_counts!=NULL)++active_counts->evolution;
    if(workload==WORKLOAD_FULL_RISK){
        if(active_counts!=NULL)++active_counts->basis_forward;
        asian_genuine_msfr_basis_forward_diag(fixture.basis_context,fixture.basis);
    }else{
        if(active_counts!=NULL)++active_counts->sql_dual_control;
        asian_genuine_sql_dual_control_diag(fixture.routes,fixture.n,fixture.state);
    }
}

static void copy_price_output(uint32_t fields)
{
    for(uint32_t strike=0;strike<fixture.k;++strike){
        fixture.output->values[strike].call.price=fixture.strip_output->values[strike].call_price;
        fixture.output->values[strike].put.price=fixture.strip_output->values[strike].put_price;
        if(fields==2u){
            fixture.output->values[strike].call.delta=fixture.strip_output->values[strike].call_delta;
            fixture.output->values[strike].put.delta=fixture.strip_output->values[strike].put_delta;
        }
    }
}

__attribute__((noinline)) void asian_fixed_e2e_consumer(
    enum workload_kind workload,int estimator,uint32_t fields)
{
    if(active_counts!=NULL)++active_counts->consumer_api;
    if(workload==WORKLOAD_FULL_RISK){
        if(active_counts!=NULL){
            ++active_counts->accumulator_init;++active_counts->consume_block;
            ++active_counts->finalize;active_counts->tile4_leaf+=
                fixture.consumer_context->padded_count_tile4/4u;
        }
        if(asian_genuine_msfr_accumulator_init(fixture.accumulator,fixture.consumer_context,
           (enum asian_genuine_msfr_estimator)estimator)!=0||
           asian_genuine_msfr_consume_block(fixture.basis,fixture.consumer_context,
           (enum asian_genuine_msfr_estimator)estimator,4u,fixture.accumulator)!=0||
           asian_genuine_msfr_finalize(fixture.consumer_context,fixture.accumulator,
           fixture.output)!=0)abort();
    }else{
        if(active_counts!=NULL)++active_counts->strip_leaf;
        if(estimator==ASIAN_GENUINE_STRIP_GEOMETRIC_CV){
            if(active_counts!=NULL)++active_counts->l_to_g;
            asian_genuine_strip_l_to_g_diag(fixture.state->l,fixture.strip_context,fixture.g);
        }
        const int status=workload==WORKLOAD_PRICE?
            asian_genuine_strip_price_diag(fixture.state->q,fixture.g,fixture.strip_context,
              (enum asian_genuine_strip_estimator)estimator,4u,fixture.strip_output):
            asian_genuine_strip_price_delta_diag(fixture.state->q,fixture.g,fixture.strip_context,
              (enum asian_genuine_strip_estimator)estimator,4u,fixture.strip_output);
        if(status!=0)abort();
        copy_price_output(fields);
    }
}

__attribute__((noinline)) void asian_fixed_e2e_post_source(
    enum workload_kind workload,int estimator,uint32_t fields)
{
    asian_fixed_e2e_vector_exp();
    asian_fixed_e2e_route_evolution(workload);
    asian_fixed_e2e_consumer(workload,estimator,fields);
}

__attribute__((noinline)) void asian_fixed_e2e_run_complete(
    enum source_candidate source,enum workload_kind workload,int estimator,uint32_t fields)
{
    if(source==SOURCE_X3)asian_fixed_e2e_source_x3();
    else if(source==SOURCE_FIXED)asian_fixed_e2e_source_fixed();
    else asian_fixed_e2e_source_exact();
    asian_fixed_e2e_post_source(workload,estimator,fields);
}

static double output_checksum(uint32_t fields)
{
    double total=0.0;
    for(uint32_t strike=0;strike<fixture.k;++strike){
        const double *call=(const double *)&fixture.output->values[strike].call;
        const double *put=(const double *)&fixture.output->values[strike].put;
        for(uint32_t field=0;field<fields;++field)total+=call[field]+put[field];
    }
    return total;
}

static void consume_checksum(uint32_t fields)
{
    sink+=output_checksum(fields)+fixture.x[17]+fixture.growth[37];
}

static uint64_t touch_region(const void *data,size_t bytes)
{
    const unsigned char *region=data;uint64_t value=0u;
    for(size_t offset=0;offset<bytes;offset+=64u)value+=region[offset];
    return value;
}

static void condition_candidate(enum source_candidate candidate,
                                enum workload_kind workload,int cache_mode)
{
    uint64_t value=1u;
    if(cache_mode!=0){
        for(uint32_t i=0;i<8192u;++i){fixture.pressure[i]+=i+3u;value+=fixture.pressure[i];}
    }else{
        if(candidate==SOURCE_X3)value+=touch_region(fixture.x3,sizeof(*fixture.x3));
        else if(candidate==SOURCE_FIXED)value+=touch_region(fixture.fixed->signed_z,32768u);
        else value+=touch_region(fixture.exact_x,32768u);
        value+=touch_region(fixture.x,32768u);
        value+=touch_region(fixture.growth,32768u);
        value+=touch_region(fixture.routes,(size_t)fixture.n*sizeof(*fixture.routes));
        value+=touch_region(fixture.output,sizeof(*fixture.output));
        if(workload==WORKLOAD_FULL_RISK){
            value+=touch_region(fixture.basis_controls,sizeof(*fixture.basis_controls));
            value+=touch_region(fixture.basis_context,sizeof(*fixture.basis_context));
            value+=touch_region(fixture.basis,sizeof(*fixture.basis));
            value+=touch_region(fixture.strike_controls,sizeof(*fixture.strike_controls));
            value+=touch_region(fixture.consumer_context,sizeof(*fixture.consumer_context));
            value+=touch_region(fixture.accumulator,sizeof(*fixture.accumulator));
        }else{
            value+=touch_region(fixture.state,sizeof(*fixture.state));
            value+=touch_region(fixture.g,sizeof(*fixture.g));
            value+=touch_region(fixture.strip_context,sizeof(*fixture.strip_context));
            value+=touch_region(fixture.strip_output,sizeof(*fixture.strip_output));
        }
    }
    sink+=(double)value;
}

static duration_t time_complete(enum source_candidate candidate,
                                const workload_spec_t *workload,int estimator,
                                int cache_mode)
{
    reset_pipeline();condition_candidate(candidate,workload->kind,cache_mode);
    const uint64_t wall0=wall_now(),tsc0=tsc_begin();
    asian_fixed_e2e_run_complete(candidate,workload->kind,estimator,workload->fields);
    const uint64_t tsc1=tsc_end(),wall1=wall_now();
    consume_checksum(workload->fields);
    duration_t result={tsc1-tsc0,wall1-wall0};return result;
}

static void time_profile(enum source_candidate candidate,
                         const workload_spec_t *workload,int estimator,
                         int cache_mode,duration_t result[STAGES])
{
    reset_pipeline();condition_candidate(candidate,workload->kind,cache_mode);
    uint64_t w0=wall_now(),t0=tsc_begin();
    if(candidate==SOURCE_X3)asian_fixed_e2e_source_x3();
    else if(candidate==SOURCE_FIXED)asian_fixed_e2e_source_fixed();
    else asian_fixed_e2e_source_exact();
    uint64_t t1=tsc_end(),w1=wall_now();result[STAGE_SOURCE]=(duration_t){t1-t0,w1-w0};
    w0=wall_now();t0=tsc_begin();asian_fixed_e2e_vector_exp();t1=tsc_end();w1=wall_now();
    result[STAGE_VECTOR_EXP]=(duration_t){t1-t0,w1-w0};
    w0=wall_now();t0=tsc_begin();asian_fixed_e2e_route_evolution(workload->kind);
    t1=tsc_end();w1=wall_now();result[STAGE_EVOLUTION]=(duration_t){t1-t0,w1-w0};
    w0=wall_now();t0=tsc_begin();asian_fixed_e2e_consumer(workload->kind,estimator,workload->fields);
    t1=tsc_end();w1=wall_now();result[STAGE_CONSUMER]=(duration_t){t1-t0,w1-w0};
    consume_checksum(workload->fields);
}

static duration_t average_duration(duration_t left,duration_t right)
{
    duration_t result={(left.tsc+right.tsc)/2u,(left.wall+right.wall)/2u};return result;
}

static void run_complete_quartet(enum source_candidate a,enum source_candidate b,
                                 const workload_spec_t *workload,int estimator,
                                 int cache_mode,uint32_t quartet,duration_t result[2])
{
    const enum source_candidate abba[4]={a,b,b,a};
    const enum source_candidate baab[4]={b,a,a,b};
    const enum source_candidate *order=(quartet&1u)==0u?abba:baab;
    duration_t samples[2][2];uint32_t used[2]={0,0};
    for(uint32_t position=0;position<4u;++position){
        const uint32_t side=order[position]==a?0u:1u;
        samples[side][used[side]++]=time_complete(order[position],workload,estimator,cache_mode);
    }
    result[0]=average_duration(samples[0][0],samples[0][1]);
    result[1]=average_duration(samples[1][0],samples[1][1]);
}

static void run_profile_quartet(enum source_candidate a,enum source_candidate b,
                                const workload_spec_t *workload,int estimator,
                                int cache_mode,uint32_t quartet,
                                duration_t result[2][STAGES])
{
    const enum source_candidate abba[4]={a,b,b,a};
    const enum source_candidate baab[4]={b,a,a,b};
    const enum source_candidate *order=(quartet&1u)==0u?abba:baab;
    duration_t samples[2][2][STAGES];uint32_t used[2]={0,0};
    for(uint32_t position=0;position<4u;++position){
        const uint32_t side=order[position]==a?0u:1u;
        time_profile(order[position],workload,estimator,cache_mode,samples[side][used[side]++]);
    }
    for(uint32_t side=0;side<2u;++side)for(uint32_t stage=0;stage<STAGES;++stage)
        result[side][stage]=average_duration(samples[side][0][stage],samples[side][1][stage]);
}

static void measure_pair_complete(enum source_candidate a,enum source_candidate b,
                                  const workload_spec_t *workload,int estimator,
                                  int cache_mode,pair_complete_t *result)
{
    duration_t ignored[2];
    for(uint32_t quartet=0;quartet<active_warmup_quartets;++quartet)
        run_complete_quartet(a,b,workload,estimator,cache_mode,quartet,ignored);
    for(uint32_t quartet=0;quartet<active_measured_quartets;++quartet){duration_t measured[2];
        run_complete_quartet(a,b,workload,estimator,cache_mode,quartet,measured);
        result->candidate[0][quartet]=measured[0];result->candidate[1][quartet]=measured[1];}
}

static void measure_pair_profile(enum source_candidate a,enum source_candidate b,
                                 const workload_spec_t *workload,int estimator,
                                 int cache_mode,pair_profile_t *result)
{
    duration_t ignored[2][STAGES];
    for(uint32_t quartet=0;quartet<active_warmup_quartets;++quartet)
        run_profile_quartet(a,b,workload,estimator,cache_mode,quartet,ignored);
    for(uint32_t quartet=0;quartet<active_measured_quartets;++quartet){duration_t measured[2][STAGES];
        run_profile_quartet(a,b,workload,estimator,cache_mode,quartet,measured);
        for(uint32_t side=0;side<2u;++side)for(uint32_t stage=0;stage<STAGES;++stage)
            result->candidate[side][stage][quartet]=measured[side][stage];}
}

static double maximum_output_difference(const asian_genuine_msfr_output_t *left,
                                        const asian_genuine_msfr_output_t *right,
                                        uint32_t k,uint32_t fields)
{
    double maximum=0.0;
    for(uint32_t strike=0;strike<k;++strike){
        const double *a=(const double *)&left->values[strike];
        const double *b=(const double *)&right->values[strike];
        for(uint32_t side=0;side<2u;++side)for(uint32_t field=0;field<fields;++field){
            const double difference=fabs(a[side*4u+field]-b[side*4u+field]);
            if(difference>maximum)maximum=difference;}}
    return maximum;
}

static uint64_t bytes_hash(const void *data,size_t bytes)
{
    const unsigned char *input=data;uint64_t hash=UINT64_C(1469598103934665603);
    while(bytes--!=0u){hash^=*input++;hash*=UINT64_C(1099511628211);}return hash;
}

static int validate_cell(const workload_spec_t *workload,int estimator,
                         asian_genuine_msfr_output_t outputs[SOURCE_CANDIDATES],
                         double errors[SOURCE_CANDIDATES],
                         invocation_counts_t counts[SOURCE_CANDIDATES])
{
    const uint64_t route_hash=bytes_hash(fixture.routes,(size_t)fixture.n*sizeof(*fixture.routes));
    const uint64_t table_hash=bytes_hash(asian_genuine_fixed_block_signed_z,
                                         ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    const uint64_t x3_hash=bytes_hash(fixture.x3,sizeof(*fixture.x3));
    const uint64_t fixed_context_hash=bytes_hash(fixture.fixed,sizeof(*fixture.fixed));
    const uint64_t exact_context_hash=bytes_hash(fixture.exact,sizeof(*fixture.exact));
    const uint64_t exact_x_hash=bytes_hash(fixture.exact_x,
                                          ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    const uint64_t basis_controls_hash=bytes_hash(fixture.basis_controls,
                                                  sizeof(*fixture.basis_controls));
    const uint64_t basis_context_hash=bytes_hash(fixture.basis_context,
                                                 sizeof(*fixture.basis_context));
    const uint64_t strike_controls_hash=bytes_hash(fixture.strike_controls,
                                                   sizeof(*fixture.strike_controls));
    const uint64_t consumer_context_hash=bytes_hash(fixture.consumer_context,
                                                    sizeof(*fixture.consumer_context));
    const uint64_t strip_context_hash=bytes_hash(fixture.strip_context,
                                                 sizeof(*fixture.strip_context));
    for(uint32_t candidate=0;candidate<SOURCE_CANDIDATES;++candidate){
        reset_pipeline();memset(&counts[candidate],0,sizeof(counts[candidate]));
        active_counts=&counts[candidate];
        asian_fixed_e2e_run_complete((enum source_candidate)candidate,workload->kind,
                                     estimator,workload->fields);
        active_counts=NULL;memcpy(&outputs[candidate],fixture.output,sizeof(*fixture.output));
        const uint32_t expected_tile4=workload->kind==WORKLOAD_FULL_RISK?
            fixture.consumer_context->padded_count_tile4/4u:1u;
        if(counts[candidate].source!=1u||counts[candidate].vector_exp!=2u||
           counts[candidate].evolution!=1u||counts[candidate].consumer_api!=1u||
           counts[candidate].sql_dual_control!=(workload->kind!=WORKLOAD_FULL_RISK)||
           counts[candidate].basis_forward!=(workload->kind==WORKLOAD_FULL_RISK)||
           counts[candidate].l_to_g!=(workload->kind!=WORKLOAD_FULL_RISK&&
             estimator==ASIAN_GENUINE_STRIP_GEOMETRIC_CV)||
           counts[candidate].strip_leaf!=(workload->kind!=WORKLOAD_FULL_RISK)||
           counts[candidate].accumulator_init!=(workload->kind==WORKLOAD_FULL_RISK)||
           counts[candidate].consume_block!=(workload->kind==WORKLOAD_FULL_RISK)||
           counts[candidate].finalize!=(workload->kind==WORKLOAD_FULL_RISK)||
           counts[candidate].tile4_leaf!=(workload->kind==WORKLOAD_FULL_RISK?
             expected_tile4:0u))return -1;
    }
    for(uint32_t candidate=0;candidate<SOURCE_CANDIDATES;++candidate)
        errors[candidate]=maximum_output_difference(&outputs[SOURCE_X3],
            &outputs[candidate],fixture.k,workload->fields);
    if(memcmp(&outputs[SOURCE_FIXED],&outputs[SOURCE_EXACT],sizeof(outputs[0]))!=0||
       errors[SOURCE_FIXED]>1e-3||
       bytes_hash(fixture.routes,(size_t)fixture.n*sizeof(*fixture.routes))!=route_hash||
       bytes_hash(asian_genuine_fixed_block_signed_z,
                  ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES)!=table_hash||
       bytes_hash(fixture.x3,sizeof(*fixture.x3))!=x3_hash||
       bytes_hash(fixture.fixed,sizeof(*fixture.fixed))!=fixed_context_hash||
       bytes_hash(fixture.exact,sizeof(*fixture.exact))!=exact_context_hash||
       bytes_hash(fixture.exact_x,ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES)!=exact_x_hash||
       bytes_hash(fixture.basis_controls,sizeof(*fixture.basis_controls))!=basis_controls_hash||
       bytes_hash(fixture.basis_context,sizeof(*fixture.basis_context))!=basis_context_hash||
       bytes_hash(fixture.strike_controls,sizeof(*fixture.strike_controls))!=strike_controls_hash||
       bytes_hash(fixture.consumer_context,sizeof(*fixture.consumer_context))!=consumer_context_hash||
       bytes_hash(fixture.strip_context,sizeof(*fixture.strip_context))!=strip_context_hash)return -1;
    return 0;
}

static const char *find_key(const char *begin,const char *end,const char *key)
{
    char needle[160];snprintf(needle,sizeof(needle),"\"%s\"",key);
    const size_t length=strlen(needle);const char *cursor=begin;
    while(cursor!=NULL&&cursor<end){cursor=strstr(cursor,needle);
        if(cursor!=NULL&&cursor<end&&cursor+length<=end)return cursor+length;
        if(cursor!=NULL)cursor+=length;}
    return NULL;
}

static int object_string(const char *begin,const char *end,const char *key,
                         char *output,size_t output_bytes)
{
    const char *cursor=find_key(begin,end,key);if(cursor==NULL)return -1;
    cursor=strchr(cursor,':');if(cursor==NULL||cursor>=end)return -1;++cursor;
    while(cursor<end&&(*cursor==' '||*cursor=='\n'||*cursor=='\r'||*cursor=='\t'))++cursor;
    if(cursor>=end||*cursor!='\"')return -1;
    ++cursor;const char *finish=cursor;
    while(finish<end&&*finish!='\"')++finish;
    if(finish>=end)return -1;
    size_t length=(size_t)(finish-cursor);if(length>=output_bytes)length=output_bytes-1u;
    memcpy(output,cursor,length);output[length]=0;return 0;
}

static int object_number(const char *begin,const char *end,const char *key,double *output)
{
    const char *cursor=find_key(begin,end,key);if(cursor==NULL)return -1;
    cursor=strchr(cursor,':');if(cursor==NULL||cursor>=end)return -1;++cursor;
    char *finish;errno=0;const double value=strtod(cursor,&finish);
    if(finish==cursor||finish>end||errno!=0)return -1;
    *output=value;return 0;
}

static char *load_file(const char *path,size_t *bytes)
{
    FILE *file=fopen(path,"rb");if(file==NULL)return NULL;
    if(fseek(file,0,SEEK_END)!=0){fclose(file);return NULL;}long length=ftell(file);
    if(length<0||length>268435456L||fseek(file,0,SEEK_SET)!=0){fclose(file);return NULL;}
    char *data=malloc((size_t)length+1u);if(data==NULL){fclose(file);return NULL;}
    if(fread(data,1u,(size_t)length,file)!=(size_t)length){free(data);fclose(file);return NULL;}
    fclose(file);data[length]=0;*bytes=(size_t)length;return data;
}

static const char *containing_object_begin(const char *start,const char *position)
{
    while(position>start){--position;if(*position=='{')return position;}return NULL;
}

static const char *object_end(const char *position,const char *end)
{
    uint32_t depth=0;int string=0,escape=0;
    for(;position<end;++position){const char character=*position;
        if(string){if(escape)escape=0;else if(character=='\\')escape=1;else if(character=='\"')string=0;continue;}
        if(character=='\"'){string=1;continue;}if(character=='{')++depth;
        else if(character=='}'&&--depth==0u)return position+1;}
    return NULL;
}

static void load_historical_reference(const char *path,const char *cpu_model,
                                      const char *kernel_release,int physical_cpu,
                                      historical_reference_t *reference)
{
    memset(reference,0,sizeof(*reference));
    if(path==NULL){strcpy(reference->reason,"historical_path_not_supplied");return;}
    reference->requested=1;snprintf(reference->path,sizeof(reference->path),"%s",path);
    size_t bytes=0;char *data=load_file(path,&bytes);if(data==NULL){
        snprintf(reference->reason,sizeof(reference->reason),"historical_file_unreadable");return;}
    reference->loaded=1;if(file_sha256(path,reference->sha256)!=0)strcpy(reference->sha256,"unavailable");
    const char *layer=getenv("MKL_THREADING_LAYER");
    const char *thread_setting=getenv("MKL_NUM_THREADS");
    const char *dynamic=getenv("MKL_DYNAMIC");
    if(layer==NULL||thread_setting==NULL||dynamic==NULL||strcmp(layer,"SEQUENTIAL")!=0||
       strcmp(thread_setting,"1")!=0||strcmp(dynamic,"FALSE")!=0){
        strcpy(reference->reason,"current_execution_environment_mismatch");free(data);return;}
    const char *end=data+bytes;double number;
    if(object_string(data,end,"git_commit",reference->commit,sizeof(reference->commit))!=0||
       object_string(data,end,"binary_sha256",reference->binary_sha256,sizeof(reference->binary_sha256))!=0||
       object_string(data,end,"measurement_date_utc",reference->measurement_date,sizeof(reference->measurement_date))!=0||
       object_string(data,end,"cpu_model",reference->cpu_model,sizeof(reference->cpu_model))!=0||
       object_string(data,end,"kernel_release",reference->kernel_release,sizeof(reference->kernel_release))!=0){
        strcpy(reference->reason,"historical_provenance_or_environment_incomplete");free(data);return;}
    if(strcmp(reference->cpu_model,cpu_model)!=0||strcmp(reference->kernel_release,kernel_release)!=0||
       object_number(data,end,"physical_cpu",&number)!=0||(int)number!=physical_cpu){
        strcpy(reference->reason,"historical_execution_environment_mismatch");free(data);return;}
    double s0,r,q,sigma,maturity,paths,threads,gaussians,evolutions;
    if(object_number(data,end,"S0",&s0)!=0||object_number(data,end,"r",&r)!=0||
       object_number(data,end,"q",&q)!=0||object_number(data,end,"sigma",&sigma)!=0||
       object_number(data,end,"T",&maturity)!=0||object_number(data,end,"paths",&paths)!=0||
       object_number(data,end,"thread_count",&threads)!=0||
       s0!=100.0||r!=.03||q!=0.0||sigma!=.20||maturity!=1.0||paths!=4096.0||
       threads!=1.0){
        strcpy(reference->reason,"historical_contract_or_source_semantics_mismatch");free(data);return;}
    const char *semantics=strstr(data,"\"onemkl_forward_basis\"");
    const char *semantics_begin=semantics==NULL?NULL:strchr(semantics,'{');
    const char *semantics_end=semantics_begin==NULL?NULL:object_end(semantics_begin,end);
    if(semantics_begin==NULL||semantics_end==NULL||
       object_number(semantics_begin,semantics_end,"gaussian_matrices",&gaussians)!=0||
       object_number(semantics_begin,semantics_end,"evolutions",&evolutions)!=0||
       gaussians!=1.0||evolutions!=1.0){
        strcpy(reference->reason,"historical_contract_or_source_semantics_mismatch");free(data);return;}
    if(strstr(data,"\"mkl_threading_layer\":\"SEQUENTIAL\"")==NULL||
       strstr(data,"\"mkl_num_threads\":1")==NULL||
       strstr(data,"\"mkl_dynamic\":false")==NULL||
       strstr(data,"\"timer\":\"fenced_TSC_and_CLOCK_MONOTONIC_RAW\"")==NULL||
       strstr(data,"\"tsc_units\":\"not_CPU_cycles\"")==NULL||
       strstr(data,"\"strike_contract_id\":\"asian_qualified_nested_strikes_v1\"")==NULL){
        strcpy(reference->reason,"historical_threading_environment_mismatch");free(data);return;}
    const char *candidate="\"candidate\":\"onemkl_gaussian_plus_matched_forward_basis\"";
    const char *cursor=data;
    while((cursor=strstr(cursor,candidate))!=NULL&&reference->row_count<256u){
        const char *begin=containing_object_begin(data,cursor);const char *finish=begin?object_end(begin,end):NULL;
        if(begin==NULL||finish==NULL)break;
        historical_row_t row;memset(&row,0,sizeof(row));
        char estimator[32],cache[64],classification[64];double n,k,tsc,wall;
        if(object_number(begin,finish,"N",&n)==0&&object_number(begin,finish,"K",&k)==0&&
           object_number(begin,finish,"tsc_median",&tsc)==0&&
           object_number(begin,finish,"wall_ns_median",&wall)==0&&
           object_string(begin,finish,"estimator",estimator,sizeof(estimator))==0&&
           object_string(begin,finish,"cache_mode",cache,sizeof(cache))==0&&
           object_string(begin,finish,"classification",classification,sizeof(classification))==0&&
           strcmp(classification,"complete_two_sided_full_risk")==0){
            row.n=(uint32_t)n;row.k=(uint32_t)k;
            row.estimator=strcmp(estimator,"geometric_cv")==0?1:0;
            row.cache_mode=strcmp(cache,"historical_32KiB_rmw")==0?1:0;
            row.tsc_median=(uint64_t)tsc;row.wall_median=(uint64_t)wall;
            reference->rows[reference->row_count++]=row;}
        cursor=finish;
    }
    if(reference->row_count==0u)strcpy(reference->reason,"no_exact_matched_forward_full_risk_rows");
    else {reference->compatible=1;strcpy(reference->reason,"compatible_frozen_cross_run_reference");}
    free(data);
}

static const historical_row_t *historical_row(const historical_reference_t *reference,
                                               const workload_spec_t *workload,
                                               uint32_t n,int estimator,int cache_mode)
{
    if(!reference->compatible||workload->kind!=WORKLOAD_FULL_RISK)return NULL;
    for(uint32_t row=0;row<reference->row_count;++row)
        if(reference->rows[row].n==n&&reference->rows[row].k==workload->strikes&&
           reference->rows[row].estimator==estimator&&
           reference->rows[row].cache_mode==cache_mode)return &reference->rows[row];
    return NULL;
}

static int atomic_json_open(atomic_json_t *json,const char *output)
{
    if(access(output,F_OK)==0){errno=EEXIST;return -1;}memset(json,0,sizeof(*json));
    const size_t bytes=strlen(output)+16u;json->temporary=malloc(bytes);if(json->temporary==NULL)return -1;
    snprintf(json->temporary,bytes,"%s.tmp.XXXXXX",output);const int fd=mkstemp(json->temporary);
    if(fd<0)return -1;
    json->file=fdopen(fd,"w");if(json->file==NULL){close(fd);unlink(json->temporary);return -1;}
    return 0;
}

static void atomic_json_abort(atomic_json_t *json)
{
    if(json->file!=NULL)fclose(json->file);
    if(json->temporary!=NULL){unlink(json->temporary);free(json->temporary);}
    memset(json,0,sizeof(*json));
}

static int publish_no_replace(const char *temporary,const char *output)
{
#ifdef SYS_renameat2
    return syscall(SYS_renameat2,AT_FDCWD,temporary,AT_FDCWD,output,1u);
#else
    if(link(temporary,output)!=0)return -1;return unlink(temporary);
#endif
}

static int atomic_json_commit(atomic_json_t *json,const char *output)
{
    int error=0;if(ferror(json->file)||fflush(json->file)||fsync(fileno(json->file)))error=errno?errno:EIO;
    if(fclose(json->file)!=0&&error==0)error=errno;
    json->file=NULL;
    if(error==0&&publish_no_replace(json->temporary,output)!=0)error=errno;
    if(error!=0)unlink(json->temporary);
    free(json->temporary);json->temporary=NULL;errno=error;
    return error==0?0:-1;
}

static void write_failure(const char *output,const char *stage)
{
    char path[4096],temporary[4128];const char *dot=strrchr(output,'.');
    const size_t stem=dot!=NULL&&strcmp(dot,".json")==0?(size_t)(dot-output):strlen(output);
    snprintf(path,sizeof(path),"%.*s.failure.json",(int)stem,output);
    if(access(path,F_OK)==0)return;
    snprintf(temporary,sizeof(temporary),"%s.tmp.XXXXXX",path);
    const int fd=mkstemp(temporary);if(fd<0)return;FILE *file=fdopen(fd,"w");
    if(file==NULL){close(fd);unlink(temporary);return;}
    fprintf(file,"{\"status\":\"FAIL\",\"stage\":\"%s\",\"git_commit\":\"%s\"}\n",
            stage,FIXED_E2E_GIT_COMMIT);
    const int failed=ferror(file)||fflush(file)||fsync(fd)||fclose(file);
    if(failed||publish_no_replace(temporary,path)!=0)unlink(temporary);
}

static void extract_clock(const pair_complete_t *pair,uint32_t side,int wall,
                          uint64_t output[MEASURED_QUARTETS])
{
    for(uint32_t i=0;i<active_measured_quartets;++i)
        output[i]=wall?pair->candidate[side][i].wall:pair->candidate[side][i].tsc;
}

static void paired_ratios(const pair_complete_t *pair,int wall,
                          double output[MEASURED_QUARTETS])
{
    for(uint32_t i=0;i<active_measured_quartets;++i){
        const uint64_t a=wall?pair->candidate[0][i].wall:pair->candidate[0][i].tsc;
        const uint64_t b=wall?pair->candidate[1][i].wall:pair->candidate[1][i].tsc;
        output[i]=(double)b/(double)a;
    }
}

static double mean_log_ratio(const pair_complete_t *pair,int wall)
{
    double total=0.0;for(uint32_t i=0;i<active_measured_quartets;++i){
        const double a=(double)(wall?pair->candidate[0][i].wall:pair->candidate[0][i].tsc);
        const double b=(double)(wall?pair->candidate[1][i].wall:pair->candidate[1][i].tsc);
        total+=log(b/a);}return total/active_measured_quartets;
}

static void emit_duration_array(FILE *json,const duration_t values[MEASURED_QUARTETS],int wall)
{
    for(uint32_t i=0;i<active_measured_quartets;++i)
        fprintf(json,"%s%"PRIu64,i?",":"",wall?values[i].wall:values[i].tsc);
}

static void emit_output(FILE *json,const asian_genuine_msfr_output_t *output,
                        uint32_t k,uint32_t fields)
{
    fputc('[',json);for(uint32_t strike=0;strike<k;++strike){
        const double *call=(const double *)&output->values[strike].call;
        const double *put=(const double *)&output->values[strike].put;
        fprintf(json,"%s{\"strike_index\":%u,\"call\":[",strike?",":"",strike);
        for(uint32_t field=0;field<fields;++field)fprintf(json,"%s%.17g",field?",":"",call[field]);
        fputs("],\"put\":[",json);
        for(uint32_t field=0;field<fields;++field)
            fprintf(json,"%s%.17g",field?",":"",put[field]);
        fputs("]}",json);
    }
    fputc(']',json);
}

static void emit_candidate_metrics(FILE *json,const char *name,
                                   const pair_complete_t *pair,uint32_t side,
                                   const pair_profile_t *profile,uint32_t profile_side,
                                   uint32_t k,uint32_t fields)
{
    uint64_t tsc[MEASURED_QUARTETS],wall[MEASURED_QUARTETS];
    extract_clock(pair,side,0,tsc);extract_clock(pair,side,1,wall);
    const uint64_t tsc_median=quantile_u64(tsc,50u),wall_median=quantile_u64(wall,50u);
    fprintf(json,"\"%s\":{\"tsc_p10\":%"PRIu64",\"tsc_median\":%"PRIu64
      ",\"tsc_p90\":%"PRIu64",\"wall_ns_p10\":%"PRIu64
      ",\"wall_ns_median\":%"PRIu64",\"wall_ns_p90\":%"PRIu64
      ",\"wall_ns_per_path\":%.9g,\"paths_per_second\":%.9g,"
      "\"wall_ns_per_strike\":%.9g,\"strikes_per_second\":%.9g,"
      "\"complete_valuations_per_second\":%.9g,"
      "\"complete_risk_sets_per_second\":",name,quantile_u64(tsc,10u),
      tsc_median,quantile_u64(tsc,90u),quantile_u64(wall,10u),wall_median,
      quantile_u64(wall,90u),(double)wall_median/PATHS,
      (double)PATHS*1e9/(double)wall_median,(double)wall_median/k,
      (double)k*1e9/(double)wall_median,1e9/(double)wall_median);
    if(fields==4u)fprintf(json,"%.9g",(double)k*1e9/(double)wall_median);
    else fputs("null",json);
    fprintf(json,",\"fields_per_side\":%u,\"components\":{",fields);
    uint64_t component_wall_medians[STAGES];
    for(uint32_t stage=0;stage<STAGES;++stage){uint64_t st[MEASURED_QUARTETS],sw[MEASURED_QUARTETS];
        for(uint32_t i=0;i<active_measured_quartets;++i){st[i]=profile->candidate[profile_side][stage][i].tsc;
            sw[i]=profile->candidate[profile_side][stage][i].wall;}
        component_wall_medians[stage]=quantile_u64(sw,50u);
        fprintf(json,"%s\"%s\":{\"tsc_median\":%"PRIu64",\"wall_ns_median\":%"PRIu64"}",
                stage?",":"",stage_names[stage],quantile_u64(st,50u),component_wall_medians[stage]);}
    uint64_t source_wall[MEASURED_QUARTETS];for(uint32_t i=0;i<active_measured_quartets;++i)
        source_wall[i]=profile->candidate[profile_side][STAGE_SOURCE][i].wall;
    uint32_t dominant=0;for(uint32_t stage=1;stage<STAGES;++stage)
        if(component_wall_medians[stage]>component_wall_medians[dominant])dominant=stage;
    fprintf(json,"},\"source_percent_of_complete_wall\":%.9g,"
      "\"dominant_profiled_stage\":\"%s\"}",
      100.0*(double)quantile_u64(source_wall,50u)/(double)wall_median,
      stage_names[dominant]);
}

static void emit_raw_pair(FILE *json,const char *name,const pair_complete_t *pair,
                          const char *a,const char *b)
{
    fprintf(json,"\"%s\":{\"order\":\"alternating_ABBA_BAAB\",\"%s_tsc\":[",name,a);
    emit_duration_array(json,pair->candidate[0],0);fprintf(json,"],\"%s_wall_ns\":[",a);
    emit_duration_array(json,pair->candidate[0],1);fprintf(json,"],\"%s_tsc\":[",b);
    emit_duration_array(json,pair->candidate[1],0);fprintf(json,"],\"%s_wall_ns\":[",b);
    emit_duration_array(json,pair->candidate[1],1);fputs("]}",json);
}

static double global_upper95(const double cell_logs[MAX_GLOBAL_CELLS],uint32_t cells,
                             double *ratio)
{
    double mean=0.0;for(uint32_t i=0;i<cells;++i)mean+=cell_logs[i];mean/=cells;
    if(cells==1u){*ratio=exp(mean);return *ratio;}
    double sum=0.0;for(uint32_t i=0;i<cells;++i){const double d=cell_logs[i]-mean;sum+=d*d;}
    const double standard_error=sqrt(sum/(cells-1u)/(double)cells);
    *ratio=exp(mean);return exp(mean+1.959963984540054*standard_error);
}

static int preflight_case(uint32_t n,const workload_spec_t *workload,int estimator)
{
    if(prepare_fixture(n,workload->strikes)!=0)return -1;
    asian_genuine_msfr_output_t outputs[SOURCE_CANDIDATES];double errors[SOURCE_CANDIDATES];
    invocation_counts_t counts[SOURCE_CANDIDATES];
    const int status=validate_cell(workload,estimator,outputs,errors,counts);
    printf("fixed_e2e_preflight N=%u workload=%s estimator=%s fixed_raw=%.9g exact_raw=%.9g "
           "source=%u exp=%u evolution=%u consumer=%u tile4_leaves=%u status=%s\n",
           n,workload->name,estimator?"geometric_cv":"arithmetic",errors[SOURCE_FIXED],
           errors[SOURCE_EXACT],counts[SOURCE_FIXED].source,counts[SOURCE_FIXED].vector_exp,
           counts[SOURCE_FIXED].evolution,counts[SOURCE_FIXED].consumer_api,
           counts[SOURCE_FIXED].tile4_leaf,status==0?"PASS":"FAIL");
    release_fixture();return status;
}

static int preflight(void)
{
    const uint32_t checks[2]={16u,256u};const uint32_t selected[4]={0u,1u,2u,6u};
    for(uint32_t ni=0;ni<2u;++ni)for(uint32_t wi=0;wi<4u;++wi)for(int estimator=0;estimator<2;++estimator)
        if(preflight_case(checks[ni],&workloads[selected[wi]],estimator)!=0)return -1;
    puts("asian_genuine_fixed_block_e2e benchmark_preflight=PASS sources=3 N=16,256 "
         "price_price_delta_full_risk=yes arithmetic_cv=yes one_source_per_valuation=yes");
    return 0;
}

int main(int argc,char **argv)
{
    const char *output="results/asian_genuine_fixed_block_e2e/aws.json";
    const char *historical_path=NULL;int check_only=0,serialization_check=0;
    for(int i=1;i<argc;++i){
        if(strcmp(argv[i],"--check-only")==0)check_only=1;
        else if(strcmp(argv[i],"--json-self-check")==0)serialization_check=1;
        else if(strcmp(argv[i],"--json")==0&&++i<argc)output=argv[i];
        else if(strcmp(argv[i],"--onemkl-reference")==0&&++i<argc)historical_path=argv[i];
        else return 2;
    }
    if(check_only&&serialization_check)return 2;
    if(serialization_check){active_warmup_quartets=0u;active_measured_quartets=1u;}
    char binary_hash[65]="unavailable",cpu_model[256]="unavailable";
    char joe_kuo_hash[65]="unavailable",signed_z_hash[65]="unavailable";
    struct utsname system_name;memset(&system_name,0,sizeof(system_name));uname(&system_name);
    binary_sha256(binary_hash);read_cpu_model(cpu_model);
    file_sha256("direction_numbers/joe_kuo_6_21201.bin",joe_kuo_hash);
    file_sha256("private/asian_genuine_fixed_block_signed_z.bin",signed_z_hash);
    const int physical_cpu=pin_first_cpu();
    if(physical_cpu<0){write_failure(output,"cpu_affinity");return 2;}
    if(check_only)return preflight()==0?0:2;
    if(access(output,F_OK)==0){write_failure(output,"success_output_exists");
        fprintf(stderr,"refusing to replace existing success JSON: %s\n",output);return 2;}
    historical_reference_t historical;load_historical_reference(historical_path,cpu_model,
        system_name.release,physical_cpu,&historical);
    atomic_json_t publication;if(atomic_json_open(&publication,output)!=0){
        write_failure(output,"success_temp_open");return 2;}
    FILE *json=publication.file;const long status_offset=ftell(json)+11L;
    time_t now=time(NULL);struct tm utc;gmtime_r(&now,&utc);char date[64];
    strftime(date,sizeof(date),"%Y-%m-%dT%H:%M:%SZ",&utc);
    fprintf(json,"{\"status\":\"PEND\",\"benchmark_provenance\":{"
      "\"git_commit\":\"%s\",\"branch\":\"research/asian-fixed-block-end-to-end\","
      "\"binary_sha256\":\"%s\",\"base_commit\":\"9a9b204ed770f4e1ff6edecfdf177676e6a4579c\","
      "\"measurement_date_utc\":\"%s\",\"reused_artifact_hashes\":{"
      "\"joe_kuo_table_sha256\":\"%s\",\"signed_z_table_sha256\":\"%s\"}},"
      "\"environment\":{"
      "\"cpu_model\":\"%s\",\"kernel_release\":\"%s\",\"physical_cpu\":%d,"
      "\"thread_count\":1,\"timer\":\"fenced_TSC_and_CLOCK_MONOTONIC_RAW\","
      "\"tsc_units\":\"not_CPU_cycles\",\"mkl_threading_layer\":\"%s\","
      "\"mkl_num_threads\":\"%s\",\"mkl_dynamic\":\"%s\"},\"contract\":{"
      "\"S0\":100,\"r\":0.03,\"q\":0,\"sigma\":0.20,\"T\":1,\"paths\":4096,"
      "\"target_indices\":\"8192-12287\",\"N\":[16,32,64,128,256],"
      "\"K\":[1,4,8,16,32],"
      "\"strike_contract_id\":\"asian_qualified_nested_strikes_v1\","
      "\"strikes\":{\"K1\":[100],\"K4\":[80,100,100.5,120],"
      "\"K8\":[70,80,90,100,100.5,110,120,130],"
      "\"K16\":[70,76,80,84,90,94,98,100,100.5,102,106,110,116,120,124,130],"
      "\"K32\":[70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,100.5,"
      "102,104,106,108,110,112,114,116,118,120,122,124,126,128,130]}},"
      "\"protocol\":{\"configured_warmup_quartets\":16,"
      "\"configured_measured_quartets\":201,\"executed_warmup_quartets\":%u,"
      "\"executed_measured_quartets\":%u,\"pair_order\":\"alternating_ABBA_BAAB\","
      "\"json_self_check\":%s,\"qualification_eligible\":%s,"
      "\"cache_modes\":[\"candidate_warm\",\"historical_32KiB_rmw\"],"
      "\"setup_outside_timing\":true,\"checksums_outside_timing\":true,"
      "\"raw_quartet_value\":\"mean of the two observations for each candidate within its quartet\","
      "\"component_profiles\":\"separate paired runs; fence overhead means component medians are not additive\"},"
      "\"terminology\":{\"candidate\":\"prepared fixed-block source consumption\","
      "\"x3\":\"general generated-source baseline\"},"
      "\"historical_onemkl_reference\":{\"requested\":%s,\"loaded\":%s,"
      "\"compatible\":%s,\"path\":\"%s\",\"sha256\":\"%s\","
      "\"commit\":\"%s\",\"binary_sha256\":\"%s\","
      "\"measurement_date_utc\":\"%s\",\"reason\":\"%s\","
      "\"frozen_reference_unmodified\":true,\"cross_run_reference\":%s,"
      "\"required_contract\":{\"S0\":100,\"r\":0.03,\"q\":0,\"sigma\":0.20,"
      "\"T\":1,\"paths\":4096,\"gaussian_matrices\":1,\"evolutions\":1,"
      "\"strike_contract_id\":\"asian_qualified_nested_strikes_v1\","
      "\"classification\":\"complete_two_sided_full_risk\"}},\"cells\":[",
      FIXED_E2E_GIT_COMMIT,binary_hash,date,joe_kuo_hash,signed_z_hash,
      cpu_model,system_name.release,physical_cpu,
      getenv("MKL_THREADING_LAYER")?getenv("MKL_THREADING_LAYER"):"unset",
      getenv("MKL_NUM_THREADS")?getenv("MKL_NUM_THREADS"):"unset",
      getenv("MKL_DYNAMIC")?getenv("MKL_DYNAMIC"):"unset",
      active_warmup_quartets,active_measured_quartets,
      serialization_check?"true":"false",serialization_check?"false":"true",
      historical.requested?"true":"false",historical.loaded?"true":"false",
      historical.compatible?"true":"false",historical.path,historical.sha256,
      historical.commit,historical.binary_sha256,historical.measurement_date,historical.reason,
      historical.compatible?"true":"false");
    double global_logs[2][2][MAX_GLOBAL_CELLS];uint32_t global_cells[2][2]={{0,0},{0,0}};
    int comma=0,operational_failure=0;
    const uint32_t n_limit=serialization_check?1u:N_COUNT;
    const uint32_t workload_limit=serialization_check?1u:WORKLOAD_COUNT;
    const int estimator_limit=serialization_check?1:2;
    for(uint32_t ni=0;ni<n_limit&&!operational_failure;++ni)
      for(uint32_t wi=0;wi<workload_limit&&!operational_failure;++wi){
        const workload_spec_t *workload=&workloads[wi];
        if(prepare_fixture(native_n[ni],workload->strikes)!=0){operational_failure=1;break;}
        for(int estimator=0;estimator<estimator_limit&&!operational_failure;++estimator)
          for(int cache_mode=0;cache_mode<2&&!operational_failure;++cache_mode){
            asian_genuine_msfr_output_t outputs[SOURCE_CANDIDATES];double errors[SOURCE_CANDIDATES];
            invocation_counts_t counts[SOURCE_CANDIDATES];
            if(validate_cell(workload,estimator,outputs,errors,counts)!=0){operational_failure=1;break;}
            pair_complete_t x3_fixed,fixed_exact;pair_profile_t profile_x3_fixed,profile_fixed_exact;
            measure_pair_complete(SOURCE_X3,SOURCE_FIXED,workload,estimator,cache_mode,&x3_fixed);
            measure_pair_complete(SOURCE_FIXED,SOURCE_EXACT,workload,estimator,cache_mode,&fixed_exact);
            measure_pair_profile(SOURCE_X3,SOURCE_FIXED,workload,estimator,cache_mode,&profile_x3_fixed);
            measure_pair_profile(SOURCE_FIXED,SOURCE_EXACT,workload,estimator,cache_mode,&profile_fixed_exact);
            double ratio_tsc[MEASURED_QUARTETS],ratio_wall[MEASURED_QUARTETS];
            paired_ratios(&x3_fixed,0,ratio_tsc);paired_ratios(&x3_fixed,1,ratio_wall);
            double ceiling_tsc[MEASURED_QUARTETS],ceiling_wall[MEASURED_QUARTETS];
            paired_ratios(&fixed_exact,0,ceiling_tsc);paired_ratios(&fixed_exact,1,ceiling_wall);
            global_logs[cache_mode][0][global_cells[cache_mode][0]++]=mean_log_ratio(&x3_fixed,0);
            global_logs[cache_mode][1][global_cells[cache_mode][1]++]=mean_log_ratio(&x3_fixed,1);
            uint64_t fixed_wall_values[MEASURED_QUARTETS],fixed_tsc_values[MEASURED_QUARTETS];
            extract_clock(&x3_fixed,1,1,fixed_wall_values);extract_clock(&x3_fixed,1,0,fixed_tsc_values);
            const uint64_t fixed_wall_median=quantile_u64(fixed_wall_values,50u);
            const uint64_t fixed_tsc_median=quantile_u64(fixed_tsc_values,50u);
            const historical_row_t *old=historical_row(&historical,workload,native_n[ni],estimator,cache_mode);
            fprintf(json,"%s{\"N\":%u,\"K\":%u,\"workload\":\"%s\","
              "\"estimator\":\"%s\",\"cache_mode\":\"%s\",\"candidates\":{",
              comma++?",":"",native_n[ni],workload->strikes,workload->name,
              estimator?"geometric_cv":"arithmetic",cache_mode?"historical_32KiB_rmw":"candidate_warm");
            emit_candidate_metrics(json,candidate_names[SOURCE_X3],&x3_fixed,0,&profile_x3_fixed,0,
                                   workload->strikes,workload->fields);fputc(',',json);
            emit_candidate_metrics(json,candidate_names[SOURCE_FIXED],&x3_fixed,1,&profile_x3_fixed,1,
                                   workload->strikes,workload->fields);fputc(',',json);
            emit_candidate_metrics(json,candidate_names[SOURCE_EXACT],&fixed_exact,1,&profile_fixed_exact,1,
                                   workload->strikes,workload->fields);
            fprintf(json,"},\"paired_results\":{"
              "\"fixed_block_over_x3_tsc_ratio\":%.9g,\"fixed_block_over_x3_wall_ratio\":%.9g,"
              "\"fixed_block_over_x3_tsc_speedup\":%.9g,\"fixed_block_over_x3_wall_speedup\":%.9g,"
              "\"fixed_block_over_x3_tsc_ratio_p10\":%.9g,\"fixed_block_over_x3_tsc_ratio_p90\":%.9g,"
              "\"fixed_block_over_x3_wall_ratio_p10\":%.9g,\"fixed_block_over_x3_wall_ratio_p90\":%.9g,"
              "\"exact_lookup_over_fixed_block_tsc_ratio\":%.9g,"
              "\"exact_lookup_over_fixed_block_wall_ratio\":%.9g},"
              "\"numerics\":{\"max_abs_error_fixed_vs_x3\":%.9g,"
              "\"max_abs_error_exact_vs_x3\":%.9g,\"fixed_exact_bit_identity\":true,"
              "\"outputs\":{\"x3\":",quantile_double(ratio_tsc,50u),
              quantile_double(ratio_wall,50u),1.0/quantile_double(ratio_tsc,50u),
              1.0/quantile_double(ratio_wall,50u),quantile_double(ratio_tsc,10u),
              quantile_double(ratio_tsc,90u),quantile_double(ratio_wall,10u),
              quantile_double(ratio_wall,90u),quantile_double(ceiling_tsc,50u),
              quantile_double(ceiling_wall,50u),errors[SOURCE_FIXED],errors[SOURCE_EXACT]);
            emit_output(json,&outputs[SOURCE_X3],workload->strikes,workload->fields);
            fputs(",\"fixed_block\":",json);emit_output(json,&outputs[SOURCE_FIXED],workload->strikes,workload->fields);
            fputs(",\"exact_lookup\":",json);emit_output(json,&outputs[SOURCE_EXACT],workload->strikes,workload->fields);
            fprintf(json,"}},\"invocation_counts\":{\"per_candidate\":{"
              "\"source\":1,\"vector_exp\":2,\"route_evolution\":1,"
              "\"consumer_api\":1,\"sql_dual_control\":%u,\"basis_forward\":%u,"
              "\"l_to_g\":%u,\"strip_leaf\":%u,\"accumulator_init\":%u,"
              "\"consume_block\":%u,\"finalize\":%u,\"tile4_leaf\":%u},"
              "\"source_regenerated_per_fixing_dimension_strike_estimator_or_greek\":false},"
              "\"historical_onemkl\":{\"status\":\"%s\",\"cross_run_reference\":%s,"
              "\"unavailable_reason\":\"%s\"",
              counts[SOURCE_FIXED].sql_dual_control,counts[SOURCE_FIXED].basis_forward,
              counts[SOURCE_FIXED].l_to_g,counts[SOURCE_FIXED].strip_leaf,
              counts[SOURCE_FIXED].accumulator_init,counts[SOURCE_FIXED].consume_block,
              counts[SOURCE_FIXED].finalize,counts[SOURCE_FIXED].tile4_leaf,
              old?"AVAILABLE":"ONEMKL_REFERENCE_UNAVAILABLE",
              old?"true":"false",old?"not_applicable":
              workload->kind!=WORKLOAD_FULL_RISK?"output_set_mismatch":
              historical.compatible?"no_exact_historical_row":historical.reason);
            if(old!=NULL)fprintf(json,",\"historical_wall_ns_median\":%"PRIu64
              ",\"historical_tsc_median\":%"PRIu64
              ",\"historical_oneMKL_over_new_fixed_block_wall_ratio\":%.9g,"
              "\"historical_oneMKL_over_new_fixed_block_tsc_ratio\":%.9g,"
              "\"historical_measurement_date_utc\":\"%s\","
              "\"historical_commit\":\"%s\",\"new_measurement_date_utc\":\"%s\","
              "\"new_commit\":\"%s\",\"paired_measurement\":false,"
              "\"confidence_bound_claim\":false",old->wall_median,old->tsc_median,
              (double)old->wall_median/(double)fixed_wall_median,
              (double)old->tsc_median/(double)fixed_tsc_median,
              historical.measurement_date,historical.commit,date,FIXED_E2E_GIT_COMMIT);
            fputs("},\"raw_complete_quartets\":{",json);
            emit_raw_pair(json,"x3_fixed",&x3_fixed,"x3","fixed_block");fputc(',',json);
            emit_raw_pair(json,"fixed_exact",&fixed_exact,"fixed_block","exact_lookup");
            fputs("}}",json);
          }
        release_fixture();
      }
    if(operational_failure){release_fixture();atomic_json_abort(&publication);
        write_failure(output,"benchmark_or_validation");return 2;}
    fputs("],\"global_qualification\":{\"method\":"
      "\"equal-cell-weighted mean log of paired quartet ratios; normal 95-percent upper bound from across-cell variation\","
      "\"clocks\":{",json);int qualified=serialization_check?0:1;
    for(int cache_mode=0;cache_mode<2;++cache_mode)for(int clock=0;clock<2;++clock){
        double ratio;const double upper=global_upper95(global_logs[cache_mode][clock],
            global_cells[cache_mode][clock],&ratio);if(!(upper<1.0))qualified=0;
        fprintf(json,"%s\"%s_%s\":{\"cells\":%u,\"global_ratio\":%.9g,"
          "\"upper_95\":%.9g,\"upper_below_one\":%s}",cache_mode||clock?",":"",
          cache_mode?"historical_32KiB_rmw":"candidate_warm",clock?"wall":"tsc",
          global_cells[cache_mode][clock],ratio,upper,upper<1.0?"true":"false");}
    fprintf(json,"},\"require_all_clocks_and_modes\":true,"
      "\"qualification_eligible\":%s,\"passed\":%s},"
      "\"decision_questions\":["
      "\"Does prepared fixed-block source consumption make every complete pipeline faster than X3?\","
      "\"What is the improvement for every N and product?\","
      "\"Is the one-FMA source effectively at the exact-x lookup ceiling?\","
      "\"Which component dominates complete execution?\","
      "\"What compatible frozen oneMKL cross-run advantage remains?\"]}\n",
      serialization_check?"false":"true",qualified?"true":"false");
    if(fflush(json)!=0||fseek(json,status_offset,SEEK_SET)!=0||
       fwrite(serialization_check?"PASS":qualified?"PASS":"FAIL",1u,4u,json)!=4u||
       fseek(json,0,SEEK_END)!=0){
        atomic_json_abort(&publication);write_failure(output,"status_finalize");return 2;}
    if(atomic_json_commit(&publication,output)!=0){write_failure(output,"success_commit");return 2;}
    return serialization_check?0:qualified?0:1;
}
