#include "private/asian_genuine_arithmetic_growth_only_strip_adapter.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static uint32_t padded_width(uint32_t count)
{
    if(count==1u)return 1u;
    if(count<=4u)return 4u;
    if(count<=8u)return 8u;
    if(count<=16u)return 16u;
    if(count<=32u)return 32u;
    return 0u;
}

int asian_genuine_arithmetic_growth_only_strip_prepare_padded(
    asian_genuine_strip_context_t *out,
    double s0, double rate, double dividend_yield, double sigma,
    double maturity, uint32_t future_fixings, uint32_t completed_fixings,
    double initial_q, double past_log_sum,
    const float *strikes, uint32_t strike_count,
    uint32_t *padded_count)
{
    if(out==NULL||strikes==NULL||padded_count==NULL||
       ((uintptr_t)out&63u)!=0u)return-1;
    const uint32_t padded=padded_width(strike_count);if(padded==0u)return-1;
    for(uint32_t i=0;i<strike_count;++i)
        if(!isfinite(strikes[i])||!(strikes[i]>0.0f))return-1;
    float qualified[ASIAN_GENUINE_STRIP_MAX_STRIKES];
    if(asian_genuine_strip_fixed_strikes(padded,qualified)!=0||
       asian_genuine_strip_prepare(out,s0,rate,dividend_yield,sigma,maturity,
         future_fixings,completed_fixings,initial_q,past_log_sum,
         qualified,padded)!=0)return-1;
    const uint32_t total=future_fixings+completed_fixings;
    const double dt=maturity/(double)future_fixings;
    const double carry=rate-dividend_yield;
    const double discount=exp(-rate*maturity);
    double expected_q_future=0.0,expected_q_delta=0.0;
    for(uint32_t j=1;j<=future_fixings;++j){const double growth=exp(carry*dt*j);
        expected_q_future+=s0*growth;expected_q_delta+=growth;}
    const double expected_a=(initial_q+expected_q_future)/(double)total;
    const double expected_a_delta=discount*expected_q_delta/(double)total;
    uint32_t nearest=0;double nearest_distance=DBL_MAX;
    for(uint32_t i=0;i<padded;++i){
        const uint32_t source=i<strike_count?i:strike_count-1u;
        const float strike=strikes[source];
        const double price_parity=discount*(expected_a-strike);
        const double delta_parity=expected_a_delta;
        const int direct_call=strike>=expected_a;
        asian_genuine_strip_strike_t *record=&out->strikes[i];memset(record,0,sizeof(*record));
        record->strike=strike;record->direct_sign=direct_call?1.0f:-1.0f;
        record->call_price_adjust=direct_call?0.0:price_parity;
        record->put_price_adjust=direct_call?-price_parity:0.0;
        record->call_delta_adjust=direct_call?0.0:delta_parity;
        record->put_delta_adjust=direct_call?-delta_parity:0.0;
        memcpy(&record->strike_bits,&strike,4u);
        record->flags=direct_call?ASIAN_GENUINE_STRIP_DIRECT_CALL:0u;
        if(strike<expected_a)record->flags|=ASIAN_GENUINE_STRIP_CALL_ITM;
        else if(strike>expected_a)record->flags|=ASIAN_GENUINE_STRIP_CALL_OTM;
        else record->flags|=ASIAN_GENUINE_STRIP_CALL_ATM;
        if(i<strike_count){const double distance=fabs((double)strike-expected_a);
            if(distance<nearest_distance){nearest=i;nearest_distance=distance;}}
    }
    out->strikes[nearest].flags|=ASIAN_GENUINE_STRIP_NEAREST_ATM;
    out->strike_count=(uint16_t)padded;*padded_count=padded;return 0;
}

int asian_genuine_arithmetic_growth_only_strip_consume_padded(
    const float q_future[ASIAN_GENUINE_STRIP_PATHS],
    const float aligned_unused_g[ASIAN_GENUINE_STRIP_PATHS],
    const asian_genuine_strip_context_t *context,
    uint32_t requested_count, int price_delta,
    asian_genuine_strip_output_t *output)
{
    if(context==NULL||output==NULL||requested_count==0u||
       requested_count>context->strike_count||
       context->strike_count!=padded_width(requested_count)||
       (price_delta!=0&&price_delta!=1))return-1;
    const int status=price_delta?
      asian_genuine_strip_price_delta_diag(q_future,aligned_unused_g,context,
        ASIAN_GENUINE_STRIP_ARITHMETIC,4u,output):
      asian_genuine_strip_price_diag(q_future,aligned_unused_g,context,
        ASIAN_GENUINE_STRIP_ARITHMETIC,4u,output);
    if(status!=0)return status;
    memset(output->values+requested_count,0,
      (ASIAN_GENUINE_STRIP_MAX_STRIKES-requested_count)*sizeof(output->values[0]));
    return 0;
}

int asian_genuine_arithmetic_growth_only_immediate_consume(
    const asian_genuine_arithmetic_growth_only_context_t *growth_context,
    const asian_genuine_strip_context_t *strip_context,
    uint32_t requested_count, int price_delta,
    int tile4_price_delta_accepted,
    asian_genuine_strip_output_t *output)
{
    if (growth_context == NULL || strip_context == NULL || output == NULL ||
        ((uintptr_t)growth_context & 63u) != 0u ||
        ((uintptr_t)strip_context & 63u) != 0u ||
        ((uintptr_t)output & 63u) != 0u ||
        growth_context->magic !=
          ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAGIC ||
        growth_context->abi_version !=
          ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_ABI_VERSION ||
        growth_context->fixing_count <
          ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MIN_FIXINGS ||
        growth_context->fixing_count >
          ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_MAX_FIXINGS ||
        growth_context->d1_growth == NULL ||
        growth_context->routes_d2 == NULL ||
        strip_context->magic != ASIAN_GENUINE_STRIP_MAGIC ||
        strip_context->abi_version != ASIAN_GENUINE_STRIP_ABI_VERSION ||
        strip_context->future_fixings != growth_context->fixing_count ||
        requested_count == 0u || requested_count > 32u ||
        strip_context->strike_count != padded_width(requested_count) ||
        (price_delta != 0 && price_delta != 1) ||
        (tile4_price_delta_accepted != 0 &&
         tile4_price_delta_accepted != 1))
        return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_INVALID;

    asian_genuine_arithmetic_growth_only_immediate_leaf_t leaf = NULL;
    if (requested_count == 1u) {
        leaf = price_delta ?
          asian_genuine_arithmetic_growth_only_price_delta_1_diag :
          asian_genuine_arithmetic_growth_only_price_1_diag;
    } else if (requested_count == 2u) {
        leaf = price_delta ?
          asian_genuine_arithmetic_growth_only_price_delta_2_diag :
          asian_genuine_arithmetic_growth_only_price_2_diag;
    } else if (requested_count <= 4u) {
        if (price_delta && !tile4_price_delta_accepted)
            return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_FALLBACK;
        leaf = price_delta ?
          asian_genuine_arithmetic_growth_only_price_delta_4_diag :
          asian_genuine_arithmetic_growth_only_price_4_diag;
    } else {
        return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_FALLBACK;
    }

    memset(output, 0, sizeof(*output));
    leaf(growth_context,strip_context,strip_context->strikes,output->values);
    memset(output->values + requested_count, 0,
      (ASIAN_GENUINE_STRIP_MAX_STRIKES - requested_count) *
        sizeof(output->values[0]));
    return ASIAN_GENUINE_ARITHMETIC_GROWTH_ONLY_IMMEDIATE_USED;
}
