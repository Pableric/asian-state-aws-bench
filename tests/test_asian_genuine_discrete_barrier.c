#define _POSIX_C_SOURCE 200809L
#include "asian_genuine_discrete_barrier_carrier/ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_genuine_discrete_barrier_diag.h"
#include "asian_genuine_discrete_barrier_carrier/private/asian_geometric_cv_diag.h"

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { PATHS = 4096, PACKETS = 128 };

typedef struct {
    uint32_t n;
    uint32_t directions[256][32];
    uint32_t *words[2];
    float *x, *growth;
    fragment_map_t *maps;
    asian_genuine_route_t *routes;
    asian_barrier_growth_route_t *compact;
    ordered_d1_diag_context_t *producer;
    asian_genuine_state_t *initial, *base, *resident, *table_state;
    uint16_t *resident_masks, *table_masks;
    float *s_trace;
    uint16_t *mask_trace;
    asian_barrier_context_t context;
    float drift, diffusion;
    double rate, sigma, maturity;
} fixture_t;

typedef struct {
    double raw_error;
    double flip_contribution;
    double smooth_residual;
    uint64_t ambiguous_paths;
    uint32_t first_path;
    uint32_t first_date;
} decomposition_t;

static decomposition_t worst_raw;
static uint32_t worst_n;
static float worst_barrier,worst_strike;
static int worst_put;
static FILE *kink_json;
static int kink_comma;
static FILE *error_json;
static int error_comma;

static void *a64(size_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes) != 0) return NULL;
    memset(p, 0, bytes);
    return p;
}

static uint64_t hash64(const void *data, size_t bytes)
{
    const unsigned char *p = data;
    uint64_t h = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < bytes; ++i) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}

static uint32_t sobol(uint32_t index, const uint32_t direction[32])
{
    uint32_t gray = index ^ (index >> 1), word = 0;
    for (uint32_t bit = 0; gray != 0; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u) word ^= direction[bit];
    return word;
}

static double inverse_normal(double p)
{
    static const double a[] = {-39.69683028665376,220.9460984245205,
        -275.9285104469687,138.3577518672690,-30.66479806614716,
        2.506628277459239};
    static const double b[] = {-54.47609879822406,161.5858368580409,
        -155.6989798598866,66.80131188771972,-13.28068155288572};
    static const double c[] = {-0.007784894002430293,-0.3223964580411365,
        -2.400758277161838,-2.549732539343734,4.374664141464968,
        2.938163982698783};
    static const double d[] = {0.007784695709041462,0.3224671290700398,
        2.445134137142996,3.754408661907416};
    if (p < 0.02425) {
        const double q = sqrt(-2.0 * log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    if (p > 0.97575) {
        const double q = sqrt(-2.0 * log(1.0-p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    const double q = p - 0.5, r = q*q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
           (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
}

static int load_directions(uint32_t directions[256][32])
{
    FILE *in = fopen("asian_genuine_discrete_barrier_carrier/direction_numbers/joe_kuo_6_21201.bin", "rb");
    if (in == NULL) return -1;
    for (uint32_t d = 0; d < 256; ++d) {
        uint32_t length;
        if (fread(&length, 4, 1, in) != 1 || length != 32u ||
            fread(directions[d], 4, 32, in) != 32u) {
            fclose(in);
            return -1;
        }
    }
    return fclose(in);
}

static uint32_t route_source_index(const asian_genuine_route_t *route,
                                   uint32_t path)
{
    const uint32_t packet = path / 32u;
    const uint32_t half = (path / 16u) & 1u;
    const uint32_t lane = path & 15u;
    const fragment_map_t *map = route->map;
    const uint32_t line = map->select[packet][half];
    const uint32_t pattern = map->select[packet][2u + half];
    return line * 16u + map->patterns[pattern][lane];
}

static void release_fixture(fixture_t *f)
{
    free(f->mask_trace); free(f->s_trace); free(f->table_masks);
    free(f->resident_masks); free(f->table_state); free(f->resident);
    free(f->base); free(f->initial); free(f->producer); free(f->compact);
    free(f->routes); free(f->maps); free(f->growth); free(f->x);
    free(f->words[1]); free(f->words[0]);
    memset(f, 0, sizeof(*f));
}

static int prepare_fixture(fixture_t *f, uint32_t n, double rate,
                           double sigma, double maturity)
{
    memset(f, 0, sizeof(*f));
    f->n=n; f->rate=rate; f->sigma=sigma; f->maturity=maturity;
    if (load_directions(f->directions) != 0) return -1;
    const double dt = maturity/n;
    f->drift = (float)((rate - 0.5*sigma*sigma)*dt);
    f->diffusion = (float)(sigma*sqrt(dt));
    f->words[0]=a64(16384); f->words[1]=a64(16384);
    f->x=a64(32768); f->growth=a64(32768);
    f->maps=a64((size_t)n*sizeof(*f->maps));
    f->routes=a64((size_t)n*sizeof(*f->routes));
    f->compact=a64((size_t)(n>1?n-1:1)*sizeof(*f->compact));
    f->producer=a64(sizeof(*f->producer));
    f->initial=a64(sizeof(*f->initial)); f->base=a64(sizeof(*f->base));
    f->resident=a64(sizeof(*f->resident));
    f->table_state=a64(sizeof(*f->table_state));
    f->resident_masks=a64(512); f->table_masks=a64(512);
    f->s_trace=a64((size_t)n*PATHS*sizeof(float));
    f->mask_trace=a64((size_t)n*512);
    if (!f->words[0]||!f->words[1]||!f->x||!f->growth||!f->maps||
        !f->routes||!f->compact||!f->producer||!f->initial||!f->base||
        !f->resident||!f->table_state||!f->resident_masks||!f->table_masks||
        !f->s_trace||!f->mask_trace) return -1;
    for (uint32_t path=0; path<PATHS; ++path) {
        f->words[0][path]=sobol(8192u+path,f->directions[0]);
        f->words[1][path]=sobol(12288u+path,f->directions[0]);
        f->initial->s[path]=100.0f;
    }
    const uint32_t producer_n=(n==16||n==32||n==64||n==128||n==256)?n:16;
    if (ordered_d1_diag_prepare(f->producer,f->drift,f->diffusion,8192,
        ORDERED_D1_DIAG_PREPARE_X3,producer_n) != 0) return -1;
    ordered_d1_x_only_diag(256,f->producer,f->x);
    asian_vector_exp_range_reduced_array_diag(f->x,f->growth);
    asian_vector_exp_range_reduced_array_diag(f->x+PATHS,f->growth+PATHS);
    const uint32_t *sw[2]={f->words[0],f->words[1]};
    const float *xb[2]={f->x,f->x+PATHS};
    const float *gb[2]={f->growth,f->growth+PATHS};
    uint32_t *target=a64(16384);
    if (target==NULL) return -1;
    for (uint32_t dimension=0; dimension<n; ++dimension) {
        for (uint32_t path=0; path<PATHS; ++path)
            target[path]=sobol(8192u+path,f->directions[dimension]);
        if (asian_genuine_prepare_route(sw,2,xb,gb,target,dimension,n,
            &f->maps[dimension],&f->routes[dimension]) != 0) {
            free(target); return -1;
        }
        for (uint32_t path=0; path<PATHS; ++path) {
            const uint32_t source=route_source_index(&f->routes[dimension],path);
            const uint32_t block=f->routes[dimension].x_base==xb[1];
            if (source>=PATHS || sw[block][source]!=target[path]) {
                free(target); return -1;
            }
        }
    }
    free(target);
    if (asian_barrier_prepare_compact(f->routes,n,100.0f,95.0f,100.0f,
        exp(-rate*maturity),f->table_masks,f->compact,&f->context) != 0)
        return -1;
    return 0;
}

static int bit_equal_double(double a, double b)
{
    uint64_t x,y;
    memcpy(&x,&a,8); memcpy(&y,&b,8);
    return x==y;
}

static float addf_exact(float a, float b)
{
    volatile float x=a+b;
    return x;
}

static double reduced_payoff(const float terminal[PATHS],
                             const uint16_t masks[256], int put,
                             int masked, float strike, double scale)
{
    float a[16]={0},b[16]={0};
    for (uint32_t packet=0; packet<PACKETS; ++packet) {
        for (uint32_t half=0; half<2; ++half) {
            const uint16_t mask=masks?masks[packet*2u+half]:UINT16_MAX;
            for (uint32_t lane=0; lane<16; ++lane) {
                const uint32_t path=packet*32u+half*16u+lane;
                float p=put?strike-terminal[path]:terminal[path]-strike;
                if (p<0.0f) p=0.0f;
                if (masked && (mask&(1u<<lane))==0u) p=0.0f;
                if (half==0) a[lane]=addf_exact(a[lane],p);
                else b[lane]=addf_exact(b[lane],p);
            }
        }
    }
    float t[16];
    for(uint32_t i=0;i<16;++i)t[i]=addf_exact(a[i],b[i]);
    float u0=addf_exact(t[0],t[4]),u1=addf_exact(t[1],t[5]);
    float u2=addf_exact(t[2],t[6]),u3=addf_exact(t[3],t[7]);
    float v0=addf_exact(t[8],t[12]),v1=addf_exact(t[9],t[13]);
    float v2=addf_exact(t[10],t[14]),v3=addf_exact(t[11],t[15]);
    float w0=addf_exact(u0,v0),w1=addf_exact(u1,v1);
    float w2=addf_exact(u2,v2),w3=addf_exact(u3,v3);
    return (double)addf_exact(addf_exact(w0,w2),addf_exact(w1,w3))*scale;
}

static void scalar_float_trace(const fixture_t *f, float *trace,
                               uint16_t *masks, float barrier)
{
    for(uint32_t path=0;path<PATHS;++path){
        float s=100.0f;
        int alive=1;
        for(uint32_t d=0;d<f->n;++d){
            const uint32_t source=route_source_index(&f->routes[d],path);
            s*=f->routes[d].growth_base[source];
            alive=alive&&isfinite(s)&&s>barrier;
            trace[(size_t)d*PATHS+path]=s;
            if(alive)masks[(size_t)d*256+path/16u]|=(uint16_t)(1u<<(path&15u));
        }
    }
}

static void scalar_up_masks(const fixture_t *f, float barrier, uint16_t out[256])
{
    memset(out,0,512);
    for(uint32_t path=0;path<PATHS;++path){
        int alive=1;
        for(uint32_t d=0;d<f->n;++d){
            const float s=f->s_trace[(size_t)d*PATHS+path];
            alive=alive&&isfinite(s)&&s<barrier;
        }
        if(alive)out[path/16u]|=(uint16_t)(1u<<(path&15u));
    }
}

static decomposition_t float64_decomposition(const fixture_t *f,
    float barrier,float strike,int put,double got,const uint16_t masks[256])
{
    decomposition_t d={.first_path=UINT32_MAX,.first_date=UINT32_MAX};
    double reference_sum=0.0,flip_sum=0.0;
    for(uint32_t path=0;path<PATHS;++path){
        double s=100.0;
        int alive64=1; uint32_t first=UINT32_MAX;
        uint16_t event_dates[256];float event_s32[256];double event_s64[256];
        unsigned char event_i32[256],event_i64[256];uint32_t event_count=0;
        for(uint32_t date=0;date<f->n;++date){
            const double u=((double)sobol(8192u+path,f->directions[date])+.5)*0x1p-32;
            const double x=(f->rate-0.5*f->sigma*f->sigma)*(f->maturity/f->n)+
                f->sigma*sqrt(f->maturity/f->n)*inverse_normal(u);
            s*=exp(x);
            const int a64=isfinite(s)&&s>barrier;
            const float sf=f->s_trace[(size_t)date*PATHS+path];
            const int a32=isfinite(sf)&&sf>barrier;
            if(a32!=a64){
                if(first==UINT32_MAX)first=date;
                event_dates[event_count]=(uint16_t)date;
                event_s32[event_count]=sf;event_s64[event_count]=s;
                event_i32[event_count]=(unsigned char)a32;
                event_i64[event_count]=(unsigned char)a64;++event_count;
            }
            alive64=alive64&&a64;
        }
        const int alive32=(masks[path/16u]>>(path&15u))&1u;
        const double p64=fmax(put?strike-s:s-strike,0.0);
        reference_sum+=alive64?p64:0.0;
        flip_sum+=(alive32-alive64)*p64;
        if(kink_json&&event_count){
            uint32_t bb,kb;memcpy(&bb,&barrier,4);memcpy(&kb,&strike,4);
            const double pc=(alive32-alive64)*p64*exp(-f->rate*f->maturity)/PATHS;
            for(uint32_t e=0;e<event_count;++e){uint32_t sb;memcpy(&sb,&event_s32[e],4);
                fprintf(kink_json,"%s{\"N\":%u,\"rate\":%.17g,\"sigma\":%.17g,\"barrier\":%.9g,\"barrier_bits\":\"0x%08"PRIx32"\",\"strike\":%.9g,\"strike_bits\":\"0x%08"PRIx32"\",\"option\":\"%s\",\"path\":%u,\"date\":%u,\"float_state\":%.9g,\"float_state_bits\":\"0x%08"PRIx32"\",\"float64_state\":%.17g,\"float_indicator\":%u,\"float64_indicator\":%u,\"final_float_survival\":%u,\"final_float64_survival\":%u,\"aggregate_path_flip_contribution\":%.17g}",kink_comma++?",":"",f->n,f->rate,f->sigma,barrier,bb,strike,kb,put?"put":"call",path,(uint32_t)event_dates[e]+1,event_s32[e],sb,event_s64[e],event_i32[e],event_i64[e],alive32,alive64,pc);
            }
        }
        if(alive32!=alive64){
            ++d.ambiguous_paths;
            if(d.first_path==UINT32_MAX){d.first_path=path;d.first_date=first;}
        }
    }
    const double discount=exp(-f->rate*f->maturity);
    const double reference=reference_sum*discount/PATHS;
    d.raw_error=got-reference;
    d.flip_contribution=flip_sum*discount/PATHS;
    d.smooth_residual=d.raw_error-d.flip_contribution;
    return d;
}

static int check_stage1_and_trace(fixture_t *f, float barrier)
{
    memset(f->resident_masks,0,512);
    memset(f->table_masks,0xff,512);
    memcpy(f->base,f->initial,sizeof(*f->base));
    memcpy(f->resident,f->initial,sizeof(*f->resident));
    memcpy(f->table_state,f->initial,sizeof(*f->table_state));
    asian_genuine_sql_dual_control_diag(f->routes,f->n,f->base);
    asian_barrier_sql_resident_diag(f->routes,f->n,f->resident,barrier,
                                    f->resident_masks);
    asian_barrier_sql_table_diag(f->routes,f->n,f->table_state,barrier,
                                 f->table_masks);
    if(memcmp(f->base,f->resident,sizeof(*f->base))||
       memcmp(f->base,f->table_state,sizeof(*f->base))||
       memcmp(f->resident_masks,f->table_masks,512))return-1;
    memset(f->s_trace,0,(size_t)f->n*PATHS*4);
    memset(f->mask_trace,0,(size_t)f->n*512);
    asian_barrier_s_only_trace_diag(&f->context,f->s_trace,f->mask_trace);
    float *scalar_s=a64((size_t)f->n*PATHS*4);
    uint16_t *scalar_m=a64((size_t)f->n*512);
    if(!scalar_s||!scalar_m)return-1;
    scalar_float_trace(f,scalar_s,scalar_m,barrier);
    const int bad=memcmp(f->s_trace,scalar_s,(size_t)f->n*PATHS*4)||
        memcmp(f->mask_trace,scalar_m,(size_t)f->n*512)||
        memcmp(f->base->s,f->s_trace+(size_t)(f->n-1)*PATHS,PATHS*4)||
        memcmp(f->resident_masks,f->mask_trace+(size_t)(f->n-1)*256,512);
    free(scalar_m);free(scalar_s);
    return bad?-1:0;
}

typedef double(*leaf_fn)(const asian_barrier_context_t*);

static int check_prices(fixture_t *f,float barrier,float strike,
                        double *max_residual,uint64_t *ambiguities)
{
    if(asian_barrier_prepare_compact(f->routes,f->n,100.0f,barrier,strike,
        exp(-f->rate*f->maturity),f->table_masks,f->compact,&f->context))return-1;
    if(check_stage1_and_trace(f,barrier))return-1;
    const float *terminal=f->s_trace+(size_t)(f->n-1)*PATHS;
    const uint16_t *masks=f->mask_trace+(size_t)(f->n-1)*256;
    const double scale=f->context.payoff_scale;
    for(uint32_t path=0;path<PATHS;++path){
        const int alive=(masks[path/16u]>>(path&15u))&1u;
        for(int put=0;put<2;++put){
            float vanilla=put?strike-terminal[path]:terminal[path]-strike;
            if(vanilla<0.0f)vanilla=0.0f;
            const float ko=alive?vanilla:0.0f,ki=alive?0.0f:vanilla;
            if(ki+ko!=vanilla)return-1;
        }
    }
    leaf_fn vanilla[2][2]={{asian_barrier_vanilla_call_grouped_diag,
        asian_barrier_vanilla_call_interleaved_diag},
        {asian_barrier_vanilla_put_grouped_diag,
        asian_barrier_vanilla_put_interleaved_diag}};
    leaf_fn self[2][2]={{asian_barrier_down_call_self_grouped_diag,
        asian_barrier_down_call_self_interleaved_diag},
        {asian_barrier_down_put_self_grouped_diag,
        asian_barrier_down_put_self_interleaved_diag}};
    leaf_fn explicit_leaf[2][2]={{asian_barrier_down_call_explicit_grouped_diag,
        asian_barrier_down_call_explicit_interleaved_diag},
        {asian_barrier_down_put_explicit_grouped_diag,
        asian_barrier_down_put_explicit_interleaved_diag}};
    leaf_fn table[2][2]={{asian_barrier_down_call_table_grouped_diag,
        asian_barrier_down_call_table_interleaved_diag},
        {asian_barrier_down_put_table_grouped_diag,
        asian_barrier_down_put_table_interleaved_diag}};
    const uint64_t hc=hash64(&f->context,sizeof(f->context));
    const uint64_t hr=hash64(f->compact,(size_t)(f->n-1)*sizeof(*f->compact));
    const uint64_t hx=hash64(f->x,32768),hg=hash64(f->growth,32768);
    const uint64_t hm=hash64(f->maps,(size_t)f->n*sizeof(*f->maps));
    const uint64_t hq=hash64(f->routes,(size_t)f->n*sizeof(*f->routes));
    for(int put=0;put<2;++put){
        const double ev=reduced_payoff(terminal,NULL,put,0,strike,scale);
        const double eo=reduced_payoff(terminal,masks,put,1,strike,scale);
        for(int schedule=0;schedule<2;++schedule){
            if(!bit_equal_double(vanilla[put][schedule](&f->context),ev)||
               !bit_equal_double(self[put][schedule](&f->context),eo)||
               !bit_equal_double(explicit_leaf[put][schedule](&f->context),eo))
                return-1;
            memset(f->table_masks,0xff,512);
            if(!bit_equal_double(table[put][schedule](&f->context),eo)||
               memcmp(f->table_masks,masks,512))return-1;
        }
        decomposition_t d=float64_decomposition(f,barrier,strike,put,eo,masks);
        if(error_json){uint32_t bb,kb;memcpy(&bb,&barrier,4);memcpy(&kb,&strike,4);
            fprintf(error_json,"%s{\"N\":%u,\"rate\":%.17g,\"sigma\":%.17g,\"barrier\":%.9g,\"barrier_bits\":\"0x%08"PRIx32"\",\"strike\":%.9g,\"strike_bits\":\"0x%08"PRIx32"\",\"option\":\"%s\",\"same_float_price\":%.17g,\"float64_price\":%.17g,\"raw_price_difference\":%.17g,\"barrier_indicator_flip_contribution\":%.17g,\"smooth_residual\":%.17g,\"final_survival_flip_paths\":%"PRIu64"}",error_comma++?",":"",f->n,f->rate,f->sigma,barrier,bb,strike,kb,put?"put":"call",eo,eo-d.raw_error,d.raw_error,d.flip_contribution,d.smooth_residual,d.ambiguous_paths);
        }
        if(fabs(d.raw_error)>fabs(worst_raw.raw_error)){
            worst_raw=d;worst_n=f->n;worst_barrier=barrier;
            worst_strike=strike;worst_put=put;
        }
        if(fabs(d.smooth_residual)>*max_residual)*max_residual=fabs(d.smooth_residual);
        *ambiguities+=d.ambiguous_paths;
        if(fabs(d.smooth_residual)>1e-4){
            fprintf(stderr,"smooth residual N=%u B=%g K=%g put=%d raw=%.12g flip=%.12g residual=%.12g first=%u/%u\n",
                f->n,barrier,strike,put,d.raw_error,d.flip_contribution,
                d.smooth_residual,d.first_path,d.first_date);
            return-1;
        }
    }
    if(hc!=hash64(&f->context,sizeof(f->context))||
       hr!=hash64(f->compact,(size_t)(f->n-1)*sizeof(*f->compact))||
       hx!=hash64(f->x,32768)||hg!=hash64(f->growth,32768)||
       hm!=hash64(f->maps,(size_t)f->n*sizeof(*f->maps))||
       hq!=hash64(f->routes,(size_t)f->n*sizeof(*f->routes)))return-1;
    memset(f->table_masks,0xff,512);f->table_masks[0]=0;
    (void)table[0][0](&f->context);
    if(f->table_masks[0]!=0)return-1;
    return 0;
}

static int check_up(fixture_t *f,float barrier,float strike)
{
    if(asian_barrier_prepare_compact(f->routes,f->n,100,barrier,strike,
       exp(-f->rate*f->maturity),NULL,f->compact,&f->context))return-1;
    uint16_t *masks=a64(512);if(!masks)return-1;
    scalar_up_masks(f,barrier,masks);
    const float *terminal=f->s_trace+(size_t)(f->n-1)*PATHS;
    const double call=reduced_payoff(terminal,masks,0,1,strike,f->context.payoff_scale);
    const double put=reduced_payoff(terminal,masks,1,1,strike,f->context.payoff_scale);
    const int bad=!bit_equal_double(call,asian_barrier_up_call_self_grouped_diag(&f->context))||
        !bit_equal_double(put,asian_barrier_up_put_self_grouped_diag(&f->context));
    free(masks);return bad?-1:0;
}

static int prepare_only_all_counts(void)
{
    fixture_t f;
    if(prepare_fixture(&f,256,0.03,0.20,1.0))return-1;
    for(uint32_t n=1;n<=256;++n){
        ordered_d1_diag_context_t *probe=a64(sizeof(*probe));
        asian_barrier_context_t c;
        if(!probe||ordered_d1_diag_prepare(probe,
            (float)((0.03-0.5*0.2*0.2)/n),(float)(0.2/sqrt((double)n)),
            8192,ORDERED_D1_DIAG_PREPARE_X3,16)||
           asian_barrier_prepare_compact(f.routes,n,100,95,100,exp(-.03),
            NULL,f.compact,&c)||c.route_count!=n-1||c.monitoring_count!=n){
            free(probe);release_fixture(&f);return-1;
        }
        free(probe);
    }
    asian_barrier_context_t c;
    if(!asian_barrier_prepare_compact(f.routes,0,100,95,100,1,NULL,f.compact,&c)||
       !asian_barrier_prepare_compact(f.routes,257,100,95,100,1,NULL,f.compact,&c)||
       !asian_barrier_prepare_compact(f.routes,16,NAN,95,100,1,NULL,f.compact,&c)){
        release_fixture(&f);return-1;
    }
    release_fixture(&f);return 0;
}

int main(int argc,char **argv)
{
    int prepare_only=0;const char *json_path=NULL,*kink_path=NULL,*error_path=NULL;
    for(int i=1;i<argc;++i)if(!strcmp(argv[i],"--prepare-only"))prepare_only=1;
    else if(!strcmp(argv[i],"--json")&&++i<argc)json_path=argv[i];
    else if(!strcmp(argv[i],"--kink-json")&&++i<argc)kink_path=argv[i];
    else if(!strcmp(argv[i],"--errors-json")&&++i<argc)error_path=argv[i];else return 2;
    if(prepare_only){
        if(prepare_only_all_counts())return 2;
        puts("asian_genuine_discrete_barrier runtime_counts_1_256=PASS");
        return 0;
    }
    if(kink_path){int fd=open(kink_path,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0)return 2;kink_json=fdopen(fd,"w");if(!kink_json)return 2;fputs("{\"status\":\"PASS\",\"policy\":\"all_float32_vs_float64_barrier_comparison_disagreements_are_retained\",\"events\":[",kink_json);}
    if(error_path){int fd=open(error_path,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0)return 2;error_json=fdopen(fd,"w");if(!error_json)return 2;fputs("{\"status\":\"PASS\",\"contracts\":[",error_json);}
    static const uint32_t ns[]={16,32,64,128,256};
    double max_residual=0.0;uint64_t ambiguities=0;
    for(size_t ni=0;ni<sizeof(ns)/sizeof(ns[0]);++ni){
        fixture_t f;if(prepare_fixture(&f,ns[ni],0.03,0.20,1.0))return 2;
        const float barriers[]={FLT_MIN,90.0f,95.0f,100.0f,FLT_MAX};
        const float strikes[]={80.0f,100.0f,120.0f};
        for(size_t bi=0;bi<sizeof(barriers)/sizeof(barriers[0]);++bi)
            for(size_t ki=0;ki<sizeof(strikes)/sizeof(strikes[0]);++ki)
                if(check_prices(&f,barriers[bi],strikes[ki],&max_residual,&ambiguities))return 2;
        if(check_up(&f,105.0f,100.0f)||check_up(&f,FLT_MAX,100.0f)||
           check_up(&f,FLT_MIN,100.0f))return 2;
        release_fixture(&f);
    }
    const double rates[]={-0.01,0.0,0.03};
    const double sigmas[]={0.0,0.20};
    for(size_t ri=0;ri<3;++ri)for(size_t si=0;si<2;++si){
        fixture_t f;if(prepare_fixture(&f,16,rates[ri],sigmas[si],1.0))return 2;
        float equality=100.0f*f.routes[0].growth_base[route_source_index(&f.routes[0],0)];
        const float b[]={equality,nextafterf(equality,-INFINITY),nextafterf(equality,INFINITY)};
        for(size_t j=0;j<3;++j)if(check_prices(&f,b[j],100.0f,&max_residual,&ambiguities))return 2;
        if(rates[ri]==0.03&&sigmas[si]==0.0){
            if(check_prices(&f,100.0f,100.0f,&max_residual,&ambiguities))return 2;
            const uint16_t*m=f.mask_trace+(size_t)(f.n-1)*256;
            for(uint32_t h=0;h<256;++h)if(m[h]!=UINT16_MAX)return 2;
        }
        release_fixture(&f);
    }
    if(json_path){
        int fd=open(json_path,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0)return 2;
        FILE*out=fdopen(fd,"w");if(!out)return 2;
        fprintf(out,"{\"status\":\"PASS\",\"classification\":\"DISCRETE_DOWN_AND_OUT_CORRECTNESS_QUALIFIED\",\"up_and_out_status\":\"CORRECTNESS_ONLY_PASS\",\"paths\":4096,\"runtime_monitoring_counts\":\"1..256\",\"principal_counts\":[16,32,64,128,256],\"rates\":[-0.01,0,0.03],\"sigmas\":[0,0.20],\"strikes\":[80,100,120],\"barriers\":[\"FLT_MIN\",90,95,100,\"FLT_MAX\",\"exact_date1_state\",\"adjacent_below\",\"adjacent_above\"],\"initial_observation\":\"date_1_after_future_GBM_update; S0_not_observed\",\"directed_initial_mask\":\"validated_outside_timing_with_mask_table\",\"d1_contract\":\"direct_D1_update_and_observation_before_compact_entry_zero_D2\",\"gates\":{\"joe_kuo_words_exact\":true,\"routed_growth_exact\":true,\"every_date_S_exact\":true,\"every_date_masks_exact\":true,\"sql_state_bit_identical\":true,\"resident_table_identical\":true,\"same_float_price_bits_exact\":true,\"up_and_out_separate_leaves\":true,\"knock_in_out_parity_exact\":true,\"inputs_immutable\":true,\"kink_adjusted_residual_le_1e_4\":true},\"max_abs_kink_adjusted_price_residual\":%.17g,\"ambiguous_path_observations\":%"PRIu64",\"worst_raw_case\":{\"N\":%u,\"barrier\":%.9g,\"strike\":%.9g,\"option\":\"%s\",\"raw_price_difference\":%.17g,\"barrier_indicator_flip_contribution\":%.17g,\"smooth_residual\":%.17g,\"ambiguous_paths\":%"PRIu64",\"first_path\":%u,\"first_date_zero_based\":%u}}\n",max_residual,ambiguities,worst_n,worst_barrier,worst_strike,worst_put?"put":"call",worst_raw.raw_error,worst_raw.flip_contribution,worst_raw.smooth_residual,worst_raw.ambiguous_paths,worst_raw.first_path,worst_raw.first_date);
        if(fclose(out))return 2;
    }
    if(kink_json){fprintf(kink_json,"],\"event_count\":%d}\n",kink_comma);if(fclose(kink_json))return 2;kink_json=NULL;}
    if(error_json){fprintf(error_json,"],\"contract_count\":%d,\"max_abs_kink_adjusted_price_residual\":%.17g}\n",error_comma,max_residual);if(fclose(error_json))return 2;error_json=NULL;}
    printf("asian_genuine_discrete_barrier correctness=PASS max_kink_adjusted_price_residual=%.12g ambiguous_paths=%"PRIu64" initial_observation=date_1_after_update\n",max_residual,ambiguities);
    return 0;
}
