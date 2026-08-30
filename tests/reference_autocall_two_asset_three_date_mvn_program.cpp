#include "tests/autocall_two_asset_three_date_mvn_program_cases.h"
#include "tests/autocall_two_asset_three_date_mvn_reference_math.h"
#include "tests/autocall_two_asset_three_date_worstof_cases.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string_view>
#include <vector>

namespace mvn_program {

constexpr std::uint64_t PROGRAM_MAGIC = UINT64_C(0x4d564e5032365239);
constexpr double SPOT_BUMP = 0x1.47ae147ae147bp-7; /* 0.01 */
constexpr double VOL_BUMP = 0x1.47ae147ae147bp-7;  /* 0.01 */

static std::uint64_t cdf_invocations;
static std::uint64_t graph_invocations;
static std::uint64_t template_invocations;

static std::uint64_t bits(double x) {
    return std::bit_cast<std::uint64_t>(x);
}

static bool finite_positive(double x) {
    return std::isfinite(x) && x > 0.0;
}

struct PreparedTopology {
    double protection_level{};
    std::array<double, 2> absolute_protection{};
};

static mvn_program_status_t validate_topology(
    const mvn_program_market_input_t &market,
    const mvn_program_contract_input_t &contract,
    PreparedTopology *out) {
    const double h0 = contract.normalized_protection_barrier[0];
    const double h1 = contract.normalized_protection_barrier[1];
    if (!finite_positive(h0) || !finite_positive(h1) || bits(h0) != bits(h1))
        return UNSUPPORTED_MVN_TOPOLOGY;
    if (out != nullptr) {
        out->protection_level = h0;
        out->absolute_protection[0] = market.spot[0] * h0;
        out->absolute_protection[1] = market.spot[1] * h0;
        if (!finite_positive(out->absolute_protection[0]) ||
            !finite_positive(out->absolute_protection[1]))
            return UNSUPPORTED_MVN_TOPOLOGY;
        for (unsigned asset=0;asset<2u;++asset) {
            const double normalized_call=
                contract.call_barrier[2][asset]/market.spot[asset];
            if (!std::isfinite(normalized_call) || normalized_call<h0)
                return UNSUPPORTED_MVN_TOPOLOGY;
        }
    } else {
        for (unsigned asset=0;asset<2u;++asset) {
            const double normalized_call=
                contract.call_barrier[2][asset]/market.spot[asset];
            if (!std::isfinite(normalized_call) || normalized_call<h0)
                return UNSUPPORTED_MVN_TOPOLOGY;
        }
    }
    return MVN_PROGRAM_OK;
}

static mvn_program_status_t validate_ordinary_inputs(
    const mvn_program_market_input_t &m,
    const mvn_program_contract_input_t &c) {
    for (unsigned a = 0; a < 2; ++a) {
        if (!finite_positive(m.spot[a]) || !finite_positive(m.volatility[a]) ||
            !std::isfinite(m.dividend[a]))
            return MVN_PROGRAM_INVALID_INPUT;
    }
    if (!std::isfinite(m.rate) || !std::isfinite(m.correlation) ||
        m.correlation < -1.0 || m.correlation > 1.0 ||
        !finite_positive(c.notional))
        return MVN_PROGRAM_INVALID_INPUT;
    double previous = 0.0;
    for (unsigned d = 0; d < 3; ++d) {
        if (!finite_positive(m.observation_time[d]) ||
            !(m.observation_time[d] > previous))
            return MVN_PROGRAM_INVALID_INPUT;
        previous = m.observation_time[d];
        if (!std::isfinite(c.coupon_cashflow[d]) ||
            c.coupon_cashflow[d] < 0.0 ||
            !finite_positive(c.call_redemption[d]) ||
            !std::isfinite(c.coupon_payment_time[d]) ||
            !std::isfinite(c.call_payment_time[d]) ||
            c.coupon_payment_time[d] < m.observation_time[d] ||
            c.call_payment_time[d] < m.observation_time[d])
            return MVN_PROGRAM_INVALID_INPUT;
        for (unsigned a = 0; a < 2; ++a) {
            if (!finite_positive(c.call_barrier[d][a]) ||
                !finite_positive(c.coupon_barrier[d][a]))
                return MVN_PROGRAM_INVALID_INPUT;
        }
    }
    if (!std::isfinite(c.terminal_payment_time) ||
        c.terminal_payment_time < m.observation_time[2])
        return MVN_PROGRAM_INVALID_INPUT;
    return MVN_PROGRAM_OK;
}

struct LogicalNode {
    mvn_probability_node_kind_t kind{};
    std::uint8_t terminal_date{};
    std::uint8_t included_call_mask{};
    std::uint8_t selected_asset{};
    std::int8_t sign{};
    std::uint8_t dimension{};
    double coefficient{};
};

struct LogicalProgram {
    std::array<LogicalNode, MVN_PROGRAM_LOGICAL_NODES> node{};
    std::size_t count{};
    std::array<unsigned, 7> dimension_count{};
};

struct ProgramEvaluation {
    long double price{};
    long double independent_price{};
    long double disjoint_price{};
    long double representation_difference{};
    long double disjoint_certificate{};
    long double minimal_kind[5]{};
    long double disjoint_kind[5]{};
    long double propagated_error{};
    long double representation_condition{};
    std::uint64_t cdf_count{};
    unsigned fallback_count{};
    bool converged{true};
};

struct ProbabilityCacheKey {
    unsigned dimension{};
    std::array<long double,6> lower{};
    std::array<long double,6> upper{};
    std::array<long double,36> covariance{};
    long double requested_error{};
    bool operator<(const ProbabilityCacheKey &other) const {
        if (dimension!=other.dimension) return dimension<other.dimension;
        if (lower!=other.lower) return lower<other.lower;
        if (upper!=other.upper) return upper<other.upper;
        if (covariance!=other.covariance) return covariance<other.covariance;
        return requested_error<other.requested_error;
    }
};

static std::map<ProbabilityCacheKey,mvn_reference_math::Evaluation>
    probability_cache;

static mvn_reference_math::Evaluation evaluate_cached(
    const mvn_reference_math::Rectangle &rectangle,long double requested_error) {
    const ProbabilityCacheKey key{rectangle.dimension,rectangle.lower,
        rectangle.upper,rectangle.covariance,requested_error};
    const auto found=probability_cache.find(key);
    if (found!=probability_cache.end()) return found->second;
    const auto value=mvn_reference_math::evaluate(rectangle,requested_error);
    ++cdf_invocations;
    probability_cache.emplace(key,value);
    return value;
}

struct CompensatedSum {
    long double sum{};
    long double correction{};
    void add(long double value) {
        const long double updated=sum+value;
        if (std::fabs(sum)>=std::fabs(value))
            correction+=(sum-updated)+value;
        else
            correction+=(value-updated)+sum;
        sum=updated;
    }
    long double value() const { return sum+correction; }
};

struct DerivedRisk {
    long double delta[2]{};
    long double gamma[2]{};
    long double vega[2]{};
};

static unsigned popcount3(unsigned x) {
    return static_cast<unsigned>(__builtin_popcount(x & 7u));
}

static void append_node(LogicalProgram &program, LogicalNode node) {
    if (program.count >= program.node.size()) {
        std::fprintf(stderr, "logical program overflow\n");
        std::abort();
    }
    program.dimension_count[node.dimension] += 1u;
    program.node[program.count++] = node;
}

static void append_prior_inclusion(LogicalProgram &program,
                                   mvn_probability_node_kind_t kind,
                                   unsigned terminal_date,
                                   unsigned selected_asset,
                                   double coefficient) {
    const unsigned prior_mask = (1u << terminal_date) - 1u;
    for (unsigned subset = prior_mask;; subset = (subset - 1u) & prior_mask) {
        const int sign = (popcount3(subset) & 1u) ? -1 : 1;
        const unsigned dates = popcount3(subset | (1u << terminal_date));
        append_node(program, LogicalNode{
            kind,
            static_cast<std::uint8_t>(terminal_date),
            static_cast<std::uint8_t>(subset),
            static_cast<std::uint8_t>(selected_asset),
            static_cast<std::int8_t>(sign),
            static_cast<std::uint8_t>(2u * dates),
            coefficient,
        });
        if (subset == 0u)
            break;
    }
}

static LogicalProgram build_program(const mvn_program_market_input_t &market,
                                    const mvn_program_contract_input_t &contract,
                                    mvn_program_status_t *status) {
    LogicalProgram program;
    PreparedTopology topology;
    *status = validate_ordinary_inputs(market, contract);
    if (*status != MVN_PROGRAM_OK)
        return program;
    *status = validate_topology(market, contract, &topology);
    if (*status != MVN_PROGRAM_OK)
        return program;

    ++graph_invocations;
    const double terminal = contract.notional *
        std::exp(-market.rate * contract.terminal_payment_time);
    for (unsigned date = 0; date < 3; ++date) {
        double redemption = contract.call_redemption[date] *
            std::exp(-market.rate * contract.call_payment_time[date]);
        if (date == 2u)
            redemption -= terminal;
        append_prior_inclusion(program, MVN_NODE_CALL, date, 0u, redemption);
        const double coupon = contract.coupon_cashflow[date] *
            std::exp(-market.rate * contract.coupon_payment_time[date]);
        append_prior_inclusion(program, MVN_NODE_COUPON, date, 0u, coupon);
    }
    append_prior_inclusion(program, MVN_NODE_PROTECTED, 2u, 0u, terminal);
    append_prior_inclusion(program, MVN_NODE_DOWNSIDE_A, 2u, 0u, terminal);
    append_prior_inclusion(program, MVN_NODE_DOWNSIDE_B, 2u, 1u, terminal);

    if (program.count != MVN_PROGRAM_LOGICAL_NODES ||
        program.dimension_count[2] != 9u ||
        program.dimension_count[4] != 12u ||
        program.dimension_count[6] != 5u) {
        std::fprintf(stderr,
                     "logical count mismatch total=%zu phi2=%u phi4=%u phi6=%u\n",
                     program.count, program.dimension_count[2],
                     program.dimension_count[4], program.dimension_count[6]);
        std::abort();
    }
    return program;
}

static long double asset_correlation(const mvn_program_market_input_t &market,
                                     unsigned asset0, unsigned asset1) {
    return asset0 == asset1 ? 1.0L :
        static_cast<long double>(market.correlation);
}

static long double time_correlation(long double t0, long double t1) {
    return std::min(t0, t1) / std::sqrt(t0 * t1);
}

struct Form {
    unsigned date{};
    long double coefficient[2]{};
};

static long double form_covariance(const Form &a, const Form &b,
                                   const mvn_program_market_input_t &market) {
    long double sum = 0.0L;
    for (unsigned i = 0; i < 2; ++i) {
        for (unsigned j = 0; j < 2; ++j)
            sum += a.coefficient[i] * b.coefficient[j] *
                asset_correlation(market, i, j);
    }
    return sum * time_correlation(market.observation_time[a.date],
                                  market.observation_time[b.date]);
}

static long double drift(const mvn_program_market_input_t &market,
                         unsigned asset) {
    const long double sigma = market.volatility[asset];
    return static_cast<long double>(market.rate - market.dividend[asset]) -
        0.5L * sigma * sigma;
}

static long double standardized_threshold(
    const mvn_program_market_input_t &market, unsigned date, unsigned asset,
    long double normalized_barrier) {
    const long double t = market.observation_time[date];
    const long double sigma = market.volatility[asset];
    return (std::log(normalized_barrier / market.spot[asset]) -
            drift(market, asset) * t) /
        (sigma * std::sqrt(t));
}

static long double standardized_normalized_threshold(
    const mvn_program_market_input_t &market, unsigned date, unsigned asset,
    long double normalized_barrier) {
    const long double t = market.observation_time[date];
    const long double sigma = market.volatility[asset];
    return (std::log(normalized_barrier) - drift(market, asset) * t) /
        (sigma * std::sqrt(t));
}

static void apply_lower(long double value, long double &lower) {
    lower = std::max(lower, value);
}

static void apply_upper(long double value, long double &upper) {
    upper = std::min(upper, value);
}

static bool build_singular_rectangle(
    const mvn_program_market_input_t &market,
    const mvn_program_contract_input_t &contract, const LogicalNode &node,
    mvn_reference_math::Rectangle &rectangle, long double &tilt_prefactor) {
    const long double sign_b = market.correlation > 0.0 ? 1.0L : -1.0L;
    std::array<unsigned, 3> dates{};
    unsigned groups = 0u;
    for (unsigned d = 0; d <= node.terminal_date; ++d) {
        if ((node.included_call_mask & (1u << d)) != 0u ||
            d == node.terminal_date)
            dates[groups++] = d;
    }
    rectangle.dimension = 2u * groups;
    std::array<long double, 3> mean_shift{};
    tilt_prefactor = 1.0L;
    if (node.kind == MVN_NODE_DOWNSIDE_A || node.kind == MVN_NODE_DOWNSIDE_B) {
        const unsigned selected = node.kind == MVN_NODE_DOWNSIDE_A ? 0u : 1u;
        const long double selected_sign = selected == 0u ? 1.0L : sign_b;
        const long double terminal_time = market.observation_time[2];
        const long double sigma = market.volatility[selected];
        tilt_prefactor = std::exp(drift(market, selected) * terminal_time +
                                  0.5L * sigma * sigma * terminal_time);
        for (unsigned g = 0; g < groups; ++g)
            mean_shift[g] = sigma * selected_sign *
                std::sqrt(static_cast<long double>(market.observation_time[dates[g]]));
    }
    for (unsigned g = 0; g < groups; ++g) {
        const unsigned d = dates[g];
        const unsigned offset = 2u * g;
        rectangle.lower[offset] = -std::numeric_limits<long double>::infinity();
        rectangle.upper[offset] = std::numeric_limits<long double>::infinity();
        rectangle.lower[offset + 1u] = -std::numeric_limits<long double>::infinity();
        rectangle.upper[offset + 1u] = std::numeric_limits<long double>::infinity();
        if (d < node.terminal_date ||
            (node.included_call_mask & (1u << d)) != 0u) {
            const long double a = standardized_threshold(
                market, d, 0u, contract.call_barrier[d][0]);
            const long double b = standardized_threshold(
                market, d, 1u, contract.call_barrier[d][1]);
            apply_lower(a, rectangle.lower[offset]);
            if (sign_b > 0.0L)
                apply_lower(b, rectangle.lower[offset]);
            else
                apply_upper(-b, rectangle.upper[offset]);
        }
        if (d == node.terminal_date) {
            if (node.kind == MVN_NODE_CALL || node.kind == MVN_NODE_COUPON ||
                node.kind == MVN_NODE_PROTECTED) {
                for (unsigned asset = 0; asset < 2; ++asset) {
                    long double barrier = 0.0L;
                    if (node.kind == MVN_NODE_CALL)
                        barrier = contract.call_barrier[d][asset];
                    else if (node.kind == MVN_NODE_COUPON)
                        barrier = contract.coupon_barrier[d][asset];
                    else
                        barrier = contract.normalized_protection_barrier[0];
                    const long double threshold = node.kind == MVN_NODE_PROTECTED ?
                        standardized_normalized_threshold(market, d, asset,
                                                          barrier) :
                        standardized_threshold(market, d, asset, barrier);
                    if (asset == 0u || sign_b > 0.0L)
                        apply_lower(asset == 0u ? threshold : threshold,
                                    rectangle.lower[offset]);
                    else
                        apply_upper(-threshold, rectangle.upper[offset]);
                }
            } else {
                const unsigned selected = node.kind == MVN_NODE_DOWNSIDE_A ? 0u : 1u;
                const unsigned other = 1u - selected;
                const long double t = market.observation_time[d];
                const long double selected_sign = selected == 0u ? 1.0L : sign_b;
                const long double other_sign = other == 0u ? 1.0L : sign_b;
                const long double selected_sigma = market.volatility[selected];
                const long double other_sigma = market.volatility[other];
                const long double k_selected = selected_sigma * selected_sign * std::sqrt(t);
                const long double k_other = other_sigma * other_sign * std::sqrt(t);
                const long double protection_log =
                    std::log(contract.normalized_protection_barrier[0]);
                const long double protection_bound =
                    (protection_log - drift(market, selected) * t) / k_selected;
                if (k_selected > 0.0L)
                    apply_upper(protection_bound, rectangle.upper[offset]);
                else
                    apply_lower(protection_bound, rectangle.lower[offset]);
                const long double difference = k_other - k_selected;
                const long double drift_difference =
                    (drift(market, other) - drift(market, selected)) * t;
                if (difference == 0.0L) {
                    if (drift_difference < 0.0L)
                        return false;
                } else {
                    const long double crossing = -drift_difference / difference;
                    if (difference > 0.0L)
                        apply_lower(crossing, rectangle.lower[offset]);
                    else
                        apply_upper(crossing, rectangle.upper[offset]);
                }
            }
        }
        rectangle.lower[offset] -= mean_shift[g];
        rectangle.upper[offset] -= mean_shift[g];
    }
    for (unsigned i = 0; i < groups; ++i) {
        for (unsigned j = 0; j < groups; ++j) {
            const long double corr = time_correlation(
                market.observation_time[dates[i]], market.observation_time[dates[j]]);
            rectangle.covariance[(2u * i) * 6u + 2u * j] = corr;
            rectangle.covariance[(2u * i + 1u) * 6u + 2u * j + 1u] =
                i == j ? 1.0L : 0.0L;
        }
    }
    return true;
}

static bool build_rectangle(const mvn_program_market_input_t &market,
                            const mvn_program_contract_input_t &contract,
                            const LogicalNode &node,
                            mvn_reference_math::Rectangle &rectangle,
                            long double &tilt_prefactor) {
    ++template_invocations;
    if (market.correlation == 1.0 || market.correlation == -1.0)
        return build_singular_rectangle(market, contract, node, rectangle,
                                        tilt_prefactor);
    std::array<unsigned, 3> dates{};
    unsigned groups = 0u;
    for (unsigned d = 0; d <= node.terminal_date; ++d) {
        if ((node.included_call_mask & (1u << d)) != 0u ||
            d == node.terminal_date)
            dates[groups++] = d;
    }
    rectangle.dimension = 2u * groups;
    std::array<Form, 6> form{};
    tilt_prefactor = 1.0L;
    unsigned selected = 0u;
    const bool downside = node.kind == MVN_NODE_DOWNSIDE_A ||
                          node.kind == MVN_NODE_DOWNSIDE_B;
    if (downside) {
        selected = node.kind == MVN_NODE_DOWNSIDE_A ? 0u : 1u;
        const long double t = market.observation_time[2];
        const long double sigma = market.volatility[selected];
        tilt_prefactor = std::exp(drift(market, selected) * t +
                                  0.5L * sigma * sigma * t);
    }
    for (unsigned g = 0; g < groups; ++g) {
        const unsigned d = dates[g];
        const unsigned offset = 2u * g;
        form[offset].date = d;
        form[offset + 1u].date = d;
        form[offset].coefficient[0] = 1.0L;
        form[offset + 1u].coefficient[1] = 1.0L;
        if (downside && d == node.terminal_date) {
            const unsigned other = 1u - selected;
            form[offset].coefficient[0] = selected == 0u ? 1.0L : 0.0L;
            form[offset].coefficient[1] = selected == 1u ? 1.0L : 0.0L;
            const long double sigma_selected = market.volatility[selected];
            const long double sigma_other = market.volatility[other];
            long double raw[2]{};
            raw[other] = sigma_other;
            raw[selected] = -sigma_selected;
            const long double variance = raw[0] * raw[0] + raw[1] * raw[1] +
                2.0L * market.correlation * raw[0] * raw[1];
            if (!(variance > 0.0L))
                return build_singular_rectangle(market, contract, node,
                                                rectangle, tilt_prefactor);
            const long double inv_sd = 1.0L / std::sqrt(variance);
            form[offset + 1u].coefficient[0] = raw[0] * inv_sd;
            form[offset + 1u].coefficient[1] = raw[1] * inv_sd;
        }
        rectangle.lower[offset] = -std::numeric_limits<long double>::infinity();
        rectangle.upper[offset] = std::numeric_limits<long double>::infinity();
        rectangle.lower[offset + 1u] = -std::numeric_limits<long double>::infinity();
        rectangle.upper[offset + 1u] = std::numeric_limits<long double>::infinity();
        if (d < node.terminal_date ||
            (node.included_call_mask & (1u << d)) != 0u) {
            rectangle.lower[offset] = standardized_threshold(
                market, d, 0u, contract.call_barrier[d][0]);
            rectangle.lower[offset + 1u] = standardized_threshold(
                market, d, 1u, contract.call_barrier[d][1]);
        }
        if (d == node.terminal_date) {
            if (node.kind == MVN_NODE_CALL || node.kind == MVN_NODE_COUPON ||
                node.kind == MVN_NODE_PROTECTED) {
                for (unsigned asset = 0; asset < 2u; ++asset) {
                    const long double barrier = node.kind == MVN_NODE_CALL ?
                        contract.call_barrier[d][asset] :
                        (node.kind == MVN_NODE_COUPON ?
                            contract.coupon_barrier[d][asset] :
                            contract.normalized_protection_barrier[0]);
                    rectangle.lower[offset + asset] = std::max(
                        rectangle.lower[offset + asset],
                        node.kind == MVN_NODE_PROTECTED ?
                            standardized_normalized_threshold(market, d, asset,
                                                              barrier) :
                            standardized_threshold(market, d, asset, barrier));
                }
            } else {
                const unsigned other = 1u - selected;
                rectangle.upper[offset] = standardized_normalized_threshold(
                    market, d, selected,
                    contract.normalized_protection_barrier[0]);
                const long double t = market.observation_time[d];
                const long double difference_drift =
                    (drift(market, other) - drift(market, selected)) * t;
                const long double variance = form_covariance(
                    form[offset + 1u], form[offset + 1u], market);
                rectangle.lower[offset + 1u] =
                    -difference_drift / (std::sqrt(t) * std::sqrt(variance));
            }
        }
    }
    for (unsigned i = 0; i < rectangle.dimension; ++i) {
        for (unsigned j = 0; j < rectangle.dimension; ++j)
            rectangle.covariance[i * 6u + j] =
                form_covariance(form[i], form[j], market);
    }
    if (downside) {
        const long double terminal_time = market.observation_time[2];
        Form tilt_form{};
        tilt_form.date = 2u;
        tilt_form.coefficient[selected] = 1.0L;
        const long double tilt_scale =
            market.volatility[selected] * std::sqrt(terminal_time);
        for (unsigned i = 0; i < rectangle.dimension; ++i) {
            const long double shift =
                form_covariance(form[i], tilt_form, market) * tilt_scale;
            rectangle.lower[i] -= shift;
            rectangle.upper[i] -= shift;
        }
    }
    return true;
}

static long double downside_prior_shift(
    const mvn_program_market_input_t &market,const LogicalNode &node,
    unsigned date,unsigned asset) {
    if (node.kind!=MVN_NODE_DOWNSIDE_A && node.kind!=MVN_NODE_DOWNSIDE_B)
        return 0.0L;
    const unsigned selected=node.kind==MVN_NODE_DOWNSIDE_A ? 0u : 1u;
    return market.volatility[selected]*std::sqrt(
        static_cast<long double>(market.observation_time[date]))*
        asset_correlation(market,asset,selected);
}

static void apply_disjoint_no_call(
    const mvn_program_market_input_t &market,
    const mvn_program_contract_input_t &contract,const LogicalNode &node,
    unsigned date,unsigned choice,mvn_reference_math::Rectangle &rectangle) {
    const unsigned offset=2u*date;
    const long double ta=standardized_threshold(
        market,date,0u,contract.call_barrier[date][0]);
    const long double tb=standardized_threshold(
        market,date,1u,contract.call_barrier[date][1]);
    if (market.correlation==1.0 || market.correlation==-1.0) {
        const long double selected_shift=(node.kind==MVN_NODE_DOWNSIDE_A ||
            node.kind==MVN_NODE_DOWNSIDE_B) ?
            market.volatility[node.kind==MVN_NODE_DOWNSIDE_A ? 0u : 1u]*
            ((node.kind==MVN_NODE_DOWNSIDE_B && market.correlation<0.0) ?
                -1.0L : 1.0L)*std::sqrt(
                    static_cast<long double>(market.observation_time[date])) :
            0.0L;
        rectangle.lower[offset]=-
            std::numeric_limits<long double>::infinity();
        rectangle.upper[offset]=
            std::numeric_limits<long double>::infinity();
        if (choice==0u)
            rectangle.upper[offset]=ta-selected_shift;
        else if (market.correlation>0.0) {
            rectangle.lower[offset]=ta-selected_shift;
            rectangle.upper[offset]=tb-selected_shift;
        } else {
            rectangle.lower[offset]=std::max(ta,-tb)-selected_shift;
        }
        rectangle.lower[offset+1u]=-
            std::numeric_limits<long double>::infinity();
        rectangle.upper[offset+1u]=
            std::numeric_limits<long double>::infinity();
        return;
    }
    rectangle.lower[offset]=-
        std::numeric_limits<long double>::infinity();
    rectangle.upper[offset]=
        std::numeric_limits<long double>::infinity();
    rectangle.lower[offset+1u]=-
        std::numeric_limits<long double>::infinity();
    rectangle.upper[offset+1u]=
        std::numeric_limits<long double>::infinity();
    const long double shifted_a=ta-downside_prior_shift(market,node,date,0u);
    const long double shifted_b=tb-downside_prior_shift(market,node,date,1u);
    if (choice==0u)
        rectangle.upper[offset]=shifted_a;
    else {
        rectangle.lower[offset]=shifted_a;
        rectangle.upper[offset+1u]=shifted_b;
    }
}

struct DisjointEvaluation {
    long double price{};
    long double propagated_error{};
    std::uint64_t cdf_count{};
    bool finite{true};
    long double kind[5]{};
};

static DisjointEvaluation evaluate_disjoint_program(
    const mvn_program_market_input_t &market,
    const mvn_program_contract_input_t &contract,
    long double requested_price_error) {
    DisjointEvaluation out;
    CompensatedSum total;
    std::array<CompensatedSum,5> kind_total{};
    /*
     * The complete graph certificate below remains authoritative.  This is
     * a bounded initial error budget, not an acceptance relaxation: every
     * weighted node error is propagated and the price is rejected if the
     * resulting sum exceeds requested_price_error.
     */
    /* Positive-region reconstruction is cancellation-sensitive by design. */
    const long double per_node_error=requested_price_error/
        (96.0L*std::max(1.0,contract.notional));
    const auto evaluate_partition=[&](LogicalNode node,unsigned choices,
                                      unsigned no_call_dates,
                                      long double coefficient) {
        for (unsigned choice_mask=0;choice_mask<choices;++choice_mask) {
            mvn_reference_math::Rectangle rectangle;
            long double tilt=1.0L;
            if (!build_rectangle(market,contract,node,rectangle,tilt)) {
                out.finite=false;
                continue;
            }
            const unsigned regular_no_call_dates=
                node.kind==MVN_NODE_PROTECTED && no_call_dates==3u ? 2u :
                no_call_dates;
            for (unsigned date=0;date<regular_no_call_dates;++date)
                apply_disjoint_no_call(market,contract,node,date,
                    (choice_mask>>date)&1u,rectangle);
            if (node.kind==MVN_NODE_PROTECTED && no_call_dates==3u) {
                const unsigned offset=4u;
                const unsigned d3_choice=(choice_mask>>2u)&1u;
                const long double ta=standardized_threshold(
                    market,2u,0u,contract.call_barrier[2][0]);
                const long double tb=standardized_threshold(
                    market,2u,1u,contract.call_barrier[2][1]);
                if (market.correlation!=1.0 && market.correlation!=-1.0) {
                    if (d3_choice==0u)
                        rectangle.upper[offset]=ta;
                    else {
                        rectangle.lower[offset]=std::max(
                            rectangle.lower[offset],ta);
                        rectangle.upper[offset+1u]=tb;
                    }
                } else {
                    apply_disjoint_no_call(market,contract,node,2u,d3_choice,
                                           rectangle);
                }
            }
            const auto probability=evaluate_cached(rectangle,per_node_error);
            const long double contribution=coefficient*tilt*probability.primary;
            total.add(contribution);
            kind_total[static_cast<unsigned>(node.kind)].add(contribution);
            out.propagated_error+=std::fabs(coefficient*tilt)*
                (probability.paired_error+probability.independent_error+
                 probability.mass_error);
            out.cdf_count+=probability.cdf_count;
            out.finite=out.finite && probability.finite;
        }
    };
    for (unsigned date=0;date<3u;++date) {
        const unsigned prior_mask=(1u<<date)-1u;
        const unsigned choices=1u<<date;
        const long double redemption=contract.call_redemption[date]*
            std::exp(-market.rate*contract.call_payment_time[date]);
        evaluate_partition(LogicalNode{MVN_NODE_CALL,
            static_cast<std::uint8_t>(date),static_cast<std::uint8_t>(prior_mask),
            0u,1,static_cast<std::uint8_t>(2u*(date+1u)),
            static_cast<double>(redemption)},choices,date,redemption);
        const long double coupon=contract.coupon_cashflow[date]*
            std::exp(-market.rate*contract.coupon_payment_time[date]);
        evaluate_partition(LogicalNode{MVN_NODE_COUPON,
            static_cast<std::uint8_t>(date),static_cast<std::uint8_t>(prior_mask),
            0u,1,static_cast<std::uint8_t>(2u*(date+1u)),
            static_cast<double>(coupon)},choices,date,coupon);
    }
    const long double terminal=contract.notional*
        std::exp(-market.rate*contract.terminal_payment_time);
    evaluate_partition(LogicalNode{MVN_NODE_PROTECTED,2u,3u,0u,1,6u,
        static_cast<double>(terminal)},8u,3u,terminal);
    for (unsigned selected=0;selected<2u;++selected)
        evaluate_partition(LogicalNode{selected==0u ? MVN_NODE_DOWNSIDE_A :
            MVN_NODE_DOWNSIDE_B,2u,3u,static_cast<std::uint8_t>(selected),
            1,6u,static_cast<double>(terminal)},4u,2u,terminal);
    out.price=total.value();
    for (unsigned i=0;i<5u;++i)
        out.kind[i]=kind_total[i].value();
    return out;
}

static ProgramEvaluation evaluate_program(
    const mvn_program_market_input_t &market,
    const mvn_program_contract_input_t &contract,
    const LogicalProgram &program, long double requested_price_error) {
    ProgramEvaluation out;
    CompensatedSum positive;
    CompensatedSum negative;
    CompensatedSum independent_positive;
    CompensatedSum independent_negative;
    std::array<CompensatedSum,5> minimal_kind{};
    probability_cache.clear();
    const long double per_node_error = requested_price_error /
        std::max(1.0, contract.notional);
    for (std::size_t i = 0; i < program.count; ++i) {
        mvn_reference_math::Rectangle rectangle;
        long double tilt = 1.0L;
        if (!build_rectangle(market, contract, program.node[i], rectangle, tilt)) {
            out.converged = false;
            continue;
        }
        const mvn_reference_math::Evaluation probability =
            evaluate_cached(rectangle, per_node_error);
        const long double weight = program.node[i].coefficient * tilt;
        const long double term = weight * probability.primary;
        const long double independent_term = weight * probability.independent;
        minimal_kind[static_cast<unsigned>(program.node[i].kind)].add(
            program.node[i].sign>0 ? term : -term);
        if (std::fabs(probability.primary - probability.independent) > 1.0e-7L) {
            std::printf("REFERENCE_NODE index=%zu kind=%u dim=%u mask=%u sign=%d "
                        "primary=%.17Lg independent=%.17Lg difference=%.9Lg "
                        "order=%u converged=%s\n",
                        i, static_cast<unsigned>(program.node[i].kind),
                        static_cast<unsigned>(program.node[i].dimension),
                        static_cast<unsigned>(program.node[i].included_call_mask),
                        static_cast<int>(program.node[i].sign),
                        probability.primary, probability.independent,
                        probability.primary - probability.independent,
                        probability.primary_order,
                        probability.converged ? "YES" : "NO");
        }
        if (program.node[i].sign > 0) {
            positive.add(term);
            independent_positive.add(independent_term);
        } else {
            negative.add(term);
            independent_negative.add(independent_term);
        }
        out.propagated_error += std::fabs(weight) *
            (probability.paired_error + probability.independent_error +
             probability.mass_error);
        out.cdf_count += probability.cdf_count;
        if (!probability.converged)
            ++out.fallback_count;
        out.converged = out.converged && probability.finite;
    }
    out.price = positive.value() - negative.value();
    out.independent_price = independent_positive.value() -
        independent_negative.value();
    const DisjointEvaluation disjoint=evaluate_disjoint_program(
        market,contract,requested_price_error);
    out.disjoint_price=disjoint.price;
    out.representation_difference=std::fabs(out.price-out.disjoint_price);
    out.disjoint_certificate=disjoint.propagated_error;
    for (unsigned i=0;i<5u;++i) {
        out.minimal_kind[i]=minimal_kind[i].value();
        out.disjoint_kind[i]=disjoint.kind[i];
    }
    out.cdf_count+=disjoint.cdf_count;
    out.converged=out.converged && disjoint.finite;
    out.representation_condition =
        (std::fabs(positive.value()) + std::fabs(negative.value())) /
        std::max(std::fabs(out.price), 1.0e-30L);
    out.converged = out.converged &&
        out.propagated_error <= requested_price_error;
    return out;
}

static mvn_program_case_t convert_parent_case(
    const autocall_worstof_case_t &source) {
    mvn_program_case_t out{};
    out.name = source.name;
    out.stress = static_cast<std::uint32_t>(source.stress != 0);
    out.market.spot[0] = source.contract.spot_a;
    out.market.spot[1] = source.contract.spot_b;
    out.market.volatility[0] = source.market.sigma_a;
    out.market.volatility[1] = source.market.sigma_b;
    out.market.dividend[0] = source.market.dividend_a;
    out.market.dividend[1] = source.market.dividend_b;
    out.market.rate = source.market.rate;
    out.market.correlation = source.market.rho;
    out.contract.notional = source.contract.notional;
    for (unsigned d = 0; d < 3; ++d) {
        out.market.observation_time[d] = source.market.maturity *
            static_cast<double>(d + 1u) / 3.0;
        for (unsigned a = 0; a < 2; ++a) {
            const double spot = a == 0u ? source.contract.spot_a :
                                          source.contract.spot_b;
            out.contract.call_barrier[d][a] =
                spot * source.contract.call_barrier[d];
            out.contract.coupon_barrier[d][a] =
                spot * source.contract.coupon_barrier[d];
        }
        out.contract.coupon_cashflow[d] = source.contract.coupon_cashflow[d];
        out.contract.call_redemption[d] = source.contract.call_redemption[d];
        out.contract.coupon_payment_time[d] =
            source.contract.coupon_payment_time[d];
        out.contract.call_payment_time[d] =
            source.contract.call_payment_time[d];
    }
    out.contract.normalized_protection_barrier[0] =
        source.contract.protection_barrier;
    out.contract.normalized_protection_barrier[1] =
        source.contract.protection_barrier;
    out.contract.terminal_payment_time = source.contract.terminal_payment_time;
    return out;
}

static int test_topology_boundary(void) {
    const autocall_worstof_case_t *parent = autocall_worstof_cases();
    mvn_program_case_t base = convert_parent_case(parent[0]);
    mvn_program_status_t status = MVN_PROGRAM_OK;

    const auto run = [&](mvn_program_case_t fixture,
                         mvn_program_status_t expected) -> bool {
        const std::uint64_t cdf_before = cdf_invocations;
        const std::uint64_t graph_before = graph_invocations;
        const std::uint64_t template_before = template_invocations;
        const LogicalProgram program = build_program(
            fixture.market, fixture.contract, &status);
        const bool rejected = expected != MVN_PROGRAM_OK;
        const bool no_work = !rejected ||
            (cdf_invocations == cdf_before && graph_invocations == graph_before &&
             template_invocations == template_before && program.count == 0u);
        return status == expected && no_work;
    };

    base.contract.call_barrier[2][0] =
        std::nextafter(base.market.spot[0] *
                           base.contract.normalized_protection_barrier[0],
                       std::numeric_limits<double>::infinity());
    base.contract.call_barrier[2][1] =
        std::nextafter(base.market.spot[1] *
                           base.contract.normalized_protection_barrier[0],
                       std::numeric_limits<double>::infinity());
    if (!run(base, MVN_PROGRAM_OK))
        return 1;

    base.contract.call_barrier[2][0] =
        base.market.spot[0] * base.contract.normalized_protection_barrier[0];
    base.contract.call_barrier[2][1] =
        base.market.spot[1] * base.contract.normalized_protection_barrier[0];
    if (!run(base, MVN_PROGRAM_OK))
        return 2;

    base.contract.call_barrier[2][0] = base.market.spot[0]*std::nextafter(
        base.contract.normalized_protection_barrier[0],0.0);
    if (!run(base, UNSUPPORTED_MVN_TOPOLOGY))
        return 3;

    base = convert_parent_case(parent[0]);
    base.contract.call_barrier[2][1] = base.market.spot[1]*std::nextafter(
        base.contract.normalized_protection_barrier[0],0.0);
    if (!run(base, UNSUPPORTED_MVN_TOPOLOGY))
        return 4;

    base = convert_parent_case(parent[0]);
    base.contract.normalized_protection_barrier[1] = std::nextafter(
        base.contract.normalized_protection_barrier[0],
        std::numeric_limits<double>::infinity());
    if (!run(base, UNSUPPORTED_MVN_TOPOLOGY))
        return 5;

    base = convert_parent_case(parent[0]);
    base.market.spot[0] = 83.0;
    base.market.spot[1] = 127.0;
    base.contract.call_barrier[2][0] = base.market.spot[0];
    base.contract.call_barrier[2][1] = base.market.spot[1];
    if (!run(base, MVN_PROGRAM_OK))
        return 6;

    base.contract.normalized_protection_barrier[0] =
        std::numeric_limits<double>::quiet_NaN();
    base.contract.normalized_protection_barrier[1] =
        std::numeric_limits<double>::quiet_NaN();
    if (!run(base, UNSUPPORTED_MVN_TOPOLOGY))
        return 7;
    return 0;
}

static void print_layouts(void) {
#define MVN_FIELD(type,field) \
    std::printf("TYPE_FIELD name=%s field=%s offset=%zu\n",#type,#field, \
                offsetof(type,field))
    std::printf("TYPE_LAYOUT name=CovarianceTemplate size=%zu align=%zu\n",
                sizeof(mvn_covariance_template_t),
                alignof(mvn_covariance_template_t));
    std::printf("TYPE_LAYOUT name=BoundInstance size=%zu align=%zu\n",
                sizeof(mvn_bound_instance_t), alignof(mvn_bound_instance_t));
    std::printf("TYPE_LAYOUT name=TiltFamily size=%zu align=%zu\n",
                sizeof(mvn_tilt_family_t), alignof(mvn_tilt_family_t));
    std::printf("TYPE_LAYOUT name=ProbabilityNode size=%zu align=%zu\n",
                sizeof(mvn_probability_node_t), alignof(mvn_probability_node_t));
    std::printf("TYPE_LAYOUT name=LinearCombinationNode size=%zu align=%zu\n",
                sizeof(mvn_linear_combination_node_t),
                alignof(mvn_linear_combination_node_t));
    std::printf("TYPE_LAYOUT name=PreparedProbabilityBatch size=%zu align=%zu\n",
                sizeof(mvn_prepared_probability_batch_t),
                alignof(mvn_prepared_probability_batch_t));
    std::printf("TYPE_LAYOUT name=PreparedGaussianProgram size=%zu align=%zu\n",
                sizeof(mvn_prepared_gaussian_program_t),
                alignof(mvn_prepared_gaussian_program_t));
    std::printf("TYPE_LAYOUT name=PriceOutput size=%zu align=%zu\n",
                sizeof(mvn_price_output_t),alignof(mvn_price_output_t));
    std::printf("TYPE_LAYOUT name=RiskScenarioOutput size=%zu align=%zu\n",
                sizeof(mvn_risk_scenario_output_t),
                alignof(mvn_risk_scenario_output_t));
    MVN_FIELD(mvn_covariance_template_t,id);
    MVN_FIELD(mvn_covariance_template_t,covariance);
    MVN_FIELD(mvn_covariance_template_t,factor);
    MVN_FIELD(mvn_covariance_template_t,conditional_variance);
    MVN_FIELD(mvn_covariance_template_t,partial_correlation);
    MVN_FIELD(mvn_covariance_template_t,identity);
    MVN_FIELD(mvn_bound_instance_t,template_id);
    MVN_FIELD(mvn_bound_instance_t,lower);
    MVN_FIELD(mvn_bound_instance_t,upper);
    MVN_FIELD(mvn_bound_instance_t,mean_shift);
    MVN_FIELD(mvn_bound_instance_t,identity);
    MVN_FIELD(mvn_tilt_family_t,id);
    MVN_FIELD(mvn_tilt_family_t,exponential_prefactor);
    MVN_FIELD(mvn_tilt_family_t,mean_shift);
    MVN_FIELD(mvn_probability_node_t,bound_id);
    MVN_FIELD(mvn_probability_node_t,coefficient);
    MVN_FIELD(mvn_probability_node_t,certified_abs_error);
    MVN_FIELD(mvn_linear_combination_node_t,first_node);
    MVN_FIELD(mvn_linear_combination_node_t,constant);
    MVN_FIELD(mvn_lowered_batch_t,first_task);
    MVN_FIELD(mvn_lowered_batch_t,identity);
    MVN_FIELD(mvn_prepared_probability_batch_t,lower);
    MVN_FIELD(mvn_prepared_probability_batch_t,upper);
    MVN_FIELD(mvn_prepared_probability_batch_t,coefficient);
    MVN_FIELD(mvn_prepared_probability_batch_t,identity);
    MVN_FIELD(mvn_prepared_gaussian_program_t,templates);
    MVN_FIELD(mvn_prepared_gaussian_program_t,tilts);
    MVN_FIELD(mvn_prepared_gaussian_program_t,nodes);
    MVN_FIELD(mvn_prepared_gaussian_program_t,reductions);
    MVN_FIELD(mvn_prepared_gaussian_program_t,schedule);
    MVN_FIELD(mvn_prepared_gaussian_program_t,tasks);
    MVN_FIELD(mvn_prepared_gaussian_program_t,template_count);
    MVN_FIELD(mvn_price_output_t,price);
    MVN_FIELD(mvn_price_output_t,status);
    MVN_FIELD(mvn_risk_scenario_output_t,price);
    MVN_FIELD(mvn_risk_scenario_output_t,delta);
    MVN_FIELD(mvn_risk_scenario_output_t,status);
#undef MVN_FIELD
}

static mvn_program_case_t scenario_case(const mvn_program_case_t &base,
                                        unsigned scenario) {
    mvn_program_case_t out = base;
    if (scenario == MVN_SCENARIO_SPOT_A_MINUS)
        out.market.spot[0] = base.market.spot[0] * (1.0 - SPOT_BUMP);
    else if (scenario == MVN_SCENARIO_SPOT_A_PLUS)
        out.market.spot[0] = base.market.spot[0] * (1.0 + SPOT_BUMP);
    else if (scenario == MVN_SCENARIO_SPOT_B_MINUS)
        out.market.spot[1] = base.market.spot[1] * (1.0 - SPOT_BUMP);
    else if (scenario == MVN_SCENARIO_SPOT_B_PLUS)
        out.market.spot[1] = base.market.spot[1] * (1.0 + SPOT_BUMP);
    else if (scenario == MVN_SCENARIO_VOL_A_MINUS)
        out.market.volatility[0] = base.market.volatility[0] - VOL_BUMP;
    else if (scenario == MVN_SCENARIO_VOL_A_PLUS)
        out.market.volatility[0] = base.market.volatility[0] + VOL_BUMP;
    else if (scenario == MVN_SCENARIO_VOL_B_MINUS)
        out.market.volatility[1] = base.market.volatility[1] - VOL_BUMP;
    else if (scenario == MVN_SCENARIO_VOL_B_PLUS)
        out.market.volatility[1] = base.market.volatility[1] + VOL_BUMP;
    return out;
}

static bool run_reference_smoke(void) {
    std::size_t count = 0u;
    const mvn_program_case_t *cases = mvn_selection_product_cases(&count);
    if (count != MVN_SELECTION_PRODUCT_CASES)
        return false;
    const mvn_program_case_t fixture = scenario_case(cases[0], MVN_SCENARIO_BASE);
    mvn_program_status_t status = MVN_PROGRAM_OK;
    const LogicalProgram program = build_program(
        fixture.market, fixture.contract, &status);
    if (status != MVN_PROGRAM_OK)
        return false;
    const ProgramEvaluation value = evaluate_program(
        fixture.market, fixture.contract, program, 5.0e-6L);
    std::printf("REFERENCE_SMOKE case=%s scenario=BASE price=%.17Lg "
                "independent=%.17Lg disjoint=%.17Lg abs_difference=%.9Lg "
                "representation_difference=%.9Lg propagated=%.9Lg "
                "disjoint_certificate=%.9Lg "
                "condition=%.9Lg cdf_count=%llu converged=%s\n",
                fixture.name, value.price, value.independent_price,
                value.disjoint_price,
                std::fabs(value.price - value.independent_price),
                value.representation_difference,
                value.propagated_error,value.disjoint_certificate,
                value.representation_condition,
                static_cast<unsigned long long>(value.cdf_count),
                value.converged ? "YES" : "NO");
    for (unsigned kind=0;kind<5u;++kind)
        std::printf("REFERENCE_DECOMPOSITION kind=%u minimal=%.17Lg "
                    "disjoint=%.17Lg difference=%.9Lg\n",kind,
                    value.minimal_kind[kind],value.disjoint_kind[kind],
                    value.minimal_kind[kind]-value.disjoint_kind[kind]);
    return value.converged && std::isfinite(value.price) &&
        std::fabs(value.price - value.independent_price) <= 5.0e-6L &&
        value.representation_difference <= 2.0e-5L &&
        value.propagated_error <= 5.0e-6L;
}

static long double derivative_first(long double minus, long double base,
                                    long double plus, long double dm,
                                    long double dp) {
    return -dp / (dm * (dm + dp)) * minus +
        (dp - dm) / (dm * dp) * base +
        dm / (dp * (dm + dp)) * plus;
}

static long double derivative_second(long double minus, long double base,
                                     long double plus, long double dm,
                                     long double dp) {
    return 2.0L / (dm + dp) *
        (minus / dm - base * (1.0L / dm + 1.0L / dp) + plus / dp);
}

static DerivedRisk derive_risk(const mvn_program_case_t &fixture,
                               const std::array<long double,9> &price) {
    DerivedRisk out;
    for (unsigned asset = 0; asset < 2u; ++asset) {
        const unsigned minus = asset == 0u ? MVN_SCENARIO_SPOT_A_MINUS :
                                             MVN_SCENARIO_SPOT_B_MINUS;
        const unsigned plus = asset == 0u ? MVN_SCENARIO_SPOT_A_PLUS :
                                            MVN_SCENARIO_SPOT_B_PLUS;
        const mvn_program_case_t lo = scenario_case(fixture, minus);
        const mvn_program_case_t hi = scenario_case(fixture, plus);
        const long double dm = fixture.market.spot[asset] - lo.market.spot[asset];
        const long double dp = hi.market.spot[asset] - fixture.market.spot[asset];
        out.delta[asset] = derivative_first(price[minus], price[0], price[plus],
                                            dm, dp);
        out.gamma[asset] = derivative_second(price[minus], price[0], price[plus],
                                             dm, dp);
        const unsigned vminus = asset == 0u ? MVN_SCENARIO_VOL_A_MINUS :
                                              MVN_SCENARIO_VOL_B_MINUS;
        const unsigned vplus = asset == 0u ? MVN_SCENARIO_VOL_A_PLUS :
                                             MVN_SCENARIO_VOL_B_PLUS;
        const mvn_program_case_t vlo = scenario_case(fixture, vminus);
        const mvn_program_case_t vhi = scenario_case(fixture, vplus);
        const long double vm = fixture.market.volatility[asset] -
            vlo.market.volatility[asset];
        const long double vp = vhi.market.volatility[asset] -
            fixture.market.volatility[asset];
        out.vega[asset] = derivative_first(price[vminus], price[0], price[vplus],
                                           vm, vp);
    }
    return out;
}

static bool run_selection_reference(unsigned first, unsigned requested_count,
                                    bool risk_scenarios) {
    std::size_t case_count = 0u;
    const mvn_program_case_t *cases = mvn_selection_product_cases(&case_count);
    const std::size_t end = std::min<std::size_t>(
        case_count, static_cast<std::size_t>(first) + requested_count);
    bool all_pass = first < end;
    for (std::size_t c = first; c < end; ++c) {
        std::array<long double,9> primary{};
        std::array<long double,9> independent{};
        long double max_price_difference = 0.0L;
        long double max_representation_difference = 0.0L;
        long double max_propagated = 0.0L;
        bool case_converged = true;
        const unsigned scenario_count=risk_scenarios ? 9u : 1u;
        for (unsigned scenario = 0; scenario < scenario_count; ++scenario) {
            const mvn_program_case_t fixture = scenario_case(cases[c], scenario);
            mvn_program_status_t status = MVN_PROGRAM_OK;
            const LogicalProgram program = build_program(
                fixture.market, fixture.contract, &status);
            if (status != MVN_PROGRAM_OK) {
                case_converged = false;
                continue;
            }
            const long double reference_cap = fixture.stress ? 2.0e-5L : 5.0e-6L;
            const ProgramEvaluation value = evaluate_program(
                fixture.market, fixture.contract, program, reference_cap);
            primary[scenario] = value.price;
            independent[scenario] = value.independent_price;
            max_price_difference = std::max(max_price_difference,
                std::fabs(value.price - value.independent_price));
            max_representation_difference=std::max(
                max_representation_difference,value.representation_difference);
            max_propagated = std::max(max_propagated, value.propagated_error);
            case_converged = case_converged && value.converged;
        }
        const DerivedRisk risk = risk_scenarios ?
            derive_risk(cases[c], primary) : DerivedRisk{};
        const DerivedRisk risk_independent = risk_scenarios ?
            derive_risk(cases[c], independent) : DerivedRisk{};
        long double max_risk_bp = 0.0L;
        for (unsigned asset = 0; asset < 2u; ++asset) {
            const long double delta_pnl =
                (risk.delta[asset] - risk_independent.delta[asset]) *
                (0.01L * cases[c].market.spot[asset]);
            const long double gamma_pnl = 0.5L *
                (risk.gamma[asset] - risk_independent.gamma[asset]) *
                std::pow(0.01L * cases[c].market.spot[asset], 2);
            const long double vega_pnl =
                (risk.vega[asset] - risk_independent.vega[asset]) * 0.01L;
            for (long double pnl : {delta_pnl, gamma_pnl, vega_pnl})
                max_risk_bp = std::max(max_risk_bp,
                    std::fabs(pnl) / cases[c].contract.notional * 10000.0L);
        }
        const long double price_bp = max_price_difference /
            cases[c].contract.notional * 10000.0L;
        const long double propagated_bp = max_propagated /
            cases[c].contract.notional * 10000.0L;
        const long double representation_bp=max_representation_difference/
            cases[c].contract.notional*10000.0L;
        const long double price_gate = cases[c].stress ? 0.020L : 0.005L;
        const long double risk_gate = cases[c].stress ? 0.10L : 0.025L;
        const long double reference_gate = cases[c].stress ? 0.002L : 0.0005L;
        const long double representation_gate=cases[c].stress ? 0.010L : 0.002L;
        const bool pass = case_converged && price_bp <= price_gate &&
            representation_bp<=representation_gate &&
            (!risk_scenarios || max_risk_bp <= risk_gate) &&
            propagated_bp <= reference_gate;
        all_pass = all_pass && pass;
        std::printf("SELECTION_REFERENCE case=%s class=%s base=%.17Lg "
                    "independent=%.17Lg price_difference_bp=%.9Lg "
                    "representation_bp=%.9Lg propagated_bp=%.9Lg "
                    "max_risk_pnl_bp=%.9Lg "
                    "delta_a=%.17Lg gamma_a=%.17Lg vega_a=%.17Lg "
                    "delta_b=%.17Lg gamma_b=%.17Lg vega_b=%.17Lg status=%s\n",
                    cases[c].name, cases[c].stress ? "STRESS" : "CORE",
                    primary[0], independent[0], price_bp, representation_bp,
                    propagated_bp,max_risk_bp, risk.delta[0], risk.gamma[0], risk.vega[0],
                    risk.delta[1], risk.gamma[1], risk.vega[1],
                    pass ? "PASS" : "FAIL");
    }
    std::printf("SELECTION_REFERENCE_SUMMARY first=%u cases=%zu expected=%u "
                "risk_scenarios=%s status=%s\n", first, end - first,
                requested_count,risk_scenarios?"YES":"NO",
                all_pass ? "PASS" : "FAIL");
    return all_pass;
}

static int run_reference_stage(void) {
    const int topology = test_topology_boundary();
    if (topology != 0) {
        std::fprintf(stderr, "topology boundary test failed case=%d\n", topology);
        return 1;
    }
    print_layouts();
    std::printf("TOPOLOGY_GATE status=PASS logical=26 phi2=9 phi4=12 phi6=5 "
                "common_protection=YES d3_nested=YES rejection_before_graph=YES\n");
    if (!run_reference_smoke()) {
        std::printf("MVN_REFERENCE_NOT_QUALIFIED reason=reference_smoke\n");
        return 2;
    }
    std::printf("REFERENCE_STAGE_SMOKE status=PASS\n");
    return 0;
}

} // namespace mvn_program

int main(int argc, char **argv) {
    std::setvbuf(stdout,nullptr,_IOLBF,0);
    std::string_view mode = argc > 1 ? argv[1] : "--reference";
    if (mode == "--reference")
        return mvn_program::run_reference_stage();
    if (mode == "--manifest-integrity")
        return mvn_verify_manifest_integrity() ? 0 : 1;
    if (mode == "--selection-smoke")
        return mvn_program::run_selection_reference(1u, 1u, false) ? 0 : 1;
    if (mode == "--selection")
        return mvn_program::run_selection_reference(
            0u, MVN_SELECTION_PRODUCT_CASES, true) ? 0 : 1;
    if (mode == "--selection-range" && argc == 4) {
        char *end0=nullptr;
        char *end1=nullptr;
        const unsigned long first=std::strtoul(argv[2],&end0,10);
        const unsigned long count=std::strtoul(argv[3],&end1,10);
        if (end0==argv[2] || *end0!='\0' || end1==argv[3] || *end1!='\0' ||
            first>MVN_SELECTION_PRODUCT_CASES || count>MVN_SELECTION_PRODUCT_CASES)
            return 2;
        return mvn_program::run_selection_reference(
            static_cast<unsigned>(first),static_cast<unsigned>(count),true) ? 0 : 1;
    }
    std::fprintf(stderr, "unsupported mode: %.*s\n",
                 static_cast<int>(mode.size()), mode.data());
    return 2;
}
