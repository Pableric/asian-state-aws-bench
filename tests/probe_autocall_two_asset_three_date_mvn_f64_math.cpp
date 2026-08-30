#include "private/autocall_two_asset_three_date_mvn_f64_math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

int main() {
    constexpr double INV_SQRT_2=0x1.6a09e667f3bcdp-1;
    double max_cdf=0.0;
    double max_exp_relative=0.0;
    double max_vector_cdf=0.0;
    double max_vector_density=0.0;
    double max_vector_exp=0.0;
    double max_vector_cdf_x=0.0;
    double max_vector_cdf_candidate=0.0;
    double max_vector_cdf_scalar=0.0;
    double previous=-1.0;
    bool monotone=true;
    for (unsigned i=0;i<=400000u;++i) {
        const double x=-10.0+20.0*static_cast<double>(i)/400000.0;
        const double candidate=mvn_f64_math::cdf_scalar(x);
        const double reference=0.5*std::erfc(-x*INV_SQRT_2);
        max_cdf=std::max(max_cdf,std::fabs(candidate-reference));
        monotone=monotone && candidate>=previous && candidate>=0.0 &&
            candidate<=1.0 && std::isfinite(candidate);
        previous=candidate;
    }
    for (unsigned i=0;i<=200000u;++i) {
        const double x=-50.0+100.0*static_cast<double>(i)/200000.0;
        const double candidate=mvn_f64_math::exp_scalar(x);
        const double reference=std::exp(x);
        max_exp_relative=std::max(max_exp_relative,
            std::fabs(candidate/reference-1.0));
    }
    for (unsigned block=0;block<50001u;++block) {
        alignas(64) double input[8],cdf[8],density[8],exponential[8];
        for (unsigned lane=0;lane<8u;++lane)
            input[lane]=-40.0+80.0*static_cast<double>(block*8u+lane)/400007.0;
        mvn_f64_math::probe_batch8(input,cdf,density,exponential);
        for (unsigned lane=0;lane<8u;++lane) {
            const double cdf_error=std::fabs(
                cdf[lane]-mvn_f64_math::cdf_scalar(input[lane]));
            if (cdf_error>max_vector_cdf) {
                max_vector_cdf=cdf_error;
                max_vector_cdf_x=input[lane];
                max_vector_cdf_candidate=cdf[lane];
                max_vector_cdf_scalar=mvn_f64_math::cdf_scalar(input[lane]);
            }
            if (std::fabs(input[lane])<=12.0)
                max_vector_density=std::max(max_vector_density,
                    std::fabs(density[lane]-mvn_f64_math::density_scalar(input[lane])));
            max_vector_exp=std::max(max_vector_exp,
                std::fabs(exponential[lane]/mvn_f64_math::exp_scalar(input[lane])-1.0));
        }
    }
    const bool pass=monotone && max_cdf<=2.0e-15 &&
        max_exp_relative<=2.0e-15 && max_vector_cdf<=2.0e-15 &&
        max_vector_density<=2.0e-15 && max_vector_exp<=2.0e-15;
    std::printf("F64_MATH_SELECTION cdf_max_abs=%.17g "
                "exp_max_relative=%.17g vector_cdf_max=%.17g "
                "vector_cdf_x=%a vector_cdf_bits=%a/%a "
                "vector_density_max=%.17g vector_exp_max=%.17g "
                "monotone=%s status=%s\n",
                max_cdf,max_exp_relative,max_vector_cdf,max_vector_cdf_x,
                max_vector_cdf_candidate,max_vector_cdf_scalar,max_vector_density,
                max_vector_exp,monotone ? "YES" : "NO",
                pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
