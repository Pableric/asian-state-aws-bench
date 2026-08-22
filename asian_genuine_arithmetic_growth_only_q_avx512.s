.equ AGO_CTX_D1_GROWTH,       0
.equ AGO_CTX_ROUTES_D2,       8
.equ AGO_CTX_Q_OUT,          16
.equ AGO_CTX_N,              24
.equ AGO_CTX_S0,             28
.equ AGO_ROUTE_GROWTH,        8
.equ AGO_ROUTE_MAP,          16
.equ AGO_ROUTE_BYTES,        32
.equ AGO_MAP_PATTERNS,      576
.equ AGO_PATH_BYTES,      16384
.equ AGO_TRACE_PLANE,     32768

.section .text

/*
 * Frozen D2..DN body, excluding the three loop-control instructions.
 *
 *   1-2   growth/map pointers
 *   3-6   selector bytes
 *   7-10  selector-to-cache-line shifts
 *   11-12 growth source lines
 *   13-14 controls
 *   15-16 growth-only vpermd (controls die into results)
 *   17-18 S recurrence
 *   19-20 Q recurrence
 *
 * addq/decl/jne make the exact recurring floor 23 instructions.
 */
.macro AGO_ROUTE_GROWTH_ONLY
    movq AGO_ROUTE_GROWTH(%rdi), %r8
    movq AGO_ROUTE_MAP(%rdi), %r9
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
    vmovdqa32 AGO_MAP_PATTERNS(%r9,%rdx), %zmm18
    vmovdqa32 AGO_MAP_PATTERNS(%r9,%r10), %zmm19
    vpermd %zmm14, %zmm18, %zmm18
    vpermd %zmm15, %zmm19, %zmm19
    vmulps %zmm18, %zmm4, %zmm4
    vmulps %zmm19, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
.endm

.p2align 6
.globl asian_genuine_arithmetic_growth_only_q_diag
.type asian_genuine_arithmetic_growth_only_q_diag,@function
asian_genuine_arithmetic_growth_only_q_diag:
    movq AGO_CTX_D1_GROWTH(%rdi), %r8
    kmovq %r8, %k5
    movq AGO_CTX_ROUTES_D2(%rdi), %r8
    kmovq %r8, %k3
    movq AGO_CTX_Q_OUT(%rdi), %r8
    kmovq %r8, %k2
    movl AGO_CTX_N(%rdi), %r8d
    decl %r8d
    kmovd %r8d, %k4
    vbroadcastss AGO_CTX_S0(%rdi), %zmm31
    xorq %rax, %rax
.Lago_packet:
    kmovq %k5, %r8
    vmovaps 0(%r8,%rax), %zmm4
    vmovaps 64(%r8,%rax), %zmm5
    vmulps %zmm31, %zmm4, %zmm4
    vmulps %zmm31, %zmm5, %zmm5
    vmovaps %zmm4, %zmm6
    vmovaps %zmm5, %zmm7
    movq %rax, %rcx
    shrq $7, %rcx
    kmovq %rax, %k7
    kmovq %k3, %rdi
    kmovd %k4, %eax
.Lago_route:
    AGO_ROUTE_GROWTH_ONLY
    addq $AGO_ROUTE_BYTES, %rdi
    decl %eax
    jne .Lago_route
    kmovq %k2, %rdx
    kmovq %k7, %rax
    vmovaps %zmm6, 0(%rdx,%rax)
    vmovaps %zmm7, 64(%rdx,%rax)
    addq $128, %rax
    cmpq $AGO_PATH_BYTES, %rax
    jb .Lago_packet
    vzeroupper
    ret
.size asian_genuine_arithmetic_growth_only_q_diag,.-asian_genuine_arithmetic_growth_only_q_diag

/* Test-only full fixing trace for one canonical packet. */
.p2align 6
.globl asian_genuine_arithmetic_growth_only_packet_probe_diag
.type asian_genuine_arithmetic_growth_only_packet_probe_diag,@function
asian_genuine_arithmetic_growth_only_packet_probe_diag:
    kmovq %rdx, %k6
    movq AGO_CTX_D1_GROWTH(%rdi), %r8
    movq AGO_CTX_ROUTES_D2(%rdi), %r9
    kmovq %r9, %k3
    movl AGO_CTX_N(%rdi), %eax
    decl %eax
    vbroadcastss AGO_CTX_S0(%rdi), %zmm31
    movl %esi, %ecx
    movq %rcx, %r9
    shlq $7, %r9
    vmovaps 0(%r8,%r9), %zmm18
    vmovaps 64(%r8,%r9), %zmm19
    vmovaps %zmm18, %zmm4
    vmovaps %zmm19, %zmm5
    vmulps %zmm31, %zmm4, %zmm4
    vmulps %zmm31, %zmm5, %zmm5
    vmovaps %zmm4, %zmm6
    vmovaps %zmm5, %zmm7
    kmovq %k6, %rdx
    vmovaps %zmm18, 0(%rdx)
    vmovaps %zmm19, 64(%rdx)
    vmovaps %zmm4, AGO_TRACE_PLANE(%rdx)
    vmovaps %zmm5, AGO_TRACE_PLANE+64(%rdx)
    vmovaps %zmm6, AGO_TRACE_PLANE*2(%rdx)
    vmovaps %zmm7, AGO_TRACE_PLANE*2+64(%rdx)
    kmovq %k3, %rdi
.Lago_probe_route:
    AGO_ROUTE_GROWTH_ONLY
    addq $AGO_ROUTE_BYTES, %rdi
    movq %rdi, %r8
    kmovq %k3, %r9
    subq %r9, %r8
    shlq $2, %r8
    kmovq %k6, %rdx
    vmovaps %zmm18, 0(%rdx,%r8)
    vmovaps %zmm19, 64(%rdx,%r8)
    vmovaps %zmm4, AGO_TRACE_PLANE(%rdx,%r8)
    vmovaps %zmm5, AGO_TRACE_PLANE+64(%rdx,%r8)
    vmovaps %zmm6, AGO_TRACE_PLANE*2(%rdx,%r8)
    vmovaps %zmm7, AGO_TRACE_PLANE*2+64(%rdx,%r8)
    decl %eax
    jne .Lago_probe_route
    vzeroupper
    ret
.size asian_genuine_arithmetic_growth_only_packet_probe_diag,.-asian_genuine_arithmetic_growth_only_packet_probe_diag

/* Private immediate arithmetic consumers. */
.equ AGO_STRIP_INV_TOTAL,              24
.equ AGO_STRIP_INITIAL_Q,              28
.equ AGO_STRIP_DISCOUNT,               32
.equ AGO_STRIP_DELTA_Q_SCALE,          36
.equ AGO_STRIKE_BYTES,                 64
.equ AGO_STRIKE_VALUE,                  0
.equ AGO_STRIKE_SIGN,                   4
.equ AGO_STRIKE_CALL_PRICE_ADJUST,     24
.equ AGO_STRIKE_PUT_PRICE_ADJUST,      32
.equ AGO_STRIKE_CALL_DELTA_ADJUST,     40
.equ AGO_STRIKE_PUT_DELTA_ADJUST,      48
.equ AGO_OUTPUT_BYTES,                 32
.equ AGO_OUTPUT_CALL_PRICE,             0
.equ AGO_OUTPUT_PUT_PRICE,              8
.equ AGO_OUTPUT_CALL_DELTA,            16
.equ AGO_OUTPUT_PUT_DELTA,             24

.section .rodata
.p2align 3
.Lago_inv_paths: .quad 0x3f30000000000000

.section .text

.macro AGO_IMMEDIATE_LOAD_STRIKES count
    vbroadcastss AGO_STRIKE_VALUE(%rdx), %zmm0
    .if \count > 1
      vbroadcastss AGO_STRIKE_VALUE+AGO_STRIKE_BYTES(%rdx), %zmm1
    .endif
    .if \count > 2
      vbroadcastss AGO_STRIKE_VALUE+AGO_STRIKE_BYTES*2(%rdx), %zmm2
      vbroadcastss AGO_STRIKE_VALUE+AGO_STRIKE_BYTES*3(%rdx), %zmm3
    .endif
.endm

/* Price low halves use zmm8..11; high halves use zmm20..23. */
.macro AGO_IMMEDIATE_ZERO_PRICE count
    vxorps %zmm8, %zmm8, %zmm8
    vxorps %zmm20, %zmm20, %zmm20
    .if \count > 1
      vxorps %zmm9, %zmm9, %zmm9
      vxorps %zmm21, %zmm21, %zmm21
    .endif
    .if \count > 2
      vxorps %zmm10, %zmm10, %zmm10
      vxorps %zmm11, %zmm11, %zmm11
      vxorps %zmm22, %zmm22, %zmm22
      vxorps %zmm23, %zmm23, %zmm23
    .endif
.endm

/* Delta low halves use zmm12,13,16,17; high halves use zmm24..27. */
.macro AGO_IMMEDIATE_ZERO_DELTA count
    vxorps %zmm12, %zmm12, %zmm12
    vxorps %zmm24, %zmm24, %zmm24
    .if \count > 1
      vxorps %zmm13, %zmm13, %zmm13
      vxorps %zmm25, %zmm25, %zmm25
    .endif
    .if \count > 2
      vxorps %zmm16, %zmm16, %zmm16
      vxorps %zmm17, %zmm17, %zmm17
      vxorps %zmm26, %zmm26, %zmm26
      vxorps %zmm27, %zmm27, %zmm27
    .endif
.endm

.macro AGO_IMMEDIATE_PRICE_ONE index, strike_reg, acc_reg, a_reg
    vsubps %zmm\strike_reg, %zmm\a_reg, %zmm29
    vmulps AGO_STRIKE_SIGN+AGO_STRIKE_BYTES*\index(%rdx){1to16}, %zmm29, %zmm29
    vmaxps %zmm28, %zmm29, %zmm29
    vmulps AGO_STRIP_DISCOUNT(%rsi){1to16}, %zmm29, %zmm29
    vaddps %zmm29, %zmm\acc_reg, %zmm\acc_reg
.endm

.macro AGO_IMMEDIATE_PRICE_STEPS count, high, a_reg
    .if \high
      AGO_IMMEDIATE_PRICE_ONE 0,0,20,\a_reg
      .if \count > 1
        AGO_IMMEDIATE_PRICE_ONE 1,1,21,\a_reg
      .endif
      .if \count > 2
        AGO_IMMEDIATE_PRICE_ONE 2,2,22,\a_reg
        AGO_IMMEDIATE_PRICE_ONE 3,3,23,\a_reg
      .endif
    .else
      AGO_IMMEDIATE_PRICE_ONE 0,0,8,\a_reg
      .if \count > 1
        AGO_IMMEDIATE_PRICE_ONE 1,1,9,\a_reg
      .endif
      .if \count > 2
        AGO_IMMEDIATE_PRICE_ONE 2,2,10,\a_reg
        AGO_IMMEDIATE_PRICE_ONE 3,3,11,\a_reg
      .endif
    .endif
.endm

.macro AGO_IMMEDIATE_DELTA_ONE index, strike_reg, acc_reg, a_reg, basis_reg
    vsubps %zmm\strike_reg, %zmm\a_reg, %zmm30
    vmulps AGO_STRIKE_SIGN+AGO_STRIKE_BYTES*\index(%rdx){1to16}, %zmm30, %zmm30
    vcmpps $14, %zmm28, %zmm30, %k1
    vmulps AGO_STRIKE_SIGN+AGO_STRIKE_BYTES*\index(%rdx){1to16}, %zmm\basis_reg, %zmm30{%k1}{z}
    vaddps %zmm30, %zmm\acc_reg, %zmm\acc_reg
.endm

.macro AGO_IMMEDIATE_DELTA_STEPS count, high, a_reg, basis_reg
    .if \high
      AGO_IMMEDIATE_DELTA_ONE 0,0,24,\a_reg,\basis_reg
      .if \count > 1
        AGO_IMMEDIATE_DELTA_ONE 1,1,25,\a_reg,\basis_reg
      .endif
      .if \count > 2
        AGO_IMMEDIATE_DELTA_ONE 2,2,26,\a_reg,\basis_reg
        AGO_IMMEDIATE_DELTA_ONE 3,3,27,\a_reg,\basis_reg
      .endif
    .else
      AGO_IMMEDIATE_DELTA_ONE 0,0,12,\a_reg,\basis_reg
      .if \count > 1
        AGO_IMMEDIATE_DELTA_ONE 1,1,13,\a_reg,\basis_reg
      .endif
      .if \count > 2
        AGO_IMMEDIATE_DELTA_ONE 2,2,16,\a_reg,\basis_reg
        AGO_IMMEDIATE_DELTA_ONE 3,3,17,\a_reg,\basis_reg
      .endif
    .endif
.endm

.macro AGO_IMMEDIATE_REDUCE_PRICE index, lo_reg, hi_reg
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
    vmulsd .Lago_inv_paths(%rip), %xmm0, %xmm0
    vmovapd %xmm0, %xmm1
    vaddsd AGO_STRIKE_CALL_PRICE_ADJUST+AGO_STRIKE_BYTES*\index(%rcx), %xmm0, %xmm0
    vaddsd AGO_STRIKE_PUT_PRICE_ADJUST+AGO_STRIKE_BYTES*\index(%rcx), %xmm1, %xmm1
    vmovsd %xmm0, AGO_OUTPUT_CALL_PRICE+AGO_OUTPUT_BYTES*\index(%r8)
    vmovsd %xmm1, AGO_OUTPUT_PUT_PRICE+AGO_OUTPUT_BYTES*\index(%r8)
.endm

.macro AGO_IMMEDIATE_REDUCE_DELTA index, lo_reg, hi_reg
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
    vmulsd .Lago_inv_paths(%rip), %xmm0, %xmm0
    vmovapd %xmm0, %xmm1
    vaddsd AGO_STRIKE_CALL_DELTA_ADJUST+AGO_STRIKE_BYTES*\index(%rcx), %xmm0, %xmm0
    vaddsd AGO_STRIKE_PUT_DELTA_ADJUST+AGO_STRIKE_BYTES*\index(%rcx), %xmm1, %xmm1
    vmovsd %xmm0, AGO_OUTPUT_CALL_DELTA+AGO_OUTPUT_BYTES*\index(%r8)
    vmovsd %xmm1, AGO_OUTPUT_PUT_DELTA+AGO_OUTPUT_BYTES*\index(%r8)
.endm

.macro AGO_IMMEDIATE_REDUCE_PRICES count
    AGO_IMMEDIATE_REDUCE_PRICE 0,8,20
    .if \count > 1
      AGO_IMMEDIATE_REDUCE_PRICE 1,9,21
    .endif
    .if \count > 2
      AGO_IMMEDIATE_REDUCE_PRICE 2,10,22
      AGO_IMMEDIATE_REDUCE_PRICE 3,11,23
    .endif
.endm

.macro AGO_IMMEDIATE_REDUCE_DELTAS count
    AGO_IMMEDIATE_REDUCE_DELTA 0,12,24
    .if \count > 1
      AGO_IMMEDIATE_REDUCE_DELTA 1,13,25
    .endif
    .if \count > 2
      AGO_IMMEDIATE_REDUCE_DELTA 2,16,26
      AGO_IMMEDIATE_REDUCE_DELTA 3,17,27
    .endif
.endm

/*
 * Four-argument private ABI:
 *   rdi growth context, rsi strip context, rdx strike records, rcx output.
 * q_out at growth-context offset 16 is deliberately never loaded.
 */
.macro AGO_IMMEDIATE_LEAF name, count, delta
.p2align 6
.globl \name
.type \name,@function
\name:
    kmovq %rsi, %k6
    kmovq %rdx, %k7
    kmovq %rcx, %k2
    movq AGO_CTX_D1_GROWTH(%rdi), %r8
    kmovq %r8, %k5
    movq AGO_CTX_ROUTES_D2(%rdi), %r8
    kmovq %r8, %k3
    movl AGO_CTX_N(%rdi), %r8d
    decl %r8d
    kmovd %r8d, %k4
    vbroadcastss AGO_CTX_S0(%rdi), %zmm31
    AGO_IMMEDIATE_LOAD_STRIKES \count
    AGO_IMMEDIATE_ZERO_PRICE \count
    .if \delta
      AGO_IMMEDIATE_ZERO_DELTA \count
    .endif
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
    AGO_ROUTE_GROWTH_ONLY
    addq $AGO_ROUTE_BYTES, %rdi
    decl %eax
    jne .Lroute_\name
    kmovq %k6, %rsi
    kmovq %k7, %rdx
    kmovq %k0, %rax
    vxorps %zmm28, %zmm28, %zmm28
    .if \delta
      vaddps AGO_STRIP_INITIAL_Q(%rsi){1to16}, %zmm6, %zmm4
      vmulps AGO_STRIP_INV_TOTAL(%rsi){1to16}, %zmm4, %zmm4
      vmulps AGO_STRIP_DELTA_Q_SCALE(%rsi){1to16}, %zmm6, %zmm14
      AGO_IMMEDIATE_PRICE_STEPS \count,0,4
      AGO_IMMEDIATE_DELTA_STEPS \count,0,4,14
      vaddps AGO_STRIP_INITIAL_Q(%rsi){1to16}, %zmm7, %zmm4
      vmulps AGO_STRIP_INV_TOTAL(%rsi){1to16}, %zmm4, %zmm4
      vmulps AGO_STRIP_DELTA_Q_SCALE(%rsi){1to16}, %zmm7, %zmm14
      AGO_IMMEDIATE_PRICE_STEPS \count,1,4
      AGO_IMMEDIATE_DELTA_STEPS \count,1,4,14
    .else
      vaddps AGO_STRIP_INITIAL_Q(%rsi){1to16}, %zmm6, %zmm6
      vmulps AGO_STRIP_INV_TOTAL(%rsi){1to16}, %zmm6, %zmm6
      AGO_IMMEDIATE_PRICE_STEPS \count,0,6
      vaddps AGO_STRIP_INITIAL_Q(%rsi){1to16}, %zmm7, %zmm7
      vmulps AGO_STRIP_INV_TOTAL(%rsi){1to16}, %zmm7, %zmm7
      AGO_IMMEDIATE_PRICE_STEPS \count,1,7
    .endif
    addq $128, %rax
    cmpq $AGO_PATH_BYTES, %rax
    jb .Lpacket_\name
    kmovq %k7, %rcx
    kmovq %k2, %r8
    AGO_IMMEDIATE_REDUCE_PRICES \count
    .if \delta
      AGO_IMMEDIATE_REDUCE_DELTAS \count
    .endif
    vzeroupper
    ret
.size \name,.-\name
.endm

AGO_IMMEDIATE_LEAF asian_genuine_arithmetic_growth_only_price_1_diag,1,0
AGO_IMMEDIATE_LEAF asian_genuine_arithmetic_growth_only_price_delta_1_diag,1,1
AGO_IMMEDIATE_LEAF asian_genuine_arithmetic_growth_only_price_2_diag,2,0
AGO_IMMEDIATE_LEAF asian_genuine_arithmetic_growth_only_price_delta_2_diag,2,1
AGO_IMMEDIATE_LEAF asian_genuine_arithmetic_growth_only_price_4_diag,4,0
AGO_IMMEDIATE_LEAF asian_genuine_arithmetic_growth_only_price_delta_4_diag,4,1

.section .note.GNU-stack,"",@progbits
