#include "private/asian_quantlib_accuracy_cases.h"

#include <ql/instruments/asianoption.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/pricingengines/asian/mc_discr_arith_av_price.hpp>
#include <ql/pricingengines/asian/analytic_discr_geom_av_price.hpp>
#include <ql/math/randomnumbers/rngtraits.hpp>
#include <ql/methods/montecarlo/brownianbridge.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/settings.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/version.hpp>

#include <cerrno>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace QuantLib;

namespace {

const Date reference_date(1, January, 2020);

class ExactBasisDayCounter final : public DayCounter {
  private:
    class ExactImpl final : public DayCounter::Impl {
      public:
        explicit ExactImpl(Integer basis)
        : basis_(basis), name_("ExactTicks/" + std::to_string(basis)) {}
        std::string name() const override { return name_; }
        Time yearFraction(const Date& d1, const Date& d2,
                          const Date&, const Date&) const override {
            return static_cast<Time>(d2 - d1) /
                   static_cast<Time>(basis_);
        }
      private:
        Integer basis_;
        std::string name_;
    };
  public:
    explicit ExactBasisDayCounter(Integer basis)
    : DayCounter(ext::make_shared<ExactImpl>(basis)) {}
};

ext::shared_ptr<BlackScholesMertonProcess> process(double s0,
                                                   const DayCounter& dc)
{
    const Handle<Quote> spot(ext::make_shared<SimpleQuote>(s0));
    const Handle<YieldTermStructure> q(
        ext::make_shared<FlatForward>(reference_date, 0.0, dc));
    const Handle<YieldTermStructure> r(
        ext::make_shared<FlatForward>(reference_date, 0.03, dc));
    const Handle<BlackVolTermStructure> vol(
        ext::make_shared<BlackConstantVol>(reference_date, NullCalendar(),
                                           0.20, dc));
    return ext::make_shared<BlackScholesMertonProcess>(spot, q, r, vol);
}

double analytic_geometric(double s0, Option::Type side,
                          const DayCounter& dc,
                          const std::vector<Date>& dates)
{
    const auto p = process(s0, dc);
    for (uint32_t fixing = 0; fixing < 64u; ++fixing)
        QL_REQUIRE(p->time(dates[fixing]) ==
                   static_cast<double>(fixing + 1u) / 64.0,
                   "N64 fixing-time mismatch");
    const ext::shared_ptr<PricingEngine> engine =
        ext::make_shared<AnalyticDiscreteGeometricAveragePriceAsianEngine>(p);
    const ext::shared_ptr<StrikedTypePayoff> payoff =
        ext::make_shared<PlainVanillaPayoff>(side, 100.0);
    const ext::shared_ptr<Exercise> exercise =
        ext::make_shared<EuropeanExercise>(dates.back());
    DiscreteAveragingAsianOption option(Average::Geometric, 1.0, 0u,
                                        dates, payoff, exercise);
    option.setPricingEngine(engine);
    return option.NPV();
}

uint32_t parse_uint(const char *text)
{
    errno = 0;
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    QL_REQUIRE(errno == 0 && end != text && *end == '\0' &&
               value <= UINT32_MAX, "invalid integer");
    return static_cast<uint32_t>(value);
}

double parse_fraction(const char *text)
{
    errno = 0;
    char *end = nullptr;
    const double value = std::strtod(text, &end);
    QL_REQUIRE(errno == 0 && end != text && *end == '\0' &&
               (value == 0.005 || value == 0.01 || value == 0.02),
               "unsupported bump fraction");
    return value;
}

int replication(uint32_t samples, uint32_t level, uint32_t replication_id,
                double fraction)
{
    QL_REQUIRE(replication_id < ASIAN_ACCURACY_REFERENCE_REPLICATIONS,
               "replication out of range");
    const ExactBasisDayCounter dc(64);
    std::vector<Date> dates;
    dates.reserve(64u);
    for (uint32_t fixing = 1u; fixing <= 64u; ++fixing)
        dates.push_back(reference_date + static_cast<Integer>(fixing));
    const uint32_t seed =
        asian_accuracy_reference_seed(level, replication_id);
    const float effective_bump = (float)(fraction * 100.0);
    const double h = static_cast<double>(effective_bump);
    const double spots[3] = {100.0 - h, 100.0, 100.0 + h};
    double geometric_exact[2][3];
    for (uint32_t side = 0; side < 2u; ++side)
        for (uint32_t spot = 0; spot < 3u; ++spot)
            geometric_exact[side][spot] = analytic_geometric(
                spots[spot], side == 0u ? Option::Call : Option::Put,
                dc, dates);

    std::vector<Time> times(64u);
    for (uint32_t fixing = 0; fixing < 64u; ++fixing)
        times[fixing] = static_cast<double>(fixing + 1u) / 64.0;
    BrownianBridge bridge(times);
    PseudoRandom::rsg_type generator =
        PseudoRandom::make_sequence_generator(64u, seed);
    std::vector<Real> increments(64u);
    long double sums[2][3] = {{0.0L, 0.0L, 0.0L},
                              {0.0L, 0.0L, 0.0L}};
    const double dt = 1.0 / 64.0;
    const double drift = (0.03 - 0.5 * 0.20 * 0.20) * dt;
    const double diffusion = 0.20 * std::sqrt(dt);
    const double discount = std::exp(-0.03);
    for (uint32_t sample = 0; sample < samples; ++sample) {
        const auto& sequence = generator.nextSequence();
        bridge.transform(sequence.value.begin(), sequence.value.end(),
                         increments.begin());
        double paired[2][3] = {{0.0, 0.0, 0.0},
                               {0.0, 0.0, 0.0}};
        for (uint32_t antithetic = 0; antithetic < 2u; ++antithetic) {
            const double sign = antithetic == 0u ? 1.0 : -1.0;
            double factor = 1.0;
            double arithmetic_sum = 0.0;
            double log_sum = 0.0;
            for (uint32_t fixing = 0; fixing < 64u; ++fixing) {
                factor *= std::exp(drift +
                                   diffusion * sign * increments[fixing]);
                arithmetic_sum += factor;
                log_sum += std::log(factor);
            }
            const double arithmetic_factor = arithmetic_sum / 64.0;
            const double geometric_factor = std::exp(log_sum / 64.0);
            for (uint32_t spot = 0; spot < 3u; ++spot) {
                const double arithmetic = spots[spot] * arithmetic_factor;
                const double geometric = spots[spot] * geometric_factor;
                const double call_a = std::max(arithmetic - 100.0, 0.0);
                const double call_g = std::max(geometric - 100.0, 0.0);
                const double put_a = std::max(100.0 - arithmetic, 0.0);
                const double put_g = std::max(100.0 - geometric, 0.0);
                paired[0][spot] += 0.5 *
                    (discount * (call_a - call_g) +
                     geometric_exact[0][spot]);
                paired[1][spot] += 0.5 *
                    (discount * (put_a - put_g) +
                     geometric_exact[1][spot]);
            }
        }
        for (uint32_t side = 0; side < 2u; ++side)
            for (uint32_t spot = 0; spot < 3u; ++spot)
                sums[side][spot] += paired[side][spot];
    }
    double values[2][3];
    for (uint32_t side = 0; side < 2u; ++side)
        for (uint32_t spot = 0; spot < 3u; ++spot)
            values[side][spot] = static_cast<double>(
                sums[side][spot] / static_cast<long double>(samples));
    const double inv_h2 = 1.0 / (h * h);
    const double call_gamma =
        ((values[0][2] - 2.0 * values[0][1]) + values[0][0]) * inv_h2;
    const double put_gamma =
        ((values[1][2] - 2.0 * values[1][1]) + values[1][0]) * inv_h2;
    std::printf("QL_GAMMA_REP samples=%u level=%u replication=%u seed=%u "
                "basis=64 first_tick=1 step_tick=1 n=64 s0=%a strike=%a "
                "q=%a r=%a sigma=%a maturity=%a fraction=%a bump=%a "
                "call_minus=%a call_base=%a call_plus=%a "
                "put_minus=%a put_base=%a put_plus=%a call_gamma=%a "
                "put_gamma=%a times_exact=YES bridge=YES antithetic=YES "
                "control=YES engine=QuantLibPseudoRandomCRN\n",
                samples, level, replication_id, seed, 100.0, 100.0, 0.0,
                0.03, 0.20, 1.0, fraction, h,
                values[0][0], values[0][1], values[0][2],
                values[1][0], values[1][1], values[1][2],
                call_gamma, put_gamma);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    try {
        Settings::instance().evaluationDate() = reference_date;
        if (argc == 3 && std::strcmp(argv[1], "--stage") == 0 &&
            std::strcmp(argv[2], "version") == 0) {
            std::printf("QL_VERSION version=%s\n", QL_VERSION);
            return 0;
        }
        if (argc == 11 && std::strcmp(argv[1], "--stage") == 0 &&
            std::strcmp(argv[2], "gamma") == 0 &&
            std::strcmp(argv[3], "--samples") == 0 &&
            std::strcmp(argv[5], "--level") == 0 &&
            std::strcmp(argv[7], "--replication") == 0 &&
            std::strcmp(argv[9], "--fraction") == 0)
            return replication(parse_uint(argv[4]), parse_uint(argv[6]),
                               parse_uint(argv[8]), parse_fraction(argv[10]));
        std::fprintf(stderr,
            "usage: %s --stage version\n"
            "       %s --stage gamma --samples N --level L "
            "--replication R --fraction F\n", argv[0], argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "QuantLib Gamma failure: %s\n", error.what());
        return 1;
    }
}
