#define _GNU_SOURCE
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_geometric_cv_diag.h"
#include "private/asian_genuine_aad_phase1_diag.h"
#include "private/asian_genuine_price_delta_strip_diag.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <mkl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#ifndef PHASE1_GIT_COMMIT
#define PHASE1_GIT_COMMIT "unknown"
#endif

enum { PATHS=4096,SAMPLES=51,WARMUPS=16,CANDIDATES=7,VALUATIONS=7 };
enum candidate { EXISTING_PRICE,EXISTING_PRICE_DELTA,TARGETED_FORWARD,
                 CONTRACTED_SUFFIX,CRN_OURS,CRN_ONEMKL,GENERIC_REVERSE };
enum fixture_prepare_status {
 PREPARE_OK=0,PREPARE_FIXING_COUNT,PREPARE_DIRECTIONS,PREPARE_ALLOCATIONS,
 PREPARE_PRODUCER_ADAPTER,PREPARE_PRODUCER,PREPARE_ROUTE_TARGET,
 PREPARE_ROUTE,PREPARE_CONTROLS,PREPARE_CONTEXT,PREPARE_PAYOFF,
 PREPARE_STRIP,PREPARE_ONEMKL
};
static const char *const names[CANDIDATES]={
 "existing_price_only","existing_price_plus_delta",
 "targeted_forward_price_delta_vega_rho",
 "contracted_suffix_price_delta_vega_rho",
 "crn_ours_seven_valuation","crn_onemkl_seven_valuation",
 "generic_reverse_unranked_diagnostic"};
static const uint32_t ns[]={16,32,64,128,256};

typedef struct {
 uint32_t n,side,cv,directions[256][32],*words[2],*pressure;
 float *x,*growth,*g,*weights,*z,*pm,*tape;
 float (*basis)[PATHS];
 fragment_map_t *maps;asian_genuine_route_t *routes;
 asian_genuine_state_t *states;
 ordered_d1_diag_context_t *producers[5];
 asian_genuine_aad_phase1_controls_t *controls;
 asian_genuine_aad_phase1_context_t *context;
 asian_genuine_aad_phase1_value_t *risk;
 asian_geometric_cv_context_t *payoffs;
 asian_genuine_strip_context_t *strip;
 asian_genuine_strip_output_t *strip_output;
 VSLStreamStatePtr base,work;
 double bump_spot,bump_sigma,bump_rate;
 int mkl_status;
} fixture_t;

static fixture_t b;static volatile double sink;
static void *a64(size_t n){void*p=0;if(posix_memalign(&p,64,n))return 0;memset(p,0,n);return p;}
static uint32_t sobol(uint32_t i,const uint32_t*v){uint32_t g=i^(i>>1),w=0;for(uint32_t k=0;g;++k,g>>=1)if(g&1)w^=v[k];return w;}
static uint64_t tsc0(void){_mm_lfence();return __rdtsc();}
static uint64_t tsc1(void){unsigned a;uint64_t t=__rdtscp(&a);_mm_lfence();return t;}
static uint64_t wall_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC_RAW,&t))abort();return(uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static int cmp64(const void*a,const void*c){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)c;return(x>y)-(x<y);}
static uint64_t quantile(const uint64_t*x,uint32_t rank){uint64_t y[SAMPLES];memcpy(y,x,sizeof y);qsort(y,SAMPLES,8,cmp64);return y[rank];}
static uint64_t rng_step(uint64_t*x){*x^=*x<<13;*x^=*x>>7;return *x^=*x<<17;}
static void shuffle(uint32_t*x,uint64_t*seed){for(uint32_t i=0;i<CANDIDATES;++i)x[i]=i;for(uint32_t i=CANDIDATES-1;i;--i){uint32_t j=(uint32_t)(rng_step(seed)%(i+1)),q=x[i];x[i]=x[j];x[j]=q;}}
static int first_cpu(void){cpu_set_t a,p;CPU_ZERO(&a);if(sched_getaffinity(0,sizeof a,&a))return-1;int c=0;while(c<CPU_SETSIZE&&!CPU_ISSET(c,&a))++c;if(c==CPU_SETSIZE)return-1;CPU_ZERO(&p);CPU_SET(c,&p);return sched_setaffinity(0,sizeof p,&p)?-1:c;}

static int load_directions(void){FILE*f=fopen("direction_numbers/joe_kuo_6_21201.bin","rb");if(!f)return-1;for(uint32_t d=0;d<256;++d){uint32_t n;if(fread(&n,4,1,f)!=1||n!=32||fread(b.directions[d],4,32,f)!=32){fclose(f);return-1;}}fclose(f);return 0;}
static void produce(const ordered_d1_diag_context_t*p){ordered_d1_x_only_diag(256,p,b.x);asian_vector_exp_range_reduced_array_diag(b.x,b.growth);asian_vector_exp_range_reduced_array_diag(b.x+PATHS,b.growth+PATHS);}
static void init_state(asian_genuine_state_t*s,float s0){memset(s,0,sizeof(*s));for(uint32_t i=0;i<PATHS;++i)s->s[i]=s0;}
static double state_price(uint32_t i){return b.cv?asian_geometric_cv_payoff_reduce_diag(b.states[i].q,b.states[i].l,&b.payoffs[i]):asian_arithmetic_payoff_reduce_diag(b.states[i].q,&b.payoffs[i]);}

static int prepare_mkl(void){size_t count=3+(size_t)b.n*32;MKL_UINT*p=calloc(count,sizeof(*p));if(!p)return-1;p[0]=b.n;p[1]=VSL_USER_QRNG_INITIAL_VALUES;p[2]=VSL_USER_DIRECTION_NUMBERS;for(uint32_t d=0;d<b.n;++d)for(uint32_t k=0;k<32;++k)p[3+d*32+k]=b.directions[d][k];int e=vslNewStreamEx(&b.base,VSL_BRNG_SOBOL,count,p);free(p);if(e||vslSkipAheadStream(b.base,(long long)(8192-1)*b.n)||vslCopyStream(&b.work,b.base))return-1;VSLStreamStatePtr check=0;if(vslCopyStream(&check,b.base))return-1;uint32_t*raw=a64((size_t)b.n*PATHS*4);if(!raw)return-1;e=viRngUniformBits(VSL_RNG_METHOD_UNIFORMBITS_STD,check,b.n*PATHS,raw);for(uint32_t path=0;!e&&path<PATHS;++path)for(uint32_t d=0;d<b.n;++d)if(raw[(size_t)path*b.n+d]!=sobol(8192+path,b.directions[d]))e=-1;free(raw);vslDeleteStream(&check);return e;}

static void release_fixture(void){if(b.work)vslDeleteStream(&b.work);if(b.base)vslDeleteStream(&b.base);free(b.strip_output);free(b.strip);free(b.payoffs);free(b.risk);free(b.context);free(b.controls);for(int i=0;i<5;++i)free(b.producers[i]);free(b.states);free(b.routes);free(b.maps);free(b.basis);free(b.tape);free(b.pm);free(b.z);free(b.weights);free(b.g);free(b.growth);free(b.x);free(b.pressure);free(b.words[1]);free(b.words[0]);memset(&b,0,sizeof b);}

static int preparation_error(enum fixture_prepare_status status,
                             const char *stage, int detail, int item)
{
 fprintf(stderr,"asian_genuine_aad_phase1 preparation_error stage=%s status=%d "
   "detail=%d item=%d N=%u side=%s estimator=%s\n",stage,status,detail,item,
   b.n,b.side?"put":"call",b.cv?"geometric_cv":"arithmetic");
 release_fixture();
 return status;
}

static int prepare_fixture(uint32_t n,uint32_t side,uint32_t cv)
{
 memset(&b,0,sizeof b);b.n=n;b.side=side;b.cv=cv;
 b.bump_spot=.1;b.bump_sigma=.0002;b.bump_rate=.00003;
 if(n<ASIAN_GENUINE_AAD_PHASE1_MIN_FIXINGS||
    n>ASIAN_GENUINE_AAD_PHASE1_MAX_FIXINGS)
  return preparation_error(PREPARE_FIXING_COUNT,"fixing_count",
    ASIAN_GENUINE_AAD_PHASE1_FIXING_COUNT_UNSUPPORTED,-1);
 if(load_directions())
  return preparation_error(PREPARE_DIRECTIONS,"direction_table",-1,-1);
 b.words[0]=a64(16384);b.words[1]=a64(16384);b.pressure=a64(32768);
 b.x=a64(32768);b.growth=a64(32768);b.g=a64(16384);b.weights=a64(n*4);
 b.z=a64((size_t)n*16384);b.pm=a64((size_t)n*16384);
 b.tape=a64(ASIAN_GENUINE_AAD_PHASE1_TAPE_BYTES);
 b.basis=a64((size_t)ASIAN_GENUINE_AAD_PHASE1_BASIS_FIELDS*PATHS*4);
 b.maps=a64((size_t)n*sizeof(*b.maps));
 b.routes=a64((size_t)n*sizeof(*b.routes));
 b.states=a64((size_t)VALUATIONS*sizeof(*b.states));
 for(int i=0;i<5;++i)b.producers[i]=a64(sizeof(**b.producers));
 b.controls=a64(sizeof(*b.controls));b.context=a64(sizeof(*b.context));
 b.risk=a64(sizeof(*b.risk));
 b.payoffs=a64((size_t)VALUATIONS*sizeof(*b.payoffs));
 b.strip=a64(sizeof(*b.strip));b.strip_output=a64(sizeof(*b.strip_output));
 if(!b.words[0]||!b.words[1]||!b.pressure||!b.x||!b.growth||!b.g||
    !b.weights||!b.z||!b.pm||!b.tape||!b.basis||!b.maps||!b.routes||
    !b.states||!b.controls||!b.context||!b.risk||!b.payoffs||!b.strip||
    !b.strip_output)
  return preparation_error(PREPARE_ALLOCATIONS,"aligned_allocations",-1,-1);
 for(int i=0;i<5;++i)if(!b.producers[i])
  return preparation_error(PREPARE_ALLOCATIONS,"producer_allocation",-1,i);
 for(uint32_t i=0;i<8192;++i)b.pressure[i]=i;
 for(uint32_t i=0;i<PATHS;++i){
  b.words[0][i]=sobol(8192+i,b.directions[0]);
  b.words[1][i]=sobol(12288+i,b.directions[0]);
 }
 const double s0[VALUATIONS]={100,100.1,99.9,100,100,100,100};
 const double r[VALUATIONS]={.03,.03,.03,.03003,.02997,.03,.03};
 const double sig[VALUATIONS]={.20,.20,.20,.20,.20,.2002,.1998};
 const uint32_t pn=asian_genuine_aad_phase1_producer_fixing_count(n);
 if(pn==0)
  return preparation_error(PREPARE_PRODUCER_ADAPTER,"producer_adapter",0,-1);
 const uint32_t pi[5]={0,3,4,5,6};
 for(int j=0;j<5;++j){
  const uint32_t i=pi[j];const double dt=1.0/n;
  const float drift=(float)((r[i]-0.0-.5*sig[i]*sig[i])*dt);
  const float diff=(float)(sig[i]*sqrt(dt));
  const int status=ordered_d1_diag_prepare(b.producers[j],drift,diff,8192,
    ORDERED_D1_DIAG_PREPARE_X3,pn);
  if(status)
   return preparation_error(PREPARE_PRODUCER,"ordered_d1_diag_prepare",
     status,j);
 }
 produce(b.producers[0]);
 const uint32_t*sw[2]={b.words[0],b.words[1]};
 const float*xb[2]={b.x,b.x+PATHS},*gb[2]={b.growth,b.growth+PATHS};
 uint32_t*target=a64(16384);
 if(!target)
  return preparation_error(PREPARE_ROUTE_TARGET,"route_target_allocation",-1,-1);
 for(uint32_t k=0;k<n;++k){
  for(uint32_t i=0;i<PATHS;++i)target[i]=sobol(8192+i,b.directions[k]);
  const int status=asian_genuine_prepare_route(sw,2,xb,gb,target,k,n,
    &b.maps[k],&b.routes[k]);
  if(status){
   free(target);
   return preparation_error(PREPARE_ROUTE,"route_preparation",status,(int)k);
  }
  memcpy(&b.weights[k],&b.routes[k].weight_bits,4);
 }
 free(target);
 int status=asian_genuine_aad_phase1_prepare_controls(b.controls,100,100,
   .03,0.0,.20,1.0,n);
 if(status)
  return preparation_error(PREPARE_CONTROLS,"analytic_controls",status,-1);
 status=asian_genuine_aad_phase1_prepare_context(b.context,b.routes,b.tape,
   b.controls,100,100,.03,0.0,.20,1.0,n);
 if(status)
  return preparation_error(PREPARE_CONTEXT,"hot_context",status,-1);
 for(uint32_t i=0;i<VALUATIONS;++i){
  init_state(&b.states[i],(float)s0[i]);
  status=asian_geometric_cv_prepare(&b.payoffs[i],s0[i],100,r[i],0.0,sig[i],
    1.0,n,0,0,0,side?ASIAN_GEOMETRIC_PUT:ASIAN_GEOMETRIC_CALL);
  if(status)
   return preparation_error(PREPARE_PAYOFF,"geometric_payoff",status,(int)i);
 }
 float strike=100;
 status=asian_genuine_strip_prepare(b.strip,100,.03,0.0,.20,1.0,n,
   0,0,0,&strike,1);
 if(status)return preparation_error(PREPARE_STRIP,"price_delta_strip",status,-1);
 status=prepare_mkl();
 if(status)return preparation_error(PREPARE_ONEMKL,"onemkl_stream",status,-1);
 return PREPARE_OK;
}

static void reset_candidate(uint32_t c){for(uint32_t i=0;i<VALUATIONS;++i){const float s0=i==1?100.1f:i==2?99.9f:100.0f;init_state(&b.states[i],s0);}memset(b.risk,0,sizeof(*b.risk));memset(b.strip_output,0,sizeof(*b.strip_output));memset(b.x,0,32768);memset(b.growth,0,32768);memset(b.g,0,16384);b.mkl_status=0;if(c==CRN_ONEMKL){if(vslCopyStreamState(b.work,b.base))abort();memset(b.z,0,(size_t)b.n*16384);memset(b.pm,0,(size_t)b.n*16384);}}
static void selected_leaf(int suffix)
{
 if(suffix){
  if(b.cv){
   if(b.side)asian_genuine_aad_phase1_suffix_cv_put_diag(b.context,b.risk);
   else asian_genuine_aad_phase1_suffix_cv_call_diag(b.context,b.risk);
  }else if(b.side)
   asian_genuine_aad_phase1_suffix_arithmetic_put_diag(b.context,b.risk);
  else asian_genuine_aad_phase1_suffix_arithmetic_call_diag(b.context,b.risk);
 }else{
  if(b.cv){
   if(b.side)asian_genuine_aad_phase1_forward_cv_put_diag(b.context,b.risk);
   else asian_genuine_aad_phase1_forward_cv_call_diag(b.context,b.risk);
  }else if(b.side)
   asian_genuine_aad_phase1_forward_arithmetic_put_diag(b.context,b.risk);
  else asian_genuine_aad_phase1_forward_arithmetic_call_diag(b.context,b.risk);
 }
}
static void bump_finish(const double p[VALUATIONS]){b.risk->price=p[0];b.risk->delta=(p[1]-p[2])/(2*b.bump_spot);b.risk->rho=(p[3]-p[4])/(2*b.bump_rate);b.risk->vega=(p[5]-p[6])/(2*b.bump_sigma);}

static double run_candidate(uint32_t c){if(c==EXISTING_PRICE||c==EXISTING_PRICE_DELTA){produce(b.producers[0]);asian_genuine_sql_dual_control_diag(b.routes,b.n,&b.states[0]);if(b.cv)asian_genuine_strip_l_to_g_diag(b.states[0].l,b.strip,b.g);if(c==EXISTING_PRICE){if(asian_genuine_strip_price_diag(b.states[0].q,b.g,b.strip,b.cv,4,b.strip_output))abort();}else if(asian_genuine_strip_price_delta_diag(b.states[0].q,b.g,b.strip,b.cv,4,b.strip_output))abort();const asian_genuine_strip_value_t*v=&b.strip_output->values[0];return b.side?v->put_price+(c==EXISTING_PRICE_DELTA?v->put_delta:0):v->call_price+(c==EXISTING_PRICE_DELTA?v->call_delta:0);}if(c==TARGETED_FORWARD||c==CONTRACTED_SUFFIX){produce(b.producers[0]);selected_leaf(c==CONTRACTED_SUFFIX);return b.risk->price+b.risk->delta+b.risk->vega+b.risk->rho;}if(c==GENERIC_REVERSE){produce(b.producers[0]);asian_genuine_aad_phase1_generic_basis_diag(b.context,b.basis);asian_genuine_aad_phase1_consume_basis_diag(b.context,(const float(*)[PATHS])b.basis,(enum asian_genuine_aad_phase1_side)b.side,b.cv,b.risk);return b.risk->price+b.risk->delta+b.risk->vega+b.risk->rho;}double p[VALUATIONS];if(c==CRN_OURS){produce(b.producers[0]);for(uint32_t i=0;i<3;++i)asian_genuine_sql_dual_control_diag(b.routes,b.n,&b.states[i]);for(uint32_t j=1;j<5;++j){produce(b.producers[j]);asian_genuine_sql_dual_control_diag(b.routes,b.n,&b.states[j+2]);}}else{b.mkl_status=vsRngGaussian(VSL_RNG_METHOD_GAUSSIAN_ICDF,b.work,b.n*PATHS,b.z,0.0f,1.0f);const double r[VALUATIONS]={.03,.03,.03,.03003,.02997,.03,.03},sig[VALUATIONS]={.20,.20,.20,.20,.20,.2002,.1998};for(uint32_t i=0;i<VALUATIONS;++i){const double dt=1.0/b.n;const float drift=(float)((r[i]-0.0-.5*sig[i]*sig[i])*dt),diff=(float)(sig[i]*sqrt(dt));vsLinearFrac((MKL_INT)(b.n*PATHS),b.z,b.z,diff,drift,0.0f,1.0f,b.pm);asian_intel_point_major_sql_diag(b.pm,b.n,b.weights,&b.states[i]);}}for(uint32_t i=0;i<VALUATIONS;++i)p[i]=state_price(i);bump_finish(p);return b.risk->price+b.risk->delta+b.risk->vega+b.risk->rho;}

static void condition(uint32_t c,int pressure){uint64_t z=1;if(pressure){for(uint32_t i=0;i<8192;++i){b.pressure[i]+=i+3;z+=b.pressure[i];}}else{const unsigned char*p[]={(unsigned char*)b.x,(unsigned char*)b.growth,(unsigned char*)b.routes,(unsigned char*)b.maps,(unsigned char*)b.tape,(unsigned char*)b.controls,(unsigned char*)b.context,(unsigned char*)b.states,(unsigned char*)b.z};const size_t n[]={32768,32768,b.n*sizeof(*b.routes),b.n*sizeof(*b.maps),ASIAN_GENUINE_AAD_PHASE1_TAPE_BYTES,sizeof(*b.controls),sizeof(*b.context),VALUATIONS*sizeof(*b.states),(size_t)b.n*16384};for(uint32_t k=0;k<(c==CRN_ONEMKL?9u:8u);++k)for(size_t i=0;i<n[k];i+=64)z+=p[k][i];}sink+=z;}
static void checksum(double x){sink+=x+b.risk->price+b.risk->delta+b.risk->vega+b.risk->rho+b.states[0].q[17]+b.states[0].l[31]+b.strip_output->values[0].call_price;}

static int preflight_case(uint32_t n)
{
 if(prepare_fixture(n,0,1)!=PREPARE_OK)return-1;
 double values[CANDIDATES];
 for(uint32_t c=0;c<CANDIDATES;++c){
  reset_candidate(c);values[c]=run_candidate(c);
  if(b.mkl_status||!isfinite(values[c])){
   fprintf(stderr,"asian_genuine_aad_phase1 preflight_error stage=candidate "
     "N=%u candidate=%s mkl_status=%d\n",n,names[c],b.mkl_status);
   release_fixture();return-1;
  }
 }
 reset_candidate(TARGETED_FORWARD);run_candidate(TARGETED_FORWARD);
 asian_genuine_aad_phase1_value_t f=*b.risk;
 reset_candidate(CONTRACTED_SUFFIX);run_candidate(CONTRACTED_SUFFIX);
 asian_genuine_aad_phase1_value_t s=*b.risk;
 if(fabs(f.price-s.price)>1e-7||fabs(f.delta-s.delta)>1e-6||
    fabs(f.vega-s.vega)>1e-4||fabs(f.rho-s.rho)>1e-4){
  fprintf(stderr,"asian_genuine_aad_phase1 preflight_error "
    "stage=forward_suffix N=%u residuals=%g,%g,%g,%g\n",n,
    f.price-s.price,f.delta-s.delta,f.vega-s.vega,f.rho-s.rho);
  release_fixture();return-1;
 }
 printf("asian_genuine_aad_phase1 benchmark_preflight N=%u PASS "
   "forward_suffix=%g,%g,%g,%g\n",n,f.price-s.price,f.delta-s.delta,
   f.vega-s.vega,f.rho-s.rho);
 release_fixture();return 0;
}

static int preflight(void)
{
 static const uint32_t check_ns[]={2,16,256};
 for(size_t i=0;i<sizeof(check_ns)/sizeof(check_ns[0]);++i)
  if(preflight_case(check_ns[i]))return-1;
 puts("asian_genuine_aad_phase1 benchmark_preflight=PASS checked_N=2,16,256 "
   "contract=S0:100,K:100,r:.03,q:0,sigma:.20,T:1 crn_epsilon=.001 "
   "bumps=spot:.1,rate:.00003,sigma:.0002 ours_sources=5 "
   "ours_evolutions=7 onemkl_gaussian_matrices=1 onemkl_evolutions=7");
 return 0;
}

typedef struct {FILE *stream;char *temporary_path;} atomic_json_t;

static int atomic_json_open(atomic_json_t *json,const char *output_path)
{
 memset(json,0,sizeof(*json));
 const size_t bytes=strlen(output_path)+sizeof(".tmp.XXXXXX");
 json->temporary_path=malloc(bytes);
 if(!json->temporary_path){
  fprintf(stderr,"asian_genuine_aad_phase1 json_error stage=temp_path "
    "path=%s detail=out_of_memory\n",output_path);return-1;
 }
 snprintf(json->temporary_path,bytes,"%s.tmp.XXXXXX",output_path);
 const int fd=mkstemp(json->temporary_path);
 if(fd<0){
  fprintf(stderr,"asian_genuine_aad_phase1 json_error stage=mkstemp "
    "path=%s detail=%s\n",json->temporary_path,strerror(errno));
  free(json->temporary_path);json->temporary_path=NULL;return-1;
 }
 json->stream=fdopen(fd,"w");
 if(!json->stream){
  const int saved=errno;close(fd);unlink(json->temporary_path);errno=saved;
  fprintf(stderr,"asian_genuine_aad_phase1 json_error stage=fdopen "
    "path=%s detail=%s\n",json->temporary_path,strerror(errno));
  free(json->temporary_path);json->temporary_path=NULL;return-1;
 }
 return 0;
}

static void atomic_json_abort(atomic_json_t *json)
{
 if(json->stream)fclose(json->stream);
 if(json->temporary_path){unlink(json->temporary_path);free(json->temporary_path);}
 memset(json,0,sizeof(*json));
}

static int atomic_json_commit(atomic_json_t *json,const char *output_path)
{
 int saved=0;
 if(ferror(json->stream)){saved=EIO;}
 else if(fflush(json->stream)){saved=errno;}
 else if(fsync(fileno(json->stream))){saved=errno;}
 if(fclose(json->stream)&&saved==0)saved=errno;
 json->stream=NULL;
 if(saved==0&&rename(json->temporary_path,output_path))saved=errno;
 if(saved){
  fprintf(stderr,"asian_genuine_aad_phase1 json_error stage=commit "
    "path=%s detail=%s\n",output_path,strerror(saved));
  unlink(json->temporary_path);free(json->temporary_path);
  json->temporary_path=NULL;return-1;
 }
 free(json->temporary_path);json->temporary_path=NULL;return 0;
}

int main(int argc,char**argv)
{
 const char*out="results/asian_genuine_aad_phase1/aws.json";
 int check=0,audit=0;
 for(int i=1;i<argc;++i){
  if(!strcmp(argv[i],"--check-only"))check=1;
  else if(!strcmp(argv[i],"--json")&&++i<argc)out=argv[i];
  else if(!strcmp(argv[i],"--audit-leaf")&&++i<argc){
   if(!strcmp(argv[i],"forward"))audit=1;
   else if(!strcmp(argv[i],"suffix"))audit=2;
   else{
    fprintf(stderr,"asian_genuine_aad_phase1 argument_error "
      "stage=audit_leaf value=%s\n",argv[i]);return 2;
   }
  }else{
   fprintf(stderr,"asian_genuine_aad_phase1 argument_error stage=arguments\n");
   return 2;
  }
 }
 const int cpu=first_cpu();
 if(cpu<0){
  fprintf(stderr,"asian_genuine_aad_phase1 preparation_error "
    "stage=cpu_affinity status=-1\n");return 2;
 }
 if(check)return preflight()?2:0;
 if(audit){
  if(prepare_fixture(256u,0,1)!=PREPARE_OK)return 2;
  produce(b.producers[0]);
  for(int i=0;i<32;++i)selected_leaf(audit==2);
  printf("audit_leaf=%s checksum=%.17g\n",audit==1?"forward":"suffix",
    b.risk->price+b.risk->delta+b.risk->vega+b.risk->rho);
  release_fixture();return 0;
 }

 atomic_json_t json;
 if(atomic_json_open(&json,out))return 2;
 FILE*j=json.stream;
 const char *failure_stage=NULL;
 uint32_t active_n=0,failure_candidate=UINT32_MAX;
 int fixture_live=0;
 if(fprintf(j,"{\"status\":\"PASS\",\"cpu\":%d,\"paths\":4096,"
   "\"warmups\":16,\"samples\":51,"
   "\"timer\":\"fenced_TSC_and_CLOCK_MONOTONIC_RAW\","
   "\"tsc_unit_name\":\"TSC_units_not_CPU_cycles\","
   "\"pmu\":\"counters_unavailable\","
   "\"benchmark_provenance\":{\"git_commit\":\"%s\","
   "\"branch\":\"research/asian-aad-phase1\","
   "\"phase\":\"contracted_asian_sensitivity_basis_phase1\","
   "\"build_target\":\"tests/Makefile.asian_genuine_aad_phase1:aws-benchmark-native\","
   "\"qualified_source_pipeline\":\"ordered_d1_prepare_x3_x_only_and_two_vector_exponentials\","
   "\"sobol\":\"genuine_Joe-Kuo\",\"path_count\":4096,"
   "\"fixing_count_domain\":[2,256],"
   "\"runtime_validation_domain\":[2,256],"
   "\"native_ranking_fixing_counts\":[16,32,64,128,256]},"
   "\"contract\":{\"S0\":100.0,\"K\":100.0,\"r\":0.03,"
   "\"q\":0.0,\"sigma\":0.20,\"T\":1.0},"
   "\"crn_bumps\":{\"relative_epsilon\":0.001,"
   "\"spot_absolute\":0.1,\"spot_minus\":99.9,\"spot_plus\":100.1,"
   "\"rate_absolute\":0.00003,\"rate_minus\":0.02997,"
   "\"rate_plus\":0.03003,\"sigma_absolute\":0.0002,"
   "\"sigma_minus\":0.1998,\"sigma_plus\":0.2002},"
   "\"crn_fairness\":{\"ours_source_productions\":5,"
   "\"ours_path_evolutions\":7,\"onemkl_gaussian_matrices\":1,"
   "\"onemkl_path_evolutions\":7},\"results\":[",cpu,
   PHASE1_GIT_COMMIT)<0){
  failure_stage="json_header";goto benchmark_failure;
 }
 int comma=0;
 uint64_t seed=UINT64_C(0x4141445048313233);
 for(size_t ni=0;ni<sizeof(ns)/sizeof(ns[0]);++ni)
  for(uint32_t side=0;side<2;++side)
   for(uint32_t cv=0;cv<2;++cv){
    active_n=ns[ni];
    if(prepare_fixture(active_n,side,cv)!=PREPARE_OK){
     failure_stage="fixture_preparation";goto benchmark_failure;
    }
    fixture_live=1;
    double numerical[CANDIDATES][4]={{0}};
    for(uint32_t c=0;c<CANDIDATES;++c){
     reset_candidate(c);run_candidate(c);
     if(b.mkl_status){
      failure_stage="numerical_candidate";failure_candidate=c;
      goto benchmark_failure;
     }
     if(c==EXISTING_PRICE||c==EXISTING_PRICE_DELTA){
      const asian_genuine_strip_value_t*v=&b.strip_output->values[0];
      numerical[c][0]=side?v->put_price:v->call_price;
      if(c==EXISTING_PRICE_DELTA)
       numerical[c][1]=side?v->put_delta:v->call_delta;
     }else{
      numerical[c][0]=b.risk->price;numerical[c][1]=b.risk->delta;
      numerical[c][2]=b.risk->vega;numerical[c][3]=b.risk->rho;
     }
    }
    for(int mode=0;mode<2;++mode){
     uint64_t ticks[CANDIDATES][SAMPLES]={{0}};
     uint64_t wall[CANDIDATES][SAMPLES]={{0}};
     uint64_t medt[CANDIDATES],medw[CANDIDATES];
     for(uint32_t w=0;w<WARMUPS;++w)
      for(uint32_t c=0;c<CANDIDATES;++c){
       reset_candidate(c);condition(c,mode);checksum(run_candidate(c));
       if(b.mkl_status){
        failure_stage="warmup";failure_candidate=c;goto benchmark_failure;
       }
      }
     for(uint32_t s=0;s<SAMPLES;++s){
      uint32_t order[CANDIDATES];shuffle(order,&seed);
      for(uint32_t q=0;q<CANDIDATES;++q){
       const uint32_t c=order[q];reset_candidate(c);condition(c,mode);
       const uint64_t w0=wall_ns(),t0=tsc0();
       const double v=run_candidate(c);
       const uint64_t t1=tsc1(),w1=wall_ns();
       if(b.mkl_status){
        failure_stage="measurement";failure_candidate=c;
        goto benchmark_failure;
       }
       ticks[c][s]=t1-t0;wall[c][s]=w1-w0;checksum(v);
      }
     }
     for(uint32_t c=0;c<CANDIDATES;++c){
      medt[c]=quantile(ticks[c],25);medw[c]=quantile(wall[c],25);
      const uint64_t tape_traffic=c==CONTRACTED_SUFFIX?
        (uint64_t)PATHS*b.n*4u:0;
      fprintf(j,"%s{\"N\":%u,\"side\":\"%s\","
       "\"estimator\":\"%s\",\"conditioning\":\"%s\","
       "\"candidate\":\"%s\",\"tsc_p10\":%"PRIu64","
       "\"tsc_median\":%"PRIu64",\"tsc_p90\":%"PRIu64","
       "\"tsc_units_per_path_fixing\":%.9g,"
       "\"wall_ns_p10\":%"PRIu64",\"wall_ns_median\":%"PRIu64","
       "\"wall_ns_p90\":%"PRIu64","
       "\"complete_risk_sets_per_second\":%.9g,"
       "\"numerical\":{\"price\":%.17g,\"delta\":%.17g,"
       "\"vega\":%.17g,\"rho\":%.17g,"
       "\"price_error_vs_suffix\":%.17g,"
       "\"delta_error_vs_suffix\":%.17g,"
       "\"vega_error_vs_suffix\":%.17g,"
       "\"rho_error_vs_suffix\":%.17g},\"tape_bytes\":%u,"
       "\"modeled_tape_write_bytes\":%"PRIu64","
       "\"modeled_tape_read_bytes\":%"PRIu64","
       "\"measured_traffic\":\"counters_unavailable\","
       "\"raw_tsc\":[",comma++?",":"",b.n,side?"put":"call",
       cv?"geometric_cv":"arithmetic",
       mode?"historical_32KiB_rmw":"candidate_specific_warm",names[c],
       quantile(ticks[c],5),medt[c],quantile(ticks[c],45),
       (double)medt[c]/(PATHS*b.n),quantile(wall[c],5),medw[c],
       quantile(wall[c],45),(c>=TARGETED_FORWARD?1e9/(double)medw[c]:0.0),
       numerical[c][0],numerical[c][1],numerical[c][2],numerical[c][3],
       numerical[c][0]-numerical[CONTRACTED_SUFFIX][0],
       numerical[c][1]-numerical[CONTRACTED_SUFFIX][1],
       numerical[c][2]-numerical[CONTRACTED_SUFFIX][2],
       numerical[c][3]-numerical[CONTRACTED_SUFFIX][3],
       c==CONTRACTED_SUFFIX?ASIAN_GENUINE_AAD_PHASE1_TAPE_BYTES:0,
       tape_traffic,tape_traffic);
      for(uint32_t s=0;s<SAMPLES;++s)
       fprintf(j,"%s%"PRIu64,s?",":"",ticks[c][s]);
      fputs("],\"raw_wall_ns\":[",j);
      for(uint32_t s=0;s<SAMPLES;++s)
       fprintf(j,"%s%"PRIu64,s?",":"",wall[c][s]);
      fputs("]}",j);
     }
     fprintf(j,",{\"N\":%u,\"side\":\"%s\","
       "\"estimator\":\"%s\",\"conditioning\":\"%s\","
       "\"candidate\":\"derived_ratios\","
       "\"forward_over_suffix\":%.9g,\"suffix_over_forward\":%.9g,"
       "\"suffix_over_crn\":%.9g,\"onemkl_over_suffix\":%.9g,"
       "\"suffix_increment_over_price\":%"PRId64","
       "\"suffix_increment_over_price_delta\":%"PRId64"}",b.n,
       side?"put":"call",cv?"geometric_cv":"arithmetic",
       mode?"historical_32KiB_rmw":"candidate_specific_warm",
       (double)medt[TARGETED_FORWARD]/medt[CONTRACTED_SUFFIX],
       (double)medt[CONTRACTED_SUFFIX]/medt[TARGETED_FORWARD],
       (double)medt[CONTRACTED_SUFFIX]/medt[CRN_OURS],
       (double)medt[CRN_ONEMKL]/medt[CONTRACTED_SUFFIX],
       (int64_t)medt[CONTRACTED_SUFFIX]-(int64_t)medt[EXISTING_PRICE],
       (int64_t)medt[CONTRACTED_SUFFIX]-
         (int64_t)medt[EXISTING_PRICE_DELTA]);
     comma++;
    }
    release_fixture();fixture_live=0;
   }
 if(fprintf(j,"],\"working_sets\":{\"packet_S_tape_bytes\":32768,"
    "\"source_x_growth_bytes\":65536,\"hot_context_bytes\":64},"
    "\"decision_note\":\"Native measurements required; no residency claim "
    "is inferred from nominal sizes.\"}\n")<0){
  failure_stage="json_footer";goto benchmark_failure;
 }
 if(atomic_json_commit(&json,out))return 2;
 return sink==0;

benchmark_failure:
 fprintf(stderr,"asian_genuine_aad_phase1 benchmark_error stage=%s N=%u "
   "candidate=%s mkl_status=%d output_preserved=true\n",
   failure_stage?failure_stage:"unknown",active_n,
   failure_candidate<CANDIDATES?names[failure_candidate]:"none",b.mkl_status);
 if(fixture_live)release_fixture();
 atomic_json_abort(&json);
 return 2;
}
