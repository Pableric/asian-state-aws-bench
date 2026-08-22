.section .rodata
.balign 64
.global asian_genuine_fixed_block_signed_z
.hidden asian_genuine_fixed_block_signed_z
.type asian_genuine_fixed_block_signed_z,@object
asian_genuine_fixed_block_signed_z:
    .incbin "private/asian_genuine_fixed_block_signed_z.bin"
.global asian_genuine_fixed_block_signed_z_end
.hidden asian_genuine_fixed_block_signed_z_end
asian_genuine_fixed_block_signed_z_end:
.size asian_genuine_fixed_block_signed_z,.-asian_genuine_fixed_block_signed_z

.balign 32
.global asian_genuine_fixed_block_signed_z_sha256
.hidden asian_genuine_fixed_block_signed_z_sha256
.type asian_genuine_fixed_block_signed_z_sha256,@object
asian_genuine_fixed_block_signed_z_sha256:
    .byte 0xec,0xf3,0xbb,0x85,0x4e,0x98,0xbe,0xdc
    .byte 0xf7,0x24,0xd0,0x74,0x34,0x38,0x45,0x7c
    .byte 0xcf,0x8b,0x60,0x0e,0x12,0x64,0xcb,0x53
    .byte 0x77,0x41,0xce,0x0b,0x9d,0x90,0xd9,0x8d
.size asian_genuine_fixed_block_signed_z_sha256,32

.section .text
.p2align 6
.global asian_genuine_fixed_block_signed_z_one_fma_source_diag
.type asian_genuine_fixed_block_signed_z_one_fma_source_diag,@function
asian_genuine_fixed_block_signed_z_one_fma_source_diag:
    movq 0(%rdi),%rax
    vbroadcastss 8(%rdi),%zmm2
    vbroadcastss 12(%rdi),%zmm3
    movl $256,%ecx
.Lfixed_z_fma_loop:
    vmovaps 0(%rax),%zmm0
    vmovaps 64(%rax),%zmm1
    vfmadd132ps %zmm3,%zmm2,%zmm0
    vfmadd132ps %zmm3,%zmm2,%zmm1
    vmovaps %zmm0,0(%rsi)
    vmovaps %zmm1,64(%rsi)
    addq $128,%rax
    addq $128,%rsi
    decl %ecx
    jne .Lfixed_z_fma_loop
    vzeroupper
    ret
.size asian_genuine_fixed_block_signed_z_one_fma_source_diag,.-asian_genuine_fixed_block_signed_z_one_fma_source_diag

.p2align 6
.global asian_genuine_fixed_block_prepared_exact_x_lookup_diag
.type asian_genuine_fixed_block_prepared_exact_x_lookup_diag,@function
asian_genuine_fixed_block_prepared_exact_x_lookup_diag:
    movq 0(%rdi),%rax
    movl $256,%ecx
.Lfixed_exact_x_loop:
    vmovaps 0(%rax),%zmm0
    vmovaps 64(%rax),%zmm1
    vmovaps %zmm0,0(%rsi)
    vmovaps %zmm1,64(%rsi)
    addq $128,%rax
    addq $128,%rsi
    decl %ecx
    jne .Lfixed_exact_x_loop
    vzeroupper
    ret
.size asian_genuine_fixed_block_prepared_exact_x_lookup_diag,.-asian_genuine_fixed_block_prepared_exact_x_lookup_diag

.section .note.GNU-stack,"",@progbits
