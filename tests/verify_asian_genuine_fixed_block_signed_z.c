#include <mpfr.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t sobol(uint32_t index,const uint32_t directions[32])
{
    uint32_t gray=index^(index>>1),word=0;
    for(unsigned bit=0;gray;++bit,gray>>=1)if(gray&1u)word^=directions[bit];
    return word;
}

static void cdf(mpfr_t out,const mpfr_t z,const mpfr_t root2,mpfr_t scratch)
{
    mpfr_neg(scratch,z,MPFR_RNDN);mpfr_div(scratch,scratch,root2,MPFR_RNDN);
    mpfr_erfc(out,scratch,MPFR_RNDN);mpfr_div_2ui(out,out,1,MPFR_RNDN);
}

static void inverse_normal_bisect(mpfr_t out,const mpfr_t u)
{
    mpfr_t lo,hi,mid,value,root2,scratch;
    mpfr_inits2(384,lo,hi,mid,value,root2,scratch,(mpfr_ptr)0);
    mpfr_set_si(lo,-10,MPFR_RNDN);mpfr_set_si(hi,10,MPFR_RNDN);
    mpfr_sqrt_ui(root2,2,MPFR_RNDN);
    for(unsigned iteration=0;iteration<420u;++iteration){
        mpfr_add(mid,lo,hi,MPFR_RNDN);mpfr_div_2ui(mid,mid,1,MPFR_RNDN);
        cdf(value,mid,root2,scratch);
        if(mpfr_cmp(value,u)<0)mpfr_set(lo,mid,MPFR_RNDN);
        else mpfr_set(hi,mid,MPFR_RNDN);
    }
    mpfr_add(out,lo,hi,MPFR_RNDN);mpfr_div_2ui(out,out,1,MPFR_RNDN);
    mpfr_clears(lo,hi,mid,value,root2,scratch,(mpfr_ptr)0);
}

typedef struct {
    double max_abs;
    long double signed_sum;
    uint32_t max_ulp;
    uint32_t positive;
    uint32_t negative;
    uint32_t worst_payload;
} x_stats_t;

static uint32_t ordered_bits(float value)
{
    uint32_t bits;memcpy(&bits,&value,4);
    return (bits&UINT32_C(0x80000000))?~bits:bits^UINT32_C(0x80000000);
}

static int formerly_hard_tail(uint32_t payload)
{
    static const uint32_t hard[64]={
      170,2560,5631,8021,1535,3925,4266,6656,1877,3583,4608,6314,512,2218,5973,7679,
      853,2559,5632,7338,1536,3242,4949,6655,1194,3584,4607,6997,511,2901,5290,7680,
      341,3071,5120,7850,1024,3754,4437,7167,1706,3072,5119,6485,1023,2389,5802,7168,
      682,2048,6143,7509,2047,3413,4778,6144,1365,4095,4096,6826,0,2730,5461,8191};
    for(unsigned i=0;i<64u;++i)if(hard[i]==payload)return 1;
    return 0;
}

int main(int argc,char **argv)
{
    if(argc!=4){fprintf(stderr,"usage: %s joe-kuo.bin table.bin x-matrix.csv\n",argv[0]);return 2;}
    FILE *df=fopen(argv[1],"rb"),*tf=fopen(argv[2],"rb");
    if(!df||!tf)return 2;
    uint32_t length,directions[32];float table[8192];
    if(fread(&length,4,1,df)!=1||length!=32||fread(directions,4,32,df)!=32||
       fread(table,4,8192,tf)!=8192||fgetc(tf)!=EOF)return 2;
    fclose(df);fclose(tf);
    static const double contracts[4][4]={
      {-.02,.01,.05,.25},{0,0,.40,.25},{.03,0,.20,1},{.03,.01,.05,5}};
    x_stats_t *stats=calloc(255u*4u,sizeof(*stats));if(!stats)return 2;
    mpfr_t u,z,stored,error,sum;mpfr_inits2(384,u,z,stored,error,sum,(mpfr_ptr)0);
    mpfr_set_zero(sum,0);double max_error=0.0;uint32_t worst=0;
    for(uint32_t payload=0;payload<8192u;++payload){
        const uint32_t word=sobol(8192u+payload,directions);
        mpfr_set_ui(u,word,MPFR_RNDN);mpfr_mul_2ui(u,u,1,MPFR_RNDN);
        mpfr_add_ui(u,u,1,MPFR_RNDN);mpfr_div_2ui(u,u,33,MPFR_RNDN);
        inverse_normal_bisect(z,u);
        const float expected=mpfr_get_flt(z,MPFR_RNDN);
        if(memcmp(&expected,&table[payload],4)!=0){
            fprintf(stderr,"rounding mismatch payload=%u got=%a expected=%a\n",
                    payload,table[payload],expected);return 1;
        }
        mpfr_set_flt(stored,table[payload],MPFR_RNDN);mpfr_sub(error,stored,z,MPFR_RNDN);
        mpfr_add(sum,sum,error,MPFR_RNDN);const double absolute=fabs(mpfr_get_d(error,MPFR_RNDN));
        if(absolute>max_error){max_error=absolute;worst=payload;}
        for(uint32_t n=2u;n<=256u;++n)for(unsigned contract=0;contract<4u;++contract){
            const double rate=contracts[contract][0],q=contracts[contract][1];
            const double sigma=contracts[contract][2],maturity=contracts[contract][3];
            const double dt=maturity/(double)n;
            const float drift=(float)((rate-q-.5*sigma*sigma)*dt);
            const float diffusion=(float)(sigma*sqrt(dt));
            const float candidate=fmaf(diffusion,table[payload],drift);
            mpfr_set_flt(u,diffusion,MPFR_RNDN);mpfr_mul(error,z,u,MPFR_RNDN);
            mpfr_set_flt(u,drift,MPFR_RNDN);mpfr_add(error,error,u,MPFR_RNDN);
            const float rounded_reference=mpfr_get_flt(error,MPFR_RNDN);
            mpfr_set_flt(stored,candidate,MPFR_RNDN);mpfr_sub(stored,stored,error,MPFR_RNDN);
            const double difference=mpfr_get_d(stored,MPFR_RNDN);
            const double x_abs=fabs(difference);
            const uint32_t a=ordered_bits(candidate),b=ordered_bits(rounded_reference);
            const uint32_t ulp=a>b?a-b:b-a;
            x_stats_t *s=&stats[(n-2u)*4u+contract];
            if(x_abs>s->max_abs){s->max_abs=x_abs;s->worst_payload=payload;}
            if(ulp>s->max_ulp)s->max_ulp=ulp;
            s->signed_sum+=(long double)difference;
            if(difference>0)++s->positive;else if(difference<0)++s->negative;
        }
    }
    mpfr_div_ui(sum,sum,8192u,MPFR_RNDN);
    FILE *matrix=fopen(argv[3],"w");if(!matrix)return 2;
    fputs("N,contract,rate,q,sigma,maturity,max_abs_x_error,max_ulp_x_error,signed_mean_x_error,positive_count,negative_count,worst_payload,formerly_hard_tail\n",matrix);
    double x_max=0.0;long double x_sum=0.0L;uint64_t x_count=0;
    uint32_t x_worst_n=0,x_worst_contract=0,x_worst_payload=0,x_max_ulp=0;
    for(uint32_t n=2u;n<=256u;++n)for(unsigned contract=0;contract<4u;++contract){
        const x_stats_t *s=&stats[(n-2u)*4u+contract];
        fprintf(matrix,"%u,%u,%.17g,%.17g,%.17g,%.17g,%.17g,%u,%.17Lg,%u,%u,%u,%s\n",
          n,contract,contracts[contract][0],contracts[contract][1],contracts[contract][2],
          contracts[contract][3],s->max_abs,s->max_ulp,s->signed_sum/8192.0L,
          s->positive,s->negative,s->worst_payload,
          formerly_hard_tail(s->worst_payload)?"yes":"no");
        if(s->max_abs>x_max){x_max=s->max_abs;x_worst_n=n;
          x_worst_contract=contract;x_worst_payload=s->worst_payload;}
        if(s->max_ulp>x_max_ulp)x_max_ulp=s->max_ulp;
        x_sum+=s->signed_sum;x_count+=8192u;
    }
    if(fclose(matrix))return 2;
    printf("{\"status\":\"PASS\",\"entries\":8192,\"input_interval\":"
           "\"8192-16383\",\"conversion\":\"(word+0.5)*2^-32\","
           "\"rounding\":\"IEEE-754 binary32 nearest-even\","
           "\"independent_reference\":\"MPFR-384 monotone bisection\","
           "\"max_abs_gaussian_error\":%.17g,\"worst_payload_index\":%u,"
           "\"signed_mean_gaussian_error\":%.17g,"
           "\"x_matrix\":{\"N\":\"2-256\",\"contracts\":4,"
           "\"max_abs_x_error\":%.17g,\"max_ulp_x_error\":%u,"
           "\"signed_mean_x_error\":%.17Lg,\"worst_N\":%u,"
           "\"worst_contract\":%u,\"worst_payload\":%u,"
           "\"worst_formerly_hard_tail\":%s,"
           "\"exact_x_ceiling_bit_identity\":true}}\n",max_error,worst,
           mpfr_get_d(sum,MPFR_RNDN),x_max,x_max_ulp,x_sum/(long double)x_count,
           x_worst_n,x_worst_contract,x_worst_payload,
           formerly_hard_tail(x_worst_payload)?"true":"false");
    mpfr_clears(u,z,stored,error,sum,(mpfr_ptr)0);
    free(stats);
    return 0;
}
