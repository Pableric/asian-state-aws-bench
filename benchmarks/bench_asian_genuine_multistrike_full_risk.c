#define _GNU_SOURCE
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_geometric_cv_diag.h"
#include "private/asian_genuine_aad_phase1_diag.h"
#include "private/asian_genuine_multistrike_full_risk_diag.h"
#include "private/asian_genuine_price_delta_strip_diag.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <mkl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#ifndef MSFR_GIT_COMMIT
#define MSFR_GIT_COMMIT "unknown"
#endif

enum { PATHS=4096,VALUATIONS=7,SOURCES=5,SAMPLES=51,WARMUPS=16,
       CANDIDATES=9,N_COUNT=5,K_COUNT=5 };
enum candidate { PHASE1_COMPONENT,REPEAT_COMPLETE,SOURCE_ONCE_K_EVOLUTIONS,
 BASIS_TILE2,BASIS_TILE4,EXISTING_PRICE_DELTA,CRN_OURS,CRN_ONEMKL,
 ONEMKL_MATCHED_FORWARD };
static const char*const candidate_names[CANDIDATES]={
 "phase1_single_strike_component_K1_only",
 "K_independent_complete_phase1_direct_side_plus_parity",
 "one_source_plus_K_phase1_evolutions_direct_side_plus_parity",
 "one_source_one_basis_plus_tile2_strip",
 "one_source_one_basis_plus_tile4_strip",
 "existing_price_delta_strip_component",
 "our_seven_valuation_crn_complete_strip",
 "onemkl_gaussian_plus_seven_crn_evolutions",
 "onemkl_gaussian_plus_matched_forward_basis"};
static const uint32_t ns[N_COUNT]={16,32,64,128,256};
static const uint32_t ks[K_COUNT]={1,4,8,16,32};

typedef struct{
 uint32_t n,k,directions[256][32],*words[2],*pressure;
 float *source_x,*source_growth,*weights,*g,*z,*point_x,*dm_x,*dm_growth,*tape;
 fragment_map_t*maps;asian_genuine_route_t*route_sets;
 ordered_d1_diag_context_t*producers[SOURCES];
 asian_genuine_msfr_basis_controls_t*basis_controls;
 asian_genuine_msfr_basis_context_t*basis_context;
 asian_genuine_msfr_basis_t*basis;
 asian_genuine_msfr_strike_controls_t*strike_controls;
 asian_genuine_msfr_consumer_context_t*consumer_context;
 asian_genuine_msfr_accumulator_t*accumulator;
 asian_genuine_msfr_output_t*output;
 asian_genuine_aad_phase1_controls_t*phase_controls;
 asian_genuine_aad_phase1_context_t*phase_contexts;
 asian_genuine_aad_phase1_value_t*phase_values;
 asian_genuine_state_t*states;
 asian_genuine_strip_context_t*strip_contexts;
 asian_genuine_strip_output_t*strip_outputs;
 VSLStreamStatePtr base_stream,work_stream;
 int mkl_status;
}fixture_t;
static fixture_t b;static volatile double sink;static int audit_only;

static void*a64(size_t bytes){void*p=0;if(posix_memalign(&p,64,bytes))return 0;memset(p,0,bytes);return p;}
static uint32_t sobol(uint32_t index,const uint32_t*v){uint32_t g=index^(index>>1),w=0;for(uint32_t bit=0;g;++bit,g>>=1)if(g&1)w^=v[bit];return w;}
static uint64_t tsc0(void){_mm_lfence();return __rdtsc();}
static uint64_t tsc1(void){unsigned aux;uint64_t t=__rdtscp(&aux);_mm_lfence();return t;}
static uint64_t wall_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC_RAW,&t))abort();return(uint64_t)t.tv_sec*UINT64_C(1000000000)+t.tv_nsec;}
static int cmp64(const void*a,const void*c){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)c;return(x>y)-(x<y);}
static uint64_t quantile(const uint64_t*x,uint32_t rank){uint64_t y[SAMPLES];memcpy(y,x,sizeof y);qsort(y,SAMPLES,8,cmp64);return y[rank];}
static int cmp_double(const void*a,const void*c){double x=*(const double*)a,y=*(const double*)c;return(x>y)-(x<y);}
static double paired_ratio(const uint64_t*n,const uint64_t*d){double r[SAMPLES];for(int i=0;i<SAMPLES;++i)r[i]=d[i]?(double)n[i]/d[i]:0.0;qsort(r,SAMPLES,sizeof*r,cmp_double);return r[25];}
static uint64_t rng_step(uint64_t*x){*x^=*x<<13;*x^=*x>>7;return *x^=*x<<17;}
static int first_cpu(void){cpu_set_t a,p;CPU_ZERO(&a);if(sched_getaffinity(0,sizeof a,&a))return-1;int c=0;while(c<CPU_SETSIZE&&!CPU_ISSET(c,&a))++c;if(c==CPU_SETSIZE)return-1;CPU_ZERO(&p);CPU_SET(c,&p);return sched_setaffinity(0,sizeof p,&p)?-1:c;}

/* Compact SHA-256 used only for binary provenance. */
typedef struct{uint32_t h[8];uint64_t bits;unsigned char block[64];size_t used;}sha256_t;
static uint32_t rr(uint32_t x,unsigned n){return(x>>n)|(x<<(32-n));}
static void sha_block(sha256_t*s,const unsigned char*b0){static const uint32_t k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t)b0[4*i]<<24|(uint32_t)b0[4*i+1]<<16|(uint32_t)b0[4*i+2]<<8|b0[4*i+3];for(int i=16;i<64;++i){uint32_t a=rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3),c=rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+a+w[i-7]+c;}uint32_t a=s->h[0],c=s->h[1],d=s->h[2],e=s->h[3],f=s->h[4],g=s->h[5],h=s->h[6],j=s->h[7];for(int i=0;i<64;++i){uint32_t s1=rr(f,6)^rr(f,11)^rr(f,25),ch=(f&g)^(~f&h),t1=j+s1+ch+k[i]+w[i],s0=rr(a,2)^rr(a,13)^rr(a,22),maj=(a&c)^(a&d)^(c&d),t2=s0+maj;j=h;h=g;g=f;f=e+t1;e=d;d=c;c=a;a=t1+t2;}s->h[0]+=a;s->h[1]+=c;s->h[2]+=d;s->h[3]+=e;s->h[4]+=f;s->h[5]+=g;s->h[6]+=h;s->h[7]+=j;}
static void sha_init(sha256_t*s){static const uint32_t v[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memset(s,0,sizeof* s);memcpy(s->h,v,sizeof v);}
static void sha_add(sha256_t*s,const void*data,size_t n){const unsigned char*p=data;s->bits+=(uint64_t)n*8;while(n){size_t q=64-s->used;if(q>n)q=n;memcpy(s->block+s->used,p,q);s->used+=q;p+=q;n-=q;if(s->used==64){sha_block(s,s->block);s->used=0;}}}
static void sha_done(sha256_t*s,char out[65]){s->block[s->used++]=0x80;if(s->used>56){while(s->used<64)s->block[s->used++]=0;sha_block(s,s->block);s->used=0;}while(s->used<56)s->block[s->used++]=0;for(int i=7;i>=0;--i)s->block[s->used++]=(unsigned char)(s->bits>>(8*i));sha_block(s,s->block);for(int i=0;i<8;++i)sprintf(out+8*i,"%08"PRIx32,s->h[i]);out[64]=0;}
static int binary_sha(char out[65]){char path[4096];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);if(n<0)return-1;path[n]=0;FILE*f=fopen(path,"rb");if(!f)return-1;sha256_t s;sha_init(&s);unsigned char buf[16384];size_t got;while((got=fread(buf,1,sizeof buf,f)))sha_add(&s,buf,got);int e=ferror(f);fclose(f);if(e)return-1;sha_done(&s,out);return 0;}

static int load_directions(void){FILE*f=fopen("direction_numbers/joe_kuo_6_21201.bin","rb");if(!f)return-1;for(uint32_t d=0;d<256;++d){uint32_t n;if(fread(&n,4,1,f)!=1||n!=32||fread(b.directions[d],4,32,f)!=32){fclose(f);return-1;}}fclose(f);return 0;}
static void init_state(asian_genuine_state_t*s,float s0){memset(s,0,sizeof* s);for(uint32_t i=0;i<PATHS;++i)s->s[i]=s0;}
static float*source_x(uint32_t j){return b.source_x+(size_t)j*2*PATHS;}
static float*source_growth(uint32_t j){return b.source_growth+(size_t)j*2*PATHS;}
static asian_genuine_route_t*routes(uint32_t j){return b.route_sets+(size_t)j*b.n;}
static void produce(uint32_t j){float*x=source_x(j),*g=source_growth(j);ordered_d1_x_only_diag(256,b.producers[j],x);asian_vector_exp_range_reduced_array_diag(x,g);asian_vector_exp_range_reduced_array_diag(x+PATHS,g+PATHS);}

static int prepare_mkl(void){size_t count=3+(size_t)b.n*32;MKL_UINT*p=calloc(count,sizeof*p);if(!p)return-1;p[0]=b.n;p[1]=VSL_USER_QRNG_INITIAL_VALUES;p[2]=VSL_USER_DIRECTION_NUMBERS;for(uint32_t d=0;d<b.n;++d)for(uint32_t k=0;k<32;++k)p[3+d*32+k]=b.directions[d][k];int e=vslNewStreamEx(&b.base_stream,VSL_BRNG_SOBOL,count,p);free(p);if(e||vslSkipAheadStream(b.base_stream,(long long)(8192-1)*b.n)||vslCopyStream(&b.work_stream,b.base_stream))return-1;VSLStreamStatePtr check=0;if(vslCopyStream(&check,b.base_stream))return-1;uint32_t*raw=a64((size_t)b.n*PATHS*4);if(!raw)return-1;e=viRngUniformBits(VSL_RNG_METHOD_UNIFORMBITS_STD,check,b.n*PATHS,raw);for(uint32_t path=0;!e&&path<PATHS;++path)for(uint32_t d=0;d<b.n;++d)if(raw[(size_t)path*b.n+d]!=sobol(8192+path,b.directions[d]))e=-1;free(raw);vslDeleteStream(&check);return e;}

static void release_fixture(void){if(b.work_stream)vslDeleteStream(&b.work_stream);if(b.base_stream)vslDeleteStream(&b.base_stream);free(b.strip_outputs);free(b.strip_contexts);free(b.states);free(b.phase_values);free(b.phase_contexts);free(b.phase_controls);free(b.output);free(b.accumulator);free(b.consumer_context);free(b.strike_controls);free(b.basis);free(b.basis_context);free(b.basis_controls);for(int i=0;i<SOURCES;++i)free(b.producers[i]);free(b.route_sets);free(b.maps);free(b.tape);free(b.dm_growth);free(b.dm_x);free(b.point_x);free(b.z);free(b.g);free(b.weights);free(b.source_growth);free(b.source_x);free(b.pressure);free(b.words[1]);free(b.words[0]);memset(&b,0,sizeof b);}

static int prepare_fixture(uint32_t n,uint32_t k){
 memset(&b,0,sizeof b);b.n=n;b.k=k;if(load_directions())return-1;
 b.words[0]=a64(16384);b.words[1]=a64(16384);b.pressure=a64(32768);
 b.source_x=a64((size_t)SOURCES*32768);b.source_growth=a64((size_t)SOURCES*32768);
 b.weights=a64(n*4);b.g=a64(16384);b.z=a64((size_t)n*16384);
 b.point_x=a64((size_t)n*16384);b.dm_x=a64((size_t)n*16384);
 b.dm_growth=a64((size_t)n*16384);b.tape=a64(ASIAN_GENUINE_AAD_PHASE1_TAPE_BYTES);
 b.maps=a64((size_t)n*sizeof*b.maps);b.route_sets=a64((size_t)SOURCES*n*sizeof*b.route_sets);
 for(int j=0;j<SOURCES;++j)b.producers[j]=a64(sizeof(**b.producers));
 b.basis_controls=a64(sizeof*b.basis_controls);b.basis_context=a64(sizeof*b.basis_context);
 b.basis=a64(sizeof*b.basis);b.strike_controls=a64(sizeof*b.strike_controls);
 b.consumer_context=a64(sizeof*b.consumer_context);b.accumulator=a64(sizeof*b.accumulator);
 b.output=a64(sizeof*b.output);b.phase_controls=a64((size_t)k*sizeof*b.phase_controls);
 b.phase_contexts=a64((size_t)k*sizeof*b.phase_contexts);b.phase_values=a64((size_t)k*sizeof*b.phase_values);
 b.states=a64(VALUATIONS*sizeof*b.states);b.strip_contexts=a64(VALUATIONS*sizeof*b.strip_contexts);
 b.strip_outputs=a64(VALUATIONS*sizeof*b.strip_outputs);
 if(!b.words[0]||!b.words[1]||!b.pressure||!b.source_x||!b.source_growth||!b.weights||!b.g||!b.z||!b.point_x||!b.dm_x||!b.dm_growth||!b.tape||!b.maps||!b.route_sets||!b.basis_controls||!b.basis_context||!b.basis||!b.strike_controls||!b.consumer_context||!b.accumulator||!b.output||!b.phase_controls||!b.phase_contexts||!b.phase_values||!b.states||!b.strip_contexts||!b.strip_outputs)return-1;
 for(int j=0;j<SOURCES;++j)if(!b.producers[j])return-1;
 for(uint32_t i=0;i<8192;++i)b.pressure[i]=i;
 for(uint32_t i=0;i<PATHS;++i){b.words[0][i]=sobol(8192+i,b.directions[0]);b.words[1][i]=sobol(12288+i,b.directions[0]);}
 const double rates[SOURCES]={.03,.03003,.02997,.03,.03},sigmas[SOURCES]={.20,.20,.20,.2002,.1998};
 const uint32_t pn=asian_genuine_msfr_producer_fixing_count(n);if(!pn)return-1;
 for(int j=0;j<SOURCES;++j){double dt=1.0/n;float drift=(float)((rates[j]-.5*sigmas[j]*sigmas[j])*dt),diff=(float)(sigmas[j]*sqrt(dt));if(ordered_d1_diag_prepare(b.producers[j],drift,diff,8192,ORDERED_D1_DIAG_PREPARE_X3,pn))return-1;}
 produce(0);
 const uint32_t*sw[2]={b.words[0],b.words[1]};const float*xb[2]={source_x(0),source_x(0)+PATHS},*gb[2]={source_growth(0),source_growth(0)+PATHS};
 uint32_t*target=a64(16384);if(!target)return-1;
 for(uint32_t d=0;d<n;++d){for(uint32_t p=0;p<PATHS;++p)target[p]=sobol(8192+p,b.directions[d]);if(asian_genuine_prepare_route(sw,2,xb,gb,target,d,n,&b.maps[d],&routes(0)[d])){free(target);return-1;}memcpy(&b.weights[d],&routes(0)[d].weight_bits,4);}free(target);
 for(int j=1;j<SOURCES;++j)for(uint32_t d=0;d<n;++d){routes(j)[d]=routes(0)[d];uint32_t block=routes(0)[d].x_base==source_x(0)?0u:1u;routes(j)[d].x_base=source_x(j)+block*PATHS;routes(j)[d].growth_base=source_growth(j)+block*PATHS;}
 float strikes[32];if(asian_genuine_strip_fixed_strikes(k,strikes))return-1;
 if(asian_genuine_msfr_prepare_basis_controls(b.basis_controls,100,.03,0,.20,1,n)||asian_genuine_msfr_prepare_basis_context(b.basis_context,routes(0),b.basis_controls,100,.03,0,.20,1,n)||asian_genuine_msfr_prepare_strikes(b.strike_controls,100,.03,0,.20,1,n,strikes,k)||asian_genuine_msfr_prepare_consumer_context(b.consumer_context,b.strike_controls))return-1;
 for(uint32_t i=0;i<k;++i)if(asian_genuine_aad_phase1_prepare_controls(&b.phase_controls[i],100,strikes[i],.03,0,.20,1,n)||asian_genuine_aad_phase1_prepare_context(&b.phase_contexts[i],routes(0),b.tape,&b.phase_controls[i],100,strikes[i],.03,0,.20,1,n))return-1;
 const double s0[VALUATIONS]={100,100.1,99.9,100,100,100,100},r[VALUATIONS]={.03,.03,.03,.03003,.02997,.03,.03},sig[VALUATIONS]={.20,.20,.20,.20,.20,.2002,.1998};
 for(int v=0;v<VALUATIONS;++v)if(asian_genuine_strip_prepare(&b.strip_contexts[v],s0[v],r[v],0,sig[v],1,n,0,0,0,strikes,k))return-1;
 return audit_only?0:prepare_mkl();
}

static void phase_leaf(uint32_t i,int cv,asian_genuine_aad_phase1_value_t*out){int call=(b.strike_controls->strikes[i].flags&ASIAN_GENUINE_MSFR_DIRECT_CALL)!=0;if(cv){if(call)asian_genuine_aad_phase1_forward_cv_call_diag(&b.phase_contexts[i],out);else asian_genuine_aad_phase1_forward_cv_put_diag(&b.phase_contexts[i],out);}else{if(call)asian_genuine_aad_phase1_forward_arithmetic_call_diag(&b.phase_contexts[i],out);else asian_genuine_aad_phase1_forward_arithmetic_put_diag(&b.phase_contexts[i],out);}}
static void phase_finalize(uint32_t i,const asian_genuine_aad_phase1_value_t*v){const asian_genuine_msfr_strike_t*s=&b.strike_controls->strikes[i];const double*d=(const double*)v,*ca=s->call_adjust,*pa=s->put_adjust;double*c=(double*)&b.output->values[i].call,*p=(double*)&b.output->values[i].put;for(int f=0;f<4;++f){c[f]=d[f]+ca[f];p[f]=d[f]+pa[f];}}
static void run_strip_valuation(uint32_t v,const asian_genuine_route_t*r,int cv){init_state(&b.states[v],v==1?100.1f:v==2?99.9f:100.0f);asian_genuine_sql_dual_control_diag(r,b.n,&b.states[v]);if(cv)asian_genuine_strip_l_to_g_diag(b.states[v].l,&b.strip_contexts[v],b.g);if(asian_genuine_strip_price_diag(b.states[v].q,b.g,&b.strip_contexts[v],cv,4,&b.strip_outputs[v]))abort();}
static void finish_crn(void){for(uint32_t i=0;i<b.k;++i){const asian_genuine_strip_value_t*v[VALUATIONS];for(int j=0;j<VALUATIONS;++j)v[j]=&b.strip_outputs[j].values[i];asian_genuine_msfr_value_t*c=&b.output->values[i].call,*p=&b.output->values[i].put;c->price=v[0]->call_price;c->delta=(v[1]->call_price-v[2]->call_price)/.2;c->rho=(v[3]->call_price-v[4]->call_price)/.00006;c->vega=(v[5]->call_price-v[6]->call_price)/.0004;p->price=v[0]->put_price;p->delta=(v[1]->put_price-v[2]->put_price)/.2;p->rho=(v[3]->put_price-v[4]->put_price)/.00006;p->vega=(v[5]->put_price-v[6]->put_price)/.0004;}}
static void consume_basis(int cv,uint32_t tile){if(asian_genuine_msfr_accumulator_init(b.accumulator,b.consumer_context,cv)||asian_genuine_msfr_consume_block(b.basis,b.consumer_context,cv,tile,b.accumulator)||asian_genuine_msfr_finalize(b.consumer_context,b.accumulator,b.output))abort();}

static void reset_candidate(uint32_t c);
static double output_sum(uint32_t fields);
enum component { SOURCE_COMPONENT,BASIS_COMPONENT,TILE2_COMPONENT,TILE4_COMPONENT,COMPONENTS };
static const char*const component_names[COMPONENTS]={"qualified_source_production_component","strike_independent_forward_basis_leaf_component","tile2_strip_consumer_finalizer_component","tile4_strip_consumer_finalizer_component"};
static void prepare_component(uint32_t c){reset_candidate(BASIS_TILE2);if(c!=SOURCE_COMPONENT)produce(0);if(c>=TILE2_COMPONENT)asian_genuine_msfr_basis_forward_diag(b.basis_context,b.basis);}
static double run_component(uint32_t c,int cv){if(c==SOURCE_COMPONENT){produce(0);return b.source_x[17];}if(c==BASIS_COMPONENT){asian_genuine_msfr_basis_forward_diag(b.basis_context,b.basis);return b.basis->values[0][17];}consume_basis(cv,c==TILE2_COMPONENT?2u:4u);return output_sum(4);}

static void reset_candidate(uint32_t c){memset(b.output,0,sizeof*b.output);memset(b.accumulator,0,sizeof*b.accumulator);memset(b.basis,0,sizeof*b.basis);memset(b.phase_values,0,(size_t)b.k*sizeof*b.phase_values);memset(b.strip_outputs,0,VALUATIONS*sizeof*b.strip_outputs);for(int v=0;v<VALUATIONS;++v)init_state(&b.states[v],v==1?100.1f:v==2?99.9f:100.0f);b.mkl_status=0;if(c==CRN_ONEMKL||c==ONEMKL_MATCHED_FORWARD){if(vslCopyStreamState(b.work_stream,b.base_stream))abort();memset(b.z,0,(size_t)b.n*16384);memset(b.point_x,0,(size_t)b.n*16384);memset(b.dm_x,0,(size_t)b.n*16384);memset(b.dm_growth,0,(size_t)b.n*16384);}}
static double output_sum(uint32_t fields){double z0=0;for(uint32_t i=0;i<b.k;++i){const double*c=(const double*)&b.output->values[i].call,*p=(const double*)&b.output->values[i].put;for(uint32_t f=0;f<fields;++f)z0+=c[f]+p[f];}return z0;}
static double run_candidate(uint32_t c,int cv){
 switch(c){
 case PHASE1_COMPONENT:produce(0);phase_leaf(0,cv,&b.phase_values[0]);phase_finalize(0,&b.phase_values[0]);break;
 case REPEAT_COMPLETE:for(uint32_t i=0;i<b.k;++i){produce(0);phase_leaf(i,cv,&b.phase_values[i]);phase_finalize(i,&b.phase_values[i]);}break;
 case SOURCE_ONCE_K_EVOLUTIONS:produce(0);for(uint32_t i=0;i<b.k;++i){phase_leaf(i,cv,&b.phase_values[i]);phase_finalize(i,&b.phase_values[i]);}break;
 case BASIS_TILE2:case BASIS_TILE4:produce(0);asian_genuine_msfr_basis_forward_diag(b.basis_context,b.basis);consume_basis(cv,c==BASIS_TILE2?2:4);break;
 case EXISTING_PRICE_DELTA:produce(0);init_state(&b.states[0],100);asian_genuine_sql_dual_control_diag(routes(0),b.n,&b.states[0]);if(cv)asian_genuine_strip_l_to_g_diag(b.states[0].l,&b.strip_contexts[0],b.g);if(asian_genuine_strip_price_delta_diag(b.states[0].q,b.g,&b.strip_contexts[0],cv,4,&b.strip_outputs[0]))abort();for(uint32_t i=0;i<b.k;++i){b.output->values[i].call.price=b.strip_outputs[0].values[i].call_price;b.output->values[i].put.price=b.strip_outputs[0].values[i].put_price;b.output->values[i].call.delta=b.strip_outputs[0].values[i].call_delta;b.output->values[i].put.delta=b.strip_outputs[0].values[i].put_delta;}break;
 case CRN_OURS:produce(0);for(int v=0;v<3;++v)run_strip_valuation(v,routes(0),cv);for(int j=1;j<SOURCES;++j){produce(j);run_strip_valuation(j+2,routes(j),cv);}finish_crn();break;
 case CRN_ONEMKL:{b.mkl_status=vsRngGaussian(VSL_RNG_METHOD_GAUSSIAN_ICDF,b.work_stream,b.n*PATHS,b.z,0,1);const double r[VALUATIONS]={.03,.03,.03,.03003,.02997,.03,.03},sig[VALUATIONS]={.20,.20,.20,.20,.20,.2002,.1998};for(int v=0;v<VALUATIONS;++v){double dt=1.0/b.n;float drift=(float)((r[v]-.5*sig[v]*sig[v])*dt),diff=(float)(sig[v]*sqrt(dt));vsLinearFrac(b.n*PATHS,b.z,b.z,diff,drift,0,1,b.point_x);init_state(&b.states[v],v==1?100.1f:v==2?99.9f:100);asian_intel_point_major_sql_diag(b.point_x,b.n,b.weights,&b.states[v]);if(cv)asian_genuine_strip_l_to_g_diag(b.states[v].l,&b.strip_contexts[v],b.g);if(asian_genuine_strip_price_diag(b.states[v].q,b.g,&b.strip_contexts[v],cv,4,&b.strip_outputs[v]))abort();}finish_crn();break;}
 case ONEMKL_MATCHED_FORWARD:{b.mkl_status=vsRngGaussian(VSL_RNG_METHOD_GAUSSIAN_ICDF,b.work_stream,b.n*PATHS,b.z,0,1);double dt=1.0/b.n;float drift=(float)((.03-.5*.20*.20)*dt),diff=(float)(.20*sqrt(dt));vsLinearFrac(b.n*PATHS,b.z,b.z,diff,drift,0,1,b.point_x);for(uint32_t p=0;p<PATHS;++p)for(uint32_t d=0;d<b.n;++d)b.dm_x[(size_t)d*PATHS+p]=b.point_x[(size_t)p*b.n+d];for(uint32_t d=0;d<b.n;++d)asian_vector_exp_range_reduced_array_diag(b.dm_x+(size_t)d*PATHS,b.dm_growth+(size_t)d*PATHS);asian_genuine_msfr_dimension_major_basis_diag(b.dm_x,b.dm_growth,b.basis_context,b.basis);consume_basis(cv,4);break;}
 default:abort();
 }
 return output_sum(c==EXISTING_PRICE_DELTA?2:4);
}

static double max_difference(const asian_genuine_msfr_output_t*a,const asian_genuine_msfr_output_t*c,uint32_t fields){double m=0;for(uint32_t i=0;i<b.k;++i){const double*x=(const double*)&a->values[i],*y=(const double*)&c->values[i];for(uint32_t side=0;side<2;++side)for(uint32_t f=0;f<fields;++f){uint32_t at=side*4+f;double d=fabs(x[at]-y[at]);if(d>m)m=d;}}return m;}
static int preflight_case(uint32_t n,uint32_t k,int cv){if(prepare_fixture(n,k))return-1;asian_genuine_msfr_output_t*reference=a64(sizeof*reference);if(!reference)return-1;reset_candidate(BASIS_TILE2);run_candidate(BASIS_TILE2,cv);memcpy(reference,b.output,sizeof*reference);reset_candidate(BASIS_TILE4);run_candidate(BASIS_TILE4,cv);double tile=max_difference(reference,b.output,4);reset_candidate(REPEAT_COMPLETE);run_candidate(REPEAT_COMPLETE,cv);double repeat=max_difference(reference,b.output,4);reset_candidate(SOURCE_ONCE_K_EVOLUTIONS);run_candidate(SOURCE_ONCE_K_EVOLUTIONS,cv);double once=max_difference(reference,b.output,4);reset_candidate(EXISTING_PRICE_DELTA);run_candidate(EXISTING_PRICE_DELTA,cv);double pd=max_difference(reference,b.output,2);reset_candidate(CRN_OURS);run_candidate(CRN_OURS,cv);double crn=max_difference(reference,b.output,4);reset_candidate(CRN_ONEMKL);run_candidate(CRN_ONEMKL,cv);double mkl_crn=max_difference(reference,b.output,4);reset_candidate(ONEMKL_MATCHED_FORWARD);run_candidate(ONEMKL_MATCHED_FORWARD,cv);double mkl_forward=max_difference(reference,b.output,4);if(k==1){reset_candidate(PHASE1_COMPONENT);run_candidate(PHASE1_COMPONENT,cv);if(max_difference(reference,b.output,4)!=0)return-1;}printf("preflight N=%u K=%u estimator=%s tile=%g repeat=%g source_once=%g price_delta=%g crn=%g mkl_crn=%g mkl_forward=%g\n",n,k,cv?"geometric_cv":"arithmetic",tile,repeat,once,pd,crn,mkl_crn,mkl_forward);free(reference);int ok=!b.mkl_status&&tile==0&&repeat==0&&once==0&&pd<=1e-4&&crn<=.5&&mkl_crn<=.5&&mkl_forward<=1e-3;release_fixture();return ok?0:-1;}
static int preflight(void){const uint32_t cn[]={2,16,256},ck[]={1,4,32};for(int ni=0;ni<3;++ni)for(int ki=0;ki<3;++ki)for(int cv=0;cv<2;++cv)if(preflight_case(cn[ni],ck[ki],cv))return-1;puts("asian_genuine_multistrike_full_risk benchmark_preflight=PASS N=2,16,256 K=1,4,32 estimators=2 tiles=2,4 onemkl_words=exact");return 0;}

static void condition(uint32_t c,int pressure){uint64_t z0=1;if(pressure){for(uint32_t i=0;i<8192;++i){b.pressure[i]+=i+3;z0+=b.pressure[i];}}else{const unsigned char*p[]={(unsigned char*)b.source_x,(unsigned char*)b.source_growth,(unsigned char*)b.route_sets,(unsigned char*)b.basis,(unsigned char*)b.strike_controls,(unsigned char*)b.output,(unsigned char*)b.z};const size_t n[]={SOURCES*32768,SOURCES*32768,(size_t)SOURCES*b.n*sizeof(*b.route_sets),sizeof*b.basis,sizeof*b.strike_controls,sizeof*b.output,(size_t)b.n*16384};for(uint32_t j=0;j<(c>=CRN_ONEMKL?7u:6u);++j)for(size_t i=0;i<n[j];i+=64)z0+=p[j][i];}sink+=z0;}
static void checksum(double x){sink+=x+b.output->values[0].call.price+b.output->values[0].put.rho+b.basis->values[0][17];}
static void shuffle_active(uint32_t*out,uint32_t count,uint64_t*seed){for(uint32_t i=0;i<count;++i)out[i]=i;for(uint32_t i=count-1;i;--i){uint32_t j=(uint32_t)(rng_step(seed)%(i+1)),q=out[i];out[i]=out[j];out[j]=q;}}

typedef struct{FILE*f;char*tmp;}atomic_json_t;
static int json_open(atomic_json_t*j,const char*out){if(access(out,F_OK)==0){errno=EEXIST;return-1;}memset(j,0,sizeof*j);size_t n=strlen(out)+16;j->tmp=malloc(n);if(!j->tmp)return-1;snprintf(j->tmp,n,"%s.tmp.XXXXXX",out);int fd=mkstemp(j->tmp);if(fd<0)return-1;j->f=fdopen(fd,"w");if(!j->f){close(fd);unlink(j->tmp);return-1;}return 0;}
static void json_abort(atomic_json_t*j){if(j->f)fclose(j->f);if(j->tmp){unlink(j->tmp);free(j->tmp);}memset(j,0,sizeof*j);}
static int publish_noreplace(const char*tmp,const char*out){
#ifdef SYS_renameat2
 return syscall(SYS_renameat2,AT_FDCWD,tmp,AT_FDCWD,out,1u);
#else
 if(link(tmp,out))return-1;return unlink(tmp);
#endif
}
static int json_commit(atomic_json_t*j,const char*out){int e=0;if(ferror(j->f)||fflush(j->f)||fsync(fileno(j->f)))e=errno?errno:EIO;if(fclose(j->f)&&!e)e=errno;j->f=0;if(!e&&publish_noreplace(j->tmp,out))e=errno;if(e)unlink(j->tmp);free(j->tmp);j->tmp=0;errno=e;return e?-1:0;}
static unsigned failure_serial;
static void failure_path(const char*out,char*path,size_t bytes,unsigned attempt){const char*dot=strrchr(out,'.');size_t stem=dot&&strcmp(dot,".json")==0?(size_t)(dot-out):strlen(out);if(attempt==0){snprintf(path,bytes,"%.*s.failure.json",(int)stem,out);return;}struct timespec t;clock_gettime(CLOCK_REALTIME,&t);snprintf(path,bytes,"%.*s.failure.%.12s.%"PRIu64".%ld.%u.json",(int)stem,out,MSFR_GIT_COMMIT,(uint64_t)t.tv_sec,(long)getpid(),++failure_serial);}
static void write_failure(const char*out,const char*stage,uint32_t n,uint32_t k,const char*candidate,const char*status,const char*binary){for(unsigned attempt=0;attempt<16;++attempt){char path[4096],tmp[4128];failure_path(out,path,sizeof path,attempt);snprintf(tmp,sizeof tmp,"%s.tmp.XXXXXX",path);int fd=mkstemp(tmp);if(fd<0)return;FILE*f=fdopen(fd,"w");if(!f){close(fd);unlink(tmp);return;}fprintf(f,"{\"status\":\"%s\",\"stage\":\"%s\",\"N\":%u,\"K\":%u,\"candidate\":\"%s\",\"git_commit\":\"%s\",\"binary_sha256\":\"%s\",\"contract\":{\"S0\":100,\"r\":0.03,\"q\":0,\"sigma\":0.20,\"T\":1},\"crn_bumps\":{\"epsilon\":0.001,\"spot\":0.1,\"rate\":0.00003,\"sigma\":0.0002}}\n",status,stage,n,k,candidate?candidate:"none",MSFR_GIT_COMMIT,binary);int e=ferror(f);if(fflush(f))e=1;if(fsync(fd))e=1;if(fclose(f))e=1;if(e){unlink(tmp);return;}if(publish_noreplace(tmp,path)==0)return;int saved=errno;unlink(tmp);if(saved!=EEXIST)return;}}

static int audit_run(const char*which){audit_only=1;if(prepare_fixture(16,4))return-1;produce(0);if(!strcmp(which,"basis")){asian_genuine_msfr_basis_forward_diag(b.basis_context,b.basis);}else{asian_genuine_msfr_basis_forward_diag(b.basis_context,b.basis);memset(b.accumulator,0,sizeof*b.accumulator);void(*leaf)(const asian_genuine_msfr_basis_t*,const asian_genuine_msfr_consumer_context_t*,const asian_genuine_msfr_strike_t*,double(*)[4])=0;if(!strcmp(which,"arithmetic_tile2"))leaf=asian_genuine_msfr_arithmetic_tile2_diag;else if(!strcmp(which,"arithmetic_tile4"))leaf=asian_genuine_msfr_arithmetic_tile4_diag;else if(!strcmp(which,"cv_tile2"))leaf=asian_genuine_msfr_cv_tile2_diag;else if(!strcmp(which,"cv_tile4"))leaf=asian_genuine_msfr_cv_tile4_diag;else return-1;for(int i=0;i<64;++i)leaf(b.basis,b.consumer_context,b.strike_controls->strikes,b.accumulator->direct_sums);}printf("audit_leaf=%s checksum=%.17g\n",which,sink+b.basis->values[0][0]);release_fixture();return 0;}

int main(int argc,char**argv){
 const char*out="results/asian_genuine_multistrike_full_risk/aws.json",*audit=0;int check=0;
 for(int i=1;i<argc;++i)if(!strcmp(argv[i],"--check-only"))check=1;else if(!strcmp(argv[i],"--json")&&++i<argc)out=argv[i];else if(!strcmp(argv[i],"--audit-leaf")&&++i<argc)audit=argv[i];else return 2;
 char binary[65]="unavailable";binary_sha(binary);
 if(first_cpu()<0){write_failure(out,"cpu_affinity",0,0,0,"FAIL",binary);return 2;}
 if(check)return preflight()?2:0;
 if(audit)return audit_run(audit)?2:0;
 if(access(out,F_OK)==0){write_failure(out,"success_output_exists",0,0,0,"REFUSED",binary);fprintf(stderr,"refusing to replace existing success JSON: %s\n",out);return 2;}
 atomic_json_t json;if(json_open(&json,out)){write_failure(out,"success_temp_open",0,0,0,"FAIL",binary);return 2;}FILE*j=json.f;
 fprintf(j,"{\"status\":\"PASS\",\"benchmark_provenance\":{\"git_commit\":\"%s\",\"branch\":\"research/asian-multistrike-full-risk\",\"binary_sha256\":\"%s\",\"build_target\":\"tests/Makefile.asian_genuine_multistrike_full_risk:aws-benchmark-native\",\"sobol\":\"genuine_Joe-Kuo_points_8192_through_12287\"},\"contract\":{\"S0\":100,\"r\":0.03,\"q\":0,\"sigma\":0.20,\"T\":1,\"paths\":4096,\"fixing_domain\":[2,256],\"native_N\":[16,32,64,128,256],\"native_K\":[1,4,8,16,32]},\"crn_bumps\":{\"relative_epsilon\":0.001,\"spot_absolute\":0.1,\"rate_absolute\":0.00003,\"sigma_absolute\":0.0002},\"source_accounting\":{\"basis_strip\":{\"sources\":1,\"evolutions\":1},\"our_crn\":{\"sources\":5,\"evolutions\":7},\"onemkl_crn\":{\"gaussian_matrices\":1,\"evolutions\":7},\"onemkl_forward_basis\":{\"gaussian_matrices\":1,\"evolutions\":1},\"repeat_complete\":\"K sources and K evolutions\",\"source_once_repeat\":\"one source and K evolutions\"},\"warmups\":16,\"samples\":51,\"timer\":\"fenced_TSC_and_CLOCK_MONOTONIC_RAW\",\"tsc_units\":\"not_CPU_cycles\",\"working_sets\":{\"qualified_x_growth_bytes\":65536,\"basis_bytes\":131072},\"results\":[",MSFR_GIT_COMMIT,binary);
 int comma=0;uint64_t seed=UINT64_C(0x4d53465253545250);
 static uint64_t complete_wall[N_COUNT][K_COUNT][2][2][CANDIDATES];
 static uint64_t complete_tsc[N_COUNT][K_COUNT][2][2][CANDIDATES];
 for(uint32_t ni=0;ni<N_COUNT;++ni)for(uint32_t ki=0;ki<K_COUNT;++ki)for(int cv=0;cv<2;++cv){
  uint32_t active=CANDIDATES-(ks[ki]>1);
  if(prepare_fixture(ns[ni],ks[ki])){json_abort(&json);write_failure(out,"prepare_fixture",ns[ni],ks[ki],0,"FAIL",binary);return 2;}
  for(int mode=0;mode<2;++mode){
   uint64_t ticks[CANDIDATES][SAMPLES]={{0}},wall[CANDIDATES][SAMPLES]={{0}};
   for(int w=0;w<WARMUPS;++w)for(uint32_t p=0;p<active;++p){uint32_t c=ks[ki]>1?p+1:p;reset_candidate(c);condition(c,mode);checksum(run_candidate(c,cv));if(b.mkl_status)goto fail;}
   for(int s=0;s<SAMPLES;++s){uint32_t order[CANDIDATES];shuffle_active(order,active,&seed);for(uint32_t p=0;p<active;++p){uint32_t c=ks[ki]>1?order[p]+1:order[p];reset_candidate(c);condition(c,mode);uint64_t w0=wall_ns(),t0=tsc0();double value=run_candidate(c,cv);uint64_t t1=tsc1(),w1=wall_ns();if(b.mkl_status)goto fail;ticks[c][s]=t1-t0;wall[c][s]=w1-w0;checksum(value);}}
   uint64_t component_ticks[COMPONENTS][SAMPLES]={{0}},component_wall[COMPONENTS][SAMPLES]={{0}};
   for(int w=0;w<WARMUPS;++w)for(uint32_t c=0;c<COMPONENTS;++c){prepare_component(c);condition(c==TILE4_COMPONENT?BASIS_TILE4:BASIS_TILE2,mode);checksum(run_component(c,cv));}
   for(int s=0;s<SAMPLES;++s){uint32_t order[COMPONENTS];for(uint32_t c=0;c<COMPONENTS;++c)order[c]=c;for(uint32_t c=COMPONENTS-1;c;--c){uint32_t q=(uint32_t)(rng_step(&seed)%(c+1)),v=order[c];order[c]=order[q];order[q]=v;}for(uint32_t p=0;p<COMPONENTS;++p){uint32_t c=order[p];prepare_component(c);condition(c==TILE4_COMPONENT?BASIS_TILE4:BASIS_TILE2,mode);uint64_t w0=wall_ns(),t0=tsc0();double value=run_component(c,cv);uint64_t t1=tsc1(),w1=wall_ns();component_ticks[c][s]=t1-t0;component_wall[c][s]=w1-w0;checksum(value);}}
   asian_genuine_msfr_output_t*reference=a64(sizeof*reference);if(!reference)goto fail;reset_candidate(BASIS_TILE2);run_candidate(BASIS_TILE2,cv);memcpy(reference,b.output,sizeof*reference);double errors[CANDIDATES]={0},checksums[CANDIDATES]={0};for(uint32_t p=0;p<active;++p){uint32_t c=ks[ki]>1?p+1:p;reset_candidate(c);checksums[c]=run_candidate(c,cv);if(b.mkl_status){free(reference);goto fail;}errors[c]=max_difference(reference,b.output,c==EXISTING_PRICE_DELTA?2u:4u);}
   for(uint32_t p=0;p<active;++p){uint32_t c=ks[ki]>1?p+1:p;complete_tsc[ni][ki][cv][mode][c]=quantile(ticks[c],25);complete_wall[ni][ki][cv][mode][c]=quantile(wall[c],25);}
   for(uint32_t p=0;p<active;++p){uint32_t c=ks[ki]>1?p+1:p;uint64_t tm=complete_tsc[ni][ki][cv][mode][c],wm=complete_wall[ni][ki][cv][mode][c];fprintf(j,"%s{\"N\":%u,\"K\":%u,\"estimator\":\"%s\",\"cache_mode\":\"%s\",\"candidate\":\"%s\",\"classification\":\"%s\",\"tsc_p10\":%"PRIu64",\"tsc_median\":%"PRIu64",\"tsc_p90\":%"PRIu64",\"wall_ns_p10\":%"PRIu64",\"wall_ns_median\":%"PRIu64",\"wall_ns_p90\":%"PRIu64",\"amortized_wall_ns_per_strike\":%.9g,\"two_sided_strike_risk_sets_per_second\":%.9g,\"individual_strike_side_risk_sets_per_second\":%.9g,\"numerical_checksum\":%.17g,\"max_abs_error_vs_basis_tile2\":%.9g",comma++?",":"",b.n,b.k,cv?"geometric_cv":"arithmetic",mode?"historical_32KiB_rmw":"warm_candidate_specific",candidate_names[c],c==PHASE1_COMPONENT||c==EXISTING_PRICE_DELTA?"component":"complete_two_sided_full_risk",quantile(ticks[c],5),tm,quantile(ticks[c],45),quantile(wall[c],5),wm,quantile(wall[c],45),(double)wm/b.k,c==EXISTING_PRICE_DELTA?0.0:b.k*1e9/wm,c==EXISTING_PRICE_DELTA?0.0:2.0*b.k*1e9/wm,checksums[c],errors[c]);if(c==BASIS_TILE2||c==BASIS_TILE4){uint32_t other=c==BASIS_TILE2?BASIS_TILE4:BASIS_TILE2;uint64_t pd=complete_wall[ni][ki][cv][mode][EXISTING_PRICE_DELTA];fprintf(j,",\"paired_tile_wall_ratio\":%.9g,\"paired_tile_tsc_ratio\":%.9g,\"paired_speedup_over_K_complete_phase1_wall\":%.9g,\"paired_speedup_over_source_once_K_evolutions_wall\":%.9g,\"paired_speedup_over_our_crn_wall\":%.9g,\"paired_speedup_over_onemkl_crn_wall\":%.9g,\"paired_speedup_over_onemkl_matched_forward_wall\":%.9g,\"incremental_full_risk_wall_over_price_delta\":%.9g,\"basis_input_bytes\":131072",paired_ratio(wall[c],wall[other]),paired_ratio(ticks[c],ticks[other]),paired_ratio(wall[REPEAT_COMPLETE],wall[c]),paired_ratio(wall[SOURCE_ONCE_K_EVOLUTIONS],wall[c]),paired_ratio(wall[CRN_OURS],wall[c]),paired_ratio(wall[CRN_ONEMKL],wall[c]),paired_ratio(wall[ONEMKL_MATCHED_FORWARD],wall[c]),pd?((double)wm-(double)pd)/(double)pd:0.0);if(ki>0){uint64_t prior=complete_wall[ni][ki-1][cv][mode][c],prior_t=complete_tsc[ni][ki-1][cv][mode][c];fprintf(j,",\"marginal_wall_ns_per_added_strike\":%.9g,\"marginal_tsc_units_per_added_strike\":%.9g",((double)wm-prior)/(ks[ki]-ks[ki-1]),((double)tm-prior_t)/(ks[ki]-ks[ki-1]));}}fputs(",\"raw_tsc\":[",j);for(int s=0;s<SAMPLES;++s)fprintf(j,"%s%"PRIu64,s?",":"",ticks[c][s]);fputs("],\"raw_wall_ns\":[",j);for(int s=0;s<SAMPLES;++s)fprintf(j,"%s%"PRIu64,s?",":"",wall[c][s]);fputs("]}",j);}
   for(uint32_t c=0;c<COMPONENTS;++c){uint64_t tm=quantile(component_ticks[c],25),wm=quantile(component_wall[c],25);fprintf(j,"%s{\"N\":%u,\"K\":%u,\"estimator\":\"%s\",\"cache_mode\":\"%s\",\"candidate\":\"%s\",\"classification\":\"timed_component\",\"tsc_p10\":%"PRIu64",\"tsc_median\":%"PRIu64",\"tsc_p90\":%"PRIu64",\"wall_ns_p10\":%"PRIu64",\"wall_ns_median\":%"PRIu64",\"wall_ns_p90\":%"PRIu64",\"raw_tsc\":[",comma++?",":"",b.n,b.k,cv?"geometric_cv":"arithmetic",mode?"historical_32KiB_rmw":"warm_candidate_specific",component_names[c],quantile(component_ticks[c],5),tm,quantile(component_ticks[c],45),quantile(component_wall[c],5),wm,quantile(component_wall[c],45));for(int s=0;s<SAMPLES;++s)fprintf(j,"%s%"PRIu64,s?",":"",component_ticks[c][s]);fputs("],\"raw_wall_ns\":[",j);for(int s=0;s<SAMPLES;++s)fprintf(j,"%s%"PRIu64,s?",":"",component_wall[c][s]);fputs("]}",j);}
   free(reference);
  }
  release_fixture();continue;
fail:{uint32_t fn=b.n,fk=b.k;release_fixture();json_abort(&json);write_failure(out,"timed_candidate",fn,fk,"onemkl","FAIL",binary);return 2;}
 }
 fputs("],\"metric_definitions\":{\"two_sided_strike_risk_set\":\"call and put times price Delta Vega Rho\",\"individual_strike_side_risk_set\":\"one side times price Delta Vega Rho\"}}\n",j);
 if(json_commit(&json,out)){write_failure(out,"success_commit",0,0,0,"FAIL",binary);return 2;}
 return sink==0;
}
