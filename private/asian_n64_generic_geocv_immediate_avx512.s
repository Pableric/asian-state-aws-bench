.extern asian_genuine_arithmetic_fused_exp_constants

.equ CTX_D1_X,0
.equ CTX_D1_GROWTH,8
.equ CTX_ROUTES_D2,16
.equ CTX_FIXINGS,24
.equ CTX_S0,28
.equ CTX_D1_WEIGHT,32

.equ STRIP_INV_TOTAL,24
.equ STRIP_INITIAL_Q,28
.equ STRIP_DISCOUNT,32
.equ STRIP_DELTA_Q_SCALE,36
.equ STRIP_DELTA_G_SCALE,40
.equ STRIP_LOG_BASE,44

.equ STRIKE_BYTES,64
.equ STRIKE_VALUE,0
.equ STRIKE_SIGN,4
.equ STRIKE_GEO_PRICE,8
.equ STRIKE_GEO_DELTA,16
.equ STRIKE_CALL_PRICE_ADJUST,24
.equ STRIKE_PUT_PRICE_ADJUST,32
.equ STRIKE_CALL_DELTA_ADJUST,40
.equ STRIKE_PUT_DELTA_ADJUST,48

.equ OUTPUT_BYTES,32
.equ OUTPUT_CALL_PRICE,0
.equ OUTPUT_PUT_PRICE,8
.equ OUTPUT_CALL_DELTA,16
.equ OUTPUT_PUT_DELTA,24

.section .rodata
.p2align 4
.Limmediate_inv_paths: .quad 0x3f30000000000000

.macro IMMEDIATE_EXP input,output,exponent,reduced
    vmulps asian_genuine_arithmetic_fused_exp_constants(%rip){1to16},%zmm\input,%zmm\exponent
    vrndscaleps $0,%zmm\exponent,%zmm\exponent
    vmovaps %zmm\input,%zmm\reduced
    vfnmadd231ps asian_genuine_arithmetic_fused_exp_constants+4(%rip){1to16},%zmm\exponent,%zmm\reduced
    vfnmadd231ps asian_genuine_arithmetic_fused_exp_constants+8(%rip){1to16},%zmm\exponent,%zmm\reduced
    vbroadcastss asian_genuine_arithmetic_fused_exp_constants+44(%rip),%zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+40(%rip){1to16},%zmm\reduced,%zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+36(%rip){1to16},%zmm\reduced,%zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+32(%rip){1to16},%zmm\reduced,%zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+28(%rip){1to16},%zmm\reduced,%zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+24(%rip){1to16},%zmm\reduced,%zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+20(%rip){1to16},%zmm\reduced,%zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+16(%rip){1to16},%zmm\reduced,%zmm\output
    vfmadd213ps asian_genuine_arithmetic_fused_exp_constants+12(%rip){1to16},%zmm\reduced,%zmm\output
    vscalefps %zmm\exponent,%zmm\output,%zmm\output
.endm

.macro ZERO_ACC count,delta
    vxorps %zmm0,%zmm0,%zmm0
    vxorps %zmm4,%zmm4,%zmm4
    .if \count > 1
      vxorps %zmm1,%zmm1,%zmm1
      vxorps %zmm5,%zmm5,%zmm5
    .endif
    .if \count > 2
      vxorps %zmm2,%zmm2,%zmm2
      vxorps %zmm3,%zmm3,%zmm3
      vxorps %zmm6,%zmm6,%zmm6
      vxorps %zmm7,%zmm7,%zmm7
    .endif
    .if \delta
      vxorps %zmm8,%zmm8,%zmm8
      vxorps %zmm12,%zmm12,%zmm12
      .if \count > 1
        vxorps %zmm9,%zmm9,%zmm9
        vxorps %zmm13,%zmm13,%zmm13
      .endif
      .if \count > 2
        vxorps %zmm10,%zmm10,%zmm10
        vxorps %zmm11,%zmm11,%zmm11
        vxorps %zmm14,%zmm14,%zmm14
        vxorps %zmm15,%zmm15,%zmm15
      .endif
    .endif
.endm

.macro PRICE_ONE index,acc,a,g
    vsubps STRIKE_VALUE+STRIKE_BYTES*\index(%rdx){1to16},%zmm\a,%zmm23
    vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rdx){1to16},%zmm23,%zmm23
    vmaxps %zmm28,%zmm23,%zmm23
    vsubps STRIKE_VALUE+STRIKE_BYTES*\index(%rdx){1to16},%zmm\g,%zmm24
    vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rdx){1to16},%zmm24,%zmm24
    vmaxps %zmm28,%zmm24,%zmm24
    vsubps %zmm24,%zmm23,%zmm23
    vmulps STRIP_DISCOUNT(%rsi){1to16},%zmm23,%zmm23
    vaddps %zmm23,%zmm\acc,%zmm\acc
.endm

.macro DELTA_ONE index,acc,a,g,da,dg
    vsubps STRIKE_VALUE+STRIKE_BYTES*\index(%rdx){1to16},%zmm\a,%zmm23
    vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rdx){1to16},%zmm23,%zmm23
    vcmpps $14,%zmm28,%zmm23,%k1
    vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rdx){1to16},%zmm\da,%zmm23{%k1}{z}
    vsubps STRIKE_VALUE+STRIKE_BYTES*\index(%rdx){1to16},%zmm\g,%zmm24
    vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rdx){1to16},%zmm24,%zmm24
    vcmpps $14,%zmm28,%zmm24,%k2
    vmulps STRIKE_SIGN+STRIKE_BYTES*\index(%rdx){1to16},%zmm\dg,%zmm24{%k2}{z}
    vsubps %zmm24,%zmm23,%zmm23
    vaddps %zmm23,%zmm\acc,%zmm\acc
.endm

.macro PAYOFF_HALF count,high,delta,q,g
    .if \delta
      vmulps STRIP_DELTA_Q_SCALE(%rsi){1to16},%zmm\q,%zmm21
    .endif
    vaddps STRIP_INITIAL_Q(%rsi){1to16},%zmm\q,%zmm20
    vmulps STRIP_INV_TOTAL(%rsi){1to16},%zmm20,%zmm20
    .if \delta
      vmulps STRIP_DELTA_G_SCALE(%rsi){1to16},%zmm\g,%zmm22
    .endif
    .if \high
      PRICE_ONE 0,4,20,\g
      .if \delta
        DELTA_ONE 0,12,20,\g,21,22
      .endif
      .if \count > 1
        PRICE_ONE 1,5,20,\g
        .if \delta
          DELTA_ONE 1,13,20,\g,21,22
        .endif
      .endif
      .if \count > 2
        PRICE_ONE 2,6,20,\g
        .if \delta
          DELTA_ONE 2,14,20,\g,21,22
        .endif
        PRICE_ONE 3,7,20,\g
        .if \delta
          DELTA_ONE 3,15,20,\g,21,22
        .endif
      .endif
    .else
      PRICE_ONE 0,0,20,\g
      .if \delta
        DELTA_ONE 0,8,20,\g,21,22
      .endif
      .if \count > 1
        PRICE_ONE 1,1,20,\g
        .if \delta
          DELTA_ONE 1,9,20,\g,21,22
        .endif
      .endif
      .if \count > 2
        PRICE_ONE 2,2,20,\g
        .if \delta
          DELTA_ONE 2,10,20,\g,21,22
        .endif
        PRICE_ONE 3,3,20,\g
        .if \delta
          DELTA_ONE 3,11,20,\g,21,22
        .endif
      .endif
    .endif
.endm

.macro REDUCE_PRICE index,lo,hi
    vaddps %zmm\hi,%zmm\lo,%zmm16
    vextractf32x4 $1,%zmm16,%xmm17
    vextractf32x4 $2,%zmm16,%xmm18
    vextractf32x4 $3,%zmm16,%xmm19
    vaddps %xmm17,%xmm16,%xmm16
    vaddps %xmm19,%xmm18,%xmm18
    vaddps %xmm18,%xmm16,%xmm16
    vmovhlps %xmm16,%xmm16,%xmm17
    vaddps %xmm17,%xmm16,%xmm16
    vshufps $1,%xmm16,%xmm16,%xmm17
    vaddss %xmm17,%xmm16,%xmm16
    vcvtss2sd %xmm16,%xmm16,%xmm16
    vmulsd .Limmediate_inv_paths(%rip),%xmm16,%xmm16
    vaddsd STRIKE_GEO_PRICE+STRIKE_BYTES*\index(%rdx),%xmm16,%xmm16
    vmovapd %xmm16,%xmm17
    vaddsd STRIKE_CALL_PRICE_ADJUST+STRIKE_BYTES*\index(%rdx),%xmm16,%xmm16
    vaddsd STRIKE_PUT_PRICE_ADJUST+STRIKE_BYTES*\index(%rdx),%xmm17,%xmm17
    vmovsd %xmm16,OUTPUT_CALL_PRICE+OUTPUT_BYTES*\index(%rcx)
    vmovsd %xmm17,OUTPUT_PUT_PRICE+OUTPUT_BYTES*\index(%rcx)
.endm

.macro REDUCE_DELTA index,lo,hi
    vaddps %zmm\hi,%zmm\lo,%zmm16
    vextractf32x4 $1,%zmm16,%xmm17
    vextractf32x4 $2,%zmm16,%xmm18
    vextractf32x4 $3,%zmm16,%xmm19
    vaddps %xmm17,%xmm16,%xmm16
    vaddps %xmm19,%xmm18,%xmm18
    vaddps %xmm18,%xmm16,%xmm16
    vmovhlps %xmm16,%xmm16,%xmm17
    vaddps %xmm17,%xmm16,%xmm16
    vshufps $1,%xmm16,%xmm16,%xmm17
    vaddss %xmm17,%xmm16,%xmm16
    vcvtss2sd %xmm16,%xmm16,%xmm16
    vmulsd .Limmediate_inv_paths(%rip),%xmm16,%xmm16
    vaddsd STRIKE_GEO_DELTA+STRIKE_BYTES*\index(%rdx),%xmm16,%xmm16
    vmovapd %xmm16,%xmm17
    vaddsd STRIKE_CALL_DELTA_ADJUST+STRIKE_BYTES*\index(%rdx),%xmm16,%xmm16
    vaddsd STRIKE_PUT_DELTA_ADJUST+STRIKE_BYTES*\index(%rdx),%xmm17,%xmm17
    vmovsd %xmm16,OUTPUT_CALL_DELTA+OUTPUT_BYTES*\index(%rcx)
    vmovsd %xmm17,OUTPUT_PUT_DELTA+OUTPUT_BYTES*\index(%rcx)
.endm

.macro REDUCE_ALL count,delta
    REDUCE_PRICE 0,0,4
    .if \count > 1
      REDUCE_PRICE 1,1,5
    .endif
    .if \count > 2
      REDUCE_PRICE 2,2,6
      REDUCE_PRICE 3,3,7
    .endif
    .if \delta
      REDUCE_DELTA 0,8,12
      .if \count > 1
        REDUCE_DELTA 1,9,13
      .endif
      .if \count > 2
        REDUCE_DELTA 2,10,14
        REDUCE_DELTA 3,11,15
      .endif
    .endif
.endm

.macro IMMEDIATE_LEAF name,count,delta
.p2align 6
.global \name
.type \name,@function
\name:
    movq CTX_D1_X(%rdi),%r12
    movq CTX_D1_GROWTH(%rdi),%r13
    movl CTX_FIXINGS(%rdi),%r9d
    decl %r9d
    shlq $5,%r9
    vbroadcastss CTX_S0(%rdi),%zmm30
    vbroadcastss CTX_D1_WEIGHT(%rdi),%zmm31
    movq CTX_ROUTES_D2(%rdi),%rdi
    leaq (%rdi,%r9),%r9
    ZERO_ACC \count,\delta
    vxorps %zmm28,%zmm28,%zmm28
    xorq %r10,%r10
.Lpacket_\name:
    movq %r10,%rax
    shlq $7,%rax
    vmovaps 0(%r13,%rax),%zmm16
    vmovaps 64(%r13,%rax),%zmm17
    vmulps %zmm30,%zmm16,%zmm16
    vmulps %zmm30,%zmm17,%zmm17
    vmovaps %zmm16,%zmm18
    vmovaps %zmm17,%zmm19
    vxorps %zmm20,%zmm20,%zmm20
    vxorps %zmm21,%zmm21,%zmm21
    vmovaps 0(%r12,%rax),%zmm22
    vmovaps 64(%r12,%rax),%zmm23
    vfmadd231ps %zmm31,%zmm22,%zmm20
    vfmadd231ps %zmm31,%zmm23,%zmm21
    movq %rdi,%r8
.Lroute_\name:
    movq 8(%r8),%r14
    movq 0(%r8),%r15
    movq 16(%r8),%rax
    movzbl 0(%rax,%r10,4),%r11d
    shlq $6,%r11
    vmovdqa32 0(%r14,%r11),%zmm22
    vmovdqa32 0(%r15,%r11),%zmm26
    movzbl 1(%rax,%r10,4),%r11d
    shlq $6,%r11
    vmovdqa32 0(%r14,%r11),%zmm23
    vmovdqa32 0(%r15,%r11),%zmm27
    movzbl 2(%rax,%r10,4),%r11d
    shlq $6,%r11
    vmovdqa32 576(%rax,%r11),%zmm24
    movzbl 3(%rax,%r10,4),%r11d
    shlq $6,%r11
    vmovdqa32 576(%rax,%r11),%zmm25
    vpermd %zmm22,%zmm24,%zmm22
    vpermd %zmm23,%zmm25,%zmm23
    vpermd %zmm26,%zmm24,%zmm24
    vpermd %zmm27,%zmm25,%zmm25
    vmulps %zmm22,%zmm16,%zmm16
    vmulps %zmm23,%zmm17,%zmm17
    vaddps %zmm16,%zmm18,%zmm18
    vaddps %zmm17,%zmm19,%zmm19
    vfmadd231ps 24(%r8){1to16},%zmm24,%zmm20
    vfmadd231ps 24(%r8){1to16},%zmm25,%zmm21
    addq $32,%r8
    cmpq %r9,%r8
    jne .Lroute_\name
    vaddps STRIP_LOG_BASE(%rsi){1to16},%zmm20,%zmm20
    vaddps STRIP_LOG_BASE(%rsi){1to16},%zmm21,%zmm21
    IMMEDIATE_EXP 20,16,22,24
    IMMEDIATE_EXP 21,17,23,25
    PAYOFF_HALF \count,0,\delta,18,16
    PAYOFF_HALF \count,1,\delta,19,17
    incq %r10
    cmpq $128,%r10
    jb .Lpacket_\name
    REDUCE_ALL \count,\delta
    vzeroupper
    ret
.size \name,.-\name
.endm

.section .text
IMMEDIATE_LEAF asian_n64_generic_geocv_immediate_price_1_diag,1,0

.section .note.GNU-stack,"",@progbits
