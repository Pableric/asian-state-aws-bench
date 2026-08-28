#include "autocall_two_asset_three_date_worstof_cases.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static autocall_worstof_case_t frozen_cases[AUTOCALL_WORSTOF_CASES];
static char stress_names[AUTOCALL_WORSTOF_STRESS_CASES][48];
static int frozen_cases_ready;

static autocall_worstof_contract_input_t base_contract(void) {
    autocall_worstof_contract_input_t c = {0};
    c.spot_a = 100.0;
    c.spot_b = 100.0;
    c.notional = 100.0;
    c.call_barrier[0] = 1.10;
    c.call_barrier[1] = 1.05;
    c.call_barrier[2] = 1.00;
    c.coupon_barrier[0] = 0.80;
    c.coupon_barrier[1] = 0.80;
    c.coupon_barrier[2] = 0.80;
    c.coupon_cashflow[0] = 1.0;
    c.coupon_cashflow[1] = 2.0;
    c.coupon_cashflow[2] = 3.0;
    c.call_redemption[0] = 100.0;
    c.call_redemption[1] = 101.0;
    c.call_redemption[2] = 102.0;
    c.protection_barrier = 0.65;
    c.coupon_payment_time[0] = 1.0 / 3.0;
    c.coupon_payment_time[1] = 2.0 / 3.0;
    c.coupon_payment_time[2] = 1.0;
    c.call_payment_time[0] = 1.0 / 3.0;
    c.call_payment_time[1] = 2.0 / 3.0;
    c.call_payment_time[2] = 1.0;
    c.terminal_payment_time = 1.0;
    return c;
}

static autocall_worstof_market_input_t base_market(void) {
    autocall_worstof_market_input_t m = {0};
    m.rate = 0.03;
    m.maturity = 1.0;
    m.dividend_a = 0.01;
    m.sigma_a = 0.20;
    m.dividend_b = 0.01;
    m.sigma_b = 0.20;
    m.rho = 0.50;
    return m;
}

static void set_case(unsigned i, const char *name,
                     autocall_worstof_market_input_t m,
                     autocall_worstof_contract_input_t c) {
    frozen_cases[i].name = name;
    frozen_cases[i].stress = 0;
    frozen_cases[i].market = m;
    frozen_cases[i].contract = c;
}

static double standardized_barrier(const autocall_worstof_market_input_t *m,
                                   int weak_asset, unsigned date,
                                   double sign) {
    const double t = m->maturity * (double)(date + 1u) / 3.0;
    const double q = weak_asset == 0 ? m->dividend_a : m->dividend_b;
    const double sigma = weak_asset == 0 ? m->sigma_a : m->sigma_b;
    return exp((m->rate - q) * t + sign * 0.25 * sigma * sqrt(t));
}

static double inactive_upper_barrier(const autocall_worstof_market_input_t *m,
                                     int asset, unsigned date) {
    const double t = m->maturity * (double)(date + 1u) / 3.0;
    const double q = asset == 0 ? m->dividend_a : m->dividend_b;
    const double sigma = asset == 0 ? m->sigma_a : m->sigma_b;
    /* 8.5 marginal standard deviations: crossing probability < 1e-17. */
    return exp((m->rate - q) * t + 8.5 * sigma * sqrt(t));
}

static void freeze_core_cases(void) {
    autocall_worstof_market_input_t m = base_market();
    autocall_worstof_contract_input_t c = base_contract();
    set_case(0, "SYMMETRIC_NORMAL", m, c);

    m.rho = 0.90;
    set_case(1, "HIGH_CORRELATION", m, c);

    m = base_market();
    m.rho = 0.0;
    set_case(2, "INDEPENDENT_ASSETS", m, c);

    m.rho = -0.50;
    set_case(3, "NEGATIVE_CORRELATION", m, c);

    m = base_market();
    m.dividend_a = 0.08;
    m.sigma_a = 0.25;
    m.dividend_b = 0.0;
    m.sigma_b = 0.15;
    set_case(4, "WEAK_ASSET_A", m, c);

    m.dividend_a = 0.0;
    m.sigma_a = 0.15;
    m.dividend_b = 0.08;
    m.sigma_b = 0.25;
    set_case(5, "WEAK_ASSET_B", m, c);

    m = base_market();
    m.dividend_a = 0.02;
    m.sigma_a = 0.55;
    m.dividend_b = 0.01;
    m.sigma_b = 0.45;
    m.rho = 0.25;
    c = base_contract();
    c.call_barrier[0] = 1.60;
    c.call_barrier[1] = 1.50;
    c.call_barrier[2] = 1.40;
    c.coupon_barrier[0] = 1.20;
    c.coupon_barrier[1] = 1.10;
    c.coupon_barrier[2] = 1.00;
    c.coupon_cashflow[0] = 0.0;
    c.coupon_cashflow[1] = 0.0;
    c.coupon_cashflow[2] = 0.0;
    c.call_redemption[0] = 100.0;
    c.call_redemption[1] = 100.0;
    c.call_redemption[2] = 100.0;
    c.protection_barrier = 0.80;
    set_case(6, "HIGH_VOLATILITY_DOWNSIDE", m, c);

    m = base_market();
    c = base_contract();
    c.call_barrier[0] = 0.85;
    c.call_barrier[1] = 0.85;
    c.call_barrier[2] = 0.85;
    c.coupon_barrier[0] = 0.80;
    c.coupon_barrier[1] = 0.80;
    c.coupon_barrier[2] = 0.80;
    c.coupon_cashflow[0] = 2.0;
    c.coupon_cashflow[1] = 2.0;
    c.coupon_cashflow[2] = 2.0;
    set_case(7, "EARLY_CALL_DOMINATED", m, c);

    m.sigma_a = 0.35;
    m.sigma_b = 0.30;
    m.rho = 0.25;
    c = base_contract();
    c.call_barrier[0] = 1.00;
    c.call_barrier[1] = 0.95;
    c.call_barrier[2] = 0.90;
    c.coupon_barrier[0] = 1.20;
    c.coupon_barrier[1] = 1.15;
    c.coupon_barrier[2] = 1.10;
    c.coupon_cashflow[0] = 3.0;
    c.coupon_cashflow[1] = 4.0;
    c.coupon_cashflow[2] = 5.0;
    set_case(8, "COUPON_ABOVE_CALL", m, c);

    m = base_market();
    c = base_contract();
    c.call_barrier[0] = 1.15;
    c.call_barrier[1] = 1.10;
    c.call_barrier[2] = 1.05;
    c.coupon_barrier[0] = 0.95;
    c.coupon_barrier[1] = 0.95;
    c.coupon_barrier[2] = 0.95;
    c.coupon_cashflow[0] = 0.5;
    c.coupon_cashflow[1] = 1.0;
    c.coupon_cashflow[2] = 1.5;
    for (unsigned d = 0; d < 3; ++d) {
        c.coupon_payment_time[d] += 0.02;
        c.call_payment_time[d] += 0.02;
    }
    c.terminal_payment_time += 0.02;
    set_case(9, "LAGGED_PAYMENTS", m, c);
}

static void freeze_stress_cases(void) {
    static const char *event_name[7] = {
        "CALL_D1", "CALL_D2", "CALL_D3", "COUPON_D1",
        "COUPON_D2", "COUPON_D3", "PROTECTION",
    };
    unsigned out = AUTOCALL_WORSTOF_CORE_CASES;
    for (unsigned event = 0; event < 7; ++event) {
        for (unsigned weak = 0; weak < 2; ++weak) {
            for (unsigned side = 0; side < 2; ++side) {
                autocall_worstof_market_input_t m = base_market();
                autocall_worstof_contract_input_t c = base_contract();
                m.rate = 0.02;
                m.rho = 0.50;
                if (weak == 0) {
                    m.dividend_a = 0.06;
                    m.sigma_a = 0.30;
                    m.dividend_b = -0.01;
                    m.sigma_b = 0.12;
                } else {
                    m.dividend_a = -0.01;
                    m.sigma_a = 0.12;
                    m.dividend_b = 0.06;
                    m.sigma_b = 0.30;
                }
                for (unsigned d = 0; d < 3; ++d) {
                    const double hi_a = inactive_upper_barrier(&m, 0, d);
                    const double hi_b = inactive_upper_barrier(&m, 1, d);
                    c.call_barrier[d] = hi_a > hi_b ? hi_a : hi_b;
                    c.coupon_barrier[d] = c.call_barrier[d];
                    c.coupon_cashflow[d] = 0.0;
                    c.call_redemption[d] = 100.0;
                }
                c.protection_barrier = 0.65;
                if (event < 3) {
                    c.call_barrier[event] = standardized_barrier(
                        &m, (int)weak, event, side == 0 ? -1.0 : 1.0);
                } else if (event < 6) {
                    const unsigned d = event - 3;
                    c.coupon_barrier[d] = standardized_barrier(
                        &m, (int)weak, d, side == 0 ? -1.0 : 1.0);
                    c.coupon_cashflow[d] = 2.0;
                } else {
                    c.protection_barrier = standardized_barrier(
                        &m, (int)weak, 2, side == 0 ? -1.0 : 1.0);
                }
                const unsigned s = out - AUTOCALL_WORSTOF_CORE_CASES;
                (void)snprintf(stress_names[s], sizeof(stress_names[s]),
                               "%s_WEAK_%c_%s", event_name[event],
                               weak == 0 ? 'A' : 'B',
                               side == 0 ? "MINUS" : "PLUS");
                frozen_cases[out].name = stress_names[s];
                frozen_cases[out].stress = 1;
                frozen_cases[out].market = m;
                frozen_cases[out].contract = c;
                ++out;
            }
        }
    }
}

const autocall_worstof_case_t *autocall_worstof_cases(void) {
    if (!frozen_cases_ready) {
        freeze_core_cases();
        freeze_stress_cases();
        frozen_cases_ready = 1;
    }
    return frozen_cases;
}

int autocall_worstof_native_case_index(unsigned ordinal) {
    static const int frozen_native_cases[5] = {0, 1, 3, 4, 6};
    return ordinal < 5 ? frozen_native_cases[ordinal] : -1;
}
