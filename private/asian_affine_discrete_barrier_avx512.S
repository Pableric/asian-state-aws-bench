.equ BCTX_ROUTES_D2,       0
.equ BCTX_D1,              8
.equ BCTX_ROUTE_COUNT,    16
.equ BCTX_MONITOR_COUNT,  20
.equ BCTX_S0,             24
.equ BCTX_BARRIER,        28
.equ BCTX_STRIKE,         32
.equ BCTX_INITIAL_MASK,   36
.equ BCTX_SCALE,          40
.equ ROUTE_GROWTH,         8
.equ ROUTE_MAP,           16
.equ ROUTE_BYTES,         32
.equ PATH_BYTES,       16384

.section .text

/* Exact current-stack affine provider, used only for D2...DN. */
.macro AFFINE_GROWTH_ROUTE
    movq ROUTE_GROWTH(%rdi), %r8
    movq ROUTE_MAP(%rdi), %r9
    movzbl 0(%r9,%rcx,2), %r11d
    movzbl 1(%r9,%rcx,2), %esi
    shll $6, %r11d
    vmovdqa32 320(%r9), %zmm12
    vmovdqa32 384(%r9), %zmm13
    vpbroadcastd %esi, %zmm14
    vpxord %zmm14, %zmm12, %zmm12
    vpxord %zmm12, %zmm13, %zmm13
    vmovdqa32 0(%r8,%r11), %zmm14
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm15
    vpermd %zmm14, %zmm12, %zmm12
    vpermd %zmm15, %zmm13, %zmm13
.endm

.macro UPDATE_MASK mode, predicate
    .if \mode
        vcmpps $\predicate, %zmm28, %zmm4, %k4{%k4}
        vcmpps $\predicate, %zmm28, %zmm5, %k6{%k6}
    .endif
.endm

.macro SCHEDULED_UPDATE schedule, maskmode, predicate
    .if \schedule == 0
        vmulps %zmm12, %zmm4, %zmm4
        vmulps %zmm13, %zmm5, %zmm5
        UPDATE_MASK \maskmode, \predicate
    .else
        vmulps %zmm12, %zmm4, %zmm4
        .if \maskmode
            vcmpps $\predicate, %zmm28, %zmm4, %k4{%k4}
        .endif
        vmulps %zmm13, %zmm5, %zmm5
        .if \maskmode
            vcmpps $\predicate, %zmm28, %zmm5, %k6{%k6}
        .endif
    .endif
.endm

.macro TERMINAL_PAY option, masked
    .if \option == 0
        vsubps %zmm29, %zmm4, %zmm0
        vsubps %zmm29, %zmm5, %zmm1
    .else
        vsubps %zmm4, %zmm29, %zmm0
        vsubps %zmm5, %zmm29, %zmm1
    .endif
    .if \masked
        vmaxps %zmm30, %zmm0, %zmm0{%k4}{z}
        vmaxps %zmm30, %zmm1, %zmm1{%k6}{z}
    .else
        vmaxps %zmm30, %zmm0, %zmm0
        vmaxps %zmm30, %zmm1, %zmm1
    .endif
    vaddps %zmm0, %zmm20, %zmm20
    vaddps %zmm1, %zmm21, %zmm21
.endm

.macro REDUCE_PAYOFF
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

/*
 * This is the qualified historical leaf with one mechanical substitution:
 * D1 remains direct; only the D2...DN generic provider becomes affine.
 */
.macro AFFINE_BARRIER_FUNCTION name, option, schedule, maskmode, predicate
.p2align 6
.globl \name
.type \name,@function
\name:
    kmovq BCTX_ROUTES_D2(%rdi), %k2
    kmovq BCTX_D1(%rdi), %k3
    kmovd BCTX_ROUTE_COUNT(%rdi), %k5
    kmovw BCTX_INITIAL_MASK(%rdi), %k1
    vbroadcastss BCTX_S0(%rdi), %zmm31
    vbroadcastss BCTX_BARRIER(%rdi), %zmm28
    vbroadcastss BCTX_STRIKE(%rdi), %zmm29
    vmovsd BCTX_SCALE(%rdi), %xmm26
    vxorps %zmm30, %zmm30, %zmm30
    vxorps %zmm20, %zmm20, %zmm20
    vxorps %zmm21, %zmm21, %zmm21
    xorq %rax, %rax
.Lpacket_\@:
    vmovaps %zmm31, %zmm4
    vmovaps %zmm31, %zmm5
    .if \maskmode
        kandw %k1, %k1, %k4
        kandw %k1, %k1, %k6
    .endif
    movq %rax, %rcx
    shrq $7, %rcx
    /* Historical direct-D1 load/update/observation. No affine D1 route. */
    kmovq %k3, %rdx
    vmovaps 0(%rdx,%rax), %zmm12
    vmovaps 64(%rdx,%rax), %zmm13
    SCHEDULED_UPDATE \schedule, \maskmode, \predicate
    kmovq %rax, %k7
    kmovd %k5, %eax
    testl %eax, %eax
    je .Lpayoff_\@
    kmovq %k2, %rdi
.Lroute_\@:
    AFFINE_GROWTH_ROUTE
    SCHEDULED_UPDATE \schedule, \maskmode, \predicate
    addq $ROUTE_BYTES, %rdi
    decl %eax
    jne .Lroute_\@
.Lpayoff_\@:
    TERMINAL_PAY \option, \maskmode
    kmovq %k7, %rax
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lpacket_\@
    REDUCE_PAYOFF
    vzeroupper
    ret
.size \name,.-\name
.endm

AFFINE_BARRIER_FUNCTION asian_affine_barrier_vanilla_call_interleaved_diag,0,1,0,0x1e
AFFINE_BARRIER_FUNCTION asian_affine_barrier_vanilla_put_interleaved_diag,1,1,0,0x1e
AFFINE_BARRIER_FUNCTION asian_affine_barrier_down_call_self_interleaved_diag,0,1,1,0x1e
AFFINE_BARRIER_FUNCTION asian_affine_barrier_down_put_self_interleaved_diag,1,1,1,0x1e

AFFINE_BARRIER_FUNCTION asian_affine_barrier_vanilla_call_grouped_diag,0,0,0,0x11
AFFINE_BARRIER_FUNCTION asian_affine_barrier_vanilla_put_grouped_diag,1,0,0,0x11
AFFINE_BARRIER_FUNCTION asian_affine_barrier_up_call_self_grouped_diag,0,0,1,0x11
AFFINE_BARRIER_FUNCTION asian_affine_barrier_up_put_self_grouped_diag,1,0,1,0x11

.section .note.GNU-stack,"",@progbits
