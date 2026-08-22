#include <mpfr.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t hard[64] = {
  170,2560,5631,8021,1535,3925,4266,6656,1877,3583,4608,6314,512,2218,5973,7679,
  853,2559,5632,7338,1536,3242,4949,6655,1194,3584,4607,6997,511,2901,5290,7680,
  341,3071,5120,7850,1024,3754,4437,7167,1706,3072,5119,6485,1023,2389,5802,7168,
  682,2048,6143,7509,2047,3413,4778,6144,1365,4095,4096,6826,0,2730,5461,8191,
};

static uint32_t sobol(uint32_t index,const uint32_t directions[32])
{
    uint32_t gray=index^(index>>1),word=0;
    for(unsigned bit=0;gray;++bit,gray>>=1)if(gray&1u)word^=directions[bit];
    return word;
}

static double inverse_normal_seed(double p)
{
    static const double a[]={-39.69683028665376,220.9460984245205,-275.9285104469687,
      138.3577518672690,-30.66479806614716,2.506628277459239};
    static const double c[]={-.007784894002430293,-.3223964580411365,-2.400758277161838,
      -2.549732539343734,4.374664141464968,2.938163982698783};
    static const double d[]={.007784695709041462,.3224671290700398,
      2.445134137142996,3.754408661907416};
    static const double den[]={-54.47609879822406,161.5858368580409,
      -155.6989798598866,66.80131188771972,-13.28068155288572};
    if(p<.02425){double q=sqrt(-2*log(p));return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5])/((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);}
    if(p>.97575){double q=sqrt(-2*log(1-p));return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5])/((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);}
    double q=p-.5,r=q*q;return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q/(((((den[0]*r+den[1])*r+den[2])*r+den[3])*r+den[4])*r+1);
}

static void inverse_normal(mpfr_t z,const mpfr_t u)
{
    mpfr_t root2,cdf,pdf,step,tmp,pi;
    mpfr_inits2(256,root2,cdf,pdf,step,tmp,pi,(mpfr_ptr)0);
    mpfr_const_pi(pi,MPFR_RNDN);mpfr_sqrt_ui(root2,2,MPFR_RNDN);
    mpfr_set_d(z,inverse_normal_seed(mpfr_get_d(u,MPFR_RNDN)),MPFR_RNDN);
    for(unsigned iteration=0;iteration<12;++iteration){
        mpfr_neg(tmp,z,MPFR_RNDN);mpfr_div(tmp,tmp,root2,MPFR_RNDN);
        mpfr_erfc(cdf,tmp,MPFR_RNDN);mpfr_div_2ui(cdf,cdf,1,MPFR_RNDN);
        mpfr_sub(step,cdf,u,MPFR_RNDN);
        mpfr_mul(tmp,z,z,MPFR_RNDN);mpfr_div_2ui(tmp,tmp,1,MPFR_RNDN);
        mpfr_neg(tmp,tmp,MPFR_RNDN);mpfr_exp(pdf,tmp,MPFR_RNDN);
        mpfr_mul_ui(tmp,pi,2,MPFR_RNDN);mpfr_sqrt(tmp,tmp,MPFR_RNDN);
        mpfr_div(pdf,pdf,tmp,MPFR_RNDN);mpfr_div(step,step,pdf,MPFR_RNDN);
        mpfr_sub(z,z,step,MPFR_RNDN);
    }
    mpfr_clears(root2,cdf,pdf,step,tmp,pi,(mpfr_ptr)0);
}

static int is_hard(uint32_t index)
{
    for(unsigned i=0;i<64;++i)if(hard[i]==index)return (int)i;
    return -1;
}

int main(int argc,char **argv)
{
    if(argc!=4){fprintf(stderr,"usage: %s joe-kuo.bin table.bin provenance.csv\n",argv[0]);return 2;}
    FILE *directions_file=fopen(argv[1],"rb");if(!directions_file){perror(argv[1]);return 2;}
    uint32_t length,directions[32];
    if(fread(&length,4,1,directions_file)!=1||length!=32||
       fread(directions,4,32,directions_file)!=32){fclose(directions_file);return 2;}
    fclose(directions_file);
    FILE *out=fopen(argv[2],"wb"),*manifest=fopen(argv[3],"w");
    if(!out||!manifest){perror("output");return 2;}
    fputs("payload_index,donor_index,source_region,region_offset,packet,half,lane,sobol_word,float_bits,hard_tail_index\n",manifest);
    mpfr_t u,z;mpfr_inits2(256,u,z,(mpfr_ptr)0);
    for(uint32_t payload=0;payload<8192u;++payload){
        const uint32_t donor=8192u+payload,word=sobol(donor,directions);
        mpfr_set_ui(u,word,MPFR_RNDN);mpfr_mul_2ui(u,u,1,MPFR_RNDN);
        mpfr_add_ui(u,u,1,MPFR_RNDN);mpfr_div_2ui(u,u,33,MPFR_RNDN);
        inverse_normal(z,u);float value=mpfr_get_flt(z,MPFR_RNDN);
        uint32_t bits;memcpy(&bits,&value,4);
        if(fwrite(&value,4,1,out)!=1)return 2;
        fprintf(manifest,"%u,%u,%u,%u,%u,%u,%u,%u,0x%08x,%d\n",
          payload,donor,payload/4096u,payload%4096u,payload/32u,
          payload%32u/16u,payload%16u,word,bits,is_hard(payload));
    }
    mpfr_clears(u,z,(mpfr_ptr)0);
    if(fclose(out)||fclose(manifest))return 2;
    return 0;
}
