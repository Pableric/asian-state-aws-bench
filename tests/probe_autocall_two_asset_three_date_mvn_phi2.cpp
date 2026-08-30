#include "private/autocall_two_asset_three_date_mvn_f64_math.h"
#include "tests/autocall_two_asset_three_date_mvn_program_cases.h"
#include "tests/autocall_two_asset_three_date_mvn_reference_math.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

static mvn_reference_math::Rectangle rectangle_from(
    const mvn_component_case_t &source) {
    mvn_reference_math::Rectangle out;
    out.dimension=2u;
    for (unsigned i=0;i<6u;++i) {
        out.lower[i]=source.lower[i];
        out.upper[i]=source.upper[i];
    }
    for (unsigned i=0;i<36u;++i)
        out.covariance[i]=source.correlation[i];
    return out;
}

static double exact_reduction(const mvn_component_case_t &source) {
    const double lo0=source.lower[0],hi0=source.upper[0];
    const double lo1=source.lower[1],hi1=source.upper[1];
    const double rho=source.correlation[1];
    const auto interval=[](double lo,double hi) {
        return std::max(0.0,mvn_f64_math::cdf_scalar(hi)-
                            mvn_f64_math::cdf_scalar(lo));
    };
    if (!(hi0>lo0) || !(hi1>lo1)) return 0.0;
    if (rho==0.0) return interval(lo0,hi0)*interval(lo1,hi1);
    if (rho==1.0) return interval(std::max(lo0,lo1),std::min(hi0,hi1));
    if (rho==-1.0) return interval(std::max(lo0,-hi1),std::min(hi0,-lo1));
    return std::numeric_limits<double>::quiet_NaN();
}

static void prepare_segments(const mvn_component_case_t &c,
                             mvn_f64_math::Phi2Batch8Input &input,
                             unsigned lane) {
    const double rho=c.correlation[1];
    const double residual=std::sqrt((1.0-rho)*(1.0+rho));
    const double lower=std::max(c.lower[0],-12.0);
    const double upper=std::min(c.upper[0],12.0);
    std::vector<double> point{lower,upper,std::clamp(0.0,lower,upper)};
    for (double bound : {c.lower[1],c.upper[1]}) {
        if (!std::isfinite(bound)) continue;
        const double center=bound/rho;
        const double width=6.0*residual/std::fabs(rho);
        point.push_back(std::clamp(center-width,lower,upper));
        point.push_back(std::clamp(center,lower,upper));
        point.push_back(std::clamp(center+width,lower,upper));
    }
    std::sort(point.begin(),point.end());
    point.erase(std::unique(point.begin(),point.end()),point.end());
    unsigned segment=0u;
    for (size_t i=1u;i<point.size() && segment<8u;++i) {
        input.segment_lower[segment][lane]=point[i-1u];
        input.segment_upper[segment][lane]=point[i];
        ++segment;
    }
    const double fill=point.empty() ? 0.0 : point.back();
    while (segment<8u) {
        input.segment_lower[segment][lane]=fill;
        input.segment_upper[segment][lane]=fill;
        ++segment;
    }
}

static void prepare_angle_rule(double rho,
                               mvn_f64_math::Phi2AngleBatch8Input &input) {
    constexpr unsigned order=40u;
    constexpr long double PI=
        3.141592653589793238462643383279502884L;
    long double node[order]{},weight[order]{};
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
        node[i]=-z; node[order-1u-i]=z;
        weight[i]=w; weight[order-1u-i]=w;
    }
    const long double middle=0.5L*std::asin(static_cast<long double>(rho));
    constexpr long double INV_TWO_PI=
        0.159154943091895335768883763372514362L;
    for (unsigned i=0;i<order;++i) {
        const long double theta=middle*(1.0L+node[i]);
        const long double cosine=std::cos(theta);
        input.sine[i]=static_cast<double>(std::sin(theta));
        input.negative_inverse_two_cosine_squared[i]=
            static_cast<double>(-0.5L/(cosine*cosine));
        input.scaled_weight[i]=static_cast<double>(middle*INV_TWO_PI*weight[i]);
    }
}

static double lower_direct(double h,double k,double rho) {
    const double infinity=std::numeric_limits<double>::infinity();
    if (h==-infinity || k==-infinity) return 0.0;
    if (h==infinity) return mvn_f64_math::cdf_scalar(k);
    if (k==infinity) return mvn_f64_math::cdf_scalar(h);
    if (rho==1.0) return mvn_f64_math::cdf_scalar(std::min(h,k));
    if (rho==-1.0) return std::max(0.0,mvn_f64_math::cdf_scalar(h)-
                                         mvn_f64_math::cdf_scalar(-k));
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace

int main() {
    size_t count=0u;
    const mvn_component_case_t *cases=mvn_selection_component_cases(2u,&count);
    bool pass=count==MVN_COMPONENT_CASES_PER_DIMENSION;
    long double max_reference_error=0.0L;
    unsigned exact_bit_mismatch=0u;
    unsigned ordinary=0u;
    unsigned reduced=0u;
    unsigned max_id=0u;
    long double angle_max_reference_error=0.0L;
    unsigned angle_bit_mismatch=0u;
    for (size_t first=0;first<count;first+=8u) {
        mvn_f64_math::Phi2Batch8Input input{};
        alignas(64) double native[8]{};
        std::array<bool,8> active{};
        for (unsigned lane=0;lane<8u && first+lane<count;++lane) {
            const auto &c=cases[first+lane];
            const double rho=c.correlation[1];
            input.lower0[lane]=c.lower[0]; input.upper0[lane]=c.upper[0];
            input.lower1[lane]=c.lower[1]; input.upper1[lane]=c.upper[1];
            input.rho[lane]=rho;
            if (std::fabs(rho)<1.0 && rho!=0.0) {
                input.inverse_residual[lane]=1.0/std::sqrt((1.0-rho)*(1.0+rho));
                prepare_segments(c,input,lane);
                active[lane]=true;
                ++ordinary;
            } else {
                input.inverse_residual[lane]=1.0;
                ++reduced;
            }
        }
        mvn_phi2_batch8_order20_asm(&input,native);
        for (unsigned lane=0;lane<8u && first+lane<count;++lane) {
            const auto &c=cases[first+lane];
            double candidate=native[lane];
            if (!active[lane]) candidate=exact_reduction(c);
            const double scalar=active[lane] ?
                mvn_f64_math::phi2_scalar_order20(&input,lane) :
                exact_reduction(c);
            if (std::bit_cast<std::uint64_t>(candidate)!=
                std::bit_cast<std::uint64_t>(scalar)) {
                ++exact_bit_mismatch;
            }
            const auto reference=mvn_reference_math::evaluate(rectangle_from(c),1.0e-11L);
            const long double truth=0.5L*(reference.primary+reference.independent);
            const long double error=std::fabs(
                static_cast<long double>(candidate)-truth);
            if (error>max_reference_error) {
                max_reference_error=error;
                max_id=c.id;
            }
            pass=pass && reference.finite && reference.converged;
        }

        mvn_f64_math::Phi2AngleBatch8Input angle{};
        const double angle_rho=cases[first].correlation[1];
        prepare_angle_rule(angle_rho,angle);
        alignas(64) double corner_native[4][8]{},corner_scalar[4][8]{};
        for (unsigned corner=0;corner<4u;++corner) {
            std::array<bool,8> finite{};
            for (unsigned lane=0;lane<8u && first+lane<count;++lane) {
                const auto &c=cases[first+lane];
                const double h=(corner&1u) ? c.lower[0] : c.upper[0];
                const double k=(corner&2u) ? c.lower[1] : c.upper[1];
                finite[lane]=std::isfinite(h) && std::isfinite(k) &&
                    std::fabs(angle_rho)<1.0;
                angle.h[lane]=finite[lane] ? h : 0.0;
                angle.k[lane]=finite[lane] ? k : 0.0;
                angle.h2_plus_k2[lane]=angle.h[lane]*angle.h[lane]+
                                        angle.k[lane]*angle.k[lane];
                angle.two_hk[lane]=2.0*angle.h[lane]*angle.k[lane];
            }
            mvn_phi2_angle_batch8_order40_asm(&angle,corner_native[corner]);
            for (unsigned lane=0;lane<8u && first+lane<count;++lane) {
                const auto &c=cases[first+lane];
                const double h=(corner&1u) ? c.lower[0] : c.upper[0];
                const double k=(corner&2u) ? c.lower[1] : c.upper[1];
                if (finite[lane]) {
                    corner_scalar[corner][lane]=
                        mvn_f64_math::phi2_angle_scalar_order40(&angle,lane);
                } else {
                    corner_native[corner][lane]=lower_direct(h,k,angle_rho);
                    corner_scalar[corner][lane]=corner_native[corner][lane];
                }
                if (std::bit_cast<std::uint64_t>(corner_native[corner][lane])!=
                    std::bit_cast<std::uint64_t>(corner_scalar[corner][lane])) {
                    ++angle_bit_mismatch;
                }
            }
        }
        for (unsigned lane=0;lane<8u && first+lane<count;++lane) {
            const double angle_value=std::clamp(
                corner_native[0][lane]-corner_native[1][lane]-
                corner_native[2][lane]+corner_native[3][lane],0.0,1.0);
            const auto reference=mvn_reference_math::evaluate(
                rectangle_from(cases[first+lane]),1.0e-11L);
            const long double truth=0.5L*(reference.primary+reference.independent);
            angle_max_reference_error=std::max(angle_max_reference_error,
                std::fabs(static_cast<long double>(angle_value)-truth));
        }
    }
    const bool genz_pass=exact_bit_mismatch==0u && max_reference_error<=2.0e-9L;
    const bool angle_pass=angle_bit_mismatch==0u &&
        angle_max_reference_error<=2.0e-9L;
    pass=pass && genz_pass;
    std::printf("PHI2_NATIVE_SELECTION kernel=GENZ_CONDITIONAL_ORDER20 "
                "ordinary=%u exact_reduced=%u max_abs=%.9Lg max_id=%u "
                "scalar_native_bit_mismatch=%u status=%s\n",
                ordinary,reduced,max_reference_error,max_id,exact_bit_mismatch,
                genz_pass ? "PASS" : "FAIL");
    std::printf("PHI2_NATIVE_SELECTION kernel=PLACKETT_ANGLE_ORDER40 "
                "max_abs=%.9Lg scalar_native_bit_mismatch=%u status=%s\n",
                angle_max_reference_error,angle_bit_mismatch,
                angle_pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
