# Complete prepared hot path plus compact resident-mask block loop.
#
# Vector operands are ZMM numbers 0..31. GPR operands are names without '%'.
# Three-register vpermd (AT&T): dest, index, src
#   vpermd  scratch, resident_mask, out
# Resident masks are never written.

.equ DIM_PATTERNS_OFFSET, 576
.equ RES_DELTA_OFF, 128
.equ RES_START_PKT, 160
.equ RES_DELTA_PKT, 224
.equ AFF_BASE_CTRL_OFF, 320
.equ AFF_HALF_DELTA_OFF, 384

# DIM_FROM_D1_FRAG out_a, out_b, scratch_a, scratch_b, \
#                  d1_base, packet, map, g0, g1, g2, g3
#
# Current generic control-load baseline. 14 insns, 4×movzbq.
# Clobbers: zmm out/scratch and g0..g3. Preserves d1_base, packet, map.
.macro DIM_FROM_D1_FRAG out_a, out_b, scratch_a, scratch_b, d1_base, packet, map, g0, g1, g2, g3
	movzbq	(%\map, %\packet, 4), %\g1
	movzbq	1(%\map, %\packet, 4), %\g2
	movzbq	2(%\map, %\packet, 4), %\g3
	movzbq	3(%\map, %\packet, 4), %\g0
	shlq	$6, %\g1
	shlq	$6, %\g2
	shlq	$6, %\g3
	shlq	$6, %\g0
	vmovdqa32	(%\d1_base, %\g1), %zmm\scratch_a
	vmovdqa32	(%\d1_base, %\g2), %zmm\scratch_b
	vmovdqa32	DIM_PATTERNS_OFFSET(%\map, %\g3), %zmm\out_a
	vmovdqa32	DIM_PATTERNS_OFFSET(%\map, %\g0), %zmm\out_b
	vpermd	%zmm\scratch_a, %zmm\out_a, %zmm\out_a
	vpermd	%zmm\scratch_b, %zmm\out_b, %zmm\out_b
.endm

# Packed single-dword select[p] instead of four movzbq loads.
# Same two-output contract. g0 holds the packed dword while unpacking.
.macro DIM_FROM_D1_FRAG_PACKED out_a, out_b, scratch_a, scratch_b, d1_base, packet, map, g0, g1, g2, g3
	movl	(%\map, %\packet, 4), %\g0\()d
	movzbl	%\g0\()b, %\g1\()d
	shrl	$8, %\g0\()d
	movzbl	%\g0\()b, %\g2\()d
	shrl	$8, %\g0\()d
	movzbl	%\g0\()b, %\g3\()d
	shrl	$8, %\g0\()d
	movzbl	%\g0\()b, %\g0\()d
	shlq	$6, %\g1
	shlq	$6, %\g2
	shlq	$6, %\g3
	shlq	$6, %\g0
	vmovdqa32	(%\d1_base, %\g1), %zmm\scratch_a
	vmovdqa32	(%\d1_base, %\g2), %zmm\scratch_b
	vmovdqa32	DIM_PATTERNS_OFFSET(%\map, %\g3), %zmm\out_a
	vmovdqa32	DIM_PATTERNS_OFFSET(%\map, %\g0), %zmm\out_b
	vpermd	%zmm\scratch_a, %zmm\out_a, %zmm\out_a
	vpermd	%zmm\scratch_b, %zmm\out_b, %zmm\out_b
.endm

# EXPERIMENTAL sequential affine-control fragment.
#
# DIM_FROM_D1_FRAG_AFFINE out_a, out_b, scratch_a, scratch_b, \
#                         base_ctrl, half_delta, d1_base, packet, map, g0, g1
#
# Vector operands are ZMM numbers 0..31. GPR operands are names without '%'
# (g0/g1 must be r8..r15 so the %g0d forms exist).
#
# map -> dim_affine_map_t: sel2[packet] = {line_a, c_a} at map + 2*packet.
# base_ctrl / half_delta must already hold base_control and broadcast(delta)
# (offsets AFF_BASE_CTRL_OFF / AFF_HALF_DELTA_OFF in the map) and are NOT
# written; they stay valid for the complete 128-packet block.
#
# 11 insns: 2 scalar selector loads, 1 shift, 1 XOR, 2 D1 ZMM loads,
# 1 vpbroadcastd, 2 vpxord, 2 vpermd. 0 control-table ZMM loads.
# Clobbers: zmm out_a/out_b/scratch_a/scratch_b and g0, g1.
# Preserves: base_ctrl, half_delta, d1_base, packet, map.
.macro DIM_FROM_D1_FRAG_AFFINE out_a, out_b, scratch_a, scratch_b, base_ctrl, half_delta, d1_base, packet, map, g0, g1
	movzbl	(%\map, %\packet, 2), %\g0\()d
	movzbl	1(%\map, %\packet, 2), %\g1\()d
	shll	$6, %\g0\()d
	vmovdqa32	(%\d1_base, %\g0), %zmm\scratch_a
	xorl	$64, %\g0\()d
	vmovdqa32	(%\d1_base, %\g0), %zmm\scratch_b
	vpbroadcastd	%\g1\()d, %zmm\out_a
	vpxord	%zmm\base_ctrl, %zmm\out_a, %zmm\out_a
	vpxord	%zmm\half_delta, %zmm\out_a, %zmm\out_b
	vpermd	%zmm\scratch_a, %zmm\out_a, %zmm\out_a
	vpermd	%zmm\scratch_b, %zmm\out_b, %zmm\out_b
.endm

# Packed two-byte selector variation: one movzwl instead of two movzbl.
# 12 insns; same contract as DIM_FROM_D1_FRAG_AFFINE.
.macro DIM_FROM_D1_FRAG_AFFINE_W out_a, out_b, scratch_a, scratch_b, base_ctrl, half_delta, d1_base, packet, map, g0, g1
	movzwl	(%\map, %\packet, 2), %\g0\()d
	movzbl	%\g0\()b, %\g1\()d
	shrl	$8, %\g0\()d
	shll	$6, %\g1\()d
	vmovdqa32	(%\d1_base, %\g1), %zmm\scratch_a
	xorl	$64, %\g1\()d
	vmovdqa32	(%\d1_base, %\g1), %zmm\scratch_b
	vpbroadcastd	%\g0\()d, %zmm\out_a
	vpxord	%zmm\base_ctrl, %zmm\out_a, %zmm\out_a
	vpxord	%zmm\half_delta, %zmm\out_a, %zmm\out_b
	vpermd	%zmm\scratch_a, %zmm\out_a, %zmm\out_a
	vpermd	%zmm\scratch_b, %zmm\out_b, %zmm\out_b
.endm

# One Gray-coded inner loop over ninner packets that share (mask_a, mask_b).
# lineoff in r8, cnt in r9, tzcnt scratch in r10.
# rsi = resident_sched_t*.
# store=1: write two ZMMs to out_base(r12) at packet r11. Also xor pkt by delta.
# Hot path has no per-packet accumulate (same as the baseline block loop).
.macro DIM_RESIDENT_INNER ninner, mask_a, mask_b, scratch_a, scratch_b, out_a, out_b, store
	xorq	%r9, %r9
1:
	vmovdqa32	(%rdi, %r8), %zmm\scratch_a
	xorq	$64, %r8
	vmovdqa32	(%rdi, %r8), %zmm\scratch_b
	xorq	$64, %r8
	vpermd	%zmm\scratch_a, %zmm\mask_a, %zmm\out_a
	vpermd	%zmm\scratch_b, %zmm\mask_b, %zmm\out_b
	.if \store
	movq	%r11, %r13
	shlq	$7, %r13
	vmovdqa32	%zmm\out_a, (%r12, %r13)
	vmovdqa32	%zmm\out_b, 64(%r12, %r13)
	.endif
	addq	$1, %r9
	cmpq	$\ninner, %r9
	je	2f
	tzcntq	%r9, %r10
	xorq	RES_DELTA_OFF(%rsi, %r10, 8), %r8
	.if \store
	movl	RES_DELTA_PKT(%rsi, %r10, 4), %r14d
	xorl	%r14d, %r11d
	.endif
	jmp	1b
2:
.endm

.macro DIM_RESIDENT_GROUP pair, ninner, mask_a, mask_b, scratch_a, scratch_b, out_a, out_b, store
	movq	\pair * 8(%rsi), %r8
	.if \store
	movl	RES_START_PKT + \pair * 4(%rsi), %r11d
	.endif
	DIM_RESIDENT_INNER \ninner, \mask_a, \mask_b, \scratch_a, \scratch_b, \out_a, \out_b, \store
.endm

.macro DIM_RESIDENT8_BODY store
	vmovdqa32	DIM_PATTERNS_OFFSET(%rdx), %zmm16
	vmovdqa32	DIM_PATTERNS_OFFSET + 64(%rdx), %zmm17
	vmovdqa32	DIM_PATTERNS_OFFSET + 128(%rdx), %zmm18
	vmovdqa32	DIM_PATTERNS_OFFSET + 192(%rdx), %zmm19
	vmovdqa32	DIM_PATTERNS_OFFSET + 256(%rdx), %zmm20
	vmovdqa32	DIM_PATTERNS_OFFSET + 320(%rdx), %zmm21
	vmovdqa32	DIM_PATTERNS_OFFSET + 384(%rdx), %zmm22
	vmovdqa32	DIM_PATTERNS_OFFSET + 448(%rdx), %zmm23
	DIM_RESIDENT_GROUP 0, 16, 16, 17, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 1, 16, 18, 19, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 2, 16, 20, 21, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 3, 16, 22, 23, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 4, 16, 17, 16, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 5, 16, 19, 18, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 6, 16, 21, 20, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 7, 16, 23, 22, 2, 3, 0, 1, \store
.endm

.macro DIM_RESIDENT16_BODY store
	vmovdqa32	DIM_PATTERNS_OFFSET(%rdx), %zmm16
	vmovdqa32	DIM_PATTERNS_OFFSET + 64(%rdx), %zmm17
	vmovdqa32	DIM_PATTERNS_OFFSET + 128(%rdx), %zmm18
	vmovdqa32	DIM_PATTERNS_OFFSET + 192(%rdx), %zmm19
	vmovdqa32	DIM_PATTERNS_OFFSET + 256(%rdx), %zmm20
	vmovdqa32	DIM_PATTERNS_OFFSET + 320(%rdx), %zmm21
	vmovdqa32	DIM_PATTERNS_OFFSET + 384(%rdx), %zmm22
	vmovdqa32	DIM_PATTERNS_OFFSET + 448(%rdx), %zmm23
	vmovdqa32	DIM_PATTERNS_OFFSET + 512(%rdx), %zmm24
	vmovdqa32	DIM_PATTERNS_OFFSET + 576(%rdx), %zmm25
	vmovdqa32	DIM_PATTERNS_OFFSET + 640(%rdx), %zmm26
	vmovdqa32	DIM_PATTERNS_OFFSET + 704(%rdx), %zmm27
	vmovdqa32	DIM_PATTERNS_OFFSET + 768(%rdx), %zmm28
	vmovdqa32	DIM_PATTERNS_OFFSET + 832(%rdx), %zmm29
	vmovdqa32	DIM_PATTERNS_OFFSET + 896(%rdx), %zmm30
	vmovdqa32	DIM_PATTERNS_OFFSET + 960(%rdx), %zmm31
	DIM_RESIDENT_GROUP 0, 8, 16, 17, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 1, 8, 18, 19, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 2, 8, 20, 21, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 3, 8, 22, 23, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 4, 8, 24, 25, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 5, 8, 26, 27, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 6, 8, 28, 29, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 7, 8, 30, 31, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 8, 8, 17, 16, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 9, 8, 19, 18, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 10, 8, 21, 20, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 11, 8, 23, 22, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 12, 8, 25, 24, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 13, 8, 27, 26, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 14, 8, 29, 28, 2, 3, 0, 1, \store
	DIM_RESIDENT_GROUP 15, 8, 31, 30, 2, 3, 0, 1, \store
.endm

.macro DIM_ACC_TO_EAX
	vextracti64x4	$1, %zmm4, %ymm5
	vpxord	%ymm5, %ymm4, %ymm4
	vextracti128	$1, %ymm4, %xmm5
	vpxord	%xmm5, %xmm4, %xmm4
	vpshufd	$0x4e, %xmm4, %xmm5
	vpxord	%xmm5, %xmm4, %xmm4
	vpshufd	$0xb1, %xmm4, %xmm5
	vpxord	%xmm5, %xmm4, %xmm4
	vmovd	%xmm4, %eax
.endm
