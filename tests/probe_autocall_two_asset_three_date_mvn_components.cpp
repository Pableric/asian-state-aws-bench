#include "tests/autocall_two_asset_three_date_mvn_program_cases.h"
#include "tests/autocall_two_asset_three_date_mvn_reference_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace {

struct CandidateSummary {
    unsigned order{};
    bool rotated{};
    long double max_abs{};
    long double rms{};
    unsigned failures{};
    std::array<long double, 8> regime_max{};
};

struct Rule {
    std::vector<long double> node;
    std::vector<long double> weight;
};

static Rule rule(unsigned order) {
    constexpr long double PI=3.141592653589793238462643383279502884L;
    Rule out;
    out.node.resize(order); out.weight.resize(order);
    for (unsigned i=0;i<(order+1u)/2u;++i) {
        long double z=std::cos(PI*(i+0.75L)/(order+0.5L));
        long double derivative=0.0L;
        for (unsigned iteration=0;iteration<64u;++iteration) {
            long double p0=1.0L,p1=z;
            for (unsigned k=2;k<=order;++k) {
                const long double pk=((2.0L*k-1.0L)*z*p1-(k-1.0L)*p0)/k;
                p0=p1; p1=pk;
            }
            derivative=order*(z*p1-p0)/(z*z-1.0L);
            const long double next=z-p1/derivative;
            if (std::fabs(next-z)<=8.0L*
                std::numeric_limits<long double>::epsilon()) { z=next; break; }
            z=next;
        }
        const long double w=2.0L/((1.0L-z*z)*derivative*derivative);
        out.node[i]=-z; out.node[order-1u-i]=z;
        out.weight[i]=w; out.weight[order-1u-i]=w;
    }
    return out;
}

static long double lower_orthant_angle(long double h,long double k,
                                        long double rho,const Rule &gl) {
    const long double infinity=std::numeric_limits<long double>::infinity();
    if (h==-infinity || k==-infinity) return 0.0L;
    if (h==infinity) return mvn_reference_math::normal_cdf(k);
    if (k==infinity) return mvn_reference_math::normal_cdf(h);
    if (rho==0.0L) return mvn_reference_math::normal_cdf(h)*
        mvn_reference_math::normal_cdf(k);
    if (rho==1.0L) return mvn_reference_math::normal_cdf(std::min(h,k));
    if (rho==-1.0L) return std::max(0.0L,
        mvn_reference_math::normal_cdf(h)-mvn_reference_math::normal_cdf(-k));
    const long double angle=std::asin(rho);
    const long double middle=0.5L*angle;
    long double sum=0.0L;
    for (unsigned i=0;i<gl.node.size();++i) {
        const long double theta=middle*(1.0L+gl.node[i]);
        const long double s=std::sin(theta);
        const long double c=std::cos(theta);
        const long double exponent=-(h*h-2.0L*h*k*s+k*k)/(2.0L*c*c);
        sum+=gl.weight[i]*std::exp(exponent);
    }
    constexpr long double INV_TWO_PI=
        0.159154943091895335768883763372514362L;
    return std::clamp(mvn_reference_math::normal_cdf(h)*
        mvn_reference_math::normal_cdf(k)+middle*INV_TWO_PI*sum,0.0L,1.0L);
}

static long double rectangle_angle(const mvn_component_case_t &c,
                                   const Rule &gl) {
    const long double rho=c.correlation[1];
    const auto f=[&](long double h,long double k) {
        return lower_orthant_angle(h,k,rho,gl);
    };
    return std::clamp(f(c.upper[0],c.upper[1])-f(c.lower[0],c.upper[1])-
                      f(c.upper[0],c.lower[1])+f(c.lower[0],c.lower[1]),
                      0.0L,1.0L);
}

static long double owen_t_fixed(long double h,long double a,const Rule &gl) {
    if (a==0.0L) return 0.0L;
    const long double sign=a<0.0L ? -1.0L : 1.0L;
    const long double angle=std::atan(std::fabs(a));
    const long double middle=0.5L*angle;
    long double sum=0.0L;
    for (unsigned i=0;i<gl.node.size();++i) {
        const long double theta=middle*(1.0L+gl.node[i]);
        const long double cosine=std::cos(theta);
        sum+=gl.weight[i]*std::exp(-0.5L*h*h/(cosine*cosine));
    }
    constexpr long double INV_TWO_PI=
        0.159154943091895335768883763372514362L;
    return sign*middle*INV_TWO_PI*sum;
}

static long double lower_orthant_owen(long double h,long double k,
                                      long double rho,const Rule &gl) {
    const long double infinity=std::numeric_limits<long double>::infinity();
    if (h==-infinity || k==-infinity) return 0.0L;
    if (h==infinity) return mvn_reference_math::normal_cdf(k);
    if (k==infinity) return mvn_reference_math::normal_cdf(h);
    if (rho==0.0L) return mvn_reference_math::normal_cdf(h)*
        mvn_reference_math::normal_cdf(k);
    if (rho==1.0L) return mvn_reference_math::normal_cdf(std::min(h,k));
    if (rho==-1.0L) return std::max(0.0L,
        mvn_reference_math::normal_cdf(h)-mvn_reference_math::normal_cdf(-k));
    /* The zero-axis limit is evaluated by the independent angle identity. */
    if (std::fabs(h)<1.0e-18L || std::fabs(k)<1.0e-18L)
        return lower_orthant_angle(h,k,rho,gl);
    const long double residual=std::sqrt((1.0L-rho)*(1.0L+rho));
    const long double ah=(k/h-rho)/residual;
    const long double ak=(h/k-rho)/residual;
    const long double delta=(std::signbit(h)==std::signbit(k)) ? 0.0L : 0.5L;
    return std::clamp(0.5L*(mvn_reference_math::normal_cdf(h)+
        mvn_reference_math::normal_cdf(k))-owen_t_fixed(h,ah,gl)-
        owen_t_fixed(k,ak,gl)-delta,0.0L,1.0L);
}

static long double rectangle_owen(const mvn_component_case_t &c,
                                  const Rule &gl) {
    const long double rho=c.correlation[1];
    const auto f=[&](long double h,long double k) {
        return lower_orthant_owen(h,k,rho,gl);
    };
    return std::clamp(f(c.upper[0],c.upper[1])-f(c.lower[0],c.upper[1])-
                      f(c.upper[0],c.lower[1])+f(c.lower[0],c.lower[1]),
                      0.0L,1.0L);
}

static mvn_reference_math::Rectangle rectangle_from(
    const mvn_component_case_t &source) {
    mvn_reference_math::Rectangle out;
    out.dimension = source.dimension;
    for (unsigned i = 0; i < 6u; ++i) {
        out.lower[i] = source.lower[i];
        out.upper[i] = source.upper[i];
    }
    for (unsigned i = 0; i < 36u; ++i)
        out.covariance[i] = source.correlation[i];
    return out;
}

static bool qualify_dimension(unsigned dimension, bool near_only=false,
                              size_t limit=MVN_COMPONENT_CASES_PER_DIMENSION,
                              size_t override_first=MVN_COMPONENT_CASES_PER_DIMENSION) {
    size_t count = 0u;
    const mvn_component_case_t *cases =
        mvn_selection_component_cases(dimension, &count);
    constexpr unsigned ORDERS[] = {12u,20u,24u,32u,40u,48u};
    std::array<CandidateSummary,12> result{};
    for (unsigned i = 0; i < result.size(); ++i) {
        result[i].order = ORDERS[i % std::size(ORDERS)];
        result[i].rotated = i >= std::size(ORDERS);
    }
    const size_t first=override_first != MVN_COMPONENT_CASES_PER_DIMENSION ?
        override_first : (near_only ? 96u : 0u);
    const size_t last=std::min(count,first+limit);
    const size_t evaluated=last-first;
    bool references = count == MVN_COMPONENT_CASES_PER_DIMENSION;
    const Rule angle_rule=dimension==2u ? rule(40u) : Rule{};
    long double angle_max=0.0L,angle_rms=0.0L;
    long double owen_max=0.0L,owen_rms=0.0L;
    for (size_t c = first; c < last; ++c) {
        const auto rectangle = rectangle_from(cases[c]);
        const auto reference = mvn_reference_math::evaluate(rectangle, 1.0e-11L);
        if (!reference.converged || reference.independent_error>2.0e-9L)
            std::printf("COMPONENT_REFERENCE id=%u dimension=%u regime=%u "
                        "primary=%.17Lg independent=%.17Lg difference=%.9Lg "
                        "paired=%.9Lg order=%u converged=%s\n",cases[c].id,
                        dimension,static_cast<unsigned>(cases[c].regime),
                        reference.primary,reference.independent,
                        reference.independent_error,reference.paired_error,
                        reference.primary_order,
                        reference.converged ? "YES" : "NO");
        references = references && reference.finite && reference.converged &&
            reference.independent_error <= 2.0e-9L;
        const long double truth = 0.5L *
            (reference.primary + reference.independent);
        if (dimension==2u) {
            const long double error=std::fabs(rectangle_angle(cases[c],angle_rule)-truth);
            angle_max=std::max(angle_max,error);
            angle_rms+=error*error;
            const long double owen_error=std::fabs(
                rectangle_owen(cases[c],angle_rule)-truth);
            owen_max=std::max(owen_max,owen_error);
            owen_rms+=owen_error*owen_error;
        }
        for (unsigned candidate = 0; candidate < result.size(); ++candidate) {
            const long double value = mvn_reference_math::evaluate_fixed(
                rectangle, result[candidate].order, result[candidate].rotated);
            const long double error = std::fabs(value - truth);
            result[candidate].max_abs =
                std::max(result[candidate].max_abs, error);
            result[candidate].rms += error * error;
            result[candidate].regime_max[cases[c].regime] = std::max(
                result[candidate].regime_max[cases[c].regime], error);
            if (!std::isfinite(value))
                ++result[candidate].failures;
        }
    }
    bool any = false;
    for (auto &candidate : result) {
        candidate.rms = std::sqrt(candidate.rms /
            static_cast<long double>(std::max<size_t>(evaluated, 1u)));
        const bool pass = references && candidate.failures == 0u &&
            candidate.max_abs <= 2.0e-9L;
        any = any || pass;
        std::printf("COMPONENT_SELECTION dimension=%u family=%s "
                    "order=%u cases=%zu max_abs=%.9Lg rms=%.9Lg failures=%u "
                    "status=%s\n", dimension,
                    candidate.rotated ? "ROTATED_FIXED" : "GENZ_FIXED",
                    candidate.order, evaluated,
                    candidate.max_abs, candidate.rms, candidate.failures,
                    pass ? "PASS" : "FAIL");
        std::printf("COMPONENT_REGIMES dimension=%u family=%s order=%u "
                    "central=%.6Lg narrow=%.6Lg lower=%.6Lg upper=%.6Lg "
                    "cancellation=%.6Lg independence=%.6Lg near_pos=%.6Lg "
                    "near_neg=%.6Lg\n", dimension,
                    candidate.rotated ? "ROTATED_FIXED" : "GENZ_FIXED",
                    candidate.order, candidate.regime_max[0],
                    candidate.regime_max[1], candidate.regime_max[2],
                    candidate.regime_max[3], candidate.regime_max[4],
                    candidate.regime_max[5], candidate.regime_max[6],
                    candidate.regime_max[7]);
    }
    if (dimension==2u) {
        angle_rms=std::sqrt(angle_rms/
            static_cast<long double>(std::max<size_t>(evaluated,1u)));
        const bool angle_pass=references && angle_max<=2.0e-9L;
        std::printf("COMPONENT_SELECTION dimension=2 family=PLACKETT_ANGLE "
                    "order=40 cases=%zu max_abs=%.9Lg rms=%.9Lg status=%s\n",
                    evaluated,angle_max,angle_rms,angle_pass ? "PASS" : "FAIL");
        any=any || angle_pass;
        owen_rms=std::sqrt(owen_rms/
            static_cast<long double>(std::max<size_t>(evaluated,1u)));
        const bool owen_pass=references && owen_max<=2.0e-9L;
        std::printf("COMPONENT_SELECTION dimension=2 "
                    "family=KOMELJ_SHAPED_OWEN_T_FIXED order=40 cases=%zu "
                    "max_abs=%.9Lg rms=%.9Lg bounded_tier=YES status=%s\n",
                    evaluated,owen_max,owen_rms,owen_pass?"PASS":"FAIL");
        any=any || owen_pass;
    }
    return any;
}

} // namespace

int main(int argc,char **argv) {
    if (argc==2 && std::strcmp(argv[1],"--near")==0) {
        const bool phi4=qualify_dimension(4u,true);
        const bool phi6=qualify_dimension(6u,true);
        return phi4 && phi6 ? 0 : 1;
    }
    if (argc==2 && std::strcmp(argv[1],"--near-one")==0) {
        const bool phi4=qualify_dimension(4u,true,2u);
        const bool phi6=qualify_dimension(6u,true,2u);
        return phi4 && phi6 ? 0 : 1;
    }
    if (argc==2 && (std::strcmp(argv[1],"--near-pos")==0 ||
                    std::strcmp(argv[1],"--near-neg")==0)) {
        const size_t first=std::strcmp(argv[1],"--near-pos")==0 ? 96u : 112u;
        const bool phi4=qualify_dimension(4u,true,16u,first);
        const bool phi6=qualify_dimension(6u,true,16u,first);
        return phi4 && phi6 ? 0 : 1;
    }
    if (argc==2 && std::strcmp(argv[1],"--near-neg-one")==0) {
        const bool phi4=qualify_dimension(4u,true,2u,112u);
        const bool phi6=qualify_dimension(6u,true,2u,112u);
        return phi4 && phi6 ? 0 : 1;
    }
    if (argc==2 && std::strcmp(argv[1],"--phi6-central")==0)
        return qualify_dimension(6u,false,16u,0u) ? 0 : 1;
    if (argc==2 && std::strcmp(argv[1],"--phi2")==0)
        return qualify_dimension(2u) ? 0 : 1;
    const bool phi2 = qualify_dimension(2u);
    if (!phi2) {
        std::printf("MVN_NATIVE_MATH_NOT_QUALIFIED stage=PHI2\n");
        return 1;
    }
    const bool phi4 = qualify_dimension(4u);
    const bool phi6 = qualify_dimension(6u);
    if (!phi4 || !phi6) {
        std::printf("MVN_NATIVE_MATH_NOT_QUALIFIED stage=PHI4_PHI6\n");
        return 1;
    }
    std::printf("PORTABLE_COMPONENT_PORTFOLIO status=PASS\n");
    return 0;
}
