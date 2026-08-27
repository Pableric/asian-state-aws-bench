/*
 * Private fused-Gamma descendants of the qualified affine K=1 Phase-1 leaf.
 * Parent entry points and sources remain untouched.  The old donor is included
 * so the evolution, base payoff and four existing risk reductions stay owned
 * by the qualified implementation.
 */
#define asian_genuine_aad_phase1_forward_arithmetic_call_diag asian_gamma_unused_forward_arithmetic_call
#define asian_genuine_aad_phase1_forward_arithmetic_put_diag asian_gamma_unused_forward_arithmetic_put
#define asian_genuine_aad_phase1_forward_cv_call_diag asian_gamma_unused_forward_cv_call
#define asian_genuine_aad_phase1_forward_cv_put_diag asian_gamma_unused_forward_cv_put
#define asian_genuine_aad_phase1_suffix_arithmetic_call_diag asian_gamma_unused_suffix_arithmetic_call
#define asian_genuine_aad_phase1_suffix_arithmetic_put_diag asian_gamma_unused_suffix_arithmetic_put
#define asian_genuine_aad_phase1_suffix_cv_call_diag asian_gamma_unused_suffix_cv_call
#define asian_genuine_aad_phase1_suffix_cv_put_diag asian_gamma_unused_suffix_cv_put
#include "../asian_genuine_aad_phase1_avx512.s"

.purgem AAD_DUAL_ROUTE

.equ GAMMA_AFF_BASE,       320
.equ GAMMA_AFF_DELTA,      384
.equ GAMMA_CTX_BUMP,        64
.equ GAMMA_CTX_INV_BUMP2,   72

/* zmm2/zmm3 cease to be live after routing x, so reuse them for the growth
 * source lines.  This leaves zmm14/zmm15 available for the persistent minus
 * payoff accumulators without changing any routed payload bits. */
.macro AAD_DUAL_ROUTE
    movq AAD_ROUTE_X(%rdi), %r8
    movq AAD_ROUTE_GROWTH(%rdi), %rsi
    kmovq %rsi, %k1
    movq AAD_ROUTE_MAP(%rdi), %r9
    movzbl 0(%r9,%rcx,2), %r11d
    movzbl 1(%r9,%rcx,2), %esi
    shll $6, %r11d
    vpbroadcastd %esi, %zmm18
    vpxord GAMMA_AFF_BASE(%r9), %zmm18, %zmm18
    vpxord GAMMA_AFF_DELTA(%r9), %zmm18, %zmm19

    vmovdqa32 0(%r8,%r11), %zmm2
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm3
    vpermd %zmm2, %zmm18, %zmm0
    vpermd %zmm3, %zmm19, %zmm1

    kmovq %k1, %r8
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm2
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm3
    vpermd %zmm2, %zmm18, %zmm12
    vpermd %zmm3, %zmm19, %zmm13
.endm

.macro GAMMA_ZERO_ACCUMULATORS
    vxorps %zmm10, %zmm10, %zmm10
    vxorps %zmm11, %zmm11, %zmm11
    vxorps %zmm14, %zmm14, %zmm14
    vxorps %zmm15, %zmm15, %zmm15
.endm

/* Accumulate the two additional discounted payoff samples.  The qualified
 * base payoff macro runs separately and byte-for-byte unchanged. */
.macro GAMMA_PAY_HALF areg, plusacc, minusacc, put
    vmulps AAD_CTX_INV_S0(%rdi){1to16}, %zmm\areg, %zmm0
    vmovaps %zmm\areg, %zmm2
    vfmadd231ps GAMMA_CTX_BUMP(%rdi){1to16}, %zmm0, %zmm2
    vmovaps %zmm\areg, %zmm3
    vfnmadd231ps GAMMA_CTX_BUMP(%rdi){1to16}, %zmm0, %zmm3
    .if \put
        vbroadcastss AAD_CTX_STRIKE(%rdi), %zmm12
        vmovaps %zmm12, %zmm13
        vsubps %zmm2, %zmm12, %zmm2
        vsubps %zmm3, %zmm13, %zmm3
    .else
        vsubps AAD_CTX_STRIKE(%rdi){1to16}, %zmm2, %zmm2
        vsubps AAD_CTX_STRIKE(%rdi){1to16}, %zmm3, %zmm3
    .endif
    vmaxps %zmm31, %zmm2, %zmm2
    vmaxps %zmm31, %zmm3, %zmm3
    vmulps AAD_CTX_DISCOUNT(%rdi){1to16}, %zmm2, %zmm2
    vmulps AAD_CTX_DISCOUNT(%rdi){1to16}, %zmm3, %zmm3
    vaddps %zmm2, %zmm\plusacc, %zmm\plusacc
    vaddps %zmm3, %zmm\minusacc, %zmm\minusacc
.endm

.macro GAMMA_REDUCE_TO lo, hi, target
    vaddps %zmm\hi, %zmm\lo, %zmm0
    vextractf32x4 $1, %zmm0, %xmm1
    vextractf32x4 $2, %zmm0, %xmm2
    vextractf32x4 $3, %zmm0, %xmm3
    vaddps %xmm1, %xmm0, %xmm0
    vaddps %xmm3, %xmm2, %xmm2
    vaddps %xmm2, %xmm0, %xmm0
    vmovhlps %xmm0, %xmm0, %xmm1
    vaddps %xmm1, %xmm0, %xmm0
    vshufps $1, %xmm0, %xmm0, %xmm1
    vaddss %xmm1, %xmm0, %xmm0
    vcvtss2sd %xmm0, %xmm0, %xmm0
    vmulsd .LAadInvPaths(%rip), %xmm0, %xmm0
    vmovapd %xmm0, %xmm\target
.endm

.macro GAMMA_FINISH put
    kmovq %k0, %r8
    kmovq %k4, %r9
    .if \put
      .set .LGammaExactBase, AAD_CTRL_PUT
    .else
      .set .LGammaExactBase, AAD_CTRL_CALL
    .endif
    /* These are the exact qualified four-field reductions and stores. */
    AAD_REDUCE_ONE 20,21,0,.LGammaExactBase+0,0
    AAD_REDUCE_ONE 22,23,8,.LGammaExactBase+8,0
    AAD_REDUCE_ONE 24,25,16,.LGammaExactBase+16,0
    AAD_REDUCE_ONE 26,27,24,.LGammaExactBase+24,0

    GAMMA_REDUCE_TO 10,11,4
    GAMMA_REDUCE_TO 14,15,5
    vmovsd 0(%r8), %xmm0
    vaddsd %xmm0, %xmm0, %xmm0
    vsubsd %xmm0, %xmm4, %xmm4
    vaddsd %xmm5, %xmm4, %xmm4
    kmovq %k6, %rdi
    vmulsd GAMMA_CTX_INV_BUMP2(%rdi), %xmm4, %xmm4
    vmovsd %xmm4, 32(%r8)
    vzeroupper
    ret
.endm

.macro GAMMA_FORWARD_FUNCTION name, put
.p2align 6
.globl \name
.type \name,@function
\name:
    kmovq %rsi, %k0
    kmovq %rdi, %k6
    movq AAD_CTX_ROUTES(%rdi), %r8
    kmovq %r8, %k2
    movq AAD_CTX_CONTROLS(%rdi), %r8
    kmovq %r8, %k4
    kmovd AAD_CTX_ROUTE_COUNT(%rdi), %k5
    AAD_ZERO_OUTPUT_ACCUMULATORS
    GAMMA_ZERO_ACCUMULATORS
    xorq %rax, %rax
.Lgamma_packet_\@:
    kmovq %k6, %rdi
    vbroadcastss AAD_CTX_S0(%rdi), %zmm4
    vmovaps %zmm4, %zmm5
    vxorps %zmm6, %zmm6, %zmm6
    vxorps %zmm7, %zmm7, %zmm7
    vxorps %zmm8, %zmm8, %zmm8
    vxorps %zmm9, %zmm9, %zmm9
    vxorps %zmm16, %zmm16, %zmm16
    vxorps %zmm17, %zmm17, %zmm17
    vxorps %zmm28, %zmm28, %zmm28
    vxorps %zmm29, %zmm29, %zmm29
    movq %rax, %rcx
    shrq $7, %rcx
    kmovq %rax, %k7
    kmovq %k2, %rdi
    movq AAD_ROUTE_X(%rdi), %r8
    movq AAD_ROUTE_GROWTH(%rdi), %r9
    vmovaps 0(%r8,%rax), %zmm0
    vmovaps 64(%r8,%rax), %zmm1
    vmovaps 0(%r9,%rax), %zmm12
    vmovaps 64(%r9,%rax), %zmm13
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    vaddps %zmm0, %zmm8, %zmm8
    vaddps %zmm1, %zmm9, %zmm9
    kmovq %k4, %r8
    vfmadd231ps AAD_CTRL_WEIGHTS(%r8){1to16}, %zmm4, %zmm16
    vfmadd231ps AAD_CTRL_WEIGHTS(%r8){1to16}, %zmm5, %zmm17
    vfmadd231ps %zmm4, %zmm8, %zmm28
    vfmadd231ps %zmm5, %zmm9, %zmm29
    addq $AAD_ROUTE_BYTES, %rdi
    kmovd %k5, %eax
.Lgamma_route_\@:
    AAD_DUAL_ROUTE
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    vaddps %zmm0, %zmm8, %zmm8
    vaddps %zmm1, %zmm9, %zmm9
    movl AAD_ROUTE_INDEX(%rdi), %edx
    kmovq %k4, %r8
    vfmadd231ps AAD_CTRL_WEIGHTS(%r8,%rdx,4){1to16}, %zmm4, %zmm16
    vfmadd231ps AAD_CTRL_WEIGHTS(%r8,%rdx,4){1to16}, %zmm5, %zmm17
    vfmadd231ps %zmm4, %zmm8, %zmm28
    vfmadd231ps %zmm5, %zmm9, %zmm29
    addq $AAD_ROUTE_BYTES, %rdi
    decl %eax
    jne .Lgamma_route_\@
    kmovq %k7, %rax
    kmovq %k6, %rdi
    vmulps AAD_CTX_INV_N(%rdi){1to16}, %zmm6, %zmm6
    vmulps AAD_CTX_INV_N(%rdi){1to16}, %zmm7, %zmm7
    vmulps AAD_CTX_DT_OVER_N(%rdi){1to16}, %zmm16, %zmm16
    vmulps AAD_CTX_DT_OVER_N(%rdi){1to16}, %zmm17, %zmm17
    vmulps AAD_CTX_INV_N(%rdi){1to16}, %zmm28, %zmm28
    vmulps AAD_CTX_INV_N(%rdi){1to16}, %zmm29, %zmm29
    vfnmadd231ps AAD_CTX_C(%rdi){1to16}, %zmm16, %zmm28
    vfnmadd231ps AAD_CTX_C(%rdi){1to16}, %zmm17, %zmm29
    vmulps AAD_CTX_INV_SIGMA(%rdi){1to16}, %zmm28, %zmm28
    vmulps AAD_CTX_INV_SIGMA(%rdi){1to16}, %zmm29, %zmm29
    kmovq %k4, %r8
    GAMMA_PAY_HALF 6,10,14,\put
    AAD_PAY_HALF 6,16,28,12,10,20,22,24,26,\put,0
    GAMMA_PAY_HALF 7,11,15,\put
    AAD_PAY_HALF 7,17,29,13,11,21,23,25,27,\put,0
    addq $128, %rax
    cmpq $AAD_PATH_BYTES, %rax
    jb .Lgamma_packet_\@
    GAMMA_FINISH \put
.size \name,.-\name
.endm

.section .text.asian_full_risk_gamma_affine,"ax",@progbits
GAMMA_FORWARD_FUNCTION asian_full_risk_gamma_affine_call_impl_diag,0
GAMMA_FORWARD_FUNCTION asian_full_risk_gamma_affine_put_impl_diag,1
