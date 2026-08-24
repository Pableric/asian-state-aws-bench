.equ META_AFF_BASE, 320
.equ META_AFF_DELTA, 384
.equ META_PACKETS, 128

.section .text
.p2align 6
.globl asian_meta_affine_dual_provider_diag
.type asian_meta_affine_dual_provider_diag,@function
/*
 * Test-only full-block provider:
 *   rdi growth donor, rsi x donor, rdx affine context,
 *   rcx growth output, r8 x output.
 *
 * The route fragment itself has two selector-byte loads, no generic pattern
 * loads, and exactly four payload permutations per packet.
 */
asian_meta_affine_dual_provider_diag:
    kmovq %rcx, %k2
    kmovq %r8, %k3
    vmovdqa32 META_AFF_BASE(%rdx), %zmm8
    vmovdqa32 META_AFF_DELTA(%rdx), %zmm9
    xorl %eax, %eax
.Lmeta_dual_packet:
    movzbl 0(%rdx,%rax,2), %r10d
    movzbl 1(%rdx,%rax,2), %r11d
    shll $6, %r10d
    vpbroadcastd %r11d, %zmm18
    vpxord %zmm8, %zmm18, %zmm18
    vpxord %zmm9, %zmm18, %zmm19

    vmovdqa32 0(%rdi,%r10), %zmm14
    xorl $64, %r10d
    vmovdqa32 0(%rdi,%r10), %zmm15
    vpermd %zmm14, %zmm18, %zmm12
    vpermd %zmm15, %zmm19, %zmm13

    xorl $64, %r10d
    vmovdqa32 0(%rsi,%r10), %zmm14
    xorl $64, %r10d
    vmovdqa32 0(%rsi,%r10), %zmm15
    vpermd %zmm14, %zmm18, %zmm0
    vpermd %zmm15, %zmm19, %zmm1

    movq %rax, %r10
    shlq $7, %r10
    kmovq %k2, %rcx
    kmovq %k3, %r8
    vmovaps %zmm12, 0(%rcx,%r10)
    vmovaps %zmm13, 64(%rcx,%r10)
    vmovaps %zmm0, 0(%r8,%r10)
    vmovaps %zmm1, 64(%r8,%r10)
    incq %rax
    cmpq $META_PACKETS, %rax
    jb .Lmeta_dual_packet
    vzeroupper
    ret
.size asian_meta_affine_dual_provider_diag,.-asian_meta_affine_dual_provider_diag

.section .note.GNU-stack,"",@progbits
