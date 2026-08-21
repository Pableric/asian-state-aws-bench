#define _GNU_SOURCE
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_genuine_seasoned_strip_diag.h"
#include "private/asian_geometric_cv_diag.h"

#include <inttypes.h>
#include <math.h>
#include <mkl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <x86intrin.h>

enum {
  PATHS = 4096,
  SAMPLES = 51,
  WARMUPS = 16,
  CANDIDATES = 24,
  CONTRACTS = 25
};
static const uint32_t totals[5] = {16, 32, 64, 128, 256};
typedef struct {
  uint32_t m, c, f, directions[256][32], *words[2], *pressure;
  float *x, *growth, *pm, *weights, *g, strikes[32];
  fragment_map_t *maps;
  asian_genuine_route_t *base_routes, *seasoned_routes;
  asian_genuine_state_t *state, *initial;
  ordered_d1_diag_context_t *producer;
  asian_genuine_strip_context_t *unseasoned, *seasoned;
  asian_genuine_seasoned_summary_t *summary;
  asian_genuine_strip_output_t *output;
  VSLStreamStatePtr base_stream, work_stream;
  double past[256];
  float drift, diffusion;
} fixture_t;
static fixture_t b;
static volatile double sink;
static int mkl_status;

static void *a64(size_t n) {
  void *p = 0;
  if (posix_memalign(&p, 64, n))
    return 0;
  memset(p, 0, n);
  return p;
}
static uint32_t sobol(uint32_t index, const uint32_t *d) {
  uint32_t g = index ^ (index >> 1), w = 0;
  for (uint32_t bit = 0; g; ++bit, g >>= 1)
    if (g & 1)
      w ^= d[bit];
  return w;
}
static uint64_t tsc0(void) {
  _mm_lfence();
  return __rdtsc();
}
static uint64_t tsc1(void) {
  unsigned aux;
  uint64_t t = __rdtscp(&aux);
  _mm_lfence();
  return t;
}
static uint64_t wallns(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &t))
    abort();
  return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}
static int ucmp(const void *a, const void *d) {
  uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)d;
  return (x > y) - (x < y);
}
static int dcmp(const void *a, const void *d) {
  double x = *(const double *)a, y = *(const double *)d;
  return (x > y) - (x < y);
}
static uint64_t quantile(const uint64_t *x, uint32_t rank) {
  uint64_t q[SAMPLES];
  memcpy(q, x, sizeof q);
  qsort(q, SAMPLES, 8, ucmp);
  return q[rank];
}
static double dquantile(const double *x, uint32_t rank) {
  double q[SAMPLES];
  memcpy(q, x, sizeof q);
  qsort(q, SAMPLES, 8, dcmp);
  return q[rank];
}
static uint64_t rng(uint64_t *x) {
  *x ^= *x << 13;
  *x ^= *x >> 7;
  return *x ^= *x << 17;
}
static void shuffle(uint32_t *x, uint64_t *seed) {
  for (uint32_t i = 0; i < CANDIDATES; ++i)
    x[i] = i;
  for (uint32_t i = CANDIDATES - 1; i; --i) {
    uint32_t j = rng(seed) % (i + 1), v = x[i];
    x[i] = x[j];
    x[j] = v;
  }
}
static void decode(uint32_t id, uint32_t *provider, uint32_t *estimator,
                   uint32_t *delta, uint32_t *tile) {
  *provider = id / 8;
  id %= 8;
  *estimator = id / 4;
  id %= 4;
  *delta = id / 2;
  *tile = id & 1 ? 8 : 4;
}
static const char *provider_name(uint32_t p) {
  return p == 0   ? "matched_f_unseasoned"
         : p == 1 ? "seasoned_ours"
                  : "seasoned_onemkl";
}

static int load_directions(void) {
  FILE *in = fopen("direction_numbers/joe_kuo_6_21201.bin", "rb");
  if (!in)
    return -1;
  for (uint32_t d = 0; d < 256; ++d) {
    uint32_t n;
    if (fread(&n, 4, 1, in) != 1 || n != 32 ||
        fread(b.directions[d], 4, 32, in) != 32) {
      fclose(in);
      return -1;
    }
  }
  fclose(in);
  return 0;
}
static void produce(void) {
  ordered_d1_x_only_diag(256, b.producer, b.x);
  asian_vector_exp_range_reduced_array_diag(b.x, b.growth);
  asian_vector_exp_range_reduced_array_diag(b.x + PATHS, b.growth + PATHS);
}
static void contract(uint32_t id, uint32_t *m, uint32_t *c) {
  *m = totals[id / 5];
  switch (id % 5) {
  case 0:
    *c = 0;
    break;
  case 1:
    *c = 1;
    break;
  case 2:
    *c = *m / 4;
    break;
  case 3:
    *c = *m / 2;
    break;
  default:
    *c = *m - 1;
  }
}
static void history(void) {
  static const double cycle[] = {82,   117.5,  94.25, 108,
                                 76.5, 123.75, 101,   89.5};
  for (uint32_t i = 0; i < b.c; ++i)
    b.past[i] = cycle[i & 7u];
}

static int prepare_mkl(void) {
  size_t n = 3 + (size_t)b.f * 32;
  MKL_UINT *p = calloc(n, sizeof *p);
  if (!p)
    return -1;
  p[0] = b.f;
  p[1] = VSL_USER_QRNG_INITIAL_VALUES;
  p[2] = VSL_USER_DIRECTION_NUMBERS;
  for (uint32_t d = 0; d < b.f; ++d)
    for (uint32_t k = 0; k < 32; ++k)
      p[3 + d * 32 + k] = b.directions[d][k];
  int e = vslNewStreamEx(&b.base_stream, VSL_BRNG_SOBOL, n, p);
  free(p);
  if (e || vslSkipAheadStream(b.base_stream, (long long)(8192 - 1) * b.f) ||
      vslCopyStream(&b.work_stream, b.base_stream))
    return -1;
  return 0;
}

static void release_fixture(void) {
  if (b.work_stream)
    vslDeleteStream(&b.work_stream);
  if (b.base_stream)
    vslDeleteStream(&b.base_stream);
  free(b.output);
  free(b.summary);
  free(b.seasoned);
  free(b.unseasoned);
  free(b.producer);
  free(b.initial);
  free(b.state);
  free(b.seasoned_routes);
  free(b.base_routes);
  free(b.maps);
  free(b.g);
  free(b.weights);
  free(b.pm);
  free(b.growth);
  free(b.x);
  free(b.pressure);
  free(b.words[1]);
  free(b.words[0]);
  memset(&b, 0, sizeof b);
}

static int prepare_fixture(uint32_t id) {
  memset(&b, 0, sizeof b);
  contract(id, &b.m, &b.c);
  b.f = b.m - b.c;
  history();
  if (load_directions())
    return -1;
  b.drift = (float)((.03 - .5 * .20 * .20) / b.f);
  b.diffusion = (float)(.20 / sqrt((double)b.f));
  b.words[0] = a64(16384);
  b.words[1] = a64(16384);
  b.x = a64(32768);
  b.growth = a64(32768);
  b.pm = a64((size_t)b.f * 16384);
  b.weights = a64(b.f * 4);
  b.g = a64(16384);
  b.maps = a64(256 * sizeof *b.maps);
  b.base_routes = a64(256 * sizeof *b.base_routes);
  b.seasoned_routes = a64(256 * sizeof *b.seasoned_routes);
  b.state = a64(sizeof *b.state);
  b.initial = a64(sizeof *b.initial);
  b.producer = a64(sizeof *b.producer);
  b.unseasoned = a64(sizeof *b.unseasoned);
  b.seasoned = a64(sizeof *b.seasoned);
  b.summary = a64(sizeof *b.summary);
  b.output = a64(sizeof *b.output);
  b.pressure = a64(32768);
  if (!b.words[0] || !b.words[1] || !b.x || !b.growth || !b.pm || !b.weights ||
      !b.g || !b.maps || !b.base_routes || !b.seasoned_routes || !b.state ||
      !b.initial || !b.producer || !b.unseasoned || !b.seasoned || !b.summary ||
      !b.output || !b.pressure)
    return -1;
  for (uint32_t i = 0; i < 8192; ++i)
    b.pressure[i] = i;
  for (uint32_t i = 0; i < PATHS; ++i) {
    b.words[0][i] = sobol(8192 + i, b.directions[0]);
    b.words[1][i] = sobol(12288 + i, b.directions[0]);
    b.initial->s[i] = 100;
  }
  if (asian_genuine_seasoned_source_prepare(b.producer, b.drift, b.diffusion,
                                            8192, b.f))
    return -1;
  produce();
  const uint32_t *sw[2] = {b.words[0], b.words[1]};
  const float *xb[2] = {b.x, b.x + PATHS},
              *gb[2] = {b.growth, b.growth + PATHS};
  uint32_t *target = a64(16384);
  if (!target)
    return -1;
  for (uint32_t k = 0; k < b.f; ++k) {
    for (uint32_t i = 0; i < PATHS; ++i)
      target[i] = sobol(8192 + i, b.directions[k]);
    if (asian_genuine_prepare_route(sw, 2, xb, gb, target, k, b.f, &b.maps[k],
                                    &b.base_routes[k])) {
      free(target);
      return -1;
    }
  }
  free(target);
  if (asian_genuine_strip_fixed_strikes(32, b.strikes) ||
      asian_genuine_strip_prepare(b.unseasoned, 100, .03, 0, .20, 1, b.f, 0, 0,
                                  0, b.strikes, 32) ||
      asian_genuine_seasoned_prepare(b.seasoned, b.seasoned_routes, b.summary,
                                     b.base_routes, 100, .03, 0, .20, 1, b.f,
                                     b.c, b.c ? b.past : 0, b.strikes, 32))
    return -1;
  for (uint32_t k = 0; k < b.f; ++k)
    memcpy(&b.weights[k], &b.seasoned_routes[k].weight_bits, 4);
  if (b.c == 0 &&
      (memcmp(b.unseasoned, b.seasoned, sizeof *b.unseasoned) ||
       memcmp(b.base_routes, b.seasoned_routes, 256 * sizeof *b.base_routes)))
    return -1;
  return prepare_mkl();
}

static void reset(uint32_t candidate) {
  uint32_t p, e, d, t;
  decode(candidate, &p, &e, &d, &t);
  (void)e;
  (void)d;
  (void)t;
  memcpy(b.state, b.initial, sizeof *b.state);
  memset(b.output, 0, sizeof *b.output);
  memset(b.x, 0, 32768);
  memset(b.growth, 0, 32768);
  memset(b.g, 0, 16384);
  if (p == 2) {
    if (vslCopyStreamState(b.work_stream, b.base_stream))
      abort();
    memset(b.pm, 0, (size_t)b.f * 16384);
  }
  mkl_status = 0;
}
static double run(uint32_t candidate) {
  uint32_t provider, estimator, delta, tile;
  decode(candidate, &provider, &estimator, &delta, &tile);
  const asian_genuine_strip_context_t *ctx =
      provider ? b.seasoned : b.unseasoned;
  if (provider == 2) {
    mkl_status = vsRngGaussian(VSL_RNG_METHOD_GAUSSIAN_ICDF, b.work_stream,
                               b.f * PATHS, b.pm, b.drift, b.diffusion);
    asian_intel_point_major_sql_diag(b.pm, b.f, b.weights, b.state);
  } else {
    produce();
    asian_genuine_sql_dual_control_diag(
        provider ? b.seasoned_routes : b.base_routes, b.f, b.state);
  }
  asian_genuine_strip_l_to_g_diag(b.state->l, ctx, b.g);
  if (delta)
    asian_genuine_strip_price_delta_diag(b.state->q, b.g, ctx, estimator, tile,
                                         b.output);
  else
    asian_genuine_strip_price_diag(b.state->q, b.g, ctx, estimator, tile,
                                   b.output);
  double z = 0;
  for (uint32_t k = 0; k < 32; ++k) {
    z += b.output->values[k].call_price + b.output->values[k].put_price;
    if (delta)
      z += b.output->values[k].call_delta + b.output->values[k].put_delta;
  }
  return z;
}
static void condition(uint32_t candidate, uint32_t mode) {
  uint64_t z = 1;
  if (mode)
    for (uint32_t i = 0; i < 8192; ++i) {
      b.pressure[i] += i + 3;
      z += b.pressure[i];
    }
  else {
    uint32_t provider, estimator, delta, tile;
    decode(candidate, &provider, &estimator, &delta, &tile);
    (void)estimator;
    (void)delta;
    (void)tile;
    const unsigned char *parts[] = {
        (unsigned char *)b.x,
        (unsigned char *)b.growth,
        (unsigned char *)(provider ? b.seasoned_routes : b.base_routes),
        (unsigned char *)b.maps,
        (unsigned char *)b.state,
        (unsigned char *)b.producer,
        (unsigned char *)(provider ? b.seasoned : b.unseasoned),
        (unsigned char *)b.pm};
    const size_t sizes[] = {32768,
                            32768,
                            256 * sizeof *b.seasoned_routes,
                            256 * sizeof *b.maps,
                            sizeof *b.state,
                            sizeof *b.producer,
                            sizeof *b.seasoned,
                            (size_t)b.f * 16384};
    const uint32_t part_count = provider == 2 ? 8 : 7;
    for (uint32_t q = 0; q < part_count; ++q)
      for (size_t i = 0; i < sizes[q]; i += 64)
        z += parts[q][i];
  }
  sink += z + candidate;
}
static void checksum(double z) {
  for (uint32_t i = 0; i < PATHS; ++i)
    z += b.state->s[i] + b.state->q[i] + b.state->l[i] + b.g[i];
  sink += z;
}

static int preflight_fixture(void) {
  asian_genuine_strip_output_t saved[24];
  for (uint32_t c = 0; c < CANDIDATES; ++c) {
    reset(c);
    double z = run(c);
    if (mkl_status || !isfinite(z))
      return -1;
    saved[c] = *b.output;
  }
  for (uint32_t p = 0; p < 3; ++p)
    for (uint32_t e = 0; e < 2; ++e)
      for (uint32_t d = 0; d < 2; ++d) {
        uint32_t a = p * 8 + e * 4 + d * 2, b8 = a + 1;
        if (memcmp(&saved[a], &saved[b8], sizeof saved[a]))
          return -1;
        if (d)
          for (uint32_t k = 0; k < 32; ++k)
            if (saved[a].values[k].call_price !=
                    saved[a - 2].values[k].call_price ||
                saved[a].values[k].put_price !=
                    saved[a - 2].values[k].put_price)
              return -1;
      }
  if (b.c == 0)
    for (uint32_t i = 0; i < 8; ++i)
      if (memcmp(&saved[i], &saved[8 + i], sizeof saved[i]))
        return -1;
  return 0;
}
static int pin_cpu(void) {
  cpu_set_t a, p;
  CPU_ZERO(&a);
  if (sched_getaffinity(0, sizeof a, &a))
    return -1;
  int cpu = 0;
  while (cpu < CPU_SETSIZE && !CPU_ISSET(cpu, &a))
    ++cpu;
  if (cpu == CPU_SETSIZE)
    return -1;
  CPU_ZERO(&p);
  CPU_SET(cpu, &p);
  return sched_setaffinity(0, sizeof p, &p) ? -1 : cpu;
}

int main(int argc, char **argv) {
  const char *out = "results/asian_genuine_seasoned_price_delta_strip/aws.json";
  int check = 0;
  for (int i = 1; i < argc; ++i)
    if (!strcmp(argv[i], "--check-only"))
      check = 1;
    else if (!strcmp(argv[i], "--json") && ++i < argc)
      out = argv[i];
    else
      return 2;
  const int cpu = pin_cpu();
  if (cpu < 0)
    return 2;
  if (check) {
    for (uint32_t id = 0; id < CONTRACTS; ++id) {
      if (prepare_fixture(id) || preflight_fixture())
        return 2;
      release_fixture();
    }
    puts("asian_genuine_seasoned_price_delta_strip native_preflight=PASS "
         "local_correctness_hash_prerequisite=qualification.json "
         "status=AWS_PERFORMANCE_PENDING");
    return 0;
  }
  FILE *j = fopen(out, "wb");
  if (!j)
    return 2;
  fprintf(j,
          "{\"schema\":\"asian-genuine-seasoned-strip-aws-v1\",\"cpu\":%d,"
          "\"paths\":4096,\"warmups\":16,\"samples\":51,\"results\":[",
          cpu);
  uint64_t seed = UINT64_C(0x534541534f4e4544);
  int comma = 0, derived_comma = 0, all_pass = 1;
  FILE *derived = tmpfile();
  if (!derived)
    return 2;
  for (uint32_t id = 0; id < CONTRACTS; ++id) {
    uint64_t setupw = wallns(), setupt = tsc0();
    if (prepare_fixture(id) || preflight_fixture())
      return 2;
    uint64_t setupt1 = tsc1(), setupw1 = wallns();
    for (uint32_t mode = 0; mode < 2; ++mode) {
      uint64_t ticks[CANDIDATES][SAMPLES] = {{0}},
               wall[CANDIDATES][SAMPLES] = {{0}};
      for (uint32_t w = 0; w < WARMUPS; ++w)
        for (uint32_t c = 0; c < CANDIDATES; ++c) {
          reset(c);
          condition(c, mode);
          checksum(run(c));
          if (mkl_status)
            return 2;
        }
      for (uint32_t sample = 0; sample < SAMPLES; ++sample) {
        uint32_t order[CANDIDATES];
        shuffle(order, &seed);
        for (uint32_t o = 0; o < CANDIDATES; ++o) {
          uint32_t c = order[o];
          reset(c);
          condition(c, mode);
          uint64_t w0 = wallns(), t0 = tsc0();
          double z = run(c);
          uint64_t t1 = tsc1(), w1 = wallns();
          ticks[c][sample] = t1 - t0;
          wall[c][sample] = w1 - w0;
          checksum(z);
          if (mkl_status)
            return 2;
        }
      }
      for (uint32_t c = 0; c < CANDIDATES; ++c) {
        uint32_t p, e, d, t;
        decode(c, &p, &e, &d, &t);
        const uint64_t tm = quantile(ticks[c], 25), wm = quantile(wall[c], 25);
        fprintf(j,
                "%s{\"M\":%u,\"c\":%u,\"f\":%u,\"provider\":\"%s\","
                "\"estimator\":\"%s\",\"output\":\"%s\",\"tile\":%u,\"mode\":"
                "\"%s\",\"setup_ticks\":%" PRIu64 ",\"setup_wall_ns\":%" PRIu64
                ",\"tsc_p10\":%" PRIu64 ",\"tsc_median\":%" PRIu64
                ",\"tsc_p90\":%" PRIu64 ",\"wall_p10_ns\":%" PRIu64
                ",\"wall_median_ns\":%" PRIu64 ",\"wall_p90_ns\":%" PRIu64
                ",\"outputs_per_second\":%.9g,\"raw_tsc\":[",
                comma++ ? "," : "", b.m, b.c, b.f, provider_name(p),
                e ? "geometric_cv" : "arithmetic", d ? "price_delta" : "price",
                t, mode ? "historical_32KiB_rmw" : "candidate_specific_warm",
                setupt1 - setupt, setupw1 - setupw, quantile(ticks[c], 5), tm,
                quantile(ticks[c], 45), quantile(wall[c], 5), wm,
                quantile(wall[c], 45), 32e9 / wm);
        for (uint32_t s = 0; s < SAMPLES; ++s)
          fprintf(j, "%s%" PRIu64, s ? "," : "", ticks[c][s]);
        fputs("],\"raw_wall_ns\":[", j);
        for (uint32_t s = 0; s < SAMPLES; ++s)
          fprintf(j, "%s%" PRIu64, s ? "," : "", wall[c][s]);
        fputs("]}", j);
      }
      for (uint32_t e = 0; e < 2; ++e)
        for (uint32_t d = 0; d < 2; ++d)
          for (uint32_t ti = 0; ti < 2; ++ti) {
            uint32_t u = e * 4 + d * 2 + ti, s = 8 + u, mkl = 16 + u;
            double rt[SAMPLES], rw[SAMPLES];
            for (uint32_t q = 0; q < SAMPLES; ++q) {
              rt[q] = (double)ticks[s][q] / ticks[u][q];
              rw[q] = (double)wall[s][q] / wall[u][q];
            }
            const double tmed = dquantile(rt, 25), tupper = dquantile(rt, 32),
                         wmed = dquantile(rw, 25), wupper = dquantile(rw, 32);
            const int pass = tmed <= 1.01 && tupper <= 1.02 && wmed <= 1.01 &&
                             wupper <= 1.02;
            if (!pass)
              all_pass = 0;
            fprintf(
                derived,
                "%s{\"M\":%u,\"c\":%u,\"f\":%u,\"mode\":\"%s\",\"estimator\":"
                "\"%s\",\"output\":\"%s\",\"tile\":%u,\"seasoned_over_"
                "unseasoned_tsc_median\":%.9g,\"seasoned_over_unseasoned_tsc_"
                "ci95_upper\":%.9g,\"seasoned_over_unseasoned_wall_median\":%."
                "9g,\"seasoned_over_unseasoned_wall_ci95_upper\":%.9g,\"onemkl_"
                "over_ours_tsc_median\":%.9g,\"gate\":\"%s\"}",
                derived_comma++ ? "," : "", b.m, b.c, b.f,
                mode ? "historical_32KiB_rmw" : "candidate_specific_warm",
                e ? "geometric_cv" : "arithmetic", d ? "price_delta" : "price",
                ti ? 8 : 4, tmed, tupper, wmed, wupper,
                (double)quantile(ticks[mkl], 25) / quantile(ticks[s], 25),
                pass ? "PASS" : "FAIL");
          }
    }
    release_fixture();
  }
  fputs("],\"paired_gates\":[", j);
  rewind(derived);
  char buf[4096];
  size_t got;
  while ((got = fread(buf, 1, sizeof buf, derived)))
    fwrite(buf, 1, got, j);
  fclose(derived);
  fprintf(j,
          "],\"status\":\"%s\",\"working_sets\":{\"Q\":16384,\"L\":16384,"
          "\"G\":16384,\"routes_each\":8192,\"source_x_growth\":65536,"
          "\"completed_values_max\":2048}}\n",
          all_pass ? "SEASONED_STRIP_QUALIFIED"
                   : "AWS_PERFORMANCE_GATE_FAILED");
  fclose(j);
  return all_pass ? 0 : 2;
}
