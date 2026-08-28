#ifndef AUTOCALL_SINGLE_ASSET_THREE_DATE_CASES_H
#define AUTOCALL_SINGLE_ASSET_THREE_DATE_CASES_H

#include <stddef.h>

typedef struct {
    const char *name;
    double rate, dividend, sigma, maturity;
    double s0, notional, protection;
    double call_barrier[3];
    double coupon_barrier[3];
    double coupon[3];
    double redemption[3];
    double payment_lag_fraction;
} autocall_frozen_case_t;

static const autocall_frozen_case_t autocall_frozen_cases[] = {
    {"EARLY_CALL", 0.03, 0.01, 0.20, 1.00, 100, 100, 60,
     {90,90,90}, {85,85,85}, {2,2,2}, {100,100,100}, 0},
    {"STEPDOWN", 0.03, 0.01, 0.20, 1.00, 100, 100, 65,
     {110,105,100}, {80,80,80}, {1,2,3}, {100,101,102}, 0},
    {"SECOND_DATE", 0.01, 0.02, 0.08, 1.00, 100, 100, 70,
     {200,100,95}, {90,90,90}, {1,1.5,2}, {101,102,103}, 0},
    {"COUPON_ABOVE_CALL", 0.01, 0.00, 0.55, 1.50, 100, 100, 65,
     {100,95,90}, {120,115,110}, {3,4,5}, {100,101,102}, 0},
    {"MATURITY_DOWNSIDE", -0.01, 0.03, 0.30, 0.75, 100, 100, 100,
     {180,170,160}, {120,110,100}, {0,0,0}, {100,100,100}, 0},
    {"COUPON_NO_CALL", 0.01, 0.02, 0.08, 1.00, 100, 100, 70,
     {160,150,140}, {90,90,90}, {1,1.5,2}, {101,102,103}, 0},
    {"LAGGED_PAYMENTS", 0.01, 0.00, 0.55, 1.50, 100, 100, 75,
     {115,110,105}, {95,95,95}, {0.5,1,1.5}, {100,100,100}, 0.02},
};

enum {
    AUTOCALL_FROZEN_CASE_COUNT =
        sizeof(autocall_frozen_cases) / sizeof(autocall_frozen_cases[0])
};

#endif
