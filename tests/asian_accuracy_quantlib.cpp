#include "private/asian_quantlib_accuracy_cases.h"

#include <ql/instruments/asianoption.hpp>
#include <ql/processes/blackscholesprocess.hpp>
#include <ql/pricingengines/asian/analytic_discr_geom_av_price.hpp>
#include <ql/pricingengines/asian/mc_discr_arith_av_price.hpp>
#include <ql/quotes/simplequote.hpp>
#include <ql/settings.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/time/calendars/nullcalendar.hpp>
#include <ql/version.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace QuantLib;

namespace {

const Date reference_date(1, January, 2020);
class FrozenJoeKuoSobolRsg {
  public:
    using sample_type = Sample<std::vector<Real>>;

    explicit FrozenJoeKuoSobolRsg(Size dimensionality,
                                  BigNatural seed = 0)
    : dimensionality_(dimensionality), sequence_counter_(seed),
      sequence_(std::vector<Real>(dimensionality), 1.0),
      directions_(dimensionality) {
        QL_REQUIRE(dimensionality > 0 && dimensionality <= 256,
                   "frozen Joe-Kuo dimensionality out of range");
        QL_REQUIRE(seed <= UINT32_MAX, "frozen Joe-Kuo seed out of range");
        std::ifstream input("direction_numbers/joe_kuo_6_21201.bin",
                            std::ios::binary);
        QL_REQUIRE(input.good(), "cannot open frozen Joe-Kuo directions");
        for (Size dimension = 0; dimension < dimensionality_; ++dimension) {
            uint32_t count = 0;
            input.read(reinterpret_cast<char *>(&count), sizeof(count));
            QL_REQUIRE(input.good() && count == 32u,
                       "invalid frozen Joe-Kuo direction row");
            input.read(reinterpret_cast<char *>(directions_[dimension].data()),
                       static_cast<std::streamsize>(
                           directions_[dimension].size() * sizeof(uint32_t)));
            QL_REQUIRE(input.good(), "truncated frozen Joe-Kuo directions");
        }
    }

    const sample_type& nextSequence() const {
        const uint32_t index = ++sequence_counter_;
        const uint32_t gray = index ^ (index >> 1);
        for (Size dimension = 0; dimension < dimensionality_; ++dimension) {
            uint32_t bits = gray;
            uint32_t word = 0u;
            for (uint32_t column = 0; bits != 0u; ++column, bits >>= 1)
                if ((bits & 1u) != 0u)
                    word ^= directions_[dimension][column];
            sequence_.value[dimension] =
                static_cast<Real>(word) * 0x1p-32;
        }
        sequence_.weight = 1.0;
        return sequence_;
    }

    const sample_type& lastSequence() const { return sequence_; }
    Size dimension() const { return dimensionality_; }

  private:
    Size dimensionality_;
    mutable uint32_t sequence_counter_;
    mutable sample_type sequence_;
    std::vector<std::array<uint32_t, 32>> directions_;
};

using FrozenLowDiscrepancy =
    GenericLowDiscrepancy<FrozenJoeKuoSobolRsg, InverseCumulativeNormal>;

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

ext::shared_ptr<BlackScholesMertonProcess> make_process(
    double s0, double q, double r, double sigma, const DayCounter& dc)
{
    const Handle<Quote> spot(ext::make_shared<SimpleQuote>(s0));
    const Handle<YieldTermStructure> qts(
        ext::make_shared<FlatForward>(reference_date, q, dc));
    const Handle<YieldTermStructure> rts(
        ext::make_shared<FlatForward>(reference_date, r, dc));
    const Handle<BlackVolTermStructure> vol(
        ext::make_shared<BlackConstantVol>(reference_date, NullCalendar(),
                                           sigma, dc));
    return ext::make_shared<BlackScholesMertonProcess>(spot, qts, rts, vol);
}

std::vector<Date> make_dates(uint32_t count, uint32_t first_tick,
                             uint32_t step_tick)
{
    std::vector<Date> dates;
    dates.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        dates.push_back(reference_date +
                        static_cast<Integer>(first_tick + step_tick * i));
    return dates;
}

void require_times(const ext::shared_ptr<BlackScholesMertonProcess>& process,
                   const std::vector<Date>& dates, uint32_t basis,
                   uint32_t first_tick, uint32_t step_tick)
{
    for (uint32_t i = 0; i < dates.size(); ++i) {
        const double got = process->time(dates[i]);
        const double expected = static_cast<double>(first_tick + step_tick * i) /
                                static_cast<double>(basis);
        if (got != expected)
            QL_FAIL("effective fixing-time mismatch at index " << i <<
                    ": " << got << " != " << expected);
    }
}

double option_value(Average::Type average, Option::Type type,
                    double strike, const std::vector<Date>& dates,
                    const ext::shared_ptr<PricingEngine>& engine)
{
    const ext::shared_ptr<StrikedTypePayoff> payoff =
        ext::make_shared<PlainVanillaPayoff>(type, strike);
    const ext::shared_ptr<Exercise> exercise =
        ext::make_shared<EuropeanExercise>(dates.back());
    const double running = average == Average::Geometric ? 1.0 : 0.0;
    DiscreteAveragingAsianOption option(average, running, 0u, dates,
                                        payoff, exercise);
    option.setPricingEngine(engine);
    return option.NPV();
}

void geometric_moments(double s0, double q, double r, double sigma,
                       const std::vector<Date>& dates, const DayCounter& dc,
                       double *mean, double *variance)
{
    const double n = static_cast<double>(dates.size());
    double sum_t = 0.0;
    double sum_min = 0.0;
    std::vector<double> times;
    times.reserve(dates.size());
    for (const Date& date : dates) {
        const double t = dc.yearFraction(reference_date, date);
        times.push_back(t);
        sum_t += t;
    }
    for (double ti : times)
        for (double tj : times)
            sum_min += std::min(ti, tj);
    *mean = std::log(s0) + (r - q - 0.5 * sigma * sigma) * sum_t / n;
    *variance = sigma * sigma * sum_min / (n * n);
}

int analytic_stage()
{
    const ExactBasisDayCounter dc(5);
    const std::vector<Date> dates = make_dates(5u, 1u, 1u);
    const auto process = make_process(100.0, 0.0, 0.03, 0.20, dc);
    require_times(process, dates, 5u, 1u, 1u);
    double mean = 0.0;
    double variance = 0.0;
    geometric_moments(100.0, 0.0, 0.03, 0.20, dates, dc,
                      &mean, &variance);
    const ext::shared_ptr<PricingEngine> engine =
        ext::make_shared<AnalyticDiscreteGeometricAveragePriceAsianEngine>(
            process);
    for (Option::Type type : {Option::Call, Option::Put}) {
        const double price = option_value(Average::Geometric, type, 100.0,
                                          dates, engine);
        std::printf("QL_ANALYTIC option=%s basis=5 first_tick=1 step_tick=1 "
                    "n=5 s0=%a strike=%a q=%a r=%a sigma=%a "
                    "price=%a mean=%a variance=%a times_exact=YES\n",
                    type == Option::Call ? "call" : "put",
                    100.0, 100.0, 0.0, 0.03, 0.20,
                    price, mean, variance);
    }
    return 0;
}

int published_stage()
{
    for (const auto& c : asian_accuracy_published_cases) {
        const uint32_t basis = asian_accuracy_schedule_basis(c.fixings);
        const uint32_t first_tick = asian_accuracy_schedule_first_tick(&c);
        const ExactBasisDayCounter dc(static_cast<Integer>(basis));
        const std::vector<Date> dates =
            make_dates(c.fixings, first_tick, 11u);
        const auto process = make_process(c.s0, c.dividend_yield, c.rate,
                                          c.sigma, dc);
        require_times(process, dates, basis, first_tick, 11u);
        const ext::shared_ptr<PricingEngine> engine =
            MakeMCDiscreteArithmeticAPEngine<FrozenLowDiscrepancy>(process)
                .withSamples(2047u)
                .withSeed(0u)
                .withBrownianBridge(true)
                .withControlVariate(true);
        const double price = option_value(Average::Arithmetic, Option::Put,
                                          c.strike, dates, engine);
        std::printf("QL_PUBLISHED id=%s first=%u n=%u basis=%u "
                    "first_tick=%u step_tick=11 s0=%a strike=%a q=%a r=%a "
                    "sigma=%a published=%a quantlib=%a times_exact=YES\n",
                    c.id, c.first_twelfths, c.fixings, basis, first_tick,
                    c.s0, c.strike, c.dividend_yield, c.rate, c.sigma,
                    c.published_price, price);
    }
    return 0;
}

int n64_replication_stage(uint32_t samples, uint32_t level,
                          uint32_t replication)
{
    QL_REQUIRE(replication < ASIAN_ACCURACY_REFERENCE_REPLICATIONS,
               "N64 replication out of range");
    const ExactBasisDayCounter dc(64);
    const std::vector<Date> dates = make_dates(64u, 1u, 1u);
    const auto process = make_process(100.0, 0.0, 0.03, 0.20, dc);
    require_times(process, dates, 64u, 1u, 1u);
    const uint32_t seed = asian_accuracy_reference_seed(level, replication);
    double values[2] = {0.0, 0.0};
    for (uint32_t option = 0; option < 2u; ++option) {
        const ext::shared_ptr<PricingEngine> engine =
            MakeMCDiscreteArithmeticAPEngine<PseudoRandom>(process)
                .withSamples(samples)
                .withSeed(seed)
                .withBrownianBridge(true)
                .withAntitheticVariate(true)
                .withControlVariate(true);
        values[option] = option_value(
            Average::Arithmetic,
            option == 0u ? Option::Call : Option::Put,
            100.0, dates, engine);
    }
    std::printf("QL_N64_REP samples=%u level=%u replication=%u seed=%u "
                "basis=64 first_tick=1 step_tick=1 n=64 "
                "s0=%a strike=%a q=%a r=%a sigma=%a call=%a put=%a "
                "times_exact=YES bridge=YES antithetic=YES control=YES\n",
                samples, level, replication, seed,
                100.0, 100.0, 0.0, 0.03, 0.20, values[0], values[1]);
    return 0;
}

uint32_t parse_uint(const char *text)
{
    errno = 0;
    char *end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX)
        QL_FAIL("invalid unsigned integer: " << text);
    return static_cast<uint32_t>(value);
}

} // namespace

int main(int argc, char **argv)
{
    try {
        Settings::instance().evaluationDate() = reference_date;
        if (argc == 3 && std::strcmp(argv[1], "--stage") == 0) {
            if (std::strcmp(argv[2], "version") == 0) {
                std::printf("QL_VERSION version=%s\n", QL_VERSION);
                return 0;
            }
            if (std::strcmp(argv[2], "analytic") == 0)
                return analytic_stage();
            if (std::strcmp(argv[2], "published") == 0)
                return published_stage();
        }
        if (argc == 9 && std::strcmp(argv[1], "--stage") == 0 &&
            std::strcmp(argv[2], "n64") == 0 &&
            std::strcmp(argv[3], "--samples") == 0 &&
            std::strcmp(argv[5], "--level") == 0 &&
            std::strcmp(argv[7], "--replication") == 0)
            return n64_replication_stage(parse_uint(argv[4]),
                                         parse_uint(argv[6]),
                                         parse_uint(argv[8]));
        std::fprintf(stderr,
            "usage: %s --stage version|analytic|published\n"
            "       %s --stage n64 --samples N --level I --replication R\n",
            argv[0], argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "QuantLib accuracy failure: %s\n", error.what());
        return 1;
    }
}
