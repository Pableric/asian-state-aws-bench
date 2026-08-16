	.text
	.include "candidate_fragment.inc"

	.globl fragment_permd
	.type fragment_permd,@function
fragment_permd:
	# rdi=out, rsi=source_a, rdx=source_b, rcx=control_a, r8=control_b
	vmovdqa32	(%rcx), %zmm2
	vmovdqa32	(%r8), %zmm3
	D1_FRAG_PERMD %zmm0, %zmm1, %zmm2, %zmm3, %zmm0, %zmm1, %rsi, %rdx
	vmovdqu32	%zmm0, (%rdi)
	vmovdqu32	%zmm1, 64(%rdi)
	vzeroupper
	ret
	.size fragment_permd,.-fragment_permd

	.globl fragment_permi2d
	.type fragment_permi2d,@function
fragment_permi2d:
	# rdi=out, rsi=source_a, rdx=source_b, rcx=control_a, r8=control_b
	vmovdqa32	(%rcx), %zmm0
	vmovdqa32	(%r8), %zmm1
	D1_FRAG_PERMI2D %zmm0, %zmm1, %zmm0, %zmm1, %zmm2, %zmm3, %rsi, %rdx
	vmovdqu32	%zmm0, (%rdi)
	vmovdqu32	%zmm1, 64(%rdi)
	vzeroupper
	ret
	.size fragment_permi2d,.-fragment_permi2d

	.globl fragment_gen
	.type fragment_gen,@function
fragment_gen:
	# rdi=out, rsi=indices_a, rdx=indices_b, rcx=constant_table
	vmovdqa32	(%rsi), %zmm0
	vmovdqa32	(%rdx), %zmm1
	vmovdqa32	(%rcx), %zmm25
	vmovdqa32	64(%rcx), %zmm26
	vmovdqa32	128(%rcx), %zmm27
	D1_FRAG_GEN %zmm0, %zmm1, %zmm0, %zmm1, %zmm2, %zmm3, %zmm4, %zmm25, %zmm26, %zmm27
	vmovdqu32	%zmm0, (%rdi)
	vmovdqu32	%zmm1, 64(%rdi)
	vzeroupper
	ret
	.size fragment_gen,.-fragment_gen

	.globl fragment_gen_load
	.type fragment_gen_load,@function
fragment_gen_load:
	# rdi=out, rsi=indices_a, rdx=indices_b, rcx=constant_table
	vmovdqa32	(%rsi), %zmm0
	vmovdqa32	(%rdx), %zmm1
	D1_FRAG_GEN_LOAD %zmm0, %zmm1, %zmm0, %zmm1, %zmm2, %zmm3, %zmm4, %zmm25, %zmm26, %zmm27, %rcx
	vmovdqu32	%zmm0, (%rdi)
	vmovdqu32	%zmm1, 64(%rdi)
	vzeroupper
	ret
	.size fragment_gen_load,.-fragment_gen_load

	.section .note.GNU-stack,"",@progbits
