#ifndef EUROPEAN_PRICER_H
#define EUROPEAN_PRICER_H

#include <stdint.h>

#define EUROPEAN_SOBOL_BLOCK_SIZE 8192u

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EUROPEAN_CALL = 0,
    EUROPEAN_PUT = 1
} european_option_type_t;

typedef enum {
    EUROPEAN_MODE_BUFFER_REFERENCE = 0,
    EUROPEAN_MODE_GAUSSIAN_EXP = 1,
    EUROPEAN_MODE_DIRECT_PAYOFF = 2,
    EUROPEAN_MODE_HYBRID = 3,
    EUROPEAN_MODE_HYBRID_DIRECT_TAIL = 4,
    EUROPEAN_MODE_GAUSSIAN_SPLIT_TAIL = 5,
    EUROPEAN_MODE_GAUSSIAN_CENTER_SHARED = 6,
    EUROPEAN_MODE_GAUSSIAN_DYNAMIC_RANGES = 7,
    EUROPEAN_MODE_GAUSSIAN_EXP_REDUCED_FMA = 8,
    EUROPEAN_MODE_ORDERED_D1_MIN_FMA = 9
} european_pricing_mode_t;

typedef struct {
    float s0;
    float k;
    float r;
    float sigma;
    float t;
    uint64_t num_blocks;
    european_option_type_t type;
    european_pricing_mode_t mode;
} european_price_request_t;

typedef struct {
    double price;
    double payoff_sum;
    uint64_t samples;
    double coeff_setup_seconds;
    double kernel_seconds;
} european_price_result_t;

/* Immutable, thread-safe contract setup for repeated pricing calls. */
typedef struct european_prepared_contract european_prepared_contract_t;

int price_european(const european_price_request_t* req, european_price_result_t* out);
int price_european_points(
    const european_price_request_t* req,
    uint64_t num_points,
    european_price_result_t* out
);
int european_prepare(
    const european_price_request_t* req,
    european_prepared_contract_t** prepared
);
int european_price_prepared(
    const european_prepared_contract_t* prepared,
    european_price_result_t* out
);
int european_price_prepared_points(
    const european_prepared_contract_t* prepared,
    uint64_t num_points,
    european_price_result_t* out
);
void european_prepared_destroy(european_prepared_contract_t* prepared);
double black_scholes_price(const european_price_request_t* req);

#ifdef __cplusplus
}
#endif

#endif
