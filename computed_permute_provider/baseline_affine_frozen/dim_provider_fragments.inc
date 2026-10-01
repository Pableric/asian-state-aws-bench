# Isolated dest-order dim-from-D1 packet fragments. AT&T / GAS.
# Vector operands are ZMM numbers 0..31. GPR operands are names without '%'.
# vpermd dest, index, src  (AT&T: dest, control, data)
#
# No calls, stores, gathers, or stack operands in these macros.

.equ DIM_PROVIDER_PATTERNS_OFFSET, 576
.equ DIM_PROVIDER_AFF_BASE_OFF, 320
.equ DIM_PROVIDER_AFF_DELTA_OFF, 384

# DIM_PROVIDER_PACKET_GENERIC out_a, out_b, scratch_a, scratch_b, \
#     d1_base, packet, map, g0, g1, g2, g3
#
# Clobbers: out_*, scratch_*, g0..g3.
# Preserves: d1_base, packet, map.
# Aliases: out/scratch must be distinct; g0..g3 distinct and not packet/map/d1.
.macro DIM_PROVIDER_PACKET_GENERIC out_a, out_b, scratch_a, scratch_b, d1_base, packet, map, g0, g1, g2, g3
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
	vmovdqa32	DIM_PROVIDER_PATTERNS_OFFSET(%\map, %\g3), %zmm\out_a
	vmovdqa32	DIM_PROVIDER_PATTERNS_OFFSET(%\map, %\g0), %zmm\out_b
	vpermd	%zmm\scratch_a, %zmm\out_a, %zmm\out_a
	vpermd	%zmm\scratch_b, %zmm\out_b, %zmm\out_b
.endm

# DIM_PROVIDER_PRELOAD_AFFINE base_ctrl, half_delta, map
# Clobbers: base_ctrl, half_delta. Preserves map. Does not write map.
.macro DIM_PROVIDER_PRELOAD_AFFINE base_ctrl, half_delta, map
	vmovdqa32	DIM_PROVIDER_AFF_BASE_OFF(%\map), %zmm\base_ctrl
	vmovdqa32	DIM_PROVIDER_AFF_DELTA_OFF(%\map), %zmm\half_delta
.endm

# DIM_PROVIDER_PACKET_AFFINE out_a, out_b, scratch_a, scratch_b, \
#     base_ctrl, half_delta, d1_base, packet, map, g0, g1
#
# Clobbers: out_*, scratch_*, g0, g1.
# Preserves: base_ctrl, half_delta, d1_base, packet, map.
.macro DIM_PROVIDER_PACKET_AFFINE out_a, out_b, scratch_a, scratch_b, base_ctrl, half_delta, d1_base, packet, map, g0, g1
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

# DIM_PROVIDER_PRELOAD_RES2XOR mask_a, mask_b, live_c, map
# mask_a = base, mask_b = base XOR delta, live_c = 0.
# Clobbers: mask_a, mask_b, live_c. Preserves map.
.macro DIM_PROVIDER_PRELOAD_RES2XOR mask_a, mask_b, live_c, map
	vmovdqa32	DIM_PROVIDER_AFF_BASE_OFF(%\map), %zmm\mask_a
	vmovdqa32	DIM_PROVIDER_AFF_DELTA_OFF(%\map), %zmm\mask_b
	vpxord	%zmm\mask_b, %zmm\mask_a, %zmm\mask_b
	xorl	%\live_c\()d, %\live_c\()d
.endm

# DIM_PROVIDER_PACKET_RES2XOR out_a, out_b, scratch_a, scratch_b, \
#     mask_a, mask_b, live_c, d1_base, packet, map, g0, g1
#
# Dest-order packet p. Updates live masks when sel2[p].c_a != live_c.
# Clobbers: out_*, scratch_*, g0, g1, mask_* (in place), live_c.
# Preserves: d1_base, packet, map.
# Sequential p=0..127. Mask XOR when sel2[p].c_a differs from live_c.
.macro DIM_PROVIDER_PACKET_RES2XOR out_a, out_b, scratch_a, scratch_b, mask_a, mask_b, live_c, d1_base, packet, map, g0, g1
	movzbl	1(%\map, %\packet, 2), %\g1\()d
	cmpl	%\live_c\()d, %\g1\()d
	je	.Ldim_res2_\@_same
	xorl	%\live_c\()d, %\g1\()d
	vpbroadcastd	%\g1\()d, %zmm\scratch_a
	vpxord	%zmm\scratch_a, %zmm\mask_a, %zmm\mask_a
	vpxord	%zmm\scratch_a, %zmm\mask_b, %zmm\mask_b
	xorl	%\g1\()d, %\live_c\()d
.Ldim_res2_\@_same:
	movzbl	(%\map, %\packet, 2), %\g0\()d
	shll	$6, %\g0\()d
	vmovdqa32	(%\d1_base, %\g0), %zmm\scratch_a
	xorl	$64, %\g0\()d
	vmovdqa32	(%\d1_base, %\g0), %zmm\scratch_b
	vpermd	%zmm\scratch_a, %zmm\mask_a, %zmm\out_a
	vpermd	%zmm\scratch_b, %zmm\mask_b, %zmm\out_b
.endm
