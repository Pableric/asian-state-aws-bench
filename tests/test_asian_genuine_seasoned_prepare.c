#define _POSIX_C_SOURCE 200112L
#include "private/asian_genuine_seasoned_strip_diag.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *a64(size_t bytes) {
  void *p = 0;
  if (posix_memalign(&p, 64, bytes))
    return 0;
  memset(p, 0, bytes);
  return p;
}

static uint32_t float_bits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  return bits;
}

static void fill_routes(asian_genuine_route_t routes[256],
                        fragment_map_t maps[256], uint32_t future) {
  static float x[1], growth[1];
  memset(routes, 0, 256 * sizeof *routes);
  memset(maps, 0, 256 * sizeof *maps);
  for (uint32_t k = 0; k < future; ++k) {
    maps[k].dimension = k + 1;
    routes[k].x_base = x;
    routes[k].growth_base = growth;
    routes[k].map = &maps[k];
    routes[k].weight_bits = float_bits((float)(future - k) / (float)future);
    routes[k].fixing_index = k;
  }
}

static int exhaustive_weights(void) {
  for (uint32_t total = 1; total <= 256; ++total)
    for (uint32_t future = 1; future <= total; ++future)
      for (uint32_t k = 0; k < future; ++k) {
        const float independent =
            (float)((long double)(future - k) / (long double)total);
        if (asian_genuine_seasoned_weight_bits(future - k, total) !=
            float_bits(independent))
          return -1;
      }
  return asian_genuine_seasoned_weight_bits(0, 1) == 0 &&
                 asian_genuine_seasoned_weight_bits(2, 1) == 0 &&
                 asian_genuine_seasoned_weight_bits(1, 257) == 0
             ? 0
             : -1;
}

static int source_adapter(void) {
  ordered_d1_diag_context_t *irregular = a64(sizeof *irregular);
  ordered_d1_diag_context_t *qualified = a64(sizeof *qualified);
  if (!irregular || !qualified)
    return -1;
  if (asian_genuine_seasoned_source_prepare(irregular, 0.001f, 0.05f, 8192,
                                            15) ||
      ordered_d1_diag_prepare(qualified, 0.001f, 0.05f, 8192,
                              ORDERED_D1_DIAG_PREPARE_X3, 32) ||
      memcmp(irregular, qualified,
             offsetof(ordered_d1_diag_context_t, weights)) ||
      asian_genuine_seasoned_source_prepare(irregular, 0.001f, 0.05f, 8192,
                                            0) == 0 ||
      asian_genuine_seasoned_source_prepare(irregular, 0.001f, 0.05f, 8192,
                                            257) == 0) {
    free(qualified);
    free(irregular);
    return -1;
  }
  free(qualified);
  free(irregular);
  return 0;
}

static int one_case(uint32_t future, uint32_t completed, const double *past) {
  asian_genuine_route_t *base = a64(256 * sizeof *base);
  asian_genuine_route_t *got = a64(256 * sizeof *got);
  asian_genuine_route_t *saved = a64(256 * sizeof *saved);
  asian_genuine_route_t *again = a64(256 * sizeof *again);
  fragment_map_t *maps = a64(256 * sizeof *maps);
  asian_genuine_strip_context_t *context = a64(sizeof *context);
  asian_genuine_strip_context_t *direct = a64(sizeof *direct);
  asian_genuine_seasoned_summary_t *summary = a64(sizeof *summary);
  asian_genuine_seasoned_summary_t *summary_again = a64(sizeof *summary_again);
  float strikes[32];
  float strikes_saved[32];
  double past_saved[256];
  if (!base || !got || !saved || !again || !maps || !context || !direct ||
      !summary || !summary_again ||
      asian_genuine_strip_fixed_strikes(32, strikes))
    return -1;
  memcpy(strikes_saved, strikes, sizeof strikes);
  if (completed)
    memcpy(past_saved, past, completed * sizeof *past);
  fill_routes(base, maps, future);
  memcpy(saved, base, 256 * sizeof *saved);
  if (asian_genuine_seasoned_prepare(context, got, summary, base, 100.0, 0.03,
                                     0.0, 0.20, 1.0, future, completed, past,
                                     strikes, 32))
    return -1;
  if (asian_genuine_seasoned_prepare(direct, again, summary_again, base, 100.0,
                                     0.03, 0.0, 0.20, 1.0, future, completed,
                                     past, strikes, 32) ||
      memcmp(context, direct, sizeof *context) ||
      memcmp(got, again, 256 * sizeof *got) ||
      memcmp(summary, summary_again, sizeof *summary) ||
      memcmp(strikes, strikes_saved, sizeof strikes) ||
      (completed && memcmp(past, past_saved, completed * sizeof *past)))
    return -1;
  if (memcmp(base, saved, 256 * sizeof *base) ||
      summary->magic != ASIAN_GENUINE_SEASONED_MAGIC ||
      summary->future_fixings != future ||
      summary->completed_fixings != completed ||
      summary->total_fixings != future + completed)
    return -1;
  for (uint32_t k = 0; k < future; ++k) {
    asian_genuine_route_t expected = base[k];
    if (completed)
      expected.weight_bits =
          asian_genuine_seasoned_weight_bits(future - k, future + completed);
    if (memcmp(&expected, &got[k], sizeof expected))
      return -1;
  }
  if (completed == 0) {
    if (asian_genuine_strip_prepare(direct, 100, .03, 0, .20, 1, future, 0, 0,
                                    0, strikes, 32) ||
        memcmp(context, direct, sizeof *direct) ||
        memcmp(got, base, 256 * sizeof *base))
      return -1;
  } else {
    double q0 = 0, logs = 0;
    for (uint32_t i = 0; i < completed; ++i) {
      q0 += past[i];
      logs += log(past[i]);
    }
    if (summary->q0 != q0 || summary->past_log_sum != logs ||
        context->initial_q != (float)q0)
      return -1;
  }
  free(summary_again);
  free(summary);
  free(direct);
  free(context);
  free(maps);
  free(again);
  free(saved);
  free(got);
  free(base);
  return 0;
}

static int invalid_cases(void) {
  asian_genuine_route_t *base = a64(256 * sizeof *base),
                        *out = a64(256 * sizeof *out);
  fragment_map_t *maps = a64(256 * sizeof *maps);
  asian_genuine_strip_context_t *context = a64(sizeof *context);
  asian_genuine_seasoned_summary_t *summary = a64(sizeof *summary);
  float strikes[32];
  double good[1] = {100}, bad0[1] = {0}, badn[1] = {NAN}, badi[1] = {INFINITY},
         huge[1] = {DBL_MAX};
  if (!base || !out || !maps || !context || !summary ||
      asian_genuine_strip_fixed_strikes(1, strikes))
    return -1;
  fill_routes(base, maps, 1);
#define REJECT(f, c, p)                                                        \
  do {                                                                         \
    if (!asian_genuine_seasoned_prepare(context, out, summary, base, 100, .03, \
                                        0, .2, 1, (f), (c), (p), strikes, 1))  \
      return -1;                                                               \
  } while (0)
  REJECT(0, 0, 0);
  REJECT(257, 0, 0);
  REJECT(1, 256, good);
  REJECT(1, 0, good);
  REJECT(1, 1, 0);
  REJECT(1, 1, bad0);
  REJECT(1, 1, badn);
  REJECT(1, 1, badi);
  REJECT(1, 1, huge);
  if (!asian_genuine_seasoned_prepare(context, out, summary, base, 100, NAN, 0,
                                      .2, 1, 1, 1, good, strikes, 1) ||
      !asian_genuine_seasoned_prepare(context, out, summary, base, 1e-40, .03,
                                      0, 1.0, 1, 1, 1, good, strikes, 1))
    return -1;
  if (!asian_genuine_seasoned_prepare(context, base, summary, base, 100, .03, 0,
                                      .2, 1, 1, 1, good, strikes, 1))
    return -1;
  base[0].fixing_index = 2;
  REJECT(1, 1, good);
#undef REJECT
  free(summary);
  free(context);
  free(maps);
  free(out);
  free(base);
  return 0;
}

int main(void) {
  if (exhaustive_weights() || source_adapter())
    return 2;
  static const uint32_t totals[] = {16, 32, 64, 128, 256};
  double past[256];
  for (size_t m = 0; m < sizeof totals / sizeof totals[0]; ++m) {
    const uint32_t total = totals[m],
                   cs[] = {0, 1, total / 4, total / 2, total - 1};
    for (size_t j = 0; j < 5; ++j) {
      for (uint32_t i = 0; i < cs[j]; ++i)
        past[i] = 82.0 + 0.125 * i;
      if (one_case(total - cs[j], cs[j], cs[j] ? past : 0))
        return 2;
    }
  }
  static const uint32_t smoke[][2] = {{0, 1},     {1, 1},   {1, 2},
                                      {2, 15},    {7, 26},  {63, 64},
                                      {128, 127}, {1, 255}, {255, 1}};
  for (size_t j = 0; j < sizeof smoke / sizeof smoke[0]; ++j) {
    for (uint32_t i = 0; i < smoke[j][0]; ++i)
      past[i] = 100 + i * .01;
    if (one_case(smoke[j][1], smoke[j][0], smoke[j][0] ? past : 0))
      return 2;
  }
  if (invalid_cases())
    return 2;
  puts("asian_genuine_seasoned_prepare stage1=PASS exhaustive_weight_bits=PASS "
       "source_adapter_x_fields=PASS c0_bits=PASS immutable=PASS "
       "invalid_inputs=PASS");
  return 0;
}
