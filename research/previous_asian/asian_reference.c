#include "asian_reference.h"
#include "private/asian_generated_assets.h"
#include <math.h>
#include <stddef.h>

const uint32_t (*asian_w_for_mode(asian_pricing_mode_t mode))[32] {
    switch (mode) {
    case ASIAN_MODE_RANK1: return asian_w_rank1;
    case ASIAN_MODE_COEFFICIENT_PAIR: return asian_w_pair;
    case ASIAN_MODE_FINAL_Z: return asian_w_final_z;
    default: return asian_w_reference;
    }
}

double asian_inverse_normal_u32(uint32_t word) {
    /* Acklam's inverse normal.  The exact Sobol endpoint is intentional. */
    if (word == 0) return -INFINITY;
    const double p = (double)word * 0x1p-32;
    static const double a[] = {-3.969683028665376e1,2.209460984245205e2,
        -2.759285104469687e2,1.383577518672690e2,-3.066479806614716e1,2.506628277459239};
    static const double b[] = {-5.447609879822406e1,1.615858368580409e2,
        -1.556989798598866e2,6.680131188771972e1,-1.328068155288572e1};
    static const double c[] = {-7.784894002430293e-3,-3.223964580411365e-1,
        -2.400758277161838,-2.549732539343734,4.374664141464968,2.938163982698783};
    static const double d[] = {7.784695709041462e-3,3.224671290700398e-1,
        2.445134137142996,3.754408661907416};
    if (p < 0.02425) {
        const double q = sqrt(-2.0 * log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    if (p > 0.97575) {
        const double q = sqrt(-2.0 * log1p(-p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    const double q = p - 0.5, r = q*q;
    return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
           (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
}

static uint32_t sobol_word(uint64_t n, const uint32_t w[32][32], int dim) {
    uint64_t gray = n ^ (n >> 1);
    uint32_t x = 0;
    for (unsigned bit = 0; gray && bit < 32; ++bit, gray >>= 1)
        if (gray & 1) x ^= w[bit][dim];
    return x;
}

double asian_scalar_payoff_sum(const asian_price_request_t* req,
                               const uint32_t w[32][32]) {
    const uint64_t samples = req->num_blocks * (uint64_t)ASIAN_SOBOL_BLOCK_SIZE;
    const double dt = (double)req->t / 32.0;
    const double drift = ((double)req->r - 0.5*(double)req->sigma*(double)req->sigma)*dt;
    const double df = exp(-(double)req->r*(double)req->t);
    double total = 0.0;
    for (uint64_t n = 0; n < samples; ++n) {
        double live[16] = {0}, asset_sum = 0.0;
        for (int d = 0; d < 32; ++d) {
            const double z = asian_inverse_normal_u32(sobol_word(n, w, d));
            const int node = asian_bridge_node[d];
            double brownian;
            if (d == 0) {
                brownian = (double)req->sigma * sqrt((double)req->t) * z;
            } else {
                const int ls = asian_bridge_left_slot[d], rs = asian_bridge_right_slot[d];
                const double left = ls < 0 ? 0.0 : live[ls];
                const double right = live[rs];
                const double scale = (double)req->sigma * sqrt((double)req->t / (double)(1u << (asian_bridge_depth[d] + 1)));
                brownian = 0.5*(left + right) + scale*z;
            }
            if (asian_bridge_store_slot[d] >= 0) live[asian_bridge_store_slot[d]] = brownian;
            const double st = (double)req->s0 * exp(drift*(node+1) + brownian);
            asset_sum += isfinite(st) ? st : (st > 0.0 ? INFINITY : 0.0);
        }
        const double average = asset_sum / 32.0;
        const double intrinsic = req->type == ASIAN_CALL ? average-(double)req->k : (double)req->k-average;
        total += df * fmax(intrinsic, 0.0);
    }
    return total;
}
