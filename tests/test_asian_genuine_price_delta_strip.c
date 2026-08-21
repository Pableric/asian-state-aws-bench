#define _POSIX_C_SOURCE 200112L
#include "private/asian_genuine_price_delta_strip_diag.h"
#include "private/asian_geometric_cv_diag.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int price_only;

static void *a64(size_t bytes)
{
    void *p = 0;
    if (posix_memalign(&p, 64, bytes)) return 0;
    memset(p, 0, bytes);
    return p;
}

static int same(const void *a, const void *b, size_t bytes)
{
    return memcmp(a, b, bytes) == 0;
}

static void reference_values(const float *q, const float *g,
    const asian_genuine_strip_context_t *ctx, int cv,
    asian_genuine_strip_output_t *out)
{
    for (uint32_t k = 0; k < ctx->strike_count; ++k) {
        const asian_genuine_strip_strike_t *s = &ctx->strikes[k];
        double sum = 0.0, sum_delta = 0.0;
        for (uint32_t i = 0; i < ASIAN_GENUINE_STRIP_PATHS; ++i) {
            const double a = (ctx->initial_q + q[i]) / ctx->total_fixings;
            const double ap = fmax(s->direct_sign * (a - s->strike), 0.0);
            const double gp = fmax(s->direct_sign * (g[i] - s->strike), 0.0);
            const double da = q[i] / (ctx->total_fixings * 100.0);
            const double dg = (double)ctx->future_fixings /
                ctx->total_fixings * g[i] / 100.0;
            const double ad = s->direct_sign * (a-s->strike) > 0.0 ?
                s->direct_sign * da : 0.0;
            const double gd = s->direct_sign * (g[i]-s->strike) > 0.0 ?
                s->direct_sign * dg : 0.0;
            sum += ctx->discount * (ap - (cv ? gp : 0.0));
            sum_delta += ctx->discount * (ad - (cv ? gd : 0.0));
        }
        const double direct = sum / ASIAN_GENUINE_STRIP_PATHS +
            (cv ? s->geometric_price_exact_direct : 0.0);
        const double direct_delta = sum_delta / ASIAN_GENUINE_STRIP_PATHS +
            (cv ? s->geometric_delta_exact_direct : 0.0);
        out->values[k].call_price = direct + s->call_price_adjust;
        out->values[k].put_price = direct + s->put_price_adjust;
        out->values[k].call_delta = direct_delta + s->call_delta_adjust;
        out->values[k].put_delta = direct_delta + s->put_delta_adjust;
    }
}

static int state_only_case(uint32_t future, uint32_t completed,
                           double q0, double past_log_sum,
                           double rate, double sigma, uint32_t strike_count)
{
    float strikes[32];
    if (asian_genuine_strip_fixed_strikes(strike_count, strikes)) return -1;
    asian_genuine_strip_context_t *ctx = a64(sizeof *ctx);
    asian_genuine_strip_context_t *saved = a64(sizeof *saved);
    asian_genuine_strip_output_t *got4 = a64(sizeof *got4);
    asian_genuine_strip_output_t *got8 = a64(sizeof *got8);
    asian_genuine_strip_output_t *ref = a64(sizeof *ref);
    float *q = a64(16384), *l = a64(16384), *g = a64(16384);
    float *qs = a64(16384), *ls = a64(16384), *gs = a64(16384);
    if (!ctx || !saved || !got4 || !got8 || !ref || !q || !l || !g ||
        !qs || !ls || !gs) return -1;
    if (asian_genuine_strip_prepare(ctx, 100, rate, 0, sigma, 1,
                                   future, completed, q0, past_log_sum,
                                   strikes, strike_count)) return -1;
    uint32_t nearest_count=0;
    for(uint32_t k=0;k<strike_count;++k){
        const uint32_t flags=ctx->strikes[k].flags;
        const uint32_t money=flags&(ASIAN_GENUINE_STRIP_CALL_ITM|
            ASIAN_GENUINE_STRIP_CALL_ATM|ASIAN_GENUINE_STRIP_CALL_OTM);
        if(money==0||(money&(money-1)))return -1;
        if(flags&ASIAN_GENUINE_STRIP_NEAREST_ATM)++nearest_count;
        if(((flags&ASIAN_GENUINE_STRIP_DIRECT_CALL)!=0)!=(strikes[k]>=ctx->expected_arithmetic))return -1;
    }
    if(nearest_count!=1)return -1;
    for (uint32_t i = 0; i < 4096; ++i) {
        const double z = ((int)(i % 97) - 48) / 48.0;
        q[i] = (float)(future * 100.0 * exp(0.02 * z));
        l[i] = (float)(0.04 * z * future / (future + completed));
    }
    memcpy(saved, ctx, sizeof *saved);
    memcpy(qs, q, 16384); memcpy(ls, l, 16384);
    if (asian_genuine_strip_exp_preflight(ctx, l, 0, 0)) return -1;
    asian_genuine_strip_l_to_g_diag(l, ctx, g);
    memcpy(gs, g, 16384);
    for (int cv = 0; cv < 2; ++cv) {
        memset(got4, 0, sizeof *got4); memset(got8, 0, sizeof *got8);
        memset(ref, 0, sizeof *ref);
        reference_values(q, g, ctx, cv, ref);
        if (asian_genuine_strip_price_diag(q, g, ctx, cv, 4, got4) ||
            asian_genuine_strip_price_diag(q, g, ctx, cv, 8, got8)) return -1;
        asian_genuine_strip_output_t price4 = *got4, price8 = *got8;
        if (!price_only && (asian_genuine_strip_price_delta_diag(q,g,ctx,cv,4,got4) ||
            asian_genuine_strip_price_delta_diag(q,g,ctx,cv,8,got8))) return -1;
        for (uint32_t k = 0; k < strike_count; ++k) {
            const double e4c = got4->values[k].call_price-ref->values[k].call_price;
            const double e4p = got4->values[k].put_price-ref->values[k].put_price;
            const double e8c = got8->values[k].call_price-ref->values[k].call_price;
            const double e8p = got8->values[k].put_price-ref->values[k].put_price;
            const double e4cd = got4->values[k].call_delta-ref->values[k].call_delta;
            const double e4pd = got4->values[k].put_delta-ref->values[k].put_delta;
            const double e8cd = got8->values[k].call_delta-ref->values[k].call_delta;
            const double e8pd = got8->values[k].put_delta-ref->values[k].put_delta;
            if (fabs(e4c) > 1e-4 || fabs(e4p) > 1e-4 ||
                fabs(e8c) > 1e-4 || fabs(e8p) > 1e-4 ||
                (!price_only&&(fabs(e4cd)>1e-4 || fabs(e4pd)>1e-4 ||
                fabs(e8cd)>1e-4 || fabs(e8pd)>1e-4 ||
                got4->values[k].call_price!=price4.values[k].call_price ||
                got4->values[k].put_price!=price4.values[k].put_price ||
                got8->values[k].call_price!=price8.values[k].call_price ||
                got8->values[k].put_price!=price8.values[k].put_price)) ||
                got4->values[k].call_price != got8->values[k].call_price ||
                got4->values[k].put_price != got8->values[k].put_price ||
                (!price_only&&(got4->values[k].call_delta != got8->values[k].call_delta ||
                got4->values[k].put_delta != got8->values[k].put_delta))) {
                fprintf(stderr, "stage1 mismatch f=%u c=%u K=%.9g cv=%d "
                    "price4=(%.9g,%.9g) price8=(%.9g,%.9g) "
                    "delta4=(%.9g,%.9g) delta8=(%.9g,%.9g)\n", future, completed,
                    strikes[k], cv, e4c, e4p, e8c, e8p,e4cd,e4pd,e8cd,e8pd);
                return -1;
            }
            const double parity = ctx->discount *
                ((double)ctx->expected_arithmetic - strikes[k]);
            if (fabs((got4->values[k].call_price-got4->values[k].put_price)-parity) > 2e-5)
                return -1;
            uint32_t arithmetic_kinks=0,geometric_kinks=0;
            for(uint32_t i=0;i<4096;++i){
                const float a=(ctx->initial_q+q[i])*ctx->inv_total;
                arithmetic_kinks+=(a==strikes[k]);
                geometric_kinks+=(g[i]==strikes[k]);
            }
            if(!price_only&&!arithmetic_kinks && (!cv || !geometric_kinks)){
                const double dparity=ctx->expected_arithmetic_delta;
                if(fabs((got4->values[k].call_delta-got4->values[k].put_delta)-dparity)>2e-5)return -1;
            }
        }
    }
    if (!same(ctx, saved, sizeof *ctx) || !same(q, qs, 16384) ||
        !same(l, ls, 16384) || !same(g, gs, 16384)) return -1;
    free(gs); free(ls); free(qs); free(g); free(l); free(q);
    free(ref); free(got8); free(got4); free(saved); free(ctx);
    return 0;
}

int main(int argc,char**argv)
{
    if(argc==2&&!strcmp(argv[1],"--price-only"))price_only=1;
    else if(argc!=1)return 2;
    static const uint32_t expected_bits[32]={
        0x428c0000,0x42900000,0x42940000,0x42980000,0x429c0000,0x42a00000,
        0x42a40000,0x42a80000,0x42ac0000,0x42b00000,0x42b40000,0x42b80000,
        0x42bc0000,0x42c00000,0x42c40000,0x42c80000,0x42c90000,0x42cc0000,
        0x42d00000,0x42d40000,0x42d80000,0x42dc0000,0x42e00000,0x42e40000,
        0x42e80000,0x42ec0000,0x42f00000,0x42f40000,0x42f80000,0x42fc0000,
        0x43000000,0x43020000};
    float full_grid[32];
    if(asian_genuine_strip_fixed_strikes(32,full_grid))return 2;
    for(uint32_t i=0;i<32;++i){uint32_t bits;memcpy(&bits,&full_grid[i],4);if(bits!=expected_bits[i])return 2;}
    static const uint32_t smoke[] = {1,3,17,33,127,255,256};
    static const uint32_t strike_counts[] = {1,4,8,16,32};
    for (size_t i = 0; i < sizeof smoke/sizeof smoke[0]; ++i)
        for (size_t k = 0; k < sizeof strike_counts/sizeof strike_counts[0]; ++k)
            if (state_only_case(smoke[i], 0, 0, 0, .03, .20, strike_counts[k])) return 2;
    if (state_only_case(17, 3, 285.5, 3*log(95.0), -.02, .25, 32) ||
        state_only_case(17, 3, 285.5, 3*log(95.0), 0, 0, 8) ||
        state_only_case(17, 3, 285.5, 3*log(95.0), .08, .10, 4)) return 2;
    float strikes[32];
    asian_genuine_strip_context_t *bad = a64(sizeof *bad);
    asian_genuine_strip_fixed_strikes(1, strikes);
    if (!asian_genuine_strip_prepare(bad,100,.03,0,.2,1,0,0,0,0,strikes,1) ||
        !asian_genuine_strip_prepare(bad,100,.03,0,.2,1,257,0,0,0,strikes,1)) return 2;
    if(price_only)puts("asian_genuine_price_delta_strip stage1_price=PASS "
        "runtime_fixings=1..256 tiles=4,8 completed_state_isolation=PASS "
        "exact_strike_bits=PASS option_moneyness=PASS immutable=PASS");
    else puts("asian_genuine_price_delta_strip stage1_price=PASS stage2_delta=PASS "
        "runtime_fixings=1..256 tiles=4,8 completed_state_isolation=PASS "
        "strict_kinks=PASS exact_strike_bits=PASS option_moneyness=PASS immutable=PASS");
    return 0;
}
