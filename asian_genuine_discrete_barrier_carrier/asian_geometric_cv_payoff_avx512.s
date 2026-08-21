.include "private/asian_exp_p8_18diag.inc"

.equ CV_INV_TOTAL, 20
.equ CV_STRIKE, 24
.equ CV_DISCOUNT, 28
.equ CV_LOG_S0, 32
.equ CV_SIGN, 36
.equ CV_G_EXACT, 56
.equ PATH_BYTES, 16384

.section .rodata
.p2align 4
.LCvExpLog2E: .long 0x3fb8aa3b
.LCvExpLn2Hi: .long 0x3f318000
.LCvExpLn2Lo: .long 0xb95e8083
.LCvInvPaths: .quad 0x3f30000000000000

.section .text

.macro CV_EXP input, output, exponent, reduced
    vmulps .LCvExpLog2E(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vmovaps %zmm\input, %zmm\reduced
    vfnmadd231ps .LCvExpLn2Hi(%rip){1to16}, %zmm\exponent, %zmm\reduced
    vfnmadd231ps .LCvExpLn2Lo(%rip){1to16}, %zmm\exponent, %zmm\reduced
    ASIAN_EXP_P8_18DIAG \reduced, \output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

.macro REDUCE_PAIR a, b
    vaddps %zmm\b, %zmm\a, %zmm0
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
    vmulsd .LCvInvPaths(%rip), %xmm0, %xmm0
.endm

.p2align 6
.globl asian_vector_exp_range_reduced_array_diag
.type asian_vector_exp_range_reduced_array_diag,@function
asian_vector_exp_range_reduced_array_diag:
    xorl %eax, %eax
.Lexp_array:
    vmovaps 0(%rdi,%rax), %zmm4
    vmovaps 64(%rdi,%rax), %zmm5
    CV_EXP 4, 6, 8, 10
    CV_EXP 5, 7, 9, 11
    vmovaps %zmm6, 0(%rsi,%rax)
    vmovaps %zmm7, 64(%rsi,%rax)
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lexp_array
    vzeroupper
    ret
.size asian_vector_exp_range_reduced_array_diag,.-asian_vector_exp_range_reduced_array_diag

.macro ARITH_PAY qreg, out, ctx
    vmulps CV_INV_TOTAL(%\ctx){1to16}, %zmm\qreg, %zmm\out
    vsubps CV_STRIKE(%\ctx){1to16}, %zmm\out, %zmm\out
    vmulps CV_SIGN(%\ctx){1to16}, %zmm\out, %zmm\out
    vmaxps %zmm29, %zmm\out, %zmm\out
    vmulps CV_DISCOUNT(%\ctx){1to16}, %zmm\out, %zmm\out
.endm

.macro GEO_PAY lreg, out, exponent, reduced, ctx
    vaddps CV_LOG_S0(%\ctx){1to16}, %zmm\lreg, %zmm\out
    CV_EXP \out, \out, \exponent, \reduced
    vsubps CV_STRIKE(%\ctx){1to16}, %zmm\out, %zmm\out
    vmulps CV_SIGN(%\ctx){1to16}, %zmm\out, %zmm\out
    vmaxps %zmm29, %zmm\out, %zmm\out
    vmulps CV_DISCOUNT(%\ctx){1to16}, %zmm\out, %zmm\out
.endm

.p2align 6
.globl asian_arithmetic_payoff_reduce_diag
.type asian_arithmetic_payoff_reduce_diag,@function
asian_arithmetic_payoff_reduce_diag:
    vxorps %zmm29, %zmm29, %zmm29
    vxorps %zmm30, %zmm30, %zmm30
    vxorps %zmm31, %zmm31, %zmm31
    xorl %eax, %eax
.Larithmetic:
    vmovaps 0(%rdi,%rax), %zmm4
    vmovaps 64(%rdi,%rax), %zmm5
    ARITH_PAY 4, 6, rsi
    ARITH_PAY 5, 7, rsi
    vaddps %zmm6, %zmm30, %zmm30
    vaddps %zmm7, %zmm31, %zmm31
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Larithmetic
    REDUCE_PAIR 30, 31
    vzeroupper
    ret
.size asian_arithmetic_payoff_reduce_diag,.-asian_arithmetic_payoff_reduce_diag

.p2align 6
.globl asian_geometric_payoff_reduce_diag
.type asian_geometric_payoff_reduce_diag,@function
asian_geometric_payoff_reduce_diag:
    vxorps %zmm29, %zmm29, %zmm29
    vxorps %zmm30, %zmm30, %zmm30
    vxorps %zmm31, %zmm31, %zmm31
    xorl %eax, %eax
.Lgeometric:
    vmovaps 0(%rdi,%rax), %zmm4
    vmovaps 64(%rdi,%rax), %zmm5
    GEO_PAY 4, 6, 8, 10, rsi
    GEO_PAY 5, 7, 9, 11, rsi
    vaddps %zmm6, %zmm30, %zmm30
    vaddps %zmm7, %zmm31, %zmm31
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lgeometric
    REDUCE_PAIR 30, 31
    vzeroupper
    ret
.size asian_geometric_payoff_reduce_diag,.-asian_geometric_payoff_reduce_diag

.p2align 6
.globl asian_geometric_cv_payoff_reduce_diag
.type asian_geometric_cv_payoff_reduce_diag,@function
asian_geometric_cv_payoff_reduce_diag:
    vxorps %zmm29, %zmm29, %zmm29
    vxorps %zmm30, %zmm30, %zmm30
    vxorps %zmm31, %zmm31, %zmm31
    xorl %eax, %eax
.Lcombined:
    vmovaps 0(%rdi,%rax), %zmm4
    vmovaps 64(%rdi,%rax), %zmm5
    vmovaps 0(%rsi,%rax), %zmm6
    vmovaps 64(%rsi,%rax), %zmm7
    ARITH_PAY 4, 14, rdx
    ARITH_PAY 5, 15, rdx
    GEO_PAY 6, 8, 10, 12, rdx
    GEO_PAY 7, 9, 11, 13, rdx
    vsubps %zmm8, %zmm14, %zmm14
    vsubps %zmm9, %zmm15, %zmm15
    vaddps %zmm14, %zmm30, %zmm30
    vaddps %zmm15, %zmm31, %zmm31
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lcombined
    REDUCE_PAIR 30, 31
    vaddsd CV_G_EXACT(%rdx), %xmm0, %xmm0
    vzeroupper
    ret
.size asian_geometric_cv_payoff_reduce_diag,.-asian_geometric_cv_payoff_reduce_diag

.section .note.GNU-stack,"",@progbits
