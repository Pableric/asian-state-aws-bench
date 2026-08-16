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
# === GPR REGISTER MAP ======================================================
# rdi destination memory address on entry; coefficient byte offset inside GAUSS_STORE_PAIR.
# rsi last 4096-value internal chunk index to generate, using esi in the loop.
# rdx base address direction numbers array
# r8  gauss_linear_c0 base, kept live for the normal Gaussian path.
# r9  gauss_linear_c1 base, kept live for the normal Gaussian path.
# rax/eax this is the offset of special_x_zmm_offsets
# rbx copy the values of rdx, this is to make it a calle-saved register
# r10 scalar mask accumulator before set_bit13; fold-mask base inside GAUSS_STORE_PAIR.
# r11 pointer to special_x_zmm_offsets
# r12 main loop index.
# r13 current 4096-value internal chunk index.
# r14 for greycode calculation
# r15 byte offset inside the current 256-entry exp coefficient schedule.
# rbp, rsp: Stack frame; rbp is used for stack-passed coefficient arguments.
#
# === ZMM REGISTER MAP ======================================================
# zmm0         raw-float bit mask 0x3f800000, used for mantissa injection.
# zmm1-zmm3    fixed Sobol direction increments for store-pair steps 1/2/4.
# zmm7         dynamic Sobol direction increment for store-pair step 8.
# zmm8         broadcast block-start Sobol xor state.
# zmm9         direction bit 13, applied with k1/k2 at chunk setup.
# zmm10        scalar coefficient broadcast scratch for PRICE_ACCUM_Z_GLOBAL.
# zmm11        endpoint correction scratch during accumulator initialization.
# zmm12-zmm15  Sobol integer state ping-pong registers.
# zmm16        running float payoff accumulator.
# zmm17        alpha_z = sigma * sqrt(T).
# zmm18        signed payoff scale.
# zmm19        payoff beta.
# zmm20        zero.
# zmm21        Gaussian sign-bit mask.
# zmm22        local tail index mask 2047.
# zmm23        folded tail center index.
# zmm24-zmm25  intentionally free.
# zmm26-zmm27  generated Gaussian z values for A/B halves.
# zmm28-zmm29  pricing and range-2047 mask/gather scratch.
# zmm30-zmm31  preserved tail local indices for range-2047 gathers.
#
# k1-k2        chunk-parity masks for direction bit 13.
# k3-k4        tail range-2047 active-lane masks.
# k5-k6        copies of k3/k4 consumed by masked moves.
# k7           one-shot lane-0 first Sobol endpoint mask.
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

exp_p0:
    .float 1.00000000361
exp_p1:
    .float 0.999999559932
exp_p2:
    .float 0.499999873009
exp_p3:
    .float 0.166670788605
exp_p4:
    .float 0.0416673696717
exp_p5:
    .float 0.00832308095835
exp_p6:
    .float 0.0013875434074
exp_p7:
    .float 0.0002077216867
exp_p8:
    .float 2.58406812172e-05

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
.global price_european_sequence_center_shared
.type price_european_sequence_center_shared, @function

# Pricing parameter appended after the original Gaussian generator arguments:
# 112(%rbp)   params[3]:
#             params[0] = alpha_z = sigma * sqrt(T)
#             params[1] = payoff scale:
#                         call:  df * S0 * exp(mu)
#                         put : -df * S0 * exp(mu)
#             params[2] = payoff beta:
#                         call: -df * K
#                         put :  df * K
#             params[3] = first Sobol point payoff correction
#
# PRICE_ACCUM_Z_GLOBAL
#   Input:
#     zreg = zmm register containing Gaussian z values.
#   Uses:
#     zmm16 = running float payoff sum
#     zmm17 = alpha_z = sigma * sqrt(T)
#     zmm18 = payoff scale, already signed for call/put
#     zmm19 = payoff beta
#     zmm20 = zero
#     zmm28 = alpha_z * z scratch
#     zmm29 = exp/payoff scratch
#     zmm10 = scalar polynomial coefficient broadcast scratch
#   Action:
#     x      = alpha_z * z
#     exp_x  = degree-8 baked polynomial approximation to exp(x)
#     payoff = max(scale * exp_x + beta, 0)
#     zmm16 += payoff
#   Needed:
#     Yes for the tail/hard paths until those get their own compact scheduled
#     pricing approximation. It is intentionally no longer used by the normal
#     steady-state Gaussian path.
.macro PRICE_ACCUM_Z_GLOBAL zreg
    vmulps  %zmm17, %\zreg, %zmm28
    vbroadcastss exp_p8(%rip), %zmm29
    vbroadcastss exp_p7(%rip), %zmm10
    vfmadd213ps %zmm10, %zmm28, %zmm29
    vbroadcastss exp_p6(%rip), %zmm10
    vfmadd213ps %zmm10, %zmm28, %zmm29
    vbroadcastss exp_p5(%rip), %zmm10
    vfmadd213ps %zmm10, %zmm28, %zmm29
    vbroadcastss exp_p4(%rip), %zmm10
    vfmadd213ps %zmm10, %zmm28, %zmm29
    vbroadcastss exp_p3(%rip), %zmm10
    vfmadd213ps %zmm10, %zmm28, %zmm29
    vbroadcastss exp_p2(%rip), %zmm10
    vfmadd213ps %zmm10, %zmm28, %zmm29
    vbroadcastss exp_p1(%rip), %zmm10
    vfmadd213ps %zmm10, %zmm28, %zmm29
    vbroadcastss exp_p0(%rip), %zmm10
    vfmadd213ps %zmm10, %zmm28, %zmm29
    vfmadd213ps %zmm19, %zmm18, %zmm29
    vmaxps  %zmm20, %zmm29, %zmm29
    vaddps  %zmm29, %zmm16, %zmm16
.endm

# PRICE_ACCUM_Z_SCHED
#   Input:
#     zreg = zmm register containing Gaussian z values.
#     r14  = current contract-specific exp schedule base.
#     r15  = byte offset for this zmm slot inside c2[256], c1[256], c0[256].
#   Uses:
#     zmm16 = running float payoff sum
#     zmm17 = alpha_z = sigma * sqrt(T)
#     zmm18 = payoff scale, already signed for call/put
#     zmm19 = payoff beta
#     zmm20 = zero
#     zmm28 = alpha_z * z scratch
#     zmm29 = scheduled exp/payoff scratch
#   Action:
#     Computes x = alpha_z * z, streams scheduled c2/c1/c0 coefficients for
#     this deterministic zmm slot, evaluates c0 + c1*x + c2*x*x, applies the
#     discounted affine payoff, and advances the schedule cursor by one zmm.
#   Needed:
#     Yes for the normal path. This is the compact replacement for the old
#     global degree-8 exp polynomial.
.macro PRICE_ACCUM_Z_SCHED zreg
    vmulps        %zmm17, %\zreg, %zmm28
    vbroadcastss     0(%r14,%r15), %zmm29
    vfmadd213ps   1024(%r14,%r15){1to16}, %zmm28, %zmm29
    vfmadd213ps   2048(%r14,%r15){1to16}, %zmm28, %zmm29
    vfmadd213ps   %zmm19, %zmm18, %zmm29
    vmaxps        %zmm20, %zmm29, %zmm29
    vaddps        %zmm29, %zmm16, %zmm16
    addq          $4, %r15
.endm

# GAUSS_PATCH_OUT_A
#   Input:
#     out_a = destination zmm for a precomputed first-block Gaussian vector.
#   Action:
#     Loads the next first-block patch vector. The cursor lives at -48(%rbp).
#     The very first Sobol point is -inf; k7 masks lane 0 to zero so the normal
#     exp polynomial does not see infinity. zmm16 was pre-seeded with the exact
#     endpoint payoff correction.
#   Needed:
#     Yes, while the first public block uses precomputed patch vectors.
.macro GAUSS_PATCH_OUT_A out_a
    movq    -48(%rbp), %rax
    vmovaps 0(%rax), %\out_a
    vmovaps %zmm20, %\out_a{%k7}
    kxord   %k7, %k7, %k7
    addq    $64, %rax
    movq    %rax, -48(%rbp)
.endm

# GAUSS_STORE_PAIR_PATCH_A_NORMAL_B
#   Input:
#     in_b = Sobol integer state for the B half of a 32-value pair.
#     out_a = patched Gaussian z vector for the A half.
#     out_b = output register for the generated B Gaussian vector.
#     swap = odd internal chunk flag; swaps coefficient half selection.
#   Action:
#     A half: load first-block Gaussian patch, then price/accumulate it.
#     B half: run the normal one-FMA Gaussian path, then price/accumulate it.
#   Needed:
#     Yes for first-block pair positions where only A diverges from the steady
#     state range schedule and B can still use normal Gaussian coefficients.
.macro GAUSS_STORE_PAIR_PATCH_A_NORMAL_B in_b, out_a, out_b, neg=0, swap=0
    GAUSS_PATCH_OUT_A \out_a

    vpsrld  $9, %\in_b, %\in_b
    vpord   %zmm0, %\in_b, %\in_b

.if \swap
    vmovaps       0(%r9,%rdi), %zmm27
.else
    vmovaps      64(%r9,%rdi), %zmm27
.endif

.if \swap
    vfmadd213ps   0(%r8,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps  64(%r8,%rdi), %\in_b, %zmm27
.endif

    PRICE_ACCUM_Z_SCHED \out_a
    PRICE_ACCUM_Z_SCHED zmm27
    add      $128, %rdi
.endm

# GAUSS_STORE_PAIR_PATCH_A_TAIL_B
#   Input:
#     Same first-block patch-A shape as GAUSS_STORE_PAIR_PATCH_A_NORMAL_B.
#   Action:
#     A half: load first-block Gaussian patch, then price/accumulate it.
#     B half: run the scheduled Gaussian tail path, including range-2047 LUT
#     override where needed, then price/accumulate it.
#   Needed:
#     Yes for first-block pair positions where A is patched and B is one of
#     the known tail/hard Gaussian slots.
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

    PRICE_ACCUM_Z_GLOBAL \out_a
    addq     $4, %r15
    PRICE_ACCUM_Z_GLOBAL \out_b
    addq     $4, %r15
    add      $128, %rdi
.endm

# GAUSS_STORE_PAIR_NORMAL
#   Input:
#     in_a/in_b = two Sobol integer zmm states for a 32-value pair.
#     swap = odd internal chunk flag; swaps coefficient half selection.
#   Action:
#     Converts both Sobol states to Gaussian z:
#       1. keep top 23 Sobol bits
#       2. mantissa-inject to raw x = 1 + u
#       3. evaluate scheduled signed linear Gaussian coefficient pair
#     Then prices/accumulates both zmm vectors instead of storing them.
#   Needed:
#     Yes. This is the steady-state fast path for most values.
.macro GAUSS_STORE_PAIR_NORMAL in_a, in_b, out_a, out_b, neg=0, swap=0
    vpsrld  $9, %\in_a, %\in_a
    vpsrld  $9, %\in_b, %\in_b
    vpord   %zmm0, %\in_a, %\in_a
    vpord   %zmm0, %\in_b, %\in_b

.if \swap
    vmovaps      64(%r9,%rdi), %zmm26
    vmovaps       0(%r9,%rdi), %zmm27
.else
    vmovaps       0(%r9,%rdi), %zmm26
    vmovaps      64(%r9,%rdi), %zmm27
.endif

.if \swap
    vfmadd213ps  64(%r8,%rdi), %\in_a, %zmm26
    vfmadd213ps   0(%r8,%rdi), %\in_b, %zmm27
.else
    vfmadd213ps   0(%r8,%rdi), %\in_a, %zmm26
    vfmadd213ps  64(%r8,%rdi), %\in_b, %zmm27
.endif

    PRICE_ACCUM_Z_SCHED zmm26
    PRICE_ACCUM_Z_SCHED zmm27
    add      $128, %rdi
.endm

# GAUSS_STORE_PAIR_CENTER_SHARED
#   Experimental center/body path. Uses one shared linear coefficient pair per
#   selected zmm slot. Selection is compile-time scheduled; this macro must only
#   be used for slots emitted by gaussian_center_shared_coeff_values_2048.h.
.macro GAUSS_STORE_PAIR_CENTER_SHARED in_a, in_b, out_a, out_b, neg=0, swap=0
    vpsrld  $9, %\in_a, %\in_a
    vpsrld  $9, %\in_b, %\in_b
    vpord   %zmm0, %\in_a, %\in_a
    vpord   %zmm0, %\in_b, %\in_b

    movq    %rdi, %rdx
    shrq    $4, %rdx                 # full-table byte offset / 16 = shared float byte offset.

    movq    136(%rbp), %rax          # gauss_center_shared_c1 base
.if \swap
    vbroadcastss  4(%rax,%rdx), %zmm26
    vbroadcastss  0(%rax,%rdx), %zmm27
.else
    vbroadcastss  0(%rax,%rdx), %zmm26
    vbroadcastss  4(%rax,%rdx), %zmm27
.endif

    movq    128(%rbp), %rax          # gauss_center_shared_c0 base
.if \swap
    vbroadcastss  4(%rax,%rdx), %zmm24
    vbroadcastss  0(%rax,%rdx), %zmm25
.else
    vbroadcastss  0(%rax,%rdx), %zmm24
    vbroadcastss  4(%rax,%rdx), %zmm25
.endif

    vfmadd213ps %zmm24, %\in_a, %zmm26
    vfmadd213ps %zmm25, %\in_b, %zmm27

    PRICE_ACCUM_Z_SCHED zmm26
    PRICE_ACCUM_Z_SCHED zmm27
    add      $128, %rdi
.endm

# GAUSS_STORE_PAIR_TAIL
#   Input:
#     in_a/in_b = two Sobol integer zmm states for a known hard/tail pair.
#     neg = deterministic sign side for this pair.
#     swap = odd internal chunk flag; swaps coefficient/mask half selection.
#   Action:
#     Builds folded local Gaussian coordinates, evaluates the scheduled
#     degree-5 Gaussian tail polynomial, applies the range-2047 LUT override,
#     restores Gaussian sign, then prices/accumulates both zmm vectors.
#   Needed:
#     Yes until the tail positions are absorbed into a different specialized
#     pricing path. This preserves the current Gaussian correctness.
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

    PRICE_ACCUM_Z_GLOBAL \out_a
    addq     $4, %r15
    PRICE_ACCUM_Z_GLOBAL \out_b
    addq     $4, %r15
    add      $128, %rdi
.endm

price_european_sequence_center_shared:
    pushq   %rbp
    movq    %rsp,   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    subq      $8,   %rsp
    xorq    %r15,   %r15
    movq    104(%rbp), %rax
    movq    %rax, -48(%rbp)          # first-block patch-table cursor

    leaq    special_x_zmm_offsets(%rip), %r11

    movl    $2, %r13d            # Skip the problematic Sobol prefix; chunks 2+ use the steady-state schedule.

    movq    %rdx,   %rbx          # %rbx = vectors base pointer (from 3rd argument, %rdx)
    xorl    %r10d,  %r10d          # r10d = block-start Sobol XOR accumulator.

    vbroadcastss bit_mask_14(%rip), %zmm0
    vpbroadcastd gauss_sign_bit(%rip), %zmm21
    vpbroadcastd local_mask_2047(%rip), %zmm22
    vpbroadcastd gauss_center_q(%rip), %zmm23

    vbroadcastss  0(%rbx),  %zmm1
    vbroadcastss  4(%rbx),  %zmm2
    vbroadcastss  8(%rbx),  %zmm3

    vbroadcastss 48(%rbx), %zmm9
    vxorps  %zmm20, %zmm20, %zmm20
    movq    112(%rbp), %rax
    vbroadcastss 0(%rax), %zmm17
    vbroadcastss 4(%rax), %zmm18
    vbroadcastss 8(%rax), %zmm19
    vxorps  %zmm16, %zmm16, %zmm16
    movq    88(%rbp), %r8
    movq    96(%rbp), %r9

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

    movq    120(%rbp), %r14          # contract-specific regular exp schedule.
    xorq    %rdi, %rdi               # 4096-entry Gaussian coefficient schedule: reset every internal chunk.
    xorq    %r15, %r15               # 256-entry European exp schedule: reset every internal chunk.

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
    # unrolled body r12=15
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15
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
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=14
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=13
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=12
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=11
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=10
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1
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
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=9
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=8
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=7
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=6
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=5
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=4
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=3
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=2
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    # unrolled body r12=1
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    addq    $4, %r11

    leaq special_x_zmm_offsets(%rip), %r11
    # final 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # final 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15
    # final 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13
    # final 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1
    # final 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1
    # final 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15
    # final 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13
    # final 8
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 1
    jmp next_chunk

regular_loop_swap_start:
    # unrolled swap body r12=15
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 0, 1
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
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=14
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=13
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 0, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=12
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=11
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 0, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=10
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1, 1
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
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=9
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 0, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=8
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=7
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 0, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=6
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=5
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=4
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=3
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 0, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=2
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_TAIL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 1, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    # unrolled swap body r12=1
    movslq  (%r11), %rax
    vbroadcastss (%rbx, %rax, 1), %zmm7
    # 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_CENTER_SHARED zmm14, zmm15, zmm14, zmm15, 0, 1
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
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 1, 1
    # 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # 8
    vpxord %zmm7, %zmm14, %zmm12
    vpxord %zmm7, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    addq    $4, %r11

    leaq special_x_zmm_offsets(%rip), %r11
    # final 1
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # final 2
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_TAIL zmm14, zmm15, zmm14, zmm15, 0, 1
    # final 3
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 0, 1
    # final 4
    vpxord %zmm3, %zmm14, %zmm12
    vpxord %zmm3, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 1, 1
    # final 5
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_NORMAL zmm12, zmm13, zmm12, zmm13, 1, 1
    # final 6
    vpxord %zmm2, %zmm14, %zmm12
    vpxord %zmm2, %zmm15, %zmm13
    GAUSS_STORE_PAIR_NORMAL zmm14, zmm15, zmm14, zmm15, 0, 1
    # final 7
    vpxord %zmm1, %zmm12, %zmm14
    vpxord %zmm1, %zmm13, %zmm15
    GAUSS_STORE_PAIR_CENTER_SHARED zmm12, zmm13, zmm12, zmm13, 0, 1
    # final 8
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
    vextractf32x8 $1, %zmm16, %ymm0
    vaddps  %ymm0, %ymm16, %ymm0
    vextractf128 $1, %ymm0, %xmm1
    vaddps  %xmm1, %xmm0, %xmm0
    vhaddps %xmm0, %xmm0, %xmm0
    vhaddps %xmm0, %xmm0, %xmm0
    vcvtss2sd %xmm0, %xmm0, %xmm0

    vzeroupper
    addq      $8,   %rsp
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    leave
    ret
