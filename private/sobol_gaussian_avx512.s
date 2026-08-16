# === High-level shape =======================================================
# generate_sobol_sequence emits Gaussian-transformed dimension-1 Sobol values.
# One public block is 8192 floats, implemented as two 4096-value internal
# chunks. Each store step writes two zmm registers (32 floats).
#
# Chunks 0 and 1 are the true Sobol prefix. Their low logical half does not
# share the steady-state folded-range schedule, so a first-block path patches
# the known divergent first zmm register of selected two-zmm stores with
# precomputed Gaussian values. Chunks 2+ use the scheduled fast path.
#
# Gaussian sign handling:
#   Normal fast-path coefficients are scheduled per store slot and are fitted
#   directly against the raw mantissa-injected value x = 1 + u. That means the
#   coefficient stream already encodes folded range and sign side: the normal
#   path does not compute a dynamic range, does not fold locally, and does not
#   xor a sign bit. Tail paths still use folded local coordinates plus an
#   explicit sign xor because their higher-degree/LUT handling is shared by
#   positive and negative sides.
#
# === REGISTERS USED: ===
# rdi destination memory address on entry; coefficient byte offset inside GAUSS_STORE_PAIR.
# rsi last 4096-value internal chunk index to generate, using esi in the loop.
# rdx base address direction numbers array
# rax/eax this is the offset of special_x_zmm_offsets
# rbx copy the values of rdx, this is to make it a calle-saved register
# r10 scalar mask accumulator before set_bit13; fold-mask base inside GAUSS_STORE_PAIR.
# r11 pointer to special_x_zmm_offsets
# r12 main loop index.
# r13 current 4096-value internal chunk index.
# r14 for greycode calculation
# r15 calle-saved of rdi
# rbp, rsp: Stack frame; rbp is used for stack-passed coefficient arguments.
#
# Gaussian coefficient parameters, System V ABI:
# rcx         gauss_sched_c0 base
# r8          gauss_sched_c1 base
# r9          gauss_sched_c2 base
# 16(%rbp)    gauss_fold_mask_2048 base (debug/generic fold table; main path no longer loads it)
# 24(%rbp)    gauss_tail_c0 base
# 32(%rbp)    gauss_tail_c1 base
# 40(%rbp)    gauss_tail_c2 base
# 48(%rbp)    gauss_tail_c3 base
# 56(%rbp)    gauss_tail_c4 base
# 64(%rbp)    gauss_tail_c5 base
# 72(%rbp)    gauss_range2047_lut base
# 80(%rbp)    gauss_range2047_mask base
# 88(%rbp)    gauss_linear_c0 base
# 96(%rbp)    gauss_linear_c1 base
# 104(%rbp)   gauss_first_patch_zmm base


.section .rodata
.align 64

bit_mask_14:
    .long 0x3F800000

local_mask_2047:
    .long 0x000007ff

gauss_center_q:
    .long 0x00400000

gauss_sign_bit:
    .long 0x80000000

# Direction-vector byte offsets for the repeated Sobol update pattern.
special_x_zmm_offsets:
    .long 12
    .long 16
    .long 12
    .long 20

    .long 12
    .long 16
    .long 12
    .long 24

    .long 12
    .long 16
    .long 12
    .long 20

    .long 12
    .long 16
    .long 12
    .long 28

    .long 12
    .long 16
    .long 12
    .long 20

    .long 12
    .long 16
    .long 12
    .long 24

    .long 12
    .long 16
    .long 12
    .long 20

    .long 12
    .long 16
    .long 12


.section .text
.global generate_sobol_sequence
.type generate_sobol_sequence, @function
.global generate_sobol_sequence_debug
.type generate_sobol_sequence_debug, @function

# Load the next precomputed first-block Gaussian zmm. The patch table is laid
# out in the exact execution order of the first-block path, so no index or
# branch is needed here.
.macro GAUSS_PATCH_OUT_A out_a
    movq    -48(%rbp), %rax
    vmovaps 0(%rax), %\out_a
    addq    $64, %rax
    movq    %rax, -48(%rbp)
.endm

# First-block special case:
#   - out_a is loaded from the precomputed zmm patch table.
#   - in_b/out_b still runs the normal one-FMA Gaussian path in raw x = 1 + u.
# This avoids wasting normalization/FMA work for the patched zmm while keeping
# the pair store layout and coefficient byte offset identical to the normal
# two-zmm path.
.macro GAUSS_STORE_PAIR_PATCH_A_NORMAL_B in_b, out_a, out_b, neg=0, swap=0
    GAUSS_PATCH_OUT_A \out_a

    vpsrld  $9, %\in_b, %\in_b
    vpord   %zmm0, %\in_b, %\in_b

    movq    96(%rbp), %rax
.if \swap
    vmovaps       0(%rax,%rdi), %zmm27
.else
    vmovaps      64(%rax,%rdi), %zmm27
.endif

    movq    88(%rbp), %rax
.if \swap
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    vmovntps %\out_a, 0(%r15)
    vmovntps %zmm27, 64(%r15)
    add      $128, %r15
    add      $128, %rdi
.endm

# Same as GAUSS_STORE_PAIR_PATCH_A_NORMAL_B, but B uses the scheduled tail
# polynomial/LUT path. A remains a precomputed first-block Gaussian zmm.
.macro GAUSS_STORE_PAIR_PATCH_A_TAIL_B in_b, out_a, out_b, neg=0, swap=0
    GAUSS_PATCH_OUT_A \out_a

    vpsrld  $9, %\in_b, %\in_b

.if \neg
    # Negative side: folded distance is center - shifted. zmm31 keeps the
    # 0..2047 local lattice index for the optional range-2047 LUT override.
    vpsubd  %\in_b, %zmm23, %\in_b
.else
    # Positive side: folded distance is shifted - center. zmm31 keeps the
    # 0..2047 local lattice index for the optional range-2047 LUT override.
    vpsubd  %zmm23, %\in_b, %\in_b
.endif

    vpandd  %zmm22, %\in_b, %\in_b
    vmovdqa32 %\in_b, %zmm31
    vpslld  $12, %\in_b, %\in_b
    vpord   %zmm0, %\in_b, %\in_b
    vsubps  %zmm0, %\in_b, %\in_b

    movq    64(%rbp), %rax
.if \swap
    vmovaps       0(%rax,%rdi), %zmm27
.else
    vmovaps      64(%rax,%rdi), %zmm27
.endif

    movq    56(%rbp), %rax
.if \swap
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    48(%rbp), %rax
.if \swap
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    40(%rbp), %rax
.if \swap
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    32(%rbp), %rax
.if \swap
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    24(%rbp), %rax
.if \swap
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    80(%rbp), %rax
.if \swap
    vmovdqa32   0(%rax,%rdi), %zmm29
.else
    vmovdqa32  64(%rax,%rdi), %zmm29
.endif
    vptestmd %zmm29, %zmm29, %k4
    kortestw %k4, %k4
    jz .Lpatch_tail_b_done\@
    korw %k4, %k4, %k6
    movq    72(%rbp), %rax
    vxorps %zmm29, %zmm29, %zmm29
    vgatherdps (%rax,%zmm31,4), %zmm29{%k4}
    vmovaps %zmm29, %zmm27{%k6}
.Lpatch_tail_b_done\@:

.if \neg
    vxorps  %zmm21, %zmm27, %\out_b   # apply negative Gaussian sign
.else
    vmovaps %zmm27, %\out_b
.endif

    vmovntps %\out_a, 0(%r15)
    vmovntps %\out_b, 64(%r15)
    add      $128, %r15
    add      $128, %rdi
.endm

# Fast normal path: scheduled signed linear coefficients in raw x = 1 + u.
# Converts two Sobol zmm registers into two Gaussian zmm registers:
#   1. Keep the high 23 Sobol bits with vpsrld $9.
#   2. Mantissa-inject to x = 1 + u.
#   3. Evaluate signed a0 + a1*x with one FMA. The scheduled coefficient
#      stream already encodes the folded range and side for this exact slot.
.macro GAUSS_STORE_PAIR_NORMAL in_a, in_b, out_a, out_b, neg=0, swap=0
    vpsrld  $9, %\in_a, %\in_a
    vpsrld  $9, %\in_b, %\in_b
    vpord   %zmm0, %\in_a, %\in_a
    vpord   %zmm0, %\in_b, %\in_b

    movq    96(%rbp), %rax
.if \swap
    vmovaps      64(%rax,%rdi), %zmm26
    vmovaps       0(%rax,%rdi), %zmm27
.else
    vmovaps       0(%rax,%rdi), %zmm26
    vmovaps      64(%rax,%rdi), %zmm27
.endif

    movq    88(%rbp), %rax
.if \swap
    vfmadd213ps  64(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps   0(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    vmovntps %zmm26, 0(%r15)
    vmovntps %zmm27, 64(%r15)
    add      $128, %r15
    add      $128, %rdi
.endm

# Tail path: scheduled degree-5 coefficients in local t plus range-2047 LUT.
# Used only for the known hard store positions. The mask table selects lanes in
# folded range 2047, where the exact precomputed LUT beats the polynomial.
.macro GAUSS_STORE_PAIR_TAIL in_a, in_b, out_a, out_b, neg=0, swap=0
    vpsrld  $9, %\in_a, %\in_a
    vpsrld  $9, %\in_b, %\in_b

.if \neg
    # Negative side: folded distance is center - shifted. zmm30/zmm31 preserve
    # the local lattice indices for optional range-2047 gathers below.
    vpsubd  %\in_a, %zmm23, %\in_a
    vpsubd  %\in_b, %zmm23, %\in_b
.else
    # Positive side: folded distance is shifted - center. zmm30/zmm31 preserve
    # the local lattice indices for optional range-2047 gathers below.
    vpsubd  %zmm23, %\in_a, %\in_a
    vpsubd  %zmm23, %\in_b, %\in_b
.endif

    vpandd  %zmm22, %\in_a, %\in_a
    vpandd  %zmm22, %\in_b, %\in_b
    vmovdqa32 %\in_a, %zmm30
    vmovdqa32 %\in_b, %zmm31
    vpslld  $12, %\in_a, %\in_a
    vpslld  $12, %\in_b, %\in_b
    vpord   %zmm0, %\in_a, %\in_a
    vpord   %zmm0, %\in_b, %\in_b
    vsubps  %zmm0, %\in_a, %\in_a
    vsubps  %zmm0, %\in_b, %\in_b

    movq    64(%rbp), %rax
.if \swap
    vmovaps      64(%rax,%rdi), %zmm26
    vmovaps       0(%rax,%rdi), %zmm27
.else
    vmovaps       0(%rax,%rdi), %zmm26
    vmovaps      64(%rax,%rdi), %zmm27
.endif

    movq    56(%rbp), %rax
.if \swap
    vfmadd213ps  64(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps   0(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    48(%rbp), %rax
.if \swap
    vfmadd213ps  64(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps   0(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    40(%rbp), %rax
.if \swap
    vfmadd213ps  64(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps   0(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    32(%rbp), %rax
.if \swap
    vfmadd213ps  64(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps   0(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    24(%rbp), %rax
.if \swap
    vfmadd213ps  64(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps   0(%rax,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps   0(%rax,%rdi), %\in_a, %zmm26
    vfmadd213ps  64(%rax,%rdi), %\in_b, %zmm27
.endif

    movq    80(%rbp), %rax
.if \swap
    vmovdqa32  64(%rax,%rdi), %zmm28
    vmovdqa32   0(%rax,%rdi), %zmm29
.else
    vmovdqa32   0(%rax,%rdi), %zmm28
    vmovdqa32  64(%rax,%rdi), %zmm29
.endif
    vptestmd %zmm28, %zmm28, %k3
    vptestmd %zmm29, %zmm29, %k4
    kortestw %k3, %k3
    jz 1f
    korw %k3, %k3, %k5
    movq    72(%rbp), %rax
    vxorps %zmm28, %zmm28, %zmm28
    vgatherdps (%rax,%zmm30,4), %zmm28{%k3}
    vmovaps %zmm28, %zmm26{%k5}
1:
    kortestw %k4, %k4
    jz 2f
    korw %k4, %k4, %k6
    movq    72(%rbp), %rax
    vxorps %zmm29, %zmm29, %zmm29
    vgatherdps (%rax,%zmm31,4), %zmm29{%k4}
    vmovaps %zmm29, %zmm27{%k6}
2:

.if \neg
    vxorps  %zmm21, %zmm26, %\out_a   # apply negative Gaussian sign
    vxorps  %zmm21, %zmm27, %\out_b
.else
    vmovaps %zmm26, %\out_a
    vmovaps %zmm27, %\out_b
.endif

    vmovntps %\out_a, 0(%r15)
    vmovntps %\out_b, 64(%r15)
    add      $128, %r15
    add      $128, %rdi
.endm

# Debug mirror for one two-zmm Gaussian conversion step.
# r11 points to gauss_debug_step_t. Offsets match sobol.h.
.macro GAUSS_STORE_PAIR_DEBUG in_a, in_b, out_a, out_b
    vmovdqu32 %\in_a,   0(%r11)       # sobol_raw[0..15]
    vmovdqu32 %\in_b,  64(%r11)       # sobol_raw[16..31]

    vpsrld  $9, %\in_a, %\in_a
    vpsrld  $9, %\in_b, %\in_b
    vmovdqu32 %\in_a, 128(%r11)       # sobol_shifted
    vmovdqu32 %\in_b, 192(%r11)

    vmovdqa32   0(%r10,%rdi), %zmm24
    vmovdqa32  64(%r10,%rdi), %zmm25
    vmovdqu32 %zmm24, 512(%r11)       # fold_mask
    vmovdqu32 %zmm25, 576(%r11)

    vpsubd  %zmm23, %\in_a, %\in_a
    vpsubd  %zmm23, %\in_b, %\in_b
    vpxord  %zmm24, %\in_a, %\in_a
    vpxord  %zmm25, %\in_b, %\in_b
    vpsubd  %zmm24, %\in_a, %\in_a
    vpsubd  %zmm25, %\in_b, %\in_b
    vmovdqu32 %\in_a, 256(%r11)       # folded_delta
    vmovdqu32 %\in_b, 320(%r11)

    vpandd  %zmm22, %\in_a, %\in_a
    vpandd  %zmm22, %\in_b, %\in_b
    vmovdqu32 %\in_a, 384(%r11)       # folded_local
    vmovdqu32 %\in_b, 448(%r11)

    vpslld  $12, %\in_a, %\in_a
    vpslld  $12, %\in_b, %\in_b
    vpord   %zmm0, %\in_a, %\in_a
    vpord   %zmm0, %\in_b, %\in_b
    vmovups %\in_a, 640(%r11)         # s
    vmovups %\in_b, 704(%r11)

    vmovaps       0(%r9,%rdi), %zmm26
    vmovaps      64(%r9,%rdi), %zmm27
    vmovups %zmm26, 768(%r11)         # c2
    vmovups %zmm27, 832(%r11)

    vmovaps       0(%r8,%rdi), %zmm28
    vmovaps      64(%r8,%rdi), %zmm29
    vmovups %zmm28, 896(%r11)         # c1
    vmovups %zmm29, 960(%r11)
    vfmadd213ps %zmm28, %\in_a, %zmm26
    vfmadd213ps %zmm29, %\in_b, %zmm27

    vpslld  $31, %zmm24, %zmm24
    vpslld  $31, %zmm25, %zmm25

    vmovaps       0(%rcx,%rdi), %zmm28
    vmovaps      64(%rcx,%rdi), %zmm29
    vmovups %zmm28, 1024(%r11)        # c0
    vmovups %zmm29, 1088(%r11)
    vfmadd213ps %zmm28, %\in_a, %zmm26
    vfmadd213ps %zmm29, %\in_b, %zmm27

    vxorps  %zmm24, %zmm26, %\out_a
    vxorps  %zmm25, %zmm27, %\out_b
    vmovups %\out_a, 1152(%r11)       # out
    vmovups %\out_b, 1216(%r11)

    vmovntps %\out_a, 0(%r15)
    vmovntps %\out_b, 64(%r15)
.endm

generate_sobol_sequence_debug:
    pushq   %rbp
    movq    %rsp, %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    movq    %rdi, %r15               # output
    movq    %rsi, %rbx               # direction vectors
    movq    %r9,  %r10               # fold masks
    movq    %r8,  %r9                # c2
    movq    %rcx, %r8                # c1
    movq    %rdx, %rcx               # c0
    movq    16(%rbp), %r11           # gauss_debug_step_t

    movl    $2, %r13d
    xorl    %r12d, %r12d             # block-start Sobol XOR accumulator.

    vbroadcastss bit_mask_14(%rip), %zmm0
    vpbroadcastd gauss_sign_bit(%rip), %zmm21
    vpbroadcastd local_mask_2047(%rip), %zmm22
    vpbroadcastd gauss_center_q(%rip), %zmm23

    vbroadcastss  0(%rbx),  %zmm1
    vbroadcastss  4(%rbx),  %zmm2
    vbroadcastss  8(%rbx),  %zmm3
    vbroadcastss 12(%rbx),  %zmm4
    vbroadcastss 16(%rbx),  %zmm5
    vbroadcastss 20(%rbx),  %zmm6
    vbroadcastss 24(%rbx),  %zmm7
    vbroadcastss 48(%rbx),  %zmm9

    movl    %r13d, %r14d
    shll    $12, %r14d
    movl    %r14d, %eax
    shrl    $1, %eax
    xorl    %eax, %r14d
    jz      debug_set_bit13

debug_xor_accumulator:
    tzcntl  %r14d, %eax
    movl    %eax, %edx
    xorl    (%rbx, %rdx, 4), %r12d
    blsr    %r14d, %r14d
    jnz     debug_xor_accumulator

debug_set_bit13:
    movl        %r13d, %eax
    andl        $1, %eax
    negl        %eax
    kmovd       %eax, %k1
    notl        %eax
    kmovd       %eax, %k2

    vmovdqu32 128(%rbx), %zmm12
    vmovdqu32 192(%rbx), %zmm13

    vmovd          %r12d, %xmm8
    vpbroadcastd   %xmm8, %zmm8

    vpxord %zmm8, %zmm12, %zmm12
    vpxord %zmm8, %zmm13, %zmm13

    vpxord %zmm9, %zmm12, %zmm12{%k1}
    vpxord %zmm9, %zmm13, %zmm13{%k2}

    xorq    %rdi, %rdi
    GAUSS_STORE_PAIR_DEBUG zmm12, zmm13, zmm12, zmm13

    xorl %eax, %eax
    vzeroupper
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    leave
    ret

generate_sobol_sequence:
    pushq   %rbp
    movq    %rsp,   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    subq      $8,   %rsp 
    movq    %rdi,   %r15
    movq    104(%rbp), %rax
    movq    %rax, -48(%rbp)          # first-block patch-table cursor

    leaq    special_x_zmm_offsets(%rip), %r11

    xorl    %r13d, %r13d          # Start at the true Sobol prefix.

    movq    %rdx,   %rbx          # %rbx = vectors base pointer (from 3rd argument, %rdx)
    xorl    %r10d,  %r10d          # r10d = block-start Sobol XOR accumulator.

    vbroadcastss bit_mask_14(%rip), %zmm0
    vpbroadcastd gauss_sign_bit(%rip), %zmm21
    vpbroadcastd local_mask_2047(%rip), %zmm22
    vpbroadcastd gauss_center_q(%rip), %zmm23

    vbroadcastss  0(%rbx),  %zmm1
    vbroadcastss  4(%rbx),  %zmm2
    vbroadcastss  8(%rbx),  %zmm3
    vbroadcastss 12(%rbx),  %zmm4
    vbroadcastss 16(%rbx),  %zmm5
    vbroadcastss 20(%rbx),  %zmm6
    vbroadcastss 24(%rbx),  %zmm7

    vbroadcastss 48(%rbx), %zmm9

    # Build the Sobol block-start xor accumulator from Gray(chunk * 4096).
    # The scalar loop runs once per 4096-value internal chunk, not per value.
    movl    %r13d,  %r14d
    shll      $12,  %r14d
    movl    %r14d,  %eax
    shrl       $1,  %eax
    xorl     %eax,  %r14d
    jz set_bit13

xor_accumulator:
    # XOR direction numbers for the set bits in Gray(chunk_index * 4096).
    tzcntl  %r14d, %eax
    movl    %eax, %edx
    xorl    (%rbx, %rdx, 4), %r10d
    blsr    %r14d, %r14d
    jnz     xor_accumulator

set_bit13:
    # Direction bit 13 flips every 4096-value internal chunk. Using masks here
    # keeps the per-lane initial vectors branchless.
    movl        %r13d, %eax
    andl        $1,    %eax
    negl        %eax
    kmovd       %eax, %k1
    notl        %eax
    kmovd       %eax, %k2

    vmovdqu32 128(%rbx), %zmm12
    vmovdqu32 192(%rbx), %zmm13

    vmovd          %r10d, %xmm8
    vpbroadcastd   %xmm8, %zmm8

    vpxord %zmm8, %zmm12, %zmm12
    vpxord %zmm8, %zmm13, %zmm13

    vpxord %zmm9, %zmm12, %zmm12{%k1}
    vpxord %zmm9, %zmm13, %zmm13{%k2}

    movq    16(%rbp), %r10           # r10 becomes fold-mask table base for debug-era arguments.
    xorq    %rdi, %rdi               # 4096-entry hot schedule: reset every internal chunk.

    # Chunks 0 and 1 are the true Sobol prefix and need fixed precomputed zmm
    # patches. Chunks 2+ are the repeating steady-state schedule.
    cmpl    $2, %r13d
    jb      first_block_dispatch

    testl   $1, %r13d
    jnz     regular_loop_swap_start
    jmp     regular_loop_start

first_block_dispatch:
    testl   $1, %r13d
    jnz     first_block_loop_swap_start
    jmp     first_block_loop_start

regular_loop_start:
    movl    $15, %r12d 
loop:

    # The 8 store-pair body advances through the Sobol direction-vector pattern.
    # r11 holds the scalar direction offset needed by store-pair #8.
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    cmpl $15, %r12d
    je .Lloop_store1_tail
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    jmp .Lloop_store1_done
.Lloop_store1_tail:
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13, 1
.Lloop_store1_done:

    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15

    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    cmpl $10, %r12d
    je .Lloop_store3_tail
    cmpl $2, %r12d
    je .Lloop_store3_tail
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    jmp .Lloop_store3_done
.Lloop_store3_tail:
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13
.Lloop_store3_done:

    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1

    # 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1

    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    cmpl $5, %r12d
    je .Lloop_store6_tail
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    jmp .Lloop_store6_done
.Lloop_store6_tail:
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15
.Lloop_store6_done:

    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13

    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13

    cmpl $8, %r12d
    je .Lloop_store8_tail
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    jmp .Lloop_store8_done
.Lloop_store8_tail:
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 1
.Lloop_store8_done:


    addq    $4, %r11            # Advance table pointer
    decl    %r12d               # Decrement loop counter IN %r12d


    jnz     loop
    # Reset the special direction offset stream for the tail stores.
    leaq special_x_zmm_offsets(%rip), %r11

    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1

    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15

    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13

    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1

    # 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1

    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15

    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13

    # 8
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 1

    jmp next_chunk

regular_loop_swap_start:
    movl    $15, %r12d
loop_swap:

    # Odd internal chunks reuse the same 4096-entry schedule, but the matching
    # coefficients/masks are in the opposite zmm half of each 32-float pair.
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    cmpl $15, %r12d
    je .Lloop_swap_store1_tail
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    jmp .Lloop_swap_store1_done
.Lloop_swap_store1_tail:
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13, 1, 1
.Lloop_swap_store1_done:

    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1

    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    cmpl $10, %r12d
    je .Lloop_swap_store3_tail
    cmpl $2, %r12d
    je .Lloop_swap_store3_tail
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    jmp .Lloop_swap_store3_done
.Lloop_swap_store3_tail:
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13, 0, 1
.Lloop_swap_store3_done:

    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1

    # 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1

    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    cmpl $5, %r12d
    je .Lloop_swap_store6_tail
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    jmp .Lloop_swap_store6_done
.Lloop_swap_store6_tail:
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 0, 1
.Lloop_swap_store6_done:

    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1

    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13

    cmpl $8, %r12d
    je .Lloop_swap_store8_tail
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    jmp .Lloop_swap_store8_done
.Lloop_swap_store8_tail:
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 1, 1
.Lloop_swap_store8_done:

    addq    $4, %r11
    decl    %r12d
    jnz     loop_swap
    leaq special_x_zmm_offsets(%rip), %r11

    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1

    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 0, 1

    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1

    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1

    # 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1

    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1

    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1

    # 8
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 1, 1

next_chunk:

    cmpl    %esi, %r13d
    jae     done
    incl    %r13d                         # Increment iteration counter n

    xorl    %r10d, %r10d

    movl    %r13d,  %r14d                   # r14 = n
    shll      $12,  %r14d                    # r14 = n * 4096

    movl    %r14d,  %eax
    shrl       $1,  %eax
    xorl     %eax,  %r14d                  # r14 = Gray(n * 4096)

    vbroadcastss 48(%rbx), %zmm9

    jnz     xor_accumulator  # If NOT ZERO, jump to the scalar loop

first_block_loop_start:
    # First public 8192 values. Patch store-pair #1, #4, #5, and #8 in each
    # 8-pair body; those are exactly the zmm vector positions with range
    # mismatches versus the steady-state coefficient schedule.
    movl    $15, %r12d
first_loop:

    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    cmpl $15, %r12d
    je .Lfirst_store1_tail
    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm13, zmm12, zmm13, 1
    jmp .Lfirst_store1_done
.Lfirst_store1_tail:
    GAUSS_STORE_PAIR_PATCH_A_TAIL_B zmm13, zmm12, zmm13, 1
.Lfirst_store1_done:

    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15

    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    cmpl $10, %r12d
    je .Lfirst_store3_tail
    cmpl $2, %r12d
    je .Lfirst_store3_tail
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    jmp .Lfirst_store3_done
.Lfirst_store3_tail:
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13
.Lfirst_store3_done:

    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm15, zmm14, zmm15, 1

    # 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm13, zmm12, zmm13, 1

    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    cmpl $5, %r12d
    je .Lfirst_store6_tail
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    jmp .Lfirst_store6_done
.Lfirst_store6_tail:
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15
.Lfirst_store6_done:

    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13

    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13

    cmpl $8, %r12d
    je .Lfirst_store8_tail
    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm15, zmm14, zmm15, 1
    jmp .Lfirst_store8_done
.Lfirst_store8_tail:
    GAUSS_STORE_PAIR_PATCH_A_TAIL_B zmm15, zmm14, zmm15, 1
.Lfirst_store8_done:

    addq    $4, %r11
    decl    %r12d
    jnz     first_loop
    leaq special_x_zmm_offsets(%rip), %r11

    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm13, zmm12, zmm13, 1

    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15

    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13

    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm15, zmm14, zmm15, 1

    # 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm13, zmm12, zmm13, 1

    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15

    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13

    # 8
    GAUSS_STORE_PAIR_PATCH_A_TAIL_B zmm15, zmm14, zmm15, 1

    jmp next_chunk

first_block_loop_swap_start:
    # First public block, odd internal chunk. Patch positions are identical to
    # first_block_loop_start, but coefficient/mask halves are swapped.
    movl    $15, %r12d
first_loop_swap:

    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    cmpl $15, %r12d
    je .Lfirst_swap_store1_tail
    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm13, zmm12, zmm13, 1, 1
    jmp .Lfirst_swap_store1_done
.Lfirst_swap_store1_tail:
    GAUSS_STORE_PAIR_PATCH_A_TAIL_B zmm13, zmm12, zmm13, 1, 1
.Lfirst_swap_store1_done:

    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1

    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    cmpl $10, %r12d
    je .Lfirst_swap_store3_tail
    cmpl $2, %r12d
    je .Lfirst_swap_store3_tail
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    jmp .Lfirst_swap_store3_done
.Lfirst_swap_store3_tail:
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13, 0, 1
.Lfirst_swap_store3_done:

    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm15, zmm14, zmm15, 1, 1

    # 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm13, zmm12, zmm13, 1, 1

    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    cmpl $5, %r12d
    je .Lfirst_swap_store6_tail
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    jmp .Lfirst_swap_store6_done
.Lfirst_swap_store6_tail:
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 0, 1
.Lfirst_swap_store6_done:

    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1

    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13

    cmpl $8, %r12d
    je .Lfirst_swap_store8_tail
    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm15, zmm14, zmm15, 1, 1
    jmp .Lfirst_swap_store8_done
.Lfirst_swap_store8_tail:
    GAUSS_STORE_PAIR_PATCH_A_TAIL_B zmm15, zmm14, zmm15, 1, 1
.Lfirst_swap_store8_done:

    addq    $4, %r11
    decl    %r12d
    jnz     first_loop_swap
    leaq special_x_zmm_offsets(%rip), %r11

    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm13, zmm12, zmm13, 1, 1

    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 0, 1

    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1

    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm15, zmm14, zmm15, 1, 1

    # 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_PATCH_A_NORMAL_B zmm13, zmm12, zmm13, 1, 1

    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13

    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1

    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15

    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1

    # 8
    GAUSS_STORE_PAIR_PATCH_A_TAIL_B zmm15, zmm14, zmm15, 1, 1

    jmp next_chunk


done:
    xorl %eax, %eax # Return value 0 for success
    
    vzeroupper
    addq      $8,   %rsp 
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    leave
    ret
