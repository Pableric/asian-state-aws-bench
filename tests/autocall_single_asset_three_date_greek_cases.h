#ifndef AUTOCALL_SINGLE_ASSET_THREE_DATE_GREEK_CASES_H
#define AUTOCALL_SINGLE_ASSET_THREE_DATE_GREEK_CASES_H

#include "tests/autocall_single_asset_three_date_cases.h"

#include <math.h>
#include <stddef.h>

enum {
    AUTOCALL_GREEK_CORE_CASES = AUTOCALL_FROZEN_CASE_COUNT,
    AUTOCALL_GREEK_STRESS_CASES = 17,
    AUTOCALL_GREEK_CASES = AUTOCALL_GREEK_CORE_CASES +
                           AUTOCALL_GREEK_STRESS_CASES,
};

static inline double autocall_greek_forward(
    const autocall_frozen_case_t *c, unsigned date)
{
    const double t = c->maturity * (double)(date + 1u) / 3.0;
    return c->s0 * exp((c->rate - c->dividend) * t);
}

static inline double autocall_greek_near_barrier(
    const autocall_frozen_case_t *c, unsigned date, int sign)
{
    const double t = c->maturity * (double)(date + 1u) / 3.0;
    return autocall_greek_forward(c, date) *
        exp((double)sign * 0.25 * c->sigma * sqrt(t));
}

static inline double autocall_greek_inactive_call(
    const autocall_frozen_case_t *c, unsigned date)
{
    const double t = c->maturity * (double)(date + 1u) / 3.0;
    return autocall_greek_forward(c, date) * exp(8.5 * c->sigma * sqrt(t));
}

static inline autocall_frozen_case_t autocall_greek_stress_case(unsigned index)
{
    autocall_frozen_case_t c = {
        "STRESS", 0.02, 0.01, 0.25, 1.0, 100.0, 100.0, 65.0,
        {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0},
        {100.0, 100.0, 100.0}, 0.0
    };
    static const char *const names[AUTOCALL_GREEK_STRESS_CASES] = {
        "CALL_D1_MINUS", "CALL_D1_PLUS", "CALL_D2_MINUS",
        "CALL_D2_PLUS", "CALL_D3_MINUS", "CALL_D3_PLUS",
        "COUPON_D1_MINUS", "COUPON_D1_PLUS", "COUPON_D2_MINUS",
        "COUPON_D2_PLUS", "COUPON_D3_MINUS", "COUPON_D3_PLUS",
        "PROTECTION_MINUS", "PROTECTION_PLUS", "COINCIDENT_D1",
        "COINCIDENT_D2", "COINCIDENT_D3"
    };
    c.name = index < AUTOCALL_GREEK_STRESS_CASES ? names[index] : "INVALID";
    for (unsigned d = 0; d < 3u; ++d)
        c.call_barrier[d] = autocall_greek_inactive_call(&c, d);

    if (index < 6u) {
        const unsigned date = index / 2u;
        const int sign = (index & 1u) ? 1 : -1;
        c.call_barrier[date] = autocall_greek_near_barrier(&c, date, sign);
    } else if (index < 12u) {
        const unsigned local = index - 6u;
        const unsigned date = local / 2u;
        const int sign = (local & 1u) ? 1 : -1;
        c.coupon_barrier[date] = autocall_greek_near_barrier(&c, date, sign);
        c.coupon[date] = 2.0;
    } else if (index < 14u) {
        const int sign = (index & 1u) ? 1 : -1;
        c.protection = autocall_greek_near_barrier(&c, 2u, sign);
    } else {
        const unsigned date = index - 14u;
        const double barrier = autocall_greek_forward(&c, date);
        c.call_barrier[date] = barrier;
        c.coupon_barrier[date] = barrier;
        c.coupon[date] = 2.0;
    }
    return c;
}

static inline autocall_frozen_case_t autocall_greek_case(unsigned index)
{
    return index < AUTOCALL_GREEK_CORE_CASES ? autocall_frozen_cases[index] :
        autocall_greek_stress_case(index - AUTOCALL_GREEK_CORE_CASES);
}

#endif
