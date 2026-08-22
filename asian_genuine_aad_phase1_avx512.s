.include "private/asian_exp_p8_18diag.inc"

.equ AAD_CTX_ROUTES,       0
.equ AAD_CTX_TAPE,         8
.equ AAD_CTX_CONTROLS,    16
.equ AAD_CTX_N,           24
.equ AAD_CTX_ROUTE_COUNT, 28
.equ AAD_CTX_S0,          32
.equ AAD_CTX_STRIKE,      36
.equ AAD_CTX_INV_N,       40
.equ AAD_CTX_DT_OVER_N,   44
.equ AAD_CTX_C,           48
.equ AAD_CTX_INV_SIGMA,   52
.equ AAD_CTX_INV_S0,      56
.equ AAD_CTX_DISCOUNT,    60

.equ AAD_CTRL_MATURITY,   16
.equ AAD_CTRL_B,          20
.equ AAD_CTRL_LOG_S0,     28
.equ AAD_CTRL_WEIGHTS,    32
.equ AAD_CTRL_CALL,     1056
.equ AAD_CTRL_PUT,      1088

.equ AAD_ROUTE_X,          0
.equ AAD_ROUTE_GROWTH,     8
.equ AAD_ROUTE_MAP,       16
.equ AAD_ROUTE_WEIGHT,    24
.equ AAD_ROUTE_INDEX,     28
.equ AAD_ROUTE_BYTES,     32
.equ AAD_MAP_PATTERNS,   576
.equ AAD_PATH_BYTES,   16384

.section .rodata
.p2align 4
.LAadLog2E:    .long 0x3fb8aa3b
.LAadLn2Hi:    .long 0x3f318000
.LAadLn2Lo:    .long 0xb95e8083
.LAadInvPaths: .quad 0x3f30000000000000

.section .text

/* Frozen two-half selector/control decode.  The controls are reused for x and
 * growth; there is no provider switch. */
.macro AAD_DUAL_ROUTE
    movq AAD_ROUTE_X(%rdi), %r8
    movq AAD_ROUTE_GROWTH(%rdi), %rsi
    kmovq %rsi, %k1
    movq AAD_ROUTE_MAP(%rdi), %r9
    movzbq 0(%r9,%rcx,4), %r11
    movzbq 1(%r9,%rcx,4), %rsi
    movzbq 2(%r9,%rcx,4), %rdx
    movzbq 3(%r9,%rcx,4), %r10
    shlq $6, %r11
    shlq $6, %rsi
    shlq $6, %rdx
    shlq $6, %r10
    vmovdqa32 0(%r8,%r11), %zmm2
    vmovdqa32 0(%r8,%rsi), %zmm3
    vmovdqa32 AAD_MAP_PATTERNS(%r9,%rdx), %zmm18
    vmovdqa32 AAD_MAP_PATTERNS(%r9,%r10), %zmm19
    vpermd %zmm2, %zmm18, %zmm0
    vpermd %zmm3, %zmm19, %zmm1
    kmovq %k1, %r8
    vmovdqa32 0(%r8,%r11), %zmm14
    vmovdqa32 0(%r8,%rsi), %zmm15
    vpermd %zmm14, %zmm18, %zmm12
    vpermd %zmm15, %zmm19, %zmm13
.endm

/* Ranked suffix reverse route: two x payloads, one selector decode, two
 * controls and two vpermd.  Growth is neither loaded nor permuted. */
.macro AAD_X_ROUTE
    movq AAD_ROUTE_X(%rdi), %r8
    movq AAD_ROUTE_MAP(%rdi), %r9
    movzbq 0(%r9,%rcx,4), %r11
    movzbq 1(%r9,%rcx,4), %rsi
    movzbq 2(%r9,%rcx,4), %rdx
    movzbq 3(%r9,%rcx,4), %r10
    shlq $6, %r11
    shlq $6, %rsi
    shlq $6, %rdx
    shlq $6, %r10
    vmovdqa32 0(%r8,%r11), %zmm2
    vmovdqa32 0(%r8,%rsi), %zmm3
    vmovdqa32 AAD_MAP_PATTERNS(%r9,%rdx), %zmm0
    vmovdqa32 AAD_MAP_PATTERNS(%r9,%r10), %zmm1
    vpermd %zmm2, %zmm0, %zmm0
    vpermd %zmm3, %zmm1, %zmm1
.endm

.macro AAD_EXP input, output, exponent, reduced
    vmulps .LAadLog2E(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vmovaps %zmm\input, %zmm\reduced
    vfnmadd231ps .LAadLn2Hi(%rip){1to16}, %zmm\exponent, %zmm\reduced
    vfnmadd231ps .LAadLn2Lo(%rip){1to16}, %zmm\exponent, %zmm\reduced
    ASIAN_EXP_P8_18DIAG \reduced, \output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

.macro AAD_ZERO_OUTPUT_ACCUMULATORS
    vxorps %zmm20, %zmm20, %zmm20
    vxorps %zmm21, %zmm21, %zmm21
    vxorps %zmm22, %zmm22, %zmm22
    vxorps %zmm23, %zmm23, %zmm23
    vxorps %zmm24, %zmm24, %zmm24
    vxorps %zmm25, %zmm25, %zmm25
    vxorps %zmm26, %zmm26, %zmm26
    vxorps %zmm27, %zmm27, %zmm27
    vxorps %zmm31, %zmm31, %zmm31
.endm

/* Consume one half immediately.  rdi=hot context, r8=cold controls.
 * zmm31 is zero; k1/k7 are short-lived payoff indicators. */
.macro AAD_PAY_HALF areg, arho, avega, greg, gvega, pacc, dacc, vacc, racc, put, cv
    .if \put
        vbroadcastss AAD_CTX_STRIKE(%rdi), %zmm0
        vsubps %zmm\areg, %zmm0, %zmm0
    .else
        vsubps AAD_CTX_STRIKE(%rdi){1to16}, %zmm\areg, %zmm0
    .endif
    vcmpps $14, %zmm31, %zmm0, %k1
    vmaxps %zmm31, %zmm0, %zmm0
    .if \cv
        .if \put
            vbroadcastss AAD_CTX_STRIKE(%rdi), %zmm1
            vsubps %zmm\greg, %zmm1, %zmm1
        .else
            vsubps AAD_CTX_STRIKE(%rdi){1to16}, %zmm\greg, %zmm1
        .endif
        vcmpps $14, %zmm31, %zmm1, %k7
        vmaxps %zmm31, %zmm1, %zmm1
        vmulps AAD_CTX_DISCOUNT(%rdi){1to16}, %zmm0, %zmm0
        vmulps AAD_CTX_DISCOUNT(%rdi){1to16}, %zmm1, %zmm1
        vsubps %zmm1, %zmm0, %zmm0
    .else
        vmulps AAD_CTX_DISCOUNT(%rdi){1to16}, %zmm0, %zmm0
    .endif

    vmulps AAD_CTX_INV_S0(%rdi){1to16}, %zmm\areg, %zmm2
    .if \put
        vsubps %zmm2, %zmm31, %zmm2
    .endif
    vmovaps %zmm2, %zmm2{%k1}{z}
    .if \cv
        vmulps AAD_CTX_INV_S0(%rdi){1to16}, %zmm\greg, %zmm3
        .if \put
            vsubps %zmm3, %zmm31, %zmm3
        .endif
        vmovaps %zmm3, %zmm3{%k7}{z}
        vsubps %zmm3, %zmm2, %zmm2
    .endif
    vmulps AAD_CTX_DISCOUNT(%rdi){1to16}, %zmm2, %zmm2
    vaddps %zmm2, %zmm\dacc, %zmm\dacc

    .if \put
        vsubps %zmm\avega, %zmm31, %zmm2
        vmovaps %zmm2, %zmm2{%k1}{z}
    .else
        vmovaps %zmm\avega, %zmm2{%k1}{z}
    .endif
    .if \cv
        .if \put
            vsubps %zmm\gvega, %zmm31, %zmm3
            vmovaps %zmm3, %zmm3{%k7}{z}
        .else
            vmovaps %zmm\gvega, %zmm3{%k7}{z}
        .endif
        vsubps %zmm3, %zmm2, %zmm2
    .endif
    vmulps AAD_CTX_DISCOUNT(%rdi){1to16}, %zmm2, %zmm2
    vaddps %zmm2, %zmm\vacc, %zmm\vacc

    .if \put
        vsubps %zmm\arho, %zmm31, %zmm2
        vmovaps %zmm2, %zmm2{%k1}{z}
    .else
        vmovaps %zmm\arho, %zmm2{%k1}{z}
    .endif
    .if \cv
        vmulps AAD_CTRL_B(%r8){1to16}, %zmm\greg, %zmm3
        .if \put
            vsubps %zmm3, %zmm31, %zmm3
        .endif
        vmovaps %zmm3, %zmm3{%k7}{z}
        vsubps %zmm3, %zmm2, %zmm2
    .endif
    vmulps AAD_CTX_DISCOUNT(%rdi){1to16}, %zmm2, %zmm2
    vfnmadd231ps AAD_CTRL_MATURITY(%r8){1to16}, %zmm0, %zmm2
    vaddps %zmm2, %zmm\racc, %zmm\racc
    vaddps %zmm0, %zmm\pacc, %zmm\pacc
.endm

.macro AAD_REDUCE_ONE lo, hi, output_offset, exact_offset, cv
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
    .if \cv
        vaddsd \exact_offset(%r9), %xmm0, %xmm0
    .endif
    vmovsd %xmm0, \output_offset(%r8)
.endm

.macro AAD_FINISH put, cv
    kmovq %k0, %r8
    kmovq %k4, %r9
    .if \put
      .set .LAadExactBase, AAD_CTRL_PUT
    .else
      .set .LAadExactBase, AAD_CTRL_CALL
    .endif
    AAD_REDUCE_ONE 20,21,0,.LAadExactBase+0,\cv
    AAD_REDUCE_ONE 22,23,8,.LAadExactBase+8,\cv
    AAD_REDUCE_ONE 24,25,16,.LAadExactBase+16,\cv
    AAD_REDUCE_ONE 26,27,24,.LAadExactBase+24,\cv
    vzeroupper
    ret
.endm

.macro AAD_FORWARD_FUNCTION name, put, cv
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
    xorq %rax, %rax
.Lpacket_\@:
    kmovq %k6, %rdi
    vbroadcastss AAD_CTX_S0(%rdi), %zmm4
    vmovaps %zmm4, %zmm5
    vxorps %zmm6, %zmm6, %zmm6
    vxorps %zmm7, %zmm7, %zmm7
    vxorps %zmm8, %zmm8, %zmm8
    vxorps %zmm9, %zmm9, %zmm9
    .if \cv
        vxorps %zmm10, %zmm10, %zmm10
        vxorps %zmm11, %zmm11, %zmm11
    .endif
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
    .if \cv
        vfmadd231ps AAD_ROUTE_WEIGHT(%rdi){1to16}, %zmm0, %zmm10
        vfmadd231ps AAD_ROUTE_WEIGHT(%rdi){1to16}, %zmm1, %zmm11
    .endif
    vaddps %zmm0, %zmm8, %zmm8
    vaddps %zmm1, %zmm9, %zmm9
    kmovq %k4, %r8
    vfmadd231ps AAD_CTRL_WEIGHTS(%r8){1to16}, %zmm4, %zmm16
    vfmadd231ps AAD_CTRL_WEIGHTS(%r8){1to16}, %zmm5, %zmm17
    vfmadd231ps %zmm4, %zmm8, %zmm28
    vfmadd231ps %zmm5, %zmm9, %zmm29
    addq $AAD_ROUTE_BYTES, %rdi
    kmovd %k5, %eax
.Lroute_\@:
    AAD_DUAL_ROUTE
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    .if \cv
        vfmadd231ps AAD_ROUTE_WEIGHT(%rdi){1to16}, %zmm0, %zmm10
        vfmadd231ps AAD_ROUTE_WEIGHT(%rdi){1to16}, %zmm1, %zmm11
    .endif
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
    jne .Lroute_\@
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
    .if \cv
        kmovq %k4, %r8
        vaddps AAD_CTRL_LOG_S0(%r8){1to16}, %zmm10, %zmm14
        vaddps AAD_CTRL_LOG_S0(%r8){1to16}, %zmm11, %zmm15
        AAD_EXP 14,12,0,2
        AAD_EXP 15,13,1,3
        vbroadcastss AAD_CTX_C(%rdi), %zmm14
        vmulps AAD_CTRL_B(%r8){1to16}, %zmm14, %zmm14
        vsubps %zmm14, %zmm10, %zmm10
        vsubps %zmm14, %zmm11, %zmm11
        vmulps AAD_CTX_INV_SIGMA(%rdi){1to16}, %zmm10, %zmm10
        vmulps AAD_CTX_INV_SIGMA(%rdi){1to16}, %zmm11, %zmm11
        vmulps %zmm12, %zmm10, %zmm10
        vmulps %zmm13, %zmm11, %zmm11
    .endif
    kmovq %k4, %r8
    AAD_PAY_HALF 6,16,28,12,10,20,22,24,26,\put,\cv
    AAD_PAY_HALF 7,17,29,13,11,21,23,25,27,\put,\cv
    addq $128, %rax
    cmpq $AAD_PATH_BYTES, %rax
    jb .Lpacket_\@
    AAD_FINISH \put,\cv
.size \name,.-\name
.endm

.macro AAD_SUFFIX_FUNCTION name, put, cv
.p2align 6
.globl \name
.type \name,@function
\name:
    kmovq %rsi, %k0
    kmovq %rdi, %k6
    movq AAD_CTX_ROUTES(%rdi), %r8
    kmovq %r8, %k2
    movq AAD_CTX_TAPE(%rdi), %r8
    kmovq %r8, %k3
    movq AAD_CTX_CONTROLS(%rdi), %r8
    kmovq %r8, %k4
    kmovd AAD_CTX_ROUTE_COUNT(%rdi), %k5
    AAD_ZERO_OUTPUT_ACCUMULATORS
    xorq %rax, %rax
.Lpacket_\@:
    kmovq %k6, %rdi
    vbroadcastss AAD_CTX_S0(%rdi), %zmm4
    vmovaps %zmm4, %zmm5
    vxorps %zmm6, %zmm6, %zmm6
    vxorps %zmm7, %zmm7, %zmm7
    .if \cv
        vxorps %zmm10, %zmm10, %zmm10
        vxorps %zmm11, %zmm11, %zmm11
    .endif
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
    kmovq %k3, %r8
    vmovaps %zmm4, 0(%r8)
    vmovaps %zmm5, 64(%r8)
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    .if \cv
        vfmadd231ps AAD_ROUTE_WEIGHT(%rdi){1to16}, %zmm0, %zmm10
        vfmadd231ps AAD_ROUTE_WEIGHT(%rdi){1to16}, %zmm1, %zmm11
    .endif
    addq $AAD_ROUTE_BYTES, %rdi
    kmovd %k5, %eax
.Lforward_\@:
    AAD_DUAL_ROUTE
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    kmovq %k2, %r8
    movq %rdi, %rdx
    subq %r8, %rdx
    shlq $2, %rdx
    kmovq %k3, %r8
    vmovaps %zmm4, 0(%r8,%rdx)
    vmovaps %zmm5, 64(%r8,%rdx)
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    .if \cv
        vfmadd231ps AAD_ROUTE_WEIGHT(%rdi){1to16}, %zmm0, %zmm10
        vfmadd231ps AAD_ROUTE_WEIGHT(%rdi){1to16}, %zmm1, %zmm11
    .endif
    addq $AAD_ROUTE_BYTES, %rdi
    decl %eax
    jne .Lforward_\@
    kmovq %k7, %rax
    kmovq %k6, %rdi
    vmulps AAD_CTX_INV_N(%rdi){1to16}, %zmm6, %zmm8
    vmulps AAD_CTX_INV_N(%rdi){1to16}, %zmm7, %zmm9
    .if \cv
        kmovq %k4, %r8
        vaddps AAD_CTRL_LOG_S0(%r8){1to16}, %zmm10, %zmm14
        vaddps AAD_CTRL_LOG_S0(%r8){1to16}, %zmm11, %zmm15
        AAD_EXP 14,16,0,2
        AAD_EXP 15,17,1,3
        vbroadcastss AAD_CTX_C(%rdi), %zmm14
        vmulps AAD_CTRL_B(%r8){1to16}, %zmm14, %zmm14
        vsubps %zmm14, %zmm10, %zmm10
        vsubps %zmm14, %zmm11, %zmm11
        vmulps AAD_CTX_INV_SIGMA(%rdi){1to16}, %zmm10, %zmm10
        vmulps AAD_CTX_INV_SIGMA(%rdi){1to16}, %zmm11, %zmm11
        vmulps %zmm16, %zmm10, %zmm10
        vmulps %zmm17, %zmm11, %zmm11
    .endif

    vxorps %zmm4, %zmm4, %zmm4
    vxorps %zmm5, %zmm5, %zmm5
    vxorps %zmm6, %zmm6, %zmm6
    vxorps %zmm7, %zmm7, %zmm7
    vxorps %zmm28, %zmm28, %zmm28
    vxorps %zmm29, %zmm29, %zmm29
    kmovq %k2, %rdi
    kmovd %k5, %eax
    movl %eax, %edx
    shll $5, %edx
    addq %rdx, %rdi
.Lreverse_\@:
    kmovq %k2, %r8
    movq %rdi, %rdx
    subq %r8, %rdx
    shlq $2, %rdx
    kmovq %k3, %r8
    vaddps 0(%r8,%rdx), %zmm4, %zmm4
    vaddps 64(%r8,%rdx), %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    AAD_X_ROUTE
    vfmadd231ps %zmm4, %zmm0, %zmm28
    vfmadd231ps %zmm5, %zmm1, %zmm29
    subq $AAD_ROUTE_BYTES, %rdi
    decl %eax
    jne .Lreverse_\@
    kmovq %k3, %r8
    vaddps 0(%r8), %zmm4, %zmm4
    vaddps 64(%r8), %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    kmovq %k7, %rax
    kmovq %k2, %rdi
    movq AAD_ROUTE_X(%rdi), %r8
    vmovaps 0(%r8,%rax), %zmm0
    vmovaps 64(%r8,%rax), %zmm1
    vfmadd231ps %zmm4, %zmm0, %zmm28
    vfmadd231ps %zmm5, %zmm1, %zmm29
    kmovq %k6, %rdi
    vmulps AAD_CTX_DT_OVER_N(%rdi){1to16}, %zmm6, %zmm6
    vmulps AAD_CTX_DT_OVER_N(%rdi){1to16}, %zmm7, %zmm7
    vmulps AAD_CTX_INV_N(%rdi){1to16}, %zmm28, %zmm28
    vmulps AAD_CTX_INV_N(%rdi){1to16}, %zmm29, %zmm29
    vfnmadd231ps AAD_CTX_C(%rdi){1to16}, %zmm6, %zmm28
    vfnmadd231ps AAD_CTX_C(%rdi){1to16}, %zmm7, %zmm29
    vmulps AAD_CTX_INV_SIGMA(%rdi){1to16}, %zmm28, %zmm28
    vmulps AAD_CTX_INV_SIGMA(%rdi){1to16}, %zmm29, %zmm29
    kmovq %k4, %r8
    AAD_PAY_HALF 8,6,28,16,10,20,22,24,26,\put,\cv
    AAD_PAY_HALF 9,7,29,17,11,21,23,25,27,\put,\cv
    addq $128, %rax
    cmpq $AAD_PATH_BYTES, %rax
    jb .Lpacket_\@
    AAD_FINISH \put,\cv
.size \name,.-\name
.endm

AAD_FORWARD_FUNCTION asian_genuine_aad_phase1_forward_arithmetic_call_diag,0,0
AAD_FORWARD_FUNCTION asian_genuine_aad_phase1_forward_arithmetic_put_diag,1,0
AAD_FORWARD_FUNCTION asian_genuine_aad_phase1_forward_cv_call_diag,0,1
AAD_FORWARD_FUNCTION asian_genuine_aad_phase1_forward_cv_put_diag,1,1

AAD_SUFFIX_FUNCTION asian_genuine_aad_phase1_suffix_arithmetic_call_diag,0,0
AAD_SUFFIX_FUNCTION asian_genuine_aad_phase1_suffix_arithmetic_put_diag,1,0
AAD_SUFFIX_FUNCTION asian_genuine_aad_phase1_suffix_cv_call_diag,0,1
AAD_SUFFIX_FUNCTION asian_genuine_aad_phase1_suffix_cv_put_diag,1,1

.section .note.GNU-stack,"",@progbits
