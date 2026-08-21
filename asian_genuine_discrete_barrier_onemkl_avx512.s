.include "asian_genuine_discrete_barrier_carrier/private/asian_exp_p8_18diag.inc"

.equ BCTX_MONITOR_COUNT, 28
.equ BCTX_S0, 32
.equ BCTX_BARRIER, 36
.equ BCTX_STRIKE, 40
.equ BCTX_SCALE, 48
.equ PATH_BYTES, 16384

.section .rodata
.p2align 6
.Lbarrier_idx: .long 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
.Lbarrier_log2e: .long 0x3fb8aa3b
.Lbarrier_ln2hi: .long 0x3f318000
.Lbarrier_ln2lo: .long 0xb95e8083

.section .text
.macro PM_EXP input, output, exponent, reduced
    vmulps .Lbarrier_log2e(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vmovaps %zmm\input, %zmm\reduced
    vfnmadd231ps .Lbarrier_ln2hi(%rip){1to16}, %zmm\exponent, %zmm\reduced
    vfnmadd231ps .Lbarrier_ln2lo(%rip){1to16}, %zmm\exponent, %zmm\reduced
    ASIAN_EXP_P8_18DIAG \reduced, \output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

.macro PM_REDUCE
    vaddps %zmm21, %zmm20, %zmm0
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
    vmulsd %xmm26, %xmm0, %xmm0
.endm

.macro POINT_MAJOR_FUNCTION name, option
.p2align 6
.globl \name
.type \name,@function
\name:
    kmovq %rdi, %k2
    movl BCTX_MONITOR_COUNT(%rsi), %r9d
    kmovd %r9d, %k5
    shll $2, %r9d
    vpbroadcastd %r9d, %zmm24
    vpmulld .Lbarrier_idx(%rip), %zmm24, %zmm25
    vbroadcastss BCTX_S0(%rsi), %zmm31
    vbroadcastss BCTX_BARRIER(%rsi), %zmm28
    vbroadcastss BCTX_STRIKE(%rsi), %zmm29
    vmovsd BCTX_SCALE(%rsi), %xmm26
    vxorps %zmm30, %zmm30, %zmm30
    vxorps %zmm20, %zmm20, %zmm20
    vxorps %zmm21, %zmm21, %zmm21
    xorq %rax, %rax
.Lpm_packet_\@:
    vmovaps %zmm31, %zmm4
    vmovaps %zmm31, %zmm5
    kxnorw %k4, %k4, %k4
    kxnorw %k6, %k6, %k6
    kmovq %k2, %rdi
    movq %rax, %r8
    shrq $7, %r8
    imull %r9d, %r8d
    shll $5, %r8d
    addq %r8, %rdi
    xorl %r10d, %r10d
    kmovd %k5, %esi
.Lpm_step_\@:
    vpbroadcastd %r10d, %zmm27
    vpaddd %zmm27, %zmm25, %zmm27
    kxnorw %k1, %k1, %k1
    vgatherdps (%rdi,%zmm27), %zmm0{%k1}
    movq %r9, %r11
    shlq $4, %r11
    addq %rdi, %r11
    kxnorw %k1, %k1, %k1
    vgatherdps (%r11,%zmm27), %zmm1{%k1}
    PM_EXP 0,12,14,16
    PM_EXP 1,13,15,17
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vcmpps $0x1e, %zmm28, %zmm4, %k4{%k4}
    vcmpps $0x1e, %zmm28, %zmm5, %k6{%k6}
    addl $4, %r10d
    decl %esi
    jne .Lpm_step_\@
    .if \option == 0
        vsubps %zmm29, %zmm4, %zmm0
        vsubps %zmm29, %zmm5, %zmm1
    .else
        vsubps %zmm4, %zmm29, %zmm0
        vsubps %zmm5, %zmm29, %zmm1
    .endif
    vmaxps %zmm30, %zmm0, %zmm0{%k4}{z}
    vmaxps %zmm30, %zmm1, %zmm1{%k6}{z}
    vaddps %zmm0, %zmm20, %zmm20
    vaddps %zmm1, %zmm21, %zmm21
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lpm_packet_\@
    PM_REDUCE
    vzeroupper
    ret
.size \name,.-\name
.endm

POINT_MAJOR_FUNCTION asian_barrier_onemkl_point_major_down_call_diag, 0
POINT_MAJOR_FUNCTION asian_barrier_onemkl_point_major_down_put_diag, 1

.section .note.GNU-stack,"",@progbits
