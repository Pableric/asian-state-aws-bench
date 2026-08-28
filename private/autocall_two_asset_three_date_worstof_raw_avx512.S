.equ ROUTE_GROWTH,             8
.equ ROUTE_MAP,               16
.equ ROUTE_BYTES,             32
.equ PATH_BYTES,           16384

.equ DATE_COUPON_A,            0
.equ DATE_COUPON_B,            4
.equ DATE_CALL_A,              8
.equ DATE_CALL_B,             12
.equ DATE_COUPON_CASH,        16
.equ DATE_CALL_CASH,          20
.equ DATE_BYTES,              32

.equ PCTX_D1,                  0
.equ PCTX_D3,                  8
.equ PCTX_D5,                 16
.equ PCTX_B,                  24
.equ PCTX_DATE0,              32
.equ PCTX_DATE1,              64
.equ PCTX_DATE2,              96
.equ PCTX_SPOT_A,            128
.equ PCTX_SPOT_B,            132
.equ PCTX_INV_A,             136
.equ PCTX_INV_B,             140
.equ PCTX_PROTECTION_A,      144
.equ PCTX_PROTECTION_B,      148
.equ PCTX_TERMINAL,          152
.equ PCTX_INV_PATHS,         160

.equ ICTX_D1,                  0
.equ ICTX_ROUTES_D2,           8
.equ ICTX_DATE0,              16
.equ ICTX_DATE1,              48
.equ ICTX_DATE2,              80
.equ ICTX_SPOT_A,            112
.equ ICTX_SPOT_B,            116
.equ ICTX_INV_A,             120
.equ ICTX_INV_B,             124
.equ ICTX_PROTECTION_A,      128
.equ ICTX_PROTECTION_B,      132
.equ ICTX_TERMINAL,          136
.equ ICTX_DRIFT_A,           140
.equ ICTX_DIFFUSION_A,       144
.equ ICTX_DRIFT_B,           148
.equ ICTX_DIFFUSION_B,       152
.equ ICTX_RHO,               156
.equ ICTX_CHOLESKY,          160
.equ ICTX_INV_PATHS,         168

.equ BCTX_D1,                  0
.equ BCTX_ROUTES_D2,           8
.equ BCTX_OUTPUT,             16
.equ BCTX_DRIFT,              24
.equ BCTX_DIFFUSION,          28
.equ BCTX_RHO,                32
.equ BCTX_CHOLESKY,           36

.macro WORSTOF_EXP input, output, exponent, reduced
    vmulps asian_genuine_arithmetic_fused_exp_constants(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vmovaps %zmm\input, %zmm\reduced
    vfnmadd231ps asian_genuine_arithmetic_fused_exp_constants+4(%rip){1to16}, %zmm\exponent, %zmm\reduced
    vfnmadd231ps asian_genuine_arithmetic_fused_exp_constants+8(%rip){1to16}, %zmm\exponent, %zmm\reduced
    vbroadcastss asian_genuine_arithmetic_fused_exp_constants+44(%rip), %zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+40(%rip){1to16}, %zmm\reduced, %zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+36(%rip){1to16}, %zmm\reduced, %zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+32(%rip){1to16}, %zmm\reduced, %zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+28(%rip){1to16}, %zmm\reduced, %zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+24(%rip){1to16}, %zmm\reduced, %zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+20(%rip){1to16}, %zmm\reduced, %zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+16(%rip){1to16}, %zmm\reduced, %zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+12(%rip){1to16}, %zmm\reduced, %zmm\output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

/* Producer-only affine signed-z provider.  Outputs zmm12/zmm13. */
.macro B_AFFINE_SOURCE route_offset
    movq \route_offset+ROUTE_GROWTH(%rsi), %r9
    movq \route_offset+ROUTE_MAP(%rsi), %r10
    movzbl 0(%r10,%rcx,2), %r11d
    movzbl 1(%r10,%rcx,2), %edi
    shll $6, %r11d
    vmovdqa32 320(%r10), %zmm12
    vmovdqa32 384(%r10), %zmm13
    vpbroadcastd %edi, %zmm0
    vpxord %zmm0, %zmm12, %zmm12
    vpxord %zmm12, %zmm13, %zmm13
    vmovdqa32 0(%r9,%r11), %zmm14
    xorl $64, %r11d
    vmovdqa32 0(%r9,%r11), %zmm15
    vpermd %zmm14, %zmm12, %zmm12
    vpermd %zmm15, %zmm13, %zmm13
.endm

.macro B_CORRELATE_EXP_STORE output_offset
    vmulps %zmm31, %zmm12, %zmm12
    vmulps %zmm31, %zmm13, %zmm13
    vfmadd231ps %zmm30, %zmm4, %zmm12
    vfmadd231ps %zmm30, %zmm5, %zmm13
    vfmadd132ps %zmm29, %zmm28, %zmm12
    vfmadd132ps %zmm29, %zmm28, %zmm13
    WORSTOF_EXP 12, 6, 8, 10
    WORSTOF_EXP 13, 7, 9, 11
    vmovaps %zmm6, \output_offset(%rdx,%rax)
    vmovaps %zmm7, \output_offset+64(%rdx,%rax)
.endm

.section .text.autocall_worstof_prepare_asset_b_growth,"ax",@progbits
.p2align 6
.globl autocall_worstof_prepare_asset_b_growth
.type autocall_worstof_prepare_asset_b_growth,@function
autocall_worstof_prepare_asset_b_growth:
    movq BCTX_D1(%rdi), %r8
    movq BCTX_ROUTES_D2(%rdi), %rsi
    movq BCTX_OUTPUT(%rdi), %rdx
    vbroadcastss BCTX_DRIFT(%rdi), %zmm28
    vbroadcastss BCTX_DIFFUSION(%rdi), %zmm29
    vbroadcastss BCTX_RHO(%rdi), %zmm30
    vbroadcastss BCTX_CHOLESKY(%rdi), %zmm31
    xorq %rax, %rax
    xorq %rcx, %rcx
.Lworstof_b_packet:
    vmovaps 0(%r8,%rax), %zmm4
    vmovaps 64(%r8,%rax), %zmm5
    B_AFFINE_SOURCE 0
    B_CORRELATE_EXP_STORE 0

    B_AFFINE_SOURCE ROUTE_BYTES
    vmovaps %zmm12, %zmm4
    vmovaps %zmm13, %zmm5
    B_AFFINE_SOURCE (2*ROUTE_BYTES)
    B_CORRELATE_EXP_STORE PATH_BYTES

    B_AFFINE_SOURCE (3*ROUTE_BYTES)
    vmovaps %zmm12, %zmm4
    vmovaps %zmm13, %zmm5
    B_AFFINE_SOURCE (4*ROUTE_BYTES)
    B_CORRELATE_EXP_STORE (2*PATH_BYTES)

    addq $128, %rax
    incq %rcx
    cmpq $PATH_BYTES, %rax
    jb .Lworstof_b_packet
    vzeroupper
    ret
.size autocall_worstof_prepare_asset_b_growth,.-autocall_worstof_prepare_asset_b_growth

/* Prepared-price affine provider.  Outputs zmm12/zmm13. */
.macro P_AFFINE_GROWTH context_route
    movq \context_route(%rdi), %rsi
    movq ROUTE_GROWTH(%rsi), %r8
    movq ROUTE_MAP(%rsi), %r9
    movzbl 0(%r9,%rcx,2), %r11d
    movzbl 1(%r9,%rcx,2), %r10d
    shll $6, %r11d
    vmovdqa32 320(%r9), %zmm16
    vmovdqa32 384(%r9), %zmm17
    vpbroadcastd %r10d, %zmm0
    vpxord %zmm0, %zmm16, %zmm16
    vpxord %zmm16, %zmm17, %zmm17
    vmovdqa32 0(%r8,%r11), %zmm18
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm19
    vpermd %zmm18, %zmm16, %zmm12
    vpermd %zmm19, %zmm17, %zmm13
.endm

.macro WORSTOF_DATE context_date
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vmulps %zmm14, %zmm8, %zmm8
    vmulps %zmm15, %zmm9, %zmm9

    vbroadcastss \context_date+DATE_COUPON_A(%rdi), %zmm0
    vbroadcastss \context_date+DATE_COUPON_B(%rdi), %zmm1
    vcmpps $0x1d, %zmm0, %zmm4, %k1
    vcmpps $0x1d, %zmm1, %zmm8, %k2
    kandw %k2, %k1, %k1
    kandw %k4, %k1, %k1
    vcmpps $0x1d, %zmm0, %zmm5, %k5
    vcmpps $0x1d, %zmm1, %zmm9, %k3
    kandw %k3, %k5, %k5
    kandw %k6, %k5, %k5
    vaddps \context_date+DATE_COUPON_CASH(%rdi){1to16}, %zmm6, %zmm6{%k1}
    vaddps \context_date+DATE_COUPON_CASH(%rdi){1to16}, %zmm7, %zmm7{%k5}

    vbroadcastss \context_date+DATE_CALL_A(%rdi), %zmm0
    vbroadcastss \context_date+DATE_CALL_B(%rdi), %zmm1
    vcmpps $0x1d, %zmm0, %zmm4, %k1
    vcmpps $0x1d, %zmm1, %zmm8, %k2
    kandw %k2, %k1, %k1
    kandw %k4, %k1, %k1
    vcmpps $0x1d, %zmm0, %zmm5, %k5
    vcmpps $0x1d, %zmm1, %zmm9, %k3
    kandw %k3, %k5, %k5
    kandw %k6, %k5, %k5
    vaddps \context_date+DATE_CALL_CASH(%rdi){1to16}, %zmm6, %zmm6{%k1}
    vaddps \context_date+DATE_CALL_CASH(%rdi){1to16}, %zmm7, %zmm7{%k5}
    knotw %k1, %k1
    knotw %k5, %k5
    kandw %k1, %k4, %k4
    kandw %k5, %k6, %k6
.endm

.macro WORSTOF_TERMINAL prot_a, prot_b, inv_a, inv_b, terminal
    vbroadcastss \prot_a(%rdi), %zmm0
    vbroadcastss \prot_b(%rdi), %zmm1
    vcmpps $0x1d, %zmm0, %zmm4, %k1
    vcmpps $0x1d, %zmm1, %zmm8, %k2
    kandw %k2, %k1, %k1
    kandw %k4, %k1, %k1
    vcmpps $0x1d, %zmm0, %zmm5, %k5
    vcmpps $0x1d, %zmm1, %zmm9, %k3
    kandw %k3, %k5, %k5
    kandw %k6, %k5, %k5
    vaddps \terminal(%rdi){1to16}, %zmm6, %zmm6{%k1}
    vaddps \terminal(%rdi){1to16}, %zmm7, %zmm7{%k5}

    knotw %k1, %k2
    knotw %k5, %k3
    kandw %k4, %k2, %k2
    kandw %k6, %k3, %k3
    vmulps \inv_a(%rdi){1to16}, %zmm4, %zmm0
    vmulps \inv_b(%rdi){1to16}, %zmm8, %zmm1
    vminps %zmm1, %zmm0, %zmm0
    vmulps \terminal(%rdi){1to16}, %zmm0, %zmm0
    vaddps %zmm0, %zmm6, %zmm6{%k2}
    vmulps \inv_a(%rdi){1to16}, %zmm5, %zmm0
    vmulps \inv_b(%rdi){1to16}, %zmm9, %zmm1
    vminps %zmm1, %zmm0, %zmm0
    vmulps \terminal(%rdi){1to16}, %zmm0, %zmm0
    vaddps %zmm0, %zmm7, %zmm7{%k3}
.endm

.macro WORSTOF_REDUCE inv_paths
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
    vmulsd \inv_paths(%rdi), %xmm0, %xmm0
.endm

.section .text.autocall_worstof_prepared_leaf,"ax",@progbits
.p2align 6
.globl autocall_worstof_prepared_leaf
.type autocall_worstof_prepared_leaf,@function
autocall_worstof_prepared_leaf:
    movq PCTX_D1(%rdi), %rdx
    vbroadcastss PCTX_SPOT_A(%rdi), %zmm31
    vbroadcastss PCTX_SPOT_B(%rdi), %zmm30
    vxorps %zmm20, %zmm20, %zmm20
    vxorps %zmm21, %zmm21, %zmm21
    xorq %rax, %rax
.Lworstof_prepared_packet:
    vmovaps %zmm31, %zmm4
    vmovaps %zmm31, %zmm5
    vmovaps %zmm30, %zmm8
    vmovaps %zmm30, %zmm9
    vxorps %zmm6, %zmm6, %zmm6
    vxorps %zmm7, %zmm7, %zmm7
    kxnorw %k4, %k4, %k4
    kxnorw %k6, %k6, %k6
    movq %rax, %rcx
    shrq $7, %rcx

    vmovaps 0(%rdx,%rax), %zmm12
    vmovaps 64(%rdx,%rax), %zmm13
    movq PCTX_B(%rdi), %r10
    vmovaps 0(%r10,%rax), %zmm14
    vmovaps 64(%r10,%rax), %zmm15
    WORSTOF_DATE PCTX_DATE0

    P_AFFINE_GROWTH PCTX_D3
    movq PCTX_B(%rdi), %r10
    vmovaps PATH_BYTES(%r10,%rax), %zmm14
    vmovaps PATH_BYTES+64(%r10,%rax), %zmm15
    WORSTOF_DATE PCTX_DATE1

    P_AFFINE_GROWTH PCTX_D5
    movq PCTX_B(%rdi), %r10
    vmovaps (2*PATH_BYTES)(%r10,%rax), %zmm14
    vmovaps (2*PATH_BYTES+64)(%r10,%rax), %zmm15
    WORSTOF_DATE PCTX_DATE2

    WORSTOF_TERMINAL PCTX_PROTECTION_A, PCTX_PROTECTION_B, PCTX_INV_A, PCTX_INV_B, PCTX_TERMINAL
    vaddps %zmm6, %zmm20, %zmm20
    vaddps %zmm7, %zmm21, %zmm21
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lworstof_prepared_packet
    WORSTOF_REDUCE PCTX_INV_PATHS
    vzeroupper
    ret
.size autocall_worstof_prepared_leaf,.-autocall_worstof_prepared_leaf

/* Inline-price affine signed-z provider.  Outputs zmm12/zmm13. */
.macro I_AFFINE_SOURCE route_offset
    movq \route_offset+ROUTE_GROWTH(%rsi), %r8
    movq \route_offset+ROUTE_MAP(%rsi), %r9
    movzbl 0(%r9,%rcx,2), %r11d
    movzbl 1(%r9,%rcx,2), %r10d
    shll $6, %r11d
    vmovdqa32 320(%r9), %zmm10
    vmovdqa32 384(%r9), %zmm11
    vpbroadcastd %r10d, %zmm2
    vpxord %zmm2, %zmm10, %zmm10
    vpxord %zmm10, %zmm11, %zmm11
    vmovdqa32 0(%r8,%r11), %zmm16
    xorl $64, %r11d
    vmovdqa32 0(%r8,%r11), %zmm17
    vpermd %zmm16, %zmm10, %zmm12
    vpermd %zmm17, %zmm11, %zmm13
.endm

.macro I_GROWTH_FROM_UV
    vmovaps %zmm0, %zmm16
    vmovaps %zmm1, %zmm17
    vfmadd132ps %zmm23, %zmm22, %zmm16
    vfmadd132ps %zmm23, %zmm22, %zmm17
    WORSTOF_EXP 16, 14, 2, 10
    WORSTOF_EXP 17, 15, 3, 11

    vmulps %zmm29, %zmm12, %zmm16
    vmulps %zmm29, %zmm13, %zmm17
    vfmadd231ps %zmm28, %zmm0, %zmm16
    vfmadd231ps %zmm28, %zmm1, %zmm17
    vfmadd132ps %zmm25, %zmm24, %zmm16
    vfmadd132ps %zmm25, %zmm24, %zmm17
    WORSTOF_EXP 16, 12, 2, 10
    WORSTOF_EXP 17, 13, 3, 11
.endm

.section .text.autocall_worstof_inline_leaf,"ax",@progbits
.p2align 6
.globl autocall_worstof_inline_leaf
.type autocall_worstof_inline_leaf,@function
autocall_worstof_inline_leaf:
    movq ICTX_D1(%rdi), %rdx
    movq ICTX_ROUTES_D2(%rdi), %rsi
    vbroadcastss ICTX_DRIFT_A(%rdi), %zmm22
    vbroadcastss ICTX_DIFFUSION_A(%rdi), %zmm23
    vbroadcastss ICTX_DRIFT_B(%rdi), %zmm24
    vbroadcastss ICTX_DIFFUSION_B(%rdi), %zmm25
    vbroadcastss ICTX_RHO(%rdi), %zmm28
    vbroadcastss ICTX_CHOLESKY(%rdi), %zmm29
    vbroadcastss ICTX_SPOT_A(%rdi), %zmm31
    vbroadcastss ICTX_SPOT_B(%rdi), %zmm30
    vxorps %zmm20, %zmm20, %zmm20
    vxorps %zmm21, %zmm21, %zmm21
    xorq %rax, %rax
.Lworstof_inline_packet:
    vmovaps %zmm31, %zmm4
    vmovaps %zmm31, %zmm5
    vmovaps %zmm30, %zmm8
    vmovaps %zmm30, %zmm9
    vxorps %zmm6, %zmm6, %zmm6
    vxorps %zmm7, %zmm7, %zmm7
    kxnorw %k4, %k4, %k4
    kxnorw %k6, %k6, %k6
    movq %rax, %rcx
    shrq $7, %rcx

    vmovaps 0(%rdx,%rax), %zmm0
    vmovaps 64(%rdx,%rax), %zmm1
    I_AFFINE_SOURCE 0
    I_GROWTH_FROM_UV
    WORSTOF_DATE ICTX_DATE0

    I_AFFINE_SOURCE ROUTE_BYTES
    vmovaps %zmm12, %zmm0
    vmovaps %zmm13, %zmm1
    I_AFFINE_SOURCE (2*ROUTE_BYTES)
    I_GROWTH_FROM_UV
    WORSTOF_DATE ICTX_DATE1

    I_AFFINE_SOURCE (3*ROUTE_BYTES)
    vmovaps %zmm12, %zmm0
    vmovaps %zmm13, %zmm1
    I_AFFINE_SOURCE (4*ROUTE_BYTES)
    I_GROWTH_FROM_UV
    WORSTOF_DATE ICTX_DATE2

    WORSTOF_TERMINAL ICTX_PROTECTION_A, ICTX_PROTECTION_B, ICTX_INV_A, ICTX_INV_B, ICTX_TERMINAL
    vaddps %zmm6, %zmm20, %zmm20
    vaddps %zmm7, %zmm21, %zmm21
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lworstof_inline_packet
    WORSTOF_REDUCE ICTX_INV_PATHS
    vzeroupper
    ret
.size autocall_worstof_inline_leaf,.-autocall_worstof_inline_leaf

.section .note.GNU-stack,"",@progbits
