# Consecutive-32 canonical D1 European prototype.
#
# ABI:
#   rdi: number of 32-point packets
#   rsi: initial uint32_t states[32]
#   rdx: packet jump words J[32]
#   rcx: compact payoff-ready c0[128][16]
#   r8 : compact payoff-ready c1[128][16]
#   r9 : sparse hard-tail correction context

.section .rodata
.align 64

ordered_bit_mask:
    .long 0x3f800000
ordered_local_mask_2047:
    .long 0x000007ff
ordered_gauss_center:
    .long 0x00400000
ordered_block_xor_fix:
    .long 0x08080000

ordered_exp_p0: .float 1.00000000361
ordered_exp_p1: .float 0.999999559932
ordered_exp_p2: .float 0.499999873009
ordered_exp_p3: .float 0.166670788605
ordered_exp_p4: .float 0.0416673696717
ordered_exp_p5: .float 0.00832308095835
ordered_exp_p6: .float 0.0013875434074
ordered_exp_p7: .float 0.0002077216867
ordered_exp_p8: .float 2.58406812172e-05

# Byte offsets selecting J3,J4,J3,J5,... for the first 31 eight-packet
# groups of every aligned 8192-point block. No runtime ctz is used here.
ordered_group_jump_offsets:
    .long 12, 16, 12, 20, 12, 16, 12, 24
    .long 12, 16, 12, 20, 12, 16, 12, 28
    .long 12, 16, 12, 20, 12, 16, 12, 24
    .long 12, 16, 12, 20, 12, 16, 12

# Fixed transitions used by a final one-to-seven packet remainder.
ordered_tail_jump_indices:
    .byte 0, 1, 0, 2, 0, 1, 0

.include "private/european_ordered_d1_tail.inc"

.equ ORDERED_TAIL_LUT,       16
.equ ORDERED_TAIL_FAST_C0,   64
.equ ORDERED_TAIL_FAST_C1,  320
.equ ORDERED_TAIL_CUBIC_C0, 576
.equ ORDERED_TAIL_CUBIC_C1, 768
.equ ORDERED_TAIL_CUBIC_C2, 960
.equ ORDERED_TAIL_CUBIC_C3, 1152

.section .text
.global price_european_sequence_ordered_d1
.type price_european_sequence_ordered_d1, @function

.macro ORDERED_PRICE_PAIR in_a, in_b
    vpsrld        $9, %\in_a, %\in_a
    vpsrld        $9, %\in_b, %\in_b
    vpord      %zmm0, %\in_a, %\in_a
    vpord      %zmm0, %\in_b, %\in_b
    vmovaps    (%r10,%r11), %zmm26
    vmovaps     (%r9,%r11), %zmm24
    vfmadd132ps %zmm26, %zmm24, %\in_a
    vfmadd132ps %zmm26, %zmm24, %\in_b
    vmaxps     %zmm20, %\in_a, %\in_a
    vmaxps     %zmm20, %\in_b, %\in_b
    vaddps     %\in_a, %zmm16, %zmm16
    vaddps     %\in_b, %zmm17, %zmm17
    addq          $64, %r11
.endm

.macro ORDERED_ADVANCE_PRICE cur_a, cur_b, next_a, next_b, jump
    vpxord     %\jump, %\cur_a, %\next_a
    vpxord     %\jump, %\cur_b, %\next_b
    ORDERED_PRICE_PAIR \cur_a, \cur_b
.endm

.macro ORDERED_PROCESS_EIGHT
    ORDERED_ADVANCE_PRICE zmm12, zmm13, zmm14, zmm15, zmm1
    ORDERED_ADVANCE_PRICE zmm14, zmm15, zmm12, zmm13, zmm2
    ORDERED_ADVANCE_PRICE zmm12, zmm13, zmm14, zmm15, zmm1
    ORDERED_ADVANCE_PRICE zmm14, zmm15, zmm12, zmm13, zmm3
    ORDERED_ADVANCE_PRICE zmm12, zmm13, zmm14, zmm15, zmm1
    ORDERED_ADVANCE_PRICE zmm14, zmm15, zmm12, zmm13, zmm2
    ORDERED_ADVANCE_PRICE zmm12, zmm13, zmm14, zmm15, zmm1
    ORDERED_ADVANCE_PRICE zmm14, zmm15, zmm12, zmm13, zmm7
.endm

# Evaluate one packed 16-lane hard vector.  The packed vectors are grouped by
# raw Gaussian range, so their polynomial degrees are fixed at assembly time.
# The main-loop approximation is recomputed with the identical FMA/clamp, then
# subtracted from the accurate payoff and accumulated under the active mask.
.macro ORDERED_CORRECT_VECTOR off, degree, mask
    vmovdqa32 ordered_tail_base_words+\off(%rip), %zmm4
    vpxord    %zmm8, %zmm4, %zmm4

    # Cheap payoff already included by ORDERED_PRICE_PAIR.
    vmovdqa32 %zmm4, %zmm5
    vpsrld       $9, %zmm5, %zmm5
    vpord     %zmm0, %zmm5, %zmm5
    vmovaps ORDERED_TAIL_FAST_C1+\off(%rdi), %zmm6
    vfmadd213ps ORDERED_TAIL_FAST_C0+\off(%rdi), %zmm5, %zmm6
    vmaxps   %zmm20, %zmm6, %zmm6

    # Fold to local t in [0,1), preserving the local range-2047 LUT index.
    vpsrld       $9, %zmm4, %zmm4
    vpsubd   %zmm23, %zmm4, %zmm4
    vpabsd   %zmm4, %zmm4
    vpandd   %zmm22, %zmm4, %zmm4
.if \off == 192
    vmovdqa32 %zmm4, %zmm30
.endif
    vpslld      $12, %zmm4, %zmm4
    vpord     %zmm0, %zmm4, %zmm4
    vsubps    %zmm0, %zmm4, %zmm4

    vmovaps ordered_tail_gauss_c\degree+\off(%rip), %zmm5
.if \degree >= 5
    vfmadd213ps ordered_tail_gauss_c4+\off(%rip), %zmm4, %zmm5
.endif
.if \degree >= 4
    vfmadd213ps ordered_tail_gauss_c3+\off(%rip), %zmm4, %zmm5
.endif
.if \degree >= 3
    vfmadd213ps ordered_tail_gauss_c2+\off(%rip), %zmm4, %zmm5
.endif
    vfmadd213ps ordered_tail_gauss_c1+\off(%rip), %zmm4, %zmm5
    vfmadd213ps ordered_tail_gauss_c0+\off(%rip), %zmm4, %zmm5

.if \off == 192
    # Range 2047 occupies lanes 12..15 of the final packed vector.
    movw       $0xf000, %ax
    kmovw      %eax, %k5
    kandw      %\mask, %k5, %k5
    korw       %k5, %k5, %k6
    movq       ORDERED_TAIL_LUT(%rdi), %rax
    vxorps     %zmm31, %zmm31, %zmm31
    vgatherdps (%rax,%zmm30,4), %zmm31{%k5}
    vmovaps    %zmm31, %zmm5{%k6}
.endif
    vxorps ordered_tail_sign_bits+\off(%rip), %zmm5, %zmm5

.if \off < 192
    vmovaps ORDERED_TAIL_CUBIC_C3+\off(%rdi), %zmm4
    vfmadd213ps ORDERED_TAIL_CUBIC_C2+\off(%rdi), %zmm5, %zmm4
    vfmadd213ps ORDERED_TAIL_CUBIC_C1+\off(%rdi), %zmm5, %zmm4
    vfmadd213ps ORDERED_TAIL_CUBIC_C0+\off(%rdi), %zmm5, %zmm4
.else
    vmulps    %zmm25, %zmm5, %zmm5
    vbroadcastss ordered_exp_p8(%rip), %zmm4
    vfmadd213ps ordered_exp_p7(%rip){1to16}, %zmm5, %zmm4
    vfmadd213ps ordered_exp_p6(%rip){1to16}, %zmm5, %zmm4
    vfmadd213ps ordered_exp_p5(%rip){1to16}, %zmm5, %zmm4
    vfmadd213ps ordered_exp_p4(%rip){1to16}, %zmm5, %zmm4
    vfmadd213ps ordered_exp_p3(%rip){1to16}, %zmm5, %zmm4
    vfmadd213ps ordered_exp_p2(%rip){1to16}, %zmm5, %zmm4
    vfmadd213ps ordered_exp_p1(%rip){1to16}, %zmm5, %zmm4
    vfmadd213ps ordered_exp_p0(%rip){1to16}, %zmm5, %zmm4
    vfmadd213ps %zmm28, %zmm27, %zmm4
.endif
    vmaxps    %zmm20, %zmm4, %zmm4
    vsubps    %zmm6, %zmm4, %zmm4
    vaddps    %zmm4, %zmm18, %zmm18{%\mask}
.endm

.macro ORDERED_CORRECT_BLOCK
    leaq ordered_tail_active_masks(%rip), %rax
    movq (%rax,%r13,8), %rcx
    kmovw %ecx, %k1
    shrq  $16, %rcx
    kmovw %ecx, %k2
    shrq  $16, %rcx
    kmovw %ecx, %k3
    shrq  $16, %rcx
    kmovw %ecx, %k4
    ORDERED_CORRECT_VECTOR   0, 2, k1
    ORDERED_CORRECT_VECTOR  64, 2, k2
    ORDERED_CORRECT_VECTOR 128, 3, k3
    ORDERED_CORRECT_VECTOR 192, 5, k4
.endm

price_european_sequence_ordered_d1:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    movq    %rdi, %r12
    movq    %r9, %rdi
    movq    %rdx, %rbx
    movq    %rcx, %r9
    movq    %r8, %r10
    movl    $1, %r15d                 # absolute 8192-point block index

    vbroadcastss ordered_bit_mask(%rip), %zmm0
    vpbroadcastd ordered_local_mask_2047(%rip), %zmm22
    vpbroadcastd ordered_gauss_center(%rip), %zmm23
    vpbroadcastd ordered_block_xor_fix(%rip), %zmm29
    vbroadcastss  0(%rdi), %zmm25
    vbroadcastss  4(%rdi), %zmm27
    vbroadcastss  8(%rdi), %zmm28
    vbroadcastss  0(%rbx), %zmm1
    vbroadcastss  4(%rbx), %zmm2
    vbroadcastss  8(%rbx), %zmm3
    vmovdqu32      0(%rsi), %zmm12
    vmovdqu32     64(%rsi), %zmm13
    vxorps       %zmm20, %zmm20, %zmm20
    vxorps       %zmm16, %zmm16, %zmm16
    vxorps       %zmm17, %zmm17, %zmm17
    vxorps       %zmm18, %zmm18, %zmm18
    vxorps        %zmm8,  %zmm8,  %zmm8

.Lordered_block:
    movq    %r12, %r13
    cmpq    $256, %r13
    jbe     .Lordered_block_count_ready
    movl    $256, %r13d
.Lordered_block_count_ready:
    ORDERED_CORRECT_BLOCK
    xorq    %r11, %r11
    leaq    ordered_group_jump_offsets(%rip), %r14

    # First coefficient half: at most sixteen complete eight-packet groups.
    movq    %r13, %rcx
    shrq    $3, %rcx
    cmpq    $16, %rcx
    jbe     .Lordered_first_count_ready
    movl    $16, %ecx
.Lordered_first_count_ready:
    testl   %ecx, %ecx
    jz      .Lordered_after_first
.Lordered_first_groups:
    movslq  (%r14), %rax
    vbroadcastss (%rbx,%rax), %zmm7
    addq    $4, %r14
    ORDERED_PROCESS_EIGHT
    decl    %ecx
    jnz     .Lordered_first_groups
.Lordered_after_first:

    cmpq    $128, %r13
    jbe     .Lordered_tail
    xorq    %r11, %r11

    # Second coefficient half: groups 17..31 use the rest of the static table.
    movq    %r13, %rcx
    subq    $128, %rcx
    shrq    $3, %rcx
    cmpq    $15, %rcx
    jbe     .Lordered_second_count_ready
    movl    $15, %ecx
.Lordered_second_count_ready:
    testl   %ecx, %ecx
    jz      .Lordered_after_second
.Lordered_second_groups:
    movslq  (%r14), %rax
    vbroadcastss (%rbx,%rax), %zmm7
    addq    $4, %r14
    ORDERED_PROCESS_EIGHT
    decl    %ecx
    jnz     .Lordered_second_groups
.Lordered_after_second:

    cmpq    $256, %r13
    jne     .Lordered_tail

    # Group 32 needs a block-boundary transition only when more samples follow.
    cmpq    $256, %r12
    jbe     .Lordered_final_block_jump
    leal    1(%r15), %eax
    tzcntl  %eax, %eax
    addl    $8, %eax
    vbroadcastss (%rbx,%rax,4), %zmm7
    vpxord  %zmm29, %zmm7, %zmm30
    vpxord  %zmm30, %zmm8, %zmm8
    jmp     .Lordered_group32
.Lordered_final_block_jump:
    vxorps  %zmm7, %zmm7, %zmm7
.Lordered_group32:
    ORDERED_PROCESS_EIGHT
    jmp     .Lordered_block_done

.Lordered_tail:
    movl    %r13d, %ecx
    andl    $7, %ecx
    jz      .Lordered_block_done
    xorl    %eax, %eax
    leaq    ordered_tail_jump_indices(%rip), %rsi
.Lordered_tail_loop:
    movzbl  (%rsi,%rax), %edx
    vbroadcastss (%rbx,%rdx,4), %zmm7
    vpxord  %zmm7, %zmm12, %zmm14
    vpxord  %zmm7, %zmm13, %zmm15
    ORDERED_PRICE_PAIR zmm12, zmm13
    vmovdqa32 %zmm14, %zmm12
    vmovdqa32 %zmm15, %zmm13
    incl    %eax
    decl    %ecx
    jnz     .Lordered_tail_loop

.Lordered_block_done:
    subq    %r13, %r12
    jz      .Lordered_done
    incl    %r15d
    jmp     .Lordered_block

.Lordered_done:
    vaddps  %zmm17, %zmm16, %zmm16
    vaddps  %zmm18, %zmm16, %zmm16
    vextractf32x8 $1, %zmm16, %ymm0
    vaddps  %ymm0, %ymm16, %ymm0
    vextractf128 $1, %ymm0, %xmm1
    vaddps  %xmm1, %xmm0, %xmm0
    vhaddps %xmm0, %xmm0, %xmm0
    vhaddps %xmm0, %xmm0, %xmm0
    vcvtss2sd %xmm0, %xmm0, %xmm0
    vzeroupper

    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    leave
    ret

.size price_european_sequence_ordered_d1, .-price_european_sequence_ordered_d1
