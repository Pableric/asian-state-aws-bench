.include "private/asian_exp_p8_18diag.inc"

.equ MSFR_CTX_ROUTES,       0
.equ MSFR_CTX_CONTROLS,     8
.equ MSFR_CTX_N,           16
.equ MSFR_CTX_ROUTE_COUNT, 20
.equ MSFR_CTX_S0,          24
.equ MSFR_CTX_INV_N,       28
.equ MSFR_CTX_DT_OVER_N,   32
.equ MSFR_CTX_C,           36
.equ MSFR_CTX_INV_SIGMA,   40
.equ MSFR_CTX_INV_S0,      44

.equ MSFR_CTRL_B,          20
.equ MSFR_CTRL_LOG_S0,     28
.equ MSFR_CTRL_WEIGHTS,    32

.equ MSFR_ROUTE_X,          0
.equ MSFR_ROUTE_GROWTH,     8
.equ MSFR_ROUTE_MAP,       16
.equ MSFR_ROUTE_WEIGHT,    24
.equ MSFR_ROUTE_INDEX,     28
.equ MSFR_ROUTE_BYTES,     32
.equ MSFR_MAP_PATTERNS,   576

.equ MSFR_PATH_BYTES,   16384
.equ MSFR_BASIS_A,          0
.equ MSFR_BASIS_A_DELTA, 16384
.equ MSFR_BASIS_A_VEGA,  32768
.equ MSFR_BASIS_A_RHO,   49152
.equ MSFR_BASIS_G,       65536
.equ MSFR_BASIS_G_DELTA, 81920
.equ MSFR_BASIS_G_VEGA,  98304
.equ MSFR_BASIS_G_RHO,  114688

.equ MSFR_CONSUMER_DISCOUNT, 16
.equ MSFR_CONSUMER_MATURITY, 20
.equ MSFR_STRIKE_BYTES,     128
.equ MSFR_STRIKE_VALUE,       0
.equ MSFR_STRIKE_SIGN,        4
.equ MSFR_RAW_BYTES,          32
.equ MSFR_RAW_PRICE,           0
.equ MSFR_RAW_DELTA,           8
.equ MSFR_RAW_VEGA,           16
.equ MSFR_RAW_RHO,            24

.section .rodata
.p2align 4
.LmsfrLog2E: .long 0x3fb8aa3b
.LmsfrLn2Hi: .long 0x3f318000
.LmsfrLn2Lo: .long 0xb95e8083

.section .text

/* Frozen qualified x/growth route.  It consumes the existing two 32-KiB
 * source payloads without creating per-dimension copies. */
.macro MSFR_DUAL_ROUTE
    movq MSFR_ROUTE_X(%rdi), %r8
    movq MSFR_ROUTE_GROWTH(%rdi), %rsi
    kmovq %rsi, %k1
    movq MSFR_ROUTE_MAP(%rdi), %r9
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
    vmovdqa32 MSFR_MAP_PATTERNS(%r9,%rdx), %zmm18
    vmovdqa32 MSFR_MAP_PATTERNS(%r9,%r10), %zmm19
    vpermd %zmm2, %zmm18, %zmm0
    vpermd %zmm3, %zmm19, %zmm1
    kmovq %k1, %r8
    vmovdqa32 0(%r8,%r11), %zmm14
    vmovdqa32 0(%r8,%rsi), %zmm15
    vpermd %zmm14, %zmm18, %zmm12
    vpermd %zmm15, %zmm19, %zmm13
.endm

.macro MSFR_EXP input, output, exponent, reduced
    vmulps .LmsfrLog2E(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vmovaps %zmm\input, %zmm\reduced
    vfnmadd231ps .LmsfrLn2Hi(%rip){1to16}, %zmm\exponent, %zmm\reduced
    vfnmadd231ps .LmsfrLn2Lo(%rip){1to16}, %zmm\exponent, %zmm\reduced
    ASIAN_EXP_P8_18DIAG \reduced, \output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

.p2align 6
.globl asian_genuine_msfr_basis_forward_diag
.type asian_genuine_msfr_basis_forward_diag,@function
asian_genuine_msfr_basis_forward_diag:
    kmovq %rsi, %k0
    kmovq %rdi, %k6
    movq MSFR_CTX_ROUTES(%rdi), %r8
    kmovq %r8, %k2
    movq MSFR_CTX_CONTROLS(%rdi), %r8
    kmovq %r8, %k4
    kmovd MSFR_CTX_ROUTE_COUNT(%rdi), %k5
    xorq %rax, %rax
.Lmsfr_basis_packet:
    kmovq %k6, %rdi
    vbroadcastss MSFR_CTX_S0(%rdi), %zmm4
    vmovaps %zmm4, %zmm5
    vxorps %zmm6, %zmm6, %zmm6
    vxorps %zmm7, %zmm7, %zmm7
    vxorps %zmm8, %zmm8, %zmm8
    vxorps %zmm9, %zmm9, %zmm9
    vxorps %zmm10, %zmm10, %zmm10
    vxorps %zmm11, %zmm11, %zmm11
    vxorps %zmm16, %zmm16, %zmm16
    vxorps %zmm17, %zmm17, %zmm17
    vxorps %zmm28, %zmm28, %zmm28
    vxorps %zmm29, %zmm29, %zmm29
    movq %rax, %rcx
    shrq $7, %rcx
    kmovq %rax, %k7
    kmovq %k2, %rdi
    movq MSFR_ROUTE_X(%rdi), %r8
    movq MSFR_ROUTE_GROWTH(%rdi), %r9
    vmovaps 0(%r8,%rax), %zmm0
    vmovaps 64(%r8,%rax), %zmm1
    vmovaps 0(%r9,%rax), %zmm12
    vmovaps 64(%r9,%rax), %zmm13
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    vfmadd231ps MSFR_ROUTE_WEIGHT(%rdi){1to16}, %zmm0, %zmm10
    vfmadd231ps MSFR_ROUTE_WEIGHT(%rdi){1to16}, %zmm1, %zmm11
    vaddps %zmm0, %zmm8, %zmm8
    vaddps %zmm1, %zmm9, %zmm9
    kmovq %k4, %r8
    vfmadd231ps MSFR_CTRL_WEIGHTS(%r8){1to16}, %zmm4, %zmm16
    vfmadd231ps MSFR_CTRL_WEIGHTS(%r8){1to16}, %zmm5, %zmm17
    vfmadd231ps %zmm4, %zmm8, %zmm28
    vfmadd231ps %zmm5, %zmm9, %zmm29
    addq $MSFR_ROUTE_BYTES, %rdi
    kmovd %k5, %eax
.Lmsfr_basis_route:
    MSFR_DUAL_ROUTE
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    vfmadd231ps MSFR_ROUTE_WEIGHT(%rdi){1to16}, %zmm0, %zmm10
    vfmadd231ps MSFR_ROUTE_WEIGHT(%rdi){1to16}, %zmm1, %zmm11
    vaddps %zmm0, %zmm8, %zmm8
    vaddps %zmm1, %zmm9, %zmm9
    movl MSFR_ROUTE_INDEX(%rdi), %edx
    kmovq %k4, %r8
    vfmadd231ps MSFR_CTRL_WEIGHTS(%r8,%rdx,4){1to16}, %zmm4, %zmm16
    vfmadd231ps MSFR_CTRL_WEIGHTS(%r8,%rdx,4){1to16}, %zmm5, %zmm17
    vfmadd231ps %zmm4, %zmm8, %zmm28
    vfmadd231ps %zmm5, %zmm9, %zmm29
    addq $MSFR_ROUTE_BYTES, %rdi
    decl %eax
    jne .Lmsfr_basis_route

    kmovq %k7, %rax
    kmovq %k6, %rdi
    vmulps MSFR_CTX_INV_N(%rdi){1to16}, %zmm6, %zmm6
    vmulps MSFR_CTX_INV_N(%rdi){1to16}, %zmm7, %zmm7
    vmulps MSFR_CTX_DT_OVER_N(%rdi){1to16}, %zmm16, %zmm16
    vmulps MSFR_CTX_DT_OVER_N(%rdi){1to16}, %zmm17, %zmm17
    vmulps MSFR_CTX_INV_N(%rdi){1to16}, %zmm28, %zmm28
    vmulps MSFR_CTX_INV_N(%rdi){1to16}, %zmm29, %zmm29
    vfnmadd231ps MSFR_CTX_C(%rdi){1to16}, %zmm16, %zmm28
    vfnmadd231ps MSFR_CTX_C(%rdi){1to16}, %zmm17, %zmm29
    vmulps MSFR_CTX_INV_SIGMA(%rdi){1to16}, %zmm28, %zmm28
    vmulps MSFR_CTX_INV_SIGMA(%rdi){1to16}, %zmm29, %zmm29

    kmovq %k4, %r8
    vaddps MSFR_CTRL_LOG_S0(%r8){1to16}, %zmm10, %zmm14
    vaddps MSFR_CTRL_LOG_S0(%r8){1to16}, %zmm11, %zmm15
    MSFR_EXP 14,12,0,2
    MSFR_EXP 15,13,1,3
    vbroadcastss MSFR_CTX_C(%rdi), %zmm14
    vmulps MSFR_CTRL_B(%r8){1to16}, %zmm14, %zmm14
    vsubps %zmm14, %zmm10, %zmm10
    vsubps %zmm14, %zmm11, %zmm11
    vmulps MSFR_CTX_INV_SIGMA(%rdi){1to16}, %zmm10, %zmm10
    vmulps MSFR_CTX_INV_SIGMA(%rdi){1to16}, %zmm11, %zmm11
    vmulps %zmm12, %zmm10, %zmm10
    vmulps %zmm13, %zmm11, %zmm11
    vmulps MSFR_CTX_INV_S0(%rdi){1to16}, %zmm6, %zmm4
    vmulps MSFR_CTX_INV_S0(%rdi){1to16}, %zmm7, %zmm5
    vmulps MSFR_CTX_INV_S0(%rdi){1to16}, %zmm12, %zmm8
    vmulps MSFR_CTX_INV_S0(%rdi){1to16}, %zmm13, %zmm9
    vmulps MSFR_CTRL_B(%r8){1to16}, %zmm12, %zmm14
    vmulps MSFR_CTRL_B(%r8){1to16}, %zmm13, %zmm15

    kmovq %k0, %r8
    vmovaps %zmm6, MSFR_BASIS_A(%r8,%rax)
    vmovaps %zmm7, MSFR_BASIS_A+64(%r8,%rax)
    vmovaps %zmm4, MSFR_BASIS_A_DELTA(%r8,%rax)
    vmovaps %zmm5, MSFR_BASIS_A_DELTA+64(%r8,%rax)
    vmovaps %zmm28, MSFR_BASIS_A_VEGA(%r8,%rax)
    vmovaps %zmm29, MSFR_BASIS_A_VEGA+64(%r8,%rax)
    vmovaps %zmm16, MSFR_BASIS_A_RHO(%r8,%rax)
    vmovaps %zmm17, MSFR_BASIS_A_RHO+64(%r8,%rax)
    vmovaps %zmm12, MSFR_BASIS_G(%r8,%rax)
    vmovaps %zmm13, MSFR_BASIS_G+64(%r8,%rax)
    vmovaps %zmm8, MSFR_BASIS_G_DELTA(%r8,%rax)
    vmovaps %zmm9, MSFR_BASIS_G_DELTA+64(%r8,%rax)
    vmovaps %zmm10, MSFR_BASIS_G_VEGA(%r8,%rax)
    vmovaps %zmm11, MSFR_BASIS_G_VEGA+64(%r8,%rax)
    vmovaps %zmm14, MSFR_BASIS_G_RHO(%r8,%rax)
    vmovaps %zmm15, MSFR_BASIS_G_RHO+64(%r8,%rax)
    addq $128, %rax
    cmpq $MSFR_PATH_BYTES, %rax
    jb .Lmsfr_basis_packet
    vzeroupper
    ret
.size asian_genuine_msfr_basis_forward_diag,.-asian_genuine_msfr_basis_forward_diag

/* Matched oneMKL comparator.  Its point-major-to-dimension-major transform and
 * x/growth production are pipeline work; this leaf performs the same targeted
 * forward evolution without genuine-route permutes. */
.p2align 6
.globl asian_genuine_msfr_dimension_major_basis_diag
.type asian_genuine_msfr_dimension_major_basis_diag,@function
asian_genuine_msfr_dimension_major_basis_diag:
    kmovq %rcx, %k0
    kmovq %rdx, %k6
    kmovq %rdi, %k2
    kmovq %rsi, %k3
    movq MSFR_CTX_ROUTES(%rdx), %r8
    kmovq %r8, %k1
    movq MSFR_CTX_CONTROLS(%rdx), %r8
    kmovq %r8, %k4
    kmovd MSFR_CTX_N(%rdx), %k5
    xorq %rax, %rax
.Lmsfr_dm_packet:
    kmovq %k6, %rdx
    vbroadcastss MSFR_CTX_S0(%rdx), %zmm4
    vmovaps %zmm4, %zmm5
    vxorps %zmm6, %zmm6, %zmm6
    vxorps %zmm7, %zmm7, %zmm7
    vxorps %zmm8, %zmm8, %zmm8
    vxorps %zmm9, %zmm9, %zmm9
    vxorps %zmm10, %zmm10, %zmm10
    vxorps %zmm11, %zmm11, %zmm11
    vxorps %zmm16, %zmm16, %zmm16
    vxorps %zmm17, %zmm17, %zmm17
    vxorps %zmm28, %zmm28, %zmm28
    vxorps %zmm29, %zmm29, %zmm29
    kmovq %rax, %k7
    xorq %r11, %r11
    xorl %r10d, %r10d
    kmovd %k5, %r9d
.Lmsfr_dm_fixing:
    kmovq %k2, %r8
    addq %r11, %r8
    vmovaps 0(%r8,%rax), %zmm0
    vmovaps 64(%r8,%rax), %zmm1
    kmovq %k3, %r8
    addq %r11, %r8
    vmovaps 0(%r8,%rax), %zmm12
    vmovaps 64(%r8,%rax), %zmm13
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    kmovq %k1, %r8
    movq %r10, %rdi
    shlq $5, %rdi
    vfmadd231ps MSFR_ROUTE_WEIGHT(%r8,%rdi){1to16}, %zmm0, %zmm10
    vfmadd231ps MSFR_ROUTE_WEIGHT(%r8,%rdi){1to16}, %zmm1, %zmm11
    vaddps %zmm0, %zmm8, %zmm8
    vaddps %zmm1, %zmm9, %zmm9
    kmovq %k4, %r8
    vfmadd231ps MSFR_CTRL_WEIGHTS(%r8,%r10,4){1to16}, %zmm4, %zmm16
    vfmadd231ps MSFR_CTRL_WEIGHTS(%r8,%r10,4){1to16}, %zmm5, %zmm17
    vfmadd231ps %zmm4, %zmm8, %zmm28
    vfmadd231ps %zmm5, %zmm9, %zmm29
    addq $MSFR_PATH_BYTES, %r11
    incl %r10d
    decl %r9d
    jne .Lmsfr_dm_fixing

    kmovq %k7, %rax
    kmovq %k6, %rdx
    vmulps MSFR_CTX_INV_N(%rdx){1to16}, %zmm6, %zmm6
    vmulps MSFR_CTX_INV_N(%rdx){1to16}, %zmm7, %zmm7
    vmulps MSFR_CTX_DT_OVER_N(%rdx){1to16}, %zmm16, %zmm16
    vmulps MSFR_CTX_DT_OVER_N(%rdx){1to16}, %zmm17, %zmm17
    vmulps MSFR_CTX_INV_N(%rdx){1to16}, %zmm28, %zmm28
    vmulps MSFR_CTX_INV_N(%rdx){1to16}, %zmm29, %zmm29
    vfnmadd231ps MSFR_CTX_C(%rdx){1to16}, %zmm16, %zmm28
    vfnmadd231ps MSFR_CTX_C(%rdx){1to16}, %zmm17, %zmm29
    vmulps MSFR_CTX_INV_SIGMA(%rdx){1to16}, %zmm28, %zmm28
    vmulps MSFR_CTX_INV_SIGMA(%rdx){1to16}, %zmm29, %zmm29
    kmovq %k4, %r8
    vaddps MSFR_CTRL_LOG_S0(%r8){1to16}, %zmm10, %zmm14
    vaddps MSFR_CTRL_LOG_S0(%r8){1to16}, %zmm11, %zmm15
    MSFR_EXP 14,12,0,2
    MSFR_EXP 15,13,1,3
    vbroadcastss MSFR_CTX_C(%rdx), %zmm14
    vmulps MSFR_CTRL_B(%r8){1to16}, %zmm14, %zmm14
    vsubps %zmm14, %zmm10, %zmm10
    vsubps %zmm14, %zmm11, %zmm11
    vmulps MSFR_CTX_INV_SIGMA(%rdx){1to16}, %zmm10, %zmm10
    vmulps MSFR_CTX_INV_SIGMA(%rdx){1to16}, %zmm11, %zmm11
    vmulps %zmm12, %zmm10, %zmm10
    vmulps %zmm13, %zmm11, %zmm11
    vmulps MSFR_CTX_INV_S0(%rdx){1to16}, %zmm6, %zmm4
    vmulps MSFR_CTX_INV_S0(%rdx){1to16}, %zmm7, %zmm5
    vmulps MSFR_CTX_INV_S0(%rdx){1to16}, %zmm12, %zmm8
    vmulps MSFR_CTX_INV_S0(%rdx){1to16}, %zmm13, %zmm9
    vmulps MSFR_CTRL_B(%r8){1to16}, %zmm12, %zmm14
    vmulps MSFR_CTRL_B(%r8){1to16}, %zmm13, %zmm15
    kmovq %k0, %r8
    vmovaps %zmm6, MSFR_BASIS_A(%r8,%rax)
    vmovaps %zmm7, MSFR_BASIS_A+64(%r8,%rax)
    vmovaps %zmm4, MSFR_BASIS_A_DELTA(%r8,%rax)
    vmovaps %zmm5, MSFR_BASIS_A_DELTA+64(%r8,%rax)
    vmovaps %zmm28, MSFR_BASIS_A_VEGA(%r8,%rax)
    vmovaps %zmm29, MSFR_BASIS_A_VEGA+64(%r8,%rax)
    vmovaps %zmm16, MSFR_BASIS_A_RHO(%r8,%rax)
    vmovaps %zmm17, MSFR_BASIS_A_RHO+64(%r8,%rax)
    vmovaps %zmm12, MSFR_BASIS_G(%r8,%rax)
    vmovaps %zmm13, MSFR_BASIS_G+64(%r8,%rax)
    vmovaps %zmm8, MSFR_BASIS_G_DELTA(%r8,%rax)
    vmovaps %zmm9, MSFR_BASIS_G_DELTA+64(%r8,%rax)
    vmovaps %zmm10, MSFR_BASIS_G_VEGA(%r8,%rax)
    vmovaps %zmm11, MSFR_BASIS_G_VEGA+64(%r8,%rax)
    vmovaps %zmm14, MSFR_BASIS_G_RHO(%r8,%rax)
    vmovaps %zmm15, MSFR_BASIS_G_RHO+64(%r8,%rax)
    addq $128, %rax
    cmpq $MSFR_PATH_BYTES, %rax
    jb .Lmsfr_dm_packet
    vzeroupper
    ret
.size asian_genuine_msfr_dimension_major_basis_diag,.-asian_genuine_msfr_dimension_major_basis_diag

.macro MSFR_LOAD_STRIKES count
    vbroadcastss MSFR_STRIKE_VALUE(%rdx), %zmm0
    vbroadcastss MSFR_STRIKE_VALUE+MSFR_STRIKE_BYTES(%rdx), %zmm1
    .if \count == 4
      vbroadcastss MSFR_STRIKE_VALUE+MSFR_STRIKE_BYTES*2(%rdx), %zmm2
      vbroadcastss MSFR_STRIKE_VALUE+MSFR_STRIKE_BYTES*3(%rdx), %zmm3
    .endif
.endm

.macro MSFR_ZERO_ACC count
    vxorps %zmm8, %zmm8, %zmm8
    vxorps %zmm9, %zmm9, %zmm9
    vxorps %zmm16, %zmm16, %zmm16
    vxorps %zmm17, %zmm17, %zmm17
    .if \count == 4
      vxorps %zmm10, %zmm10, %zmm10
      vxorps %zmm11, %zmm11, %zmm11
      vxorps %zmm18, %zmm18, %zmm18
      vxorps %zmm19, %zmm19, %zmm19
    .endif
.endm

.macro MSFR_PRICE_ONE index, strike_reg, acc_reg, a_reg, g_reg, cv
    vsubps %zmm\strike_reg, %zmm\a_reg, %zmm28
    vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm28, %zmm28
    vmaxps %zmm31, %zmm28, %zmm28
    .if \cv
      vsubps %zmm\strike_reg, %zmm\g_reg, %zmm29
      vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm29, %zmm29
      vmaxps %zmm31, %zmm29, %zmm29
      vmulps MSFR_CONSUMER_DISCOUNT(%rsi){1to16}, %zmm28, %zmm28
      vmulps MSFR_CONSUMER_DISCOUNT(%rsi){1to16}, %zmm29, %zmm29
      vsubps %zmm29, %zmm28, %zmm28
    .else
      vmulps MSFR_CONSUMER_DISCOUNT(%rsi){1to16}, %zmm28, %zmm28
    .endif
    vaddps %zmm28, %zmm\acc_reg, %zmm\acc_reg
.endm

.macro MSFR_PRICE_STEPS count, high, a_reg, g_reg, cv
    .if \high
      MSFR_PRICE_ONE 0,0,16,\a_reg,\g_reg,\cv
      MSFR_PRICE_ONE 1,1,17,\a_reg,\g_reg,\cv
      .if \count == 4
        MSFR_PRICE_ONE 2,2,18,\a_reg,\g_reg,\cv
        MSFR_PRICE_ONE 3,3,19,\a_reg,\g_reg,\cv
      .endif
    .else
      MSFR_PRICE_ONE 0,0,8,\a_reg,\g_reg,\cv
      MSFR_PRICE_ONE 1,1,9,\a_reg,\g_reg,\cv
      .if \count == 4
        MSFR_PRICE_ONE 2,2,10,\a_reg,\g_reg,\cv
        MSFR_PRICE_ONE 3,3,11,\a_reg,\g_reg,\cv
      .endif
    .endif
.endm

.macro MSFR_GREEK_ONE index, strike_reg, acc_reg, a_reg, g_reg, av_reg, gv_reg, cv
    vsubps %zmm\strike_reg, %zmm\a_reg, %zmm28
    vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm28, %zmm28
    vcmpps $14, %zmm31, %zmm28, %k1
    vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm\av_reg, %zmm28{%k1}{z}
    .if \cv
      vsubps %zmm\strike_reg, %zmm\g_reg, %zmm29
      vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm29, %zmm29
      vcmpps $14, %zmm31, %zmm29, %k2
      vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm\gv_reg, %zmm29{%k2}{z}
      vsubps %zmm29, %zmm28, %zmm28
    .endif
    vmulps MSFR_CONSUMER_DISCOUNT(%rsi){1to16}, %zmm28, %zmm28
    vaddps %zmm28, %zmm\acc_reg, %zmm\acc_reg
.endm

.macro MSFR_GREEK_STEPS count, high, a_reg, g_reg, av_reg, gv_reg, cv
    .if \high
      MSFR_GREEK_ONE 0,0,16,\a_reg,\g_reg,\av_reg,\gv_reg,\cv
      MSFR_GREEK_ONE 1,1,17,\a_reg,\g_reg,\av_reg,\gv_reg,\cv
      .if \count == 4
        MSFR_GREEK_ONE 2,2,18,\a_reg,\g_reg,\av_reg,\gv_reg,\cv
        MSFR_GREEK_ONE 3,3,19,\a_reg,\g_reg,\av_reg,\gv_reg,\cv
      .endif
    .else
      MSFR_GREEK_ONE 0,0,8,\a_reg,\g_reg,\av_reg,\gv_reg,\cv
      MSFR_GREEK_ONE 1,1,9,\a_reg,\g_reg,\av_reg,\gv_reg,\cv
      .if \count == 4
        MSFR_GREEK_ONE 2,2,10,\a_reg,\g_reg,\av_reg,\gv_reg,\cv
        MSFR_GREEK_ONE 3,3,11,\a_reg,\g_reg,\av_reg,\gv_reg,\cv
      .endif
    .endif
.endm

.macro MSFR_RHO_ONE index, strike_reg, acc_reg, a_reg, g_reg, ar_reg, gr_reg, cv
    vsubps %zmm\strike_reg, %zmm\a_reg, %zmm28
    vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm28, %zmm28
    vcmpps $14, %zmm31, %zmm28, %k1
    vmaxps %zmm31, %zmm28, %zmm28
    vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm\ar_reg, %zmm30{%k1}{z}
    .if \cv
      vsubps %zmm\strike_reg, %zmm\g_reg, %zmm29
      vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm29, %zmm29
      vcmpps $14, %zmm31, %zmm29, %k2
      vmaxps %zmm31, %zmm29, %zmm29
      vmulps MSFR_CONSUMER_DISCOUNT(%rsi){1to16}, %zmm28, %zmm28
      vmulps MSFR_CONSUMER_DISCOUNT(%rsi){1to16}, %zmm29, %zmm29
      vsubps %zmm29, %zmm28, %zmm28
      vmulps MSFR_STRIKE_SIGN+MSFR_STRIKE_BYTES*\index(%rdx){1to16}, %zmm\gr_reg, %zmm29{%k2}{z}
      vsubps %zmm29, %zmm30, %zmm30
    .else
      vmulps MSFR_CONSUMER_DISCOUNT(%rsi){1to16}, %zmm28, %zmm28
    .endif
    vmulps MSFR_CONSUMER_DISCOUNT(%rsi){1to16}, %zmm30, %zmm30
    vfnmadd231ps MSFR_CONSUMER_MATURITY(%rsi){1to16}, %zmm28, %zmm30
    vaddps %zmm30, %zmm\acc_reg, %zmm\acc_reg
.endm

.macro MSFR_RHO_STEPS count, high, a_reg, g_reg, ar_reg, gr_reg, cv
    .if \high
      MSFR_RHO_ONE 0,0,16,\a_reg,\g_reg,\ar_reg,\gr_reg,\cv
      MSFR_RHO_ONE 1,1,17,\a_reg,\g_reg,\ar_reg,\gr_reg,\cv
      .if \count == 4
        MSFR_RHO_ONE 2,2,18,\a_reg,\g_reg,\ar_reg,\gr_reg,\cv
        MSFR_RHO_ONE 3,3,19,\a_reg,\g_reg,\ar_reg,\gr_reg,\cv
      .endif
    .else
      MSFR_RHO_ONE 0,0,8,\a_reg,\g_reg,\ar_reg,\gr_reg,\cv
      MSFR_RHO_ONE 1,1,9,\a_reg,\g_reg,\ar_reg,\gr_reg,\cv
      .if \count == 4
        MSFR_RHO_ONE 2,2,10,\a_reg,\g_reg,\ar_reg,\gr_reg,\cv
        MSFR_RHO_ONE 3,3,11,\a_reg,\g_reg,\ar_reg,\gr_reg,\cv
      .endif
    .endif
.endm

.macro MSFR_REDUCE_ONE index, lo, hi, field
    vaddps %zmm\hi, %zmm\lo, %zmm24
    vextractf32x4 $1, %zmm24, %xmm25
    vextractf32x4 $2, %zmm24, %xmm26
    vextractf32x4 $3, %zmm24, %xmm27
    vaddps %xmm25, %xmm24, %xmm24
    vaddps %xmm27, %xmm26, %xmm26
    vaddps %xmm26, %xmm24, %xmm24
    vmovhlps %xmm24, %xmm24, %xmm25
    vaddps %xmm25, %xmm24, %xmm24
    vshufps $1, %xmm24, %xmm24, %xmm25
    vaddss %xmm25, %xmm24, %xmm24
    vcvtss2sd %xmm24, %xmm24, %xmm24
    vaddsd MSFR_RAW_BYTES*\index+\field(%rcx), %xmm24, %xmm24
    vmovsd %xmm24, MSFR_RAW_BYTES*\index+\field(%rcx)
.endm

.macro MSFR_REDUCE count, field
    MSFR_REDUCE_ONE 0,8,16,\field
    MSFR_REDUCE_ONE 1,9,17,\field
    .if \count == 4
      MSFR_REDUCE_ONE 2,10,18,\field
      MSFR_REDUCE_ONE 3,11,19,\field
    .endif
.endm

.macro MSFR_CONSUMER_LEAF name, count, cv
.p2align 6
.globl \name
.type \name,@function
\name:
    MSFR_LOAD_STRIKES \count
    MSFR_ZERO_ACC \count
    vxorps %zmm31, %zmm31, %zmm31
    xorq %rax, %rax
.Lprice_\@:
    vmovaps MSFR_BASIS_A(%rdi,%rax), %zmm24
    .if \cv
      vmovaps MSFR_BASIS_G(%rdi,%rax), %zmm25
    .endif
    MSFR_PRICE_STEPS \count,0,24,25,\cv
    vmovaps MSFR_BASIS_A+64(%rdi,%rax), %zmm24
    .if \cv
      vmovaps MSFR_BASIS_G+64(%rdi,%rax), %zmm25
    .endif
    MSFR_PRICE_STEPS \count,1,24,25,\cv
    addq $128, %rax
    cmpq $MSFR_PATH_BYTES, %rax
    jb .Lprice_\@
    MSFR_REDUCE \count,MSFR_RAW_PRICE

    MSFR_LOAD_STRIKES \count
    MSFR_ZERO_ACC \count
    vxorps %zmm31, %zmm31, %zmm31
    xorq %rax, %rax
.Ldelta_\@:
    vmovaps MSFR_BASIS_A(%rdi,%rax), %zmm24
    vmovaps MSFR_BASIS_A_DELTA(%rdi,%rax), %zmm26
    .if \cv
      vmovaps MSFR_BASIS_G(%rdi,%rax), %zmm25
      vmovaps MSFR_BASIS_G_DELTA(%rdi,%rax), %zmm27
    .endif
    MSFR_GREEK_STEPS \count,0,24,25,26,27,\cv
    vmovaps MSFR_BASIS_A+64(%rdi,%rax), %zmm24
    vmovaps MSFR_BASIS_A_DELTA+64(%rdi,%rax), %zmm26
    .if \cv
      vmovaps MSFR_BASIS_G+64(%rdi,%rax), %zmm25
      vmovaps MSFR_BASIS_G_DELTA+64(%rdi,%rax), %zmm27
    .endif
    MSFR_GREEK_STEPS \count,1,24,25,26,27,\cv
    addq $128, %rax
    cmpq $MSFR_PATH_BYTES, %rax
    jb .Ldelta_\@
    MSFR_REDUCE \count,MSFR_RAW_DELTA

    MSFR_LOAD_STRIKES \count
    MSFR_ZERO_ACC \count
    vxorps %zmm31, %zmm31, %zmm31
    xorq %rax, %rax
.Lvega_\@:
    vmovaps MSFR_BASIS_A(%rdi,%rax), %zmm24
    vmovaps MSFR_BASIS_A_VEGA(%rdi,%rax), %zmm26
    .if \cv
      vmovaps MSFR_BASIS_G(%rdi,%rax), %zmm25
      vmovaps MSFR_BASIS_G_VEGA(%rdi,%rax), %zmm27
    .endif
    MSFR_GREEK_STEPS \count,0,24,25,26,27,\cv
    vmovaps MSFR_BASIS_A+64(%rdi,%rax), %zmm24
    vmovaps MSFR_BASIS_A_VEGA+64(%rdi,%rax), %zmm26
    .if \cv
      vmovaps MSFR_BASIS_G+64(%rdi,%rax), %zmm25
      vmovaps MSFR_BASIS_G_VEGA+64(%rdi,%rax), %zmm27
    .endif
    MSFR_GREEK_STEPS \count,1,24,25,26,27,\cv
    addq $128, %rax
    cmpq $MSFR_PATH_BYTES, %rax
    jb .Lvega_\@
    MSFR_REDUCE \count,MSFR_RAW_VEGA

    MSFR_LOAD_STRIKES \count
    MSFR_ZERO_ACC \count
    vxorps %zmm31, %zmm31, %zmm31
    xorq %rax, %rax
.Lrho_\@:
    vmovaps MSFR_BASIS_A(%rdi,%rax), %zmm24
    vmovaps MSFR_BASIS_A_RHO(%rdi,%rax), %zmm26
    .if \cv
      vmovaps MSFR_BASIS_G(%rdi,%rax), %zmm25
      vmovaps MSFR_BASIS_G_RHO(%rdi,%rax), %zmm27
    .endif
    MSFR_RHO_STEPS \count,0,24,25,26,27,\cv
    vmovaps MSFR_BASIS_A+64(%rdi,%rax), %zmm24
    vmovaps MSFR_BASIS_A_RHO+64(%rdi,%rax), %zmm26
    .if \cv
      vmovaps MSFR_BASIS_G+64(%rdi,%rax), %zmm25
      vmovaps MSFR_BASIS_G_RHO+64(%rdi,%rax), %zmm27
    .endif
    MSFR_RHO_STEPS \count,1,24,25,26,27,\cv
    addq $128, %rax
    cmpq $MSFR_PATH_BYTES, %rax
    jb .Lrho_\@
    MSFR_REDUCE \count,MSFR_RAW_RHO
    vzeroupper
    ret
.size \name,.-\name
.endm

MSFR_CONSUMER_LEAF asian_genuine_msfr_arithmetic_tile2_diag,2,0
MSFR_CONSUMER_LEAF asian_genuine_msfr_arithmetic_tile4_diag,4,0
MSFR_CONSUMER_LEAF asian_genuine_msfr_cv_tile2_diag,2,1
MSFR_CONSUMER_LEAF asian_genuine_msfr_cv_tile4_diag,4,1

.section .note.GNU-stack,"",@progbits
