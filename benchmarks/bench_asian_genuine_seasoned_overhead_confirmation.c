#define _GNU_SOURCE
#include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "private/asian_genuine_seasoned_strip_diag.h"
#include "private/asian_geometric_cv_diag.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

enum {
  PATHS = 4096,
  CASES = 7,
  ESTIMATORS = 2,
  CACHE_MODES = 2,
  WARMUP_QUARTETS = 16,
  MEASURED_QUARTETS = 201,
  QUARTET_INVOCATIONS = 4
};

static const uint32_t case_m[CASES] = {16, 128, 256, 256, 256, 256, 256};
static const uint32_t case_c[CASES] = {0, 32, 0, 1, 64, 128, 255};
static const uint64_t quartet_seed = UINT64_C(0x534541534f4e4142);

typedef struct {
  uint32_t m, c, f, directions[256][32], *words[2], *pressure;
  float *x, *growth, *g, strikes[32];
  fragment_map_t *maps;
  asian_genuine_route_t *base_routes, *seasoned_routes, *active_routes;
  asian_genuine_state_t *state, *initial;
  ordered_d1_diag_context_t *producer;
  asian_genuine_strip_context_t *unseasoned, *seasoned, *active_context;
  asian_genuine_seasoned_summary_t *summary;
  asian_genuine_strip_output_t *output;
  double past[256];
  float drift, diffusion;
} fixture_t;

typedef struct {
  uint32_t state[8];
  uint64_t bits;
  unsigned char block[64];
} sha256_t;

static fixture_t fixture;
static volatile uint64_t checksum_sink;

static void *aligned_zero(size_t bytes) {
  void *p = NULL;
  if (posix_memalign(&p, 64, bytes))
    return NULL;
  memset(p, 0, bytes);
  return p;
}

static uint32_t sobol_word(uint32_t index, const uint32_t *directions) {
  uint32_t gray = index ^ (index >> 1), word = 0;
  for (uint32_t bit = 0; gray; ++bit, gray >>= 1)
    if (gray & 1u)
      word ^= directions[bit];
  return word;
}

static uint64_t tsc_begin(void) {
  _mm_lfence();
  return __rdtsc();
}

static uint64_t tsc_end(void) {
  unsigned aux;
  const uint64_t value = __rdtscp(&aux);
  _mm_lfence();
  return value;
}

static uint64_t wall_ns(void) {
  struct timespec value;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &value))
    abort();
  return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
         (uint64_t)value.tv_nsec;
}

static uint64_t next_random(uint64_t *state) {
  *state ^= *state << 13;
  *state ^= *state >> 7;
  return *state ^= *state << 17;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t bytes) {
  const unsigned char *p = data;
  for (size_t i = 0; i < bytes; ++i) {
    hash ^= p[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint32_t rotate_right(uint32_t x, unsigned n) {
  return (x >> n) | (x << (32 - n));
}

static void sha256_transform(sha256_t *ctx, const unsigned char block[64]) {
  static const uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
      0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
      0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
      0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
      0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
      0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
      0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
      0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
      0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
      0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  uint32_t w[64];
  for (unsigned i = 0; i < 16; ++i)
    w[i] = (uint32_t)block[4 * i] << 24 |
           (uint32_t)block[4 * i + 1] << 16 |
           (uint32_t)block[4 * i + 2] << 8 | block[4 * i + 3];
  for (unsigned i = 16; i < 64; ++i) {
    const uint32_t a = w[i - 15], b = w[i - 2];
    const uint32_t s0 = rotate_right(a, 7) ^ rotate_right(a, 18) ^ (a >> 3);
    const uint32_t s1 = rotate_right(b, 17) ^ rotate_right(b, 19) ^ (b >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2],
           d = ctx->state[3], e = ctx->state[4], f = ctx->state[5],
           g = ctx->state[6], h = ctx->state[7];
  for (unsigned i = 0; i < 64; ++i) {
    const uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                        rotate_right(e, 25);
    const uint32_t choose = (e & f) ^ (~e & g);
    const uint32_t t1 = h + s1 + choose + k[i] + w[i];
    const uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                        rotate_right(a, 22);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

static void sha256_init(sha256_t *ctx) {
  static const uint32_t initial[8] = {0x6a09e667u, 0xbb67ae85u,
                                      0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu,
                                      0x1f83d9abu, 0x5be0cd19u};
  memcpy(ctx->state, initial, sizeof initial);
  ctx->bits = 0;
}

static void sha256_update(sha256_t *ctx, const void *data, size_t bytes) {
  const unsigned char *p = data;
  size_t used = (size_t)((ctx->bits >> 3) & 63u);
  ctx->bits += (uint64_t)bytes << 3;
  while (bytes) {
    size_t take = 64 - used;
    if (take > bytes)
      take = bytes;
    memcpy(ctx->block + used, p, take);
    used += take;
    p += take;
    bytes -= take;
    if (used == 64) {
      sha256_transform(ctx, ctx->block);
      used = 0;
    }
  }
}

static void sha256_final(sha256_t *ctx, unsigned char digest[32]) {
  const uint64_t bits = ctx->bits;
  const unsigned char marker = 0x80;
  const unsigned char zero = 0;
  sha256_update(ctx, &marker, 1);
  while (((ctx->bits >> 3) & 63u) != 56)
    sha256_update(ctx, &zero, 1);
  unsigned char length[8];
  for (unsigned i = 0; i < 8; ++i)
    length[7 - i] = (unsigned char)(bits >> (8 * i));
  sha256_update(ctx, length, sizeof length);
  for (unsigned i = 0; i < 8; ++i) {
    digest[4 * i] = (unsigned char)(ctx->state[i] >> 24);
    digest[4 * i + 1] = (unsigned char)(ctx->state[i] >> 16);
    digest[4 * i + 2] = (unsigned char)(ctx->state[i] >> 8);
    digest[4 * i + 3] = (unsigned char)ctx->state[i];
  }
}

static int file_sha256(const char *path, char hex[65]) {
  FILE *in = fopen(path, "rb");
  if (!in)
    return -1;
  sha256_t ctx;
  sha256_init(&ctx);
  unsigned char buffer[16384], digest[32];
  size_t got;
  while ((got = fread(buffer, 1, sizeof buffer, in)) != 0)
    sha256_update(&ctx, buffer, got);
  const int failed = ferror(in);
  fclose(in);
  if (failed)
    return -1;
  sha256_final(&ctx, digest);
  for (unsigned i = 0; i < 32; ++i)
    snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  hex[64] = 0;
  return 0;
}

static int executable_sha256(char hex[65]) {
  char path[4096];
  const ssize_t length = readlink("/proc/self/exe", path, sizeof path - 1);
  if (length <= 0 || (size_t)length >= sizeof path - 1)
    return -1;
  path[length] = 0;
  return file_sha256(path, hex);
}

static int read_line(const char *path, char *out, size_t capacity) {
  FILE *in = fopen(path, "rb");
  if (!in)
    return -1;
  if (!fgets(out, (int)capacity, in)) {
    fclose(in);
    return -1;
  }
  fclose(in);
  out[strcspn(out, "\r\n")] = 0;
  return 0;
}

static int file_contains(const char *path, const char *needle) {
  FILE *in = fopen(path, "rb");
  if (!in)
    return 0;
  char buffer[4096];
  size_t used = 0, got;
  int found = 0;
  while ((got = fread(buffer + used, 1, sizeof buffer - 1 - used, in)) != 0) {
    used += got;
    buffer[used] = 0;
    if (strstr(buffer, needle)) {
      found = 1;
      break;
    }
    if (used > strlen(needle)) {
      const size_t keep = strlen(needle) - 1;
      memmove(buffer, buffer + used - keep, keep);
      used = keep;
    }
  }
  fclose(in);
  return found;
}

static int committed_prerequisites(void) {
  return file_contains(
             "results/asian_genuine_seasoned_price_delta_strip/qualification.json",
             "SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING") &&
         file_contains(
             "results/asian_genuine_seasoned_price_delta_strip/structural_audit.json",
             "\"dynamic_route_trace_exact\": true") &&
         file_contains(
             "results/asian_genuine_seasoned_price_delta_strip/structural_audit.json",
             "\"status\": \"PASS\"");
}

static int frozen_files_unchanged(void) {
  static const struct {
    const char *path;
    const char *sha256;
  } files[] = {
      {"asian_genuine_price_delta_strip_avx512.s",
       "42c52432e1c0b49956d5db11305e9b7d6a8f8c86d35c5fa819ea96443f03893e"},
      {"asian_genuine_price_delta_strip_setup.c",
       "907c4a05807de27d8ec3c226382e6b640048a87fcd31f9b97d82c5c37668dced"},
      {"asian_genuine_sql_variable_avx512.s",
       "08f685f29e86a1480269797ff4ee4313088f33c3275af727faa72c12d1957709"},
      {"asian_genuine_permute_setup.c",
       "e2e54ebca65f6586cbb2fbfa0621e20a63dbaafec3cbbdb2515f5a9a432a0301"},
      {"asian_geometric_cv_payoff_avx512.s",
       "78331e0b544b983d3b32d3fabc33745e858463aebca53928d7b6843f60802ee6"},
      {"private/asian_genuine_seasoned_strip_setup.c",
       "281b5842e4f43d41b0917e49c25559bda4eafc636d746bd3d62292ff065c8656"},
      {"private/asian_genuine_seasoned_strip_diag.h",
       "1a0a884d3db55d485997db63b033126c4d1a3efade533cbda577e7a0a043a554"},
      {"private/asian_genuine_price_delta_strip_diag.h",
       "e187cb32872c6240cafa808d6b8b46e25ea121d30124bea2900eaf4640d37b21"},
      {"private/asian_genuine_permute.h",
       "1270223b14a73c79818711f9b407e448acdcd6cdc1002c851f9ddb6b454d54bf"},
      {"ordered_d1_x_growth_handoff/ordered_d1_x_growth_setup.c",
       "886bcc7bdd71ff7355c4b7b278a7d19b5608c2fbbed7b9983cd98ff1608f7f08"},
      {"ordered_d1_x_growth_handoff/sobol_ordered_d1_x_growth_diag_avx512.s",
       "bbc08b0348309e47852b550d53f9008e05c880475f6ce4b1899ab69844aa5b89"},
      {"direction_numbers/joe_kuo_6_21201.bin",
       "fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199"},
  };
  char observed[65];
  for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
    if (file_sha256(files[i].path, observed)) {
      fprintf(stderr, "cannot hash frozen prerequisite %s\n", files[i].path);
      return 0;
    }
    if (strcmp(observed, files[i].sha256)) {
      fprintf(stderr, "frozen prerequisite mismatch %s observed=%s\n",
              files[i].path, observed);
      return 0;
    }
  }
  return 1;
}

static int load_directions(void) {
  FILE *in = fopen("direction_numbers/joe_kuo_6_21201.bin", "rb");
  if (!in)
    return -1;
  for (uint32_t d = 0; d < 256; ++d) {
    uint32_t count;
    if (fread(&count, 4, 1, in) != 1 || count != 32 ||
        fread(fixture.directions[d], 4, 32, in) != 32) {
      fclose(in);
      return -1;
    }
  }
  fclose(in);
  return 0;
}

static void produce_sources(void) {
  ordered_d1_x_only_diag(256, fixture.producer, fixture.x);
  asian_vector_exp_range_reduced_array_diag(fixture.x, fixture.growth);
  asian_vector_exp_range_reduced_array_diag(fixture.x + PATHS,
                                             fixture.growth + PATHS);
}

static void prepare_history(void) {
  static const double cycle[] = {82, 117.5, 94.25, 108,
                                 76.5, 123.75, 101, 89.5};
  for (uint32_t i = 0; i < fixture.c; ++i)
    fixture.past[i] = cycle[i & 7u];
}

static void release_fixture(void) {
  free(fixture.output);
  free(fixture.summary);
  free(fixture.active_context);
  free(fixture.seasoned);
  free(fixture.unseasoned);
  free(fixture.producer);
  free(fixture.initial);
  free(fixture.state);
  free(fixture.active_routes);
  free(fixture.seasoned_routes);
  free(fixture.base_routes);
  free(fixture.maps);
  free(fixture.g);
  free(fixture.growth);
  free(fixture.x);
  free(fixture.pressure);
  free(fixture.words[1]);
  free(fixture.words[0]);
  memset(&fixture, 0, sizeof fixture);
}

static int prepare_fixture(uint32_t case_index) {
  memset(&fixture, 0, sizeof fixture);
  fixture.m = case_m[case_index];
  fixture.c = case_c[case_index];
  fixture.f = fixture.m - fixture.c;
  prepare_history();
  if (load_directions())
    return -1;
  fixture.drift = (float)((.03 - .5 * .20 * .20) / fixture.f);
  fixture.diffusion = (float)(.20 / sqrt((double)fixture.f));
  fixture.words[0] = aligned_zero(16384);
  fixture.words[1] = aligned_zero(16384);
  fixture.x = aligned_zero(32768);
  fixture.growth = aligned_zero(32768);
  fixture.g = aligned_zero(16384);
  fixture.maps = aligned_zero(256 * sizeof *fixture.maps);
  fixture.base_routes = aligned_zero(256 * sizeof *fixture.base_routes);
  fixture.seasoned_routes = aligned_zero(256 * sizeof *fixture.seasoned_routes);
  fixture.active_routes = aligned_zero(256 * sizeof *fixture.active_routes);
  fixture.state = aligned_zero(sizeof *fixture.state);
  fixture.initial = aligned_zero(sizeof *fixture.initial);
  fixture.producer = aligned_zero(sizeof *fixture.producer);
  fixture.unseasoned = aligned_zero(sizeof *fixture.unseasoned);
  fixture.seasoned = aligned_zero(sizeof *fixture.seasoned);
  fixture.active_context = aligned_zero(sizeof *fixture.active_context);
  fixture.summary = aligned_zero(sizeof *fixture.summary);
  fixture.output = aligned_zero(sizeof *fixture.output);
  fixture.pressure = aligned_zero(32768);
  if (!fixture.words[0] || !fixture.words[1] || !fixture.x ||
      !fixture.growth || !fixture.g || !fixture.maps || !fixture.base_routes ||
      !fixture.seasoned_routes || !fixture.active_routes || !fixture.state ||
      !fixture.initial || !fixture.producer || !fixture.unseasoned ||
      !fixture.seasoned || !fixture.active_context || !fixture.summary ||
      !fixture.output || !fixture.pressure)
    return -1;
  for (uint32_t i = 0; i < 8192; ++i)
    fixture.pressure[i] = i;
  for (uint32_t i = 0; i < PATHS; ++i) {
    fixture.words[0][i] = sobol_word(8192 + i, fixture.directions[0]);
    fixture.words[1][i] = sobol_word(12288 + i, fixture.directions[0]);
    fixture.initial->s[i] = 100;
  }
  if (asian_genuine_seasoned_source_prepare(
          fixture.producer, fixture.drift, fixture.diffusion, 8192, fixture.f))
    return -1;
  produce_sources();
  const uint32_t *source_words[2] = {fixture.words[0], fixture.words[1]};
  const float *x_blocks[2] = {fixture.x, fixture.x + PATHS};
  const float *growth_blocks[2] = {fixture.growth,
                                   fixture.growth + PATHS};
  uint32_t *target = aligned_zero(16384);
  if (!target)
    return -1;
  for (uint32_t k = 0; k < fixture.f; ++k) {
    for (uint32_t i = 0; i < PATHS; ++i)
      target[i] = sobol_word(8192 + i, fixture.directions[k]);
    if (asian_genuine_prepare_route(source_words, 2, x_blocks, growth_blocks,
                                    target, k, fixture.f, &fixture.maps[k],
                                    &fixture.base_routes[k])) {
      free(target);
      return -1;
    }
  }
  free(target);
  if (asian_genuine_strip_fixed_strikes(32, fixture.strikes) ||
      asian_genuine_strip_prepare(fixture.unseasoned, 100, .03, 0, .20, 1,
                                  fixture.f, 0, 0, 0, fixture.strikes, 32) ||
      asian_genuine_seasoned_prepare(
          fixture.seasoned, fixture.seasoned_routes, fixture.summary,
          fixture.base_routes, 100, .03, 0, .20, 1, fixture.f, fixture.c,
          fixture.c ? fixture.past : NULL, fixture.strikes, 32))
    return -1;
  if (fixture.c == 0 &&
      (memcmp(fixture.unseasoned, fixture.seasoned,
              sizeof *fixture.unseasoned) ||
       memcmp(fixture.base_routes, fixture.seasoned_routes,
              256 * sizeof *fixture.base_routes)))
    return -1;
  return 0;
}

static void select_candidate(uint32_t seasoned) {
  const asian_genuine_route_t *routes =
      seasoned ? fixture.seasoned_routes : fixture.base_routes;
  const asian_genuine_strip_context_t *context =
      seasoned ? fixture.seasoned : fixture.unseasoned;
  memcpy(fixture.active_routes, routes, 256 * sizeof *routes);
  memcpy(fixture.active_context, context, sizeof *context);
}

static void reset_active(void) {
  memcpy(fixture.state, fixture.initial, sizeof *fixture.state);
  memset(fixture.output, 0, sizeof *fixture.output);
  memset(fixture.x, 0, 32768);
  memset(fixture.growth, 0, 32768);
  memset(fixture.g, 0, 16384);
}

static void run_active(uint32_t estimator) {
  produce_sources();
  asian_genuine_sql_dual_control_diag(fixture.active_routes, fixture.f,
                                      fixture.state);
  asian_genuine_strip_l_to_g_diag(fixture.state->l, fixture.active_context,
                                  fixture.g);
  if (asian_genuine_strip_price_delta_diag(
          fixture.state->q, fixture.g, fixture.active_context,
          (enum asian_genuine_strip_estimator)estimator,
          ASIAN_GENUINE_STRIP_TILE8, fixture.output))
    abort();
}

static uint64_t active_checksum(void) {
  uint64_t hash = UINT64_C(1469598103934665603);
  hash = hash_bytes(hash, fixture.state, sizeof *fixture.state);
  hash = hash_bytes(hash, fixture.g, 16384);
  hash = hash_bytes(hash, fixture.output, sizeof *fixture.output);
  return hash;
}

static void warm_touch(void) {
  const unsigned char *parts[] = {
      (const unsigned char *)fixture.x,
      (const unsigned char *)fixture.growth,
      (const unsigned char *)fixture.active_routes,
      (const unsigned char *)fixture.maps,
      (const unsigned char *)fixture.state,
      (const unsigned char *)fixture.producer,
      (const unsigned char *)fixture.active_context,
      (const unsigned char *)fixture.g,
      (const unsigned char *)fixture.output};
  const size_t sizes[] = {32768,
                          32768,
                          256 * sizeof *fixture.active_routes,
                          256 * sizeof *fixture.maps,
                          sizeof *fixture.state,
                          sizeof *fixture.producer,
                          sizeof *fixture.active_context,
                          16384,
                          sizeof *fixture.output};
  uint64_t value = 1;
  for (uint32_t part = 0; part < sizeof parts / sizeof parts[0]; ++part)
    for (size_t i = 0; i < sizes[part]; i += 64)
      value += parts[part][i];
  checksum_sink ^= value;
}

static void historical_pressure(void) {
  uint64_t value = 1;
  for (uint32_t i = 0; i < 8192; ++i) {
    fixture.pressure[i] += i + 3;
    value += fixture.pressure[i];
  }
  checksum_sink ^= value;
}

static void condition(uint32_t mode) {
  if (mode)
    historical_pressure();
  else
    warm_touch();
}

static void candidate_specific_warm(uint32_t seasoned, uint32_t estimator,
                                    uint32_t mode) {
  select_candidate(seasoned);
  reset_active();
  condition(mode);
  run_active(estimator);
  checksum_sink ^= active_checksum();
}

static void timed_invocation(uint32_t seasoned, uint32_t estimator,
                             uint32_t mode, uint64_t *ticks,
                             uint64_t *elapsed_ns, uint64_t *checksum) {
  candidate_specific_warm(seasoned, estimator, mode);
  select_candidate(seasoned);
  reset_active();
  condition(mode);
  const uint64_t w0 = wall_ns();
  const uint64_t t0 = tsc_begin();
  run_active(estimator);
  const uint64_t t1 = tsc_end();
  const uint64_t w1 = wall_ns();
  const uint64_t value = active_checksum();
  *ticks = t1 - t0;
  *elapsed_ns = w1 - w0;
  *checksum = value;
  checksum_sink ^= value;
}

static int preflight_fixture(void) {
  const uint64_t base_route_hash = hash_bytes(
      UINT64_C(1469598103934665603), fixture.base_routes,
      256 * sizeof *fixture.base_routes);
  const uint64_t seasoned_route_hash = hash_bytes(
      UINT64_C(1469598103934665603), fixture.seasoned_routes,
      256 * sizeof *fixture.seasoned_routes);
  const uint64_t base_context_hash = hash_bytes(
      UINT64_C(1469598103934665603), fixture.unseasoned,
      sizeof *fixture.unseasoned);
  const uint64_t seasoned_context_hash = hash_bytes(
      UINT64_C(1469598103934665603), fixture.seasoned,
      sizeof *fixture.seasoned);
  for (uint32_t estimator = 0; estimator < ESTIMATORS; ++estimator) {
    asian_genuine_state_t *state_a = aligned_zero(sizeof *state_a);
    float *g_a = aligned_zero(16384);
    asian_genuine_strip_output_t *out_a = aligned_zero(sizeof *out_a);
    if (!state_a || !g_a || !out_a)
      return -1;
    select_candidate(0);
    reset_active();
    run_active(estimator);
    const uint64_t hash_a = active_checksum();
    memcpy(state_a, fixture.state, sizeof *state_a);
    memcpy(g_a, fixture.g, 16384);
    memcpy(out_a, fixture.output, sizeof *out_a);
    select_candidate(1);
    reset_active();
    run_active(estimator);
    const uint64_t hash_b = active_checksum();
    const int finite = isfinite(fixture.output->values[0].call_price) &&
                       isfinite(fixture.output->values[31].put_delta);
    const int control_equal =
        fixture.c != 0 ||
        (hash_a == hash_b &&
         !memcmp(state_a, fixture.state, sizeof *state_a) &&
         !memcmp(g_a, fixture.g, 16384) &&
         !memcmp(out_a, fixture.output, sizeof *out_a));
    free(out_a);
    free(g_a);
    free(state_a);
    if (!finite || !control_equal)
      return -1;
  }
  if (base_route_hash !=
          hash_bytes(UINT64_C(1469598103934665603), fixture.base_routes,
                     256 * sizeof *fixture.base_routes) ||
      seasoned_route_hash !=
          hash_bytes(UINT64_C(1469598103934665603), fixture.seasoned_routes,
                     256 * sizeof *fixture.seasoned_routes) ||
      base_context_hash !=
          hash_bytes(UINT64_C(1469598103934665603), fixture.unseasoned,
                     sizeof *fixture.unseasoned) ||
      seasoned_context_hash !=
          hash_bytes(UINT64_C(1469598103934665603), fixture.seasoned,
                     sizeof *fixture.seasoned))
    return -1;
  return 0;
}

static int preflight_all(void) {
  if (!committed_prerequisites() || !frozen_files_unchanged())
    return -1;
  for (uint32_t case_index = 0; case_index < CASES; ++case_index) {
    if (prepare_fixture(case_index) || preflight_fixture()) {
      release_fixture();
      return -1;
    }
    release_fixture();
  }
  return 0;
}

static int pin_first_permitted_cpu(char *allowed, size_t capacity) {
  cpu_set_t set, pinned;
  CPU_ZERO(&set);
  if (sched_getaffinity(0, sizeof set, &set))
    return -1;
  size_t used = 0;
  int first = -1;
  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (!CPU_ISSET(cpu, &set))
      continue;
    if (first < 0)
      first = cpu;
    const int wrote = snprintf(allowed + used, capacity - used, "%s%d",
                               used ? "," : "", cpu);
    if (wrote < 0 || (size_t)wrote >= capacity - used)
      return -1;
    used += (size_t)wrote;
  }
  if (first < 0)
    return -1;
  CPU_ZERO(&pinned);
  CPU_SET(first, &pinned);
  if (sched_setaffinity(0, sizeof pinned, &pinned))
    return -1;
  return first;
}

static void json_string(FILE *out, const char *value) {
  fputc('"', out);
  for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
    if (*p == '"' || *p == '\\')
      fputc('\\', out);
    if (*p >= 0x20)
      fputc(*p, out);
  }
  fputc('"', out);
}

static FILE *exclusive_output(const char *path) {
  const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
  if (fd < 0)
    return NULL;
  FILE *out = fdopen(fd, "wb");
  if (!out)
    close(fd);
  return out;
}

static void write_platform(FILE *out, int cpu, const char *allowed) {
  char model[512] = "unknown", siblings[512] = "unknown";
  FILE *cpuinfo = fopen("/proc/cpuinfo", "rb");
  if (cpuinfo) {
    char line[1024];
    while (fgets(line, sizeof line, cpuinfo))
      if (!strncmp(line, "model name", 10)) {
        const char *colon = strchr(line, ':');
        if (colon) {
          ++colon;
          while (*colon == ' ' || *colon == '\t')
            ++colon;
          snprintf(model, sizeof model, "%s", colon);
          model[strcspn(model, "\r\n")] = 0;
        }
        break;
      }
    fclose(cpuinfo);
  }
  char path[512];
  snprintf(path, sizeof path,
           "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", cpu);
  (void)read_line(path, siblings, sizeof siblings);
  fprintf(out, "\"cpu\":%d,\"allowed_affinity\":", cpu);
  json_string(out, allowed);
  fputs(",\"thread_siblings\":", out);
  json_string(out, siblings);
  fputs(",\"cpu_model\":", out);
  json_string(out, model);
  fputs(",\"clock\":\"CLOCK_MONOTONIC_RAW\",\"cache\":[", out);
  int comma = 0;
  for (unsigned index = 0; index < 8; ++index) {
    char level[64], type[64], size[64], line[64], shared[512];
    snprintf(path, sizeof path,
             "/sys/devices/system/cpu/cpu%d/cache/index%u/level", cpu, index);
    if (read_line(path, level, sizeof level))
      break;
    snprintf(path, sizeof path,
             "/sys/devices/system/cpu/cpu%d/cache/index%u/type", cpu, index);
    if (read_line(path, type, sizeof type))
      strcpy(type, "unknown");
    snprintf(path, sizeof path,
             "/sys/devices/system/cpu/cpu%d/cache/index%u/size", cpu, index);
    if (read_line(path, size, sizeof size))
      strcpy(size, "unknown");
    snprintf(path, sizeof path,
             "/sys/devices/system/cpu/cpu%d/cache/index%u/coherency_line_size",
             cpu, index);
    if (read_line(path, line, sizeof line))
      strcpy(line, "unknown");
    snprintf(path, sizeof path,
             "/sys/devices/system/cpu/cpu%d/cache/index%u/shared_cpu_list", cpu,
             index);
    if (read_line(path, shared, sizeof shared))
      strcpy(shared, "unknown");
    fprintf(out, "%s{\"index\":%u,\"level\":", comma++ ? "," : "",
            index);
    json_string(out, level);
    fputs(",\"type\":", out);
    json_string(out, type);
    fputs(",\"size\":", out);
    json_string(out, size);
    fputs(",\"line_bytes\":", out);
    json_string(out, line);
    fputs(",\"shared_cpu_list\":", out);
    json_string(out, shared);
    fputc('}', out);
  }
  fputc(']', out);
}

static void write_quartet(FILE *out, uint32_t index, uint32_t pattern,
                          uint32_t estimator, uint32_t mode) {
  const uint32_t candidates[2][4] = {{0, 1, 1, 0}, {1, 0, 0, 1}};
  uint64_t ticks[4], elapsed[4], checksums[4];
  for (uint32_t invocation = 0; invocation < QUARTET_INVOCATIONS;
       ++invocation)
    timed_invocation(candidates[pattern][invocation], estimator, mode,
                     &ticks[invocation], &elapsed[invocation],
                     &checksums[invocation]);
  fprintf(out, "{\"index\":%u,\"pattern\":\"%s\",\"candidates\":[",
          index, pattern ? "BAAB" : "ABBA");
  for (uint32_t i = 0; i < 4; ++i)
    fprintf(out, "%s\"%c\"", i ? "," : "", candidates[pattern][i] ? 'B' : 'A');
  fputs("],\"tsc\":[", out);
  for (uint32_t i = 0; i < 4; ++i)
    fprintf(out, "%s%" PRIu64, i ? "," : "", ticks[i]);
  fputs("],\"wall_ns\":[", out);
  for (uint32_t i = 0; i < 4; ++i)
    fprintf(out, "%s%" PRIu64, i ? "," : "", elapsed[i]);
  fputs("],\"checksums\":[", out);
  for (uint32_t i = 0; i < 4; ++i)
    fprintf(out, "%s\"0x%016" PRIx64 "\"", i ? "," : "", checksums[i]);
  fputs("]}", out);
}

int main(int argc, char **argv) {
  const char *output_path =
      "results/asian_genuine_seasoned_overhead_confirmation/raw_aws.json";
  int check_only = 0;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--check-only"))
      check_only = 1;
    else if (!strcmp(argv[i], "--json") && i + 1 < argc)
      output_path = argv[++i];
    else {
      fprintf(stderr, "usage: %s [--check-only] [--json PATH]\n", argv[0]);
      return 2;
    }
  }
  char allowed[8192] = {0}, binary_hash[65];
  const int cpu = pin_first_permitted_cpu(allowed, sizeof allowed);
  if (cpu < 0 || executable_sha256(binary_hash) || preflight_all()) {
    fputs("seasoned overhead native preflight failed\n", stderr);
    return 2;
  }
  if (check_only) {
    printf("asian_genuine_seasoned_overhead_confirmation native_preflight=PASS "
           "tile=8 candidates=A_matched_f_unseasoned,B_seasoned "
           "binary_sha256=%s\n",
           binary_hash);
    return 0;
  }
  FILE *out = exclusive_output(output_path);
  if (!out) {
    fprintf(stderr, "refusing to overwrite or create %s: %s\n", output_path,
            strerror(errno));
    return 2;
  }
  fputs("{\"schema\":\"asian-genuine-seasoned-overhead-raw-v1\",", out);
  fprintf(out,
          "\"frozen_commit\":\"d31ed2eafdaeb892fbbbf49f55765600548ab46d\","
          "\"binary_sha256\":\"%s\",\"paths\":4096,\"tile\":8,"
          "\"frozen_files_verified\":true,"
          "\"output\":\"price_delta\",\"warmup_quartets\":16,"
          "\"measured_quartets\":201,\"quartet_seed\":"
          "\"0x%016" PRIx64 "\",\"ratio_definition\":"
          "\"(B1+B2)/(A1+A2)\",\"candidate_A\":"
          "\"matched_f_unseasoned\",\"candidate_B\":\"seasoned\",",
          binary_hash, quartet_seed);
  write_platform(out, cpu, allowed);
  fputs(",\"cells\":[", out);
  uint64_t random_state = quartet_seed;
  int cell_comma = 0;
  for (uint32_t case_index = 0; case_index < CASES; ++case_index) {
    if (prepare_fixture(case_index) || preflight_fixture()) {
      fclose(out);
      return 2;
    }
    for (uint32_t estimator = 0; estimator < ESTIMATORS; ++estimator)
      for (uint32_t mode = 0; mode < CACHE_MODES; ++mode) {
        fprintf(out,
                "%s{\"case_index\":%u,\"M\":%u,\"c\":%u,\"f\":%u,"
                "\"negative_control\":%s,\"estimator\":\"%s\","
                "\"cache_mode\":\"%s\",\"warmups\":[",
                cell_comma++ ? "," : "", case_index, fixture.m, fixture.c,
                fixture.f, fixture.c ? "false" : "true",
                estimator ? "geometric_cv" : "arithmetic",
                mode ? "historical_32KiB_rmw" : "candidate_specific_warm");
        uint32_t pair_pattern = 0;
        for (uint32_t quartet = 0; quartet < WARMUP_QUARTETS; ++quartet) {
          if (!(quartet & 1u))
            pair_pattern = (uint32_t)(next_random(&random_state) & 1u);
          const uint32_t pattern = pair_pattern ^ (quartet & 1u);
          if (quartet)
            fputc(',', out);
          write_quartet(out, quartet, pattern, estimator, mode);
        }
        fputs("],\"quartets\":[", out);
        pair_pattern = 0;
        for (uint32_t quartet = 0; quartet < MEASURED_QUARTETS; ++quartet) {
          if (!(quartet & 1u))
            pair_pattern = (uint32_t)(next_random(&random_state) & 1u);
          const uint32_t pattern = pair_pattern ^ (quartet & 1u);
          if (quartet)
            fputc(',', out);
          write_quartet(out, quartet, pattern, estimator, mode);
        }
        fputs("]}", out);
        fflush(out);
      }
    release_fixture();
  }
  fprintf(out,
          "],\"final_rng_state\":\"0x%016" PRIx64
          "\",\"checksum_sink\":\"0x%016" PRIx64 "\"}\n",
          random_state, checksum_sink);
  if (fclose(out))
    return 2;
  printf("raw paired overhead timings written without replacement: %s\n",
         output_path);
  return 0;
}
