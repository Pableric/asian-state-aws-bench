#ifndef ASIAN_GENUINE_AAD_PHASE1_REFERENCE_H
#define ASIAN_GENUINE_AAD_PHASE1_REFERENCE_H

#include <stdint.h>

typedef struct {
    double a, a_delta, a_vega, a_rho;
    double l, g, g_delta, g_vega, g_rho;
} asian_aad_ref_basis_t;

typedef struct {
    double price, delta, vega, rho;
} asian_aad_ref_value_t;

void asian_aad_ref_primal(const double *x, uint32_t n, double s0,
                          double *s, asian_aad_ref_basis_t *out);
void asian_aad_ref_targeted(const double *x, uint32_t n, double s0,
                            double rate, double dividend_yield, double sigma,
                            double maturity, asian_aad_ref_basis_t *out);
void asian_aad_ref_suffix(const double *x, uint32_t n, double s0,
                          double rate, double dividend_yield, double sigma,
                          double maturity, asian_aad_ref_basis_t *out);
void asian_aad_ref_generic(const double *x, uint32_t n, double s0,
                           double rate, double dividend_yield, double sigma,
                           double maturity, asian_aad_ref_basis_t *out);
void asian_aad_ref_payoff(const asian_aad_ref_basis_t *b, double strike,
                          double rate, double maturity, int put, int cv,
                          const asian_aad_ref_value_t *exact_geometric,
                          asian_aad_ref_value_t *out);
double asian_aad_ref_price_from_z(const double *z, uint32_t n, double s0,
                                  double strike, double rate,
                                  double dividend_yield, double sigma,
                                  double maturity, int put, int cv,
                                  const asian_aad_ref_value_t *exact_geometric);

#endif
