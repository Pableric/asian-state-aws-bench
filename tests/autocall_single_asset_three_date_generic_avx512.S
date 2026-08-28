.include "asian_genuine_discrete_barrier_carrier/tools/fragment_service/public_harness/dim_from_d1_frags.inc"

.equ ACTX_D1,                0
.equ ACTX_ROUTES_D2,         8
.equ ACTX_DATE0,            16
.equ ACTX_DATE1,            32
.equ ACTX_DATE2,            48
.equ ACTX_S0,               64
.equ ACTX_INV_S0,           68
.equ ACTX_PROTECTION,       72
.equ ACTX_TERMINAL,         76
.equ ACTX_INV_PATHS,        80
.equ DATE_COUPON_BARRIER,    0
.equ DATE_CALL_BARRIER,      4
.equ DATE_COUPON_CASH,       8
.equ DATE_CALL_CASH,        12
.equ ROUTE_GROWTH,           8
.equ ROUTE_MAP,             16
.equ ROUTE_BYTES,           32
.equ PATH_BYTES,         16384

.section .text.autocall_3date_generic_price_leaf_test,"ax",@progbits

.macro GENERIC_GROWTH route_offset
    movq \route_offset+ROUTE_GROWTH(%rsi), %r8
    movq \route_offset+ROUTE_MAP(%rsi), %r9
    movzbq 0(%r9,%rcx,4), %r10
    shlq $6, %r10
    vmovdqa32 0(%r8,%r10), %zmm14
    movzbq 1(%r9,%rcx,4), %r10
    shlq $6, %r10
    vmovdqa32 0(%r8,%r10), %zmm15
    movzbq 2(%r9,%rcx,4), %r10
    shlq $6, %r10
    vmovdqa32 576(%r9,%r10), %zmm12
    movzbq 3(%r9,%rcx,4), %r10
    shlq $6, %r10
    vmovdqa32 576(%r9,%r10), %zmm13
    vpermd %zmm14, %zmm12, %zmm12
    vpermd %zmm15, %zmm13, %zmm13
.endm

.macro DATE_EVENT date_offset
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vbroadcastss \date_offset+DATE_COUPON_BARRIER(%rdi), %zmm8
    vcmpps $0x1d, %zmm8, %zmm4, %k1
    vcmpps $0x1d, %zmm8, %zmm5, %k5
    kandw %k4, %k1, %k1
    kandw %k6, %k5, %k5
    vaddps \date_offset+DATE_COUPON_CASH(%rdi){1to16}, %zmm6, %zmm6{%k1}
    vaddps \date_offset+DATE_COUPON_CASH(%rdi){1to16}, %zmm7, %zmm7{%k5}
    vbroadcastss \date_offset+DATE_CALL_BARRIER(%rdi), %zmm9
    vcmpps $0x1d, %zmm9, %zmm4, %k2
    vcmpps $0x1d, %zmm9, %zmm5, %k3
    kandw %k4, %k2, %k2
    kandw %k6, %k3, %k3
    vaddps \date_offset+DATE_CALL_CASH(%rdi){1to16}, %zmm6, %zmm6{%k2}
    vaddps \date_offset+DATE_CALL_CASH(%rdi){1to16}, %zmm7, %zmm7{%k3}
    knotw %k2, %k2
    knotw %k3, %k3
    kandw %k2, %k4, %k4
    kandw %k3, %k6, %k6
.endm

.macro TERMINAL_EVENT
    vbroadcastss ACTX_PROTECTION(%rdi), %zmm8
    vcmpps $0x1d, %zmm8, %zmm4, %k1
    vcmpps $0x1d, %zmm8, %zmm5, %k5
    kandw %k4, %k1, %k1
    kandw %k6, %k5, %k5
    vaddps ACTX_TERMINAL(%rdi){1to16}, %zmm6, %zmm6{%k1}
    vaddps ACTX_TERMINAL(%rdi){1to16}, %zmm7, %zmm7{%k5}
    knotw %k1, %k2
    knotw %k5, %k3
    kandw %k4, %k2, %k2
    kandw %k6, %k3, %k3
    vmulps %zmm27, %zmm4, %zmm0
    vmulps %zmm27, %zmm5, %zmm1
    vmulps ACTX_TERMINAL(%rdi){1to16}, %zmm0, %zmm0
    vmulps ACTX_TERMINAL(%rdi){1to16}, %zmm1, %zmm1
    vaddps %zmm0, %zmm6, %zmm6{%k2}
    vaddps %zmm1, %zmm7, %zmm7{%k3}
.endm

.macro REDUCE
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

.p2align 6
.globl autocall_3date_generic_price_leaf_test
.type autocall_3date_generic_price_leaf_test,@function
autocall_3date_generic_price_leaf_test:
    movq ACTX_D1(%rdi), %rdx
    movq ACTX_ROUTES_D2(%rdi), %rsi
    vbroadcastss ACTX_S0(%rdi), %zmm31
    vbroadcastss ACTX_INV_S0(%rdi), %zmm27
    vmovsd ACTX_INV_PATHS(%rdi), %xmm26
    vxorps %zmm20, %zmm20, %zmm20
    vxorps %zmm21, %zmm21, %zmm21
    xorq %rax, %rax
.Lgeneric_packet:
    vmovaps %zmm31, %zmm4
    vmovaps %zmm31, %zmm5
    vxorps %zmm6, %zmm6, %zmm6
    vxorps %zmm7, %zmm7, %zmm7
    kxnorw %k4, %k4, %k4
    kxnorw %k6, %k6, %k6
    movq %rax, %rcx
    shrq $7, %rcx
    vmovaps 0(%rdx,%rax), %zmm12
    vmovaps 64(%rdx,%rax), %zmm13
    DATE_EVENT ACTX_DATE0
    GENERIC_GROWTH 0
    DATE_EVENT ACTX_DATE1
    GENERIC_GROWTH ROUTE_BYTES
    DATE_EVENT ACTX_DATE2
    TERMINAL_EVENT
    vaddps %zmm6, %zmm20, %zmm20
    vaddps %zmm7, %zmm21, %zmm21
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lgeneric_packet
    REDUCE
    vzeroupper
    ret
.size autocall_3date_generic_price_leaf_test,.-autocall_3date_generic_price_leaf_test

.section .note.GNU-stack,"",@progbits
