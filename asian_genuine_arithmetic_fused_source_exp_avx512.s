.section .rodata
.p2align 6
.global asian_genuine_arithmetic_fused_exp_constants
.hidden asian_genuine_arithmetic_fused_exp_constants
.type asian_genuine_arithmetic_fused_exp_constants,@object
asian_genuine_arithmetic_fused_exp_constants:
    .long 0x3fb8aa3b
    .long 0x3f318000
    .long 0xb95e8083
    .long 0x3f800000, 0x3f7ffff9, 0x3efffffc, 0x3e2aabbf
    .long 0x3d2aab67, 0x3c085d88, 0x3ab5de3b, 0x3959cfde
    .long 0x37d8c471
.size asian_genuine_arithmetic_fused_exp_constants,.-asian_genuine_arithmetic_fused_exp_constants

.macro FUSED_EXP input, output, exponent, reduced
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
.global asian_genuine_arithmetic_fused_source_exp_diag
.type asian_genuine_arithmetic_fused_source_exp_diag,@function
asian_genuine_arithmetic_fused_source_exp_diag:
    movq 0(%rdi),%rax
    movq 8(%rdi),%rsi
    vbroadcastss 16(%rdi),%zmm2
    vbroadcastss 20(%rdi),%zmm3
    movl $256,%ecx
.Lfused_source_exp_loop:
    vmovaps 0(%rax),%zmm4
    vmovaps 64(%rax),%zmm5
    vfmadd132ps %zmm3,%zmm2,%zmm4
    vfmadd132ps %zmm3,%zmm2,%zmm5
    FUSED_EXP 4, 6, 8, 10
    FUSED_EXP 5, 7, 9, 11
    vmovaps %zmm6,0(%rsi)
    vmovaps %zmm7,64(%rsi)
    addq $128,%rax
    addq $128,%rsi
    decl %ecx
    jne .Lfused_source_exp_loop
    vzeroupper
    ret
.size asian_genuine_arithmetic_fused_source_exp_diag,.-asian_genuine_arithmetic_fused_source_exp_diag

.section .note.GNU-stack,"",@progbits
