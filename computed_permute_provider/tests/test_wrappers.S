# Test instantiations. Wrappers may store, branch and use the stack;
# the fragments they expand do not.

.include "cpp_provider_fragments.inc"
.include "dim_provider_fragments.inc"

.text

# void name(const uint32_t *donor, const cpp_dim_record_t *rec,
#           uint32_t *cst_io, uint64_t block, uint32_t *out4096,
#           uint64_t advance)
# advance != 0: CPP_BLOCK_ADVANCE with block (>= 1) and store cst to *cst_io.
# Then preload, enter, packet 0 .. 127, storing 4096 values to out4096.
# rax is the wrapper temporary and must not appear in the register map.
.macro CPP_TEST_BLOCK name, d1, rec, p, ga, gb, t, cst, blk, outp, base, half, live, oa, ob
.p2align 6
.globl \name
.type \name, @function
\name:
	pushq	%rbx
	pushq	%rbp
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$56, %rsp
	movq	%rdi, 0(%rsp)
	movq	%rsi, 8(%rsp)
	movq	%rdx, 16(%rsp)
	movq	%rcx, 24(%rsp)
	movq	%r8, 32(%rsp)
	movq	%r9, 40(%rsp)
	movq	0(%rsp), %\d1
	movq	8(%rsp), %\rec
	movq	24(%rsp), %\blk
	movq	16(%rsp), %rax
	movl	(%rax), %\cst\()d
	cmpq	$0, 40(%rsp)
	je	1f
	CPP_BLOCK_ADVANCE \cst, \rec, \blk, \t
	movq	16(%rsp), %rax
	movl	%\cst\()d, (%rax)
1:
	movq	32(%rsp), %\outp
	CPP_PRELOAD_DIM \base, \half, \rec
	CPP_BLOCK_ENTER \live, \base, \cst, \ga, \gb
	CPP_PACKET_FIRST \oa, \ob, \live, \half, \d1, \ga, \gb
	vmovdqa32	%zmm\oa, (%\outp)
	vmovdqa32	%zmm\ob, 64(%\outp)
	movl	$1, %\p\()d
2:
	addq	$128, %\outp
	CPP_PACKET_NEXT \oa, \ob, \live, \half, \d1, \rec, \p, \ga, \gb, \t
	vmovdqa32	%zmm\oa, (%\outp)
	vmovdqa32	%zmm\ob, 64(%\outp)
	addl	$1, %\p\()d
	cmpl	$128, %\p\()d
	jne	2b
	addq	$56, %rsp
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx
	vzeroupper
	ret
.size \name, .-\name
.endm

#               name                 d1   rec  p    ga   gb   t    cst  blk  outp base half live oa  ob
CPP_TEST_BLOCK cpp_test_block_main,  rdi, rsi, r8,  r9,  r10, r11, r12, r13, rdx, 16,  17,  18,  0,  1
CPP_TEST_BLOCK cpp_test_block_alt1,  rbx, rbp, r15, r14, r13, r12, r11, r10, rcx, 31,  30,  29,  28, 27
CPP_TEST_BLOCK cpp_test_block_alt2,  rcx, rdx, r9,  r12, r15, r8,  r14, r11, rsi, 5,   9,   3,   20, 7

# void cpp_test_block_affine(const uint32_t *donor, const dim_affine_ctx_t *ctx,
#                            uint32_t *out4096)
# Frozen baseline: DIM_PROVIDER_PRELOAD_AFFINE + DIM_PROVIDER_PACKET_AFFINE.
.p2align 6
.globl cpp_test_block_affine
.type cpp_test_block_affine, @function
cpp_test_block_affine:
	pushq	%r12
	movq	%rdx, %r12
	movq	%rsi, %rdx
	xorq	%rsi, %rsi
	DIM_PROVIDER_PRELOAD_AFFINE 16, 17, rdx
1:
	DIM_PROVIDER_PACKET_AFFINE 0, 1, 2, 3, 16, 17, rdi, rsi, rdx, r8, r9
	vmovdqa32	%zmm0, (%r12)
	vmovdqa32	%zmm1, 64(%r12)
	addq	$128, %r12
	addq	$1, %rsi
	cmpq	$128, %rsi
	jne	1b
	popq	%r12
	vzeroupper
	ret
.size cpp_test_block_affine, .-cpp_test_block_affine

# void cpp_test_advance_sweep(const cpp_dim_record_t *rec, uint64_t nblocks,
#                             uint32_t *states)
# states[0] = 0; states[B] = CPP_BLOCK_ADVANCE(states[B-1], B) for B = 1..nblocks-1.
.p2align 6
.globl cpp_test_advance_sweep
.type cpp_test_advance_sweep, @function
cpp_test_advance_sweep:
	pushq	%r12
	pushq	%r13
	xorl	%r12d, %r12d
	movl	%r12d, (%rdx)
	movl	$1, %r13d
	cmpq	$1, %rsi
	jbe	2f
1:
	CPP_BLOCK_ADVANCE r12, rdi, r13, r11
	movl	%r12d, (%rdx, %r13, 4)
	addq	$1, %r13
	cmpq	%rsi, %r13
	jne	1b
2:
	popq	%r13
	popq	%r12
	ret
.size cpp_test_advance_sweep, .-cpp_test_advance_sweep

# Contract probes: void name(const cpp_probe_io_t *in, cpp_probe_io_t *out)
# io layout: zmm[32][16] uint32 at 0, gpr[16] uint64 at 2048 indexed by the
# hardware register number (rax 0, rcx 1, rdx 2, rbx 3, rsp 4 unused, rbp 5,
# rsi 6, rdi 7, r8..r15 8..15). Every ZMM and GPR except rsp is loaded from
# in, the probed macro runs once, then every ZMM and GPR is stored to out.
.macro CPP_PROBE_PROLOGUE
	pushq	%rbx
	pushq	%rbp
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	pushq	%rsi
	subq	$8, %rsp
	.irp z, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	vmovdqu32	(\z * 64)(%rdi), %zmm\z
	.endr
	movq	2048 + 0*8(%rdi), %rax
	movq	2048 + 1*8(%rdi), %rcx
	movq	2048 + 2*8(%rdi), %rdx
	movq	2048 + 3*8(%rdi), %rbx
	movq	2048 + 5*8(%rdi), %rbp
	movq	2048 + 6*8(%rdi), %rsi
	movq	2048 + 8*8(%rdi), %r8
	movq	2048 + 9*8(%rdi), %r9
	movq	2048 + 10*8(%rdi), %r10
	movq	2048 + 11*8(%rdi), %r11
	movq	2048 + 12*8(%rdi), %r12
	movq	2048 + 13*8(%rdi), %r13
	movq	2048 + 14*8(%rdi), %r14
	movq	2048 + 15*8(%rdi), %r15
	movq	2048 + 7*8(%rdi), %rdi
.endm

.macro CPP_PROBE_EPILOGUE
	pushq	%rax
	movq	16(%rsp), %rax
	movq	%rcx, 2048 + 1*8(%rax)
	movq	%rdx, 2048 + 2*8(%rax)
	movq	%rbx, 2048 + 3*8(%rax)
	movq	%rbp, 2048 + 5*8(%rax)
	movq	%rsi, 2048 + 6*8(%rax)
	movq	%rdi, 2048 + 7*8(%rax)
	movq	%r8, 2048 + 8*8(%rax)
	movq	%r9, 2048 + 9*8(%rax)
	movq	%r10, 2048 + 10*8(%rax)
	movq	%r11, 2048 + 11*8(%rax)
	movq	%r12, 2048 + 12*8(%rax)
	movq	%r13, 2048 + 13*8(%rax)
	movq	%r14, 2048 + 14*8(%rax)
	movq	%r15, 2048 + 15*8(%rax)
	movq	(%rsp), %rcx
	movq	%rcx, 2048 + 0*8(%rax)
	movq	$0, 2048 + 4*8(%rax)
	.irp z, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	vmovdqu32	%zmm\z, (\z * 64)(%rax)
	.endr
	addq	$16, %rsp
	popq	%rsi
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx
	vzeroupper
	ret
.endm

.macro CPP_PROBES suffix, d1, rec, p, ga, gb, t, cst, blk, base, half, live, oa, ob
.p2align 6
.globl cpp_probe_preload_\suffix
.type cpp_probe_preload_\suffix, @function
cpp_probe_preload_\suffix:
	CPP_PROBE_PROLOGUE
	CPP_PRELOAD_DIM \base, \half, \rec
	CPP_PROBE_EPILOGUE
.size cpp_probe_preload_\suffix, .-cpp_probe_preload_\suffix

.p2align 6
.globl cpp_probe_advance_\suffix
.type cpp_probe_advance_\suffix, @function
cpp_probe_advance_\suffix:
	CPP_PROBE_PROLOGUE
	CPP_BLOCK_ADVANCE \cst, \rec, \blk, \t
	CPP_PROBE_EPILOGUE
.size cpp_probe_advance_\suffix, .-cpp_probe_advance_\suffix

.p2align 6
.globl cpp_probe_enter_\suffix
.type cpp_probe_enter_\suffix, @function
cpp_probe_enter_\suffix:
	CPP_PROBE_PROLOGUE
	CPP_BLOCK_ENTER \live, \base, \cst, \ga, \gb
	CPP_PROBE_EPILOGUE
.size cpp_probe_enter_\suffix, .-cpp_probe_enter_\suffix

.p2align 6
.globl cpp_probe_first_\suffix
.type cpp_probe_first_\suffix, @function
cpp_probe_first_\suffix:
	CPP_PROBE_PROLOGUE
	CPP_PACKET_FIRST \oa, \ob, \live, \half, \d1, \ga, \gb
	CPP_PROBE_EPILOGUE
.size cpp_probe_first_\suffix, .-cpp_probe_first_\suffix

.p2align 6
.globl cpp_probe_next_\suffix
.type cpp_probe_next_\suffix, @function
cpp_probe_next_\suffix:
	CPP_PROBE_PROLOGUE
	CPP_PACKET_NEXT \oa, \ob, \live, \half, \d1, \rec, \p, \ga, \gb, \t
	CPP_PROBE_EPILOGUE
.size cpp_probe_next_\suffix, .-cpp_probe_next_\suffix
.endm

#          suffix d1   rec  p    ga   gb   t    cst  blk  base half live oa  ob
CPP_PROBES main,  rdi, rsi, r8,  r9,  r10, r11, r12, r13, 16,  17,  18,  0,  1
CPP_PROBES alt1,  rbx, rbp, r15, r14, r13, r12, r11, r10, 31,  30,  29,  28, 27
CPP_PROBES alt2,  rcx, rdx, r9,  r12, r15, r8,  r14, r11, 5,   9,   3,   20, 7

.section .note.GNU-stack,"",@progbits
