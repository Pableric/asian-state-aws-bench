.section .rodata
.balign 64
.global asian_variable_signed_z_bank
.hidden asian_variable_signed_z_bank
.type asian_variable_signed_z_bank,@object
.global asian_genuine_fixed_block_signed_z
.hidden asian_genuine_fixed_block_signed_z
asian_variable_signed_z_bank:
asian_genuine_fixed_block_signed_z:
    .incbin "private/asian_variable_sobol_signed_z.bin",0,32768
.global asian_genuine_fixed_block_signed_z_end
.hidden asian_genuine_fixed_block_signed_z_end
asian_genuine_fixed_block_signed_z_end:
    .incbin "private/asian_variable_sobol_signed_z.bin",32768
.global asian_variable_signed_z_bank_end
.hidden asian_variable_signed_z_bank_end
asian_variable_signed_z_bank_end:
.size asian_variable_signed_z_bank,.-asian_variable_signed_z_bank
.size asian_genuine_fixed_block_signed_z,32768

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

.balign 32
.global asian_variable_signed_z_bank_sha256
.hidden asian_variable_signed_z_bank_sha256
.type asian_variable_signed_z_bank_sha256,@object
asian_variable_signed_z_bank_sha256:
    .byte 0xf8,0x09,0x8b,0xba,0x7c,0x3c,0xbb,0x4f
    .byte 0xc4,0x06,0x38,0x9b,0xa2,0x9b,0xac,0x37
    .byte 0xc2,0x99,0x13,0x06,0xf3,0x59,0x8b,0x37
    .byte 0x3d,0x1b,0xd7,0xb5,0xf8,0x9c,0x99,0xcc
.size asian_variable_signed_z_bank_sha256,32

.section .text
.p2align 6
.global asian_genuine_fixed_block_signed_z_one_fma_source_diag
.type asian_genuine_fixed_block_signed_z_one_fma_source_diag,@function
asian_genuine_fixed_block_signed_z_one_fma_source_diag:
    movq 0(%rdi),%rax
    vbroadcastss 8(%rdi),%zmm2
    vbroadcastss 12(%rdi),%zmm3
    movl $256,%ecx
.Lvariable_fixed_z_fma_loop:
    vmovaps 0(%rax),%zmm0
    vmovaps 64(%rax),%zmm1
    vfmadd132ps %zmm3,%zmm2,%zmm0
    vfmadd132ps %zmm3,%zmm2,%zmm1
    vmovaps %zmm0,0(%rsi)
    vmovaps %zmm1,64(%rsi)
    addq $128,%rax
    addq $128,%rsi
    decl %ecx
    jne .Lvariable_fixed_z_fma_loop
    vzeroupper
    ret
.size asian_genuine_fixed_block_signed_z_one_fma_source_diag,.-asian_genuine_fixed_block_signed_z_one_fma_source_diag

.p2align 6
.global asian_genuine_fixed_block_prepared_exact_x_lookup_diag
.type asian_genuine_fixed_block_prepared_exact_x_lookup_diag,@function
asian_genuine_fixed_block_prepared_exact_x_lookup_diag:
    movq 0(%rdi),%rax
    movl $256,%ecx
.Lvariable_fixed_exact_x_loop:
    vmovaps 0(%rax),%zmm0
    vmovaps 64(%rax),%zmm1
    vmovaps %zmm0,0(%rsi)
    vmovaps %zmm1,64(%rsi)
    addq $128,%rax
    addq $128,%rsi
    decl %ecx
    jne .Lvariable_fixed_exact_x_loop
    vzeroupper
    ret
.size asian_genuine_fixed_block_prepared_exact_x_lookup_diag,.-asian_genuine_fixed_block_prepared_exact_x_lookup_diag

.section .note.GNU-stack,"",@progbits
