.equ META_ROUTE_BYTES, 32
.equ META_AFF_BASE, 320
.equ META_AFF_DELTA, 384

.section .text
.p2align 6
.globl asian_meta_affine_sql_dual_control_diag
.type asian_meta_affine_sql_dual_control_diag,@function
asian_meta_affine_sql_dual_control_diag:
    kmovq %rdi,%k2
    kmovq %rdx,%k3
    kmovd %esi,%k1
    xorq %rax,%rax
.Lmeta_sql_packet:
    kmovq %k3,%rdx
    vmovaps 0(%rdx,%rax),%zmm4
    vmovaps 64(%rdx,%rax),%zmm5
    vmovaps 16384(%rdx,%rax),%zmm6
    vmovaps 16448(%rdx,%rax),%zmm7
    vmovaps 32768(%rdx,%rax),%zmm10
    vmovaps 32832(%rdx,%rax),%zmm11
    movq %rax,%rcx
    shrq $7,%rcx
    kmovq %rax,%k7
    kmovq %k2,%rdi
    kmovd %k1,%eax
.Lmeta_sql_route:
    movq 0(%rdi),%r8
    movq 8(%rdi),%rsi
    kmovq %rsi,%k5
    movq 16(%rdi),%r9
    movzbl 0(%r9,%rcx,2),%r11d
    movzbl 1(%r9,%rcx,2),%esi
    shll $6,%r11d
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
    kmovq %k5,%r8
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
    addq $META_ROUTE_BYTES,%rdi
    decl %eax
    jne .Lmeta_sql_route
    kmovq %k3,%rdx
    kmovq %k7,%rax
    vmovaps %zmm4,0(%rdx,%rax)
    vmovaps %zmm5,64(%rdx,%rax)
    vmovaps %zmm6,16384(%rdx,%rax)
    vmovaps %zmm7,16448(%rdx,%rax)
    vmovaps %zmm10,32768(%rdx,%rax)
    vmovaps %zmm11,32832(%rdx,%rax)
    addq $128,%rax
    cmpq $16384,%rax
    jb .Lmeta_sql_packet
    vzeroupper
    ret
.size asian_meta_affine_sql_dual_control_diag,.-asian_meta_affine_sql_dual_control_diag

.section .note.GNU-stack,"",@progbits
