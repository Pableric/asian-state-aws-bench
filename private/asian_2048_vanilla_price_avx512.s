.equ A2_CTX_D1_GROWTH,       0
.equ A2_CTX_ROUTES_D2,       8
.equ A2_CTX_N,              24
.equ A2_CTX_S0,             28
.equ A2_ROUTE_GROWTH,        8
.equ A2_ROUTE_MAP,          16
.equ A2_ROUTE_BYTES,        32
.equ A2_AFFINE_BASE,       192
.equ A2_AFFINE_HALF,       256
.equ A2_GENERIC_PATTERNS,  576
.equ A2_PATH_BYTES,       8192

.equ A2_PAYOFF_INV_TOTAL,   24
.equ A2_PAYOFF_INITIAL_Q,   28
.equ A2_PAYOFF_DISCOUNT,    32
.equ A2_STRIKE_VALUE,        0
.equ A2_STRIKE_SIGN,         4
.equ A2_STRIKE_CALL_ADJUST, 24
.equ A2_STRIKE_PUT_ADJUST,  32
.equ A2_OUTPUT_CALL,         0
.equ A2_OUTPUT_PUT,          8

.section .rodata
.p2align 3
.La2_inv_paths: .quad 0x3f40000000000000

.section .text

.macro A2_ROUTE_AFFINE
    movq A2_ROUTE_GROWTH(%rdi), %r8
    movq A2_ROUTE_MAP(%rdi), %r9
    movzbl 0(%r9,%rcx,2), %r11d
    movzbl 1(%r9,%rcx,2), %esi
    shll $6, %r11d
    vmovdqa32 A2_AFFINE_BASE(%r9), %zmm18
    vmovdqa32 A2_AFFINE_HALF(%r9), %zmm19
    vpbroadcastd %esi, %zmm14
    vpxord %zmm14, %zmm18, %zmm18
    vpxord %zmm18, %zmm19, %zmm19
    vmovdqa32 0(%r8,%r11), %zmm14
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm15
    vpermd %zmm14, %zmm18, %zmm18
    vpermd %zmm15, %zmm19, %zmm19
    vmulps %zmm18, %zmm4, %zmm4
    vmulps %zmm19, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
.endm

.macro A2_ROUTE_GENERIC
    movq A2_ROUTE_GROWTH(%rdi), %r8
    movq A2_ROUTE_MAP(%rdi), %r9
    movzbq 0(%r9,%rcx,4), %r11
    movzbq 1(%r9,%rcx,4), %rsi
    movzbq 2(%r9,%rcx,4), %rdx
    movzbq 3(%r9,%rcx,4), %r10
    shlq $6, %r11
    shlq $6, %rsi
    shlq $6, %rdx
    shlq $6, %r10
    vmovdqa32 0(%r8,%r11), %zmm14
    vmovdqa32 0(%r8,%rsi), %zmm15
    vmovdqa32 A2_GENERIC_PATTERNS(%r9,%rdx), %zmm18
    vmovdqa32 A2_GENERIC_PATTERNS(%r9,%r10), %zmm19
    vpermd %zmm14, %zmm18, %zmm18
    vpermd %zmm15, %zmm19, %zmm19
    vmulps %zmm18, %zmm4, %zmm4
    vmulps %zmm19, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
.endm

.macro A2_REDUCE
    vaddps %zmm20, %zmm8, %zmm0
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
    vmulsd .La2_inv_paths(%rip), %xmm0, %xmm0
    vmovapd %xmm0, %xmm1
    vaddsd A2_STRIKE_CALL_ADJUST(%rcx), %xmm0, %xmm0
    vaddsd A2_STRIKE_PUT_ADJUST(%rcx), %xmm1, %xmm1
    vmovsd %xmm0, A2_OUTPUT_CALL(%r8)
    vmovsd %xmm1, A2_OUTPUT_PUT(%r8)
.endm

.macro A2_PRICE_LEAF name, affine
.p2align 6
.globl \name
.type \name,@function
\name:
    kmovq %rsi, %k6
    kmovq %rdx, %k7
    kmovq %rcx, %k2
    movq A2_CTX_D1_GROWTH(%rdi), %r8
    kmovq %r8, %k5
    movq A2_CTX_ROUTES_D2(%rdi), %r8
    kmovq %r8, %k3
    movl A2_CTX_N(%rdi), %r8d
    decl %r8d
    kmovd %r8d, %k4
    vbroadcastss A2_CTX_S0(%rdi), %zmm31
    vbroadcastss A2_STRIKE_VALUE(%rdx), %zmm0
    vxorps %zmm8, %zmm8, %zmm8
    vxorps %zmm20, %zmm20, %zmm20
    xorq %rax, %rax
.Lpacket_\name:
    kmovq %k5, %r8
    vmovaps 0(%r8,%rax), %zmm4
    vmovaps 64(%r8,%rax), %zmm5
    vmulps %zmm31, %zmm4, %zmm4
    vmulps %zmm31, %zmm5, %zmm5
    vmovaps %zmm4, %zmm6
    vmovaps %zmm5, %zmm7
    movq %rax, %rcx
    shrq $7, %rcx
    kmovq %rax, %k0
    kmovq %k3, %rdi
    kmovd %k4, %eax
.Lroute_\name:
    .if \affine
      A2_ROUTE_AFFINE
    .else
      A2_ROUTE_GENERIC
    .endif
    addq $A2_ROUTE_BYTES, %rdi
    decl %eax
    jne .Lroute_\name
    kmovq %k6, %rsi
    kmovq %k7, %rdx
    kmovq %k0, %rax
    vxorps %zmm28, %zmm28, %zmm28
    vaddps A2_PAYOFF_INITIAL_Q(%rsi){1to16}, %zmm6, %zmm6
    vmulps A2_PAYOFF_INV_TOTAL(%rsi){1to16}, %zmm6, %zmm6
    vsubps %zmm0, %zmm6, %zmm29
    vmulps A2_STRIKE_SIGN(%rdx){1to16}, %zmm29, %zmm29
    vmaxps %zmm28, %zmm29, %zmm29
    vmulps A2_PAYOFF_DISCOUNT(%rsi){1to16}, %zmm29, %zmm29
    vaddps %zmm29, %zmm8, %zmm8
    vaddps A2_PAYOFF_INITIAL_Q(%rsi){1to16}, %zmm7, %zmm7
    vmulps A2_PAYOFF_INV_TOTAL(%rsi){1to16}, %zmm7, %zmm7
    vsubps %zmm0, %zmm7, %zmm29
    vmulps A2_STRIKE_SIGN(%rdx){1to16}, %zmm29, %zmm29
    vmaxps %zmm28, %zmm29, %zmm29
    vmulps A2_PAYOFF_DISCOUNT(%rsi){1to16}, %zmm29, %zmm29
    vaddps %zmm29, %zmm20, %zmm20
    addq $128, %rax
    cmpq $A2_PATH_BYTES, %rax
    jb .Lpacket_\name
    kmovq %k7, %rcx
    kmovq %k2, %r8
    A2_REDUCE
    vzeroupper
    ret
.size \name,.-\name
.endm

A2_PRICE_LEAF asian_2048_affine_price_1_diag,1
A2_PRICE_LEAF asian_2048_generic_price_1_diag,0

.section .note.GNU-stack,"",@progbits
