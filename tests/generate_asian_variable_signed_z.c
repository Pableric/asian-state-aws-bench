#include <mpfr.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FIRST_DONOR = 2, LAST_DONOR = 31, PATHS = 4096, COLUMNS = 17 };

static uint32_t ordinary_word(uint32_t index)
{
    uint32_t gray = index ^ (index >> 1), out = 0u;
    for (uint32_t bit = 0; gray != 0u; ++bit, gray >>= 1)
        if ((gray & 1u) != 0u) out ^= UINT32_C(1) << (31u - bit);
    return out;
}

static double inverse_normal_seed(double p)
{
    static const double a[] = {-39.69683028665376,220.9460984245205,
      -275.9285104469687,138.3577518672690,-30.66479806614716,
      2.506628277459239};
    static const double c[] = {-.007784894002430293,-.3223964580411365,
      -2.400758277161838,-2.549732539343734,4.374664141464968,
      2.938163982698783};
    static const double d[] = {.007784695709041462,.3224671290700398,
      2.445134137142996,3.754408661907416};
    static const double den[] = {-54.47609879822406,161.5858368580409,
      -155.6989798598866,66.80131188771972,-13.28068155288572};
    if (p < .02425) {
        double q = sqrt(-2 * log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
          ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
    }
    if (p > .97575) {
        double q = sqrt(-2 * log(1-p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
          ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
    }
    double q = p-.5, r = q*q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
      (((((den[0]*r+den[1])*r+den[2])*r+den[3])*r+den[4])*r+1);
}

static void inverse_normal(mpfr_t z, const mpfr_t u)
{
    mpfr_t root2,cdf,pdf,step,tmp,pi;
    mpfr_inits2(256,root2,cdf,pdf,step,tmp,pi,(mpfr_ptr)0);
    mpfr_const_pi(pi,MPFR_RNDN); mpfr_sqrt_ui(root2,2,MPFR_RNDN);
    mpfr_set_d(z,inverse_normal_seed(mpfr_get_d(u,MPFR_RNDN)),MPFR_RNDN);
    for (unsigned iteration=0; iteration<12; ++iteration) {
        mpfr_neg(tmp,z,MPFR_RNDN); mpfr_div(tmp,tmp,root2,MPFR_RNDN);
        mpfr_erfc(cdf,tmp,MPFR_RNDN); mpfr_div_2ui(cdf,cdf,1,MPFR_RNDN);
        mpfr_sub(step,cdf,u,MPFR_RNDN);
        mpfr_mul(tmp,z,z,MPFR_RNDN); mpfr_div_2ui(tmp,tmp,1,MPFR_RNDN);
        mpfr_neg(tmp,tmp,MPFR_RNDN); mpfr_exp(pdf,tmp,MPFR_RNDN);
        mpfr_mul_ui(tmp,pi,2,MPFR_RNDN); mpfr_sqrt(tmp,tmp,MPFR_RNDN);
        mpfr_div(pdf,pdf,tmp,MPFR_RNDN); mpfr_div(step,step,pdf,MPFR_RNDN);
        mpfr_sub(z,z,step,MPFR_RNDN);
    }
    mpfr_clears(root2,cdf,pdf,step,tmp,pi,(mpfr_ptr)0);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s old-prefix.bin output.bin joe-kuo.bin\n", argv[0]);
        return 2;
    }
    FILE *directions = fopen(argv[3], "rb");
    if (directions == NULL) return 2;
    uint32_t count, d1[32];
    if (fread(&count,4,1,directions)!=1 || count!=32 ||
        fread(d1,4,32,directions)!=32) return 2;
    fclose(directions);
    for (uint32_t bit=0; bit<32; ++bit)
        if (d1[bit] != (UINT32_C(1) << (31u-bit))) return 2;

    uint32_t special[COLUMNS], jump[12];
    special[0] = d1[0];
    for (uint32_t bit=1; bit<COLUMNS; ++bit)
        special[bit] = d1[bit] ^ d1[bit-1];
    uint32_t cumulative = 0u;
    for (uint32_t bit=0; bit<12; ++bit) {
        cumulative ^= special[bit]; jump[bit] = cumulative;
    }

    FILE *out = fopen(argv[2], "wb");
    if (out == NULL) return 2;
    mpfr_t u,z; mpfr_inits2(256,u,z,(mpfr_ptr)0);
    for (uint32_t donor=FIRST_DONOR; donor<=LAST_DONOR; ++donor) {
        const uint32_t start = donor * PATHS;
        uint32_t transformed = 0u;
        for (uint32_t bit=0; bit<COLUMNS; ++bit)
            if ((start & (UINT32_C(1)<<bit)) != 0u) transformed ^= special[bit];
        for (uint32_t physical=0; physical<PATHS; ++physical) {
            /* The qualified 32-lane ABI has pi(j)=j at its materialized bank. */
            const uint32_t exact_index = start + physical;
            if (transformed != ordinary_word(exact_index)) {
                fprintf(stderr,"transformed W mismatch at %u\n",exact_index);
                return 2;
            }
            mpfr_set_ui(u,transformed,MPFR_RNDN); mpfr_mul_2ui(u,u,1,MPFR_RNDN);
            mpfr_add_ui(u,u,1,MPFR_RNDN); mpfr_div_2ui(u,u,33,MPFR_RNDN);
            inverse_normal(z,u);
            const float value = mpfr_get_flt(z,MPFR_RNDN);
            if (fwrite(&value,sizeof(value),1,out)!=1) return 2;
            if (physical+1u<PATHS)
                transformed ^= jump[__builtin_ctz(physical+1u)];
        }
    }
    mpfr_clears(u,z,(mpfr_ptr)0);
    if (fclose(out)!=0) return 2;

    FILE *old = fopen(argv[1],"rb"), *fresh = fopen(argv[2],"rb");
    if (old == NULL || fresh == NULL) return 2;
    for (uint32_t byte=0; byte<8192u*4u; ++byte) {
        if (fgetc(old) != fgetc(fresh)) {
            fprintf(stderr,"frozen prefix mismatch at byte %u\n",byte);
            return 2;
        }
    }
    if (fgetc(old) != EOF) return 2;
    fclose(old); fclose(fresh);
    return 0;
}
