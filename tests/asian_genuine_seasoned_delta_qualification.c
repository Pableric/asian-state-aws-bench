#define _POSIX_C_SOURCE 200112L
#include "private/asian_genuine_seasoned_strip_diag.h"

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
  REPS = 32,
  CONTRACTS = 25,
  STRIKES = 32,
  PREFIXES = 4,
  ESTIMATORS = 2,
  SIDES = 2,
  BUMPS = 3
};
static const uint32_t prefix_grid[PREFIXES] = {512, 1024, 2048, 4096};
static const uint32_t total_grid[5] = {16, 32, 64, 128, 256};
static const int bump_power[BUMPS] = {12, 14, 16};
static const uint64_t shift_master = UINT64_C(0xd1e17a5eedc0ffee);
static const uint64_t shift_stride = UINT64_C(0x9e3779b97f4a7c15);

typedef struct {
  long double diff_sum, diff_sumsq;
  long double adjacent_sum[2], adjacent_sumsq[2];
  long double estimate_sum, estimate_sumsq, estimate8_sum, estimate8_sumsq;
  uint32_t n;
} stat_t;

typedef struct {
  long double sum, sumsq;
  uint64_t n;
} pool_t;
typedef struct {
  long double magnitude, error, flip, residual;
  uint32_t m, c, rep, prefix, strike, estimator, side, path;
} worst_t;

static stat_t stats[CONTRACTS][STRIKES][ESTIMATORS][SIDES][PREFIXES];
static pool_t pooled[PREFIXES], smooth_pool, same_pool;
static worst_t worst_smooth, worst_same, worst_kink;
static uint64_t smooth_positive, smooth_negative, arithmetic_ambiguous;
static uint64_t geometric_ambiguous, sobol_mismatch, sobol_duplicates;
static uint64_t expansion_violations, tested_words;
static uint64_t shift_initial[REPS], shift_hash[REPS];
static FILE *kink_file;

static void *a64(size_t bytes) {
  void *p = NULL;
  if (posix_memalign(&p, 64, bytes))
    return NULL;
  memset(p, 0, bytes);
  return p;
}

static uint64_t splitmix64(uint64_t *state) {
  uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
  z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
  return z ^ (z >> 31);
}

static uint64_t hash_u32(uint64_t h, uint32_t word) {
  for (uint32_t b = 0; b < 4; ++b) {
    h ^= (word >> (8 * b)) & 255u;
    h *= UINT64_C(1099511628211);
  }
  return h;
}

static uint32_t sobol(uint32_t index, const uint32_t directions[32]) {
  uint32_t gray = index ^ (index >> 1), word = 0;
  for (uint32_t bit = 0; gray; ++bit, gray >>= 1)
    if (gray & 1u)
      word ^= directions[bit];
  return word;
}

static int compare_u32(const void *aa, const void *bb) {
  const uint32_t a = *(const uint32_t *)aa, b = *(const uint32_t *)bb;
  return (a > b) - (a < b);
}

static int compare_ld(const void *aa, const void *bb) {
  const long double a = *(const long double *)aa, b = *(const long double *)bb;
  return (a > b) - (a < b);
}

static long double ncdf(long double x) {
  return .5L * erfcl(-x * 0.707106781186547524400844362104849039L);
}

static long double inverse_normal(long double p) {
  static const long double a[] = {-39.69683028665376L, 220.9460984245205L,
                                  -275.9285104469687L, 138.3577518672690L,
                                  -30.66479806614716L, 2.506628277459239L};
  static const long double c[] = {-.007784894002430293L, -.3223964580411365L,
                                  -2.400758277161838L,   -2.549732539343734L,
                                  4.374664141464968L,    2.938163982698783L};
  static const long double d[] = {.007784695709041462L, .3224671290700398L,
                                  2.445134137142996L, 3.754408661907416L};
  static const long double b[] = {-54.47609879822406L, 161.5858368580409L,
                                  -155.6989798598866L, 66.80131188771972L,
                                  -13.28068155288572L};
  long double x;
  if (p < .02425L) {
    const long double q = sqrtl(-2 * logl(p));
    x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
        ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
  } else if (p > .97575L) {
    const long double q = sqrtl(-2 * logl(1 - p));
    x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
        ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
  } else {
    const long double q = p - .5L, r = q * q;
    x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) *
        q / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
  }
  const long double invsqrt = 0.398942280401432677939946059934381868L;
  for (uint32_t i = 0; i < 2; ++i)
    x -= (ncdf(x) - p) / (invsqrt * expl(-.5L * x * x));
  return x;
}

static float fbits(uint32_t bits) {
  float x;
  memcpy(&x, &bits, 4);
  return x;
}

static float qualified_exp(float input) {
  static const uint32_t cb[9] = {0x3f800000, 0x3f7ffff9, 0x3efffffc,
                                 0x3e2aabbf, 0x3d2aab67, 0x3c085d88,
                                 0x3ab5de3b, 0x3959cfde, 0x37d8c471};
  const float e = nearbyintf(input * fbits(0x3fb8aa3b));
  float r = fmaf(-fbits(0x3f318000), e, input);
  r = fmaf(-fbits(0xb95e8083), e, r);
  float p = fbits(cb[8]);
  for (int i = 7; i >= 0; --i)
    p = fmaf(r, p, fbits(cb[i]));
  return scalbnf(p, (int)e);
}

static int load_directions(uint32_t directions[DIMS][32]) {
  FILE *in = fopen("direction_numbers/joe_kuo_6_21201.bin", "rb");
  if (!in)
    return -1;
  for (uint32_t d = 0; d < DIMS; ++d) {
    uint32_t n;
    if (fread(&n, 4, 1, in) != 1 || n != 32 ||
        fread(directions[d], 4, 32, in) != 32) {
      fclose(in);
      return -1;
    }
  }
  fclose(in);
  return 0;
}

static int build_normals(const uint32_t directions[DIMS][32], uint32_t rep,
                         long double *normal) {
  uint32_t shifts[DIMS], *d1 = malloc(PATHS * 4), *saved = malloc(PATHS * 4),
                         *sorted = malloc(PATHS * 4);
  if (!d1 || !saved || !sorted) {
    free(sorted);
    free(saved);
    free(d1);
    return -1;
  }
  uint64_t state = shift_master + shift_stride * (uint64_t)(rep + 1),
           hash = UINT64_C(1469598103934665603);
  shift_initial[rep] = state;
  for (uint32_t d = 0; d < DIMS; ++d) {
    shifts[d] = (uint32_t)splitmix64(&state);
    hash = hash_u32(hash, shifts[d]);
  }
  shift_hash[rep] = hash;
  for (uint32_t d = 0; d < DIMS; ++d) {
    uint32_t recurrence = sobol(8192, directions[d]);
    for (uint32_t p = 0; p < PATHS; ++p) {
      const uint32_t index = 8192 + p;
      if (p)
        recurrence ^= directions[d][__builtin_ctz(index)];
      const uint32_t direct = sobol(index, directions[d]);
      if (direct != recurrence)
        ++sobol_mismatch;
      const uint32_t shifted = direct ^ shifts[d];
      if (d == 0)
        d1[p] = shifted;
      normal[(size_t)d * PATHS + p] =
          inverse_normal(((long double)shifted + .5L) * 0x1p-32L);
      ++tested_words;
    }
  }
  memcpy(saved, d1, PATHS * 4);
  memcpy(sorted, d1, PATHS * 4);
  qsort(sorted, PATHS, 4, compare_u32);
  for (uint32_t p = 1; p < PATHS; ++p)
    if (sorted[p] == sorted[p - 1])
      ++sobol_duplicates;
  for (uint32_t pi = 0; pi < PREFIXES; ++pi)
    for (uint32_t p = 0; p < prefix_grid[pi]; ++p)
      if (d1[p] != saved[p])
        ++expansion_violations;
  free(sorted);
  free(saved);
  free(d1);
  return 0;
}

static void contract_counts(uint32_t id, uint32_t *m, uint32_t *c) {
  *m = total_grid[id / 5];
  const uint32_t which = id % 5;
  *c = which == 0   ? 0
       : which == 1 ? 1
       : which == 2 ? *m / 4
       : which == 3 ? *m / 2
                    : *m - 1;
}

static void nonuniform_history(uint32_t c, long double *q0, long double *logs) {
  static const double cycle[] = {82,   117.5,  94.25, 108,
                                 76.5, 123.75, 101,   89.5};
  *q0 = 0;
  *logs = 0;
  for (uint32_t i = 0; i < c; ++i) {
    *q0 += cycle[i & 7u];
    *logs += logl(cycle[i & 7u]);
  }
}

static void geometric_exact(long double s0, uint32_t f, uint32_t m,
                            long double logs, long double strike,
                            long double *call_price, long double *put_price,
                            long double *call_delta, long double *put_delta) {
  const long double rate = .03L, sigma = .20L, dt = 1.0L / f,
                    mu = rate - .5L * sigma * sigma;
  const long double st = dt * f * (f + 1.0L) * .5L;
  const long double sm = f * (f + 1.0L) * (2.0L * f + 1.0L) / 6.0L;
  const long double mean =
      logs / m + (long double)f / m * logl(s0) + mu * st / m;
  const long double variance = sigma * sigma * dt * sm / ((long double)m * m),
                    root = sqrtl(variance);
  const long double forward = expl(mean + .5L * variance),
                    d2 = (mean - logl(strike)) / root, d1 = d2 + root;
  const long double disc = expl(-rate), scale = (long double)f / (m * s0);
  *call_price = disc * (forward * ncdf(d1) - strike * ncdf(d2));
  *put_price = disc * (strike * ncdf(-d2) - forward * ncdf(-d1));
  *call_delta = disc * forward * ncdf(d1) * scale;
  *put_delta = -disc * forward * ncdf(-d1) * scale;
}

static long double payoff(long double value, long double strike, int sign) {
  const long double x = sign * (value - strike);
  return x > 0 ? x : 0;
}

static float reduce_lanes(const float lo[16], const float hi[16]) {
  float lane[16], a[4], b[4];
  for (uint32_t i = 0; i < 16; ++i)
    lane[i] = lo[i] + hi[i];
  for (uint32_t i = 0; i < 4; ++i) {
    a[i] = lane[i] + lane[4 + i];
    b[i] = lane[8 + i] + lane[12 + i];
  }
  for (uint32_t i = 0; i < 4; ++i)
    a[i] = a[i] + b[i];
  a[0] = a[0] + a[2];
  a[1] = a[1] + a[3];
  return a[0] + a[1];
}

static void emulate_delta(const float *q, const float *g,
                          const asian_genuine_strip_context_t *ctx,
                          uint32_t estimator,
                          asian_genuine_strip_output_t *out) {
  for (uint32_t k = 0; k < ctx->strike_count; ++k) {
    const asian_genuine_strip_strike_t *r = &ctx->strikes[k];
    float lo[16] = {0}, hi[16] = {0};
    for (uint32_t packet = 0; packet < PATHS; packet += 32)
      for (uint32_t half = 0; half < 2; ++half)
        for (uint32_t lane = 0; lane < 16; ++lane) {
          const uint32_t p = packet + half * 16 + lane;
          const float a = (q[p] + ctx->initial_q) * ctx->inv_total;
          float d = r->direct_sign * (a - r->strike) > 0
                        ? r->direct_sign * q[p] * ctx->delta_q_scale
                        : 0;
          if (estimator) {
            const float gd = r->direct_sign * (g[p] - r->strike) > 0
                                 ? r->direct_sign * g[p] * ctx->delta_g_scale
                                 : 0;
            d = d - gd;
          }
          if (half)
            hi[lane] = hi[lane] + d;
          else
            lo[lane] = lo[lane] + d;
        }
    double d = (double)reduce_lanes(lo, hi) / PATHS;
    if (estimator)
      d += r->geometric_delta_exact_direct;
    out->values[k].call_delta = d + r->call_delta_adjust;
    out->values[k].put_delta = d + r->put_delta_adjust;
  }
}

static void pool_add(pool_t *p, long double x) {
  p->sum += x;
  p->sumsq += x * x;
  ++p->n;
}
static long double sd(long double sum, long double sumsq, uint32_t n) {
  if (n < 2)
    return 0;
  long double v = (sumsq - sum * sum / n) / (n - 1);
  if (v < 0 && v > -64 * LDBL_EPSILON)
    v = 0;
  return v > 0 ? sqrtl(v) : 0;
}

static void update_worst(worst_t *w, long double magnitude, long double error,
                         long double flip, long double residual, uint32_t m,
                         uint32_t c, uint32_t rep, uint32_t prefix,
                         uint32_t strike, uint32_t estimator, uint32_t side,
                         uint32_t path) {
  if (magnitude <= w->magnitude)
    return;
  *w = (worst_t){magnitude, error,  flip,   residual,  m,    c,
                 rep,       prefix, strike, estimator, side, path};
}

static int run_contract(uint32_t id, uint32_t rep, const long double *normal,
                        const float strikes[32]) {
  uint32_t m, c;
  contract_counts(id, &m, &c);
  const uint32_t f = m - c;
  long double q0, logs;
  nonuniform_history(c, &q0, &logs);
  _Alignas(64) asian_genuine_strip_context_t ctx;
  if (asian_genuine_strip_prepare(&ctx, 100, .03, 0, .20, 1, f, c, (double)q0,
                                  (double)logs, strikes, 32))
    return -1;
  float *sf = a64(PATHS * 4), *qf = a64(PATHS * 4), *lf = a64(PATHS * 4),
        *gf = a64(PATHS * 4);
  long double *sl = calloc(PATHS, sizeof *sl), *ql = calloc(PATHS, sizeof *ql),
              *ll = calloc(PATHS, sizeof *ll), *al = calloc(PATHS, sizeof *al),
              *gl = calloc(PATHS, sizeof *gl);
  if (!sf || !qf || !lf || !gf || !sl || !ql || !ll || !al || !gl)
    return -1;
  for (uint32_t p = 0; p < PATHS; ++p) {
    sf[p] = 100;
    sl[p] = 100;
  }
  const float driftf = (float)((.03 - .5 * .20 * .20) / f),
              difff = (float)(.20 / sqrt((double)f));
  const long double drift = (.03L - .5L * .20L * .20L) / f,
                    diff = .20L / sqrtl(f);
  for (uint32_t d = 0; d < f; ++d) {
    const float wf = fbits(asian_genuine_seasoned_weight_bits(f - d, m));
    const long double wl = (long double)(f - d) / m;
    for (uint32_t p = 0; p < PATHS; ++p) {
      const long double z = normal[(size_t)d * PATHS + p];
      const float x = fmaf(difff, (float)z, driftf), growth = qualified_exp(x);
      sf[p] = sf[p] * growth;
      qf[p] = qf[p] + sf[p];
      lf[p] = fmaf(wf, x, lf[p]);
      const long double xx = drift + diff * z;
      sl[p] *= expl(xx);
      ql[p] += sl[p];
      ll[p] += wl * xx;
    }
  }
  const long double base = logs / m + (long double)f / m * logl(100);
  for (uint32_t p = 0; p < PATHS; ++p) {
    gf[p] = qualified_exp(ctx.log_base + lf[p]);
    al[p] = (q0 + ql[p]) / m;
    gl[p] = expl(base + ll[p]);
  }

  long double pw[STRIKES][ESTIMATORS] = {{0}},
              same[STRIKES][ESTIMATORS] = {{0}};
  long double flips[STRIKES][ESTIMATORS] = {{0}};
  long double bump[STRIKES][BUMPS][ESTIMATORS][SIDES] = {{{{0}}}};
  _Alignas(64) asian_genuine_strip_output_t emulated[ESTIMATORS];
  emulate_delta(qf, gf, &ctx, 0, &emulated[0]);
  emulate_delta(qf, gf, &ctx, 1, &emulated[1]);
  uint32_t next_prefix = 0;
  for (uint32_t p = 0; p < PATHS; ++p) {
    const float af = (ctx.initial_q + qf[p]) * ctx.inv_total;
    const long double da = ql[p] / (m * 100.0L),
                      dg = (long double)f / m * gl[p] / 100;
    const float daf = qf[p] * ctx.delta_q_scale,
                dgf = gf[p] * ctx.delta_g_scale;
    for (uint32_t k = 0; k < STRIKES; ++k) {
      const asian_genuine_strip_strike_t *r = &ctx.strikes[k];
      const long double strike = strikes[k];
      const int sign = r->direct_sign > 0 ? 1 : -1;
      const int ia = sign * (al[p] - strike) > 0,
                ig = sign * (gl[p] - strike) > 0;
      const int ifa = sign * ((long double)af - strike) > 0,
                ifg = sign * ((long double)gf[p] - strike) > 0;
      pw[k][0] += ia ? sign * da * expl(-.03L) : 0;
      pw[k][1] += (ia ? sign * da * expl(-.03L) : 0) -
                  (ig ? sign * dg * expl(-.03L) : 0);
      same[k][0] += ifa ? sign * daf : 0;
      same[k][1] += (ifa ? sign * daf : 0) - (ifg ? sign * dgf : 0);
      const long double fa = (ifa - ia) * sign * da * expl(-.03L);
      const long double fg = (ifg - ig) * sign * dg * expl(-.03L);
      flips[k][0] += fa;
      flips[k][1] += fa - fg;
      const int aa = (long double)af != al[p] && strike >= fminl(af, al[p]) &&
                     strike <= fmaxl(af, al[p]);
      const int ag = (long double)gf[p] != gl[p] &&
                     strike >= fminl(gf[p], gl[p]) &&
                     strike <= fmaxl(gf[p], gl[p]);
      if (aa) {
        ++arithmetic_ambiguous;
        fprintf(kink_file,
                "{\"state\":\"arithmetic\",\"M\":%u,\"c\":%u,\"f\":%u,"
                "\"replication\":%u,\"path\":%u,\"strike\":%.9g,\"float_"
                "value\":%.9g,\"reference_value\":%.17Lg,\"indicator_flip_"
                "contribution\":%.17Lg}\n",
                m, c, f, rep, p, strikes[k], af, al[p], fa / PATHS);
      }
      if (ag) {
        ++geometric_ambiguous;
        fprintf(kink_file,
                "{\"state\":\"geometric\",\"M\":%u,\"c\":%u,\"f\":%u,"
                "\"replication\":%u,\"path\":%u,\"strike\":%.9g,\"float_"
                "value\":%.9g,\"reference_value\":%.17Lg,\"indicator_flip_"
                "contribution\":%.17Lg}\n",
                m, c, f, rep, p, strikes[k], gf[p], gl[p], fg / PATHS);
      }
      for (uint32_t b = 0; b < BUMPS; ++b) {
        const long double h = ldexpl(100.0L, -bump_power[b]);
        const long double plus = (100 + h) / 100, minus = (100 - h) / 100;
        const long double ap = (q0 + ql[p] * plus) / m,
                          am = (q0 + ql[p] * minus) / m;
        const long double gp = gl[p] * powl(plus, (long double)f / m);
        const long double gm = gl[p] * powl(minus, (long double)f / m);
        for (uint32_t side = 0; side < SIDES; ++side) {
          const int ss = side ? -1 : 1;
          const long double ad =
              expl(-.03L) * (payoff(ap, strike, ss) - payoff(am, strike, ss)) /
              (2 * h);
          const long double gd =
              expl(-.03L) * (payoff(gp, strike, ss) - payoff(gm, strike, ss)) /
              (2 * h);
          bump[k][b][0][side] += ad;
          bump[k][b][1][side] += ad - gd;
        }
      }
    }
    if (next_prefix < PREFIXES && p + 1 == prefix_grid[next_prefix]) {
      const long double inv = 1.0L / (p + 1);
      for (uint32_t k = 0; k < STRIKES; ++k) {
        const asian_genuine_strip_strike_t *r = &ctx.strikes[k];
        long double cp, pp, cd, pd;
        geometric_exact(100, f, m, logs, strikes[k], &cp, &pp, &cd, &pd);
        const uint32_t selected = r->direct_sign > 0 ? 0 : 1;
        const long double exact_selected = selected ? pd : cd;
        for (uint32_t e = 0; e < ESTIMATORS; ++e)
          for (uint32_t side = 0; side < SIDES; ++side) {
            const long double adjustment =
                side == 0 ? r->call_delta_adjust : r->put_delta_adjust;
            const long double path =
                pw[k][e] * inv + (e ? exact_selected : 0) + adjustment;
            const long double float_oracle =
                same[k][e] * inv + (e ? r->geometric_delta_exact_direct : 0) +
                adjustment;
            long double bumps[BUMPS];
            for (uint32_t b = 0; b < BUMPS; ++b) {
              const long double h = ldexpl(100.0L, -bump_power[b]);
              long double pcp, ppp, pcd, ppd, mcp, mpp, mcd, mpd;
              geometric_exact(100 + h, f, m, logs, strikes[k], &pcp, &ppp, &pcd,
                              &ppd);
              geometric_exact(100 - h, f, m, logs, strikes[k], &mcp, &mpp, &mcd,
                              &mpd);
              const long double exact_bump =
                  ((side ? ppp : pcp) - (side ? mpp : mcp)) / (2 * h);
              bumps[b] = bump[k][b][e][side] * inv + (e ? exact_bump : 0);
            }
            stat_t *st = &stats[id][k][e][side][next_prefix];
            const long double diffv = path - bumps[2];
            st->diff_sum += diffv;
            st->diff_sumsq += diffv * diffv;
            st->estimate_sum += path;
            st->estimate_sumsq += path * path;
            if (rep < 8) {
              st->estimate8_sum += path;
              st->estimate8_sumsq += path * path;
            }
            for (uint32_t a = 0; a < 2; ++a) {
              const long double v = bumps[a] - bumps[a + 1];
              st->adjacent_sum[a] += v;
              st->adjacent_sumsq[a] += v * v;
            }
            ++st->n;
            pool_add(&pooled[next_prefix], diffv);
            const long double flip = flips[k][e] * inv,
                              residual = float_oracle - path - flip;
            pool_add(&smooth_pool, residual);
            if (residual > 1e-8L)
              ++smooth_positive;
            if (residual < -1e-8L)
              ++smooth_negative;
            update_worst(&worst_smooth, fabsl(residual), float_oracle - path,
                         flip, residual, m, c, rep, p + 1, k, e, side, p);
            update_worst(&worst_kink, fabsl(flip), float_oracle - path, flip,
                         residual, m, c, rep, p + 1, k, e, side, p);
            if (next_prefix == PREFIXES - 1) {
              const long double actual = side == 0
                                             ? emulated[e].values[k].call_delta
                                             : emulated[e].values[k].put_delta;
              const long double same_error = actual - float_oracle;
              pool_add(&same_pool, same_error);
              update_worst(&worst_same, fabsl(same_error), same_error, 0, 0, m,
                           c, rep, p + 1, k, e, side, p);
            }
          }
      }
      ++next_prefix;
    }
  }
  free(gl);
  free(al);
  free(ll);
  free(ql);
  free(sl);
  free(gf);
  free(lf);
  free(qf);
  free(sf);
  return 0;
}

static int write_reports(const char *json_path, const char *md_path) {
  uint64_t covered = 0, total = 0;
  long double worst_ratio = 0;
  pool_t aggregate[ESTIMATORS][SIDES][PREFIXES] = {{{{0}}}};
  uint64_t aggregate_covered[ESTIMATORS][SIDES][PREFIXES] = {{{0}}};
  uint64_t aggregate_total[ESTIMATORS][SIDES][PREFIXES] = {{{0}}};
  for (uint32_t id = 0; id < CONTRACTS; ++id)
    for (uint32_t k = 0; k < STRIKES; ++k)
      for (uint32_t e = 0; e < ESTIMATORS; ++e)
        for (uint32_t s = 0; s < SIDES; ++s)
          for (uint32_t p = 0; p < PREFIXES; ++p) {
            stat_t *st = &stats[id][k][e][s][p];
            const long double se =
                sd(st->diff_sum, st->diff_sumsq, st->n) / sqrtl(st->n);
            long double uncertainty = 0;
            for (uint32_t a = 0; a < 2; ++a) {
              const long double mean = st->adjacent_sum[a] / st->n;
              const long double use =
                  fabsl(mean) +
                  5 * sd(st->adjacent_sum[a], st->adjacent_sumsq[a], st->n) /
                      sqrtl(st->n);
              if (use > uncertainty)
                uncertainty = use;
            }
            const long double bias = fabsl(st->diff_sum / st->n),
                              limit = 5 * se + uncertainty + 1e-6L;
            ++total;
            ++aggregate_total[e][s][p];
            pool_add(&aggregate[e][s][p], st->diff_sum / st->n);
            if (bias <= limit) {
              ++covered;
              ++aggregate_covered[e][s][p];
            }
            if (limit > 0 && bias / limit > worst_ratio)
              worst_ratio = bias / limit;
          }
  const long double rmse0 = sqrtl(pooled[0].sumsq / pooled[0].n);
  const long double rmse3 = sqrtl(pooled[3].sumsq / pooled[3].n);
  long double se512[CONTRACTS * STRIKES * ESTIMATORS * SIDES];
  long double se4096[CONTRACTS * STRIKES * ESTIMATORS * SIDES],
      se8[CONTRACTS * STRIKES * ESTIMATORS * SIDES];
  uint32_t sn = 0;
  for (uint32_t id = 0; id < CONTRACTS; ++id)
    for (uint32_t k = 0; k < STRIKES; ++k)
      for (uint32_t e = 0; e < ESTIMATORS; ++e)
        for (uint32_t s = 0; s < SIDES; ++s) {
          stat_t *a = &stats[id][k][e][s][0], *b = &stats[id][k][e][s][3];
          se512[sn] = sd(a->estimate_sum, a->estimate_sumsq, 32) / sqrtl(32);
          se4096[sn] = sd(b->estimate_sum, b->estimate_sumsq, 32) / sqrtl(32);
          se8[sn] = sd(b->estimate8_sum, b->estimate8_sumsq, 8) / sqrtl(8);
          ++sn;
        }
  qsort(se512, sn, sizeof *se512, compare_ld);
  qsort(se4096, sn, sizeof *se4096, compare_ld);
  qsort(se8, sn, sizeof *se8, compare_ld);
  const long double med512 = se512[sn / 2], med4096 = se4096[sn / 2],
                    med8 = se8[sn / 2];
  const long double smooth_mean = smooth_pool.sum / smooth_pool.n,
                    same_mean = same_pool.sum / same_pool.n;
  const uint64_t nz = smooth_positive + smooth_negative;
  const int sign_ok = nz < 20 || (smooth_positive * 100 < 95 * nz &&
                                  smooth_negative * 100 < 95 * nz);
  const int pass =
      sobol_mismatch == 0 && sobol_duplicates == 0 &&
      expansion_violations == 0 && covered == total &&
      worst_smooth.magnitude <= 1e-4L && fabsl(smooth_mean) <= 1e-6L &&
      sign_ok && worst_same.magnitude <= 1e-6L && fabsl(same_mean) <= 1e-7L &&
      rmse3 <= .8L * rmse0 + 1e-6L && med4096 <= .8L * med512 + 1e-6L &&
      med4096 <= .8L * med8 + 1e-8L;
  FILE *j = fopen(json_path, "wb"), *md = fopen(md_path, "wb");
  if (!j || !md)
    return -1;
  fprintf(
      j,
      "{\n  \"schema\": \"asian-genuine-seasoned-delta-qualification-v1\",\n");
  fprintf(j, "  \"decision\": \"%s\",\n",
          pass ? "SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING"
               : "SEASONED_DELTA_REMAINS_DIAGNOSTIC");
  fprintf(
      j,
      "  \"grid\": "
      "{\"totals\":[16,32,64,128,256],\"completed\":[\"0\",\"1\",\"M/4\",\"M/"
      "2\",\"M-1\"],\"strike_counts\":[1,4,8,16,32],\"prefixes\":[512,1024,"
      "2048,4096],\"replications\":32,\"bump_powers\":[12,14,16],\"rates\":[-0."
      "02,0,0.03],\"sigmas\":[0,0.20],\"histories\":[\"flat\",\"increasing\","
      "\"decreasing\",\"nonuniform_cycle\"]},\n");
  float fixed_strikes[32];
  asian_genuine_strip_fixed_strikes(32, fixed_strikes);
  fprintf(j, "  \"strike_grid\": [");
  for (uint32_t k = 0; k < 32; ++k) {
    uint32_t bits;
    memcpy(&bits, &fixed_strikes[k], 4);
    fprintf(j, "%s{\"value\":%.9g,\"float32_bits\":\"0x%08" PRIx32 "\"}",
            k ? "," : "", fixed_strikes[k], bits);
  }
  fprintf(j, "],\n");
  fprintf(j, "  \"tolerances\": "
             "{\"price_max_abs\":0.0001,\"delta_smooth_max_abs\":0.0001,\"same_"
             "state_delta_max_abs\":0.000001,\"same_state_signed_mean_abs\":0."
             "0000001,\"smooth_signed_mean_abs\":0.000001,\"bump_expanded_"
             "sigma\":5,\"convergence_ratio\":0.8},\n");
  fprintf(j,
          "  \"gates\": "
          "{\"sobol_exact_unique_expanding\":%s,\"bump_coverage\":%s,\"smooth_"
          "residual\":%s,\"same_state\":%s,\"convergence\":%s},\n",
          sobol_mismatch || sobol_duplicates || expansion_violations ? "false"
                                                                     : "true",
          covered == total ? "true" : "false",
          worst_smooth.magnitude <= 1e-4L && fabsl(smooth_mean) <= 1e-6L &&
                  sign_ok
              ? "true"
              : "false",
          worst_same.magnitude <= 1e-6L && fabsl(same_mean) <= 1e-7L ? "true"
                                                                     : "false",
          rmse3 <= .8L * rmse0 + 1e-6L && med4096 <= .8L * med512 + 1e-6L &&
                  med4096 <= .8L * med8 + 1e-8L
              ? "true"
              : "false");
  fprintf(j,
          "  \"sobol\": {\"tested_words\":%" PRIu64 ",\"mismatches\":%" PRIu64
          ",\"duplicates\":%" PRIu64 ",\"expansion_violations\":%" PRIu64
          "},\n",
          tested_words, sobol_mismatch, sobol_duplicates, expansion_violations);
  fprintf(j,
          "  \"delta\": "
          "{\"max_smooth_residual\":%.17Lg,\"signed_mean_smooth_residual\":%."
          "17Lg,\"max_same_state_error\":%.17Lg,\"signed_mean_same_state_"
          "error\":%.17Lg,\"arithmetic_ambiguities\":%" PRIu64
          ",\"geometric_ambiguities\":%" PRIu64 "},\n",
          worst_smooth.magnitude, smooth_mean, worst_same.magnitude, same_mean,
          arithmetic_ambiguous, geometric_ambiguous);
  fprintf(j,
          "  \"bump\": {\"contracts\":%" PRIu64 ",\"covered\":%" PRIu64
          ",\"coverage\":%.17Lg,\"worst_ratio\":%.17Lg},\n",
          total, covered, (long double)covered / total, worst_ratio);
  fprintf(j, "  \"aggregate_tables\": [");
  int table_comma = 0;
  for (uint32_t e = 0; e < ESTIMATORS; ++e)
    for (uint32_t s = 0; s < SIDES; ++s)
      for (uint32_t p = 0; p < PREFIXES; ++p) {
        pool_t *a = &aggregate[e][s][p];
        fprintf(
            j,
            "%s{\"estimator\":\"%s\",\"side\":\"%s\",\"prefix\":%u,\"bias\":%."
            "17Lg,\"rmse\":%.17Lg,\"coverage\":%.17Lg,\"covered\":%" PRIu64
            ",\"contracts\":%" PRIu64 "}",
            table_comma++ ? "," : "", e ? "geometric_cv" : "arithmetic",
            s ? "put" : "call", prefix_grid[p], a->sum / a->n,
            sqrtl(a->sumsq / a->n),
            (long double)aggregate_covered[e][s][p] / aggregate_total[e][s][p],
            aggregate_covered[e][s][p], aggregate_total[e][s][p]);
      }
  fprintf(j, "],\n");
  fprintf(j,
          "  \"convergence\": "
          "{\"rmse_512\":%.17Lg,\"rmse_4096\":%.17Lg,\"median_se_512\":%.17Lg,"
          "\"median_se_4096\":%.17Lg,\"median_se_8_replications\":%.17Lg},\n",
          rmse0, rmse3, med512, med4096, med8);
  fprintf(j,
          "  \"worst_cases\": "
          "{\"smooth\":{\"M\":%u,\"c\":%u,\"replication\":%u,\"prefix\":%u,"
          "\"strike_index\":%u,\"estimator\":%u,\"side\":%u,\"unadjusted\":%."
          "17Lg,\"flip\":%.17Lg,\"residual\":%.17Lg},\"same_state\":{\"M\":%u,"
          "\"c\":%u,\"replication\":%u,\"strike_index\":%u,\"error\":%.17Lg},"
          "\"kink_contribution\":{\"M\":%u,\"c\":%u,\"replication\":%u,"
          "\"prefix\":%u,\"strike_index\":%u,\"unadjusted\":%.17Lg,\"flip\":%."
          "17Lg,\"residual\":%.17Lg}},\n",
          worst_smooth.m, worst_smooth.c, worst_smooth.rep, worst_smooth.prefix,
          worst_smooth.strike, worst_smooth.estimator, worst_smooth.side,
          worst_smooth.error, worst_smooth.flip, worst_smooth.residual,
          worst_same.m, worst_same.c, worst_same.rep, worst_same.strike,
          worst_same.error, worst_kink.m, worst_kink.c, worst_kink.rep,
          worst_kink.prefix, worst_kink.strike, worst_kink.error,
          worst_kink.flip, worst_kink.residual);
  fprintf(j, "  \"shifts\": [");
  for (uint32_t r = 0; r < REPS; ++r)
    fprintf(j,
            "%s{\"replication\":%u,\"initial_state\":\"0x%016" PRIx64
            "\",\"vector_hash\":\"0x%016" PRIx64 "\"}",
            r ? "," : "", r, shift_initial[r], shift_hash[r]);
  fprintf(j, "]\n}\n");
  fprintf(md, "# Seasoned Asian Delta qualification\n\nDecision: `%s`.\n\n",
          pass ? "SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING"
               : "SEASONED_DELTA_REMAINS_DIAGNOSTIC");
  fprintf(md, "| Gate | Result | Evidence |\n|---|---:|---:|\n");
  fprintf(md,
          "| Sobol exact/unique/expanding | %s | mismatches=%" PRIu64
          ", duplicates=%" PRIu64 ", expansion=%" PRIu64 " |\n",
          sobol_mismatch || sobol_duplicates || expansion_violations ? "FAIL"
                                                                     : "PASS",
          sobol_mismatch, sobol_duplicates, expansion_violations);
  fprintf(md, "| Same-state Delta | %s | max %.9Lg, signed mean %.9Lg |\n",
          worst_same.magnitude <= 1e-6L && fabsl(same_mean) <= 1e-7L ? "PASS"
                                                                     : "FAIL",
          worst_same.magnitude, same_mean);
  fprintf(md, "| Smooth Delta | %s | max %.9Lg, signed mean %.9Lg |\n",
          worst_smooth.magnitude <= 1e-4L && fabsl(smooth_mean) <= 1e-6L &&
                  sign_ok
              ? "PASS"
              : "FAIL",
          worst_smooth.magnitude, smooth_mean);
  fprintf(md,
          "| CRN bump coverage | %s | %" PRIu64 "/%" PRIu64
          ", worst ratio %.6Lg |\n",
          covered == total ? "PASS" : "FAIL", covered, total, worst_ratio);
  fprintf(
      md,
      "| Convergence | %s | RMSE %.6Lg -> %.6Lg; median SE %.6Lg -> %.6Lg |\n",
      rmse3 <= .8L * rmse0 + 1e-6L && med4096 <= .8L * med512 + 1e-6L &&
              med4096 <= .8L * med8 + 1e-8L
          ? "PASS"
          : "FAIL",
      rmse0, rmse3, med512, med4096);
  fprintf(md, "\n## Aggregate CRN table\n\n| Estimator | Side | Prefix | Bias "
              "| RMSE | Coverage |\n|---|---|---:|---:|---:|---:|\n");
  for (uint32_t e = 0; e < ESTIMATORS; ++e)
    for (uint32_t s = 0; s < SIDES; ++s)
      for (uint32_t p = 0; p < PREFIXES; ++p) {
        pool_t *a = &aggregate[e][s][p];
        fprintf(md, "| %s | %s | %u | %.6Lg | %.6Lg | %.6Lg |\n",
                e ? "geometric CV" : "arithmetic", s ? "put" : "call",
                prefix_grid[p], a->sum / a->n, sqrtl(a->sumsq / a->n),
                (long double)aggregate_covered[e][s][p] /
                    aggregate_total[e][s][p]);
      }
  fprintf(md,
          "\nAll %" PRIu64 " arithmetic and %" PRIu64
          " geometric ambiguous observations are retained in `kinks.jsonl`.\n",
          arithmetic_ambiguous, geometric_ambiguous);
  fclose(md);
  fclose(j);
  return pass ? 0 : -2;
}

int main(int argc, char **argv) {
  if (argc != 7 || strcmp(argv[1], "--json") || strcmp(argv[3], "--markdown") ||
      strcmp(argv[5], "--kinks"))
    return 2;
  uint32_t (*directions)[32] = a64(DIMS * 32 * 4);
  long double *normal = a64((size_t)DIMS * PATHS * sizeof *normal);
  float strikes[32];
  if (!directions || !normal || load_directions(directions) ||
      asian_genuine_strip_fixed_strikes(32, strikes))
    return 2;
  kink_file = fopen(argv[6], "wb");
  if (!kink_file)
    return 2;
  for (uint32_t rep = 0; rep < REPS; ++rep) {
    if (build_normals(directions, rep, normal))
      return 2;
    for (uint32_t id = 0; id < CONTRACTS; ++id)
      if (run_contract(id, rep, normal, strikes))
        return 2;
    fprintf(stderr, "seasoned replication %u/%u complete\n", rep + 1, REPS);
  }
  fclose(kink_file);
  const int result = write_reports(argv[2], argv[4]);
  free(normal);
  free(directions);
  if (result == 0)
    puts(
        "seasoned delta "
        "qualification=SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING");
  return result == 0 ? 0 : 2;
}
