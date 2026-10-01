# Computed-on-the-fly permutation provider fragments (v1). AT&T / GAS.
#
# ZMM operands are numbers 0..31. Pointer operands (d1, rec) are 64-bit GPR
# names without '%'. State/scratch GPR operands (p, blk, cst, ga, gb, t) are
# used as 32-bit registers via the 'd' suffix, so they must be r8..r15.
# vpermd AT&T operand order: table, index, destination.
#
# No calls, stores, gathers, branches or stack operands in these macros.
# CPP_BLOCK_ADVANCE, CPP_BLOCK_ENTER and CPP_PACKET_NEXT write the flags
# register; CPP_PRELOAD_DIM and CPP_PACKET_FIRST do not.
# Within one invocation all GPR operands must be distinct, and all ZMM
# operands must be distinct.

.equ CPP_CTL_OFF, 0
.equ CPP_HALF_OFF, 16
.equ CPP_DADDR_OFF, 32
.equ CPP_DLANE_OFF, 64
.equ CPP_CSTEP_OFF, 96

# CPP_PRELOAD_DIM base, half, rec                         (once per dimension)
#   base <- zero-extended ctl[0..15]           half <- broadcast record.half
#   Writes: base, half.  Reads: rec (preserved).
.macro CPP_PRELOAD_DIM base, half, rec
	vpmovzxbd	CPP_CTL_OFF(%\rec), %zmm\base
	vpbroadcastd	CPP_HALF_OFF(%\rec), %zmm\half
.endm

# CPP_BLOCK_ADVANCE cst, rec, blk, t                (block B-1 -> B, B >= 1)
#   cst ^= cstep[ctz(B)]
#   Reads: rec, blk (preserved). Updates: cst. Clobbers: t.
.macro CPP_BLOCK_ADVANCE cst, rec, blk, t
	tzcntl	%\blk\()d, %\t\()d
	xorl	CPP_CSTEP_OFF(%\rec, %\t, 4), %\cst\()d
.endm

# CPP_BLOCK_ENTER live, base, cst, ga, gb                 (packet 0 of a block)
#   ga <- cst >> 16      gb <- ga ^ 64      live <- base ^ broadcast(cst)
#   Reads: base, cst (preserved). Writes: live, ga, gb.
.macro CPP_BLOCK_ENTER live, base, cst, ga, gb
	movl	%\cst\()d, %\ga\()d
	shrl	$16, %\ga\()d
	movl	%\ga\()d, %\gb\()d
	xorl	$64, %\gb\()d
	vpbroadcastd	%\cst\()d, %zmm\live
	vpxord	%zmm\base, %zmm\live, %zmm\live
.endm

# CPP_PACKET_FIRST out_a, out_b, live, half, d1, ga, gb            (p = 0)
#   out_a[l] <- d1[ga/4 + live[l]&15]       destinations 0..15
#   out_b[l] <- d1[gb/4 + (live^half)[l]&15] destinations 16..31
#   Reads: live, half, d1, ga, gb (all preserved). Writes: out_a, out_b.
.macro CPP_PACKET_FIRST out_a, out_b, live, half, d1, ga, gb
	vpxord	%zmm\half, %zmm\live, %zmm\out_b
	vpermd	(%\d1, %\ga), %zmm\live, %zmm\out_a
	vpermd	(%\d1, %\gb), %zmm\out_b, %zmm\out_b
.endm

# CPP_PACKET_NEXT out_a, out_b, live, half, d1, rec, p, ga, gb, t (p = 1..127)
#   Must follow packet p-1 of the same (dimension, block).
#   t <- ctz(p); ga ^= daddr[t]; gb ^= daddr[t]; live ^= broadcast(dlane[t])
#   then the CPP_PACKET_FIRST body for destinations 32p .. 32p+31.
#   Reads: half, d1, rec, p (preserved). Updates: live, ga, gb.
#   Writes: out_a, out_b. Clobbers: t.
.macro CPP_PACKET_NEXT out_a, out_b, live, half, d1, rec, p, ga, gb, t
	tzcntl	%\p\()d, %\t\()d
	xorl	CPP_DADDR_OFF(%\rec, %\t, 4), %\ga\()d
	xorl	CPP_DADDR_OFF(%\rec, %\t, 4), %\gb\()d
	vpxord	CPP_DLANE_OFF(%\rec, %\t, 4){1to16}, %zmm\live, %zmm\live
	vpxord	%zmm\half, %zmm\live, %zmm\out_b
	vpermd	(%\d1, %\ga), %zmm\live, %zmm\out_a
	vpermd	(%\d1, %\gb), %zmm\out_b, %zmm\out_b
.endm
