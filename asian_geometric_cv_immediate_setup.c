#include "private/asian_geometric_cv_immediate_diag.h"

#include <stdint.h>
#include <string.h>

int asian_geometric_cv_immediate_prepare(
    asian_geometric_cv_immediate_context_t *out,
    const asian_geometric_cv_packet_local_context_t *qualified,
    const asian_genuine_strip_context_t *strip)
{
    if(out==NULL||qualified==NULL||strip==NULL)
        return ASIAN_GEOMETRIC_CV_IMMEDIATE_INVALID;
    if(((uintptr_t)out&63u)||((uintptr_t)qualified&63u)||
       ((uintptr_t)strip&63u))
        return ASIAN_GEOMETRIC_CV_IMMEDIATE_ALIGNMENT;
    uint32_t strip_log_base_bits;
    memcpy(&strip_log_base_bits,&strip->log_base,4u);
    if(qualified->magic!=ASIAN_GEOMETRIC_CV_PACKET_LOCAL_MAGIC||
       qualified->abi_version!=ASIAN_GEOMETRIC_CV_PACKET_LOCAL_ABI_VERSION||
       qualified->d1_x==NULL||qualified->d1_growth==NULL||
       qualified->routes_d2==NULL||qualified->fixing_count<2u||
       qualified->fixing_count>256u||
       strip->magic!=ASIAN_GENUINE_STRIP_MAGIC||
       strip->abi_version!=ASIAN_GENUINE_STRIP_ABI_VERSION||
       strip->future_fixings!=qualified->fixing_count||
       qualified->terminal_log_base_bits!=strip_log_base_bits)
        return ASIAN_GEOMETRIC_CV_IMMEDIATE_CONTEXT;

    asian_geometric_cv_immediate_context_t prepared;
    memset(&prepared,0,sizeof(prepared));
    prepared.d1_x=qualified->d1_x;
    prepared.d1_growth=qualified->d1_growth;
    prepared.routes_d2=qualified->routes_d2;
    prepared.fixing_count=qualified->fixing_count;
    prepared.s0=qualified->s0;
    memcpy(&prepared.d1_weight_bits,&qualified->d1_weight_bits,4u);
    memcpy(&prepared.terminal_log_base_bits,
           &qualified->terminal_log_base_bits,4u);
    prepared.magic=ASIAN_GEOMETRIC_CV_IMMEDIATE_MAGIC;
    prepared.abi_version=ASIAN_GEOMETRIC_CV_IMMEDIATE_ABI_VERSION;
    memcpy(out,&prepared,sizeof(prepared));
    return ASIAN_GEOMETRIC_CV_IMMEDIATE_OK;
}

/*
 * The assembly ABI uses r12..r15 as private caller-clobbered GPRs.  These
 * fixed thunks tell the compiler the complete contract, so its normal SysV
 * prologue preserves any live callee-saved values outside the ranked leaf.
 */
#define INVOKE(mode,width) \
__attribute__((noinline,used)) \
void asian_geometric_cv_immediate_invoke_##mode##_##width( \
    const asian_geometric_cv_immediate_context_t *context, \
    const asian_genuine_strip_context_t *strip, \
    const asian_genuine_strip_strike_t *strikes, \
    asian_genuine_strip_output_t *output) \
{ \
    __asm__ volatile( \
      "call asian_geometric_cv_immediate_" #mode "_" #width "_diag" \
      : "+D"(context),"+S"(strip),"+d"(strikes),"+c"(output) \
      : \
      : "rax","r8","r9","r10","r11","r12","r13","r14","r15", \
        "k1","k2", \
        "zmm0","zmm1","zmm2","zmm3","zmm4","zmm5","zmm6","zmm7", \
        "zmm8","zmm9","zmm10","zmm11","zmm12","zmm13","zmm14","zmm15", \
        "zmm16","zmm17","zmm18","zmm19","zmm20","zmm21","zmm22","zmm23", \
        "zmm24","zmm25","zmm26","zmm27","zmm28","zmm29","zmm30","zmm31", \
        "cc","memory"); \
}

INVOKE(price,1)
INVOKE(price_delta,1)
INVOKE(price,2)
INVOKE(price_delta,2)
INVOKE(price,4)
INVOKE(price_delta,4)
#undef INVOKE

typedef void (*invoke_t)(const asian_geometric_cv_immediate_context_t *,
    const asian_genuine_strip_context_t *,const asian_genuine_strip_strike_t *,
    asian_genuine_strip_output_t *);

int asian_geometric_cv_immediate_run(
    const asian_geometric_cv_immediate_context_t *context,
    const asian_genuine_strip_context_t *strip,uint32_t requested_count,
    int price_delta,asian_genuine_strip_output_t *output)
{
    if(context==NULL||strip==NULL||output==NULL||
       ((uintptr_t)context&63u)||((uintptr_t)strip&63u)||
       ((uintptr_t)output&63u)||(price_delta!=0&&price_delta!=1)||
       requested_count==0u)
        return ASIAN_GEOMETRIC_CV_IMMEDIATE_INVALID;
    if(context->magic!=ASIAN_GEOMETRIC_CV_IMMEDIATE_MAGIC||
       context->abi_version!=ASIAN_GEOMETRIC_CV_IMMEDIATE_ABI_VERSION||
       strip->magic!=ASIAN_GENUINE_STRIP_MAGIC||
       strip->abi_version!=ASIAN_GENUINE_STRIP_ABI_VERSION||
       strip->future_fixings!=context->fixing_count)
        return ASIAN_GEOMETRIC_CV_IMMEDIATE_CONTEXT;
    if(requested_count>4u)return ASIAN_GEOMETRIC_CV_IMMEDIATE_FALLBACK;
    const uint32_t width=requested_count==1u?1u:
                         requested_count==2u?2u:4u;
    if(strip->strike_count<width)return ASIAN_GEOMETRIC_CV_IMMEDIATE_CONTEXT;
    invoke_t leaf;
    if(width==1u)leaf=price_delta?
      asian_geometric_cv_immediate_invoke_price_delta_1:
      asian_geometric_cv_immediate_invoke_price_1;
    else if(width==2u)leaf=price_delta?
      asian_geometric_cv_immediate_invoke_price_delta_2:
      asian_geometric_cv_immediate_invoke_price_2;
    else leaf=price_delta?
      asian_geometric_cv_immediate_invoke_price_delta_4:
      asian_geometric_cv_immediate_invoke_price_4;
    leaf(context,strip,strip->strikes,output);
    memset(output->values+requested_count,0,
           (32u-requested_count)*sizeof(output->values[0]));
    return ASIAN_GEOMETRIC_CV_IMMEDIATE_OK;
}
