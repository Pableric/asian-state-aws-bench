# Matched A/B pass kernels. A = frozen 448-byte affine provider, B = computed
# provider. Identical donor bank, (block, dimension, packet) order, loop
# skeleton and consumer; only the provider fragment and its per-(block, dim)
# setup differ. Marker symbols bracket the per-packet loop body for the audit.

.include "cpp_provider_fragments.inc"
.include "dim_provider_fragments.inc"

.equ JOB_DONOR, 0
.equ JOB_ACTX, 8
.equ JOB_REC, 16
.equ JOB_BSTATE, 24
.equ JOB_CSTATE, 32
.equ JOB_SINK, 40
.equ JOB_BFIRST, 48
.equ JOB_NBLOCKS, 56
.equ JOB_NDIMS, 64
.equ JOB_WEIGHTS, 72

.include "bench_consumers.inc"

# Registers: rbx job, r12 b, r13 d, r14 donor block, r15 context/record.
.macro PASS_KERNEL name, variant, kind
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
	movq	%rdi, %rbx
	vpxord	%zmm20, %zmm20, %zmm20
	vpxord	%zmm21, %zmm21, %zmm21
	xorl	%r12d, %r12d
.L\name\()_block:
	movq	%r12, %r14
	shlq	$14, %r14
	addq	JOB_DONOR(%rbx), %r14
	xorl	%r13d, %r13d
.L\name\()_dim:
.ifc \kind,fused
	movq	JOB_WEIGHTS(%rbx), %rax
	vbroadcastss	(%rax, %r13, 4), %zmm22
	movq	JOB_CSTATE(%rbx), %rdx
.endif
.ifc \variant,A
	movq	%r12, %r15
	imulq	JOB_NDIMS(%rbx), %r15
	addq	%r13, %r15
	imulq	$448, %r15, %r15
	addq	JOB_ACTX(%rbx), %r15
	DIM_PROVIDER_PRELOAD_AFFINE 16, 17, r15
	xorl	%esi, %esi
.globl \name\()_packet_begin
\name\()_packet_begin:
.L\name\()_packet:
	DIM_PROVIDER_PACKET_AFFINE 0, 1, 2, 3, 16, 17, r14, rsi, r15, r8, r9
	CONSUME \kind
	addq	$1, %rsi
	cmpq	$128, %rsi
	jne	.L\name\()_packet
.globl \name\()_packet_end
\name\()_packet_end:
.else
	imulq	$192, %r13, %r15
	addq	JOB_REC(%rbx), %r15
	movq	JOB_BFIRST(%rbx), %r8
	addq	%r12, %r8
	movq	JOB_BSTATE(%rbx), %rax
	movl	(%rax, %r13, 4), %r10d
	testq	%r8, %r8
	jz	.L\name\()_enter
.globl \name\()_transition_begin
\name\()_transition_begin:
	CPP_BLOCK_ADVANCE r10, r15, r8, r9
	movl	%r10d, (%rax, %r13, 4)
.globl \name\()_transition_end
\name\()_transition_end:
.L\name\()_enter:
	CPP_PRELOAD_DIM 16, 17, r15
	CPP_BLOCK_ENTER 18, 16, r10, r9, r11
	CPP_PACKET_FIRST 0, 1, 18, 17, r14, r9, r11
	CONSUME \kind
	movl	$1, %r8d
.globl \name\()_packet_begin
\name\()_packet_begin:
.L\name\()_packet:
	CPP_PACKET_NEXT 0, 1, 18, 17, r14, r15, r8, r9, r11, r10
	CONSUME \kind
	addl	$1, %r8d
	cmpl	$128, %r8d
	jne	.L\name\()_packet
.globl \name\()_packet_end
\name\()_packet_end:
.endif
	addq	$1, %r13
	cmpq	JOB_NDIMS(%rbx), %r13
	jne	.L\name\()_dim
	addq	$1, %r12
	cmpq	JOB_NBLOCKS(%rbx), %r12
	jne	.L\name\()_block
	movq	JOB_SINK(%rbx), %rax
	vmovdqu32	%zmm20, (%rax)
	vmovdqu32	%zmm21, 64(%rax)
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

.text
PASS_KERNEL bench_a_sink, A, sink
PASS_KERNEL bench_b_sink, B, sink
PASS_KERNEL bench_a_fused, A, fused
PASS_KERNEL bench_b_fused, B, fused

.section .note.GNU-stack,"",@progbits
