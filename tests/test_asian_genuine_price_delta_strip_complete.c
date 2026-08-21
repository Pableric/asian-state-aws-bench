#define _POSIX_C_SOURCE 200112L
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_genuine_permute.h"
#include "private/asian_geometric_cv_diag.h"
#include "private/asian_genuine_price_delta_strip_diag.h"

#include <math.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PATHS = 4096 };
static int price_only;
static double residual_sum,flip_sum;
static uint32_t residual_count,residual_positive,residual_negative;
static uint64_t arithmetic_ambiguities,geometric_ambiguities;

typedef struct {
    uint32_t n;
    uint32_t directions[256][32];
    uint32_t *words[2];
    float *x, *growth, *g, *g_check, *exp_input;
    fragment_map_t *maps;
    asian_genuine_route_t *routes;
    asian_genuine_state_t *current, *dual, *initial;
    ordered_d1_diag_context_t *producer;
    float step_drift, step_diffusion;
} fixture_t;

typedef struct {
    uint32_t arithmetic_ambiguous_paths;
    uint32_t geometric_ambiguous_paths;
    uint32_t first_arithmetic_path;
    uint32_t first_geometric_path;
    double arithmetic_flip_delta;
    double geometric_flip_delta;
} kink_decomposition_t;

static void *a64(size_t bytes)
{
    void *p = 0;
    if (posix_memalign(&p, 64, bytes)) return 0;
    memset(p, 0, bytes);
    return p;
}

static uint32_t sobol(uint32_t index, const uint32_t *directions)
{
    uint32_t gray = index ^ (index >> 1), word = 0;
    for (uint32_t bit = 0; gray; ++bit, gray >>= 1)
        if (gray & 1) word ^= directions[bit];
    return word;
}

static double inverse_normal(double p)
{
    static const double a[] = {-39.69683028665376, 220.9460984245205,
        -275.9285104469687, 138.3577518672690, -30.66479806614716,
        2.506628277459239};
    static const double c[] = {-0.007784894002430293, -0.3223964580411365,
        -2.400758277161838, -2.549732539343734, 4.374664141464968,
        2.938163982698783};
    static const double d[] = {0.007784695709041462, 0.3224671290700398,
        2.445134137142996, 3.754408661907416};
    static const double den[] = {-54.47609879822406, 161.5858368580409,
        -155.6989798598866, 66.80131188771972, -13.28068155288572};
    if (p < .02425) {
        const double q = sqrt(-2 * log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
    }
    if (p > .97575) {
        const double q = sqrt(-2 * log(1-p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1);
    }
    const double q = p-.5, r = q*q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
           (((((den[0]*r+den[1])*r+den[2])*r+den[3])*r+den[4])*r+1);
}

static void release_fixture(fixture_t *f)
{
    free(f->producer); free(f->initial); free(f->dual); free(f->current);
    free(f->routes); free(f->maps); free(f->exp_input); free(f->g_check);
    free(f->g); free(f->growth); free(f->x); free(f->words[1]); free(f->words[0]);
    memset(f, 0, sizeof *f);
}

static int load_directions(fixture_t *f)
{
    FILE *in = fopen("direction_numbers/joe_kuo_6_21201.bin", "rb");
    if (!in) return -1;
    for (uint32_t d = 0; d < 256; ++d) {
        uint32_t length;
        if (fread(&length, 4, 1, in) != 1 || length != 32 ||
            fread(f->directions[d], 4, 32, in) != 32) {
            fclose(in); return -1;
        }
    }
    fclose(in);
    return 0;
}

static void produce_corrected(fixture_t *f)
{
    ordered_d1_x_only_diag(256, f->producer, f->x);
    asian_vector_exp_range_reduced_array_diag(f->x, f->growth);
    asian_vector_exp_range_reduced_array_diag(f->x + PATHS, f->growth + PATHS);
}

static int prepare_fixture(fixture_t *f, uint32_t n, double rate, double sigma)
{
    memset(f, 0, sizeof *f); f->n = n;
    if (load_directions(f)) return -1;
    const double dt = 1.0 / n;
    f->step_drift = (float)((rate - .5*sigma*sigma)*dt);
    f->step_diffusion = (float)(sigma*sqrt(dt));
    f->words[0]=a64(16384); f->words[1]=a64(16384);
    f->x=a64(32768); f->growth=a64(32768); f->g=a64(16384);
    f->g_check=a64(16384); f->exp_input=a64(16384);
    f->maps=a64(n*sizeof *f->maps); f->routes=a64(n*sizeof *f->routes);
    f->current=a64(sizeof *f->current); f->dual=a64(sizeof *f->dual);
    f->initial=a64(sizeof *f->initial); f->producer=a64(sizeof *f->producer);
    if (!f->words[0] || !f->words[1] || !f->x || !f->growth || !f->g ||
        !f->g_check || !f->exp_input || !f->maps || !f->routes || !f->current ||
        !f->dual || !f->initial || !f->producer) return -1;
    for (uint32_t i=0;i<PATHS;++i) {
        f->words[0][i]=sobol(8192+i,f->directions[0]);
        f->words[1][i]=sobol(12288+i,f->directions[0]);
        f->initial->s[i]=100;
    }
    if (ordered_d1_diag_prepare(f->producer,f->step_drift,f->step_diffusion,
        8192,ORDERED_D1_DIAG_PREPARE_X3,n)) return -1;
    produce_corrected(f);
    const uint32_t *words[2]={f->words[0],f->words[1]};
    const float *x[2]={f->x,f->x+PATHS};
    const float *growth[2]={f->growth,f->growth+PATHS};
    uint32_t *target=a64(16384); if(!target)return -1;
    for(uint32_t k=0;k<n;++k){
        for(uint32_t i=0;i<PATHS;++i)target[i]=sobol(8192+i,f->directions[k]);
        if(asian_genuine_prepare_route(words,2,x,growth,target,k,n,
            &f->maps[k],&f->routes[k])){free(target);return -1;}
    }
    free(target);
    memcpy(f->current,f->initial,sizeof *f->current);
    memcpy(f->dual,f->initial,sizeof *f->dual);
    asian_genuine_sql_variable_diag(f->routes,n,f->current);
    asian_genuine_sql_dual_control_diag(f->routes,n,f->dual);
    if(memcmp(f->current,f->dual,sizeof *f->current))return -1;
    return 0;
}

static void chronological_reference(const fixture_t *f,
    const asian_genuine_strip_context_t *ctx,
    asian_genuine_strip_output_t *ar, asian_genuine_strip_output_t *cv,
    kink_decomposition_t decomposition[32])
{
    double sums_a[32]={0},sums_cv[32]={0},deltas_a[32]={0},deltas_cv[32]={0};
    const double discount=ctx->discount;
    for(uint32_t k=0;k<ctx->strike_count;++k){
        decomposition[k].first_arithmetic_path=UINT32_MAX;
        decomposition[k].first_geometric_path=UINT32_MAX;
    }
    for(uint32_t path=0;path<PATHS;++path){
        double s=100,q=0,l=0;
        for(uint32_t k=0;k<f->n;++k){
            const double u=((double)sobol(8192+path,f->directions[k])+.5)*0x1p-32;
            const double x=(double)f->step_drift+(double)f->step_diffusion*inverse_normal(u);
            s*=exp(x); q+=s; l+=(double)(f->n-k)/f->n*x;
        }
        const double a=q/f->n,g=exp(log(100.0)+l);
        const double da=q/(f->n*100.0),dg=g/100.0;
        const float float_a=f->dual->q[path]*(1.0f/(float)f->n);
        const float float_g=f->g[path];
        for(uint32_t k=0;k<ctx->strike_count;++k){
            const asian_genuine_strip_strike_t*r=&ctx->strikes[k];
            const double ap=fmax(r->direct_sign*(a-r->strike),0.0)*discount;
            const double gp=fmax(r->direct_sign*(g-r->strike),0.0)*discount;
            const double ad=r->direct_sign*(a-r->strike)>0.0?
                r->direct_sign*da*discount:0.0;
            const double gd=r->direct_sign*(g-r->strike)>0.0?
                r->direct_sign*dg*discount:0.0;
            sums_a[k]+=ap; sums_cv[k]+=ap-gp;
            deltas_a[k]+=ad; deltas_cv[k]+=ad-gd;
            const double strike=r->strike;
            if(float_a!=(float)a && strike>=fmin((double)float_a,a) &&
               strike<=fmax((double)float_a,a)){
                kink_decomposition_t*d=&decomposition[k];
                ++d->arithmetic_ambiguous_paths;
                if(d->first_arithmetic_path==UINT32_MAX)d->first_arithmetic_path=path;
                const int fi=r->direct_sign*((double)float_a-strike)>0.0;
                const int di=r->direct_sign*(a-strike)>0.0;
                d->arithmetic_flip_delta+=(double)(fi-di)*r->direct_sign*da*discount/PATHS;
            }
            if(float_g!=(float)g && strike>=fmin((double)float_g,g) &&
               strike<=fmax((double)float_g,g)){
                kink_decomposition_t*d=&decomposition[k];
                ++d->geometric_ambiguous_paths;
                if(d->first_geometric_path==UINT32_MAX)d->first_geometric_path=path;
                const int fi=r->direct_sign*((double)float_g-strike)>0.0;
                const int di=r->direct_sign*(g-strike)>0.0;
                d->geometric_flip_delta+=(double)(fi-di)*r->direct_sign*dg*discount/PATHS;
            }
        }
    }
    for(uint32_t k=0;k<ctx->strike_count;++k){
        const asian_genuine_strip_strike_t*r=&ctx->strikes[k];
        const double da=sums_a[k]/PATHS;
        const double dc=sums_cv[k]/PATHS+r->geometric_price_exact_direct;
        const double dda=deltas_a[k]/PATHS;
        const double ddc=deltas_cv[k]/PATHS+r->geometric_delta_exact_direct;
        ar->values[k].call_price=da+r->call_price_adjust;
        ar->values[k].put_price=da+r->put_price_adjust;
        cv->values[k].call_price=dc+r->call_price_adjust;
        cv->values[k].put_price=dc+r->put_price_adjust;
        ar->values[k].call_delta=dda+r->call_delta_adjust;
        ar->values[k].put_delta=dda+r->put_delta_adjust;
        cv->values[k].call_delta=ddc+r->call_delta_adjust;
        cv->values[k].put_delta=ddc+r->put_delta_adjust;
    }
}

static void completed_state_reference(const fixture_t*f,
    const asian_genuine_strip_context_t*ctx,asian_genuine_strip_output_t*ar,
    asian_genuine_strip_output_t*cv)
{
    double pa[32]={0},pc[32]={0},da[32]={0},dc[32]={0};
    for(uint32_t path=0;path<PATHS;++path){
        const float af=(ctx->initial_q+f->dual->q[path])*ctx->inv_total;
        const float gf=f->g[path];
        const double ad=(double)f->dual->q[path]/(ctx->total_fixings*100.0);
        const double gd=(double)ctx->future_fixings/ctx->total_fixings*gf/100.0;
        for(uint32_t k=0;k<ctx->strike_count;++k){
            const asian_genuine_strip_strike_t*r=&ctx->strikes[k];
            const double ap=fmax(r->direct_sign*((double)af-r->strike),0.0)*ctx->discount;
            const double gp=fmax(r->direct_sign*((double)gf-r->strike),0.0)*ctx->discount;
            const double aad=r->direct_sign*((double)af-r->strike)>0.0?
                r->direct_sign*ad*ctx->discount:0.0;
            const double ggd=r->direct_sign*((double)gf-r->strike)>0.0?
                r->direct_sign*gd*ctx->discount:0.0;
            pa[k]+=ap;pc[k]+=ap-gp;da[k]+=aad;dc[k]+=aad-ggd;
        }
    }
    for(uint32_t k=0;k<ctx->strike_count;++k){
        const asian_genuine_strip_strike_t*r=&ctx->strikes[k];
        const double ap=pa[k]/PATHS,cp=pc[k]/PATHS+r->geometric_price_exact_direct;
        const double ad=da[k]/PATHS,cd=dc[k]/PATHS+r->geometric_delta_exact_direct;
        ar->values[k]=(asian_genuine_strip_value_t){ap+r->call_price_adjust,
            ap+r->put_price_adjust,ad+r->call_delta_adjust,ad+r->put_delta_adjust};
        cv->values[k]=(asian_genuine_strip_value_t){cp+r->call_price_adjust,
            cp+r->put_price_adjust,cd+r->call_delta_adjust,cd+r->put_delta_adjust};
    }
}

static int check_n(uint32_t n,double *signed_sum,uint32_t *error_count,
                   uint32_t *positive,uint32_t *negative,double *max_error,
                   double *delta_signed_sum,uint32_t *delta_error_count,
                   uint32_t *delta_positive,uint32_t *delta_negative,
                   double *max_delta_error,double *max_delta_residual,
                   double *max_same_state_delta_error)
{
    fixture_t f;
    if(prepare_fixture(&f,n,.03,.20)){release_fixture(&f);return -1;}
    static const uint32_t counts[]={1,4,8,16,32};
    for(size_t ci=0;ci<sizeof counts/sizeof counts[0];++ci){
        float strikes[32]; asian_genuine_strip_fixed_strikes(counts[ci],strikes);
        asian_genuine_strip_context_t*ctx=a64(sizeof*ctx),*saved=a64(sizeof*saved);
        asian_genuine_strip_output_t*ar4=a64(sizeof*ar4),*ar8=a64(sizeof*ar8);
        asian_genuine_strip_output_t*cv4=a64(sizeof*cv4),*cv8=a64(sizeof*cv8);
        asian_genuine_strip_output_t*apd4=a64(sizeof*apd4),*apd8=a64(sizeof*apd8);
        asian_genuine_strip_output_t*cpd4=a64(sizeof*cpd4),*cpd8=a64(sizeof*cpd8);
        asian_genuine_strip_output_t*ra=a64(sizeof*ra),*rc=a64(sizeof*rc);
        asian_genuine_strip_output_t*sa=a64(sizeof*sa),*sc=a64(sizeof*sc);
        kink_decomposition_t decomposition[32]={{0}};
        if(!ctx||!saved||!ar4||!ar8||!cv4||!cv8||!apd4||!apd8||!cpd4||!cpd8||!ra||!rc||!sa||!sc)return -1;
        if(asian_genuine_strip_prepare(ctx,100,.03,0,.20,1,n,0,0,0,strikes,counts[ci]))return -1;
        memcpy(saved,ctx,sizeof*saved);
        if(asian_genuine_strip_exp_preflight(ctx,f.dual->l,0,0))return -1;
        asian_genuine_strip_l_to_g_diag(f.dual->l,ctx,f.g);
        for(uint32_t i=0;i<PATHS;++i)f.exp_input[i]=logf(100.0f)+f.dual->l[i];
        asian_vector_exp_range_reduced_array_diag(f.exp_input,f.g_check);
        if(memcmp(f.g,f.g_check,16384))return -1;
        chronological_reference(&f,ctx,ra,rc,decomposition);
        completed_state_reference(&f,ctx,sa,sc);
        if(asian_genuine_strip_price_diag(f.dual->q,f.g,ctx,0,4,ar4)||
           asian_genuine_strip_price_diag(f.dual->q,f.g,ctx,0,8,ar8)||
           asian_genuine_strip_price_diag(f.dual->q,f.g,ctx,1,4,cv4)||
           asian_genuine_strip_price_diag(f.dual->q,f.g,ctx,1,8,cv8))return -1;
        if(!price_only&&(asian_genuine_strip_price_delta_diag(f.dual->q,f.g,ctx,0,4,apd4)||
           asian_genuine_strip_price_delta_diag(f.dual->q,f.g,ctx,0,8,apd8)||
           asian_genuine_strip_price_delta_diag(f.dual->q,f.g,ctx,1,4,cpd4)||
           asian_genuine_strip_price_delta_diag(f.dual->q,f.g,ctx,1,8,cpd8)))return -1;
        if(memcmp(ctx,saved,sizeof*ctx))return -1;
        for(uint32_t k=0;k<counts[ci];++k){
            const asian_genuine_strip_output_t*got[4]={ar4,ar8,cv4,cv8};
            const asian_genuine_strip_output_t*ref[4]={ra,ra,rc,rc};
            for(uint32_t v=0;v<4;++v){
                const double values[2]={got[v]->values[k].call_price,got[v]->values[k].put_price};
                const double refs[2]={ref[v]->values[k].call_price,ref[v]->values[k].put_price};
                for(uint32_t side=0;side<2;++side){
                    const double e=values[side]-refs[side],ae=fabs(e);
                    *signed_sum+=e; ++*error_count;
                    if(ae>1e-8){if(e>0)++*positive;else++*negative;}
                    if(ae>*max_error)*max_error=ae;
                    if(ae>1e-4){fprintf(stderr,"full price gate N=%u count=%u K=%.9g v=%u side=%u got=%.12g ref=%.12g err=%.9g\n",n,counts[ci],strikes[k],v,side,values[side],refs[side],e);return -1;}
                }
            }
            if(ar4->values[k].call_price!=ar8->values[k].call_price||
               ar4->values[k].put_price!=ar8->values[k].put_price||
               cv4->values[k].call_price!=cv8->values[k].call_price||
               cv4->values[k].put_price!=cv8->values[k].put_price)return -1;
            if(!price_only){
            const asian_genuine_strip_output_t*pd[4]={apd4,apd8,cpd4,cpd8};
            const asian_genuine_strip_output_t*pr[4]={ra,ra,rc,rc};
            const asian_genuine_strip_output_t*price_only[4]={ar4,ar8,cv4,cv8};
            for(uint32_t v=0;v<4;++v){
                if(pd[v]->values[k].call_price!=price_only[v]->values[k].call_price||
                   pd[v]->values[k].put_price!=price_only[v]->values[k].put_price)return -1;
                const double values[2]={pd[v]->values[k].call_delta,pd[v]->values[k].put_delta};
                const double refs[2]={pr[v]->values[k].call_delta,pr[v]->values[k].put_delta};
                const asian_genuine_strip_output_t*same_ref=v<2?sa:sc;
                const double same_values[2]={same_ref->values[k].call_delta,same_ref->values[k].put_delta};
                for(uint32_t side=0;side<2;++side){
                    const double e=values[side]-refs[side],ae=fabs(e);
                    const double same_error=values[side]-same_values[side];
                    const double flip=v<2?decomposition[k].arithmetic_flip_delta:
                        decomposition[k].arithmetic_flip_delta-decomposition[k].geometric_flip_delta;
                    const double residual=e-flip;
                    residual_sum+=residual;flip_sum+=flip;++residual_count;
                    if(fabs(residual)>1e-8){if(residual>0)++residual_positive;else++residual_negative;}
                    *delta_signed_sum+=e;++*delta_error_count;
                    if(ae>1e-8){if(e>0)++*delta_positive;else++*delta_negative;}
                    if(ae>*max_delta_error)*max_delta_error=ae;
                    if(fabs(residual)>*max_delta_residual)*max_delta_residual=fabs(residual);
                    if(fabs(same_error)>*max_same_state_delta_error)*max_same_state_delta_error=fabs(same_error);
                    if(fabs(same_error)>1e-4||fabs(residual)>1e-4){
                        fprintf(stderr,"delta decomposition gate N=%u count=%u K=%.9g v=%u side=%u same_error=%.9g unadjusted=%.9g flip=%.9g residual=%.9g ar_ambiguous=%u geo_ambiguous=%u\n",n,counts[ci],strikes[k],v,side,same_error,e,flip,residual,decomposition[k].arithmetic_ambiguous_paths,decomposition[k].geometric_ambiguous_paths);return -1;
                    }
                }
            }
            arithmetic_ambiguities+=decomposition[k].arithmetic_ambiguous_paths;
            geometric_ambiguities+=decomposition[k].geometric_ambiguous_paths;
            if(n==128 && counts[ci]==16 && strikes[k]==98.0f){
                const double unadjusted=apd4->values[k].call_delta-ra->values[k].call_delta;
                const double residual=unadjusted-decomposition[k].arithmetic_flip_delta;
                if(decomposition[k].arithmetic_ambiguous_paths!=1||
                   decomposition[k].first_arithmetic_path!=471||fabs(residual)>1e-4)return -1;
                printf("KINK_AMBIGUITY_REPORTED N=128 K=98 arithmetic_paths=%u first_path=%u unadjusted_delta_difference=%.12g kink_flip_contribution=%.12g non_kink_residual=%.12g geometric_paths=%u\n",decomposition[k].arithmetic_ambiguous_paths,decomposition[k].first_arithmetic_path,unadjusted,decomposition[k].arithmetic_flip_delta,residual,decomposition[k].geometric_ambiguous_paths);
            }
            if(apd4->values[k].call_delta!=apd8->values[k].call_delta||
               apd4->values[k].put_delta!=apd8->values[k].put_delta||
               cpd4->values[k].call_delta!=cpd8->values[k].call_delta||
               cpd4->values[k].put_delta!=cpd8->values[k].put_delta)return -1;
            }
        }
        if(counts[ci]==1){
            const int put=!(ctx->strikes[0].flags&ASIAN_GENUINE_STRIP_DIRECT_CALL);
            asian_geometric_cv_context_t*old=a64(sizeof*old);if(!old)return -1;
            if(asian_geometric_cv_prepare(old,100,100,.03,0,.20,1,n,0,0,0,put))return -1;
            const double existing=asian_arithmetic_payoff_reduce_diag(f.dual->q,old);
            const double selected=put?ar4->values[0].put_price:ar4->values[0].call_price;
            if(existing!=selected){fprintf(stderr,"single price bits N=%u existing=%.17g selected=%.17g\n",n,existing,selected);return -1;}
            free(old);
        }
        free(sc);free(sa);free(rc);free(ra);free(cpd8);free(cpd4);free(apd8);free(apd4);
        free(cv8);free(cv4);free(ar8);free(ar4);free(saved);free(ctx);
    }
    release_fixture(&f);return 0;
}

int main(int argc,char**argv)
{
    if(argc==2&&!strcmp(argv[1],"--price-only"))price_only=1;
    else if(argc!=1)return 2;
    static const uint32_t ns[]={16,32,64,128,256};
    double signed_sum=0,max_error=0,delta_signed_sum=0,max_delta_error=0;
    double max_delta_residual=0,max_same_state_delta_error=0;
    uint32_t count=0,positive=0,negative=0,delta_count=0,delta_positive=0,delta_negative=0;
    for(size_t i=0;i<sizeof ns/sizeof ns[0];++i)
        if(check_n(ns[i],&signed_sum,&count,&positive,&negative,&max_error,
            &delta_signed_sum,&delta_count,&delta_positive,&delta_negative,&max_delta_error,
            &max_delta_residual,&max_same_state_delta_error))return 2;
    const uint32_t nonzero=positive+negative;
    if(nonzero>=20 && (positive*100>=95*nonzero || negative*100>=95*nonzero)){
        fprintf(stderr,"systematic price bias positive=%u negative=%u mean=%.9g\n",positive,negative,signed_sum/count);return 2;
    }
    if(price_only){
        printf("asian_genuine_price_delta_strip stage1_complete=PASS max_abs_price_error=%.9g signed_mean_price_error=%.9g price_positive=%u price_negative=%u corrected_x_vector_exp=PASS S_Q_L_bits=PASS\n",max_error,signed_sum/count,positive,negative);
        return 0;
    }
    const uint32_t delta_nonzero=delta_positive+delta_negative;
    if(delta_nonzero>=20 && (delta_positive*100>=95*delta_nonzero || delta_negative*100>=95*delta_nonzero)){
        fprintf(stderr,"systematic delta bias positive=%u negative=%u mean=%.9g\n",delta_positive,delta_negative,delta_signed_sum/delta_count);return 2;
    }
    const uint32_t residual_nonzero=residual_positive+residual_negative;
    if(residual_nonzero>=20&&(residual_positive*100>=95*residual_nonzero||
       residual_negative*100>=95*residual_nonzero)){
        fprintf(stderr,"systematic smooth delta bias positive=%u negative=%u mean=%.9g\n",residual_positive,residual_negative,residual_sum/residual_count);return 2;
    }
    printf("asian_genuine_price_delta_strip stage1_complete=PASS delta_status=KINK_AMBIGUITY_REPORTED max_abs_price_error=%.9g signed_mean_price_error=%.9g price_positive=%u price_negative=%u max_abs_unadjusted_delta_difference=%.9g max_abs_kink_adjusted_residual=%.9g max_abs_same_Q_G_delta_error=%.9g signed_mean_unadjusted_delta_difference=%.9g signed_mean_kink_flip_contribution=%.9g signed_mean_kink_adjusted_residual=%.9g arithmetic_ambiguous_paths=%"PRIu64" geometric_ambiguous_paths=%"PRIu64" delta_positive=%u delta_negative=%u smooth_positive=%u smooth_negative=%u corrected_x_vector_exp=PASS S_Q_L_bits=PASS\n",max_error,signed_sum/count,positive,negative,max_delta_error,max_delta_residual,max_same_state_delta_error,delta_signed_sum/delta_count,flip_sum/residual_count,residual_sum/residual_count,arithmetic_ambiguities,geometric_ambiguities,delta_positive,delta_negative,residual_positive,residual_negative);
    return 0;
}
