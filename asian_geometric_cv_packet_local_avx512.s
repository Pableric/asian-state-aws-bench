.extern asian_genuine_arithmetic_fused_exp_constants

.equ META_AFF_BASE,320
.equ META_AFF_DELTA,384

.macro PACKET_LOCAL_EXP input, output, exponent, reduced
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

.section .text
.p2align 6
.global asian_geometric_cv_packet_local_qg_diag
.type asian_geometric_cv_packet_local_qg_diag,@function
asian_geometric_cv_packet_local_qg_diag:
    movq 0(%rdi),%rax
    kmovq %rax,%k6
    movq 8(%rdi),%rax
    kmovq %rax,%k5
    movq 16(%rdi),%rax
    kmovq %rax,%k2
    movq 24(%rdi),%rax
    kmovq %rax,%k3
    movq 32(%rdi),%rax
    kmovq %rax,%k4
    movl 40(%rdi),%eax
    decl %eax
    kmovd %eax,%k1
    vbroadcastss 44(%rdi),%zmm31
    vbroadcastss 48(%rdi),%zmm30
    vbroadcastss 52(%rdi),%zmm29
    xorq %rax,%rax
.Lpacket_local_packet:
    kmovq %k6,%r8
    kmovq %k5,%rsi
    vmovaps 0(%r8,%rax),%zmm0
    vmovaps 64(%r8,%rax),%zmm1
    vmovaps 0(%rsi,%rax),%zmm12
    vmovaps 64(%rsi,%rax),%zmm13
    vmovaps %zmm31,%zmm4
    vmovaps %zmm31,%zmm5
    vmulps %zmm12,%zmm4,%zmm4
    vmulps %zmm13,%zmm5,%zmm5
    vmovaps %zmm4,%zmm6
    vmovaps %zmm5,%zmm7
    vxorps %zmm10,%zmm10,%zmm10
    vxorps %zmm11,%zmm11,%zmm11
    vfmadd231ps %zmm30,%zmm0,%zmm10
    vfmadd231ps %zmm30,%zmm1,%zmm11
    movq %rax,%rcx
    shrq $7,%rcx
    kmovq %rax,%k7
    kmovq %k2,%rdi
    kmovd %k1,%eax
.Lpacket_local_route:
    movq 0(%rdi),%r8
    movq 8(%rdi),%rsi
    kmovq %rsi,%k0
    movq 16(%rdi),%r9
    movzbl 0(%r9,%rcx,2),%r11d
    movzbl 1(%r9,%rcx,2),%esi
    shlq $6,%r11
    movl %r11d,%r10d
    xorl $64,%r10d
    vmovdqa32 META_AFF_BASE(%r9),%zmm18
    vmovdqa32 META_AFF_DELTA(%r9),%zmm19
    vpbroadcastd %esi,%zmm2
    vpxord %zmm2,%zmm18,%zmm18
    vpxord %zmm18,%zmm19,%zmm19
    vmovdqa32 0(%r8,%r11),%zmm2
    vmovdqa32 0(%r8,%r10),%zmm3
    vpermd %zmm2,%zmm18,%zmm0
    vpermd %zmm3,%zmm19,%zmm1
    kmovq %k0,%r8
    vmovdqa32 0(%r8,%r11),%zmm14
    vmovdqa32 0(%r8,%r10),%zmm15
    vpermd %zmm14,%zmm18,%zmm12
    vpermd %zmm15,%zmm19,%zmm13
    vmulps %zmm12,%zmm4,%zmm4
    vmulps %zmm13,%zmm5,%zmm5
    vaddps %zmm4,%zmm6,%zmm6
    vaddps %zmm5,%zmm7,%zmm7
    vfmadd231ps 24(%rdi){1to16},%zmm0,%zmm10
    vfmadd231ps 24(%rdi){1to16},%zmm1,%zmm11
    addq $32,%rdi
    decl %eax
    jne .Lpacket_local_route
    vaddps %zmm29,%zmm10,%zmm10
    vaddps %zmm29,%zmm11,%zmm11
    PACKET_LOCAL_EXP 10,4,8,12
    PACKET_LOCAL_EXP 11,5,9,13
    kmovq %k3,%rdx
    kmovq %k4,%rsi
    kmovq %k7,%rax
    vmovaps %zmm6,0(%rdx,%rax)
    vmovaps %zmm7,64(%rdx,%rax)
    vmovaps %zmm4,0(%rsi,%rax)
    vmovaps %zmm5,64(%rsi,%rax)
    addq $128,%rax
    cmpq $16384,%rax
    jb .Lpacket_local_packet
    vzeroupper
    ret
.size asian_geometric_cv_packet_local_qg_diag,.-asian_geometric_cv_packet_local_qg_diag

.section .note.GNU-stack,"",@progbits
