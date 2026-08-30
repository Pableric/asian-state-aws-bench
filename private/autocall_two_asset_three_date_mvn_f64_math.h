#ifndef AUTOCALL_TWO_ASSET_THREE_DATE_MVN_F64_MATH_H
#define AUTOCALL_TWO_ASSET_THREE_DATE_MVN_F64_MATH_H

/* Private deterministic operation model for the later AVX-512 math probe. */
namespace mvn_f64_math {

struct alignas(64) Phi2Batch8Input {
    double lower0[8];
    double upper0[8];
    double lower1[8];
    double upper1[8];
    double rho[8];
    double inverse_residual[8];
    /* Cold-prepared, sorted discontinuity-aware intervals, SoA by segment. */
    double segment_lower[8][8];
    double segment_upper[8][8];
};

struct alignas(64) Phi2AngleBatch8Input {
    double h[8];
    double k[8];
    double h2_plus_k2[8];
    double two_hk[8];
    double sine[40];
    double negative_inverse_two_cosine_squared[40];
    double scaled_weight[40];
};

struct alignas(64) Phi46TargetBatch8 {
    double lower0[8];
    double upper0[8];
    double lower1[8];
    double upper1[8];
    double gain00[8];
    double gain01[8];
    double gain10[8];
    double gain11[8];
    double inverse_sd0[8];
    double inverse_sd1[8];
    double rho[8];
    double inverse_residual[8];
};

struct alignas(64) Phi46Batch8Input {
    double outer_lower0[8];
    double outer_upper0[8];
    double outer_lower1[8];
    double outer_upper1[8];
    double outer_rho[8];
    double outer_residual[8];
    double outer_inverse_residual[8];
    Phi46TargetBatch8 target[2];
    double node[40];
    double weight[40];
};

double exp_scalar(double x);
double cdf_scalar(double x);
double density_scalar(double x);

/*
 * Eight-lane exact-operation probe.  This is the native-math qualification
 * surface, not a public engine API.  The selected probability microkernels
 * inline the same bodies and never call through this wrapper.
 */
void probe_batch8(const double *input, double *cdf, double *density,
                  double *exponential);

/* Frozen 20-node conditional-Genz route selected by the component probe. */
void phi2_batch8_order20(const Phi2Batch8Input *input, double *output);
double phi2_scalar_order20(const Phi2Batch8Input *input, unsigned lane);
double phi2_angle_scalar_order40(const Phi2AngleBatch8Input *input,
                                 unsigned lane);

extern "C" void mvn_phi2_batch8_order20_asm(const Phi2Batch8Input *input,
                                             double *output);
extern "C" void mvn_phi2_angle_batch8_order40_asm(
    const Phi2AngleBatch8Input *input, double *output);
extern "C" void mvn_phi4_batch8_tensor40_candidate(
    const Phi46Batch8Input *input, double *output);
extern "C" void mvn_phi6_batch8_bridge40_candidate(
    const Phi46Batch8Input *input, double *output);

} // namespace mvn_f64_math

#endif
