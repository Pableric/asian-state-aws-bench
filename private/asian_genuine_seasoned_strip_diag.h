#ifndef ASIAN_GENUINE_SEASONED_STRIP_DIAG_H
#define ASIAN_GENUINE_SEASONED_STRIP_DIAG_H

#include "../ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag.h"
#include "asian_genuine_permute.h"
#include "asian_genuine_price_delta_strip_diag.h"

#include <stdint.h>

#define ASIAN_GENUINE_SEASONED_MAGIC UINT32_C(0x53474153) /* "SAGS" */
#define ASIAN_GENUINE_SEASONED_ABI_VERSION UINT16_C(1)

typedef struct __attribute__((aligned(64))) {
  uint32_t magic;
  uint16_t abi_version;
  uint16_t reserved0;
  uint32_t future_fixings;
  uint32_t completed_fixings;
  uint32_t total_fixings;
  uint32_t strike_count;
  double q0;
  double past_log_sum;
  uint64_t past_fixing_hash;
  uint8_t reserved1[16];
} asian_genuine_seasoned_summary_t;

_Static_assert(sizeof(asian_genuine_seasoned_summary_t) == 64,
               "seasoned cold summary size");

uint32_t asian_genuine_seasoned_weight_bits(uint32_t numerator,
                                            uint32_t denominator);

int asian_genuine_seasoned_source_prepare(ordered_d1_diag_context_t *out,
                                          float drift, float diffusion,
                                          uint64_t max_points,
                                          uint32_t future_fixings);

int asian_genuine_seasoned_prepare(
    asian_genuine_strip_context_t *payoff_out,
    asian_genuine_route_t routes_out[256],
    asian_genuine_seasoned_summary_t *summary_out,
    const asian_genuine_route_t *qualified_future_routes, double s0,
    double rate, double dividend_yield, double sigma, double remaining_maturity,
    uint32_t future_fixings, uint32_t completed_fixings,
    const double *completed_values, const float *strikes,
    uint32_t strike_count);

#endif
