#include "private/autocall_two_asset_three_date_mvn_f64_math.h"
#include "tests/autocall_two_asset_three_date_mvn_program_cases.h"
#include "tests/autocall_two_asset_three_date_mvn_reference_math.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {

using mvn_f64_math::Phi46Batch8Input;
using mvn_f64_math::Phi46TargetBatch8;

static void make_rule(Phi46Batch8Input &out) {
    constexpr long double PI=3.141592653589793238462643383279502884L;
    constexpr unsigned n=40u;
    for (unsigned i=0;i<(n+1u)/2u;++i) {
        long double z=std::cos(PI*(i+0.75L)/(n+0.5L));
        long double derivative=0.0L;
        for (unsigned iteration=0;iteration<64u;++iteration) {
            long double p0=1.0L,p1=z;
            for (unsigned k=2;k<=n;++k) {
                const long double pk=((2.0L*k-1.0L)*z*p1-(k-1.0L)*p0)/k;
                p0=p1; p1=pk;
            }
            derivative=n*(z*p1-p0)/(z*z-1.0L);
            const long double next=z-p1/derivative;
            if (std::fabs(next-z)<=8.0L*
                std::numeric_limits<long double>::epsilon()) { z=next; break; }
            z=next;
        }
        const double node=static_cast<double>(z);
        const double weight=static_cast<double>(
            2.0L/((1.0L-z*z)*derivative*derivative));
        out.node[i]=-node; out.node[n-1u-i]=node;
        out.weight[i]=weight; out.weight[n-1u-i]=weight;
    }
}

static bool prepare_target(const mvn_component_case_t &source,unsigned target,
                           unsigned given,unsigned lane,
                           Phi46TargetBatch8 &out) {
    const unsigned g=2u*given,t=2u*target;
    const double c00=source.correlation[(g+0u)*6u+g+0u];
    const double c01=source.correlation[(g+0u)*6u+g+1u];
    const double c11=source.correlation[(g+1u)*6u+g+1u];
    const double determinant=c00*c11-c01*c01;
    if (!(determinant>0.0)) return false;
    const double i00=c11/determinant,i01=-c01/determinant,i11=c00/determinant;
    double gain[4]{};
    double conditional[4]{};
    for (unsigned row=0;row<2u;++row) {
        const double b0=source.correlation[(t+row)*6u+g+0u];
        const double b1=source.correlation[(t+row)*6u+g+1u];
        gain[2u*row]=b0*i00+b1*i01;
        gain[2u*row+1u]=b0*i01+b1*i11;
    }
    for (unsigned row=0;row<2u;++row)
        for (unsigned col=0;col<2u;++col) {
            const double b0=source.correlation[(t+col)*6u+g+0u];
            const double b1=source.correlation[(t+col)*6u+g+1u];
            conditional[2u*row+col]=source.correlation[(t+row)*6u+t+col]-
                gain[2u*row]*b0-gain[2u*row+1u]*b1;
        }
    const double sd0=std::sqrt(conditional[0]);
    const double sd1=std::sqrt(conditional[3]);
    const double rho=conditional[1]/(sd0*sd1);
    const double residual=std::sqrt((1.0-rho)*(1.0+rho));
    if (!(sd0>0.0) || !(sd1>0.0) || !(residual>1.0e-4) ||
        !std::isfinite(residual)) return false;
    out.lower0[lane]=source.lower[t]; out.upper0[lane]=source.upper[t];
    out.lower1[lane]=source.lower[t+1u]; out.upper1[lane]=source.upper[t+1u];
    out.gain00[lane]=gain[0]; out.gain01[lane]=gain[1];
    out.gain10[lane]=gain[2]; out.gain11[lane]=gain[3];
    out.inverse_sd0[lane]=1.0/sd0; out.inverse_sd1[lane]=1.0/sd1;
    out.rho[lane]=rho; out.inverse_residual[lane]=1.0/residual;
    return true;
}

static bool prepare_batch(unsigned dimension,Phi46Batch8Input &out) {
    size_t count=0u;
    const mvn_component_case_t *cases=mvn_selection_component_cases(dimension,&count);
    if (count<8u) return false;
    make_rule(out);
    const unsigned given=dimension==4u ? 0u : 1u;
    for (unsigned lane=0;lane<8u;++lane) {
        const auto &source=cases[lane];
        const unsigned g=2u*given;
        const double rho=source.correlation[g*6u+g+1u];
        const double residual=std::sqrt((1.0-rho)*(1.0+rho));
        if (!(residual>1.0e-4)) return false;
        out.outer_lower0[lane]=std::max(-9.0,source.lower[g]);
        out.outer_upper0[lane]=std::min(9.0,source.upper[g]);
        out.outer_lower1[lane]=source.lower[g+1u];
        out.outer_upper1[lane]=source.upper[g+1u];
        out.outer_rho[lane]=rho;
        out.outer_residual[lane]=residual;
        out.outer_inverse_residual[lane]=1.0/residual;
        if (!prepare_target(source,dimension==4u ? 1u : 0u,given,lane,
                            out.target[0])) return false;
        if (dimension==6u && !prepare_target(source,2u,given,lane,
                                              out.target[1])) return false;
    }
    return true;
}

static double conditional_scalar(const Phi46TargetBatch8 &t,unsigned lane,
                                 double x0,double x1,const double *node,
                                 const double *weight) {
    const double mean0=std::fma(t.gain01[lane],x1,t.gain00[lane]*x0);
    const double mean1=std::fma(t.gain11[lane],x1,t.gain10[lane]*x0);
    const double lower0=std::max(-9.0,(t.lower0[lane]-mean0)*t.inverse_sd0[lane]);
    const double upper0=std::min(9.0,(t.upper0[lane]-mean0)*t.inverse_sd0[lane]);
    const double lower1=(t.lower1[lane]-mean1)*t.inverse_sd1[lane];
    const double upper1=(t.upper1[lane]-mean1)*t.inverse_sd1[lane];
    const double middle=0.5*(lower0+upper0),half=0.5*(upper0-lower0);
    double sum=0.0;
    for (unsigned k=0;k<40u;++k) {
        const double y=std::fma(half,node[k],middle);
        const double lo=std::fma(-t.rho[lane],y,lower1)*t.inverse_residual[lane];
        const double hi=std::fma(-t.rho[lane],y,upper1)*t.inverse_residual[lane];
        const double interval=std::max(0.0,mvn_f64_math::cdf_scalar(hi)-
                                            mvn_f64_math::cdf_scalar(lo));
        sum=std::fma(weight[k],mvn_f64_math::density_scalar(y)*interval,sum);
    }
    return std::clamp(half*sum,0.0,1.0);
}

static double tensor_scalar(const Phi46Batch8Input &input,unsigned lane,
                            bool two_targets) {
    const double middle0=0.5*(input.outer_lower0[lane]+input.outer_upper0[lane]);
    const double half0=0.5*(input.outer_upper0[lane]-input.outer_lower0[lane]);
    double total=0.0;
    for (unsigned i=0;i<40u;++i) {
        const double x0=std::fma(half0,input.node[i],middle0);
        const double vl=std::max(-9.0,std::fma(-input.outer_rho[lane],x0,
            input.outer_lower1[lane])*input.outer_inverse_residual[lane]);
        const double vu=std::min(9.0,std::fma(-input.outer_rho[lane],x0,
            input.outer_upper1[lane])*input.outer_inverse_residual[lane]);
        const double vmiddle=0.5*(vl+vu),vhalf=0.5*(vu-vl);
        double inner=0.0;
        for (unsigned j=0;j<40u;++j) {
            const double v=std::fma(vhalf,input.node[j],vmiddle);
            const double x1=std::fma(input.outer_residual[lane],v,
                                     input.outer_rho[lane]*x0);
            double probability=conditional_scalar(input.target[0],lane,x0,x1,
                                                   input.node,input.weight);
            if (two_targets) probability*=conditional_scalar(input.target[1],
                lane,x0,x1,input.node,input.weight);
            inner=std::fma(input.weight[j],
                mvn_f64_math::density_scalar(v)*probability,inner);
        }
        total=std::fma(input.weight[i],
            mvn_f64_math::density_scalar(x0)*(vhalf*inner),total);
    }
    return std::clamp(half0*total,0.0,1.0);
}

static bool run_dimension(unsigned dimension) {
    Phi46Batch8Input input{};
    if (!prepare_batch(dimension,input)) return false;
    alignas(64) double output[8]{};
    if (dimension==4u) mvn_f64_math::mvn_phi4_batch8_tensor40_candidate(&input,output);
    else mvn_f64_math::mvn_phi6_batch8_bridge40_candidate(&input,output);
    size_t count=0u;
    const mvn_component_case_t *cases=mvn_selection_component_cases(dimension,&count);
    unsigned bit_mismatch=0u;
    long double max_reference_error=0.0L;
    for (unsigned lane=0;lane<8u;++lane) {
        const double scalar=tensor_scalar(input,lane,dimension==6u);
        bit_mismatch+=std::bit_cast<std::uint64_t>(scalar)!=
                      std::bit_cast<std::uint64_t>(output[lane]);
        mvn_reference_math::Rectangle rectangle{};
        rectangle.dimension=dimension;
        for (unsigned i=0;i<6u;++i) {
            rectangle.lower[i]=cases[lane].lower[i];
            rectangle.upper[i]=cases[lane].upper[i];
        }
        for (unsigned i=0;i<36u;++i)
            rectangle.covariance[i]=cases[lane].correlation[i];
        const long double truth=mvn_reference_math::evaluate_fixed(
            rectangle,48u,false);
        max_reference_error=std::max(max_reference_error,
            std::fabs(static_cast<long double>(output[lane])-truth));
    }
    const bool numeric=bit_mismatch==0u && max_reference_error<=2.0e-9L;
    std::printf("NATIVE_HIGH_DIM_PROBE dimension=%u cases=8 order=40 "
                "scalar_native_bit_mismatches=%u max_reference_error=%.9Lg "
                "ordinary_numeric=%s\n",dimension,bit_mismatch,
                max_reference_error,numeric?"PASS":"FAIL");
    return numeric;
}

} // namespace

int main() {
    const bool phi4=run_dimension(4u);
    const bool phi6=run_dimension(6u);
    std::printf("NATIVE_HIGH_DIM_PORTFOLIO ordinary_phi4=%s ordinary_phi6=%s "
                "near_singular_route=NOT_QUALIFIED structural_spills=YES "
                "status=MVN_NATIVE_MATH_NOT_QUALIFIED\n",
                phi4?"PASS":"FAIL",phi6?"PASS":"FAIL");
    return 0;
}
