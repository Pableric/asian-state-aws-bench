#define _GNU_SOURCE
#include "asian_genuine_discrete_barrier_carrier/ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "asian_genuine_discrete_barrier_carrier/private/asian_geometric_cv_diag.h"
#include "private/asian_genuine_discrete_barrier_diag.h"

#include <fcntl.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <mkl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

enum { PATHS=4096, WARMUPS=16, QUARTETS=201, STREAM_COUNT=19 };
static const uint32_t ns[5]={16,32,64,128,256};
static const float barriers[4]={80.0f,90.0f,95.0f,100.0f};

typedef enum {
    VANILLA_G,VANILLA_I,SELF_G,SELF_I,EXPLICIT_G,EXPLICIT_I,TABLE_G,TABLE_I,
    COMPLETE_SELF_G,COMPLETE_SELF_I,COMPLETE_EXPLICIT_G,COMPLETE_EXPLICIT_I,
    MKL_COMPLETE,SQL_COMPLETE
} candidate_t;

typedef struct {const char*phase,*name;candidate_t a,b;} stream_t;
static const stream_t streams[STREAM_COUNT]={
 {"selection","self_schedule",SELF_G,SELF_I},
 {"selection","explicit_schedule",EXPLICIT_G,EXPLICIT_I},
 {"selection","self_g_vs_explicit_g",SELF_G,EXPLICIT_G},
 {"selection","self_g_vs_explicit_i",SELF_G,EXPLICIT_I},
 {"selection","self_i_vs_explicit_g",SELF_I,EXPLICIT_G},
 {"selection","self_i_vs_explicit_i",SELF_I,EXPLICIT_I},
 {"qualification","vanilla_vs_self_g",VANILLA_G,SELF_G},
 {"qualification","vanilla_vs_self_i",VANILLA_I,SELF_I},
 {"qualification","vanilla_vs_explicit_g",VANILLA_G,EXPLICIT_G},
 {"qualification","vanilla_vs_explicit_i",VANILLA_I,EXPLICIT_I},
 {"diagnostic","self_g_vs_table_g",SELF_G,TABLE_G},
 {"diagnostic","self_i_vs_table_i",SELF_I,TABLE_I},
 {"diagnostic","explicit_g_vs_table_g",EXPLICIT_G,TABLE_G},
 {"diagnostic","explicit_i_vs_table_i",EXPLICIT_I,TABLE_I},
 {"complete","ours_self_g_vs_onemkl",COMPLETE_SELF_G,MKL_COMPLETE},
 {"complete","ours_self_i_vs_onemkl",COMPLETE_SELF_I,MKL_COMPLETE},
 {"complete","ours_explicit_g_vs_onemkl",COMPLETE_EXPLICIT_G,MKL_COMPLETE},
 {"complete","ours_explicit_i_vs_onemkl",COMPLETE_EXPLICIT_I,MKL_COMPLETE},
 {"diagnostic","ours_self_g_vs_sql_resident",COMPLETE_SELF_G,SQL_COMPLETE}
};

static const char*candidate_name(candidate_t c){
 static const char*n[]={"vanilla_grouped","vanilla_interleaved",
 "resident_self_grouped","resident_self_interleaved",
 "resident_explicit_grouped","resident_explicit_interleaved",
 "mask_table_grouped","mask_table_interleaved","complete_self_grouped",
 "complete_self_interleaved","complete_explicit_grouped",
 "complete_explicit_interleaved","complete_onemkl_point_major",
 "complete_sql_resident_context"};return n[c];}

typedef struct{
 uint32_t n,directions[256][32],*words[2],*pressure;
 float*x,*growth,*pm;fragment_map_t*maps;asian_genuine_route_t*routes;
 asian_barrier_growth_route_t*compact;ordered_d1_diag_context_t*producer;
 asian_genuine_state_t*state,*initial;uint16_t*masks;
 asian_barrier_context_t context;VSLStreamStatePtr base,work;
 float drift,diffusion,barrier,strike;double discount;int put,mkl_status;
}fixture_t;
static fixture_t f;static volatile double sink;

static void*a64(size_t n){void*p=0;if(posix_memalign(&p,64,n))return 0;memset(p,0,n);return p;}
static uint32_t sobol(uint32_t i,const uint32_t*v){uint32_t g=i^(i>>1),w=0;for(uint32_t b=0;g;++b,g>>=1)if(g&1)w^=v[b];return w;}
static uint64_t wall_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC_RAW,&t))abort();return(uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static uint64_t tsc0(void){_mm_lfence();return __rdtsc();}
static uint64_t tsc1(void){unsigned aux;uint64_t t=__rdtscp(&aux);_mm_lfence();return t;}
static uint64_t splitmix(uint64_t*x){uint64_t z=(*x+=UINT64_C(0x9e3779b97f4a7c15));z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);return z^(z>>31);}

typedef struct{uint32_t h[8];uint64_t bits;unsigned used;unsigned char b[64];}sha256_t;
static uint32_t rr(uint32_t x,unsigned n){return(x>>n)|(x<<(32-n));}
static void sha_block(sha256_t*c,const unsigned char*p){static const uint32_t k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];for(int i=16;i<64;++i){uint32_t a=rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3),b=rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+a+w[i-7]+b;}uint32_t a=c->h[0],b=c->h[1],d=c->h[3],e=c->h[4],g=c->h[6],h=c->h[7],cc=c->h[2],ff=c->h[5];for(int i=0;i<64;++i){uint32_t s1=rr(e,6)^rr(e,11)^rr(e,25),ch=(e&ff)^(~e&g),t1=h+s1+ch+k[i]+w[i],s0=rr(a,2)^rr(a,13)^rr(a,22),maj=(a&b)^(a&cc)^(b&cc),t2=s0+maj;h=g;g=ff;ff=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;}c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=ff;c->h[6]+=g;c->h[7]+=h;}
static void sha_init(sha256_t*c){static const uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memset(c,0,sizeof(*c));memcpy(c->h,h,sizeof(h));}
static void sha_add(sha256_t*c,const unsigned char*p,size_t n){c->bits+=(uint64_t)n*8;while(n){size_t take=64-c->used;if(take>n)take=n;memcpy(c->b+c->used,p,take);c->used+=(unsigned)take;p+=take;n-=take;if(c->used==64){sha_block(c,c->b);c->used=0;}}}
static void sha_done(sha256_t*c,char out[65]){uint64_t bits=c->bits;c->b[c->used++]=0x80;if(c->used>56){while(c->used<64)c->b[c->used++]=0;sha_block(c,c->b);c->used=0;}while(c->used<56)c->b[c->used++]=0;for(int i=7;i>=0;--i)c->b[c->used++]=(unsigned char)(bits>>(8*i));sha_block(c,c->b);for(int i=0;i<8;++i)sprintf(out+8*i,"%08"PRIx32,c->h[i]);out[64]=0;}
static int binary_sha256(char out[65]){FILE*in=fopen("/proc/self/exe","rb");if(!in)return-1;sha256_t c;sha_init(&c);unsigned char b[8192];size_t n;while((n=fread(b,1,sizeof(b),in))!=0)sha_add(&c,b,n);if(ferror(in)){fclose(in);return-1;}fclose(in);sha_done(&c,out);return 0;}

static int load_directions(void){FILE*in=fopen("asian_genuine_discrete_barrier_carrier/direction_numbers/joe_kuo_6_21201.bin","rb");if(!in)return-1;for(uint32_t d=0;d<256;++d){uint32_t n;if(fread(&n,4,1,in)!=1||n!=32||fread(f.directions[d],4,32,in)!=32){fclose(in);return-1;}}return fclose(in);}
static uint32_t source_index(const asian_genuine_route_t*r,uint32_t path){uint32_t p=path/32,h=(path/16)&1,l=path&15;return r->map->select[p][h]*16u+r->map->patterns[r->map->select[p][2+h]][l];}

static void produce_ours(void){ordered_d1_x_only_diag(256,f.producer,f.x);asian_vector_exp_range_reduced_array_diag(f.x,f.growth);asian_vector_exp_range_reduced_array_diag(f.x+PATHS,f.growth+PATHS);}

static int prepare_mkl(void){size_t count=3+(size_t)f.n*32;MKL_UINT*p=calloc(count,sizeof(*p));if(!p)return-1;p[0]=f.n;p[1]=VSL_USER_QRNG_INITIAL_VALUES;p[2]=VSL_USER_DIRECTION_NUMBERS;for(uint32_t d=0;d<f.n;++d)for(uint32_t k=0;k<32;++k)p[3+d*32+k]=f.directions[d][k];int e=vslNewStreamEx(&f.base,VSL_BRNG_SOBOL,count,p);free(p);if(e||vslSkipAheadStream(f.base,(long long)(8192-1)*f.n)||vslCopyStream(&f.work,f.base))return-1;VSLStreamStatePtr check=0;if(vslCopyStream(&check,f.base))return-1;uint32_t*raw=a64((size_t)f.n*PATHS*4);if(!raw)return-1;e=viRngUniformBits(VSL_RNG_METHOD_UNIFORMBITS_STD,check,f.n*PATHS,raw);for(uint32_t path=0;!e&&path<PATHS;++path)for(uint32_t d=0;d<f.n;++d)if(raw[(size_t)path*f.n+d]!=sobol(8192+path,f.directions[d]))e=-1;free(raw);vslDeleteStream(&check);return e;}

static void release_fixture(void){if(f.work)vslDeleteStream(&f.work);if(f.base)vslDeleteStream(&f.base);free(f.masks);free(f.initial);free(f.state);free(f.producer);free(f.compact);free(f.routes);free(f.maps);free(f.pm);free(f.growth);free(f.x);free(f.pressure);free(f.words[1]);free(f.words[0]);memset(&f,0,sizeof(f));}

static int prepare_fixture(uint32_t n){memset(&f,0,sizeof(f));f.n=n;f.strike=100;f.discount=exp(-.03);const double dt=1.0/n;f.drift=(float)((.03-.5*.20*.20)*dt);f.diffusion=(float)(.20*sqrt(dt));if(load_directions())return-1;f.words[0]=a64(16384);f.words[1]=a64(16384);f.pressure=a64(32768);f.x=a64(32768);f.growth=a64(32768);f.pm=a64((size_t)n*PATHS*4);f.maps=a64((size_t)n*sizeof(*f.maps));f.routes=a64((size_t)n*sizeof(*f.routes));f.compact=a64((size_t)(n-1)*sizeof(*f.compact));f.producer=a64(sizeof(*f.producer));f.state=a64(sizeof(*f.state));f.initial=a64(sizeof(*f.initial));f.masks=a64(512);if(!f.words[0]||!f.words[1]||!f.pressure||!f.x||!f.growth||!f.pm||!f.maps||!f.routes||!f.compact||!f.producer||!f.state||!f.initial||!f.masks)return-1;for(uint32_t i=0;i<8192;++i)f.pressure[i]=i;for(uint32_t path=0;path<PATHS;++path){f.words[0][path]=sobol(8192+path,f.directions[0]);f.words[1][path]=sobol(12288+path,f.directions[0]);f.initial->s[path]=100;}if(ordered_d1_diag_prepare(f.producer,f.drift,f.diffusion,8192,ORDERED_D1_DIAG_PREPARE_X3,n))return-1;produce_ours();const uint32_t*sw[2]={f.words[0],f.words[1]};const float*xb[2]={f.x,f.x+PATHS},*gb[2]={f.growth,f.growth+PATHS};uint32_t*target=a64(16384);if(!target)return-1;for(uint32_t d=0;d<n;++d){for(uint32_t path=0;path<PATHS;++path)target[path]=sobol(8192+path,f.directions[d]);if(asian_genuine_prepare_route(sw,2,xb,gb,target,d,n,&f.maps[d],&f.routes[d])){free(target);return-1;}for(uint32_t path=0;path<PATHS;++path){uint32_t si=source_index(&f.routes[d],path),block=f.routes[d].x_base==xb[1];if(sw[block][si]!=target[path]){free(target);return-1;}}}free(target);if(asian_barrier_prepare_compact(f.routes,n,100,95,100,f.discount,f.masks,f.compact,&f.context)||prepare_mkl())return-1;return 0;}

static double leaf(candidate_t c){
 const asian_barrier_context_t*x=&f.context;
 if(!f.put){switch(c){case VANILLA_G:return asian_barrier_vanilla_call_grouped_diag(x);case VANILLA_I:return asian_barrier_vanilla_call_interleaved_diag(x);case SELF_G:return asian_barrier_down_call_self_grouped_diag(x);case SELF_I:return asian_barrier_down_call_self_interleaved_diag(x);case EXPLICIT_G:return asian_barrier_down_call_explicit_grouped_diag(x);case EXPLICIT_I:return asian_barrier_down_call_explicit_interleaved_diag(x);case TABLE_G:return asian_barrier_down_call_table_grouped_diag(x);case TABLE_I:return asian_barrier_down_call_table_interleaved_diag(x);default:break;}}
 else{switch(c){case VANILLA_G:return asian_barrier_vanilla_put_grouped_diag(x);case VANILLA_I:return asian_barrier_vanilla_put_interleaved_diag(x);case SELF_G:return asian_barrier_down_put_self_grouped_diag(x);case SELF_I:return asian_barrier_down_put_self_interleaved_diag(x);case EXPLICIT_G:return asian_barrier_down_put_explicit_grouped_diag(x);case EXPLICIT_I:return asian_barrier_down_put_explicit_interleaved_diag(x);case TABLE_G:return asian_barrier_down_put_table_grouped_diag(x);case TABLE_I:return asian_barrier_down_put_table_interleaved_diag(x);default:break;}}
 return 0;
}

static double reduce_sql(void){float a[16]={0},b[16]={0};for(uint32_t path=0;path<PATHS;++path){int alive=(f.masks[path/16]>>(path&15))&1;float p=f.put?f.strike-f.state->s[path]:f.state->s[path]-f.strike;if(p<0||!alive)p=0;float*v=(path&16)?b:a;v[path&15]+=p;}double sum=0;for(int i=0;i<16;++i)sum+=(double)a[i]+b[i];return sum*f.context.payoff_scale;}

static double run_candidate(candidate_t c){if(c>=COMPLETE_SELF_G&&c<=COMPLETE_EXPLICIT_I){produce_ours();return leaf((candidate_t)(SELF_G+(c-COMPLETE_SELF_G)));}if(c==MKL_COMPLETE){f.mkl_status=vsRngGaussian(VSL_RNG_METHOD_GAUSSIAN_ICDF,f.work,f.n*PATHS,f.pm,f.drift,f.diffusion);return f.put?asian_barrier_onemkl_point_major_down_put_diag(f.pm,&f.context):asian_barrier_onemkl_point_major_down_call_diag(f.pm,&f.context);}if(c==SQL_COMPLETE){produce_ours();asian_barrier_sql_resident_diag(f.routes,f.n,f.state,f.barrier,f.masks);return reduce_sql();}return leaf(c);}

static void reset_candidate(candidate_t c){if(c==TABLE_G||c==TABLE_I||c==SQL_COMPLETE)memset(f.masks,0xff,512);if(c==SQL_COMPLETE)memcpy(f.state,f.initial,sizeof(*f.state));if(c>=COMPLETE_SELF_G&&c<=COMPLETE_EXPLICIT_I){memset(f.x,0,32768);memset(f.growth,0,32768);}if(c==MKL_COMPLETE){if(vslCopyStreamState(f.work,f.base))abort();memset(f.pm,0,(size_t)f.n*PATHS*4);}f.mkl_status=0;}

static candidate_t leaf_equivalent(candidate_t c){return c>=COMPLETE_SELF_G&&c<=COMPLETE_EXPLICIT_I?(candidate_t)(SELF_G+c-COMPLETE_SELF_G):c;}
static uintptr_t code_address(candidate_t c){c=leaf_equivalent(c);switch(c){case VANILLA_G:return(uintptr_t)(void*)(f.put?asian_barrier_vanilla_put_grouped_diag:asian_barrier_vanilla_call_grouped_diag);case VANILLA_I:return(uintptr_t)(void*)(f.put?asian_barrier_vanilla_put_interleaved_diag:asian_barrier_vanilla_call_interleaved_diag);case SELF_G:return(uintptr_t)(void*)(f.put?asian_barrier_down_put_self_grouped_diag:asian_barrier_down_call_self_grouped_diag);case SELF_I:return(uintptr_t)(void*)(f.put?asian_barrier_down_put_self_interleaved_diag:asian_barrier_down_call_self_interleaved_diag);case EXPLICIT_G:return(uintptr_t)(void*)(f.put?asian_barrier_down_put_explicit_grouped_diag:asian_barrier_down_call_explicit_grouped_diag);case EXPLICIT_I:return(uintptr_t)(void*)(f.put?asian_barrier_down_put_explicit_interleaved_diag:asian_barrier_down_call_explicit_interleaved_diag);case TABLE_G:return(uintptr_t)(void*)(f.put?asian_barrier_down_put_table_grouped_diag:asian_barrier_down_call_table_grouped_diag);case TABLE_I:return(uintptr_t)(void*)(f.put?asian_barrier_down_put_table_interleaved_diag:asian_barrier_down_call_table_interleaved_diag);case MKL_COMPLETE:return(uintptr_t)(void*)(f.put?asian_barrier_onemkl_point_major_down_put_diag:asian_barrier_onemkl_point_major_down_call_diag);case SQL_COMPLETE:return(uintptr_t)(void*)asian_barrier_sql_resident_diag;default:return 0;}}
static void touch_bytes(const void*p,size_t n,uint64_t*sum){const volatile unsigned char*x=p;for(size_t i=0;i<n;i+=64)*sum+=x[i];}
static void condition(candidate_t c,int mode){uint64_t sum=1;if(mode){for(uint32_t i=0;i<8192;++i){f.pressure[i]+=i+3;sum+=f.pressure[i];}}else{touch_bytes((const void*)code_address(c),64,&sum);touch_bytes(&f.context,sizeof(f.context),&sum);if(c==MKL_COMPLETE){touch_bytes(f.pm,(size_t)f.n*PATHS*4,&sum);}else if(c==SQL_COMPLETE){touch_bytes(f.producer,sizeof(*f.producer),&sum);touch_bytes(f.x,32768,&sum);touch_bytes(f.growth,32768,&sum);touch_bytes(f.routes,(size_t)f.n*sizeof(*f.routes),&sum);touch_bytes(f.maps,(size_t)f.n*sizeof(*f.maps),&sum);touch_bytes(f.state,sizeof(*f.state),&sum);}else{touch_bytes(f.growth,32768,&sum);touch_bytes(f.compact,(size_t)(f.n-1)*sizeof(*f.compact),&sum);touch_bytes(f.maps,(size_t)f.n*sizeof(*f.maps),&sum);if(c>=COMPLETE_SELF_G&&c<=COMPLETE_EXPLICIT_I){touch_bytes(f.producer,sizeof(*f.producer),&sum);touch_bytes(f.x,32768,&sum);}if(c==TABLE_G||c==TABLE_I)touch_bytes(f.masks,512,&sum);}}sink+=sum;}
static void checksum(double v){sink+=v;if(f.mkl_status)abort();}

typedef struct{uint64_t t[4],w[4];char pattern;}quartet_t;
static void measure_one(candidate_t c,int mode,uint64_t*t,uint64_t*w){reset_candidate(c);condition(c,mode);uint64_t w0=wall_ns(),t0=tsc0();double v=run_candidate(c);uint64_t t1=tsc1(),w1=wall_ns();*t=t1-t0;*w=w1-w0;checksum(v);}
static void patterns(uint8_t p[QUARTETS],uint64_t seed){for(int i=0;i<QUARTETS;++i)p[i]=(i>=101);for(int i=QUARTETS-1;i>0;--i){int j=(int)(splitmix(&seed)%(uint64_t)(i+1));uint8_t q=p[i];p[i]=p[j];p[j]=q;}}

static void run_stream(FILE*out,const stream_t*s,int mode,uint64_t seed){for(int i=0;i<WARMUPS;++i){candidate_t o[4];if(i&1){o[0]=s->b;o[1]=s->a;o[2]=s->a;o[3]=s->b;}else{o[0]=s->a;o[1]=s->b;o[2]=s->b;o[3]=s->a;}for(int j=0;j<4;++j){uint64_t t,w;measure_one(o[j],mode,&t,&w);}}uint8_t pat[QUARTETS];patterns(pat,seed);fprintf(out,"{\"phase\":\"%s\",\"comparison\":\"%s\",\"a\":\"%s\",\"b\":\"%s\",\"quartets\":[",s->phase,s->name,candidate_name(s->a),candidate_name(s->b));for(int q=0;q<QUARTETS;++q){candidate_t o[4];if(pat[q]){o[0]=s->b;o[1]=s->a;o[2]=s->a;o[3]=s->b;}else{o[0]=s->a;o[1]=s->b;o[2]=s->b;o[3]=s->a;}quartet_t r={.pattern=pat[q]?'B':'A'};for(int j=0;j<4;++j)measure_one(o[j],mode,&r.t[j],&r.w[j]);fprintf(out,"%s{\"pattern\":\"%s\",\"tsc\":[%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"],\"wall_ns\":[%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"]}",q?",":"",pat[q]?"BAAB":"ABBA",r.t[0],r.t[1],r.t[2],r.t[3],r.w[0],r.w[1],r.w[2],r.w[3]);}fputs("]}",out);}

static uint64_t seed_for_stream(const stream_t*s,uint64_t cell){uint64_t base=UINT64_C(0x424152525155414c);if(!strcmp(s->phase,"selection")){base=strstr(s->name,"schedule")?UINT64_C(0x4241525253434844):UINT64_C(0x424152524d41534b);}return base^cell;}

static int preflight(void){for(uint32_t ni=0;ni<5;++ni){if(prepare_fixture(ns[ni]))return-1;for(int put=0;put<2;++put){f.put=put;for(uint32_t bi=0;bi<4;++bi){f.barrier=barriers[bi];if(asian_barrier_prepare_compact(f.routes,f.n,100,f.barrier,100,f.discount,f.masks,f.compact,&f.context))return-1;double sg=leaf(SELF_G),si=leaf(SELF_I),eg=leaf(EXPLICIT_G),ei=leaf(EXPLICIT_I);memset(f.masks,0xff,512);double tg=leaf(TABLE_G);memset(f.masks,0xff,512);double ti=leaf(TABLE_I);if(sg!=si||sg!=eg||sg!=ei||sg!=tg||sg!=ti||!isfinite(sg))return-1;reset_candidate(MKL_COMPLETE);double mk=run_candidate(MKL_COMPLETE);if(f.mkl_status||!isfinite(mk))return-1;}f.barrier=FLT_MIN;if(asian_barrier_prepare_compact(f.routes,f.n,100,f.barrier,100,f.discount,f.masks,f.compact,&f.context)||leaf(VANILLA_G)!=leaf(SELF_G))return-1;}release_fixture();}return 0;}

static int pin_first_cpu(void){cpu_set_t allowed,set;CPU_ZERO(&allowed);if(sched_getaffinity(0,sizeof(allowed),&allowed))return-1;int cpu=0;while(cpu<CPU_SETSIZE&&!CPU_ISSET(cpu,&allowed))++cpu;if(cpu==CPU_SETSIZE)return-1;CPU_ZERO(&set);CPU_SET(cpu,&set);return sched_setaffinity(0,sizeof(set),&set)?-1:cpu;}
static void cpu_model(char out[160]){strcpy(out,"unknown");FILE*in=fopen("/proc/cpuinfo","r");if(!in)return;char line[256];while(fgets(line,sizeof(line),in))if(!strncmp(line,"model name",10)){char*p=strchr(line,':');if(p){while(*++p==' ');strncpy(out,p,159);out[159]=0;out[strcspn(out,"\r\n")]=0;}break;}fclose(in);}

static void write_knockout_profile(FILE*out,float barrier){float*s=a64(PATHS*4);uint16_t*m=a64(512);if(!s||!m)abort();for(uint32_t i=0;i<PATHS;++i)s[i]=100;memset(m,0xff,512);fprintf(out,"{\"N\":%u,\"barrier\":%.9g,\"dates\":[",f.n,barrier);for(uint32_t d=0;d<f.n;++d){for(uint32_t path=0;path<PATHS;++path){uint32_t si=source_index(&f.routes[d],path);s[path]*=f.routes[d].growth_base[si];if(!(isfinite(s[path])&&s[path]>barrier))m[path/16]&=(uint16_t)~(1u<<(path&15));}uint32_t alive=0,dead16=0,dead32=0;for(uint32_t h=0;h<256;++h){alive+=(uint32_t)__builtin_popcount(m[h]);dead16+=m[h]==0;}for(uint32_t p=0;p<128;++p)dead32+=m[2*p]==0&&m[2*p+1]==0;fprintf(out,"%s{\"date\":%u,\"alive\":%u,\"knockout_percent\":%.9g,\"dead_16_lane_groups\":%u,\"dead_32_path_packets\":%u}",d?",":"",d+1,alive,100.0*(PATHS-alive)/PATHS,dead16,dead32);}fputs("]}",out);free(m);free(s);}

int main(int argc,char**argv)
{
 const char*outpath="results/asian_genuine_discrete_barrier/aws.json",*audit=NULL;int check=0;
 for(int i=1;i<argc;++i)if(!strcmp(argv[i],"--check-only"))check=1;
 else if(!strcmp(argv[i],"--json")&&++i<argc)outpath=argv[i];
 else if(!strcmp(argv[i],"--audit-symbol")&&++i<argc)audit=argv[i];else return 2;
 int cpu=pin_first_cpu();if(cpu<0)return 2;
 if(audit){candidate_t c;if(!strcmp(audit,"vanilla"))c=VANILLA_G;else if(!strcmp(audit,"self"))c=SELF_G;else if(!strcmp(audit,"explicit"))c=EXPLICIT_G;else if(!strcmp(audit,"table"))c=TABLE_G;else return 2;if(prepare_fixture(256))return 2;f.put=0;f.barrier=95;if(asian_barrier_prepare_compact(f.routes,f.n,100,f.barrier,100,f.discount,f.masks,f.compact,&f.context))return 2;reset_candidate(c);double v=run_candidate(c);printf("audit_symbol=%s value=%.17g\n",audit,v);release_fixture();return 0;}
 if(preflight())return 2;
 if(check){char h[65];if(binary_sha256(h))return 2;printf("asian_genuine_discrete_barrier native_preflight=PASS source=corrected_x_then_vector_exp d1=direct_observed_after_update binary_sha256=%s\n",h);return 0;}
 int fd=open(outpath,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0){perror(outpath);return 2;}
 FILE*out=fdopen(fd,"w");if(!out)return 2;char model[160],binary_hash[65];cpu_model(model);if(binary_sha256(binary_hash))return 2;
 fprintf(out,"{\"status\":\"RAW_NATIVE_TIMINGS\",\"binary_sha256\":\"%s\",\"cpu\":%d,\"cpu_model\":\"%s\",\"paths\":4096,\"warmup_quartets\":16,\"measured_quartets\":201,\"cache_bytes\":{\"l1d\":%ld,\"l1i\":%ld,\"l2\":%ld,\"l3\":%ld},\"timer\":\"LFENCE_RDTSC_RDTSCP_LFENCE_and_CLOCK_MONOTONIC_RAW\",\"seeds\":{\"schedule\":\"0x4241525253434844\",\"mask\":\"0x424152524d41534b\",\"qualification\":\"0x424152525155414c\",\"bootstrap\":\"0x42415252424f4f54\",\"control\":\"0x424152524354524c\"},\"cells\":[",binary_hash,cpu,model,sysconf(_SC_LEVEL1_DCACHE_SIZE),sysconf(_SC_LEVEL1_ICACHE_SIZE),sysconf(_SC_LEVEL2_CACHE_SIZE),sysconf(_SC_LEVEL3_CACHE_SIZE));
 int comma=0;
 for(uint32_t ni=0;ni<5;++ni){if(prepare_fixture(ns[ni]))return 2;
  for(int put=0;put<2;++put)for(uint32_t bi=0;bi<4;++bi)for(int mode=0;mode<2;++mode){f.put=put;f.barrier=barriers[bi];if(asian_barrier_prepare_compact(f.routes,f.n,100,f.barrier,100,f.discount,f.masks,f.compact,&f.context))return 2;uint32_t bits;memcpy(&bits,&f.barrier,4);fprintf(out,"%s{\"N\":%u,\"option\":\"%s\",\"barrier\":%.9g,\"barrier_bits\":\"0x%08"PRIx32"\",\"strike\":100,\"mode\":\"%s\",\"streams\":[",comma++?",":"",f.n,put?"put":"call",f.barrier,bits,mode?"historical_32KiB_rmw":"candidate_warm");for(int si=0;si<STREAM_COUNT;++si){if(si)fputc(',',out);uint64_t cell=((uint64_t)ni<<48)^((uint64_t)put<<40)^((uint64_t)bi<<32)^((uint64_t)mode<<24)^(uint64_t)si;run_stream(out,&streams[si],mode,seed_for_stream(&streams[si],cell));}fputs("]}",out);}
  release_fixture();
 }
 fputs("],\"controls\":[",out);comma=0;
 const stream_t aa={"control","byte_identical_AA",VANILLA_G,VANILLA_G};
 const stream_t nb={"control","no_barrier_vanilla_vs_resident",VANILLA_G,SELF_G};
 for(uint32_t ni=0;ni<5;ni+=4){if(prepare_fixture(ns[ni]))return 2;for(int put=0;put<2;++put)for(int mode=0;mode<2;++mode){f.put=put;f.barrier=FLT_MIN;if(asian_barrier_prepare_compact(f.routes,f.n,100,f.barrier,100,f.discount,f.masks,f.compact,&f.context))return 2;fprintf(out,"%s{\"N\":%u,\"option\":\"%s\",\"mode\":\"%s\",\"streams\":[",comma++?",":"",f.n,put?"put":"call",mode?"historical_32KiB_rmw":"candidate_warm");run_stream(out,&aa,mode,UINT64_C(0x424152524354524c)^ni^((uint64_t)put<<32)^mode);fputc(',',out);run_stream(out,&nb,mode,UINT64_C(0x424152524354524c)^UINT64_C(0x10000)^ni^((uint64_t)put<<32)^mode);fputs("]}",out);}release_fixture();}
 fputs("],\"knockout_profiles\":[",out);comma=0;
 for(uint32_t ni=0;ni<5;++ni){if(prepare_fixture(ns[ni]))return 2;for(uint32_t bi=0;bi<4;++bi){if(comma++)fputc(',',out);write_knockout_profile(out,barriers[bi]);}release_fixture();}
 fprintf(out,"],\"conditioning\":{\"candidate_warm\":\"reset_then_candidate_specific_touch_immediately_before_timing\",\"historical_32KiB_rmw\":\"reset_then_exact_8192_float_pressure_i_plus_equals_i_plus_3_immediately_before_timing_no_following_warm\"},\"notes\":[\"S0 is not an observation date\",\"D1 is updated and observed directly before compact entry zero D2\",\"dead-lane counts are diagnostic and no acceleration claim is made\"],\"sink\":%.17g}\n",sink);
 if(fclose(out))return 2;
 return sink==0;
}
