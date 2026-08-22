#define _POSIX_C_SOURCE 200112L

#include "private/asian_geometric_cv_diag.h"
#include "private/asian_genuine_arithmetic_fused_source_exp_diag.h"
#include "private/asian_genuine_arithmetic_growth_only_diag.h"
#include "private/asian_genuine_arithmetic_growth_only_sha256.h"
#include "private/asian_genuine_arithmetic_growth_only_strip_adapter.h"
#include "private/asian_genuine_fixed_block_source_diag.h"
#include "private/asian_genuine_price_delta_strip_diag.h"

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PATHS = 4096, MAX_N = 256, HASH_SLOTS = 32768 };

typedef struct {
    uint32_t directions[MAX_N][32];
    uint32_t *source_words[2];
    float *x;
    float *growth;
    unsigned char *guarded_growth_fused;
    float *growth_fused;
    float *g_unused;
    fragment_map_t *maps;
    asian_genuine_route_t *routes;
    asian_genuine_route_t *poisoned_routes;
    asian_genuine_route_t *poisoned_fused_routes;
    uint16_t *oracle_source;
    asian_genuine_state_t *dual;
    asian_genuine_arithmetic_growth_only_context_t *context;
    asian_genuine_arithmetic_growth_only_context_t *fused_context;
    asian_genuine_arithmetic_fused_source_exp_context_t *fused_source_context;
    asian_genuine_arithmetic_growth_only_packet_trace_t *trace;
    unsigned char *guarded_q;
    float *q;
    unsigned char *guarded_q_fused;
    float *q_fused;
} fixture_t;

typedef struct { double rate, dividend, sigma, maturity; } market_t;

static const market_t markets[] = {
    {-0.02,0.01,0.05,0.25},
    { 0.00,0.00,0.40,0.25},
    { 0.03,0.00,0.20,1.00},
    { 0.03,0.01,0.05,5.00},
};

static void *a64(size_t bytes)
{
    void *pointer = NULL;
    if (posix_memalign(&pointer,64u,bytes) != 0) return NULL;
    memset(pointer,0,bytes);
    return pointer;
}

static uint32_t bits(float value)
{
    uint32_t out; memcpy(&out,&value,sizeof(out)); return out;
}

static float rounded_mul(float a, float b)
{
    volatile float value = a * b;
    return value;
}

static float rounded_add(float a, float b)
{
    volatile float value = a + b;
    return value;
}

static uint32_t sobol_word(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray=index^(index>>1),word=0;
    for(uint32_t bit=0;gray!=0u;++bit,gray>>=1)
        if((gray&1u)!=0u)word^=directions[bit];
    return word;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t bytes)
{
    const unsigned char *p=data;
    while(bytes--!=0u){hash^=*p++;hash*=UINT64_C(1099511628211);}
    return hash;
}

static uint64_t immutable_hash(const fixture_t *f, uint32_t n)
{
    uint64_t hash=UINT64_C(1469598103934665603);
    hash=hash_bytes(hash,f->directions,sizeof(f->directions));
    hash=hash_bytes(hash,f->source_words[0],PATHS*sizeof(uint32_t));
    hash=hash_bytes(hash,f->source_words[1],PATHS*sizeof(uint32_t));
    hash=hash_bytes(hash,f->x,2u*PATHS*sizeof(float));
    hash=hash_bytes(hash,f->growth,2u*PATHS*sizeof(float));
    hash=hash_bytes(hash,f->growth_fused,2u*PATHS*sizeof(float));
    hash=hash_bytes(hash,f->maps,(size_t)n*sizeof(*f->maps));
    hash=hash_bytes(hash,f->routes,(size_t)n*sizeof(*f->routes));
    hash=hash_bytes(hash,f->poisoned_routes,(size_t)n*sizeof(*f->poisoned_routes));
    hash=hash_bytes(hash,f->poisoned_fused_routes,
                    (size_t)n*sizeof(*f->poisoned_fused_routes));
    hash=hash_bytes(hash,f->context,sizeof(*f->context));
    hash=hash_bytes(hash,f->fused_context,sizeof(*f->fused_context));
    hash=hash_bytes(hash,f->fused_source_context,
                    sizeof(*f->fused_source_context));
    return hash;
}

static int load_directions(fixture_t *f)
{
    FILE *file=fopen("direction_numbers/joe_kuo_6_21201.bin","rb");
    if(file==NULL)return-1;
    for(uint32_t d=0;d<MAX_N;++d){
        uint32_t count;
        if(fread(&count,4u,1u,file)!=1u||count!=32u||
           fread(f->directions[d],4u,32u,file)!=32u){fclose(file);return-1;}
    }
    fclose(file);return 0;
}

static uint32_t hash_slot(uint32_t word)
{
    return (word*UINT32_C(2654435761))&(HASH_SLOTS-1u);
}

static int build_independent_oracle(fixture_t *f)
{
    uint32_t *keys=a64(HASH_SLOTS*sizeof(*keys));
    uint16_t *values=a64(HASH_SLOTS*sizeof(*values));
    unsigned char *used=a64(HASH_SLOTS);
    if(keys==NULL||values==NULL||used==NULL)return-1;
    for(uint32_t donor=0;donor<2u;++donor)for(uint32_t i=0;i<PATHS;++i){
        const uint32_t key=f->source_words[donor][i];uint32_t slot=hash_slot(key);
        while(used[slot]!=0u){if(keys[slot]==key){free(used);free(values);free(keys);return-1;}
            slot=(slot+1u)&(HASH_SLOTS-1u);}
        used[slot]=1u;keys[slot]=key;values[slot]=(uint16_t)(i|(donor<<12));
    }
    for(uint32_t d=0;d<MAX_N;++d)for(uint32_t path=0;path<PATHS;++path){
        const uint32_t key=sobol_word(8192u+path,f->directions[d]);
        uint32_t slot=hash_slot(key),probes=0;
        while(used[slot]!=0u&&keys[slot]!=key){slot=(slot+1u)&(HASH_SLOTS-1u);++probes;}
        if(used[slot]==0u||probes==HASH_SLOTS){free(used);free(values);free(keys);return-1;}
        f->oracle_source[(size_t)d*PATHS+path]=values[slot];
    }
    free(used);free(values);free(keys);return 0;
}

static uint32_t prepared_source(const asian_genuine_route_t *route,uint32_t path)
{
    const fragment_map_t *map=route->map;
    const uint32_t packet=path>>5,half=(path>>4)&1u,lane=path&15u;
    const uint32_t pattern=map->select[packet][2u+half];
    return (uint32_t)map->select[packet][half]*16u+map->patterns[pattern][lane];
}

static int prepare_routes(fixture_t *f)
{
    for(uint32_t path=0;path<PATHS;++path){
        f->source_words[0][path]=sobol_word(8192u+path,f->directions[0]);
        f->source_words[1][path]=sobol_word(12288u+path,f->directions[0]);
    }
    if(build_independent_oracle(f)!=0)return-1;
    const uint32_t *words[2]={f->source_words[0],f->source_words[1]};
    const float *xb[2]={f->x,f->x+PATHS};
    const float *gb[2]={f->growth,f->growth+PATHS};
    uint32_t *target=a64(PATHS*sizeof(*target));if(target==NULL)return-1;
    for(uint32_t d=0;d<MAX_N;++d){
        for(uint32_t path=0;path<PATHS;++path)
            target[path]=sobol_word(8192u+path,f->directions[d]);
        if(asian_genuine_prepare_route(words,2u,xb,gb,target,d,MAX_N,
             &f->maps[d],&f->routes[d])!=0){free(target);return-1;}
        const uint32_t expected_donor=f->oracle_source[(size_t)d*PATHS]>>12;
        for(uint32_t path=0;path<PATHS;++path){
            const uint16_t oracle=f->oracle_source[(size_t)d*PATHS+path];
            if((oracle>>12)!=expected_donor||prepared_source(&f->routes[d],path)!=(oracle&4095u)||
               f->routes[d].growth_base!=f->growth+(size_t)expected_donor*PATHS){
                free(target);return-1;
            }
        }
    }
    free(target);
    memcpy(f->poisoned_routes,f->routes,MAX_N*sizeof(*f->routes));
    memcpy(f->poisoned_fused_routes,f->routes,MAX_N*sizeof(*f->routes));
    for(uint32_t d=0;d<MAX_N;++d){
        const ptrdiff_t donor=f->routes[d].growth_base-f->growth;
        f->poisoned_routes[d].x_base=(const float *)(uintptr_t)1u;
        f->poisoned_routes[d].weight_bits=UINT32_C(0x7fc00000);
        f->poisoned_fused_routes[d].x_base=(const float *)(uintptr_t)1u;
        f->poisoned_fused_routes[d].growth_base=f->growth_fused+donor;
        f->poisoned_fused_routes[d].weight_bits=UINT32_C(0x7fc00000);
    }
    return 0;
}

static int fixture_init(fixture_t *f)
{
    memset(f,0,sizeof(*f));
    f->source_words[0]=a64(PATHS*sizeof(uint32_t));
    f->source_words[1]=a64(PATHS*sizeof(uint32_t));
    f->x=a64(2u*PATHS*sizeof(float));f->growth=a64(2u*PATHS*sizeof(float));
    f->guarded_growth_fused=a64(2u*PATHS*sizeof(float)+128u);
    if(f->guarded_growth_fused!=NULL)
        f->growth_fused=(float *)(f->guarded_growth_fused+64u);
    f->g_unused=a64(PATHS*sizeof(float));
    f->maps=a64(MAX_N*sizeof(*f->maps));f->routes=a64(MAX_N*sizeof(*f->routes));
    f->poisoned_routes=a64(MAX_N*sizeof(*f->poisoned_routes));
    f->poisoned_fused_routes=a64(MAX_N*sizeof(*f->poisoned_fused_routes));
    f->oracle_source=a64((size_t)MAX_N*PATHS*sizeof(uint16_t));
    f->dual=a64(sizeof(*f->dual));f->context=a64(sizeof(*f->context));
    f->fused_context=a64(sizeof(*f->fused_context));
    f->fused_source_context=a64(sizeof(*f->fused_source_context));
    f->trace=a64(sizeof(*f->trace));
    f->guarded_q=a64(ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_Q_BYTES+128u);
    f->guarded_q_fused=a64(
      ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_Q_BYTES+128u);
    if(!f->source_words[0]||!f->source_words[1]||!f->x||!f->growth||
       !f->growth_fused||!f->g_unused||!f->maps||!f->routes||
       !f->poisoned_routes||!f->poisoned_fused_routes||!f->oracle_source||
       !f->dual||!f->context||!f->fused_context||!f->fused_source_context||
       !f->trace||!f->guarded_q||!f->guarded_q_fused)
        return-1;
    f->q=(float *)(f->guarded_q+64u);
    f->q_fused=(float *)(f->guarded_q_fused+64u);
    if(load_directions(f)!=0||prepare_routes(f)!=0)return-1;
    for(uint32_t i=0;i<2u*PATHS;++i){f->growth[i]=1.0f;f->growth_fused[i]=1.0f;}
    return 0;
}

static void fixture_release(fixture_t *f)
{
    free(f->guarded_q_fused);free(f->guarded_q);free(f->trace);
    free(f->fused_source_context);free(f->fused_context);free(f->context);
    free(f->dual);free(f->oracle_source);free(f->poisoned_fused_routes);
    free(f->poisoned_routes);free(f->routes);free(f->maps);
    free(f->g_unused);free(f->guarded_growth_fused);free(f->growth);free(f->x);
    free(f->source_words[1]);free(f->source_words[0]);memset(f,0,sizeof(*f));
}

static void source_context(asian_genuine_fixed_block_source_context_t *context,
                           const market_t *market,uint32_t n)
{
    const double dt=market->maturity/(double)n;
    memset(context,0,sizeof(*context));
    context->signed_z=asian_genuine_fixed_block_signed_z;
    context->drift=(float)((market->rate-market->dividend-
                           .5*market->sigma*market->sigma)*dt);
    context->diffusion=(float)(market->sigma*sqrt(dt));
    context->magic=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_MAGIC;
    context->abi_version=ASIAN_GENUINE_FIXED_BLOCK_ABI_VERSION;
}

static asian_genuine_fixed_block_source_request_t source_request(
    const market_t *market,uint32_t n)
{
    asian_genuine_fixed_block_source_request_t request;
    memset(&request,0,sizeof(request));
    request.target_start_index=ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    request.path_count=ASIAN_GENUINE_FIXED_BLOCK_PATHS;
    request.block_count=1u;
    request.block_ordinal=0u;
    request.fixing_count=n;
    request.s0=100.0;
    request.rate=market->rate;
    request.dividend_yield=market->dividend;
    request.sigma=market->sigma;
    request.maturity=market->maturity;
    request.signed_z=asian_genuine_fixed_block_signed_z;
    request.signed_z_bytes=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES;
    return request;
}

static int produce_frontends(fixture_t *f,const market_t *market,uint32_t n,
                             asian_genuine_fixed_block_source_context_t *source)
{
    const asian_genuine_fixed_block_source_request_t request=
      source_request(market,n);
    if(asian_genuine_fixed_block_source_prepare(source,&request)!=
         ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK||
       asian_genuine_arithmetic_fused_source_exp_prepare(
         f->fused_source_context,&request,f->growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_OK)return-1;
    memset(f->guarded_growth_fused,0xa5,
      ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES+128u);
    const uint64_t table_hash=hash_bytes(UINT64_C(1469598103934665603),
      asian_genuine_fixed_block_signed_z,
      ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    const uint64_t request_hash=hash_bytes(UINT64_C(1469598103934665603),
      &request,sizeof(request));
    const uint64_t source_hash=hash_bytes(UINT64_C(1469598103934665603),
      source,sizeof(*source));
    const uint64_t fused_context_hash=hash_bytes(
      UINT64_C(1469598103934665603),f->fused_source_context,
      sizeof(*f->fused_source_context));
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(source,f->x);
    asian_vector_exp_range_reduced_array_diag(f->x,f->growth);
    asian_vector_exp_range_reduced_array_diag(f->x+PATHS,f->growth+PATHS);
    asian_genuine_arithmetic_fused_source_exp_diag(f->fused_source_context);
    if(memcmp(f->growth,f->growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=0)return-1;
    asian_genuine_arithmetic_fused_source_exp_diag(f->fused_source_context);
    if(memcmp(f->growth,f->growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=0)return-1;
    for(uint32_t i=0;i<64u;++i)
        if(f->guarded_growth_fused[i]!=0xa5u||
           f->guarded_growth_fused[64u+
             ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES+i]!=0xa5u)
            return-1;
    if(table_hash!=hash_bytes(UINT64_C(1469598103934665603),
         asian_genuine_fixed_block_signed_z,
         ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES)||
       request_hash!=hash_bytes(UINT64_C(1469598103934665603),
         &request,sizeof(request))||
       source_hash!=hash_bytes(UINT64_C(1469598103934665603),
         source,sizeof(*source))||
       fused_context_hash!=hash_bytes(UINT64_C(1469598103934665603),
         f->fused_source_context,sizeof(*f->fused_source_context)))return-1;
    return 0;
}

static int compare_consumer(const fixture_t *f,const market_t *market,uint32_t n)
{
    static const uint32_t ks[]={1,4,8,16,32};
    for(uint32_t ki=0;ki<sizeof(ks)/sizeof(ks[0]);++ki){
        const uint32_t k=ks[ki];float strikes[32];
        asian_genuine_strip_context_t *ctx=a64(sizeof(*ctx));
        asian_genuine_strip_output_t *a=a64(sizeof(*a)),*b=a64(sizeof(*b));
        if(!ctx||!a||!b||asian_genuine_strip_fixed_strikes(k,strikes)!=0||
           asian_genuine_strip_prepare(ctx,100.0,market->rate,market->dividend,
             market->sigma,market->maturity,n,0,0,0,strikes,k)!=0){free(b);free(a);free(ctx);return-1;}
        if(asian_genuine_strip_price_diag(f->dual->q,f->g_unused,ctx,
             ASIAN_GENUINE_STRIP_ARITHMETIC,4u,a)!=0||
           asian_genuine_strip_price_diag(f->q,f->g_unused,ctx,
             ASIAN_GENUINE_STRIP_ARITHMETIC,4u,b)!=0||
           memcmp(a->values,b->values,k*sizeof(a->values[0]))!=0){free(b);free(a);free(ctx);return-1;}
        memset(a,0,sizeof(*a));memset(b,0,sizeof(*b));
        if(asian_genuine_strip_price_delta_diag(f->dual->q,f->g_unused,ctx,
             ASIAN_GENUINE_STRIP_ARITHMETIC,4u,a)!=0||
           asian_genuine_strip_price_delta_diag(f->q,f->g_unused,ctx,
             ASIAN_GENUINE_STRIP_ARITHMETIC,4u,b)!=0||
           memcmp(a->values,b->values,k*sizeof(a->values[0]))!=0){free(b);free(a);free(ctx);return-1;}
        free(b);free(a);free(ctx);
    }
    return 0;
}

static int consume_private_policy(
    const asian_genuine_arithmetic_growth_only_context_t *growth_context,
    const float *q,const float *g_unused,
    const asian_genuine_strip_context_t *strip,uint32_t count,int price_delta,
    asian_genuine_strip_output_t *output)
{
    if((count==1u&&!price_delta)||count>=5u){
        memset(output,0,sizeof(*output));
        return asian_genuine_arithmetic_growth_only_strip_consume_padded(
          q,g_unused,strip,count,price_delta,output);
    }
    asian_genuine_arithmetic_growth_only_context_t poisoned
      __attribute__((aligned(64)))=*growth_context;
    poisoned.q_out=(float *)(uintptr_t)1u;
    const int status=asian_genuine_arithmetic_growth_only_immediate_consume(
      &poisoned,strip,count,price_delta,1,output);
    return status==ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_USED&&
      poisoned.q_out==(float *)(uintptr_t)1u?0:-1;
}

static int compare_runtime_strike_counts(const fixture_t *f,const market_t *market,
                                         uint32_t n)
{
    float strikes[32];
    for(uint32_t i=0;i<32u;++i)
        strikes[i]=70.25f+1.75f*(float)((i*13u+7u)&31u);
    const uint64_t strike_hash=hash_bytes(UINT64_C(1469598103934665603),strikes,sizeof(strikes));
    for(uint32_t count=1;count<=32u;++count){
        uint32_t padded=0;asian_genuine_strip_context_t *ctx=a64(sizeof(*ctx));
        asian_genuine_strip_output_t *a=a64(sizeof(*a)),*b=a64(sizeof(*b));
        if(!ctx||!a||!b||asian_genuine_arithmetic_growth_only_strip_prepare_padded(
             ctx,100.0,market->rate,market->dividend,market->sigma,market->maturity,
             n,0,0,0,strikes,count,&padded)!=0||padded<count)return-1;
        for(int price_delta=0;price_delta<=1;++price_delta){
            memset(a,0xa5,sizeof(*a));memset(b,0xa5,sizeof(*b));
            if(consume_private_policy(f->context,f->q,f->g_unused,ctx,count,
                 price_delta,a)!=0||
               consume_private_policy(f->fused_context,f->q_fused,f->g_unused,
                 ctx,count,price_delta,b)!=0||
               memcmp(a,b,sizeof(*a))!=0){free(b);free(a);free(ctx);return-1;}
            for(uint32_t i=count;i<32u;++i){
                asian_genuine_strip_value_t zero={0};
                if(memcmp(&a->values[i],&zero,sizeof(zero))!=0){free(b);free(a);free(ctx);return-1;}
            }
        }
        free(b);free(a);free(ctx);
    }
    return strike_hash==hash_bytes(UINT64_C(1469598103934665603),strikes,sizeof(strikes))?0:-1;
}

static void completed_history(uint32_t count,uint32_t shape,
                              double *q0,double *log_sum)
{
    *q0=0.0;*log_sum=0.0;
    for(uint32_t i=0;i<count;++i){
        double spot;
        if(shape==0u)spot=100.0;
        else if(shape==1u)spot=80.0+40.0*(double)(i+1u)/(double)(count+1u);
        else if(shape==2u)spot=120.0-40.0*(double)(i+1u)/(double)(count+1u);
        else {static const double cycle[]={82.0,97.0,113.0,104.0,89.0};spot=cycle[i%5u];}
        *q0+=spot;*log_sum+=log(spot);
    }
}

static int compare_immediate(const fixture_t *f,const market_t *market,
                             uint32_t future,uint32_t completed,
                             double q0,double log_sum)
{
    const float inv_total=1.0f/(float)(future+completed);
    const float a0=rounded_mul(rounded_add((float)q0,f->q[(future*17u)&4095u]),
                               inv_total);
    float strikes[4]={a0,nextafterf(a0,INFINITY),
                      nextafterf(a0,-INFINITY),73.25f};
    if(!(strikes[2]>0.0f))strikes[2]=nextafterf(0.0f,INFINITY);
    const uint64_t growth_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->growth,2u*PATHS*sizeof(float));
    const uint64_t route_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->poisoned_routes,(size_t)future*sizeof(*f->poisoned_routes));
    const uint64_t q_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->q,PATHS*sizeof(float));
    for(uint32_t k=1;k<=4u;++k){
        uint32_t padded=0;
        asian_genuine_strip_context_t *strip=a64(sizeof(*strip));
        asian_genuine_strip_output_t *baseline=a64(sizeof(*baseline));
        asian_genuine_strip_output_t *candidate=a64(sizeof(*candidate));
        asian_genuine_strip_output_t *repeat=a64(sizeof(*repeat));
        asian_genuine_strip_output_t *fused=a64(sizeof(*fused));
        asian_genuine_arithmetic_growth_only_context_t poisoned
          __attribute__((aligned(64)))=*f->context;
        poisoned.q_out=(float *)(uintptr_t)1u;
        if(!strip||!baseline||!candidate||!repeat||!fused||
           asian_genuine_arithmetic_growth_only_strip_prepare_padded(strip,
             100.0,market->rate,market->dividend,market->sigma,market->maturity,
             future,completed,q0,log_sum,strikes,k,&padded)!=0||
           padded!=(k==1u?1u:4u)){
            free(fused);free(repeat);free(candidate);free(baseline);free(strip);return-1;
        }
        const uint64_t strip_hash=hash_bytes(UINT64_C(1469598103934665603),
          strip,sizeof(*strip));
        for(int price_delta=0;price_delta<=1;++price_delta){
            memset(baseline,0,sizeof(*baseline));
            memset(candidate,0xa5,sizeof(*candidate));
            memset(repeat,0x5a,sizeof(*repeat));
            memset(fused,0x3c,sizeof(*fused));
            if(asian_genuine_arithmetic_growth_only_strip_consume_padded(
                 f->q,f->g_unused,strip,k,price_delta,baseline)!=0||
               asian_genuine_arithmetic_growth_only_immediate_consume(
                 &poisoned,strip,k,price_delta,1,candidate)!=
                 ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_USED||
               asian_genuine_arithmetic_growth_only_immediate_consume(
                 &poisoned,strip,k,price_delta,1,repeat)!=
                 ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_USED||
               consume_private_policy(f->fused_context,f->q_fused,
                 f->g_unused,strip,k,price_delta,fused)!=0||
               memcmp(baseline,candidate,sizeof(*baseline))!=0||
               memcmp(candidate,repeat,sizeof(*candidate))!=0||
               memcmp(baseline,fused,sizeof(*baseline))!=0||
               poisoned.q_out!=(float *)(uintptr_t)1u||
               strip_hash!=hash_bytes(UINT64_C(1469598103934665603),
                 strip,sizeof(*strip))){
                fprintf(stderr,"kink detail K=%u price_delta=%d base_fused=%d\n",
                  k,price_delta,memcmp(baseline,fused,sizeof(*baseline)));
                free(fused);free(repeat);free(candidate);free(baseline);free(strip);return-1;
            }
        }
        free(fused);free(repeat);free(candidate);free(baseline);free(strip);
    }
    if(growth_hash!=hash_bytes(UINT64_C(1469598103934665603),f->growth,
         2u*PATHS*sizeof(float))||
       route_hash!=hash_bytes(UINT64_C(1469598103934665603),
         f->poisoned_routes,(size_t)future*sizeof(*f->poisoned_routes))||
       q_hash!=hash_bytes(UINT64_C(1469598103934665603),f->q,
         PATHS*sizeof(float)))return-1;
    return 0;
}

static int compare_seasoned_consumer(const fixture_t *f,const market_t *market,
                                     uint32_t future)
{
    static const uint32_t totals[]={16,32,64,128,256};
    for(uint32_t mi=0;mi<sizeof(totals)/sizeof(totals[0]);++mi){
        const uint32_t m=totals[mi];
        const uint32_t completed_values[]={0u,1u,m/4u,m/2u,m-1u};
        for(uint32_t ci=0;ci<5u;++ci){
            const uint32_t completed=completed_values[ci];
            if(m-completed!=future||future<2u)continue;
            for(uint32_t shape=0;shape<4u;++shape){
                double q0,log_sum;completed_history(completed,shape,&q0,&log_sum);
                for(uint32_t k=1;k<=32u;++k){
                    float strikes[32];
                    for(uint32_t i=0;i<32u;++i)
                        strikes[i]=68.5f+1.625f*(float)((i*11u+3u)&31u);
                    uint32_t padded=0;
                    asian_genuine_strip_context_t *ctx=a64(sizeof(*ctx));
                    asian_genuine_strip_output_t *dual=a64(sizeof(*dual));
                    asian_genuine_strip_output_t *current=a64(sizeof(*current));
                    asian_genuine_strip_output_t *fused=a64(sizeof(*fused));
                    if(!ctx||!dual||!current||!fused||
                       asian_genuine_arithmetic_growth_only_strip_prepare_padded(
                         ctx,100.0,market->rate,market->dividend,market->sigma,
                         market->maturity,future,completed,q0,log_sum,strikes,k,
                         &padded)!=0||padded<k){
                        free(fused);free(current);free(dual);free(ctx);return-1;}
                    for(int price_delta=0;price_delta<=1;++price_delta){
                        memset(dual,0,sizeof(*dual));memset(current,0,sizeof(*current));
                        memset(fused,0,sizeof(*fused));
                        if(asian_genuine_arithmetic_growth_only_strip_consume_padded(
                             f->dual->q,f->g_unused,ctx,k,price_delta,dual)!=0||
                           consume_private_policy(f->context,f->q,f->g_unused,
                             ctx,k,price_delta,current)!=0||
                           consume_private_policy(f->fused_context,f->q_fused,
                             f->g_unused,ctx,k,price_delta,fused)!=0||
                           memcmp(dual,current,sizeof(*dual))!=0||
                           memcmp(current,fused,sizeof(*current))!=0){
                            free(fused);free(current);free(dual);free(ctx);return-1;}
                    }
                    free(fused);free(current);free(dual);free(ctx);
                }
            }
        }
    }
    return 0;
}

static int run_case(fixture_t *f,uint32_t n,uint32_t market_index,int probes)
{
    const market_t *market=&markets[market_index];
    asian_genuine_fixed_block_source_context_t source __attribute__((aligned(64)));
    if(produce_frontends(f,market,n,&source)!=0){fprintf(stderr,"front N=%u\n",n);return-1;}
    memset(f->guarded_q,0xa5,ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_Q_BYTES+128u);
    memset(f->guarded_q_fused,0xa5,
      ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_Q_BYTES+128u);
    if(asian_genuine_arithmetic_growth_only_prepare(f->context,f->poisoned_routes,n,
         100.0f,f->growth,2u*PATHS*sizeof(float),f->q,PATHS*sizeof(float))!=0||
       asian_genuine_arithmetic_growth_only_prepare(f->fused_context,
         f->poisoned_fused_routes,n,100.0f,f->growth_fused,
         2u*PATHS*sizeof(float),f->q_fused,PATHS*sizeof(float))!=0){fprintf(stderr,"growth prepare N=%u\n",n);return-1;}
    const uint64_t before=immutable_hash(f,n);
    const uint64_t source_before=hash_bytes(UINT64_C(1469598103934665603),
      &source,sizeof(source));
    memset(f->dual,0,sizeof(*f->dual));
    for(uint32_t path=0;path<PATHS;++path)f->dual->s[path]=100.0f;
    asian_genuine_sql_dual_control_diag(f->routes,n,f->dual);
    asian_genuine_arithmetic_growth_only_q_diag(f->context);
    asian_genuine_arithmetic_growth_only_q_diag(f->fused_context);
    if(memcmp(f->dual->q,f->q,PATHS*sizeof(float))!=0||
       memcmp(f->q,f->q_fused,PATHS*sizeof(float))!=0){fprintf(stderr,"q identity N=%u\n",n);return-1;}
    unsigned char *repeat=a64(PATHS*sizeof(float));if(repeat==NULL)return-1;
    memcpy(repeat,f->q,PATHS*sizeof(float));
    asian_genuine_arithmetic_growth_only_q_diag(f->context);
    if(memcmp(repeat,f->q,PATHS*sizeof(float))!=0){free(repeat);return-1;}free(repeat);
    for(uint32_t i=0;i<64u;++i)
        if(f->guarded_q[i]!=0xa5u||
           f->guarded_q[64u+PATHS*sizeof(float)+i]!=0xa5u||
           f->guarded_q_fused[i]!=0xa5u||
           f->guarded_q_fused[64u+PATHS*sizeof(float)+i]!=0xa5u)return-1;
    if(before!=immutable_hash(f,n)||source_before!=hash_bytes(
         UINT64_C(1469598103934665603),&source,sizeof(source)))return-1;

    for(uint32_t path=0;path<PATHS;++path){
        float s=100.0f,q=0.0f;
        for(uint32_t fixing=0;fixing<n;++fixing){
            const uint16_t encoded=f->oracle_source[(size_t)fixing*PATHS+path];
            const float growth=f->growth[(size_t)(encoded>>12)*PATHS+(encoded&4095u)];
            s=rounded_mul(s,growth);q=rounded_add(q,s);
        }
        if(bits(s)!=bits(f->dual->s[path])||bits(q)!=bits(f->q[path]))return-1;
    }

    if(probes)for(uint32_t packet=0;packet<128u;++packet){
        memset(f->trace,0,sizeof(*f->trace));
        asian_genuine_arithmetic_growth_only_packet_probe_diag(f->context,packet,f->trace);
        for(uint32_t lane=0;lane<32u;++lane){
            const uint32_t path=packet*32u+lane;float s=100.0f,q=0.0f;
            for(uint32_t fixing=0;fixing<n;++fixing){
                const uint16_t encoded=f->oracle_source[(size_t)fixing*PATHS+path];
                const float growth=f->growth[(size_t)(encoded>>12)*PATHS+(encoded&4095u)];
                s=rounded_mul(s,growth);q=rounded_add(q,s);
                if(bits(growth)!=bits(f->trace->growth[fixing][lane])||
                   bits(s)!=bits(f->trace->s[fixing][lane])||
                   bits(q)!=bits(f->trace->q[fixing][lane]))return-1;
                if(fixing==0u){const float plus_zero=rounded_add(0.0f,s);
                    if(bits(plus_zero)!=bits(s))return-1;}
            }
        }
    }
    if((n==16u||n==32u||n==64u||n==128u||n==256u)&&
       compare_consumer(f,market,n)!=0){fprintf(stderr,"consumer N=%u\n",n);return-1;}
    if((n==16u||n==32u||n==64u||n==128u||n==256u)&&
       compare_runtime_strike_counts(f,market,n)!=0){fprintf(stderr,"K1..32 N=%u\n",n);return-1;}
    if(compare_seasoned_consumer(f,market,n)!=0){fprintf(stderr,"seasoned N=%u\n",n);return-1;}
    if(compare_immediate(f,market,n,0u,0.0,0.0)!=0){fprintf(stderr,"kink N=%u\n",n);return-1;}
    if(n==255u||n==192u||n==128u){
        const uint32_t completed=256u-n;
        for(uint32_t shape=0;shape<4u;++shape){
            double q0,log_sum;completed_history(completed,shape,&q0,&log_sum);
            if(compare_immediate(f,market,n,completed,q0,log_sum)!=0)return-1;
        }
    }
    return 0;
}

static int consumer_only_one_fixing(fixture_t *f)
{
    static const uint32_t totals[]={16,32,64,128,256};
    for(uint32_t market_index=0;market_index<sizeof(markets)/sizeof(markets[0]);++market_index){
        const market_t *market=&markets[market_index];
        asian_genuine_fixed_block_source_context_t source __attribute__((aligned(64)));
        source_context(&source,market,1u);
        asian_genuine_fixed_block_signed_z_one_fma_source_diag(&source,f->x);
        asian_vector_exp_range_reduced_array_diag(f->x,f->growth);
        for(uint32_t path=0;path<PATHS;++path)f->q[path]=rounded_mul(100.0f,f->growth[path]);
        for(uint32_t mi=0;mi<sizeof(totals)/sizeof(totals[0]);++mi){
            const uint32_t m=totals[mi],completed=m-1u;
            for(uint32_t shape=0;shape<4u;++shape){
                double q0,log_sum;completed_history(completed,shape,&q0,&log_sum);
                float strikes[32];asian_genuine_strip_context_t *ctx=a64(sizeof(*ctx));
                asian_genuine_strip_output_t *output=a64(sizeof(*output));
                if(!ctx||!output||asian_genuine_strip_fixed_strikes(32u,strikes)!=0||
                   asian_genuine_strip_prepare(ctx,100.0,market->rate,market->dividend,
                     market->sigma,market->maturity,1u,completed,q0,log_sum,strikes,32u)!=0||
                   asian_genuine_strip_price_delta_diag(f->q,f->g_unused,ctx,
                     ASIAN_GENUINE_STRIP_ARITHMETIC,4u,output)!=0){free(output);free(ctx);return-1;}
                for(uint32_t k=0;k<32u;++k)
                    if(!isfinite(output->values[k].call_price)||
                       !isfinite(output->values[k].put_price)||
                       !isfinite(output->values[k].call_delta)||
                       !isfinite(output->values[k].put_delta)){free(output);free(ctx);return-1;}
                free(output);free(ctx);
            }
        }
    }
    return 0;
}

static int fused_preparation_tests(fixture_t *f)
{
    asian_genuine_fixed_block_source_request_t request=
      source_request(&markets[2],16u);
    asian_genuine_fixed_block_source_context_t source
      __attribute__((aligned(64)));
    float *copy=a64(ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    if(copy==NULL)return-1;
    memcpy(copy,asian_genuine_fixed_block_signed_z,
      ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES);
    if(asian_genuine_arithmetic_fused_source_exp_prepare(
         f->fused_source_context,&request,f->growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_OK||
       f->fused_source_context->signed_z!=asian_genuine_fixed_block_signed_z||
       f->fused_source_context->growth_out!=f->growth_fused||
       f->fused_source_context->path_count!=4096u||
       f->fused_source_context->region_count!=2u||
       f->fused_source_context->values_per_region!=4096u||
       f->fused_source_context->first_index!=8192u||
       f->fused_source_context->total_values!=8192u||
       f->fused_source_context->magic!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_MAGIC||
       f->fused_source_context->abi_version!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ABI_VERSION){
        free(copy);return-1;
    }
    for(uint32_t i=0;i<sizeof(f->fused_source_context->reserved);++i)
        if(f->fused_source_context->reserved[i]!=0u){free(copy);return-1;}

#define FUSED_REJECT(expr,code) do { \
    asian_genuine_fixed_block_source_request_t rejected=request; expr; \
    if(asian_genuine_arithmetic_fused_source_exp_prepare( \
         f->fused_source_context,&rejected,f->growth_fused, \
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=(code)){ \
        free(copy);return-1;} \
} while(0)
    if(asian_genuine_arithmetic_fused_source_exp_prepare(NULL,&request,
         f->growth_fused,ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_INVALID||
       asian_genuine_arithmetic_fused_source_exp_prepare(
         f->fused_source_context,NULL,f->growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_INVALID||
       asian_genuine_arithmetic_fused_source_exp_prepare(
         f->fused_source_context,&request,NULL,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_INVALID||
       asian_genuine_arithmetic_fused_source_exp_prepare(
         (asian_genuine_arithmetic_fused_source_exp_context_t *)
           ((unsigned char *)f->fused_source_context+4u),&request,
         f->growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ALIGNMENT||
       asian_genuine_arithmetic_fused_source_exp_prepare(
         f->fused_source_context,&request,
         (float *)((unsigned char *)f->growth_fused+4u),
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ALIGNMENT||
       asian_genuine_arithmetic_fused_source_exp_prepare(
         f->fused_source_context,&request,f->growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES-4u)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_INVALID){
        free(copy);return-1;
    }
    FUSED_REJECT(rejected.fixing_count=1u,
      ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_SOURCE_REJECTED);
    FUSED_REJECT(rejected.fixing_count=257u,
      ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_SOURCE_REJECTED);
    FUSED_REJECT(rejected.flags=ASIAN_GENUINE_FIXED_BLOCK_REQUEST_SCRAMBLE,
      ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_SOURCE_REJECTED);
    request.signed_z=copy;
    if(asian_genuine_fixed_block_source_prepare(&source,&request)!=
         ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK||
       asian_genuine_arithmetic_fused_source_exp_prepare(
         f->fused_source_context,&request,f->growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_TABLE_INVALID){
        free(copy);return-1;
    }
    request=source_request(&markets[2],16u);
    if(asian_genuine_arithmetic_fused_source_exp_prepare(
         f->fused_source_context,&request,
         (float *)(uintptr_t)asian_genuine_fixed_block_signed_z,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ALIAS||
       asian_genuine_arithmetic_fused_source_exp_prepare(
         (asian_genuine_arithmetic_fused_source_exp_context_t *)f->growth_fused,
         &request,f->growth_fused,
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES)!=
         ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_ALIAS){
        free(copy);return-1;
    }
#undef FUSED_REJECT

    /* Adjacent binary64 market inputs preserve fixed-source delegation. */
    for(uint32_t market=0;market<sizeof(markets)/sizeof(markets[0]);++market){
        const market_t *base=&markets[market];
        for(uint32_t field=0;field<4u;++field)
            for(uint32_t direction=0;direction<2u;++direction){
                market_t adjacent=*base;
                double *value=field==0u?&adjacent.rate:
                  field==1u?&adjacent.dividend:
                  field==2u?&adjacent.sigma:&adjacent.maturity;
                *value=nextafter(*value,direction==0u?-INFINITY:INFINITY);
                request=source_request(&adjacent,16u);
                const int baseline=asian_genuine_fixed_block_source_prepare(
                  &source,&request);
                const int fused=asian_genuine_arithmetic_fused_source_exp_prepare(
                  f->fused_source_context,&request,f->growth_fused,
                  ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_GROWTH_BYTES);
                if((baseline==ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK&&
                    fused!=ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_OK)||
                   (baseline!=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK&&
                    fused!=ASIAN_GENUINE_ARITHMETIC_FUSED_SOURCE_EXP_SOURCE_REJECTED)){
                    free(copy);return-1;
                }
            }
    }
    free(copy);return 0;
}

static int fused_growth_matrix(fixture_t *f)
{
    asian_genuine_fixed_block_source_context_t source
      __attribute__((aligned(64)));
    for(uint32_t n=2u;n<=256u;++n)
        for(uint32_t market=0;market<sizeof(markets)/sizeof(markets[0]);++market)
            if(produce_frontends(f,&markets[market],n,&source)!=0){
                fprintf(stderr,"fused growth mismatch market=%u N=%u\n",market,n);
                return-1;
            }
    return 0;
}

static int negative_tests(fixture_t *f)
{
    char digest[65];
    if(asian_genuine_arithmetic_growth_only_file_sha256(
         "private/asian_genuine_fixed_block_signed_z.bin",digest)!=0||
       strcmp(digest,"ecf3bb854e98bedcf724d0743438457ccf8b600e1264cb537741ce0b9d90d98d")!=0)
        return-1;
    asian_genuine_arithmetic_growth_only_context_t *ctx=f->context;
    if(asian_genuine_arithmetic_growth_only_prepare(NULL,f->routes,2,100,f->growth,
         32768,f->q,16384)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,NULL,2,100,f->growth,
         32768,f->q,16384)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,100,NULL,
         32768,f->q,16384)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,100,f->growth,
         32768,NULL,16384)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,1,100,f->growth,
         32768,f->q,16384)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_FIXINGS_UNSUPPORTED)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,257,100,f->growth,
         32768,f->q,16384)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_FIXINGS_UNSUPPORTED)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,100,f->growth,
         32764,f->q,16384)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,100,f->growth,
         32768,f->q,16380)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,100,f->growth,
         32768,(float *)((unsigned char *)f->q+4),16384)!=
         ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ALIGNMENT)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(
         (asian_genuine_arithmetic_growth_only_context_t *)((unsigned char *)ctx+4),
         f->routes,2,100,f->growth,32768,f->q,16384)!=
         ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ALIGNMENT)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,
         (const asian_genuine_route_t *)((const unsigned char *)f->routes+8),
         2,100,f->growth,32768,f->q,16384)!=
         ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ALIGNMENT)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,100,
         (const float *)((const unsigned char *)f->growth+4),32768,f->q,16384)!=
         ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ALIGNMENT)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,100,f->growth,
         32768,f->growth,16384)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ALIAS)return-1;
    if(asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,0.0f,f->growth,
         32768,f->q,16384)!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_INVALID)return-1;
    const float saved_growth=f->growth[0];f->growth[0]=0.0f;
    const int zero_growth=asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,
         100,f->growth,32768,f->q,16384);f->growth[0]=saved_growth;
    if(zero_growth!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DOMAIN)return-1;
    f->growth[0]=INFINITY;
    const int infinite_growth=asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,
         2,100,f->growth,32768,f->q,16384);f->growth[0]=saved_growth;
    if(infinite_growth!=ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_DOMAIN)return-1;
    fragment_map_t saved=f->maps[1];f->maps[1].dimension=999u;
    const int bad=asian_genuine_arithmetic_growth_only_prepare(ctx,f->routes,2,100,
         f->growth,32768,f->q,16384);f->maps[1]=saved;
    return bad==ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ROUTE_INVALID?0:-1;
}

int main(int argc,char **argv)
{
    if(argc!=1){fprintf(stderr,"usage: %s\n",argv[0]);return 2;}
    fixture_t fixture;if(fixture_init(&fixture)!=0){fprintf(stderr,"fixture failed\n");return 2;}
    if(negative_tests(&fixture)!=0||fused_preparation_tests(&fixture)!=0){
        fprintf(stderr,"negative/preparation gate failed\n");fixture_release(&fixture);return 1;}
    if(fused_growth_matrix(&fixture)!=0){fixture_release(&fixture);return 1;}
    for(uint32_t n=2;n<=256u;++n){
        const uint32_t market=n%(sizeof(markets)/sizeof(markets[0]));
        if(run_case(&fixture,n,market,n==2u||n==256u)!=0){
            fprintf(stderr,"failure market=%u N=%u\n",market,n);
            fixture_release(&fixture);return 1;
        }
    }
    for(uint32_t market=0;market<sizeof(markets)/sizeof(markets[0]);++market){
        if(run_case(&fixture,16u,market,market==0u)!=0){
            fprintf(stderr,"boundary failure market=%u N=16\n",market);
            fixture_release(&fixture);return 1;
        }
    }
    if(consumer_only_one_fixing(&fixture)!=0){fprintf(stderr,"one-fixing consumer gate failed\n");fixture_release(&fixture);return 1;}
    puts("fused_growth_bits N=2..256 PASS");
    puts("arithmetic_price_K1..32 PASS");
    puts("arithmetic_price_delta_K1..32 PASS");
    puts("seasoning PASS");
    fixture_release(&fixture);return 0;
}
