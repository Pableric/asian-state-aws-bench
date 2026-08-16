#include "asian_kernels.h"
#include "asian_reference.h"
#include "private/asian_generated_assets.h"
#include <immintrin.h>
#include <math.h>
#include <stdint.h>

typedef struct {
    const uint32_t (*w)[32];
    const uint32_t (*lanes)[32];
    const uint16_t *initial;
    const uint16_t (*phase)[8];
    const uint16_t (*block)[19];
    const uint16_t *line_delta;
    const uint32_t (*controls)[16];
} plan_t;

static inline uint32_t block_base(uint64_t block, const uint32_t w[32][32], int d) {
    uint64_t n = block << 13, gray = n ^ (n >> 1);
    uint32_t x = 0;
    for (unsigned b = 0; gray && b < 32; ++b, gray >>= 1)
        if (gray & 1) x ^= w[b][d];
    return x;
}

static inline __m512 fast_exp(__m512 x) {
    const __m512 log2e = _mm512_set1_ps(1.4426950408889634f);
    const __m512 ln2hi = _mm512_set1_ps(0.693145751953125f);
    const __m512 ln2lo = _mm512_set1_ps(1.428606765330187e-6f);
    __m512 n = _mm512_roundscale_ps(_mm512_mul_ps(x, log2e), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    __m512 r = _mm512_fnmadd_ps(n, ln2hi, x);
    r = _mm512_fnmadd_ps(n, ln2lo, r);
    __m512 p = _mm512_set1_ps(1.0f/720.0f);
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(1.0f/120.0f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(1.0f/24.0f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(1.0f/6.0f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(0.5f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(1.0f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(1.0f));
    return _mm512_scalef_ps(p, n);
}

static inline __m512 patch_tail(__m512i words, __m512 z) {
    const uint32_t cut = (uint32_t)(0.02425 * 4294967296.0);
    __mmask16 tail = _mm512_cmplt_epu32_mask(words, _mm512_set1_epi32((int)cut)) |
                     _mm512_cmp_epu32_mask(words, _mm512_set1_epi32((int)(0xffffffffu-cut)), _MM_CMPINT_GT);
    if (tail) {
        uint32_t raw[16] __attribute__((aligned(64)));
        float zv[16] __attribute__((aligned(64)));
        _mm512_store_si512(raw, words); _mm512_store_ps(zv, z);
        while (tail) {
            const unsigned lane=(unsigned)__builtin_ctz((unsigned)tail);
            zv[lane]=(float)asian_inverse_normal_u32(raw[lane]); tail&=tail-1;
        }
        z=_mm512_load_ps(zv);
    }
    return z;
}

static inline void gaussian_pair(__m512i words0, __m512i words1, uint16_t state,
                                 uint16_t delta, const uint32_t controls[16],
                                 __m512 *z0, __m512 *z1) {
    const unsigned row0 = state & 0x7f, row1 = row0 ^ delta;
    __m512i index = _mm512_xor_si512(_mm512_load_si512(controls),
                                    _mm512_set1_epi32((state >> 8) & 15));
    __m512 c0a = _mm512_load_ps(asian_gauss_c0 + 16*row0);
    __m512 c0b = _mm512_load_ps(asian_gauss_c0 + 16*row1);
    __m512 c1a = _mm512_load_ps(asian_gauss_c1 + 16*row0);
    __m512 c1b = _mm512_load_ps(asian_gauss_c1 + 16*row1);
    __m512 c0 = _mm512_permutex2var_ps(c0a, index, c0b);
    __m512 c1 = _mm512_permutex2var_ps(c1a, index, c1b);
    __m512i bits0=_mm512_or_si512(_mm512_srli_epi32(words0,9),_mm512_set1_epi32(0x3f800000));
    __m512i bits1=_mm512_or_si512(_mm512_srli_epi32(words1,9),_mm512_set1_epi32(0x3f800000));
    *z0=patch_tail(words0,_mm512_fmadd_ps(c1,_mm512_castsi512_ps(bits0),c0));
    *z1=patch_tail(words1,_mm512_fmadd_ps(c1,_mm512_castsi512_ps(bits1),c0));
}

static inline void consume_dimension(const asian_price_request_t *req, int d,
                                     __m512 z0, __m512 z1, __m512 live0[16],
                                     __m512 live1[16], __m512 *sum0, __m512 *sum1) {
    const int node = asian_bridge_node[d];
    __m512 b0, b1;
    if (d == 0) {
        const float scale = req->sigma * sqrtf(req->t);
        b0 = _mm512_mul_ps(z0, _mm512_set1_ps(scale));
        b1 = _mm512_mul_ps(z1, _mm512_set1_ps(scale));
    } else {
        const int ls = asian_bridge_left_slot[d], rs = asian_bridge_right_slot[d];
        const __m512 l0 = ls < 0 ? _mm512_setzero_ps() : live0[ls];
        const __m512 l1 = ls < 0 ? _mm512_setzero_ps() : live1[ls];
        const float scale = req->sigma * sqrtf(req->t / (float)(1u << (asian_bridge_depth[d] + 1)));
        b0 = _mm512_fmadd_ps(_mm512_set1_ps(0.5f), _mm512_add_ps(l0, live0[rs]), _mm512_mul_ps(_mm512_set1_ps(scale), z0));
        b1 = _mm512_fmadd_ps(_mm512_set1_ps(0.5f), _mm512_add_ps(l1, live1[rs]), _mm512_mul_ps(_mm512_set1_ps(scale), z1));
    }
    const int slot = asian_bridge_store_slot[d];
    if (slot >= 0) { live0[slot] = b0; live1[slot] = b1; }
    const float drift = (req->r - 0.5f*req->sigma*req->sigma) * (req->t/32.0f) * (float)(node+1);
    *sum0 = _mm512_add_ps(*sum0, fast_exp(_mm512_add_ps(b0, _mm512_set1_ps(drift))));
    *sum1 = _mm512_add_ps(*sum1, fast_exp(_mm512_add_ps(b1, _mm512_set1_ps(drift))));
}

static double kernel_plan(const asian_price_request_t *req, const plan_t *plan, int final_z) {
    double total = 0.0;
    uint16_t states[32];
    for (int d=0; d<32; ++d) states[d] = plan->initial[d];
    for (uint64_t block=0; block<req->num_blocks; ++block) {
        if (block) {
            const unsigned c = (unsigned)__builtin_ctzll(block);
            for (int d=0; d<32; ++d) states[d] ^= plan->block[d][c];
        }
        uint32_t bases[32];
        for (int d=0; d<32; ++d) bases[d] = block_base(block, plan->w, d);
        uint16_t phase_states[32];
        for (int d=0; d<32; ++d) phase_states[d] = states[d];
        for (unsigned phase=0; phase<256; ++phase) {
            if (phase) {
                const unsigned c = (unsigned)__builtin_ctz(phase);
                for (int d=0; d<32; ++d) {
                    bases[d] ^= plan->w[c][d];
                    phase_states[d] ^= plan->phase[d][c];
                }
            }
            __m512 live0[16], live1[16];
            __m512 sum0=_mm512_setzero_ps(), sum1=_mm512_setzero_ps();
            __m512 anchor0=_mm512_setzero_ps(), anchor1=_mm512_setzero_ps();
            __m512i index0=_mm512_set_epi32(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);
            __m512i index1=_mm512_set_epi32(31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16);
            for (int d=0; d<32; ++d) {
                __m512 z0, z1;
                if (!final_z || d == 0) {
                    __m512i lane0 = _mm512_load_si512(plan->lanes[d]);
                    __m512i lane1 = _mm512_load_si512(plan->lanes[d]+16);
                    __m512i words0 = _mm512_xor_si512(lane0, _mm512_set1_epi32((int)bases[d]));
                    __m512i words1 = _mm512_xor_si512(lane1, _mm512_set1_epi32((int)bases[d]));
                    gaussian_pair(words0,words1,phase_states[d],plan->line_delta[d],plan->controls[d],&z0,&z1);
                    if (d == 0) { anchor0=z0; anchor1=z1; }
                } else {
                    const unsigned bit = (unsigned)__builtin_ctz((unsigned)d);
                    index0 = _mm512_xor_si512(index0, _mm512_load_si512(asian_final_z_gray_masks[bit]));
                    index1 = _mm512_xor_si512(index1, _mm512_load_si512(asian_final_z_gray_masks[bit]+16));
                    z0 = _mm512_permutex2var_ps(anchor0, index0, anchor1);
                    z1 = _mm512_permutex2var_ps(anchor0, index1, anchor1);
                }
                consume_dimension(req,d,z0,z1,live0,live1,&sum0,&sum1);
            }
            const __m512 avgscale = _mm512_set1_ps(req->s0/32.0f);
            __m512 intrinsic0, intrinsic1;
            if (req->type == ASIAN_CALL) {
                intrinsic0 = _mm512_sub_ps(_mm512_mul_ps(avgscale,sum0),_mm512_set1_ps(req->k));
                intrinsic1 = _mm512_sub_ps(_mm512_mul_ps(avgscale,sum1),_mm512_set1_ps(req->k));
            } else {
                intrinsic0 = _mm512_sub_ps(_mm512_set1_ps(req->k),_mm512_mul_ps(avgscale,sum0));
                intrinsic1 = _mm512_sub_ps(_mm512_set1_ps(req->k),_mm512_mul_ps(avgscale,sum1));
            }
            intrinsic0=_mm512_max_ps(intrinsic0,_mm512_setzero_ps());
            intrinsic1=_mm512_max_ps(intrinsic1,_mm512_setzero_ps());
            total += (double)(_mm512_reduce_add_ps(intrinsic0)+_mm512_reduce_add_ps(intrinsic1));
        }
    }
    return total * exp(-(double)req->r*(double)req->t);
}

#define PLAN(NAME) {asian_w_##NAME,asian_lanes_##NAME,asian_##NAME##_initial,asian_##NAME##_phase,asian_##NAME##_block,asian_##NAME##_line_delta,asian_##NAME##_controls}
double asian_kernel_rank1(const asian_price_request_t* r){ static const plan_t p=PLAN(rank1); return kernel_plan(r,&p,0); }
double asian_kernel_pair(const asian_price_request_t* r){ static const plan_t p=PLAN(pair); return kernel_plan(r,&p,0); }
double asian_kernel_final_z(const asian_price_request_t* r){ static const plan_t p=PLAN(final_z); return kernel_plan(r,&p,1); }
