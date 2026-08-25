#define main asian_geometric_cv_packet_local_parent_test_main
#include "test_asian_geometric_cv_packet_local.c"
#undef main

#include "private/asian_geometric_cv_immediate_diag.h"

#ifdef ASIAN_GEOMETRIC_CV_IMMEDIATE_EMBEDDED
#define main asian_geometric_cv_immediate_embedded_correctness_main
#endif

typedef struct {
    unsigned char *storage;
    asian_genuine_strip_output_t *value;
} guarded_output_t;

static int guarded_output_init(guarded_output_t *out)
{
    out->storage=a64(sizeof(*out->value)+128u);
    if(out->storage==NULL)return-1;
    out->value=(asian_genuine_strip_output_t *)(out->storage+64u);
    memset(out->storage,0xa5,sizeof(*out->value)+128u);
    memset(out->value,0,sizeof(*out->value));
    return 0;
}

static int guarded_output_ok(const guarded_output_t *out)
{
    for(uint32_t i=0;i<64u;++i)
        if(out->storage[i]!=0xa5u||
           out->storage[64u+sizeof(*out->value)+i]!=0xa5u)return 0;
    return 1;
}

static void guarded_output_release(guarded_output_t *out)
{
    free(out->storage);memset(out,0,sizeof(*out));
}

static void immediate_strikes(float out[4],uint32_t count,float kink,
                              uint32_t variant)
{
    const float down=nextafterf(kink,-INFINITY)>0.0f?
      nextafterf(kink,-INFINITY):0.5f;
    const float up=nextafterf(kink,INFINITY);
    if(count==1u)out[0]=variant%3u==0u?kink:variant%3u==1u?up:down;
    else if(count==2u){out[0]=variant&1u?up:kink;out[1]=variant&1u?kink:down;}
    else if(count==3u){out[0]=up;out[1]=down;out[2]=kink;}
    else{out[0]=up;out[1]=61.25f;out[2]=kink;out[3]=down;}
}

static int prepare_terminal(fixture_t *f,const market_t *market,uint32_t future,
    uint32_t completed,double q0,double log_sum)
{
    float fixed[32];
    asian_genuine_strip_context_t *strip=a64(sizeof(*strip));
    if(strip==NULL||asian_genuine_strip_fixed_strikes(32u,fixed)!=0||
       asian_genuine_strip_prepare(strip,100.0,market->rate,market->dividend,
         market->sigma,market->maturity,future,completed,q0,log_sum,
         fixed,32u)!=0){free(strip);return-1;}
    initial_state(f->baseline);
    asian_genuine_sql_dual_control_diag(f->routes,future,f->baseline);
    if(asian_genuine_strip_exp_preflight(strip,f->baseline->l,NULL,NULL)!=0){
        free(strip);return-1;}
    asian_genuine_strip_l_to_g_diag(f->baseline->l,strip,f->baseline_g);
    memset(f->q_storage,0xa5,PATHS*4u+2u*GUARD);
    memset(f->g_storage,0xa5,PATHS*4u+2u*GUARD);
    if(asian_geometric_cv_packet_local_prepare(f->context,f->meta_routes,future,
         100.0f,f->x,2u*PATHS*4u,f->growth,2u*PATHS*4u,strip,
         f->q,PATHS*4u,f->g,PATHS*4u)!=0){free(strip);return-1;}
    asian_geometric_cv_packet_local_qg_diag(f->context);
    const int exact=memcmp(f->q,f->baseline->q,PATHS*4u)==0&&
                    memcmp(f->g,f->baseline_g,PATHS*4u)==0;
    free(strip);return exact?0:-1;
}

static int check_immediate_shape(fixture_t *f,const market_t *market,
    uint32_t future,uint32_t completed,double q0,double log_sum,
    uint32_t count,int price_delta,uint32_t variant)
{
    asian_genuine_strip_context_t *strip=a64(sizeof(*strip));
    asian_geometric_cv_immediate_context_t *immediate=a64(sizeof(*immediate));
    guarded_output_t sql={0},qg={0},candidate={0},repeat={0};
    float strikes[4];uint32_t padded=0;
    const float kink=(float)((q0+f->baseline->q[(future*37u+count*11u)&4095u])/
                             (double)(future+completed));
    immediate_strikes(strikes,count,kink,variant);
    if(strip==NULL||immediate==NULL||guarded_output_init(&sql)!=0||
       guarded_output_init(&qg)!=0||guarded_output_init(&candidate)!=0||
       guarded_output_init(&repeat)!=0||
       asian_geometric_cv_packet_local_strip_prepare_padded(strip,100.0,
         market->rate,market->dividend,market->sigma,market->maturity,
         future,completed,q0,log_sum,strikes,count,&padded)!=0||
       padded!=(count==1u?1u:4u))goto fail;

    const uint64_t strip_hash=hash_bytes(UINT64_C(1469598103934665603),
      strip,sizeof(*strip));
    const uint64_t route_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->routes,future*sizeof(*f->routes));
    const uint64_t meta_route_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->meta_routes,future*sizeof(*f->meta_routes));
    const uint64_t map_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->maps,future*sizeof(*f->maps));
    const uint64_t x_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->x,2u*PATHS*4u);
    const uint64_t growth_hash=hash_bytes(UINT64_C(1469598103934665603),
      f->growth,2u*PATHS*4u);

    const int sql_status=price_delta?asian_genuine_strip_price_delta_diag(
      f->baseline->q,f->baseline_g,strip,ASIAN_GENUINE_STRIP_GEOMETRIC_CV,
      4u,sql.value):asian_genuine_strip_price_diag(f->baseline->q,
      f->baseline_g,strip,ASIAN_GENUINE_STRIP_GEOMETRIC_CV,4u,sql.value);
    if(sql_status!=0||asian_geometric_cv_packet_local_strip_consume_padded(
         f->q,f->g,strip,count,price_delta,qg.value)!=0||
       memcmp(sql.value->values,qg.value->values,
              count*sizeof(sql.value->values[0]))!=0)goto mismatch;

    /* Poison the Stage-1 Q/G boundary.  Immediate preparation must not read it. */
    asian_geometric_cv_packet_local_context_t qualified=*f->context;
    qualified.q_out=(float *)(uintptr_t)UINT64_C(0x1111111111111111);
    qualified.g_out=(float *)(uintptr_t)UINT64_C(0x2222222222222222);
    if(asian_geometric_cv_immediate_prepare(immediate,&qualified,strip)!=
         ASIAN_GEOMETRIC_CV_IMMEDIATE_OK||
       immediate->d1_weight_bits!=f->meta_routes[0].weight_bits||
       immediate->terminal_log_base_bits!=fbits(strip->log_base))goto fail;
    const uint64_t immediate_hash=hash_bytes(UINT64_C(1469598103934665603),
      immediate,sizeof(*immediate));
    if(asian_geometric_cv_immediate_run(immediate,strip,count,price_delta,
         candidate.value)!=ASIAN_GEOMETRIC_CV_IMMEDIATE_OK||
       memcmp(qg.value->values,candidate.value->values,
              count*sizeof(qg.value->values[0]))!=0)goto mismatch;
    if(asian_geometric_cv_immediate_run(immediate,strip,count,price_delta,
         repeat.value)!=ASIAN_GEOMETRIC_CV_IMMEDIATE_OK||
       memcmp(candidate.value,repeat.value,sizeof(*candidate.value))!=0)
        goto mismatch;

    if(!guarded_output_ok(&sql)||!guarded_output_ok(&qg)||
       !guarded_output_ok(&candidate)||!guarded_output_ok(&repeat)||
       strip_hash!=hash_bytes(UINT64_C(1469598103934665603),strip,sizeof(*strip))||
       route_hash!=hash_bytes(UINT64_C(1469598103934665603),f->routes,
          future*sizeof(*f->routes))||
       meta_route_hash!=hash_bytes(UINT64_C(1469598103934665603),
          f->meta_routes,future*sizeof(*f->meta_routes))||
       map_hash!=hash_bytes(UINT64_C(1469598103934665603),f->maps,
          future*sizeof(*f->maps))||
       x_hash!=hash_bytes(UINT64_C(1469598103934665603),f->x,2u*PATHS*4u)||
       growth_hash!=hash_bytes(UINT64_C(1469598103934665603),
          f->growth,2u*PATHS*4u)||
       immediate_hash!=hash_bytes(UINT64_C(1469598103934665603),
          immediate,sizeof(*immediate)))goto fail;

    guarded_output_release(&repeat);guarded_output_release(&candidate);
    guarded_output_release(&qg);guarded_output_release(&sql);
    free(immediate);free(strip);return 0;

mismatch:
    fprintf(stderr,"immediate mismatch N=%u c=%u K=%u workload=%s variant=%u\n",
      future,completed,count,price_delta?"price_delta":"price",variant);
    for(uint32_t debug=0;debug<count;++debug)
        fprintf(stderr,"K%u sql=%a/%a/%a/%a qg=%a/%a/%a/%a cand=%a/%a/%a/%a\n",
          debug,sql.value->values[debug].call_price,
          sql.value->values[debug].put_price,sql.value->values[debug].call_delta,
          sql.value->values[debug].put_delta,qg.value->values[debug].call_price,
          qg.value->values[debug].put_price,qg.value->values[debug].call_delta,
          qg.value->values[debug].put_delta,candidate.value->values[debug].call_price,
          candidate.value->values[debug].put_price,
          candidate.value->values[debug].call_delta,
          candidate.value->values[debug].put_delta);
fail:
    guarded_output_release(&repeat);guarded_output_release(&candidate);
    guarded_output_release(&qg);guarded_output_release(&sql);
    free(immediate);free(strip);return-1;
}

static int check_all_shapes(fixture_t *f,const market_t *market,uint32_t future,
    uint32_t completed,double q0,double log_sum,uint32_t variant)
{
    if(prepare_terminal(f,market,future,completed,q0,log_sum)!=0)return-1;
    for(uint32_t count=1u;count<=4u;++count)
        for(int price_delta=0;price_delta<=1;++price_delta)
            if(check_immediate_shape(f,market,future,completed,q0,log_sum,
                 count,price_delta,variant+count)!=0)return-1;
    return 0;
}

static int immediate_negative_tests(fixture_t *f)
{
    if(prepare_shared_routes(f,2u,2u)!=0||produce(f,&markets[2],2u)!=0||
       prepare_terminal(f,&markets[2],2u,0u,0.0,0.0)!=0)return-1;
    float strike=100.0f;uint32_t padded=0;
    asian_genuine_strip_context_t *strip=a64(sizeof(*strip));
    asian_geometric_cv_immediate_context_t *context=a64(sizeof(*context));
    if(strip==NULL||context==NULL||
       asian_geometric_cv_packet_local_strip_prepare_padded(strip,100.0,.03,0,
         .20,1.0,2u,0u,0.0,0.0,&strike,1u,&padded)!=0)return-1;
#define REJECT(call,want) do{if((call)!=(want)){free(context);free(strip);return-1;}}while(0)
    REJECT(asian_geometric_cv_immediate_prepare(NULL,f->context,strip),
           ASIAN_GEOMETRIC_CV_IMMEDIATE_INVALID);
    REJECT(asian_geometric_cv_immediate_prepare(
             (asian_geometric_cv_immediate_context_t *)((char *)context+4),
             f->context,strip),ASIAN_GEOMETRIC_CV_IMMEDIATE_ALIGNMENT);
    asian_geometric_cv_packet_local_context_t bad=*f->context;bad.magic=0u;
    REJECT(asian_geometric_cv_immediate_prepare(context,&bad,strip),
           ASIAN_GEOMETRIC_CV_IMMEDIATE_CONTEXT);
    REJECT(asian_geometric_cv_immediate_prepare(context,f->context,strip),
           ASIAN_GEOMETRIC_CV_IMMEDIATE_OK);
    asian_genuine_strip_output_t *output=a64(sizeof(*output));
    if(output==NULL){free(context);free(strip);return-1;}
    REJECT(asian_geometric_cv_immediate_run(context,strip,5u,0,output),
           ASIAN_GEOMETRIC_CV_IMMEDIATE_FALLBACK);
    free(output);
#undef REJECT
    free(context);free(strip);return 0;
}

int main(int argc,char **argv)
{
    if(argc!=1){fprintf(stderr,"usage: %s\n",argv[0]);return 2;}
    fixture_t f;
    if(fixture_init(&f)!=0){fprintf(stderr,"fixture setup failed\n");return 2;}
    if(immediate_negative_tests(&f)!=0){
        fprintf(stderr,"immediate preparation rejection gate failed\n");return 1;}
    for(uint32_t n=2u;n<=256u;++n){
        if(prepare_shared_routes(&f,n,n)!=0){fprintf(stderr,"route N=%u\n",n);return 1;}
        for(uint32_t m=0;m<4u;++m){
            if(produce(&f,&markets[m],n)!=0||
               check_all_shapes(&f,&markets[m],n,0u,0.0,0.0,n+m)!=0){
                fprintf(stderr,"matrix failure N=%u market=%u\n",n,m);return 1;}
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
                double q0,log_sum;history(c,shape,&q0,&log_sum);
                if(check_all_shapes(&f,&markets[(ti+ci)%4u],future,c,q0,
                     log_sum,ti+ci+shape)!=0)return 1;
            }
        }
    }
    if(one_future_consumer(&f)!=0){
        fprintf(stderr,"one-future consumer failed\n");return 1;}
    puts("immediate_cv_price K=1..4 PASS");
    puts("immediate_cv_price_delta K=1..4 PASS");
    puts("seasoning PASS");
    fixture_release(&f);return 0;
}

#ifdef ASIAN_GEOMETRIC_CV_IMMEDIATE_EMBEDDED
#undef main
#endif
