#ifndef ASIAN_REFERENCE_H
#define ASIAN_REFERENCE_H
#include "asian_pricer.h"
#include <stdint.h>

const uint32_t (*asian_w_for_mode(asian_pricing_mode_t mode))[32];
double asian_inverse_normal_u32(uint32_t word);
double asian_scalar_payoff_sum(const asian_price_request_t* req,
                               const uint32_t w[32][32]);
#endif
