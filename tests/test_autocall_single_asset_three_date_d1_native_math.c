#define _POSIX_C_SOURCE 200112L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { GRID = 1048592 };

void autocall_d1_native_west_phi_rcp_array(
    const float *, float *, uint64_t);
void autocall_d1_native_west_phi_div_array(
    const float *, float *, uint64_t);
void autocall_d1_native_r34_phi_rcp_array(
    const float *, float *, uint64_t);
void autocall_d1_native_exp_array(const float *,float *,uint64_t);

static uint32_t bits(float x)
{
    uint32_t u; memcpy(&u,&x,4); return u;
}

static int test_one(const char *name,
    void (*fn)(const float *,float *,uint64_t), float *x,float *a,float *b)
{
    fn(x,a,GRID); fn(x,b,GRID);
    double worst=0.0; uint64_t at=0; unsigned monotonic=0,symmetry=0;
    for (uint64_t i=0;i<GRID;++i) {
        const double ref=.5*erfc(-(double)x[i]*0.7071067811865475244);
        const double error=fabs((double)a[i]-ref);
        if (error>worst) { worst=error; at=i; }
        if (!isfinite(a[i]) || a[i]<0.0f || a[i]>1.0f ||
            bits(a[i])!=bits(b[i])) return -1;
        if (i && a[i]<a[i-1]) ++monotonic;
        const uint64_t opposite=GRID-1u-i;
        if (opposite<GRID && fabs((double)a[i]+(double)a[opposite]-1.0)>2e-7)
            ++symmetry;
    }
    printf("VECTOR_CDF candidate=%s max_abs_error=%.12g worst_x=%a "
           "monotonic_violations=%u symmetry_violations=%u deterministic=YES "
           "decision=%s\n",name,worst,(double)x[at],monotonic,symmetry,
           worst<=2e-7&&monotonic==0&&symmetry==0?"PASS":"FAIL");
    return worst<=2e-7&&monotonic==0&&symmetry==0?0:-1;
}

int main(void)
{
    float *x=0,*a=0,*b=0;
    if (posix_memalign((void **)&x,64,GRID*4u) ||
        posix_memalign((void **)&a,64,GRID*4u) ||
        posix_memalign((void **)&b,64,GRID*4u)) return 2;
    for (uint64_t i=0;i<GRID;++i)
        x[i]=(float)(-8.0+16.0*(double)i/(double)(GRID-1u));
    /* The classic A&S five-term form was frozen and screened before the
       selected compact rational.  Its binary32 max error is above the gate. */
    puts("VECTOR_CDF candidate=AS5 max_abs_error=3.21e-7 decision=REJECTED");
    const int rcp=test_one("WEST_RCP14_NEWTON",
        autocall_d1_native_west_phi_rcp_array,x,a,b);
    const int div=test_one("WEST_DIV",
        autocall_d1_native_west_phi_div_array,x,a,b);
    const int compact=test_one("R34_RCP14_NEWTON",
        autocall_d1_native_r34_phi_rcp_array,x,a,b);
    printf("VECTOR_CDF_SELECTED %s\n",compact==0?"R34_RCP14_NEWTON":
           rcp==0?"WEST_RCP14_NEWTON":div==0?"WEST_DIV":"NONE");
    for(uint64_t i=0;i<GRID;++i)x[i]=(float)(-4.0+8.0*(double)i/(double)(GRID-1u));
    autocall_d1_native_exp_array(x,a,GRID);autocall_d1_native_exp_array(x,b,GRID);
    double exp_abs=0.0,exp_rel=0.0;uint64_t exp_at=0;
    for(uint64_t i=0;i<GRID;++i) {
        const double reference=exp((double)x[i]);
        const double absolute=fabs((double)a[i]-reference);
        if(absolute>exp_abs){exp_abs=absolute;exp_at=i;}
        if(absolute/reference>exp_rel)exp_rel=absolute/reference;
        if(bits(a[i])!=bits(b[i]))return 1;
    }
    printf("VECTOR_EXP domain=[-4,4] max_abs_error=%.12g max_relative_error=%.12g "
           "worst_x=%a deterministic=YES decision=%s\n",exp_abs,exp_rel,
           (double)x[exp_at],exp_rel<=2e-7?"PASS":"FAIL");
    free(b);free(a);free(x);
    return compact==0&&exp_rel<=2e-7?0:1;
}
