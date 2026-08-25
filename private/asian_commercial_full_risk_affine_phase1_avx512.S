/*
 * Private K=1 compatibility leaves.  The qualified Phase-1 source is included
 * verbatim so that payoff, sensitivity, reduction and store macros stay owned
 * by the parent implementation.  Its existing entry points are renamed into
 * an unreferenced .text section; --gc-sections removes that donor section.
 * Only AAD_DUAL_ROUTE is replaced before the two affine arithmetic leaves are
 * instantiated in their own linked section.
 */
#define asian_genuine_aad_phase1_forward_arithmetic_call_diag asian_commercial_unused_forward_arithmetic_call
#define asian_genuine_aad_phase1_forward_arithmetic_put_diag asian_commercial_unused_forward_arithmetic_put
#define asian_genuine_aad_phase1_forward_cv_call_diag asian_commercial_unused_forward_cv_call
#define asian_genuine_aad_phase1_forward_cv_put_diag asian_commercial_unused_forward_cv_put
#define asian_genuine_aad_phase1_suffix_arithmetic_call_diag asian_commercial_unused_suffix_arithmetic_call
#define asian_genuine_aad_phase1_suffix_arithmetic_put_diag asian_commercial_unused_suffix_arithmetic_put
#define asian_genuine_aad_phase1_suffix_cv_call_diag asian_commercial_unused_suffix_cv_call
#define asian_genuine_aad_phase1_suffix_cv_put_diag asian_commercial_unused_suffix_cv_put
#include "../asian_genuine_aad_phase1_avx512.s"

.purgem AAD_DUAL_ROUTE

.equ COMMERCIAL_AFF_BASE,  320
.equ COMMERCIAL_AFF_DELTA, 384

/* Existing affine dual-payload provider shape: two selector-byte loads,
 * one low/high control construction, and four payload vpermd. */
.macro AAD_DUAL_ROUTE
    movq AAD_ROUTE_X(%rdi), %r8
    movq AAD_ROUTE_GROWTH(%rdi), %rsi
    kmovq %rsi, %k1
    movq AAD_ROUTE_MAP(%rdi), %r9
    movzbl 0(%r9,%rcx,2), %r11d
    movzbl 1(%r9,%rcx,2), %esi
    shll $6, %r11d
    vpbroadcastd %esi, %zmm18
    vpxord COMMERCIAL_AFF_BASE(%r9), %zmm18, %zmm18
    vpxord COMMERCIAL_AFF_DELTA(%r9), %zmm18, %zmm19

    vmovdqa32 0(%r8,%r11), %zmm2
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm3
    vpermd %zmm2, %zmm18, %zmm0
    vpermd %zmm3, %zmm19, %zmm1

    kmovq %k1, %r8
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm14
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm15
    vpermd %zmm14, %zmm18, %zmm12
    vpermd %zmm15, %zmm19, %zmm13
.endm

.section .text.asian_commercial_full_risk_affine,"ax",@progbits
AAD_FORWARD_FUNCTION asian_affine_family_full_risk_k1_affine_call_impl_diag,0,0
AAD_FORWARD_FUNCTION asian_affine_family_full_risk_k1_affine_put_impl_diag,1,0
