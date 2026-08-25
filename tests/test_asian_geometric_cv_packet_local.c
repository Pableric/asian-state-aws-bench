#define _POSIX_C_SOURCE 200112L

#include "private/asian_geometric_cv_packet_local_diag.h"
#include "private/asian_geometric_cv_diag.h"
#include "private/asian_genuine_fixed_block_source_diag.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PATHS=4096, MAX_N=256, GUARD=64 };

typedef struct { double rate,dividend,sigma,maturity; } market_t;
static const market_t markets[]={
    {-0.02,0.01,0.05,0.25},
    { 0.00,0.00,0.40,0.25},
    { 0.03,0.00,0.20,1.00},
    { 0.03,0.01,0.05,5.00},
};

typedef struct {
    uint32_t directions[MAX_N][32];
    uint32_t *words[2],*target;
    float *x,*growth,*baseline_g;
    fragment_map_t *maps;
    asian_genuine_route_t *routes;
    asian_meta_affine_plan_t *meta_plan;
    asian_meta_affine_route_t *meta_routes;
    asian_genuine_state_t *baseline;
    unsigned char *q_storage,*g_storage;
    float *q,*g;
    asian_geometric_cv_packet_local_context_t *context;
    asian_geometric_cv_packet_local_packet_trace_t *trace;
} fixture_t;

static void *a64(size_t bytes)
{
    void *p=NULL;
    if(posix_memalign(&p,64u,bytes)!=0)return NULL;
    memset(p,0,bytes);return p;
}

static uint32_t fbits(float x){uint32_t u;memcpy(&u,&x,4u);return u;}
static float rmul(float a,float b){volatile float x=a*b;return x;}
static float radd(float a,float b){volatile float x=a+b;return x;}

static uint64_t hash_bytes(uint64_t h,const void *memory,size_t bytes)
{
    const unsigned char *p=memory;
    while(bytes--){h^=*p++;h*=UINT64_C(1099511628211);}return h;
}

static uint32_t sobol(uint32_t index,const uint32_t direction[32])
{
    uint32_t gray=index^(index>>1),word=0u;
    for(uint32_t bit=0u;gray;gray>>=1u,++bit)if(gray&1u)word^=direction[bit];
    return word;
}

static uint32_t selected_source(const asian_genuine_route_t *route,uint32_t path)
{
    const fragment_map_t *map=route->map;
    const uint32_t packet=path>>5u,half=(path>>4u)&1u,lane=path&15u;
    const uint32_t pattern=map->select[packet][2u+half];
    return (uint32_t)map->select[packet][half]*16u+map->patterns[pattern][lane];
}

static uint32_t meta_selected_source(const asian_meta_affine_route_t *route,
                                     uint32_t path)
{
    const asian_meta_dim_affine_ctx_t *map=route->map;
    const uint32_t packet=path>>5u,half=(path>>4u)&1u,lane=path&15u;
    const uint32_t line=(uint32_t)map->sel2[packet][0]^half;
    const uint32_t control=map->base_control[lane]^
      (uint32_t)map->sel2[packet][1]^(half?map->half_delta[lane]:0u);
    return line*16u+control;
}

static int load_directions(fixture_t *f)
{
    FILE *in=fopen("direction_numbers/joe_kuo_6_21201.bin","rb");
    if(in==NULL)return-1;
    for(uint32_t d=0;d<MAX_N;++d){
        uint32_t count;
        if(fread(&count,4u,1u,in)!=1u||count!=32u||
           fread(f->directions[d],4u,32u,in)!=32u){fclose(in);return-1;}
    }
    fclose(in);return 0;
}

static int fixture_init(fixture_t *f)
{
    memset(f,0,sizeof(*f));
    f->words[0]=a64(PATHS*4u);f->words[1]=a64(PATHS*4u);
    f->target=a64(PATHS*4u);f->x=a64(2u*PATHS*4u);
    f->growth=a64(2u*PATHS*4u);f->baseline_g=a64(PATHS*4u);
    f->maps=a64(MAX_N*sizeof(*f->maps));
    f->routes=a64(MAX_N*sizeof(*f->routes));
    f->meta_routes=a64(MAX_N*sizeof(*f->meta_routes));
    f->baseline=a64(sizeof(*f->baseline));f->context=a64(sizeof(*f->context));
    f->trace=a64(sizeof(*f->trace));
    f->q_storage=a64(PATHS*4u+2u*GUARD);f->g_storage=a64(PATHS*4u+2u*GUARD);
    if(!f->words[0]||!f->words[1]||!f->target||!f->x||!f->growth||
       !f->baseline_g||!f->maps||!f->routes||!f->meta_routes||
       !f->baseline||!f->context||
       !f->trace||!f->q_storage||!f->g_storage)return-1;
    f->q=(float *)(f->q_storage+GUARD);f->g=(float *)(f->g_storage+GUARD);
    if(load_directions(f)!=0||asian_meta_affine_plan_create(&f->meta_plan)!=0)
        return-1;
    for(uint32_t path=0;path<PATHS;++path){
        f->words[0][path]=sobol(8192u+path,f->directions[0]);
        f->words[1][path]=sobol(12288u+path,f->directions[0]);
    }
    return 0;
}

static void fixture_release(fixture_t *f)
{
    free(f->g_storage);free(f->q_storage);free(f->trace);free(f->context);
    asian_meta_affine_plan_destroy(f->meta_plan);
    free(f->baseline);free(f->meta_routes);free(f->routes);free(f->maps);
    free(f->baseline_g);
    free(f->growth);free(f->x);free(f->target);free(f->words[1]);free(f->words[0]);
    memset(f,0,sizeof(*f));
}

/* One caller-owned preparation supplies both baseline and candidate records. */
static int prepare_shared_routes(fixture_t *f,uint32_t future,uint32_t total)
{
    const uint32_t *words[2]={f->words[0],f->words[1]};
    const float *x[2]={f->x,f->x+PATHS};
    const float *growth[2]={f->growth,f->growth+PATHS};
    for(uint32_t fixing=0;fixing<future;++fixing){
        for(uint32_t path=0;path<PATHS;++path)
            f->target[path]=sobol(8192u+path,f->directions[fixing]);
        if(asian_genuine_prepare_route(words,2u,x,growth,f->target,fixing,total,
             &f->maps[fixing],&f->routes[fixing])!=0)return-1;
        for(uint32_t path=0;path<PATHS;++path){
            const uint32_t source=selected_source(&f->routes[fixing],path);
            if(f->words[f->routes[fixing].x_base==x[1]][source]!=f->target[path])
                return-1;
        }
        const size_t donor_offset=(size_t)f->meta_plan->donor_region[fixing]*PATHS;
        f->meta_routes[fixing].x_base=f->x+donor_offset;
        f->meta_routes[fixing].growth_base=f->growth+donor_offset;
        f->meta_routes[fixing].map=&f->meta_plan->contexts[fixing];
        f->meta_routes[fixing].weight_bits=f->routes[fixing].weight_bits;
        f->meta_routes[fixing].fixing_index=fixing;
        for(uint32_t path=0;path<PATHS;++path)
            if(meta_selected_source(&f->meta_routes[fixing],path)!=
               selected_source(&f->routes[fixing],path))return-1;
    }
    return 0;
}

static asian_genuine_fixed_block_source_request_t source_request(
    const market_t *market,uint32_t future)
{
    asian_genuine_fixed_block_source_request_t request;
    memset(&request,0,sizeof(request));
    request.target_start_index=ASIAN_GENUINE_FIXED_BLOCK_FIRST_INDEX;
    request.path_count=ASIAN_GENUINE_FIXED_BLOCK_PATHS;
    request.block_count=1u;request.fixing_count=future;request.s0=100.0;
    request.rate=market->rate;request.dividend_yield=market->dividend;
    request.sigma=market->sigma;request.maturity=market->maturity;
    request.signed_z=asian_genuine_fixed_block_signed_z;
    request.signed_z_bytes=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_BYTES;
    return request;
}

static int produce(fixture_t *f,const market_t *market,uint32_t future)
{
    asian_genuine_fixed_block_source_context_t source __attribute__((aligned(64)));
    const asian_genuine_fixed_block_source_request_t request=source_request(market,future);
    if(asian_genuine_fixed_block_source_prepare(&source,&request)!=
       ASIAN_GENUINE_FIXED_BLOCK_SOURCE_OK)return-1;
    asian_genuine_fixed_block_signed_z_one_fma_source_diag(&source,f->x);
    asian_vector_exp_range_reduced_array_diag(f->x,f->growth);
    asian_vector_exp_range_reduced_array_diag(f->x+PATHS,f->growth+PATHS);
    return 0;
}

static void initial_state(asian_genuine_state_t *state)
{
    memset(state,0,sizeof(*state));
    for(uint32_t path=0;path<PATHS;++path)state->s[path]=100.0f;
}

static void history(uint32_t count,uint32_t shape,double *q0,double *log_sum)
{
    *q0=0.0;*log_sum=0.0;
    for(uint32_t i=0;i<count;++i){
        static const double cycle[]={82.0,97.0,113.0,104.0,89.0};
        double s=shape==0u?100.0:shape==1u?80.0+40.0*(i+1.0)/(count+1.0):
          shape==2u?120.0-40.0*(i+1.0)/(count+1.0):cycle[i%5u];
        *q0+=s;*log_sum+=log(s);
    }
}

static void arbitrary_strikes(float out[32],uint32_t count,float kink)
{
    for(uint32_t i=0;i<count;++i)
        out[i]=61.25f+2.125f*(float)((i*19u+7u)%count);
    if(count>=3u){out[0]=kink;out[1]=nextafterf(kink,INFINITY);
        out[2]=nextafterf(kink,-INFINITY);if(!(out[2]>0.0f))out[2]=0.5f;}
}

static int compare_output_case(fixture_t *f,const market_t *market,
    uint32_t future,uint32_t completed,double q0,double log_sum,uint32_t count)
{
    asian_genuine_strip_context_t *strip=a64(sizeof(*strip));
    asian_genuine_strip_output_t *baseline=a64(sizeof(*baseline));
    asian_genuine_strip_output_t *candidate=a64(sizeof(*candidate));
    float strikes[32];uint32_t padded=0;
    const float kink=(q0+f->baseline->q[(future*37u+count*11u)&4095u])/
      (float)(future+completed);
    arbitrary_strikes(strikes,count,kink);
    if(!strip||!baseline||!candidate||
       asian_geometric_cv_packet_local_strip_prepare_padded(strip,100.0,
         market->rate,market->dividend,market->sigma,market->maturity,
         future,completed,q0,log_sum,strikes,count,&padded)!=0||padded<count){
        free(candidate);free(baseline);free(strip);return-1;
    }
    const uint64_t input_hash=hash_bytes(UINT64_C(1469598103934665603),strip,sizeof(*strip));
    for(int price_delta=0;price_delta<=1;++price_delta){
        memset(baseline,0,sizeof(*baseline));memset(candidate,0,sizeof(*candidate));
        const int a=price_delta?asian_genuine_strip_price_delta_diag(
          f->baseline->q,f->baseline_g,strip,ASIAN_GENUINE_STRIP_GEOMETRIC_CV,4u,baseline):
          asian_genuine_strip_price_diag(f->baseline->q,f->baseline_g,strip,
            ASIAN_GENUINE_STRIP_GEOMETRIC_CV,4u,baseline);
        if(a!=0||asian_geometric_cv_packet_local_strip_consume_padded(
             f->q,f->g,strip,count,price_delta,candidate)!=0||
           memcmp(baseline->values,candidate->values,
             count*sizeof(baseline->values[0]))!=0||
           input_hash!=hash_bytes(UINT64_C(1469598103934665603),strip,sizeof(*strip))){
            fprintf(stderr,"consumer mismatch N=%u c=%u K=%u mode=%d\n",
              future,completed,count,price_delta);
            free(candidate);free(baseline);free(strip);return-1;
        }
    }
    free(candidate);free(baseline);free(strip);return 0;
}

static int check_trace(fixture_t *f,uint32_t packet)
{
    memset(f->trace,0,sizeof(*f->trace));
    asian_geometric_cv_packet_local_packet_probe_diag(f->context,packet,f->trace);
    for(uint32_t lane=0;lane<32u;++lane){
        const uint32_t path=packet*32u+lane;float s=100.0f,q=0.0f,l=0.0f;
        for(uint32_t fixing=0;fixing<f->context->fixing_count;++fixing){
            const asian_meta_affine_route_t *route=&f->meta_routes[fixing];
            const uint32_t source=meta_selected_source(route,path);
            float weight;memcpy(&weight,&route->weight_bits,4u);
            s=rmul(s,route->growth_base[source]);q=radd(q,s);
            l=fmaf(weight,route->x_base[source],l);
            if(fbits(s)!=fbits(f->trace->s[fixing][lane])||
               fbits(q)!=fbits(f->trace->q[fixing][lane])||
               fbits(l)!=fbits(f->trace->l[fixing][lane]))return-1;
        }
    }
    return 0;
}

static int evolve_compare(fixture_t *f,const market_t *market,uint32_t future,
    uint32_t completed,double q0,double log_sum,uint32_t count,int trace)
{
    asian_genuine_strip_context_t *strip=a64(sizeof(*strip));float fixed[32];
    if(!strip||asian_genuine_strip_fixed_strikes(32u,fixed)!=0||
       asian_genuine_strip_prepare(strip,100.0,market->rate,market->dividend,
         market->sigma,market->maturity,future,completed,q0,log_sum,fixed,32u)!=0){
        free(strip);return-1;
    }
    memset(f->q_storage,0xa5,PATHS*4u+2u*GUARD);
    memset(f->g_storage,0xa5,PATHS*4u+2u*GUARD);
    initial_state(f->baseline);
    const uint64_t route_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->routes,future*sizeof(*f->routes));
    const uint64_t meta_route_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->meta_routes,future*sizeof(*f->meta_routes));
    const uint64_t map_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->maps,future*sizeof(*f->maps));
    const uint64_t x_hash=hash_bytes(UINT64_C(1469598103934665603),f->x,2u*PATHS*4u);
    const uint64_t growth_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->growth,2u*PATHS*4u);
    asian_genuine_sql_dual_control_diag(f->routes,future,f->baseline);
    if(asian_genuine_strip_exp_preflight(strip,f->baseline->l,NULL,NULL)!=0){free(strip);return-1;}
    asian_genuine_strip_l_to_g_diag(f->baseline->l,strip,f->baseline_g);
    if(asian_geometric_cv_packet_local_prepare(f->context,f->meta_routes,future,
         100.0f,f->x,2u*PATHS*4u,f->growth,2u*PATHS*4u,strip,
         f->q,PATHS*4u,f->g,PATHS*4u)!=0){free(strip);return-1;}
    if(f->context->d1_weight_bits!=f->meta_routes[0].weight_bits||
       f->context->terminal_log_base_bits!=fbits(strip->log_base)){free(strip);return-1;}
    const uint64_t context_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->context,sizeof(*f->context));
    asian_geometric_cv_packet_local_qg_diag(f->context);
    if(memcmp(f->q,f->baseline->q,PATHS*4u)!=0||
       memcmp(f->g,f->baseline_g,PATHS*4u)!=0){
        fprintf(stderr,"terminal Q/G mismatch N=%u c=%u\n",future,completed);
        free(strip);return-1;
    }
    float *repeat_q=a64(PATHS*4u),*repeat_g=a64(PATHS*4u);
    if(!repeat_q||!repeat_g){free(repeat_g);free(repeat_q);free(strip);return-1;}
    memcpy(repeat_q,f->q,PATHS*4u);memcpy(repeat_g,f->g,PATHS*4u);
    asian_geometric_cv_packet_local_qg_diag(f->context);
    if(memcmp(repeat_q,f->q,PATHS*4u)||memcmp(repeat_g,f->g,PATHS*4u)){
        free(repeat_g);free(repeat_q);free(strip);return-1;}
    free(repeat_g);free(repeat_q);
    for(uint32_t i=0;i<GUARD;++i)
        if(f->q_storage[i]!=0xa5u||f->g_storage[i]!=0xa5u||
           f->q_storage[GUARD+PATHS*4u+i]!=0xa5u||
           f->g_storage[GUARD+PATHS*4u+i]!=0xa5u){free(strip);return-1;}
    if(route_hash!=hash_bytes(UINT64_C(1469598103934665603),f->routes,
         future*sizeof(*f->routes))||
       map_hash!=hash_bytes(UINT64_C(1469598103934665603),f->maps,
         future*sizeof(*f->maps))||
       meta_route_hash!=hash_bytes(UINT64_C(1469598103934665603),
         f->meta_routes,future*sizeof(*f->meta_routes))||
       x_hash!=hash_bytes(UINT64_C(1469598103934665603),f->x,2u*PATHS*4u)||
       growth_hash!=hash_bytes(UINT64_C(1469598103934665603),f->growth,2u*PATHS*4u)||
       context_hash!=hash_bytes(UINT64_C(1469598103934665603),f->context,
         sizeof(*f->context))){free(strip);return-1;}
    if(trace&&check_trace(f,(future*37u+completed*13u)&127u)!=0){free(strip);return-1;}
    free(strip);
    return compare_output_case(f,market,future,completed,q0,log_sum,count);
}

static int negative_tests(fixture_t *f)
{
    const market_t *market=&markets[2];
    if(prepare_shared_routes(f,2u,2u)!=0||produce(f,market,2u)!=0)return-1;
    float strikes[32];asian_genuine_strip_fixed_strikes(32u,strikes);
    asian_genuine_strip_context_t *strip=a64(sizeof(*strip));
    if(!strip||asian_genuine_strip_prepare(strip,100,.03,0,.20,1,2,0,0,0,
       strikes,32u)!=0)return-1;
#define REJECT(call,want) do{if((call)!=(want)){free(strip);return-1;}}while(0)
    REJECT(asian_geometric_cv_packet_local_prepare(NULL,f->meta_routes,2,100,f->x,
      32768,f->growth,32768,strip,f->q,16384,f->g,16384),
      ASIAN_GEOMETRIC_CV_PACKET_LOCAL_INVALID);
    REJECT(asian_geometric_cv_packet_local_prepare(f->context,f->meta_routes,1,100,
      f->x,32768,f->growth,32768,strip,f->q,16384,f->g,16384),
      ASIAN_GEOMETRIC_CV_PACKET_LOCAL_FIXINGS_UNSUPPORTED);
    REJECT(asian_geometric_cv_packet_local_prepare(f->context,f->meta_routes,257,100,
      f->x,32768,f->growth,32768,strip,f->q,16384,f->g,16384),
      ASIAN_GEOMETRIC_CV_PACKET_LOCAL_FIXINGS_UNSUPPORTED);
    REJECT(asian_geometric_cv_packet_local_prepare(f->context,f->meta_routes,2,100,
      f->x,32768,f->growth,32768,strip,(float *)((char *)f->q+4),16384,f->g,16384),
      ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ALIGNMENT);
    REJECT(asian_geometric_cv_packet_local_prepare(f->context,f->meta_routes,2,100,
      f->x,32768,f->growth,32768,strip,f->q,16384,f->q,16384),
      ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ALIAS);
    const float saved=f->growth[0];f->growth[0]=0.0f;
    REJECT(asian_geometric_cv_packet_local_prepare(f->context,f->meta_routes,2,100,
      f->x,32768,f->growth,32768,strip,f->q,16384,f->g,16384),
      ASIAN_GEOMETRIC_CV_PACKET_LOCAL_DOMAIN);f->growth[0]=saved;
#undef REJECT
    free(strip);return 0;
}

static int one_future_consumer(fixture_t *f)
{
    static const uint32_t totals[]={16u,32u,64u,128u,256u};
    for(uint32_t m=0;m<4u;++m){
        const market_t *market=&markets[m];
        asian_genuine_fixed_block_source_context_t source __attribute__((aligned(64)));
        const double dt=market->maturity;
        memset(&source,0,sizeof(source));
        source.signed_z=asian_genuine_fixed_block_signed_z;
        source.drift=(float)((market->rate-market->dividend-
          0.5*market->sigma*market->sigma)*dt);
        source.diffusion=(float)(market->sigma*sqrt(dt));
        source.magic=ASIAN_GENUINE_FIXED_BLOCK_SOURCE_MAGIC;
        source.abi_version=ASIAN_GENUINE_FIXED_BLOCK_ABI_VERSION;
        asian_genuine_fixed_block_signed_z_one_fma_source_diag(&source,f->x);
        asian_vector_exp_range_reduced_array_diag(f->x,f->growth);
        for(uint32_t ti=0;ti<5u;++ti){
            const uint32_t total=totals[ti],completed=total-1u;
            if(prepare_shared_routes(f,1u,total)!=0)return-1;
            float weight;memcpy(&weight,&f->routes[0].weight_bits,4u);
            for(uint32_t path=0;path<PATHS;++path){
                f->q[path]=rmul(100.0f,f->growth[path]);
                f->baseline->l[path]=fmaf(weight,f->x[path],0.0f);
            }
            for(uint32_t shape=0;shape<4u;++shape){
                double q0,logs;history(completed,shape,&q0,&logs);
                float strikes[32];
                asian_genuine_strip_context_t *prepared=a64(sizeof(*prepared));
                if(!prepared||asian_genuine_strip_fixed_strikes(32u,strikes)!=0||
                   asian_genuine_strip_prepare(prepared,100.0,market->rate,
                     market->dividend,market->sigma,market->maturity,1u,
                     completed,q0,logs,strikes,32u)!=0){free(prepared);return-1;}
                asian_genuine_strip_l_to_g_diag(f->baseline->l,prepared,f->g);
                free(prepared);memcpy(f->baseline->q,f->q,PATHS*4u);
                memcpy(f->baseline_g,f->g,PATHS*4u);
                if(compare_output_case(f,market,1u,completed,q0,logs,
                     1u+(m*5u+ti*7u+shape*11u)%32u)!=0)return-1;
            }
        }
    }
    return 0;
}

int main(int argc,char **argv)
{
    if(argc!=1){fprintf(stderr,"usage: %s\n",argv[0]);return 2;}
    fixture_t f;if(fixture_init(&f)!=0){fprintf(stderr,"fixture setup failed\n");return 2;}
    if(negative_tests(&f)!=0){fprintf(stderr,"preparation rejection gate failed\n");return 1;}
    for(uint32_t n=2u;n<=256u;++n){
        if(prepare_shared_routes(&f,n,n)!=0){fprintf(stderr,"route N=%u\n",n);return 1;}
        for(uint32_t m=0;m<sizeof(markets)/sizeof(markets[0]);++m){
            if(produce(&f,&markets[m],n)!=0||evolve_compare(&f,&markets[m],n,0u,
                 0.0,0.0,1u+(n+m*7u-2u)%32u,1)!=0){
                fprintf(stderr,"matrix failure N=%u market=%u\n",n,m);return 1;
            }
        }
    }
    static const uint32_t totals[]={16u,32u,64u,128u,256u};
    for(uint32_t ti=0;ti<5u;++ti){
        const uint32_t total=totals[ti];
        const uint32_t completed[]={0u,1u,total/4u,total/2u,total-2u};
        for(uint32_t ci=0;ci<5u;++ci){
            const uint32_t c=completed[ci],future=total-c;
            if(future<2u||prepare_shared_routes(&f,future,total)!=0||
               produce(&f,&markets[(ti+ci)%4u],future)!=0)return 1;
            for(uint32_t shape=0;shape<4u;++shape){
                double q0,logs;history(c,shape,&q0,&logs);
                if(evolve_compare(&f,&markets[(ti+ci)%4u],future,c,q0,logs,
                     1u+(ti*7u+ci*5u+shape*11u)%32u,1)!=0)return 1;
            }
        }
    }
    if(one_future_consumer(&f)!=0){fprintf(stderr,"one-future consumer failed\n");return 1;}
    puts("packet_local_qg N=2..256 PASS");
    puts("geometric_cv_price K=1..32 PASS");
    puts("geometric_cv_price_delta K=1..32 PASS");
    puts("seasoning PASS");
    fixture_release(&f);return 0;
}
