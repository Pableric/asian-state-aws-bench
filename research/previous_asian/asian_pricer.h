#ifndef ASIAN_PRICER_H
#define ASIAN_PRICER_H

#include <stdint.h>

#define ASIAN_SOBOL_BLOCK_SIZE 8192u

typedef enum {
    ASIAN_CALL = 0,
    ASIAN_PUT = 1
} asian_option_type_t;

typedef enum {
    ASIAN_MODE_REFERENCE = 0,
    ASIAN_MODE_BUFFER_REFERENCE = 0,
    ASIAN_MODE_FINAL_Z = 1,
    ASIAN_MODE_GAUSSIAN_EXP = 1,
    ASIAN_MODE_RANK1 = 2,
    ASIAN_MODE_COEFFICIENT_PAIR = 3
} asian_pricing_mode_t;

typedef struct {
    float s0;
    float k;
    float r;
    float sigma;
    float t;
    uint64_t num_blocks;
    asian_option_type_t type;
    asian_pricing_mode_t mode;
} asian_price_request_t;

typedef struct {
    double price;
    double payoff_sum;
    uint64_t samples;
    double coeff_setup_seconds;
    double kernel_seconds;
} asian_price_result_t;

int price_asian(const asian_price_request_t* req, asian_price_result_t* out);
/* Scalar same-sample oracle for tests; sequence_mode selects its W table. */
int price_asian_scalar_mode(const asian_price_request_t* req,
                            asian_pricing_mode_t sequence_mode,
                            asian_price_result_t* out);
const char* asian_mode_name(asian_pricing_mode_t mode);
int asian_mode_is_experimental(asian_pricing_mode_t mode);
/* Retained for source/ABI compatibility; this is not an Asian analytic price. */
double asian_black_scholes_price(const asian_price_request_t* req);

#endif
