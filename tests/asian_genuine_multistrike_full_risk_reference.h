#ifndef ASIAN_GENUINE_MULTISTRIKE_FULL_RISK_REFERENCE_H
#define ASIAN_GENUINE_MULTISTRIKE_FULL_RISK_REFERENCE_H

#include <stdint.h>

typedef struct {
    double a, a_delta, a_vega, a_rho;
    double l, g, g_delta, g_vega, g_rho;
} asian_msfr_ref_basis_t;

typedef struct {
    double price, delta, vega, rho;
} asian_msfr_ref_value_t;

void asian_msfr_ref_targeted(const double *x, uint32_t fixing_count,
                             double s0, double rate, double dividend_yield,
                             double sigma, double maturity,
                             asian_msfr_ref_basis_t *out);

void asian_msfr_ref_sample(const asian_msfr_ref_basis_t *basis,
                           double strike, double rate, double maturity,
                           int put, int geometric_cv,
                           asian_msfr_ref_value_t *out);

void asian_msfr_ref_geometric_exact(double s0, double strike, double rate,
                                    double dividend_yield, double sigma,
                                    double maturity, uint32_t fixing_count,
                                    asian_msfr_ref_value_t *call,
                                    asian_msfr_ref_value_t *put);

void asian_msfr_ref_arithmetic_parity(double s0, double strike, double rate,
                                      double dividend_yield, double maturity,
                                      uint32_t fixing_count,
                                      asian_msfr_ref_value_t *call_minus_put);

#endif
