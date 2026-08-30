#define main mvn_rejected_phi46_probe_main
#include "tests/probe_autocall_two_asset_three_date_mvn_phi46.cpp"
#undef main

extern "C" void mvn_phi4_batch8_tensor40_coeffsched_asm(
    const mvn_f64_math::Phi46Batch8Input *,double *);
extern "C" void mvn_phi6_batch8_bridge40_coeffsched_asm(
    const mvn_f64_math::Phi46Batch8Input *,double *);

namespace {

static bool run_coeffsched_dimension(unsigned dimension) {
    Phi46Batch8Input input{};
    if (!prepare_batch(dimension,input)) return false;
    alignas(64) double rejected[8]{};
    alignas(64) double output[8]{};
    if (dimension==4u) {
        mvn_f64_math::mvn_phi4_batch8_tensor40_candidate(&input,rejected);
        mvn_phi4_batch8_tensor40_coeffsched_asm(&input,output);
    } else {
        mvn_f64_math::mvn_phi6_batch8_bridge40_candidate(&input,rejected);
        mvn_phi6_batch8_bridge40_coeffsched_asm(&input,output);
    }
    size_t count=0u;
    const mvn_component_case_t *cases=mvn_selection_component_cases(
        dimension,&count);
    unsigned scalar_mismatch=0u;
    unsigned rejected_mismatch=0u;
    long double max_reference_error=0.0L;
    for (unsigned lane=0;lane<8u;++lane) {
        const double scalar=tensor_scalar(input,lane,dimension==6u);
        scalar_mismatch+=std::bit_cast<std::uint64_t>(scalar)!=
                         std::bit_cast<std::uint64_t>(output[lane]);
        rejected_mismatch+=std::bit_cast<std::uint64_t>(rejected[lane])!=
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
    const long double ceiling=dimension==4u ?
        5.58770695e-17L : 2.21408314e-15L;
    const bool pass=scalar_mismatch==0u && rejected_mismatch==0u &&
                    max_reference_error<=ceiling;
    std::printf("COEFFICIENT_SCHEDULING_IDENTITY dimension=%u cases=8 "
                "order=40 scalar_native_bit_mismatches=%u "
                "rejected_native_bit_mismatches=%u "
                "max_reference_error=%.17Lg ceiling=%.17Lg status=%s\n",
                dimension,scalar_mismatch,rejected_mismatch,
                max_reference_error,ceiling,pass?"PASS":"FAIL");
    return pass;
}

} // namespace

int main() {
    const bool phi4=run_coeffsched_dimension(4u);
    const bool phi6=run_coeffsched_dimension(6u);
    std::printf("COEFFICIENT_SCHEDULING_NUMERIC phi4=%s phi6=%s status=%s\n",
                phi4?"PASS":"FAIL",phi6?"PASS":"FAIL",
                phi4&&phi6?"PASS":"FAIL");
    return phi4&&phi6 ? 0 : 1;
}
