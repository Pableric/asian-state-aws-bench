#include "private/asian_genuine_seasoned_strip_diag.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint64_t hash_word(uint64_t hash, uint64_t word) {
  for (unsigned byte = 0; byte < 8; ++byte) {
    hash ^= (word >> (8 * byte)) & UINT64_C(0xff);
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint32_t asian_genuine_seasoned_weight_bits(uint32_t numerator,
                                            uint32_t denominator) {
  if (numerator == 0 || denominator == 0 || numerator > denominator ||
      denominator > 256)
    return 0;
  uint64_t scaled = numerator;
  int exponent = 0;
  while (scaled < denominator) {
    scaled <<= 1;
    --exponent;
  }
  uint64_t dividend = scaled << 23;
  uint64_t significand = dividend / denominator;
  const uint64_t remainder = dividend % denominator;
  const uint64_t twice = remainder << 1;
  if (twice > denominator || (twice == denominator && (significand & 1)))
    ++significand;
  if (significand == (UINT64_C(1) << 24)) {
    significand >>= 1;
    ++exponent;
  }
  const uint32_t biased = (uint32_t)(exponent + 127);
  return (biased << 23) | (uint32_t)(significand - (UINT64_C(1) << 23));
}

int asian_genuine_seasoned_source_prepare(ordered_d1_diag_context_t *out,
                                          float drift, float diffusion,
                                          uint64_t max_points,
                                          uint32_t future_fixings) {
  if (future_fixings == 0 || future_fixings > 256)
    return -1;
  const uint32_t qualified_count =
      future_fixings == 16 || future_fixings == 32 || future_fixings == 64 ||
              future_fixings == 128 || future_fixings == 256
          ? future_fixings
          : 16;
  /* The qualified x-only symbol never reads the prepared weight table. */
  return ordered_d1_diag_prepare(out, drift, diffusion, max_points,
                                 ORDERED_D1_DIAG_PREPARE_X3, qualified_count);
}

static int finite_context(const asian_genuine_strip_context_t *context) {
  const float header[] = {
      context->inv_total,
      context->initial_q,
      context->discount,
      context->delta_q_scale,
      context->delta_g_scale,
      context->log_base,
      context->expected_arithmetic,
      context->expected_arithmetic_delta,
      context->exp_input_min,
      context->exp_input_max,
  };
  for (size_t i = 0; i < sizeof header / sizeof header[0]; ++i)
    if (!isfinite(header[i]))
      return 0;
  for (uint32_t i = 0; i < context->strike_count; ++i) {
    const asian_genuine_strip_strike_t *strike = &context->strikes[i];
    const double values[] = {
        strike->geometric_price_exact_direct,
        strike->geometric_delta_exact_direct,
        strike->call_price_adjust,
        strike->put_price_adjust,
        strike->call_delta_adjust,
        strike->put_delta_adjust,
    };
    if (!isfinite(strike->strike) || !isfinite(strike->direct_sign))
      return 0;
    for (size_t j = 0; j < sizeof values / sizeof values[0]; ++j)
      if (!isfinite(values[j]))
        return 0;
  }
  return 1;
}

static int overlaps(const void *a, size_t a_size, const void *b,
                    size_t b_size) {
  const uintptr_t aa = (uintptr_t)a, bb = (uintptr_t)b;
  return aa < bb + b_size && bb < aa + a_size;
}

int asian_genuine_seasoned_prepare(
    asian_genuine_strip_context_t *payoff_out,
    asian_genuine_route_t routes_out[256],
    asian_genuine_seasoned_summary_t *summary_out,
    const asian_genuine_route_t *qualified_future_routes, double s0,
    double rate, double dividend_yield, double sigma, double remaining_maturity,
    uint32_t future_fixings, uint32_t completed_fixings,
    const double *completed_values, const float *strikes,
    uint32_t strike_count) {
  if (!payoff_out || !routes_out || !summary_out || !qualified_future_routes ||
      ((uintptr_t)payoff_out & 63u) || ((uintptr_t)routes_out & 31u) ||
      ((uintptr_t)summary_out & 63u) || future_fixings == 0 ||
      future_fixings > 256 || completed_fixings > 256 - future_fixings ||
      (completed_fixings == 0 ? completed_values != 0
                              : completed_values == 0) ||
      overlaps(routes_out, 256 * sizeof *routes_out, qualified_future_routes,
               future_fixings * sizeof *qualified_future_routes)) {
    if (payoff_out)
      payoff_out->magic = 0;
    if (summary_out)
      summary_out->magic = 0;
    return -1;
  }
  payoff_out->magic = 0;
  summary_out->magic = 0;

  double q0 = 0.0, past_log_sum = 0.0;
  uint64_t past_hash = UINT64_C(1469598103934665603);
  for (uint32_t i = 0; i < completed_fixings; ++i) {
    const double value = completed_values[i];
    if (!(value > 0.0) || !isfinite(value))
      return -1;
    q0 += value;
    past_log_sum += log(value);
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    past_hash = hash_word(past_hash, bits);
    if (!isfinite(q0) || !isfinite(past_log_sum))
      return -1;
  }
  if (!isfinite((float)q0))
    return -1;

  _Alignas(64) asian_genuine_strip_context_t payoff;
  const int prepared = asian_genuine_strip_prepare(
      &payoff, s0, rate, dividend_yield, sigma, remaining_maturity,
      future_fixings, completed_fixings, q0, past_log_sum, strikes,
      strike_count);
  if (prepared || !finite_context(&payoff))
    return prepared == -2 ? -2 : -1;

  _Alignas(32) asian_genuine_route_t routes[256];
  memset(routes, 0, sizeof routes);
  for (uint32_t k = 0; k < future_fixings; ++k) {
    const asian_genuine_route_t *source = &qualified_future_routes[k];
    if (!source->x_base || !source->growth_base || !source->map ||
        source->fixing_index != k || source->map->dimension != k + 1)
      return -1;
    const uint32_t unseasoned_bits =
        asian_genuine_seasoned_weight_bits(future_fixings - k, future_fixings);
    if (source->weight_bits != unseasoned_bits)
      return -1;
    routes[k] = *source;
    if (completed_fixings != 0)
      routes[k].weight_bits = asian_genuine_seasoned_weight_bits(
          future_fixings - k, future_fixings + completed_fixings);
  }

  _Alignas(64) asian_genuine_seasoned_summary_t summary;
  memset(&summary, 0, sizeof summary);
  summary.abi_version = ASIAN_GENUINE_SEASONED_ABI_VERSION;
  summary.future_fixings = future_fixings;
  summary.completed_fixings = completed_fixings;
  summary.total_fixings = future_fixings + completed_fixings;
  summary.strike_count = strike_count;
  summary.q0 = q0;
  summary.past_log_sum = past_log_sum;
  summary.past_fixing_hash = past_hash;
  summary.magic = ASIAN_GENUINE_SEASONED_MAGIC;

  memcpy(payoff_out, &payoff, sizeof payoff);
  memcpy(routes_out, routes, sizeof routes);
  memcpy(summary_out, &summary, sizeof summary);
  return 0;
}
