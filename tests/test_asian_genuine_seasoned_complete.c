#define _POSIX_C_SOURCE 200112L
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_genuine_seasoned_strip_diag.h"
#include "private/asian_geometric_cv_diag.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PATHS = 4096, MAX_FIXINGS = 256 };
static int price_only;
static FILE *kink_log;

typedef struct {
  uint32_t directions[MAX_FIXINGS][32];
  uint32_t *words[2];
  float *x;
  float *growth;
  float *g;
  fragment_map_t *maps;
  asian_genuine_route_t *qualified;
  asian_genuine_route_t *seasoned;
  asian_genuine_state_t *state;
  asian_genuine_state_t *unseasoned;
  ordered_d1_diag_context_t *producer;
} fixture_t;

typedef struct {
  long double sum;
  long double max;
  uint64_t count;
  uint64_t positive;
  uint64_t negative;
} errors_t;

typedef struct {
  errors_t unadjusted;
  errors_t residual;
  errors_t same_state;
  long double flip_sum;
  uint64_t arithmetic_ambiguous;
  uint64_t geometric_ambiguous;
} delta_errors_t;

static void *a64(size_t bytes) {
  void *p = NULL;
  if (posix_memalign(&p, 64, bytes))
    return NULL;
  memset(p, 0, bytes);
  return p;
}

static uint32_t sobol(uint32_t index, const uint32_t directions[32]) {
  uint32_t gray = index ^ (index >> 1), word = 0;
  for (uint32_t bit = 0; gray; ++bit, gray >>= 1)
    if (gray & 1u)
      word ^= directions[bit];
  return word;
}

static double inverse_normal(double p) {
  static const double a[] = {-39.69683028665376, 220.9460984245205,
                             -275.9285104469687, 138.3577518672690,
                             -30.66479806614716, 2.506628277459239};
  static const double c[] = {-.007784894002430293, -.3223964580411365,
                             -2.400758277161838,   -2.549732539343734,
                             4.374664141464968,    2.938163982698783};
  static const double d[] = {.007784695709041462, .3224671290700398,
                             2.445134137142996, 3.754408661907416};
  static const double b[] = {-54.47609879822406, 161.5858368580409,
                             -155.6989798598866, 66.80131188771972,
                             -13.28068155288572};
  if (p < .02425) {
    const double q = sqrt(-2 * log(p));
    return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q +
            c[5]) /
           ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
  }
  if (p > .97575) {
    const double q = sqrt(-2 * log(1 - p));
    return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q +
             c[5]) /
           ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
  }
  const double q = p - .5, r = q * q;
  return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) *
         q / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
}

static long double cdf(long double x) {
  return .5L * erfcl(-x * 0.707106781186547524400844362104849039L);
}

static void exact_geometric(uint32_t f, uint32_t m, long double q0_log,
                            long double s0, long double rate, long double sigma,
                            long double strike, long double *call,
                            long double *put, long double *call_delta,
                            long double *put_delta) {
  const long double dt = 1.0L / f;
  const long double mu = rate - .5L * sigma * sigma;
  const long double sum_t = dt * f * (f + 1.0L) * .5L;
  const long double sum_min = f * (f + 1.0L) * (2.0L * f + 1.0L) / 6.0L;
  const long double mean =
      q0_log / m + (long double)f / m * logl(s0) + mu * sum_t / m;
  const long double variance =
      sigma * sigma * dt * sum_min / ((long double)m * m);
  const long double discount = expl(-rate);
  const long double dmean_ds = (long double)f / (m * s0);
  if (variance == 0) {
    const long double g = expl(mean);
    *call = discount * fmaxl(g - strike, 0);
    *put = discount * fmaxl(strike - g, 0);
    *call_delta = g > strike ? discount * g * dmean_ds : 0;
    *put_delta = g < strike ? -discount * g * dmean_ds : 0;
    return;
  }
  const long double root = sqrtl(variance),
                    forward = expl(mean + .5L * variance);
  const long double d2 = (mean - logl(strike)) / root, d1 = d2 + root;
  *call = discount * (forward * cdf(d1) - strike * cdf(d2));
  *put = discount * (strike * cdf(-d2) - forward * cdf(-d1));
  *call_delta = discount * forward * cdf(d1) * dmean_ds;
  *put_delta = -discount * forward * cdf(-d1) * dmean_ds;
}

static int load_directions(fixture_t *f) {
  FILE *in = fopen("direction_numbers/joe_kuo_6_21201.bin", "rb");
  if (!in)
    return -1;
  for (uint32_t d = 0; d < MAX_FIXINGS; ++d) {
    uint32_t n;
    if (fread(&n, 4, 1, in) != 1 || n != 32 ||
        fread(f->directions[d], 4, 32, in) != 32) {
      fclose(in);
      return -1;
    }
  }
  fclose(in);
  return 0;
}

static void release_fixture(fixture_t *f) {
  free(f->producer);
  free(f->unseasoned);
  free(f->state);
  free(f->seasoned);
  free(f->qualified);
  free(f->maps);
  free(f->g);
  free(f->growth);
  free(f->x);
  free(f->words[1]);
  free(f->words[0]);
  memset(f, 0, sizeof *f);
}

static int prepare_fixture(fixture_t *f, uint32_t future, double rate,
                           double sigma) {
  memset(f, 0, sizeof *f);
  if (load_directions(f))
    return -1;
  f->words[0] = a64(16384);
  f->words[1] = a64(16384);
  f->x = a64(32768);
  f->growth = a64(32768);
  f->g = a64(16384);
  f->maps = a64(256 * sizeof *f->maps);
  f->qualified = a64(256 * sizeof *f->qualified);
  f->seasoned = a64(256 * sizeof *f->seasoned);
  f->state = a64(sizeof *f->state);
  f->unseasoned = a64(sizeof *f->unseasoned);
  f->producer = a64(sizeof *f->producer);
  if (!f->words[0] || !f->words[1] || !f->x || !f->growth || !f->g ||
      !f->maps || !f->qualified || !f->seasoned || !f->state ||
      !f->unseasoned || !f->producer)
    return -1;
  for (uint32_t i = 0; i < PATHS; ++i) {
    f->words[0][i] = sobol(8192 + i, f->directions[0]);
    f->words[1][i] = sobol(12288 + i, f->directions[0]);
  }
  const float drift = (float)((rate - .5 * sigma * sigma) / future);
  const float diffusion = (float)(sigma / sqrt((double)future));
  if (asian_genuine_seasoned_source_prepare(f->producer, drift, diffusion, 8192,
                                            future)) {
    fprintf(stderr, "producer prepare failed f=%u\n", future);
    return -1;
  }
  ordered_d1_x_only_diag(256, f->producer, f->x);
  asian_vector_exp_range_reduced_array_diag(f->x, f->growth);
  asian_vector_exp_range_reduced_array_diag(f->x + PATHS, f->growth + PATHS);
  const uint32_t *words[2] = {f->words[0], f->words[1]};
  const float *xs[2] = {f->x, f->x + PATHS},
              *gs[2] = {f->growth, f->growth + PATHS};
  uint32_t *target = a64(16384);
  if (!target)
    return -1;
  for (uint32_t k = 0; k < future; ++k) {
    for (uint32_t p = 0; p < PATHS; ++p)
      target[p] = sobol(8192 + p, f->directions[k]);
    if (asian_genuine_prepare_route(words, 2, xs, gs, target, k, future,
                                    &f->maps[k], &f->qualified[k])) {
      fprintf(stderr, "route prepare failed f=%u k=%u\n", future, k);
      free(target);
      return -1;
    }
  }
  free(target);
  return 0;
}

static void history(double out[256], uint32_t count, uint32_t kind) {
  static const double cycle[] = {82,   117.5,  94.25, 108,
                                 76.5, 123.75, 101,   89.5};
  for (uint32_t i = 0; i < count; ++i) {
    if (kind == 0)
      out[i] = 100;
    else if (kind == 1)
      out[i] = 80 + 40.0 * (i + 1) / (count + 1);
    else if (kind == 2)
      out[i] = 120 - 40.0 * (i + 1) / (count + 1);
    else
      out[i] = cycle[i & 7u];
  }
}

static int record_error(errors_t *e, long double got, long double ref,
                        uint32_t m, uint32_t c, uint32_t count, uint32_t strike,
                        uint32_t variant) {
  const long double error = got - ref, absolute = fabsl(error);
  e->sum += error;
  ++e->count;
  if (error > 1e-8L)
    ++e->positive;
  if (error < -1e-8L)
    ++e->negative;
  if (absolute > e->max)
    e->max = absolute;
  if (absolute > 1e-4L) {
    fprintf(stderr,
            "seasoned price gate M=%u c=%u count=%u strike=%u variant=%u "
            "got=%.12Lg ref=%.12Lg error=%.12Lg\n",
            m, c, count, strike, variant, got, ref, error);
    return -1;
  }
  return 0;
}

static int record_delta(errors_t *e, long double error, long double gate,
                        const char *name, uint32_t m, uint32_t c,
                        uint32_t count, uint32_t strike, uint32_t variant,
                        long double flip) {
  const long double absolute = fabsl(error);
  e->sum += error;
  ++e->count;
  if (error > 1e-8L)
    ++e->positive;
  if (error < -1e-8L)
    ++e->negative;
  if (absolute > e->max)
    e->max = absolute;
  if (absolute > gate) {
    fprintf(stderr,
            "seasoned %s gate M=%u c=%u count=%u strike=%u variant=%u "
            "error=%.12Lg flip=%.12Lg\n",
            name, m, c, count, strike, variant, error, flip);
    return -1;
  }
  return 0;
}

static int evaluate_history(fixture_t *f, uint32_t m, uint32_t c, uint32_t kind,
                            double rate, double sigma, errors_t *errors,
                            delta_errors_t *delta_errors) {
  const uint32_t future = m - c;
  double past[256];
  history(past, c, kind);
  const double reference_drift =
      (double)(float)((rate - .5 * sigma * sigma) / future);
  const double reference_diffusion =
      (double)(float)(sigma / sqrt((double)future));
  long double q0 = 0, logsum = 0;
  for (uint32_t i = 0; i < c; ++i) {
    q0 += past[i];
    logsum += logl(past[i]);
  }
  static const uint32_t counts[] = {1, 4, 8, 16, 32};
  for (uint32_t ci = 0; ci < 5; ++ci) {
    float strikes[32];
    _Alignas(64) asian_genuine_strip_context_t ctx, direct;
    _Alignas(64) asian_genuine_seasoned_summary_t summary;
    _Alignas(64) asian_genuine_strip_output_t ar4, ar8, cv4, cv8;
    _Alignas(64) asian_genuine_strip_output_t ad4, ad8, cd4, cd8;
    if (asian_genuine_strip_fixed_strikes(counts[ci], strikes) ||
        asian_genuine_seasoned_prepare(
            &ctx, f->seasoned, &summary, f->qualified, 100, rate, 0, sigma, 1,
            future, c, c ? past : NULL, strikes, counts[ci]))
      return -1;
    if (ci == 0) {
      memset(f->state, 0, sizeof *f->state);
      for (uint32_t p = 0; p < PATHS; ++p)
        f->state->s[p] = 100;
      asian_genuine_sql_dual_control_diag(f->seasoned, future, f->state);
      if (c == 0) {
        memset(f->unseasoned, 0, sizeof *f->unseasoned);
        for (uint32_t p = 0; p < PATHS; ++p)
          f->unseasoned->s[p] = 100;
        asian_genuine_sql_dual_control_diag(f->qualified, future,
                                            f->unseasoned);
        if (memcmp(f->state, f->unseasoned, sizeof *f->state) ||
            asian_genuine_strip_prepare(&direct, 100, rate, 0, sigma, 1, future,
                                        0, 0, 0, strikes, counts[ci]) ||
            memcmp(&ctx, &direct, sizeof ctx))
          return -1;
      }
    }
    if (asian_genuine_strip_exp_preflight(&ctx, f->state->l, NULL, NULL))
      return -1;
    asian_genuine_strip_l_to_g_diag(f->state->l, &ctx, f->g);
    if (asian_genuine_strip_price_diag(f->state->q, f->g, &ctx, 0, 4, &ar4) ||
        asian_genuine_strip_price_diag(f->state->q, f->g, &ctx, 0, 8, &ar8) ||
        asian_genuine_strip_price_diag(f->state->q, f->g, &ctx, 1, 4, &cv4) ||
        asian_genuine_strip_price_diag(f->state->q, f->g, &ctx, 1, 8, &cv8))
      return -1;
    if (memcmp(&ar4, &ar8, counts[ci] * sizeof ar4.values[0]) ||
        memcmp(&cv4, &cv8, counts[ci] * sizeof cv4.values[0]))
      return -1;
    if (!price_only) {
      if (asian_genuine_strip_price_delta_diag(f->state->q, f->g, &ctx, 0, 4,
                                               &ad4) ||
          asian_genuine_strip_price_delta_diag(f->state->q, f->g, &ctx, 0, 8,
                                               &ad8) ||
          asian_genuine_strip_price_delta_diag(f->state->q, f->g, &ctx, 1, 4,
                                               &cd4) ||
          asian_genuine_strip_price_delta_diag(f->state->q, f->g, &ctx, 1, 8,
                                               &cd8))
        return -1;
      for (uint32_t k = 0; k < counts[ci]; ++k) {
        if (ad4.values[k].call_price != ar4.values[k].call_price ||
            ad4.values[k].put_price != ar4.values[k].put_price ||
            cd4.values[k].call_price != cv4.values[k].call_price ||
            cd4.values[k].put_price != cv4.values[k].put_price ||
            memcmp(&ad4.values[k], &ad8.values[k], sizeof ad4.values[k]) ||
            memcmp(&cd4.values[k], &cd8.values[k], sizeof cd4.values[k]))
          return -1;
      }
    }
    long double ar_call[32] = {0}, ar_put[32] = {0}, cv_call[32] = {0},
                cv_put[32] = {0};
    long double da_call[32] = {0}, da_put[32] = {0}, dc_call[32] = {0},
                dc_put[32] = {0};
    long double sa_call[32] = {0}, sa_put[32] = {0}, sc_call[32] = {0},
                sc_put[32] = {0};
    long double ar_flip[32] = {0}, geometric_flip[32] = {0};
    for (uint32_t p = 0; p < PATHS; ++p) {
      double s = 100, q = 0, l = 0;
      for (uint32_t k = 0; k < future; ++k) {
        const double u =
            ((double)sobol(8192 + p, f->directions[k]) + .5) * 0x1p-32;
        const double x =
            reference_drift + reference_diffusion * inverse_normal(u);
        s *= exp(x);
        q += s;
        l += (double)(future - k) / m * x;
      }
      const long double a = (q0 + q) / m,
                        g = expl(logsum / m +
                                 (long double)future / m * logl(100) + l);
      const long double da = q / ((long double)m * 100),
                        dg = (long double)future / m * g / 100;
      const float af = (ctx.initial_q + f->state->q[p]) * ctx.inv_total,
                  gf = f->g[p];
      const float daf = f->state->q[p] * ctx.delta_q_scale,
                  dgf = gf * ctx.delta_g_scale;
      for (uint32_t k = 0; k < counts[ci]; ++k) {
        const long double strike = strikes[k];
        const long double ac = fmaxl(a - strike, 0), ap = fmaxl(strike - a, 0);
        const long double gc = fmaxl(g - strike, 0), gp = fmaxl(strike - g, 0);
        ar_call[k] += ac;
        ar_put[k] += ap;
        cv_call[k] += ac - gc;
        cv_put[k] += ap - gp;
        da_call[k] += a > strike ? da : 0;
        da_put[k] += a < strike ? -da : 0;
        dc_call[k] += (a > strike ? da : 0) - (g > strike ? dg : 0);
        dc_put[k] += (a < strike ? -da : 0) - (g < strike ? -dg : 0);
        sa_call[k] += (long double)af > strike ? daf : 0;
        sa_put[k] += (long double)af < strike ? -daf : 0;
        sc_call[k] += ((long double)af > strike ? daf : 0) -
                      ((long double)gf > strike ? dgf : 0);
        sc_put[k] += ((long double)af < strike ? -daf : 0) -
                     ((long double)gf < strike ? -dgf : 0);
        const asian_genuine_strip_strike_t *rr = &ctx.strikes[k];
        const int sign = rr->direct_sign > 0 ? 1 : -1;
        const int ia = sign * (a - strike) > 0,
                  ifa = sign * ((long double)af - strike) > 0;
        const int ig = sign * (g - strike) > 0,
                  ifg = sign * ((long double)gf - strike) > 0;
        if (ifa != ia)
          ar_flip[k] += (ifa - ia) * sign * da * expl(-rate);
        if (ifg != ig)
          geometric_flip[k] += (ifg - ig) * sign * dg * expl(-rate);
        if ((long double)af != a && strike >= fminl((long double)af, a) &&
            strike <= fmaxl((long double)af, a)) {
          ++delta_errors->arithmetic_ambiguous;
          if (kink_log)
            fprintf(kink_log,
                    "{\"state\":\"arithmetic\",\"M\":%u,\"c\":%u,\"f\":%u,"
                    "\"rate\":%.9g,\"sigma\":%.9g,\"history\":%u,\"path\":%u,"
                    "\"strike\":%.9g,\"float_value\":%.9g,\"reference_value\":%"
                    ".17Lg,\"indicator_flip_contribution\":%.17Lg}\n",
                    m, c, future, rate, sigma, kind, p, strikes[k], af, a,
                    (ifa - ia) * sign * da * expl(-rate) / PATHS);
        }
        if ((long double)gf != g && strike >= fminl((long double)gf, g) &&
            strike <= fmaxl((long double)gf, g)) {
          ++delta_errors->geometric_ambiguous;
          if (kink_log)
            fprintf(kink_log,
                    "{\"state\":\"geometric\",\"M\":%u,\"c\":%u,\"f\":%u,"
                    "\"rate\":%.9g,\"sigma\":%.9g,\"history\":%u,\"path\":%u,"
                    "\"strike\":%.9g,\"float_value\":%.9g,\"reference_value\":%"
                    ".17Lg,\"indicator_flip_contribution\":%.17Lg}\n",
                    m, c, future, rate, sigma, kind, p, strikes[k], gf, g,
                    (ifg - ig) * sign * dg * expl(-rate) / PATHS);
        }
      }
    }
    const long double discount = expl(-rate);
    for (uint32_t k = 0; k < counts[ci]; ++k) {
      long double gc, gp, gdc, gdp;
      exact_geometric(future, m, logsum, 100, rate, sigma, strikes[k], &gc, &gp,
                      &gdc, &gdp);
      const asian_genuine_strip_strike_t *record = &ctx.strikes[k];
      long double refs[4];
      if (record->direct_sign > 0) {
        refs[0] = discount * ar_call[k] / PATHS;
        refs[1] = refs[0] + record->put_price_adjust;
        refs[2] = discount * cv_call[k] / PATHS + gc;
        refs[3] = refs[2] + record->put_price_adjust;
      } else {
        refs[1] = discount * ar_put[k] / PATHS;
        refs[0] = refs[1] + record->call_price_adjust;
        refs[3] = discount * cv_put[k] / PATHS + gp;
        refs[2] = refs[3] + record->call_price_adjust;
      }
      const long double got[4] = {
          ar4.values[k].call_price, ar4.values[k].put_price,
          cv4.values[k].call_price, cv4.values[k].put_price};
      for (uint32_t v = 0; v < 4; ++v)
        if (record_error(errors, got[v], refs[v], m, c, counts[ci], k, v))
          return -1;
      const long double expected_parity =
          discount * ((ctx.expected_arithmetic - strikes[k]));
      if (fabsl((got[0] - got[1]) - expected_parity) > 2e-5L)
        return -1;
      if (!price_only) {
        long double refs_delta[4], same_delta[4];
        if (record->direct_sign > 0) {
          refs_delta[0] = discount * da_call[k] / PATHS;
          refs_delta[1] = refs_delta[0] + record->put_delta_adjust;
          refs_delta[2] = discount * dc_call[k] / PATHS + gdc;
          refs_delta[3] = refs_delta[2] + record->put_delta_adjust;
          same_delta[0] = sa_call[k] / PATHS;
          same_delta[1] = same_delta[0] + record->put_delta_adjust;
          same_delta[2] =
              sc_call[k] / PATHS + record->geometric_delta_exact_direct;
          same_delta[3] = same_delta[2] + record->put_delta_adjust;
        } else {
          refs_delta[1] = discount * da_put[k] / PATHS;
          refs_delta[0] = refs_delta[1] + record->call_delta_adjust;
          refs_delta[3] = discount * dc_put[k] / PATHS + gdp;
          refs_delta[2] = refs_delta[3] + record->call_delta_adjust;
          same_delta[1] = sa_put[k] / PATHS;
          same_delta[0] = same_delta[1] + record->call_delta_adjust;
          same_delta[3] =
              sc_put[k] / PATHS + record->geometric_delta_exact_direct;
          same_delta[2] = same_delta[3] + record->call_delta_adjust;
        }
        const long double got_delta[4] = {
            ad4.values[k].call_delta, ad4.values[k].put_delta,
            cd4.values[k].call_delta, cd4.values[k].put_delta};
        for (uint32_t v = 0; v < 4; ++v) {
          const long double flip =
              (v < 2 ? ar_flip[k] : ar_flip[k] - geometric_flip[k]) / PATHS;
          const long double unadjusted = got_delta[v] - refs_delta[v];
          const long double residual = unadjusted - flip;
          delta_errors->flip_sum += flip;
          if (record_delta(&delta_errors->unadjusted, unadjusted, LDBL_MAX,
                           "unadjusted", m, c, counts[ci], k, v, flip) ||
              record_delta(&delta_errors->residual, residual, 1e-4L, "smooth",
                           m, c, counts[ci], k, v, flip) ||
              record_delta(&delta_errors->same_state,
                           got_delta[v] - same_delta[v], 1e-6L, "same-state", m,
                           c, counts[ci], k, v, flip))
            return -1;
        }
      }
    }
  }
  return 0;
}

static int one_contract(uint32_t m, uint32_t c, double rate, double sigma,
                        uint32_t first_history, uint32_t history_count,
                        errors_t *errors, delta_errors_t *delta_errors) {
  fixture_t f;
  if (prepare_fixture(&f, m - c, rate, sigma)) {
    fprintf(stderr, "seasoned fixture failed M=%u c=%u\n", m, c);
    release_fixture(&f);
    return -1;
  }
  for (uint32_t h = 0; h < history_count; ++h) {
    const uint32_t kind = (first_history + h) & 3u;
    if (evaluate_history(&f, m, c, kind, rate, sigma, errors, delta_errors)) {
      fprintf(stderr, "seasoned evaluation failed M=%u c=%u history=%u\n", m, c,
              kind);
      release_fixture(&f);
      return -1;
    }
  }
  release_fixture(&f);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--price-only"))
    price_only = 1;
  else if (argc == 3 && !strcmp(argv[1], "--kinks")) {
    kink_log = fopen(argv[2], "wb");
    if (!kink_log)
      return 2;
  } else if (argc != 1)
    return 2;
  static const uint32_t totals[] = {16, 32, 64, 128, 256};
  errors_t errors = {0};
  delta_errors_t delta_errors = {0};
  for (uint32_t mi = 0; mi < 5; ++mi) {
    const uint32_t m = totals[mi];
    const uint32_t completed[] = {0, 1, m / 4, m / 2, m - 1};
    for (uint32_t ci = 0; ci < 5; ++ci) {
      fprintf(stderr, "seasoned begin M=%u c=%u\n", m, completed[ci]);
      if (one_contract(m, completed[ci], .03, .20, 0, 4, &errors,
                       &delta_errors))
        return 2;
    }
  }
  static const uint32_t smoke[][2] = {{0, 1},     {1, 1},   {1, 2},
                                      {2, 15},    {7, 26},  {63, 64},
                                      {128, 127}, {1, 255}, {255, 1}};
  for (uint32_t i = 0; i < sizeof smoke / sizeof smoke[0]; ++i) {
    const uint32_t c = smoke[i][0], f = smoke[i][1];
    fprintf(stderr, "seasoned smoke M=%u c=%u\n", c + f, c);
    if (one_contract(c + f, c, .03, .20, 3, 1, &errors, &delta_errors))
      return 2;
  }
  static const double rates[] = {-.02, 0, .03};
  static const double sigmas[] = {0, .20};
  for (uint32_t ri = 0; ri < 3; ++ri)
    for (uint32_t si = 0; si < 2; ++si) {
      fprintf(stderr, "seasoned market r=%.3g sigma=%.3g\n", rates[ri],
              sigmas[si]);
      if (one_contract(64, 16, rates[ri], sigmas[si], ri + si, 1, &errors,
                       &delta_errors))
        return 2;
    }
  const uint64_t nonzero = errors.positive + errors.negative;
  if (fabsl(errors.sum / errors.count) > 1e-6L ||
      (nonzero >= 20 && (errors.positive * 100 >= 95 * nonzero ||
                         errors.negative * 100 >= 95 * nonzero))) {
    fprintf(stderr,
            "seasoned systematic price bias mean=%.12Lg positive=%llu "
            "negative=%llu\n",
            errors.sum / errors.count, (unsigned long long)errors.positive,
            (unsigned long long)errors.negative);
    return 2;
  }
  if (price_only) {
    printf("asian_genuine_seasoned_strip stage2_price=PASS "
           "max_abs_price_error=%.12Lg signed_mean_price_error=%.12Lg "
           "positive=%llu negative=%llu principal_contracts=25 histories=4 "
           "tile_bits=PASS c0_bits=PASS q_future_only=PASS\n",
           errors.max, errors.sum / errors.count,
           (unsigned long long)errors.positive,
           (unsigned long long)errors.negative);
    return 0;
  }
  const uint64_t smooth_nonzero =
      delta_errors.residual.positive + delta_errors.residual.negative;
  if (fabsl(delta_errors.residual.sum / delta_errors.residual.count) > 1e-6L ||
      fabsl(delta_errors.same_state.sum / delta_errors.same_state.count) >
          1e-7L ||
      (smooth_nonzero >= 20 &&
       (delta_errors.residual.positive * 100 >= 95 * smooth_nonzero ||
        delta_errors.residual.negative * 100 >= 95 * smooth_nonzero))) {
    fprintf(stderr,
            "seasoned systematic delta bias smooth_mean=%.12Lg "
            "same_mean=%.12Lg positive=%llu negative=%llu\n",
            delta_errors.residual.sum / delta_errors.residual.count,
            delta_errors.same_state.sum / delta_errors.same_state.count,
            (unsigned long long)delta_errors.residual.positive,
            (unsigned long long)delta_errors.residual.negative);
    return 2;
  }
  printf(
      "asian_genuine_seasoned_strip stage3_delta=PASS "
      "max_abs_price_error=%.12Lg max_abs_unadjusted_delta_difference=%.12Lg "
      "max_abs_kink_adjusted_residual=%.12Lg "
      "max_abs_same_Q_G_delta_error=%.12Lg "
      "signed_mean_smooth_delta_error=%.12Lg arithmetic_ambiguous=%llu "
      "geometric_ambiguous=%llu price_bits_stage2=PASS tile_bits=PASS\n",
      errors.max, delta_errors.unadjusted.max, delta_errors.residual.max,
      delta_errors.same_state.max,
      delta_errors.residual.sum / delta_errors.residual.count,
      (unsigned long long)delta_errors.arithmetic_ambiguous,
      (unsigned long long)delta_errors.geometric_ambiguous);
  if (kink_log)
    fclose(kink_log);
  return 0;
}
