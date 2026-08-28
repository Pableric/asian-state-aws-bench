.include "private/asian_exp_p8_18diag.inc"

.equ C_D1,0
.equ C_D2_DONOR,8
.equ C_D2_MAP,16
.equ C_END_BYTES,24
.equ C_PATH_COUNT,28
.equ C_INV_PATHS,32
.equ C_COUPON2,40
.equ C_CALL2,44
.equ C_COUPON3,48
.equ C_CALL3,52
.equ C_TERMINAL,56
.equ C_LEG,64
.equ L_BYTES,48
.equ L_CALL2,0
.equ L_COUPON2,4
.equ L_CALL3,8
.equ L_COUPON3,12
.equ L_PROTECTION,16
.equ L_D1_PV,20
.equ L_PRIOR_THRESHOLD,24
.equ L_PRIOR_CDF,28
.equ L_A,32
.equ L_DOWNSIDE,36
.equ MAP_BASE_CONTROL,320
.equ MAP_HALF_DELTA,384

.section .rodata.autocall_d1_native_leaf,"a",@progbits
.p2align 6
.LLeafR34P: .long 0x3f000000,0x3fc55bf4,0x401529e3,0xbd6f39ed
.LLeafR34Q: .long 0x3f800000,0x4117783b,0x42045798,0x4231a9aa
.LLeafAbsMask: .long 0x7fffffff
.LLeafNegHalf: .long 0xbf000000
.LLeafOne: .long 0x3f800000
.LLeafTwo: .long 0x40000000
.LLeafEight: .long 0x41000000
.LLeafOneEighth: .long 0x3e000000
.LLeafLog2E: .long 0x3fb8aa3b
.LLeafLn2Hi: .long 0x3f318000
.LLeafLn2Lo: .long 0xb95e8083
.LLeafLn2: .long 0x3f317218

.section .text.autocall_d1_native_leaf,"ax",@progbits

.macro LEAF_EXP input, output, exponent, reduced
    vmulps .LLeafLog2E(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vmovaps %zmm\input, %zmm\reduced
    vfnmadd231ps .LLeafLn2Hi(%rip){1to16}, %zmm\exponent, %zmm\reduced
    vfnmadd231ps .LLeafLn2Lo(%rip){1to16}, %zmm\exponent, %zmm\reduced
    ASIAN_EXP_P8_18DIAG \reduced, \output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

.macro LEAF_CDF_EXP input, output, exponent, reduced
    vmulps .LLeafLog2E(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vfnmadd231ps .LLeafLn2(%rip){1to16}, %zmm\exponent, %zmm\input
    vbroadcastss .LAsianExpP8_18diag+24(%rip), %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+20(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+16(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+12(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+8(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+4(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag(%rip){1to16}, %zmm\input, %zmm\output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

/* Inline compact degree-3/4 vector Phi.  Input/output is zmm8; zmm9..zmm13
 * and k1..k3 are scratch.  Reciprocal is RCP14 plus one Newton step. */
.macro INLINE_R34_PHI
    vpmovd2m %zmm8,%k1
    vandps .LLeafAbsMask(%rip){1to16},%zmm8,%zmm8
    vcmpps $0x1d,.LLeafEight(%rip){1to16},%zmm8,%k2
    vminps .LLeafEight(%rip){1to16},%zmm8,%zmm8

    vmulps .LLeafOneEighth(%rip){1to16},%zmm8,%zmm13
    vbroadcastss .LLeafR34P+12(%rip),%zmm9
    vfmadd213ps .LLeafR34P+8(%rip){1to16},%zmm13,%zmm9
    vfmadd213ps .LLeafR34P+4(%rip){1to16},%zmm13,%zmm9
    vfmadd213ps .LLeafR34P(%rip){1to16},%zmm13,%zmm9
    vbroadcastss .LLeafR34Q+12(%rip),%zmm10
    vfmadd213ps .LLeafR34Q+8(%rip){1to16},%zmm13,%zmm10
    vfmadd213ps .LLeafR34Q+4(%rip){1to16},%zmm13,%zmm10
    vfmadd213ps .LLeafR34Q(%rip){1to16},%zmm13,%zmm10
    vrcp14ps %zmm10,%zmm11
    vmulps %zmm10,%zmm11,%zmm12
    vbroadcastss .LLeafTwo(%rip),%zmm13
    vsubps %zmm12,%zmm13,%zmm12
    vmulps %zmm12,%zmm11,%zmm11
    vmulps %zmm11,%zmm9,%zmm9

    vmulps %zmm8,%zmm8,%zmm10
    vmulps .LLeafNegHalf(%rip){1to16},%zmm10,%zmm10
    LEAF_CDF_EXP 10,11,12,13
    vmulps %zmm11,%zmm9,%zmm9
    vxorps %zmm10,%zmm10,%zmm10
    vbroadcastss .LLeafOne(%rip),%zmm11
    vsubps %zmm9,%zmm11,%zmm8
    vmovaps %zmm9,%zmm8{%k1}
    vmovaps %zmm10,%zmm8{%k2}
    knotw %k1,%k3
    kandw %k2,%k3,%k3
    vmovaps %zmm11,%zmm8{%k3}
.endm

.macro RESIDUAL_EXP leg_offset, output
    vbroadcastss C_LEG+\leg_offset+L_A(%rdi),%zmm8
    vmulps %zmm1,%zmm8,%zmm8
    LEAF_EXP 8,\output,9,10
.endm

.macro PROCESS_LEG leg_offset, expreg, accumulator
    vxorps %zmm5,%zmm5,%zmm5
    vbroadcastss C_LEG+\leg_offset+L_PRIOR_THRESHOLD(%rdi),%zmm6
    vbroadcastss C_LEG+\leg_offset+L_PRIOR_CDF(%rdi),%zmm7

    vbroadcastss C_LEG+\leg_offset+L_COUPON2(%rdi),%zmm8
    vsubps %zmm0,%zmm8,%zmm8
    INLINE_R34_PHI
    vsubps %zmm8,%zmm7,%zmm9
    vmaxps %zmm10,%zmm9,%zmm9
    vfmadd231ps C_COUPON2(%rdi){1to16},%zmm9,%zmm5
    vbroadcastss C_LEG+\leg_offset+L_CALL2(%rdi),%zmm8
    vsubps %zmm0,%zmm8,%zmm8
    vminps %zmm8,%zmm6,%zmm6
    INLINE_R34_PHI
    vsubps %zmm8,%zmm7,%zmm9
    vmaxps %zmm10,%zmm9,%zmm9
    vfmadd231ps C_CALL2(%rdi){1to16},%zmm9,%zmm5
    vminps %zmm8,%zmm7,%zmm7

    vbroadcastss C_LEG+\leg_offset+L_COUPON3(%rdi),%zmm8
    vsubps %zmm1,%zmm8,%zmm8
    INLINE_R34_PHI
    vsubps %zmm8,%zmm7,%zmm9
    vmaxps %zmm10,%zmm9,%zmm9
    vfmadd231ps C_COUPON3(%rdi){1to16},%zmm9,%zmm5
    vbroadcastss C_LEG+\leg_offset+L_CALL3(%rdi),%zmm8
    vsubps %zmm1,%zmm8,%zmm8
    vminps %zmm8,%zmm6,%zmm6
    INLINE_R34_PHI
    vsubps %zmm8,%zmm7,%zmm9
    vmaxps %zmm10,%zmm9,%zmm9
    vfmadd231ps C_CALL3(%rdi){1to16},%zmm9,%zmm5
    vminps %zmm8,%zmm7,%zmm7

    vbroadcastss C_LEG+\leg_offset+L_PROTECTION(%rdi),%zmm8
    vsubps %zmm1,%zmm8,%zmm8
    vminps %zmm8,%zmm6,%zmm6
    INLINE_R34_PHI
    vsubps %zmm8,%zmm7,%zmm9
    vmaxps %zmm10,%zmm9,%zmm9
    vfmadd231ps C_TERMINAL(%rdi){1to16},%zmm9,%zmm5
    vsubps C_LEG+\leg_offset+L_A(%rdi){1to16},%zmm6,%zmm8
    INLINE_R34_PHI
    vmulps %zmm\expreg,%zmm8,%zmm9
    vfmadd231ps C_LEG+\leg_offset+L_DOWNSIDE(%rdi){1to16},%zmm9,%zmm5
    vaddps %zmm5,%zmm\accumulator,%zmm\accumulator
.endm

.macro REDUCE_PRICE low, high, leg_offset, output_offset
    vaddps %zmm\high,%zmm\low,%zmm0
    vextractf32x4 $1,%zmm0,%xmm1
    vextractf32x4 $2,%zmm0,%xmm2
    vextractf32x4 $3,%zmm0,%xmm3
    vaddps %xmm1,%xmm0,%xmm0
    vaddps %xmm3,%xmm2,%xmm2
    vaddps %xmm2,%xmm0,%xmm0
    vmovhlps %xmm0,%xmm0,%xmm1
    vaddps %xmm1,%xmm0,%xmm0
    vshufps $1,%xmm0,%xmm0,%xmm1
    vaddss %xmm1,%xmm0,%xmm0
    vcvtss2sd %xmm0,%xmm0,%xmm0
    vmulsd C_INV_PATHS(%rdi),%xmm0,%xmm0
    vxorpd %xmm4,%xmm4,%xmm4
    vcvtss2sd C_LEG+\leg_offset+L_D1_PV(%rdi),%xmm4,%xmm4
    vaddsd %xmm4,%xmm0,%xmm0
    vmovsd %xmm0,\output_offset(%rsi)
.endm

.p2align 6
.globl autocall_d1_native_price5_leaf
.type autocall_d1_native_price5_leaf,@function
autocall_d1_native_price5_leaf:
    movq C_D1(%rdi),%rdx
    movq C_D2_MAP(%rdi),%r8
    movq C_D2_DONOR(%rdi),%r9
    vxorps %zmm20,%zmm20,%zmm20
    vxorps %zmm21,%zmm21,%zmm21
    vxorps %zmm22,%zmm22,%zmm22
    vxorps %zmm23,%zmm23,%zmm23
    vxorps %zmm24,%zmm24,%zmm24
    vxorps %zmm25,%zmm25,%zmm25
    vxorps %zmm26,%zmm26,%zmm26
    vxorps %zmm27,%zmm27,%zmm27
    vxorps %zmm28,%zmm28,%zmm28
    vxorps %zmm29,%zmm29,%zmm29
    xorq %rax,%rax
.Ld1_native_packet:
    movq %rax,%rcx
    shrq $7,%rcx
    movzbl 0(%r8,%rcx,2),%r11d
    movzbl 1(%r8,%rcx,2),%r10d
    shll $6,%r11d
    vmovdqa32 MAP_BASE_CONTROL(%r8),%zmm12
    vmovdqa32 MAP_HALF_DELTA(%r8),%zmm13
    vpbroadcastd %r10d,%zmm14
    vpxord %zmm14,%zmm12,%zmm12
    vpxord %zmm12,%zmm13,%zmm13
    vmovdqa32 0(%r9,%r11),%zmm14
    xorl $64,%r11d
    vmovdqa32 0(%r9,%r11),%zmm15
    vpermd %zmm14,%zmm12,%zmm0
    vpermd %zmm15,%zmm13,%zmm16
    vmovaps 0(%rdx,%rax),%zmm1
    vmovaps 64(%rdx,%rax),%zmm17
    vaddps %zmm0,%zmm1,%zmm1
    RESIDUAL_EXP L_BYTES,2
    RESIDUAL_EXP 3*L_BYTES,3
    RESIDUAL_EXP 4*L_BYTES,4
    PROCESS_LEG 0*L_BYTES,2,20
    PROCESS_LEG 1*L_BYTES,2,22
    PROCESS_LEG 2*L_BYTES,2,24
    PROCESS_LEG 3*L_BYTES,3,26
    PROCESS_LEG 4*L_BYTES,4,28

    vmovaps %zmm16,%zmm0
    vaddps %zmm16,%zmm17,%zmm1
    RESIDUAL_EXP L_BYTES,2
    RESIDUAL_EXP 3*L_BYTES,3
    RESIDUAL_EXP 4*L_BYTES,4
    PROCESS_LEG 0*L_BYTES,2,21
    PROCESS_LEG 1*L_BYTES,2,23
    PROCESS_LEG 2*L_BYTES,2,25
    PROCESS_LEG 3*L_BYTES,3,27
    PROCESS_LEG 4*L_BYTES,4,29
    addq $128,%rax
    cmpl C_END_BYTES(%rdi),%eax
    jb .Ld1_native_packet

    REDUCE_PRICE 20,21,0*L_BYTES,0
    REDUCE_PRICE 22,23,1*L_BYTES,8
    REDUCE_PRICE 24,25,2*L_BYTES,16
    REDUCE_PRICE 26,27,3*L_BYTES,24
    REDUCE_PRICE 28,29,4*L_BYTES,32
    vzeroupper
    ret
.size autocall_d1_native_price5_leaf,.-autocall_d1_native_price5_leaf

.section .note.GNU-stack,"",@progbits
