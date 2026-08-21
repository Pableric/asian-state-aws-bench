#define _POSIX_C_SOURCE 200112L
#include "private/asian_genuine_price_delta_strip_diag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void*a64(size_t n){void*p=0;if(posix_memalign(&p,64,n))return 0;memset(p,0,n);return p;}

int main(int argc,char**argv)
{
    if(argc!=2)return 2;
    uint32_t count=strstr(argv[1],"_8")?8:strstr(argv[1],"_4")?4:1;
    float strikes[32];
    asian_genuine_strip_context_t*ctx=a64(sizeof*ctx);
    asian_genuine_strip_output_t*out=a64(sizeof*out);
    float*q=a64(16384),*g=a64(16384),*l=a64(16384);
    if(!ctx||!out||!q||!g||!l||asian_genuine_strip_fixed_strikes(count,strikes)||
       asian_genuine_strip_prepare(ctx,100,.03,0,.20,1,32,0,0,0,strikes,count))return 2;
    for(uint32_t i=0;i<4096;++i){q[i]=2800+(i%257)*1.75f;g[i]=85+(i%193)*.15625f;l[i]=-.2f+(i%97)*.004f;}
#define RUN(key,fn) if(!strcmp(argv[1],key))fn(q,g,ctx,ctx->strikes,out->values)
    if(!strcmp(argv[1],"l_to_g"))asian_genuine_strip_l_to_g_diag(l,ctx,g);
    else RUN("ar_price_1",asian_genuine_strip_arithmetic_price_1_diag);
    else RUN("ar_price_4",asian_genuine_strip_arithmetic_price_4_diag);
    else RUN("ar_price_8",asian_genuine_strip_arithmetic_price_8_diag);
    else RUN("cv_price_1",asian_genuine_strip_cv_price_1_diag);
    else RUN("cv_price_4",asian_genuine_strip_cv_price_4_diag);
    else RUN("cv_price_8",asian_genuine_strip_cv_price_8_diag);
    else RUN("ar_delta_1",asian_genuine_strip_arithmetic_price_delta_1_diag);
    else RUN("ar_delta_4",asian_genuine_strip_arithmetic_price_delta_4_diag);
    else RUN("ar_delta_8",asian_genuine_strip_arithmetic_price_delta_8_diag);
    else RUN("cv_delta_1",asian_genuine_strip_cv_price_delta_1_diag);
    else RUN("cv_delta_4",asian_genuine_strip_cv_price_delta_4_diag);
    else RUN("cv_delta_8",asian_genuine_strip_cv_price_delta_8_diag);
    else return 2;
#undef RUN
    printf("symbol=%s checksum=%.17g\n",argv[1],out->values[0].call_price+
        out->values[0].put_price+out->values[0].call_delta+
        out->values[0].put_delta+g[31]);
    return 0;
}
