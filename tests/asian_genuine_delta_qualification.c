#define _POSIX_C_SOURCE 200112L
#include "private/asian_genuine_price_delta_strip_diag.h"
#include "private/asian_geometric_cv_diag.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PATHS = 4096,
    DIMS = 256,
    REPLICATIONS = 32,
    N_COUNT = 5,
    STRIKE_COUNT_COUNT = 5,
    PREFIX_COUNT = 4,
    ESTIMATOR_COUNT = 2,
    SIDE_COUNT = 2,
    BUMP_COUNT = 3
};

static const uint32_t fixing_grid[N_COUNT] = {16, 32, 64, 128, 256};
static const uint32_t strike_count_grid[STRIKE_COUNT_COUNT] = {1, 4, 8, 16, 32};
static const uint32_t prefix_grid[PREFIX_COUNT] = {512, 1024, 2048, 4096};
static const int bump_powers[BUMP_COUNT] = {12, 14, 16};
static const uint64_t shift_master = UINT64_C(0xd1e17a5eedc0ffee);
static const uint64_t shift_stride = UINT64_C(0x9e3779b97f4a7c15);

typedef struct {
    long double sum_diff, sumsq_diff;
    long double sum_fd01, sumsq_fd01;
    long double sum_fd12, sumsq_fd12;
    long double sum_smooth, sumsq_smooth;
    long double sum_parity, sumsq_parity;
    long double sum_estimate, sumsq_estimate;
    long double sum_estimate8, sumsq_estimate8;
    uint32_t observations;
} contract_stat_t;

typedef struct {
    long double pathwise[ESTIMATOR_COUNT][SIDE_COUNT];
    long double bump[BUMP_COUNT][ESTIMATOR_COUNT][SIDE_COUNT];
    long double float_state[ESTIMATOR_COUNT][SIDE_COUNT];
    long double direct[ESTIMATOR_COUNT][SIDE_COUNT];
    long double flip[ESTIMATOR_COUNT];
    uint32_t arithmetic_ambiguous;
    uint32_t geometric_ambiguous;
    uint32_t first_arithmetic;
    uint32_t first_geometric;
} strike_eval_t;

typedef struct {
    long double sum, sumsq;
    uint64_t count;
} pooled_stat_t;

typedef struct {
    long double magnitude;
    long double unadjusted;
    long double flip;
    long double residual;
    uint32_t n, strike_count, strike_index, prefix, replication;
    uint32_t estimator, side, arithmetic_ambiguous, geometric_ambiguous;
    uint32_t first_arithmetic, first_geometric;
} worst_case_t;

typedef struct {
    uint64_t initial_state;
    uint64_t vector_hash;
} shift_record_t;

static contract_stat_t stats[N_COUNT][STRIKE_COUNT_COUNT][32]
                            [ESTIMATOR_COUNT][SIDE_COUNT][PREFIX_COUNT];
static pooled_stat_t pooled_diff[PREFIX_COUNT];
static pooled_stat_t pooled_smooth;
static pooled_stat_t pooled_same_state;
static uint64_t smooth_positive, smooth_negative;
static uint64_t sobol_mismatches, sobol_duplicates, expansion_violations;
static uint64_t price_bit_mismatches, tile_delta_mismatches;
static uint64_t arithmetic_ambiguities, geometric_ambiguities;
static uint64_t tested_sobol_words, tested_prefixes;
static long double max_inverse_cdf_residual;
static worst_case_t worst_smooth, worst_same_state, worst_kink;
static shift_record_t shift_records[REPLICATIONS];
static FILE *corpus_file;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t fixture_count;
    uint32_t paths;
} corpus_header_t;

typedef struct {
    uint32_t n;
    uint32_t replication;
    uint32_t strike_count_count;
    uint32_t reserved;
} corpus_fixture_header_t;

static void *aligned_zero(size_t bytes)
{
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes) != 0) return NULL;
    memset(p, 0, bytes);
    return p;
}

static uint64_t splitmix64(uint64_t *state)
{
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static uint64_t fnv1a_word(uint64_t hash, uint32_t word)
{
    for (unsigned i = 0; i < 4; ++i) {
        hash ^= (word >> (8 * i)) & 0xffu;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint32_t sobol_direct(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray = index ^ (index >> 1), word = 0;
    for (uint32_t bit = 0; gray; ++bit, gray >>= 1)
        if (gray & 1u) word ^= directions[bit];
    return word;
}

static int compare_u32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static long double normal_cdf_ld(long double x)
{
    return 0.5L * erfcl(-x * 0.707106781186547524400844362104849039L);
}

static long double inverse_normal_ld(long double p)
{
    static const long double a[] = {
        -39.69683028665376L, 220.9460984245205L, -275.9285104469687L,
        138.3577518672690L, -30.66479806614716L, 2.506628277459239L
    };
    static const long double c[] = {
        -0.007784894002430293L, -0.3223964580411365L,
        -2.400758277161838L, -2.549732539343734L,
        4.374664141464968L, 2.938163982698783L
    };
    static const long double d[] = {
        0.007784695709041462L, 0.3224671290700398L,
        2.445134137142996L, 3.754408661907416L
    };
    static const long double den[] = {
        -54.47609879822406L, 161.5858368580409L,
        -155.6989798598866L, 66.80131188771972L,
        -13.28068155288572L
    };
    long double x;
    if (p < 0.02425L) {
        const long double q = sqrtl(-2.0L * logl(p));
        x = (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
            ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0L);
    } else if (p > 0.97575L) {
        const long double q = sqrtl(-2.0L * logl(1.0L-p));
        x = -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
             ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0L);
    } else {
        const long double q = p-0.5L, r = q*q;
        x = (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
            (((((den[0]*r+den[1])*r+den[2])*r+den[3])*r+den[4])*r+1.0L);
    }
    const long double inv_sqrt_2pi =
        0.398942280401432677939946059934381868L;
    for (unsigned iteration = 0; iteration < 2; ++iteration) {
        const long double error = normal_cdf_ld(x) - p;
        const long double density = inv_sqrt_2pi * expl(-0.5L*x*x);
        x -= error / density;
    }
    return x;
}

static float float_from_bits(uint32_t bits)
{
    float value;
    memcpy(&value,&bits,sizeof value);
    return value;
}

static float qualified_exp_scalar(float input)
{
    static const uint32_t coefficient_bits[9]={
        0x3f800000,0x3f7ffff9,0x3efffffc,0x3e2aabbf,0x3d2aab67,
        0x3c085d88,0x3ab5de3b,0x3959cfde,0x37d8c471
    };
    const float log2e=float_from_bits(0x3fb8aa3b);
    const float ln2hi=float_from_bits(0x3f318000);
    const float ln2lo=float_from_bits(0xb95e8083);
    const float exponent=nearbyintf(input*log2e);
    float reduced=fmaf(-ln2hi,exponent,input);
    reduced=fmaf(-ln2lo,exponent,reduced);
    float polynomial=float_from_bits(coefficient_bits[8]);
    for(int degree=7;degree>=0;--degree)
        polynomial=fmaf(reduced,polynomial,float_from_bits(coefficient_bits[degree]));
    return scalbnf(polynomial,(int)exponent);
}

static void geometric_exact_ld(long double s0, uint32_t n, long double strike,
                               int side, long double *price, long double *delta)
{
    const long double rate = 0.03L, sigma = 0.20L, maturity = 1.0L;
    const long double nn = n, dt = maturity / nn;
    const long double mu = rate - 0.5L*sigma*sigma;
    const long double sum_t = dt*nn*(nn+1.0L)*0.5L;
    const long double sum_min = nn*(nn+1.0L)*(2.0L*nn+1.0L)/6.0L;
    const long double mean = logl(s0) + mu*sum_t/nn;
    const long double variance = sigma*sigma*dt*sum_min/(nn*nn);
    const long double root = sqrtl(variance);
    const long double forward = expl(mean+0.5L*variance);
    const long double d2 = (mean-logl(strike))/root;
    const long double d1 = d2+root;
    const long double discount = expl(-rate*maturity);
    if (side == 0) {
        *price = discount*(forward*normal_cdf_ld(d1)-strike*normal_cdf_ld(d2));
        *delta = discount*forward*normal_cdf_ld(d1)/s0;
    } else {
        *price = discount*(strike*normal_cdf_ld(-d2)-forward*normal_cdf_ld(-d1));
        *delta = -discount*forward*normal_cdf_ld(-d1)/s0;
    }
}

static long double expected_arithmetic_factor(uint32_t n)
{
    long double sum = 0.0L;
    for (uint32_t k = 1; k <= n; ++k)
        sum += expl(0.03L*(long double)k/(long double)n);
    return sum/(long double)n;
}

static long double sample_sd(long double sum, long double sumsq, uint32_t n)
{
    if (n < 2) return 0.0L;
    long double variance = (sumsq-sum*sum/(long double)n)/(long double)(n-1);
    if (variance < 0.0L && variance > -64.0L*LDBL_EPSILON) variance = 0.0L;
    return variance > 0.0L ? sqrtl(variance) : 0.0L;
}

static void pool_add(pooled_stat_t *stat, long double value)
{
    stat->sum += value;
    stat->sumsq += value*value;
    ++stat->count;
}

static int load_directions(uint32_t directions[DIMS][32])
{
    FILE *input = fopen("direction_numbers/joe_kuo_6_21201.bin", "rb");
    if (!input) return -1;
    for (uint32_t d = 0; d < DIMS; ++d) {
        uint32_t length;
        if (fread(&length, 4, 1, input) != 1 || length != 32 ||
            fread(directions[d], 4, 32, input) != 32) {
            fclose(input);
            return -1;
        }
    }
    fclose(input);
    return 0;
}

static int build_shifted_normals(const uint32_t directions[DIMS][32],
                                 uint32_t replication, long double *normals)
{
    uint32_t shifts[DIMS];
    uint64_t state = shift_master + shift_stride*(uint64_t)(replication+1);
    shift_records[replication].initial_state = state;
    uint64_t hash = UINT64_C(1469598103934665603);
    for (uint32_t d = 0; d < DIMS; ++d) {
        shifts[d] = (uint32_t)splitmix64(&state);
        hash = fnv1a_word(hash, shifts[d]);
    }
    shift_records[replication].vector_hash = hash;

    uint32_t *d1 = malloc(PATHS*sizeof *d1);
    uint32_t *sorted = malloc(PATHS*sizeof *sorted);
    if (!d1 || !sorted) { free(sorted); free(d1); return -1; }
    for (uint32_t d = 0; d < DIMS; ++d) {
        uint32_t recurrence = sobol_direct(8192, directions[d]);
        for (uint32_t path = 0; path < PATHS; ++path) {
            const uint32_t index = 8192+path;
            if (path != 0) recurrence ^= directions[d][__builtin_ctz(index)];
            const uint32_t direct = sobol_direct(index, directions[d]);
            if (direct != recurrence) ++sobol_mismatches;
            const uint32_t shifted = direct ^ shifts[d];
            if (d == 0) d1[path] = shifted;
            const long double u = ((long double)shifted+0.5L)*0x1p-32L;
            const long double normal=inverse_normal_ld(u);
            normals[(size_t)d*PATHS+path] = normal;
            const long double cdf_residual=fabsl(normal_cdf_ld(normal)-u);
            if(cdf_residual>max_inverse_cdf_residual)max_inverse_cdf_residual=cdf_residual;
            ++tested_sobol_words;
        }
    }
    memcpy(sorted, d1, PATHS*sizeof *sorted);
    qsort(sorted, PATHS, sizeof *sorted, compare_u32);
    for (uint32_t i = 1; i < PATHS; ++i)
        if (sorted[i] == sorted[i-1]) ++sobol_duplicates;
    for (uint32_t p = 0; p < PREFIX_COUNT; ++p) {
        for (uint32_t i = 0; i < prefix_grid[p]; ++i)
            if (d1[i] != (sobol_direct(8192+i, directions[0])^shifts[0]))
                ++expansion_violations;
        tested_prefixes += N_COUNT;
    }
    free(sorted); free(d1);
    return 0;
}

static int prepare_contexts(uint32_t n,
    asian_genuine_strip_context_t *contexts[STRIKE_COUNT_COUNT],
    float full_strikes[32], uint32_t maps[STRIKE_COUNT_COUNT][32])
{
    if (asian_genuine_strip_fixed_strikes(32, full_strikes)) return -1;
    for (uint32_t ci = 0; ci < STRIKE_COUNT_COUNT; ++ci) {
        float strikes[32];
        if (asian_genuine_strip_fixed_strikes(strike_count_grid[ci], strikes) ||
            asian_genuine_strip_prepare(contexts[ci], 100.0, 0.03, 0.0, 0.20,
                1.0, n, 0, 0.0, 0.0, strikes, strike_count_grid[ci])) return -1;
        for (uint32_t k = 0; k < strike_count_grid[ci]; ++k) {
            uint32_t bits;
            memcpy(&bits, &strikes[k], sizeof bits);
            maps[ci][k] = UINT32_MAX;
            for (uint32_t j = 0; j < 32; ++j) {
                uint32_t full_bits;
                memcpy(&full_bits, &full_strikes[j], sizeof full_bits);
                if (bits == full_bits) { maps[ci][k] = j; break; }
            }
            if (maps[ci][k] == UINT32_MAX) return -1;
        }
    }
    return 0;
}

static int build_states(uint32_t n, const long double *normals,
                        const asian_genuine_strip_context_t *context,
                        float *q_float, float *l_float, float *g_float,
                        long double *a_long, long double *g_long,
                        float *x_float, float *growth_float)
{
    float *s_float = aligned_zero(PATHS*sizeof *s_float);
    long double *s_long = calloc(PATHS, sizeof *s_long);
    long double *q_long = calloc(PATHS, sizeof *q_long);
    long double *l_long = calloc(PATHS, sizeof *l_long);
    if (!s_float || !s_long || !q_long || !l_long) {
        free(l_long); free(q_long); free(s_long); free(s_float); return -1;
    }
    for (uint32_t p = 0; p < PATHS; ++p) {
        s_float[p] = 100.0f;
        s_long[p] = 100.0L;
    }
    const float drift_f = (float)((0.03-0.5*0.20*0.20)/(double)n);
    const float diffusion_f = (float)(0.20/sqrt((double)n));
    const long double drift = (0.03L-0.5L*0.20L*0.20L)/(long double)n;
    const long double diffusion = 0.20L/sqrtl((long double)n);
    for (uint32_t d = 0; d < n; ++d) {
        const float weight_f = (float)(n-d)/(float)n;
        const long double weight = (long double)(n-d)/(long double)n;
        for (uint32_t p = 0; p < PATHS; ++p) {
            const long double z = normals[(size_t)d*PATHS+p];
            x_float[p] = fmaf(diffusion_f, (float)z, drift_f);
        }
        for(uint32_t p=0;p<PATHS;++p)
            growth_float[p]=qualified_exp_scalar(x_float[p]);
        for (uint32_t p = 0; p < PATHS; ++p) {
            s_float[p] = s_float[p]*growth_float[p];
            q_float[p] = q_float[p]+s_float[p];
            l_float[p] = fmaf(weight_f, x_float[p], l_float[p]);
            const long double x = drift+diffusion*normals[(size_t)d*PATHS+p];
            s_long[p] *= expl(x);
            q_long[p] += s_long[p];
            l_long[p] += weight*x;
        }
    }
    for(uint32_t p=0;p<PATHS;++p)
        g_float[p]=qualified_exp_scalar(context->log_base+l_float[p]);
    for (uint32_t p = 0; p < PATHS; ++p) {
        a_long[p] = q_long[p]/(long double)n;
        g_long[p] = 100.0L*expl(l_long[p]);
    }
    free(l_long); free(q_long); free(s_long); free(s_float);
    return 0;
}

static long double payoff(long double value, long double strike, int sign)
{
    const long double intrinsic = sign*(value-strike);
    return intrinsic > 0.0L ? intrinsic : 0.0L;
}

static void evaluate_prefix(uint32_t n, uint32_t prefix,
    const float *q_float, const float *g_float,
    const long double *a_long, const long double *g_long,
    const asian_genuine_strip_context_t *context, strike_eval_t evaluations[32])
{
    const long double discount = expl(-0.03L);
    const long double expected_factor = expected_arithmetic_factor(n);
    memset(evaluations, 0, 32*sizeof *evaluations);
    for (uint32_t k = 0; k < 32; ++k) {
        strike_eval_t *e = &evaluations[k];
        e->first_arithmetic = UINT32_MAX;
        e->first_geometric = UINT32_MAX;
        const asian_genuine_strip_strike_t *record = &context->strikes[k];
        const long double strike = record->strike;
        const int sign = record->direct_sign > 0.0f ? 1 : -1;
        long double selected_ar = 0.0L, selected_cv = 0.0L;
        long double float_ar = 0.0L, float_cv = 0.0L;
        long double direct_ar[SIDE_COUNT] = {0.0L, 0.0L};
        long double direct_cv[SIDE_COUNT] = {0.0L, 0.0L};
        long double bump_sample[BUMP_COUNT][ESTIMATOR_COUNT][SIDE_COUNT] = {{{0}}};
        long double arithmetic_flip = 0.0L, geometric_flip = 0.0L;
        for (uint32_t p = 0; p < prefix; ++p) {
            const long double a = a_long[p], g = g_long[p];
            const long double da = a/100.0L, dg = g/100.0L;
            const float af = q_float[p]*context->inv_total;
            const float gf = g_float[p];
            const float daf = q_float[p]*context->delta_q_scale;
            const float dgf = gf*context->delta_g_scale;
            const int ia = sign*(a-strike) > 0.0L;
            const int ig = sign*(g-strike) > 0.0L;
            const int ifa = sign*((long double)af-strike) > 0.0L;
            const int ifg = sign*((long double)gf-strike) > 0.0L;
            const long double ad = ia ? sign*da*discount : 0.0L;
            const long double gd = ig ? sign*dg*discount : 0.0L;
            const long double fad = ifa ? (long double)(sign*daf) : 0.0L;
            const long double fgd = ifg ? (long double)(sign*dgf) : 0.0L;
            selected_ar += ad;
            selected_cv += ad-gd;
            float_ar += fad;
            float_cv += fad-fgd;
            direct_ar[0] += a > strike ? da*discount : 0.0L;
            direct_ar[1] += a < strike ? -da*discount : 0.0L;
            direct_cv[0] += (a > strike ? da*discount : 0.0L)-
                            (g > strike ? dg*discount : 0.0L);
            direct_cv[1] += (a < strike ? -da*discount : 0.0L)-
                            (g < strike ? -dg*discount : 0.0L);
            if (ifa != ia) arithmetic_flip += (ifa-ia)*sign*da*discount;
            if (ifg != ig) geometric_flip += (ifg-ig)*sign*dg*discount;
            if ((long double)af != a && strike >= fminl((long double)af,a) &&
                strike <= fmaxl((long double)af,a)) {
                ++e->arithmetic_ambiguous;
                if (e->first_arithmetic == UINT32_MAX) e->first_arithmetic = p;
            }
            if ((long double)gf != g && strike >= fminl((long double)gf,g) &&
                strike <= fmaxl((long double)gf,g)) {
                ++e->geometric_ambiguous;
                if (e->first_geometric == UINT32_MAX) e->first_geometric = p;
            }
            for (uint32_t b = 0; b < BUMP_COUNT; ++b) {
                const long double h = ldexpl(100.0L, -bump_powers[b]);
                const long double plus_scale = (100.0L+h)/100.0L;
                const long double minus_scale = (100.0L-h)/100.0L;
                for (uint32_t bump_side = 0; bump_side < SIDE_COUNT; ++bump_side) {
                    const int bump_sign = bump_side ? -1 : 1;
                    const long double ap = payoff(a*plus_scale,strike,bump_sign);
                    const long double am = payoff(a*minus_scale,strike,bump_sign);
                    const long double gp = payoff(g*plus_scale,strike,bump_sign);
                    const long double gm = payoff(g*minus_scale,strike,bump_sign);
                    bump_sample[b][0][bump_side] += discount*(ap-am)/(2.0L*h);
                    bump_sample[b][1][bump_side] +=
                        discount*((ap-gp)-(am-gm))/(2.0L*h);
                }
            }
        }
        const long double inv_prefix = 1.0L/(long double)prefix;
        selected_ar *= inv_prefix; selected_cv *= inv_prefix;
        float_ar *= inv_prefix; float_cv *= inv_prefix;
        arithmetic_flip *= inv_prefix; geometric_flip *= inv_prefix;
        for (uint32_t side = 0; side < SIDE_COUNT; ++side) {
            direct_ar[side] *= inv_prefix;
            direct_cv[side] *= inv_prefix;
        }
        long double exact_price[SIDE_COUNT], exact_delta[SIDE_COUNT];
        for (uint32_t side = 0; side < SIDE_COUNT; ++side)
            geometric_exact_ld(100.0L,n,strike,(int)side,
                               &exact_price[side],&exact_delta[side]);
        const uint32_t selected_side = sign > 0 ? 0u : 1u;
        const long double parity_delta = discount*expected_factor;
        for (uint32_t side = 0; side < SIDE_COUNT; ++side) {
            long double adjustment = 0.0L;
            long double float_adjustment = 0.0L;
            if (sign < 0 && side == 0) {
                adjustment = parity_delta;
                float_adjustment = record->call_delta_adjust;
            } else if (sign > 0 && side == 1) {
                adjustment = -parity_delta;
                float_adjustment = record->put_delta_adjust;
            }
            e->pathwise[0][side] = selected_ar+adjustment;
            e->pathwise[1][side] = selected_cv+exact_delta[selected_side]+adjustment;
            e->float_state[0][side] = float_ar+float_adjustment;
            e->float_state[1][side] = float_cv+
                (long double)record->geometric_delta_exact_direct+float_adjustment;
            e->direct[0][side] = direct_ar[side];
            e->direct[1][side] = direct_cv[side]+exact_delta[side];
            for (uint32_t b = 0; b < BUMP_COUNT; ++b) {
                const long double h = ldexpl(100.0L,-bump_powers[b]);
                long double exact_plus_price, exact_minus_price, ignored;
                geometric_exact_ld(100.0L+h,n,strike,(int)side,
                                   &exact_plus_price,&ignored);
                geometric_exact_ld(100.0L-h,n,strike,(int)side,
                                   &exact_minus_price,&ignored);
                const long double exact_bump =
                    (exact_plus_price-exact_minus_price)/(2.0L*h);
                e->bump[b][0][side] = bump_sample[b][0][side]*inv_prefix;
                e->bump[b][1][side] = bump_sample[b][1][side]*inv_prefix+exact_bump;
            }
        }
        e->flip[0] = arithmetic_flip;
        e->flip[1] = arithmetic_flip-geometric_flip;
    }
}

static void update_worst(worst_case_t *worst, long double magnitude,
    uint32_t n, uint32_t count, uint32_t strike_index, uint32_t prefix,
    uint32_t replication, uint32_t estimator, uint32_t side,
    long double unadjusted, long double flip, long double residual,
    const strike_eval_t *evaluation)
{
    if (magnitude <= worst->magnitude) return;
    worst->magnitude = magnitude;
    worst->n = n; worst->strike_count = count; worst->strike_index = strike_index;
    worst->prefix = prefix; worst->replication = replication;
    worst->estimator = estimator; worst->side = side;
    worst->unadjusted = unadjusted; worst->flip = flip; worst->residual = residual;
    worst->arithmetic_ambiguous = evaluation->arithmetic_ambiguous;
    worst->geometric_ambiguous = evaluation->geometric_ambiguous;
    worst->first_arithmetic = evaluation->first_arithmetic;
    worst->first_geometric = evaluation->first_geometric;
}

static float reduce_leaf_accumulators(const float lo[16],const float hi[16])
{
    float lanes[16];
    for(uint32_t i=0;i<16;++i)lanes[i]=lo[i]+hi[i];
    float q0[4],q2[4];
    for(uint32_t i=0;i<4;++i){q0[i]=lanes[i]+lanes[4+i];q2[i]=lanes[8+i]+lanes[12+i];}
    for(uint32_t i=0;i<4;++i)q0[i]=q0[i]+q2[i];
    q0[0]=q0[0]+q0[2];q0[1]=q0[1]+q0[3];
    return q0[0]+q0[1];
}

static void emulate_production_output(const float *q,const float *g,
    const asian_genuine_strip_context_t *context,uint32_t estimator,
    asian_genuine_strip_output_t *output)
{
    for(uint32_t k=0;k<context->strike_count;++k){
        const asian_genuine_strip_strike_t*r=&context->strikes[k];
        float price_lo[16]={0},price_hi[16]={0},delta_lo[16]={0},delta_hi[16]={0};
        for(uint32_t packet=0;packet<PATHS;packet+=32){
            for(uint32_t half=0;half<2;++half)for(uint32_t lane=0;lane<16;++lane){
                const uint32_t path=packet+half*16+lane;
                const float a=(q[path]+context->initial_q)*context->inv_total;
                float price=fmaxf(r->direct_sign*(a-r->strike),0.0f);
                if(estimator){
                    const float gp=fmaxf(r->direct_sign*(g[path]-r->strike),0.0f);
                    price=price-gp;
                }
                price=price*context->discount;
                float da=q[path]*context->delta_q_scale;
                float delta=r->direct_sign*(a-r->strike)>0.0f?r->direct_sign*da:0.0f;
                if(estimator){
                    const float dg=g[path]*context->delta_g_scale;
                    const float gd=r->direct_sign*(g[path]-r->strike)>0.0f?
                        r->direct_sign*dg:0.0f;
                    delta=delta-gd;
                }
                if(half==0){price_lo[lane]=price_lo[lane]+price;
                            delta_lo[lane]=delta_lo[lane]+delta;}
                else{price_hi[lane]=price_hi[lane]+price;
                     delta_hi[lane]=delta_hi[lane]+delta;}
            }
        }
        double price=(double)reduce_leaf_accumulators(price_lo,price_hi)/4096.0;
        double delta=(double)reduce_leaf_accumulators(delta_lo,delta_hi)/4096.0;
        if(estimator){price+=r->geometric_price_exact_direct;
                      delta+=r->geometric_delta_exact_direct;}
        output->values[k].call_price=price+r->call_price_adjust;
        output->values[k].put_price=price+r->put_price_adjust;
        output->values[k].call_delta=delta+r->call_delta_adjust;
        output->values[k].put_delta=delta+r->put_delta_adjust;
    }
}

static int compare_production(uint32_t n,
    asian_genuine_strip_context_t *contexts[STRIKE_COUNT_COUNT],
    const float *q_float, const float *g_float,
    asian_genuine_strip_output_t *kept[STRIKE_COUNT_COUNT][ESTIMATOR_COUNT])
{
    for (uint32_t ci = 0; ci < STRIKE_COUNT_COUNT; ++ci) {
        for (uint32_t estimator = 0; estimator < ESTIMATOR_COUNT; ++estimator) {
            emulate_production_output(q_float,g_float,contexts[ci],estimator,
                                      kept[ci][estimator]);
        }
    }
    (void)n;
    return 0;
}

static void add_contract_observation(contract_stat_t *stat,
    long double pathwise, const long double bump[BUMP_COUNT],
    long double smooth, long double parity, uint32_t replication)
{
    const long double diff = pathwise-bump[2];
    const long double fd01 = bump[2]-bump[1];
    const long double fd12 = bump[1]-bump[0];
    stat->sum_diff += diff; stat->sumsq_diff += diff*diff;
    stat->sum_fd01 += fd01; stat->sumsq_fd01 += fd01*fd01;
    stat->sum_fd12 += fd12; stat->sumsq_fd12 += fd12*fd12;
    stat->sum_smooth += smooth; stat->sumsq_smooth += smooth*smooth;
    stat->sum_parity += parity; stat->sumsq_parity += parity*parity;
    stat->sum_estimate += pathwise; stat->sumsq_estimate += pathwise*pathwise;
    if (replication < 8) {
        stat->sum_estimate8 += pathwise;
        stat->sumsq_estimate8 += pathwise*pathwise;
    }
    ++stat->observations;
}

static int process_replication(uint32_t replication, const long double *normals)
{
    for (uint32_t ni = 0; ni < N_COUNT; ++ni) {
        const uint32_t n = fixing_grid[ni];
        asian_genuine_strip_context_t *contexts[STRIKE_COUNT_COUNT];
        asian_genuine_strip_output_t *production[STRIKE_COUNT_COUNT][ESTIMATOR_COUNT];
        asian_genuine_strip_output_t same_reference[STRIKE_COUNT_COUNT][ESTIMATOR_COUNT];
        asian_genuine_strip_output_t independent_reference[STRIKE_COUNT_COUNT][ESTIMATOR_COUNT];
        double flip_reference[STRIKE_COUNT_COUNT][ESTIMATOR_COUNT][32][SIDE_COUNT]={0};
        uint32_t maps[STRIKE_COUNT_COUNT][32];
        float full_strikes[32];
        memset(same_reference,0,sizeof same_reference);
        memset(independent_reference,0,sizeof independent_reference);
        for (uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci) {
            contexts[ci]=aligned_zero(sizeof *contexts[ci]);
            for(uint32_t e=0;e<ESTIMATOR_COUNT;++e)
                production[ci][e]=aligned_zero(sizeof *production[ci][e]);
            if(!contexts[ci]||!production[ci][0]||!production[ci][1])return -1;
        }
        if (prepare_contexts(n,contexts,full_strikes,maps)) return -1;
        float *q=aligned_zero(PATHS*sizeof *q),*l=aligned_zero(PATHS*sizeof *l);
        float *g=aligned_zero(PATHS*sizeof *g),*x=aligned_zero(PATHS*sizeof *x);
        float *growth=aligned_zero(PATHS*sizeof *growth);
        long double *a=calloc(PATHS,sizeof *a),*gl=calloc(PATHS,sizeof *gl);
        if(!q||!l||!g||!x||!growth||!a||!gl)return -1;
        if(build_states(n,normals,contexts[4],q,l,g,a,gl,x,growth) ||
           compare_production(n,contexts,q,g,production)) return -1;
        for(uint32_t pi=0;pi<PREFIX_COUNT;++pi){
            strike_eval_t evaluations[32];
            evaluate_prefix(n,prefix_grid[pi],q,g,a,gl,contexts[4],evaluations);
            if(pi==PREFIX_COUNT-1){
                for(uint32_t k=0;k<32;++k){
                    arithmetic_ambiguities+=evaluations[k].arithmetic_ambiguous;
                    geometric_ambiguities+=evaluations[k].geometric_ambiguous;
                }
            }
            for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci){
                for(uint32_t k=0;k<strike_count_grid[ci];++k){
                    const uint32_t j=maps[ci][k];
                    const strike_eval_t *ev=&evaluations[j];
                    for(uint32_t estimator=0;estimator<ESTIMATOR_COUNT;++estimator){
                        for(uint32_t side=0;side<SIDE_COUNT;++side){
                            long double float_value=ev->float_state[estimator][side];
                            if(pi==PREFIX_COUNT-1){
                                const asian_genuine_strip_value_t *value=&production[ci][estimator]->values[k];
                                const double got=side==0?value->call_delta:value->put_delta;
                                const long double same_error=(long double)got-ev->float_state[estimator][side];
                                pool_add(&pooled_same_state,same_error);
                                update_worst(&worst_same_state,fabsl(same_error),n,
                                    strike_count_grid[ci],j,prefix_grid[pi],replication,
                                    estimator,side,same_error,0.0L,same_error,ev);
                                float_value=got;
                                asian_genuine_strip_value_t *same=&same_reference[ci][estimator].values[k];
                                asian_genuine_strip_value_t *independent=&independent_reference[ci][estimator].values[k];
                                if(side==0){same->call_delta=(double)ev->float_state[estimator][side];
                                    independent->call_delta=(double)ev->pathwise[estimator][side];}
                                else{same->put_delta=(double)ev->float_state[estimator][side];
                                    independent->put_delta=(double)ev->pathwise[estimator][side];}
                                flip_reference[ci][estimator][k][side]=(double)ev->flip[estimator];
                            }
                            const long double unadjusted=float_value-ev->pathwise[estimator][side];
                            const long double smooth=unadjusted-ev->flip[estimator];
                            const long double parity=ev->pathwise[estimator][side]-
                                                     ev->direct[estimator][side];
                            long double bumps[BUMP_COUNT];
                            for(uint32_t b=0;b<BUMP_COUNT;++b)
                                bumps[b]=ev->bump[b][estimator][side];
                            contract_stat_t *stat=&stats[ni][ci][k][estimator][side][pi];
                            add_contract_observation(stat,ev->pathwise[estimator][side],
                                                     bumps,smooth,parity,replication);
                            pool_add(&pooled_diff[pi],ev->pathwise[estimator][side]-bumps[2]);
                            pool_add(&pooled_smooth,smooth);
                            if(fabsl(smooth)>1e-8L){if(smooth>0)++smooth_positive;else++smooth_negative;}
                            update_worst(&worst_smooth,fabsl(smooth),n,
                                strike_count_grid[ci],j,prefix_grid[pi],replication,
                                estimator,side,unadjusted,ev->flip[estimator],smooth,ev);
                            if(pi==PREFIX_COUNT-1)
                                update_worst(&worst_kink,fabsl(unadjusted),n,
                                    strike_count_grid[ci],j,prefix_grid[pi],replication,
                                    estimator,side,unadjusted,ev->flip[estimator],smooth,ev);
                        }
                    }
                }
            }
        }
        if(corpus_file){
            const corpus_fixture_header_t header={n,replication,STRIKE_COUNT_COUNT,0};
            if(fwrite(&header,sizeof header,1,corpus_file)!=1||
               fwrite(q,sizeof *q,PATHS,corpus_file)!=PATHS||
               fwrite(g,sizeof *g,PATHS,corpus_file)!=PATHS)return -1;
            for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci)
                if(fwrite(contexts[ci],sizeof *contexts[ci],1,corpus_file)!=1)return -1;
            for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci)
                for(uint32_t estimator=0;estimator<ESTIMATOR_COUNT;++estimator)
                    if(fwrite(production[ci][estimator],sizeof *production[ci][estimator],1,corpus_file)!=1||
                       fwrite(&same_reference[ci][estimator],sizeof same_reference[ci][estimator],1,corpus_file)!=1||
                       fwrite(&independent_reference[ci][estimator],sizeof independent_reference[ci][estimator],1,corpus_file)!=1||
                       fwrite(flip_reference[ci][estimator],sizeof flip_reference[ci][estimator],1,corpus_file)!=1)
                        return -1;
        }
        free(gl);free(a);free(growth);free(x);free(g);free(l);free(q);
        for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci){
            free(production[ci][1]);free(production[ci][0]);free(contexts[ci]);
        }
    }
    return 0;
}

static int compare_long_double(const void *a,const void *b)
{
    long double x=*(const long double*)a,y=*(const long double*)b;
    return (x>y)-(x<y);
}

static long double median(long double *values,uint32_t count)
{
    qsort(values,count,sizeof *values,compare_long_double);
    return count&1?values[count/2]:0.5L*(values[count/2-1]+values[count/2]);
}

static void print_worst(FILE *out,const char *name,const worst_case_t *w,
                        const float strikes[32])
{
    fprintf(out,"\"%s\":{\"N\":%u,\"strike_count\":%u,\"strike\":%.9g,"
        "\"prefix\":%u,\"replication\":%u,\"estimator\":\"%s\","
        "\"side\":\"%s\",\"magnitude\":%.17g,\"unadjusted\":%.17g,"
        "\"indicator_flip\":%.17g,\"smooth_residual\":%.17g,"
        "\"arithmetic_ambiguous\":%u,\"geometric_ambiguous\":%u,"
        "\"first_arithmetic_path\":%u,\"first_geometric_path\":%u}",
        name,w->n,w->strike_count,strikes[w->strike_index],w->prefix,w->replication,
        w->estimator?"geometric_cv":"arithmetic",w->side?"put":"call",
        (double)w->magnitude,(double)w->unadjusted,(double)w->flip,
        (double)w->residual,w->arithmetic_ambiguous,w->geometric_ambiguous,
        w->first_arithmetic,w->first_geometric);
}

int main(int argc,char **argv)
{
    const char *output_path="results/asian_genuine_delta_qualification/replication_raw.json";
    const char *corpus_path="results/asian_genuine_delta_qualification/replication_states.bin";
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--json")&&i+1<argc)output_path=argv[++i];
        else if(!strcmp(argv[i],"--corpus")&&i+1<argc)corpus_path=argv[++i];
        else return 2;
    }
    corpus_file=fopen(corpus_path,"wb");if(!corpus_file)return 2;
    const corpus_header_t corpus_header={UINT32_C(0x51445341),1,
        REPLICATIONS*N_COUNT,PATHS};
    if(fwrite(&corpus_header,sizeof corpus_header,1,corpus_file)!=1)return 2;
    uint32_t (*directions)[32]=aligned_zero(DIMS*32*sizeof(uint32_t));
    long double *normals=aligned_zero((size_t)DIMS*PATHS*sizeof *normals);
    if(!directions||!normals||load_directions(directions))return 2;
    for(uint32_t replication=0;replication<REPLICATIONS;++replication){
        if(build_shifted_normals(directions,replication,normals) ||
           process_replication(replication,normals))return 2;
        fprintf(stderr,"qualification_replication=%u/%u\n",replication+1,REPLICATIONS);
    }
    if(fclose(corpus_file))return 2;
    corpus_file=NULL;
    free(normals);free(directions);

    uint64_t contract_count=0,bump_covered=0,parity_covered=0;
    long double worst_bump_ratio=0.0L,worst_parity_ratio=0.0L;
    uint32_t bump_fail_n=0,bump_fail_count=0,bump_fail_k=0,bump_fail_prefix=0;
    uint32_t bump_fail_est=0,bump_fail_side=0;
    uint32_t parity_fail_n=0,parity_fail_count=0,parity_fail_k=0,parity_fail_prefix=0;
    uint32_t parity_fail_est=0,parity_fail_side=0;
    float count_strikes[STRIKE_COUNT_COUNT][32]={{0}},bump_fail_strike=0.0f;
    float parity_fail_strike=0.0f;
    for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci)
        if(asian_genuine_strip_fixed_strikes(strike_count_grid[ci],count_strikes[ci]))return 2;
    long double bump_fail_bias=0.0L,bump_fail_limit=0.0L,bump_fail_fd=0.0L;
    long double parity_fail_bias=0.0L,parity_fail_limit=0.0L;
    long double estimator_se_prefix[PREFIX_COUNT][6400];
    uint32_t estimator_se_count[PREFIX_COUNT]={0};
    long double estimator_se8[6400],estimator_se32[6400];uint32_t se_rep_count=0;
    for(uint32_t ni=0;ni<N_COUNT;++ni)for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci)
    for(uint32_t k=0;k<strike_count_grid[ci];++k)
    for(uint32_t estimator=0;estimator<ESTIMATOR_COUNT;++estimator)
    for(uint32_t side=0;side<SIDE_COUNT;++side)for(uint32_t pi=0;pi<PREFIX_COUNT;++pi){
        contract_stat_t *s=&stats[ni][ci][k][estimator][side][pi];
        const long double count=s->observations;
        const long double bias=s->sum_diff/count;
        const long double se=sample_sd(s->sum_diff,s->sumsq_diff,s->observations)/sqrtl(count);
        const long double fd01=fabsl(s->sum_fd01/count)+
            5.0L*sample_sd(s->sum_fd01,s->sumsq_fd01,s->observations)/sqrtl(count);
        const long double fd12=fabsl(s->sum_fd12/count)+
            5.0L*sample_sd(s->sum_fd12,s->sumsq_fd12,s->observations)/sqrtl(count);
        const long double fd=fmaxl(fd01,fd12),limit=5.0L*se+fd+1e-6L;
        const long double ratio=limit>0?fabsl(bias)/limit:0.0L;
        ++contract_count;if(fabsl(bias)<=limit)++bump_covered;
        if(ratio>worst_bump_ratio){worst_bump_ratio=ratio;bump_fail_n=fixing_grid[ni];
            bump_fail_count=strike_count_grid[ci];bump_fail_k=k;bump_fail_prefix=prefix_grid[pi];
            bump_fail_strike=count_strikes[ci][k];
            bump_fail_est=estimator;bump_fail_side=side;bump_fail_bias=bias;
            bump_fail_limit=limit;bump_fail_fd=fd;}
        const long double parity_bias=s->sum_parity/count;
        const long double parity_se=sample_sd(s->sum_parity,s->sumsq_parity,s->observations)/sqrtl(count);
        const long double parity_limit=5.0L*parity_se+1e-6L;
        const long double parity_ratio=parity_limit>0?fabsl(parity_bias)/parity_limit:0.0L;
        if(fabsl(parity_bias)<=parity_limit)++parity_covered;
        if(parity_ratio>worst_parity_ratio){worst_parity_ratio=parity_ratio;
            parity_fail_bias=parity_bias;parity_fail_limit=parity_limit;
            parity_fail_n=fixing_grid[ni];parity_fail_count=strike_count_grid[ci];
            parity_fail_k=k;parity_fail_prefix=prefix_grid[pi];
            parity_fail_strike=count_strikes[ci][k];parity_fail_est=estimator;
            parity_fail_side=side;}
        estimator_se_prefix[pi][estimator_se_count[pi]++]=
            sample_sd(s->sum_estimate,s->sumsq_estimate,s->observations)/sqrtl(count);
        if(pi==PREFIX_COUNT-1){
            estimator_se8[se_rep_count]=sample_sd(s->sum_estimate8,s->sumsq_estimate8,8)/sqrtl(8.0L);
            estimator_se32[se_rep_count]=sample_sd(s->sum_estimate,s->sumsq_estimate,s->observations)/sqrtl(count);
            ++se_rep_count;
        }
    }
    long double median_se[PREFIX_COUNT];
    for(uint32_t pi=0;pi<PREFIX_COUNT;++pi)
        median_se[pi]=median(estimator_se_prefix[pi],estimator_se_count[pi]);
    const long double median_se8=median(estimator_se8,se_rep_count);
    const long double median_se32=median(estimator_se32,se_rep_count);
    const long double rmse512=sqrtl(pooled_diff[0].sumsq/pooled_diff[0].count);
    const long double rmse4096=sqrtl(pooled_diff[3].sumsq/pooled_diff[3].count);
    const long double same_mean=pooled_same_state.sum/pooled_same_state.count;
    const long double smooth_mean=pooled_smooth.sum/pooled_smooth.count;
    const uint64_t smooth_nonzero=smooth_positive+smooth_negative;
    const int gate_sobol=sobol_mismatches==0&&sobol_duplicates==0&&expansion_violations==0;
    const int gate_price=price_bit_mismatches==0&&tile_delta_mismatches==0;
    const int gate_same=worst_same_state.magnitude<=1e-6L&&fabsl(same_mean)<=1e-7L;
    const int gate_smooth=worst_smooth.magnitude<=1e-4L&&fabsl(smooth_mean)<=1e-6L&&
        (smooth_nonzero<20||(smooth_positive*100<95*smooth_nonzero&&
                            smooth_negative*100<95*smooth_nonzero));
    const int gate_bump=bump_covered==contract_count;
    const int gate_parity=parity_covered==contract_count;
    const int gate_convergence=rmse4096<=0.80L*rmse512+1e-6L&&
        median_se[3]<=0.80L*median_se[0]+1e-6L&&
        median_se32<=0.80L*median_se8+1e-8L;
    const int qualified=gate_sobol&&gate_price&&gate_same&&gate_smooth&&
                        gate_bump&&gate_parity&&gate_convergence;
    FILE *out=fopen(output_path,"w");if(!out)return 2;
    float strikes[32];asian_genuine_strip_fixed_strikes(32,strikes);
    fprintf(out,"{\n\"schema\":1,\n\"decision\":\"%s\",\n",
        qualified?"DELTA_QUALIFIED":"DELTA_REMAINS_DIAGNOSTIC");
    fprintf(out,"\"contract\":{\"replications\":32,\"N\":[16,32,64,128,256],"
        "\"strike_counts\":[1,4,8,16,32],\"prefixes\":[512,1024,2048,4096],"
        "\"bump_relative_powers\":[-12,-14,-16],"
        "\"shift_master\":\"0x%016"PRIx64"\",\"shift_stride\":\"0x%016"PRIx64"\"},\n",
        shift_master,shift_stride);
    fputs("\"shifts\":[",out);for(uint32_t r=0;r<REPLICATIONS;++r)
        fprintf(out,"%s{\"replication\":%u,\"initial_state\":\"0x%016"PRIx64
            "\",\"vector_fnv1a64\":\"0x%016"PRIx64"\"}",r?",":"",r,
            shift_records[r].initial_state,shift_records[r].vector_hash);fputs("],\n",out);
    fprintf(out,"\"sobol\":{\"tested_words\":%"PRIu64",\"tested_prefixes\":%"PRIu64
        ",\"direct_recurrence_mismatches\":%"PRIu64",\"duplicate_shifted_d1_words\":%"PRIu64
        ",\"prefix_expansion_violations\":%"PRIu64",\"max_inverse_cdf_residual\":%.17g},\n",
        tested_sobol_words,tested_prefixes,sobol_mismatches,sobol_duplicates,
        expansion_violations,(double)max_inverse_cdf_residual);
    fprintf(out,"\"production\":{\"price_bit_mismatches\":%"PRIu64
        ",\"tile_delta_mismatches\":%"PRIu64",\"max_same_state_error\":%.17g,"
        "\"signed_mean_same_state_error\":%.17g},\n",price_bit_mismatches,
        tile_delta_mismatches,(double)worst_same_state.magnitude,(double)same_mean);
    fprintf(out,"\"pathwise\":{\"max_smooth_residual\":%.17g,"
        "\"signed_mean_smooth_residual\":%.17g,\"smooth_positive\":%"PRIu64
        ",\"smooth_negative\":%"PRIu64",\"arithmetic_ambiguous_paths\":%"PRIu64
        ",\"geometric_ambiguous_paths\":%"PRIu64"},\n",(double)worst_smooth.magnitude,
        (double)smooth_mean,smooth_positive,smooth_negative,arithmetic_ambiguities,
        geometric_ambiguities);
    fprintf(out,"\"bump_agreement\":{\"contracts\":%"PRIu64",\"covered\":%"PRIu64
        ",\"coverage\":%.17g,\"worst_ratio_to_expanded_limit\":%.17g,"
        "\"worst_contract\":{\"N\":%u,\"strike_count\":%u,\"strike\":%.9g,\"strike_position\":%u,"
        "\"prefix\":%u,\"estimator\":\"%s\",\"side\":\"%s\","
        "\"bias\":%.17g,\"expanded_limit\":%.17g,\"finite_difference_uncertainty\":%.17g}},\n",
        contract_count,bump_covered,(double)bump_covered/contract_count,
        (double)worst_bump_ratio,bump_fail_n,bump_fail_count,bump_fail_strike,bump_fail_k,bump_fail_prefix,
        bump_fail_est?"geometric_cv":"arithmetic",bump_fail_side?"put":"call",
        (double)bump_fail_bias,(double)bump_fail_limit,(double)bump_fail_fd);
    fprintf(out,"\"parity\":{\"contracts\":%"PRIu64",\"covered\":%"PRIu64
        ",\"coverage\":%.17g,\"worst_ratio_to_limit\":%.17g,"
        "\"worst_contract\":{\"N\":%u,\"strike_count\":%u,\"strike\":%.9g,"
        "\"strike_position\":%u,\"prefix\":%u,\"estimator\":\"%s\","
        "\"side\":\"%s\",\"bias\":%.17g,\"limit\":%.17g}},\n",contract_count,
        parity_covered,(double)parity_covered/contract_count,(double)worst_parity_ratio,
        parity_fail_n,parity_fail_count,parity_fail_strike,parity_fail_k,parity_fail_prefix,
        parity_fail_est?"geometric_cv":"arithmetic",parity_fail_side?"put":"call",
        (double)parity_fail_bias,(double)parity_fail_limit);
    fputs("\"convergence\":{\"pooled\":[",out);
    for(uint32_t pi=0;pi<PREFIX_COUNT;++pi){
        const long double bias=pooled_diff[pi].sum/pooled_diff[pi].count;
        const long double rmse=sqrtl(pooled_diff[pi].sumsq/pooled_diff[pi].count);
        fprintf(out,"%s{\"prefix\":%u,\"bias\":%.17g,\"rmse\":%.17g,"
            "\"median_estimator_se\":%.17g}",pi?",":"",prefix_grid[pi],
            (double)bias,(double)rmse,(double)median_se[pi]);
    }
    fprintf(out,"],\"median_contract_se_replications_8\":%.17g,"
        "\"median_contract_se_replications_32\":%.17g},\n",
        (double)median_se8,(double)median_se32);
    fputs("\"aggregate_tables\":[",out);int aggregate_comma=0;
    for(uint32_t ni=0;ni<N_COUNT;++ni)for(uint32_t estimator=0;estimator<ESTIMATOR_COUNT;++estimator)
    for(uint32_t side=0;side<SIDE_COUNT;++side)for(uint32_t pi=0;pi<PREFIX_COUNT;++pi){
        long double sum=0.0L,sumsq=0.0L;uint64_t observations=0,contracts=0,covered=0;
        for(uint32_t ci=0;ci<STRIKE_COUNT_COUNT;++ci)for(uint32_t k=0;k<strike_count_grid[ci];++k){
            contract_stat_t*s=&stats[ni][ci][k][estimator][side][pi];
            sum+=s->sum_diff;sumsq+=s->sumsq_diff;observations+=s->observations;++contracts;
            const long double count=s->observations,bias=s->sum_diff/count;
            const long double se=sample_sd(s->sum_diff,s->sumsq_diff,s->observations)/sqrtl(count);
            const long double fd01=fabsl(s->sum_fd01/count)+5.0L*
                sample_sd(s->sum_fd01,s->sumsq_fd01,s->observations)/sqrtl(count);
            const long double fd12=fabsl(s->sum_fd12/count)+5.0L*
                sample_sd(s->sum_fd12,s->sumsq_fd12,s->observations)/sqrtl(count);
            if(fabsl(bias)<=5.0L*se+fmaxl(fd01,fd12)+1e-6L)++covered;
        }
        fprintf(out,"%s{\"N\":%u,\"estimator\":\"%s\",\"side\":\"%s\","
            "\"prefix\":%u,\"bias\":%.17g,\"rmse\":%.17g,\"contracts\":%"PRIu64
            ",\"covered\":%"PRIu64",\"coverage\":%.17g}",aggregate_comma++?",":"",
            fixing_grid[ni],estimator?"geometric_cv":"arithmetic",side?"put":"call",
            prefix_grid[pi],(double)(sum/observations),(double)sqrtl(sumsq/observations),
            contracts,covered,(double)covered/contracts);
    }
    fputs("],\n",out);
    fputs("\"worst_cases\":{",out);print_worst(out,"same_state",&worst_same_state,strikes);
    fputc(',',out);print_worst(out,"smooth_residual",&worst_smooth,strikes);
    fputc(',',out);print_worst(out,"unadjusted_kink",&worst_kink,strikes);fputs("},\n",out);
    fprintf(out,"\"gates\":{\"sobol_exact_unique_expanding\":%s,"
        "\"price_and_tile_bits\":%s,\"same_state\":%s,\"smooth_residual\":%s,"
        "\"bump_agreement\":%s,\"parity\":%s,\"convergence\":%s}\n}\n",
        gate_sobol?"true":"false",gate_price?"true":"false",gate_same?"true":"false",
        gate_smooth?"true":"false",gate_bump?"true":"false",gate_parity?"true":"false",
        gate_convergence?"true":"false");
    fclose(out);
    fprintf(stderr,"replication_decision=%s\n",qualified?"DELTA_QUALIFIED":"DELTA_REMAINS_DIAGNOSTIC");
    return 0;
}
