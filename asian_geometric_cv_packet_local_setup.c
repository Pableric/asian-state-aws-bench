#include "private/asian_geometric_cv_packet_local_diag.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static int span(const void *pointer,size_t bytes,uintptr_t *first,uintptr_t *last)
{
    const uintptr_t begin=(uintptr_t)pointer;
    if(pointer==NULL||bytes==0u||bytes-1u>UINTPTR_MAX-begin)return-1;
    *first=begin;*last=begin+bytes;return 0;
}

static int overlap(const void *a,size_t a_bytes,const void *b,size_t b_bytes)
{
    uintptr_t af,al,bf,bl;
    if(span(a,a_bytes,&af,&al)!=0||span(b,b_bytes,&bf,&bl)!=0)return 1;
    return af<bl&&bf<al;
}

static int map_source(const fragment_map_t *map,uint32_t path,uint32_t *source)
{
    const uint32_t packet=path>>5,half=(path>>4)&1u,lane=path&15u;
    const uint32_t pattern=map->select[packet][2u+half];
    if(map->pattern_count==0u||map->pattern_count>FRAG_MAX_PATTERNS||
       pattern>=map->pattern_count)return-1;
    const uint32_t control=map->patterns[pattern][lane];
    if(control>=FRAG_LANES)return-1;
    *source=(uint32_t)map->select[packet][half]*FRAG_LANES+control;
    return *source<FRAG_BLOCK_VALUES?0:-1;
}

static int identity_map(const fragment_map_t *map)
{
    for(uint32_t path=0;path<FRAG_BLOCK_VALUES;++path){
        uint32_t source;if(map_source(map,path,&source)!=0||source!=path)return 0;
    }
    return 1;
}

int asian_geometric_cv_packet_local_prepare(
    asian_geometric_cv_packet_local_context_t *out,
    const asian_genuine_route_t *routes,uint32_t n,float s0,
    const float *x_donors,size_t x_bytes,
    const float *growth_donors,size_t growth_bytes,
    const asian_genuine_strip_context_t *strip,
    float *q_out,size_t q_bytes,float *g_out,size_t g_bytes)
{
    if(out==NULL||routes==NULL||x_donors==NULL||growth_donors==NULL||
       strip==NULL||q_out==NULL||g_out==NULL)
        return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_INVALID;
    if(n<ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MIN_FIXINGS||
       n>ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAX_FIXINGS)
        return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_FIXINGS_UNSUPPORTED;
    if(((uintptr_t)out&63u)||((uintptr_t)routes&31u)||
       ((uintptr_t)x_donors&63u)||((uintptr_t)growth_donors&63u)||
       ((uintptr_t)strip&63u)||((uintptr_t)q_out&63u)||
       ((uintptr_t)g_out&63u))
        return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ALIGNMENT;
    if(x_bytes!=ASIAN_GEOMETRIC_CV_PACKET_LOCAL_DONOR_BYTES||
       growth_bytes!=ASIAN_GEOMETRIC_CV_PACKET_LOCAL_DONOR_BYTES||
       q_bytes!=ASIAN_GEOMETRIC_CV_PACKET_LOCAL_OUTPUT_BYTES||
       g_bytes!=ASIAN_GEOMETRIC_CV_PACKET_LOCAL_OUTPUT_BYTES||
       !isfinite(s0)||!(s0>0.0f)||
       strip->magic!=ASIAN_GENUINE_STRIP_MAGIC||
       strip->abi_version!=ASIAN_GENUINE_STRIP_ABI_VERSION||
       strip->future_fixings!=n||!isfinite(strip->log_base)||
       !isfinite(strip->exp_input_min)||!isfinite(strip->exp_input_max)||
       !(strip->exp_input_min<=strip->exp_input_max))
        return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_INVALID;

    const size_t route_bytes=(size_t)n*sizeof(*routes);
    const struct {const void *p;size_t n;} immutable[]={
        {routes,route_bytes},{x_donors,x_bytes},{growth_donors,growth_bytes},
        {strip,sizeof(*strip)}
    };
    if(overlap(out,sizeof(*out),q_out,q_bytes)||
       overlap(out,sizeof(*out),g_out,g_bytes)||
       overlap(q_out,q_bytes,g_out,g_bytes))
        return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ALIAS;
    for(size_t i=0;i<sizeof(immutable)/sizeof(immutable[0]);++i)
        if(overlap(out,sizeof(*out),immutable[i].p,immutable[i].n)||
           overlap(q_out,q_bytes,immutable[i].p,immutable[i].n)||
           overlap(g_out,g_bytes,immutable[i].p,immutable[i].n))
            return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ALIAS;

    for(uint32_t i=0;i<8192u;++i)
        if(!isfinite(x_donors[i])||!isfinite(growth_donors[i])||
           !(growth_donors[i]>0.0f))
            return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_DOMAIN;

    for(uint32_t fixing=0;fixing<n;++fixing){
        const asian_genuine_route_t *route=&routes[fixing];
        float weight;memcpy(&weight,&route->weight_bits,sizeof(weight));
        if(route->x_base!=x_donors&&route->x_base!=x_donors+4096u)
            return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ROUTE_INVALID;
        if(route->growth_base!=growth_donors&&
           route->growth_base!=growth_donors+4096u)
            return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ROUTE_INVALID;
        if((route->x_base-x_donors)!=(route->growth_base-growth_donors))
            return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ROUTE_INVALID;
        if(route->map==NULL||((uintptr_t)route->map&63u)||
           route->map->dimension!=fixing+1u||route->fixing_index!=fixing||
           !isfinite(weight))
            return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ROUTE_INVALID;
        if(overlap(q_out,q_bytes,route->map,sizeof(*route->map))||
           overlap(g_out,g_bytes,route->map,sizeof(*route->map))||
           overlap(out,sizeof(*out),route->map,sizeof(*route->map)))
            return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ALIAS;
        for(uint32_t path=0;path<4096u;++path){
            uint32_t source;if(map_source(route->map,path,&source)!=0)
                return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ROUTE_INVALID;
        }
    }
    if(routes[0].x_base!=x_donors||routes[0].growth_base!=growth_donors||
       !identity_map(routes[0].map))
        return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ROUTE_INVALID;

    /* Market and exponential admissibility remain delegated to the unchanged
       fixed-source and strip preparations.  No new drift/diffusion envelope is
       introduced here. */

    asian_geometric_cv_packet_local_context_t prepared;
    memset(&prepared,0,sizeof(prepared));
    prepared.d1_x=routes[0].x_base;
    prepared.d1_growth=routes[0].growth_base;
    prepared.routes_d2=routes+1;
    prepared.q_out=q_out;prepared.g_out=g_out;
    prepared.fixing_count=n;prepared.s0=s0;
    memcpy(&prepared.d1_weight_bits,&routes[0].weight_bits,4u);
    memcpy(&prepared.terminal_log_base_bits,&strip->log_base,4u);
    prepared.magic=ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAGIC;
    prepared.abi_version=ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ABI_VERSION;
    memcpy(out,&prepared,sizeof(prepared));
    return ASIAN_GEOMETRIC_CV_PACKET_LOCAL_OK;
}

static double normal_cdf(double x)
{
    return 0.5*erfc(-x*0.707106781186547524400844362104849039);
}

static void geometric_exact(double mean,double variance,double strike,
                            double discount,double dmean_ds0,
                            double *call,double *put,
                            double *call_delta,double *put_delta)
{
    if(variance==0.0){
        const double g=exp(mean);
        *call=discount*fmax(g-strike,0.0);
        *put=discount*fmax(strike-g,0.0);
        *call_delta=g>strike?discount*g*dmean_ds0:0.0;
        *put_delta=g<strike?-discount*g*dmean_ds0:0.0;
        return;
    }
    const double root=sqrt(variance),forward=exp(mean+0.5*variance);
    const double d2=(mean-log(strike))/root,d1=d2+root;
    *call=discount*(forward*normal_cdf(d1)-strike*normal_cdf(d2));
    *put=discount*(strike*normal_cdf(-d2)-forward*normal_cdf(-d1));
    *call_delta=discount*forward*normal_cdf(d1)*dmean_ds0;
    *put_delta=-discount*forward*normal_cdf(-d1)*dmean_ds0;
}

static uint32_t padded_width(uint32_t count)
{
    if(count==1u)return 1u;
    if(count<=4u)return 4u;
    if(count<=8u)return 8u;
    if(count<=16u)return 16u;
    if(count<=32u)return 32u;
    return 0u;
}

int asian_geometric_cv_packet_local_strip_prepare_padded(
    asian_genuine_strip_context_t *out,
    double s0,double rate,double dividend_yield,double sigma,double maturity,
    uint32_t future,uint32_t completed,double initial_q,double past_log_sum,
    const float *strikes,uint32_t count,uint32_t *padded_count)
{
    if(out==NULL||strikes==NULL||padded_count==NULL||((uintptr_t)out&63u))return-1;
    const uint32_t padded=padded_width(count);if(padded==0u)return-1;
    for(uint32_t i=0;i<count;++i)
        if(!isfinite(strikes[i])||!(strikes[i]>0.0f))return-1;
    float qualified[32];
    if(asian_genuine_strip_fixed_strikes(padded,qualified)!=0||
       asian_genuine_strip_prepare(out,s0,rate,dividend_yield,sigma,maturity,
         future,completed,initial_q,past_log_sum,qualified,padded)!=0)return-1;

    const uint32_t total=future+completed;
    const double f=future,m=total,dt=maturity/f;
    const double carry=rate-dividend_yield;
    const double mu=carry-0.5*sigma*sigma;
    const double sum_t=dt*f*(f+1.0)*0.5;
    const double sum_min=f*(f+1.0)*(2.0*f+1.0)/6.0;
    const double log_mean=(past_log_sum+f*log(s0)+mu*sum_t)/m;
    const double log_variance=sigma*sigma*dt*sum_min/(m*m);
    const double discount=exp(-rate*maturity);
    double expected_q_future=0.0,expected_q_delta=0.0;
    for(uint32_t j=1;j<=future;++j){
        const double growth=exp(carry*dt*j);
        expected_q_future+=s0*growth;expected_q_delta+=growth;
    }
    const double expected_a=(initial_q+expected_q_future)/m;
    const double expected_a_delta=discount*expected_q_delta/m;
    uint32_t nearest=0;double nearest_distance=DBL_MAX;
    for(uint32_t i=0;i<padded;++i){
        const uint32_t source=i<count?i:count-1u;
        const double strike=strikes[source];
        double cg,pg,cdg,pdg;
        geometric_exact(log_mean,log_variance,strike,discount,f/(m*s0),
                        &cg,&pg,&cdg,&pdg);
        const double price_parity=discount*(expected_a-strike);
        const int direct_call=strike>=expected_a;
        asian_genuine_strip_strike_t *record=&out->strikes[i];
        memset(record,0,sizeof(*record));
        record->strike=strikes[source];record->direct_sign=direct_call?1.0f:-1.0f;
        record->geometric_price_exact_direct=direct_call?cg:pg;
        record->geometric_delta_exact_direct=direct_call?cdg:pdg;
        record->call_price_adjust=direct_call?0.0:price_parity;
        record->put_price_adjust=direct_call?-price_parity:0.0;
        record->call_delta_adjust=direct_call?0.0:expected_a_delta;
        record->put_delta_adjust=direct_call?-expected_a_delta:0.0;
        memcpy(&record->strike_bits,&record->strike,4u);
        record->flags=direct_call?ASIAN_GENUINE_STRIP_DIRECT_CALL:0u;
        if(strike<expected_a)record->flags|=ASIAN_GENUINE_STRIP_CALL_ITM;
        else if(strike>expected_a)record->flags|=ASIAN_GENUINE_STRIP_CALL_OTM;
        else record->flags|=ASIAN_GENUINE_STRIP_CALL_ATM;
        if(i<count){const double distance=fabs(strike-expected_a);
            if(distance<nearest_distance){nearest=i;nearest_distance=distance;}}
    }
    out->strikes[nearest].flags|=ASIAN_GENUINE_STRIP_NEAREST_ATM;
    *padded_count=padded;return 0;
}

int asian_geometric_cv_packet_local_strip_consume_padded(
    const float q[4096],const float g[4096],
    const asian_genuine_strip_context_t *context,uint32_t requested,
    int price_delta,asian_genuine_strip_output_t *output)
{
    if(q==NULL||g==NULL||context==NULL||output==NULL||requested==0u||
       requested>context->strike_count||context->strike_count!=padded_width(requested)||
       (price_delta!=0&&price_delta!=1))return-1;
    const int status=price_delta?
      asian_genuine_strip_price_delta_diag(q,g,context,
        ASIAN_GENUINE_STRIP_GEOMETRIC_CV,4u,output):
      asian_genuine_strip_price_diag(q,g,context,
        ASIAN_GENUINE_STRIP_GEOMETRIC_CV,4u,output);
    if(status!=0)return status;
    memset(output->values+requested,0,
      (32u-requested)*sizeof(output->values[0]));return 0;
}

static float rounded_mul(float a,float b){volatile float v=a*b;return v;}
static float rounded_add(float a,float b){volatile float v=a+b;return v;}

void asian_geometric_cv_packet_local_packet_probe_diag(
    const asian_geometric_cv_packet_local_context_t *context,uint32_t packet,
    asian_geometric_cv_packet_local_packet_trace_t *trace)
{
    if(context==NULL||trace==NULL||packet>=128u)return;
    memset(trace,0,sizeof(*trace));
    float d1_weight;memcpy(&d1_weight,&context->d1_weight_bits,4u);
    for(uint32_t lane=0;lane<32u;++lane){
        const uint32_t path=packet*32u+lane;
        float s=rounded_mul(context->s0,context->d1_growth[path]);
        float q=rounded_add(0.0f,s);
        float l=fmaf(d1_weight,context->d1_x[path],0.0f);
        trace->s[0][lane]=s;trace->q[0][lane]=q;trace->l[0][lane]=l;
        for(uint32_t fixing=1;fixing<context->fixing_count;++fixing){
            const asian_genuine_route_t *route=&context->routes_d2[fixing-1u];
            uint32_t source=0u;(void)map_source(route->map,path,&source);
            float weight;memcpy(&weight,&route->weight_bits,4u);
            s=rounded_mul(s,route->growth_base[source]);
            q=rounded_add(q,s);l=fmaf(weight,route->x_base[source],l);
            trace->s[fixing][lane]=s;
            trace->q[fixing][lane]=q;
            trace->l[fixing][lane]=l;
        }
    }
}
