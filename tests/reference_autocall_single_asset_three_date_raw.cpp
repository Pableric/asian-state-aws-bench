#include "tests/autocall_single_asset_three_date_cases.h"

#include <boost/math/distributions/normal.hpp>
#include <boost/math/quadrature/gauss_kronrod.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

extern "C" const unsigned char asian_arithmetic_joe_kuo_256_records[];

namespace {

constexpr double SQRT_TWO_PI = 2.506628274631000502415765284811;

double pdf(double x) { return std::exp(-0.5*x*x) / SQRT_TWO_PI; }
double cdf(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }

struct ValueError { double value; double error; };

struct Decomposition {
    std::array<double,3> call{};
    std::array<double,3> coupon{};
    double survival{};
    double protected_probability{};
    double unprotected_contribution{};
    double price{};
    double estimated_error{};
};

struct GaussianOracle {
    const autocall_frozen_case_t &c;
    std::array<double,3> mean{};
    std::array<double,3> variance{};

    explicit GaussianOracle(const autocall_frozen_case_t &input) : c(input)
    {
        const double dt = c.maturity / 3.0;
        const double drift = c.rate - c.dividend - 0.5*c.sigma*c.sigma;
        for (unsigned i=0;i<3;++i) {
            const double time = dt*(i+1.0);
            mean[i] = std::log(c.s0) + drift*time;
            variance[i] = c.sigma*c.sigma*time;
        }
    }

    double univariate_below(unsigned i, double upper,
                            const std::array<double,3> &mu) const
    {
        if (std::isinf(upper)) return upper > 0.0 ? 1.0 : 0.0;
        return cdf((upper-mu[i])/std::sqrt(variance[i]));
    }

    ValueError rect3(double upper1, double upper2, double upper3,
                     const std::array<double,3> &mu) const
    {
        if (upper1 == -std::numeric_limits<double>::infinity() ||
            upper2 == -std::numeric_limits<double>::infinity() ||
            upper3 == -std::numeric_limits<double>::infinity()) return {0,0};
        const double sd2 = std::sqrt(variance[1]);
        const double zupper = std::isinf(upper2) ? 12.0 :
            std::min(12.0, (upper2-mu[1])/sd2);
        if (zupper <= -12.0) return {0.0, cdf(-12.0)};
        const double b12 = variance[0]/variance[1];
        const double sd1c = std::sqrt(variance[0] -
            variance[0]*variance[0]/variance[1]);
        const double sd3c = std::sqrt(variance[2]-variance[1]);
        auto integrand = [&](double z) {
            const double x2 = mu[1] + sd2*z;
            const double m1 = mu[0] + b12*(x2-mu[1]);
            const double m3 = mu[2] + (x2-mu[1]);
            const double p1 = std::isinf(upper1) ? 1.0 :
                cdf((upper1-m1)/sd1c);
            const double p3 = std::isinf(upper3) ? 1.0 :
                cdf((upper3-m3)/sd3c);
            return pdf(z)*p1*p3;
        };
        double error = 0.0;
        const double value = boost::math::quadrature::gauss_kronrod<
            double,61>::integrate(integrand,-12.0,zupper,10,2e-13,&error);
        return {value, error + cdf(-12.0)};
    }

    ValueError rect2(double upper1, double upper2,
                     const std::array<double,3> &mu) const
    {
        return rect3(upper1,upper2,
                     std::numeric_limits<double>::infinity(),mu);
    }

    Decomposition evaluate() const
    {
        Decomposition out;
        std::array<double,3> call_log, coupon_log;
        for (unsigned i=0;i<3;++i) {
            call_log[i]=std::log(c.call_barrier[i]);
            coupon_log[i]=std::log(c.coupon_barrier[i]);
        }
        const std::array<double,3> mu = mean;
        const double p_alive1 = univariate_below(0,call_log[0],mu);
        out.call[0] = 1.0-p_alive1;
        out.coupon[0] = 1.0-univariate_below(0,coupon_log[0],mu);
        const ValueError alive2 = rect2(call_log[0],call_log[1],mu);
        const ValueError below_coupon2 = rect2(call_log[0],coupon_log[1],mu);
        out.call[1] = p_alive1-alive2.value;
        out.coupon[1] = p_alive1-below_coupon2.value;
        const ValueError survive = rect3(call_log[0],call_log[1],call_log[2],mu);
        const ValueError below_coupon3 =
            rect3(call_log[0],call_log[1],coupon_log[2],mu);
        out.call[2] = alive2.value-survive.value;
        out.coupon[2] = alive2.value-below_coupon3.value;
        out.survival = survive.value;

        const double protection_log = std::log(c.protection);
        ValueError below_protection{0,0};
        if (protection_log < call_log[2])
            below_protection = rect3(call_log[0],call_log[1],
                                     protection_log,mu);
        else
            below_protection = survive;
        out.protected_probability = survive.value-below_protection.value;

        std::array<double,3> tilted = mean;
        for (unsigned i=0;i<3;++i) tilted[i] += variance[i];
        const double downside_upper = std::min(call_log[2],protection_log);
        const ValueError tilted_probability =
            rect3(call_log[0],call_log[1],downside_upper,tilted);
        const double expected_spot_factor =
            std::exp(mean[2]+0.5*variance[2]);
        out.unprotected_contribution = c.notional/c.s0 *
            expected_spot_factor*tilted_probability.value;

        auto discount = [&](double time) { return std::exp(-c.rate*time); };
        for (unsigned i=0;i<3;++i) {
            const double observation=c.maturity*(i+1.0)/3.0;
            const double payment=observation+c.payment_lag_fraction*c.maturity;
            out.price += out.coupon[i]*c.coupon[i]*discount(payment);
            out.price += out.call[i]*c.redemption[i]*discount(payment);
        }
        const double terminal_time=c.maturity*(1.0+c.payment_lag_fraction);
        const double terminal_discount=discount(terminal_time);
        out.price += terminal_discount*(
            c.notional*out.protected_probability+
            out.unprotected_contribution);
        const double max_cash = *std::max_element(c.redemption,c.redemption+3)+
            c.coupon[0]+c.coupon[1]+c.coupon[2]+c.notional;
        out.estimated_error = max_cash*(alive2.error+below_coupon2.error+
            survive.error+below_coupon3.error+below_protection.error+
            tilted_probability.error);
        return out;
    }
};

struct DirectIntegrator {
    const autocall_frozen_case_t &c;
    double drift_step, diffusion;

    explicit DirectIntegrator(const autocall_frozen_case_t &input) : c(input)
    {
        const double dt=c.maturity/3.0;
        drift_step=(c.rate-c.dividend-0.5*c.sigma*c.sigma)*dt;
        diffusion=c.sigma*std::sqrt(dt);
    }

    double disc(double time) const { return std::exp(-c.rate*time); }

    double date3_value(double spot2) const
    {
        const double mu=std::log(spot2)+drift_step;
        auto below=[&](double barrier) {
            return cdf((std::log(barrier)-mu)/diffusion);
        };
        const double obs3=c.maturity;
        const double pay3=obs3+c.payment_lag_fraction*c.maturity;
        double value=c.coupon[2]*disc(pay3)*(1.0-below(c.coupon_barrier[2]));
        value+=c.redemption[2]*disc(pay3)*(1.0-below(c.call_barrier[2]));
        const double upper=std::min(c.call_barrier[2],c.protection);
        const double unprotected_spot=std::exp(mu+0.5*diffusion*diffusion)*
            cdf((std::log(upper)-mu-diffusion*diffusion)/diffusion);
        double protected_probability=0.0;
        if (c.protection<c.call_barrier[2])
            protected_probability=below(c.call_barrier[2])-below(c.protection);
        value+=disc(pay3)*(c.notional*protected_probability+
                           c.notional/c.s0*unprotected_spot);
        return value;
    }

    template<class F>
    double split_integrate(F function, double lo, double hi,
                           std::vector<double> cuts, double *error) const
    {
        cuts.push_back(lo); cuts.push_back(hi);
        std::sort(cuts.begin(),cuts.end());
        double total=0.0, total_error=0.0;
        for (std::size_t i=1;i<cuts.size();++i) {
            const double a=std::max(lo,cuts[i-1]);
            const double b=std::min(hi,cuts[i]);
            if (!(b>a)) continue;
            double local=0.0;
            total+=boost::math::quadrature::gauss_kronrod<double,61>::integrate(
                function,a,b,8,2e-12,&local);
            total_error+=local;
        }
        *error+=total_error;
        return total;
    }

    ValueError evaluate() const
    {
        const double zcall1=(std::log(c.call_barrier[0]/c.s0)-drift_step)/diffusion;
        const double zcoupon1=(std::log(c.coupon_barrier[0]/c.s0)-drift_step)/diffusion;
        const double pay1=c.maturity/3.0+c.payment_lag_fraction*c.maturity;
        double price=c.coupon[0]*disc(pay1)*(1.0-cdf(zcoupon1))+
            c.redemption[0]*disc(pay1)*(1.0-cdf(zcall1));
        double accumulated_inner_error=0.0;
        auto outer=[&](double z1) {
            const double spot1=c.s0*std::exp(drift_step+diffusion*z1);
            const double zcoupon2=(std::log(c.coupon_barrier[1]/spot1)-drift_step)/diffusion;
            const double zcall2=(std::log(c.call_barrier[1]/spot1)-drift_step)/diffusion;
            const double pay2=2.0*c.maturity/3.0+
                c.payment_lag_fraction*c.maturity;
            auto inner=[&](double z2) {
                const double spot2=spot1*std::exp(drift_step+diffusion*z2);
                double value=0.0;
                if (spot2>=c.coupon_barrier[1])
                    value+=c.coupon[1]*disc(pay2);
                if (spot2>=c.call_barrier[1])
                    value+=c.redemption[1]*disc(pay2);
                else
                    value+=date3_value(spot2);
                return pdf(z2)*value;
            };
            double local_error=0.0;
            const double value=split_integrate(inner,-12.0,12.0,
                {zcoupon2,zcall2},&local_error);
            accumulated_inner_error+=pdf(z1)*local_error;
            return pdf(z1)*value;
        };
        double outer_error=0.0;
        if (zcall1>-12.0)
            price+=split_integrate(outer,-12.0,std::min(12.0,zcall1),{},
                                   &outer_error);
        return {price,outer_error+accumulated_inner_error+1e-12};
    }
};

uint32_t sobol_word(uint32_t index, const uint32_t directions[32])
{
    uint32_t gray=index^(index>>1), word=0;
    for (unsigned bit=0;gray!=0;++bit,gray>>=1)
        if (gray&1u) word^=directions[bit];
    return word;
}

void direction_row(unsigned dimension, uint32_t out[32])
{
    const unsigned char *record=asian_arithmetic_joe_kuo_256_records+
        static_cast<std::size_t>(dimension)*132u;
    uint32_t count;
    std::memcpy(&count,record,4);
    if (count!=32u) std::abort();
    std::memcpy(out,record+4,128);
}

double mathematical_qmc(const autocall_frozen_case_t &c)
{
    uint32_t directions[3][32];
    for (unsigned d=0;d<3;++d) direction_row(d,directions[d]);
    boost::math::normal_distribution<double> normal;
    const double dt=c.maturity/3.0;
    const double drift=(c.rate-c.dividend-0.5*c.sigma*c.sigma)*dt;
    const double diffusion=c.sigma*std::sqrt(dt);
    double total=0.0;
    for (uint32_t p=0;p<4096u;++p) {
        double spot=c.s0,pv=0.0;
        bool alive=true;
        for (unsigned d=0;d<3;++d) {
            const uint32_t word=sobol_word(8192u+p,directions[d]);
            const double uniform=(static_cast<double>(word)+0.5)/4294967296.0;
            const double z=boost::math::quantile(normal,uniform);
            spot*=std::exp(drift+diffusion*z);
            const double observation=c.maturity*(d+1.0)/3.0;
            const double payment=observation+c.payment_lag_fraction*c.maturity;
            if (alive && spot>=c.coupon_barrier[d])
                pv+=c.coupon[d]*std::exp(-c.rate*payment);
            if (alive && spot>=c.call_barrier[d]) {
                pv+=c.redemption[d]*std::exp(-c.rate*payment);
                alive=false;
            }
        }
        if (alive) {
            const double payment=c.maturity*(1.0+c.payment_lag_fraction);
            pv+=c.notional*std::exp(-c.rate*payment)*
                (spot>=c.protection ? 1.0 : spot/c.s0);
        }
        total+=pv;
    }
    return total/4096.0;
}

} // namespace

int main()
{
    for (const auto &c:autocall_frozen_cases) {
        const GaussianOracle primary(c);
        const Decomposition d=primary.evaluate();
        const ValueError independent=DirectIntegrator(c).evaluate();
        const double mass=std::fabs(d.call[0]+d.call[1]+d.call[2]+d.survival-1.0);
        const double agreement=std::fabs(d.price-independent.value);
        if (mass>1e-11 || d.estimated_error>1e-9 ||
            agreement/c.notional>1e-8) {
            std::fprintf(stderr,"oracle gate failed %s mass=%g estimate=%g diff=%g\n",
                         c.name,mass,d.estimated_error,agreement);
            return 1;
        }
        const double qmc=mathematical_qmc(c);
        std::printf("REFERENCE_CASE name=%s cdf=%.17g direct=%.17g "
            "mathematical_qmc=%.17g call1=%.17g call2=%.17g call3=%.17g "
            "survival=%.17g coupon1=%.17g coupon2=%.17g coupon3=%.17g "
            "terminal_protected=%.17g terminal_unprotected=%.17g "
            "mass_error=%.12g primary_error=%.12g direct_error=%.12g\n",
            c.name,d.price,independent.value,qmc,d.call[0],d.call[1],d.call[2],
            d.survival,d.coupon[0],d.coupon[1],d.coupon[2],
            d.protected_probability,d.unprotected_contribution,mass,
            d.estimated_error,independent.error);
    }
    std::puts("semi_analytic_oracle PASS mass_error_limit=1e-11 "
              "estimated_price_error_limit=1e-9 independent_limit=1e-8_notional");
    return 0;
}
