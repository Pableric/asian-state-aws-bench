# Fixed 8192-value ordered-D1 X-only source candidate from 73e92b4.
#
# ABI:
#   rdi: qualified ordered_d1_diag_context_t with X3
#   rsi: 64-byte-aligned x_out[8192]
#
# Both 4096-value source halves advance together.  One coefficient row serves
# four Sobol ZMMs, the +32 jump schedule is static, and the 64 deterministic
# hard positions are repaired in four packed vectors after the ordinary loop.

.equ CTX_JUMPS,        128
.equ CTX_DRIFT,        256
.equ CTX_DIFFUSION,    260
.equ CTX_X3,           24896
.equ COEFF_STRIDE,     8192
.equ HALF_BYTES,       16384

.section .rodata
.align 64
opt_one_bits:          .long 0x3f800000
opt_pair_mask:         .long 0x00000fff
opt_range_mask:        .long 0x000007ff
opt_center:            .long 0x00400000
opt_second_half_xor:   .long 0x00180000

.align 64
opt_reverse_index:
    .long 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0

# Entries 0..126 are ctz(257 + row); the final transition is dead.
opt_jump_indices:
    .byte 0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4
    .byte 0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5
    .byte 0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4
    .byte 0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,6
    .byte 0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4
    .byte 0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5
    .byte 0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4
    .byte 0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,0

# Keep the frozen carrier unchanged.  These generated hard-position constants
# are object-local, as in the qualified candidate commit.
.include "ordered_d1_x_growth_handoff/private/ordered_d1_x_growth_diag_data.inc"

.section .text
.p2align 6

.macro OPT_PAIR_T state, out
    vmovdqa32 %\state, %\out
    vpsrld       $9, %\out, %\out
    vpsubd    %zmm1, %\out, %\out
    vpabsd    %\out, %\out
    vpandd    %zmm2, %\out, %\out
    vpslld      $11, %\out, %\out
    vpord     %zmm0, %\out, %\out
    vsubps    %zmm0, %\out, %\out
.endm

.macro OPT_EVAL_X_SHARED ta, tb, tc, td, oa, ob, oc, od
    vmovaps 3*COEFF_STRIDE+CTX_X3(%rdi,%rcx), %zmm28
    vmovaps %zmm28, %\oa
    vmovaps %zmm28, %\oc
    vmovaps 3*COEFF_STRIDE+CTX_X3(%rdi,%r10), %zmm28
    vmovaps %zmm28, %\ob
    vmovaps %zmm28, %\od

    vmovaps 2*COEFF_STRIDE+CTX_X3(%rdi,%rcx), %zmm28
    vfmadd213ps %zmm28, %\ta, %\oa
    vfmadd213ps %zmm28, %\tc, %\oc
    vmovaps 2*COEFF_STRIDE+CTX_X3(%rdi,%r10), %zmm28
    vfmadd213ps %zmm28, %\tb, %\ob
    vfmadd213ps %zmm28, %\td, %\od

    vmovaps 1*COEFF_STRIDE+CTX_X3(%rdi,%rcx), %zmm28
    vfmadd213ps %zmm28, %\ta, %\oa
    vfmadd213ps %zmm28, %\tc, %\oc
    vmovaps 1*COEFF_STRIDE+CTX_X3(%rdi,%r10), %zmm28
    vfmadd213ps %zmm28, %\tb, %\ob
    vfmadd213ps %zmm28, %\td, %\od

    vmovaps 0*COEFF_STRIDE+CTX_X3(%rdi,%rcx), %zmm28
    vfmadd213ps %zmm28, %\ta, %\oa
    vfmadd213ps %zmm28, %\tc, %\oc
    vmovaps 0*COEFF_STRIDE+CTX_X3(%rdi,%r10), %zmm28
    vfmadd213ps %zmm28, %\tb, %\ob
    vfmadd213ps %zmm28, %\td, %\od
.endm

.macro OPT_PROCESS_X_ROW
    movq %rax, %rcx
    shrq $1, %rcx
    movl $8128, %r10d
    subq %rcx, %r10

    OPT_PAIR_T zmm12,zmm16
    OPT_PAIR_T zmm13,zmm17
    OPT_PAIR_T zmm14,zmm18
    OPT_PAIR_T zmm15,zmm19
    vpermps %zmm17, %zmm3, %zmm17
    vpermps %zmm19, %zmm3, %zmm19

    OPT_EVAL_X_SHARED zmm16,zmm17,zmm18,zmm19,zmm20,zmm21,zmm22,zmm23

    vmovaps %zmm20, 0(%rsi,%rax)
    vpermps %zmm21, %zmm3, %zmm21
    vmovaps %zmm21, 64(%rsi,%rax)
    vmovaps %zmm22, HALF_BYTES+0(%rsi,%rax)
    vpermps %zmm23, %zmm3, %zmm23
    vmovaps %zmm23, HALF_BYTES+64(%rsi,%rax)
    addq $128, %rax
.endm

.macro OPT_ADVANCE jump
    vpxord %\jump, %zmm12, %zmm12
    vpxord %\jump, %zmm13, %zmm13
    vpxord %\jump, %zmm14, %zmm14
    vpxord %\jump, %zmm15, %zmm15
.endm

.macro OPT_HARD_X_VECTOR off
    vmovdqa32 ordered_d1_diag_hard_base_words+\off(%rip), %zmm8
    vpsrld       $9, %zmm8, %zmm8
    vpsubd    %zmm1, %zmm8, %zmm8
    vpabsd    %zmm8, %zmm8
    vpandd    %zmm9, %zmm8, %zmm8
    vpslld      $12, %zmm8, %zmm8
    vpord     %zmm0, %zmm8, %zmm8
    vsubps    %zmm0, %zmm8, %zmm8

    vmovaps ordered_d1_diag_hard_z_c6+\off(%rip), %zmm10
    vfmadd213ps ordered_d1_diag_hard_z_c5+\off(%rip), %zmm8, %zmm10
    vfmadd213ps ordered_d1_diag_hard_z_c4+\off(%rip), %zmm8, %zmm10
    vfmadd213ps ordered_d1_diag_hard_z_c3+\off(%rip), %zmm8, %zmm10
    vfmadd213ps ordered_d1_diag_hard_z_c2+\off(%rip), %zmm8, %zmm10
    vfmadd213ps ordered_d1_diag_hard_z_c1+\off(%rip), %zmm8, %zmm10
    vfmadd213ps ordered_d1_diag_hard_z_c0+\off(%rip), %zmm8, %zmm10
.if \off == 192
    vmovups ordered_d1_diag_range2047_exact(%rip), %xmm11
    vinsertf32x4 $3, %xmm11, %zmm10, %zmm10
.endif

    vbroadcastss CTX_DIFFUSION(%rdi), %zmm11
    vfmadd213ps CTX_DRIFT(%rdi){1to16}, %zmm10, %zmm11

    vmovdqa32 ordered_d1_diag_hard_output_indices+\off(%rip), %zmm13
    movl $-1, %ecx
    kmovw %ecx, %k1
    vscatterdps %zmm11, (%rsi,%zmm13,4){%k1}
.endm

.globl asian_ordered_d1_x_static_8192_diag
.type asian_ordered_d1_x_static_8192_diag,@function
asian_ordered_d1_x_static_8192_diag:
    vpbroadcastd opt_one_bits(%rip), %zmm0
    vpbroadcastd opt_center(%rip), %zmm1
    vpbroadcastd opt_pair_mask(%rip), %zmm2
    vmovdqa32 opt_reverse_index(%rip), %zmm3
    vmovdqa32 0(%rdi), %zmm12
    vmovdqa32 64(%rdi), %zmm13
    vpbroadcastd opt_second_half_xor(%rip), %zmm30
    vpxord %zmm30, %zmm12, %zmm14
    vpxord %zmm30, %zmm13, %zmm15

    xorq %rax, %rax
    leaq opt_jump_indices(%rip), %r8
    movl $128, %r9d
.Lopt_x_row_loop:
    OPT_PROCESS_X_ROW
    movzbl (%r8), %r11d
    vpbroadcastd CTX_JUMPS(%rdi,%r11,4), %zmm7
    addq $1, %r8
    OPT_ADVANCE zmm7
    decl %r9d
    jnz .Lopt_x_row_loop

    vpbroadcastd opt_range_mask(%rip), %zmm9
    OPT_HARD_X_VECTOR 0
    OPT_HARD_X_VECTOR 64
    OPT_HARD_X_VECTOR 128
    OPT_HARD_X_VECTOR 192
    vzeroupper
    ret

.size asian_ordered_d1_x_static_8192_diag,.-asian_ordered_d1_x_static_8192_diag
.section .note.GNU-stack,"",@progbits
