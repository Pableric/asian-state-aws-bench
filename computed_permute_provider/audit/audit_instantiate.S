# One symbol per accounting boundary, each ending in a single ret that the
# audit excludes. Register choices match bench/bench_kernels.S.

.include "cpp_provider_fragments.inc"
.include "dim_provider_fragments.inc"
.include "bench_consumers.inc"

.macro BOUNDARY name
.p2align 6
.globl \name
.type \name, @function
\name:
.endm

.macro END_BOUNDARY name
	ret
.size \name, .-\name
.endm

.text

# A: frozen 448-byte affine provider.
BOUNDARY audit_a_dim_preload
	DIM_PROVIDER_PRELOAD_AFFINE 16, 17, r15
END_BOUNDARY audit_a_dim_preload

# Caller side of A: select the context of (block b = r12, dimension d = r13).
BOUNDARY audit_a_context_select
	movq	%r12, %r15
	imulq	64(%rbx), %r15
	addq	%r13, %r15
	imulq	$448, %r15, %r15
	addq	8(%rbx), %r15
END_BOUNDARY audit_a_context_select

BOUNDARY audit_a_packet
	DIM_PROVIDER_PACKET_AFFINE 0, 1, 2, 3, 16, 17, r14, rsi, r15, r8, r9
END_BOUNDARY audit_a_packet

# B: computed provider.
BOUNDARY audit_b_dim_preload
	CPP_PRELOAD_DIM 16, 17, r15
END_BOUNDARY audit_b_dim_preload

BOUNDARY audit_b_block_advance
	CPP_BLOCK_ADVANCE r10, r15, r8, r9
END_BOUNDARY audit_b_block_advance

BOUNDARY audit_b_block_enter
	CPP_BLOCK_ENTER 18, 16, r10, r9, r11
END_BOUNDARY audit_b_block_enter

BOUNDARY audit_b_packet_first
	CPP_PACKET_FIRST 0, 1, 18, 17, r14, r9, r11
END_BOUNDARY audit_b_packet_first

BOUNDARY audit_b_packet_next
	CPP_PACKET_NEXT 0, 1, 18, 17, r14, r15, r8, r9, r11, r10
END_BOUNDARY audit_b_packet_next

# Caller side of B: select the record of dimension d = r13.
BOUNDARY audit_b_record_select
	imulq	$192, %r13, %r15
	addq	16(%rbx), %r15
END_BOUNDARY audit_b_record_select

# Consumers (identical for A and B).
BOUNDARY audit_consumer_sink
	CONSUME sink
END_BOUNDARY audit_consumer_sink

BOUNDARY audit_consumer_fused
	CONSUME fused
END_BOUNDARY audit_consumer_fused

.section .note.GNU-stack,"",@progbits
