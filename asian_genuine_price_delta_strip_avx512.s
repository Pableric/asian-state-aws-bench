.include "private/asian_exp_p8_18diag.inc"

.equ STRIP_INV_TOTAL, 24
.equ STRIP_INITIAL_Q, 28
.equ STRIP_DISCOUNT, 32
.equ STRIP_DELTA_Q_SCALE, 36
.equ STRIP_DELTA_G_SCALE, 40
.equ STRIP_LOG_BASE, 44
.equ STRIKE_BYTES, 64
.equ STRIKE_VALUE, 0
.equ STRIKE_SIGN, 4
.equ STRIKE_GEO_PRICE, 8
.equ STRIKE_GEO_DELTA, 16
.equ STRIKE_CALL_PRICE_ADJUST, 24
.equ STRIKE_PUT_PRICE_ADJUST, 32
.equ STRIKE_CALL_DELTA_ADJUST, 40
.equ STRIKE_PUT_DELTA_ADJUST, 48
.equ OUTPUT_BYTES, 32
.equ OUTPUT_CALL_PRICE, 0
.equ OUTPUT_PUT_PRICE, 8
.equ OUTPUT_CALL_DELTA, 16
.equ OUTPUT_PUT_DELTA, 24
.equ PATH_BYTES, 16384

.section .rodata
.p2align 4
.Lstrip_exp_log2e: .long 0x3fb8aa3b
.Lstrip_exp_ln2hi: .long 0x3f318000
.Lstrip_exp_ln2lo: .long 0xb95e8083
.Lstrip_inv_paths: .quad 0x3f30000000000000

.section .text

.macro STRIP_EXP input, output, exponent, reduced
    vmulps .Lstrip_exp_log2e(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vmovaps %zmm\input, %zmm\reduced
    vfnmadd231ps .Lstrip_exp_ln2hi(%rip){1to16}, %zmm\exponent, %zmm\reduced
    vfnmadd231ps .Lstrip_exp_ln2lo(%rip){1to16}, %zmm\exponent, %zmm\reduced
    ASIAN_EXP_P8_18DIAG \reduced, \output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

.p2align 6
.globl asian_genuine_strip_l_to_g_diag
.type asian_genuine_strip_l_to_g_diag,@function
asian_genuine_strip_l_to_g_diag:
    xorq %rax, %rax
.Lstrip_l_to_g_loop:
    vmovaps 0(%rdi,%rax), %zmm4
    vmovaps 64(%rdi,%rax), %zmm5
    vaddps STRIP_LOG_BASE(%rsi){1to16}, %zmm4, %zmm4
    vaddps STRIP_LOG_BASE(%rsi){1to16}, %zmm5, %zmm5
    STRIP_EXP 4, 6, 8, 10
    STRIP_EXP 5, 7, 9, 11
    vmovaps %zmm6, 0(%rdx,%rax)
    vmovaps %zmm7, 64(%rdx,%rax)
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lstrip_l_to_g_loop
    vzeroupper
    ret
.size asian_genuine_strip_l_to_g_diag,.-asian_genuine_strip_l_to_g_diag

.macro PRICE_ONE index, strike_reg, acc_reg, a_reg, g_reg, cv
    vsubps %zmm\strike_reg, %zmm\a_reg, %zmm26
    vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rcx){1to16}, %zmm26, %zmm26
    vmaxps %zmm28, %zmm26, %zmm26
    .if \cv
        vsubps %zmm\strike_reg, %zmm\g_reg, %zmm27
        vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rcx){1to16}, %zmm27, %zmm27
        vmaxps %zmm28, %zmm27, %zmm27
        vsubps %zmm27, %zmm26, %zmm26
    .endif
    vmulps STRIP_DISCOUNT(%rdx){1to16}, %zmm26, %zmm26
    vaddps %zmm26, %zmm\acc_reg, %zmm\acc_reg
.endm

.macro REDUCE_PRICE index, lo_reg, hi_reg, cv
    vaddps %zmm\hi_reg, %zmm\lo_reg, %zmm0
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
    vmulsd .Lstrip_inv_paths(%rip), %xmm0, %xmm0
    .if \cv
        vaddsd STRIKE_GEO_PRICE+STRIKE_BYTES*\index(%rcx), %xmm0, %xmm0
    .endif
    vmovapd %xmm0, %xmm1
    vaddsd STRIKE_CALL_PRICE_ADJUST+STRIKE_BYTES*\index(%rcx), %xmm0, %xmm0
    vaddsd STRIKE_PUT_PRICE_ADJUST+STRIKE_BYTES*\index(%rcx), %xmm1, %xmm1
    vmovsd %xmm0, OUTPUT_CALL_PRICE+OUTPUT_BYTES*\index(%r8)
    vmovsd %xmm1, OUTPUT_PUT_PRICE+OUTPUT_BYTES*\index(%r8)
.endm

.macro LOAD_STRIKES count
    vbroadcastss STRIKE_VALUE(%rcx), %zmm0
    .if \count > 1
      vbroadcastss STRIKE_VALUE+STRIKE_BYTES(%rcx), %zmm1
      vbroadcastss STRIKE_VALUE+STRIKE_BYTES*2(%rcx), %zmm2
      vbroadcastss STRIKE_VALUE+STRIKE_BYTES*3(%rcx), %zmm3
    .endif
    .if \count > 4
      vbroadcastss STRIKE_VALUE+STRIKE_BYTES*4(%rcx), %zmm4
      vbroadcastss STRIKE_VALUE+STRIKE_BYTES*5(%rcx), %zmm5
      vbroadcastss STRIKE_VALUE+STRIKE_BYTES*6(%rcx), %zmm6
      vbroadcastss STRIKE_VALUE+STRIKE_BYTES*7(%rcx), %zmm7
    .endif
.endm

.macro ZERO_ACCUMULATORS count
    vxorps %zmm8, %zmm8, %zmm8
    vxorps %zmm16, %zmm16, %zmm16
    .if \count > 1
      vxorps %zmm9, %zmm9, %zmm9
      vxorps %zmm10, %zmm10, %zmm10
      vxorps %zmm11, %zmm11, %zmm11
      vxorps %zmm17, %zmm17, %zmm17
      vxorps %zmm18, %zmm18, %zmm18
      vxorps %zmm19, %zmm19, %zmm19
    .endif
    .if \count > 4
      vxorps %zmm12, %zmm12, %zmm12
      vxorps %zmm13, %zmm13, %zmm13
      vxorps %zmm14, %zmm14, %zmm14
      vxorps %zmm15, %zmm15, %zmm15
      vxorps %zmm20, %zmm20, %zmm20
      vxorps %zmm21, %zmm21, %zmm21
      vxorps %zmm22, %zmm22, %zmm22
      vxorps %zmm23, %zmm23, %zmm23
    .endif
.endm

.macro PRICE_STEPS count, acc_base, a_reg, g_reg, cv
    .if \acc_base == 8
      PRICE_ONE 0,0,8,\a_reg,\g_reg,\cv
      .if \count > 1
        PRICE_ONE 1,1,9,\a_reg,\g_reg,\cv
        PRICE_ONE 2,2,10,\a_reg,\g_reg,\cv
        PRICE_ONE 3,3,11,\a_reg,\g_reg,\cv
      .endif
      .if \count > 4
        PRICE_ONE 4,4,12,\a_reg,\g_reg,\cv
        PRICE_ONE 5,5,13,\a_reg,\g_reg,\cv
        PRICE_ONE 6,6,14,\a_reg,\g_reg,\cv
        PRICE_ONE 7,7,15,\a_reg,\g_reg,\cv
      .endif
    .else
      PRICE_ONE 0,0,16,\a_reg,\g_reg,\cv
      .if \count > 1
        PRICE_ONE 1,1,17,\a_reg,\g_reg,\cv
        PRICE_ONE 2,2,18,\a_reg,\g_reg,\cv
        PRICE_ONE 3,3,19,\a_reg,\g_reg,\cv
      .endif
      .if \count > 4
        PRICE_ONE 4,4,20,\a_reg,\g_reg,\cv
        PRICE_ONE 5,5,21,\a_reg,\g_reg,\cv
        PRICE_ONE 6,6,22,\a_reg,\g_reg,\cv
        PRICE_ONE 7,7,23,\a_reg,\g_reg,\cv
      .endif
    .endif
.endm

.macro REDUCE_PRICES count, cv
    REDUCE_PRICE 0,8,16,\cv
    .if \count > 1
      REDUCE_PRICE 1,9,17,\cv
      REDUCE_PRICE 2,10,18,\cv
      REDUCE_PRICE 3,11,19,\cv
    .endif
    .if \count > 4
      REDUCE_PRICE 4,12,20,\cv
      REDUCE_PRICE 5,13,21,\cv
      REDUCE_PRICE 6,14,22,\cv
      REDUCE_PRICE 7,15,23,\cv
    .endif
.endm

.macro PRICE_LEAF name, count, cv
.p2align 6
.globl \name
.type \name,@function
\name:
    LOAD_STRIKES \count
    ZERO_ACCUMULATORS \count
    vxorps %zmm28, %zmm28, %zmm28
    xorq %rax, %rax
.Lloop_\name:
    vmovaps 0(%rdi,%rax), %zmm24
    vaddps STRIP_INITIAL_Q(%rdx){1to16}, %zmm24, %zmm24
    vmulps STRIP_INV_TOTAL(%rdx){1to16}, %zmm24, %zmm24
    .if \cv
      vmovaps 0(%rsi,%rax), %zmm25
    .endif
    PRICE_STEPS \count,8,24,25,\cv
    vmovaps 64(%rdi,%rax), %zmm24
    vaddps STRIP_INITIAL_Q(%rdx){1to16}, %zmm24, %zmm24
    vmulps STRIP_INV_TOTAL(%rdx){1to16}, %zmm24, %zmm24
    .if \cv
      vmovaps 64(%rsi,%rax), %zmm25
    .endif
    PRICE_STEPS \count,16,24,25,\cv
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lloop_\name
    REDUCE_PRICES \count,\cv
    vzeroupper
    ret
.size \name,.-\name
.endm

PRICE_LEAF asian_genuine_strip_arithmetic_price_1_diag,1,0
PRICE_LEAF asian_genuine_strip_arithmetic_price_4_diag,4,0
PRICE_LEAF asian_genuine_strip_arithmetic_price_8_diag,8,0
PRICE_LEAF asian_genuine_strip_cv_price_1_diag,1,1
PRICE_LEAF asian_genuine_strip_cv_price_4_diag,4,1
PRICE_LEAF asian_genuine_strip_cv_price_8_diag,8,1

.macro DELTA_ONE index, strike_reg, acc_reg, a_reg, g_reg, da_reg, dg_reg, cv
    vsubps %zmm\strike_reg, %zmm\a_reg, %zmm29
    vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rcx){1to16}, %zmm29, %zmm29
    vcmpps $14, %zmm31, %zmm29, %k1
    vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rcx){1to16}, %zmm\da_reg, %zmm29{%k1}{z}
    .if \cv
        vsubps %zmm\strike_reg, %zmm\g_reg, %zmm30
        vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rcx){1to16}, %zmm30, %zmm30
        vcmpps $14, %zmm31, %zmm30, %k2
        vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rcx){1to16}, %zmm\dg_reg, %zmm30{%k2}{z}
        vsubps %zmm30, %zmm29, %zmm29
    .endif
    vaddps %zmm29, %zmm\acc_reg, %zmm\acc_reg
.endm

.macro DELTA_STEPS count, acc_base, a_reg, g_reg, da_reg, dg_reg, cv
    .if \acc_base == 8
      DELTA_ONE 0,0,8,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
      .if \count > 1
        DELTA_ONE 1,1,9,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 2,2,10,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 3,3,11,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
      .endif
      .if \count > 4
        DELTA_ONE 4,4,12,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 5,5,13,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 6,6,14,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 7,7,15,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
      .endif
    .else
      DELTA_ONE 0,0,16,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
      .if \count > 1
        DELTA_ONE 1,1,17,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 2,2,18,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 3,3,19,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
      .endif
      .if \count > 4
        DELTA_ONE 4,4,20,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 5,5,21,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 6,6,22,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
        DELTA_ONE 7,7,23,\a_reg,\g_reg,\da_reg,\dg_reg,\cv
      .endif
    .endif
.endm

.macro REDUCE_DELTA index, lo_reg, hi_reg, cv
    vaddps %zmm\hi_reg, %zmm\lo_reg, %zmm0
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
    vmulsd .Lstrip_inv_paths(%rip), %xmm0, %xmm0
    .if \cv
        vaddsd STRIKE_GEO_DELTA+STRIKE_BYTES*\index(%rcx), %xmm0, %xmm0
    .endif
    vmovapd %xmm0, %xmm1
    vaddsd STRIKE_CALL_DELTA_ADJUST+STRIKE_BYTES*\index(%rcx), %xmm0, %xmm0
    vaddsd STRIKE_PUT_DELTA_ADJUST+STRIKE_BYTES*\index(%rcx), %xmm1, %xmm1
    vmovsd %xmm0, OUTPUT_CALL_DELTA+OUTPUT_BYTES*\index(%r8)
    vmovsd %xmm1, OUTPUT_PUT_DELTA+OUTPUT_BYTES*\index(%r8)
.endm

.macro REDUCE_DELTAS count, cv
    REDUCE_DELTA 0,8,16,\cv
    .if \count > 1
      REDUCE_DELTA 1,9,17,\cv
      REDUCE_DELTA 2,10,18,\cv
      REDUCE_DELTA 3,11,19,\cv
    .endif
    .if \count > 4
      REDUCE_DELTA 4,12,20,\cv
      REDUCE_DELTA 5,13,21,\cv
      REDUCE_DELTA 6,14,22,\cv
      REDUCE_DELTA 7,15,23,\cv
    .endif
.endm

.macro PRICE_DELTA_LEAF name, count, cv
.p2align 6
.globl \name
.type \name,@function
\name:
    LOAD_STRIKES \count
    ZERO_ACCUMULATORS \count
    vxorps %zmm28, %zmm28, %zmm28
    xorq %rax, %rax
.Lprice_\name:
    vmovaps 0(%rdi,%rax), %zmm24
    vaddps STRIP_INITIAL_Q(%rdx){1to16}, %zmm24, %zmm24
    vmulps STRIP_INV_TOTAL(%rdx){1to16}, %zmm24, %zmm24
    .if \cv
      vmovaps 0(%rsi,%rax), %zmm25
    .endif
    PRICE_STEPS \count,8,24,25,\cv
    vmovaps 64(%rdi,%rax), %zmm24
    vaddps STRIP_INITIAL_Q(%rdx){1to16}, %zmm24, %zmm24
    vmulps STRIP_INV_TOTAL(%rdx){1to16}, %zmm24, %zmm24
    .if \cv
      vmovaps 64(%rsi,%rax), %zmm25
    .endif
    PRICE_STEPS \count,16,24,25,\cv
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lprice_\name
    REDUCE_PRICES \count,\cv

    LOAD_STRIKES \count
    ZERO_ACCUMULATORS \count
    vxorps %zmm31, %zmm31, %zmm31
    xorq %rax, %rax
.Ldelta_\name:
    vmovaps 0(%rdi,%rax), %zmm24
    vmulps STRIP_DELTA_Q_SCALE(%rdx){1to16}, %zmm24, %zmm26
    vaddps STRIP_INITIAL_Q(%rdx){1to16}, %zmm24, %zmm25
    vmulps STRIP_INV_TOTAL(%rdx){1to16}, %zmm25, %zmm25
    .if \cv
      vmovaps 0(%rsi,%rax), %zmm27
      vmulps STRIP_DELTA_G_SCALE(%rdx){1to16}, %zmm27, %zmm28
    .endif
    DELTA_STEPS \count,8,25,27,26,28,\cv
    vmovaps 64(%rdi,%rax), %zmm24
    vmulps STRIP_DELTA_Q_SCALE(%rdx){1to16}, %zmm24, %zmm26
    vaddps STRIP_INITIAL_Q(%rdx){1to16}, %zmm24, %zmm25
    vmulps STRIP_INV_TOTAL(%rdx){1to16}, %zmm25, %zmm25
    .if \cv
      vmovaps 64(%rsi,%rax), %zmm27
      vmulps STRIP_DELTA_G_SCALE(%rdx){1to16}, %zmm27, %zmm28
    .endif
    DELTA_STEPS \count,16,25,27,26,28,\cv
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Ldelta_\name
    REDUCE_DELTAS \count,\cv
    vzeroupper
    ret
.size \name,.-\name
.endm

PRICE_DELTA_LEAF asian_genuine_strip_arithmetic_price_delta_1_diag,1,0
PRICE_DELTA_LEAF asian_genuine_strip_arithmetic_price_delta_4_diag,4,0
PRICE_DELTA_LEAF asian_genuine_strip_arithmetic_price_delta_8_diag,8,0
PRICE_DELTA_LEAF asian_genuine_strip_cv_price_delta_1_diag,1,1
PRICE_DELTA_LEAF asian_genuine_strip_cv_price_delta_4_diag,4,1
PRICE_DELTA_LEAF asian_genuine_strip_cv_price_delta_8_diag,8,1

.section .note.GNU-stack,"",@progbits
