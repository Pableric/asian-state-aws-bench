#ifndef AUTOCALL_SINGLE_ASSET_THREE_DATE_GREEKS_DIAG_H
#define AUTOCALL_SINGLE_ASSET_THREE_DATE_GREEKS_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUTOCALL_3DATE_GREEK_OUTPUT_MAGIC UINT32_C(0x4b474341)

enum autocall_3date_greek_status {
    AUTOCALL_3DATE_GREEK_OK = 0,
    AUTOCALL_3DATE_GREEK_INVALID = -1,
    AUTOCALL_3DATE_GREEK_UNSUPPORTED_BUMP = -2,
    AUTOCALL_3DATE_GREEK_DOMAIN = -3,
};

typedef struct {
    double minus;
    double zero;
    double plus;
} autocall_3date_greek_coefficients_t;

typedef struct {
    float minus;
    float zero;
    float plus;
    uint32_t reserved;
    double dm;
    double dp;
    autocall_3date_greek_coefficients_t first;
    autocall_3date_greek_coefficients_t second;
} autocall_3date_greek_legs_t;

typedef struct __attribute__((aligned(64))) {
    double price;
    double delta_per_spot_unit;
    double gamma_per_spot_unit_squared;
    double vega_per_unit_sigma;
    double vega_per_one_vol_point;
    float spot_minus;
    float spot_zero;
    float spot_plus;
    float sigma_minus;
    float sigma_zero;
    float sigma_plus;
    double spot_dm;
    double spot_dp;
    double volatility_vm;
    double volatility_vp;
    double requested_spot_fraction;
    double requested_volatility_bump;
    uint32_t magic;
    int32_t status;
    uint8_t reserved[8];
} autocall_3date_core_greek_output_t;

#ifdef __cplusplus
static_assert(sizeof(autocall_3date_core_greek_output_t) == 128,
              "natural two-line core-Greek output");
#else
_Static_assert(sizeof(autocall_3date_core_greek_output_t) == 128,
               "natural two-line core-Greek output");
#endif

int autocall_3date_prepare_spot_legs(double spot,double fraction,
    autocall_3date_greek_legs_t *out);
int autocall_3date_prepare_volatility_legs(double sigma,double absolute_bump,
    autocall_3date_greek_legs_t *out);

#ifdef __cplusplus
}
#endif

#endif
