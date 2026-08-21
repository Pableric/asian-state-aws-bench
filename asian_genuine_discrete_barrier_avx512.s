.include "asian_genuine_discrete_barrier_carrier/tools/fragment_service/public_harness/dim_from_d1_frags.inc"

.equ BCTX_ROUTES, 0
.equ BCTX_D1, 8
.equ BCTX_TABLE, 16
.equ BCTX_ROUTE_COUNT, 24
.equ BCTX_MONITOR_COUNT, 28
.equ BCTX_S0, 32
.equ BCTX_BARRIER, 36
.equ BCTX_STRIKE, 40
.equ BCTX_SCALE, 48
.equ PATH_BYTES, 16384

.section .text

/*
 * Stage-1 proof leaf. Its numerical recurrence is the copied qualified
 * S/Q/L leaf; only the two ordered-quiet comparisons are new.
 */
.macro SQL_BARRIER_FUNCTION name, table
.p2align 6
.globl \name
.type \name,@function
\name:
    kmovq %rdi, %k2
    kmovq %rdx, %k3
    kmovd %esi, %k1
    kmovq %rcx, %k7
    vbroadcastss %xmm0, %zmm31
    xorq %rax, %rax
.Lsql_packet_\@:
    kmovq %k3, %rdx
    vmovaps 0(%rdx,%rax), %zmm4
    vmovaps 64(%rdx,%rax), %zmm5
    vmovaps 16384(%rdx,%rax), %zmm6
    vmovaps 16448(%rdx,%rax), %zmm7
    vmovaps 32768(%rdx,%rax), %zmm10
    vmovaps 32832(%rdx,%rax), %zmm11
    movq %rax, %rcx
    shrq $7, %rcx
    .if !\table
        kxnorw %k4, %k4, %k4
        kxnorw %k6, %k6, %k6
    .endif
    kmovq %k2, %rdi
    kmovd %k1, %eax
.Lsql_route_\@:
    .if \table
        kmovq %k7, %r8
        kmovw 0(%r8,%rcx,4), %k4
        kmovw 2(%r8,%rcx,4), %k6
    .endif
    movq 0(%rdi), %r8
    movq 8(%rdi), %rsi
    kmovq %rsi, %k5
    movq 16(%rdi), %r9
    DIM_FROM_D1_FRAG 0,1,2,3,r8,rcx,r9,r10,r11,rsi,rdx
    kmovq %k5, %r8
    DIM_FROM_D1_FRAG 12,13,14,15,r8,rcx,r9,r10,r11,rsi,rdx
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vcmpps $0x1e, %zmm31, %zmm4, %k4{%k4}
    vcmpps $0x1e, %zmm31, %zmm5, %k6{%k6}
    .if \table
        kmovq %k7, %r8
        kmovw %k4, 0(%r8,%rcx,4)
        kmovw %k6, 2(%r8,%rcx,4)
    .endif
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    vfmadd231ps 24(%rdi){1to16}, %zmm0, %zmm10
    vfmadd231ps 24(%rdi){1to16}, %zmm1, %zmm11
    addq $32, %rdi
    decl %eax
    jne .Lsql_route_\@
    kmovq %k3, %rdx
    movq %rcx, %rax
    shlq $7, %rax
    vmovaps %zmm4, 0(%rdx,%rax)
    vmovaps %zmm5, 64(%rdx,%rax)
    vmovaps %zmm6, 16384(%rdx,%rax)
    vmovaps %zmm7, 16448(%rdx,%rax)
    vmovaps %zmm10, 32768(%rdx,%rax)
    vmovaps %zmm11, 32832(%rdx,%rax)
    .if !\table
        movq %rax, %rcx
        shrq $7, %rcx
        kmovq %k7, %r8
        kmovw %k4, 0(%r8,%rcx,4)
        kmovw %k6, 2(%r8,%rcx,4)
    .endif
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lsql_packet_\@
    vzeroupper
    ret
.size \name,.-\name
.endm

SQL_BARRIER_FUNCTION asian_barrier_sql_resident_diag, 0
SQL_BARRIER_FUNCTION asian_barrier_sql_table_diag, 1

/* Load and permute one compact D2..DN growth route. */
.macro GROWTH_ROUTE
    movq 0(%rdi), %r8
    movq 8(%rdi), %r9
    movzbq 0(%r9,%rcx,4), %r11
    movzbq 1(%r9,%rcx,4), %rsi
    movzbq 2(%r9,%rcx,4), %rdx
    movzbq 3(%r9,%rcx,4), %r10
    shlq $6, %r11
    shlq $6, %rsi
    shlq $6, %rdx
    shlq $6, %r10
    vmovdqa32 0(%r8,%r11), %zmm14
    vmovdqa32 0(%r8,%rsi), %zmm15
    vmovdqa32 576(%r9,%rdx), %zmm12
    vmovdqa32 576(%r9,%r10), %zmm13
    vpermd %zmm14, %zmm12, %zmm12
    vpermd %zmm15, %zmm13, %zmm13
.endm

.macro UPDATE_MASK mode, predicate
    .if \mode == 1
        vcmpps $\predicate, %zmm28, %zmm4, %k4{%k4}
        vcmpps $\predicate, %zmm28, %zmm5, %k6{%k6}
    .elseif \mode == 2
        vcmpps $\predicate, %zmm28, %zmm4, %k1
        kandw %k1, %k4, %k4
        vcmpps $\predicate, %zmm28, %zmm5, %k1
        kandw %k1, %k6, %k6
    .elseif \mode == 3
        vcmpps $\predicate, %zmm28, %zmm4, %k4{%k4}
        vcmpps $\predicate, %zmm28, %zmm5, %k6{%k6}
    .endif
.endm

.macro SCHEDULED_UPDATE schedule, maskmode, predicate
    .if \schedule == 0
        vmulps %zmm12, %zmm4, %zmm4
        vmulps %zmm13, %zmm5, %zmm5
        UPDATE_MASK \maskmode, \predicate
    .else
        vmulps %zmm12, %zmm4, %zmm4
        .if \maskmode != 0
            .if \maskmode == 2
                vcmpps $\predicate, %zmm28, %zmm4, %k1
                kandw %k1, %k4, %k4
            .else
                vcmpps $\predicate, %zmm28, %zmm4, %k4{%k4}
            .endif
        .endif
        vmulps %zmm13, %zmm5, %zmm5
        .if \maskmode != 0
            .if \maskmode == 2
                vcmpps $\predicate, %zmm28, %zmm5, %k1
                kandw %k1, %k6, %k6
            .else
                vcmpps $\predicate, %zmm28, %zmm5, %k6{%k6}
            .endif
        .endif
    .endif
.endm

.macro TABLE_LOAD
    kmovq %k1, %r8
    kmovw 0(%r8,%rcx,4), %k4
    kmovw 2(%r8,%rcx,4), %k6
.endm

.macro TABLE_STORE
    kmovq %k1, %r8
    kmovw %k4, 0(%r8,%rcx,4)
    kmovw %k6, 2(%r8,%rcx,4)
.endm

.macro TERMINAL_PAY option, masked
    .if \option == 0
        vsubps %zmm29, %zmm4, %zmm0
        vsubps %zmm29, %zmm5, %zmm1
    .else
        vsubps %zmm4, %zmm29, %zmm0
        vsubps %zmm5, %zmm29, %zmm1
    .endif
    .if \masked
        vmaxps %zmm30, %zmm0, %zmm0{%k4}{z}
        vmaxps %zmm30, %zmm1, %zmm1{%k6}{z}
    .else
        vmaxps %zmm30, %zmm0, %zmm0
        vmaxps %zmm30, %zmm1, %zmm1
    .endif
    vaddps %zmm0, %zmm20, %zmm20
    vaddps %zmm1, %zmm21, %zmm21
.endm

.macro REDUCE_PAYOFF
    vaddps %zmm21, %zmm20, %zmm0
    vextractf32x4 $1, %zmm0, %xmm1
    vextractf32x4 $2, %zmm0, %xmm2
    vextractf32x4 $3, %zmm0, %xmm3
    vaddps %xmm1, %xmm0, %xmm0
    vaddps %xmm3, %xmm2, %xmm2
    vaddps %xmm2, %xmm0, %xmm0
    vmovhlps %xmm0, %xmm0, %xmm1
    vaddps %xmm1, %xmm0, %xmm0
    vshufps $1, %xmm0, %xmm0, %xmm1
    vaddss %xmm1, %xmm0, %xmm0
    vcvtss2sd %xmm0, %xmm0, %xmm0
    vmulsd %xmm26, %xmm0, %xmm0
.endm

/* maskmode: 0 vanilla, 1 self-mask, 2 explicit, 3 table. */
/* predicate: 0x1e down (S>B), 0x11 up (S<B). */
.macro S_ONLY_FUNCTION name, option, schedule, maskmode, predicate
.p2align 6
.globl \name
.type \name,@function
\name:
    kmovq BCTX_ROUTES(%rdi), %k2
    kmovq BCTX_D1(%rdi), %k3
    .if \maskmode == 3
        kmovq BCTX_TABLE(%rdi), %k1
    .endif
    kmovd BCTX_ROUTE_COUNT(%rdi), %k5
    vbroadcastss BCTX_S0(%rdi), %zmm31
    vbroadcastss BCTX_BARRIER(%rdi), %zmm28
    vbroadcastss BCTX_STRIKE(%rdi), %zmm29
    vmovsd BCTX_SCALE(%rdi), %xmm26
    vxorps %zmm30, %zmm30, %zmm30
    vxorps %zmm20, %zmm20, %zmm20
    vxorps %zmm21, %zmm21, %zmm21
    xorq %rax, %rax
.Ls_packet_\@:
    vmovaps %zmm31, %zmm4
    vmovaps %zmm31, %zmm5
    movq %rax, %rcx
    shrq $7, %rcx
    .if \maskmode == 1 || \maskmode == 2
        kxnorw %k4, %k4, %k4
        kxnorw %k6, %k6, %k6
    .elseif \maskmode == 3
        TABLE_LOAD
    .endif
    kmovq %k3, %rdx
    vmovaps 0(%rdx,%rax), %zmm12
    vmovaps 64(%rdx,%rax), %zmm13
    SCHEDULED_UPDATE \schedule, \maskmode, \predicate
    .if \maskmode == 3
        TABLE_STORE
    .endif
    kmovq %rax, %k7
    kmovd %k5, %eax
    testl %eax, %eax
    je .Ls_payoff_\@
    kmovq %k2, %rdi
.Ls_route_\@:
    .if \maskmode == 3
        TABLE_LOAD
    .endif
    GROWTH_ROUTE
    SCHEDULED_UPDATE \schedule, \maskmode, \predicate
    .if \maskmode == 3
        TABLE_STORE
    .endif
    addq $16, %rdi
    decl %eax
    jne .Ls_route_\@
.Ls_payoff_\@:
    TERMINAL_PAY \option, (\maskmode != 0)
    kmovq %k7, %rax
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Ls_packet_\@
    REDUCE_PAYOFF
    vzeroupper
    ret
.size \name,.-\name
.endm

S_ONLY_FUNCTION asian_barrier_vanilla_call_grouped_diag, 0, 0, 0, 0x1e
S_ONLY_FUNCTION asian_barrier_vanilla_put_grouped_diag, 1, 0, 0, 0x1e
S_ONLY_FUNCTION asian_barrier_vanilla_call_interleaved_diag, 0, 1, 0, 0x1e
S_ONLY_FUNCTION asian_barrier_vanilla_put_interleaved_diag, 1, 1, 0, 0x1e

S_ONLY_FUNCTION asian_barrier_down_call_self_grouped_diag, 0, 0, 1, 0x1e
S_ONLY_FUNCTION asian_barrier_down_put_self_grouped_diag, 1, 0, 1, 0x1e
S_ONLY_FUNCTION asian_barrier_down_call_self_interleaved_diag, 0, 1, 1, 0x1e
S_ONLY_FUNCTION asian_barrier_down_put_self_interleaved_diag, 1, 1, 1, 0x1e

S_ONLY_FUNCTION asian_barrier_down_call_explicit_grouped_diag, 0, 0, 2, 0x1e
S_ONLY_FUNCTION asian_barrier_down_put_explicit_grouped_diag, 1, 0, 2, 0x1e
S_ONLY_FUNCTION asian_barrier_down_call_explicit_interleaved_diag, 0, 1, 2, 0x1e
S_ONLY_FUNCTION asian_barrier_down_put_explicit_interleaved_diag, 1, 1, 2, 0x1e

S_ONLY_FUNCTION asian_barrier_down_call_table_grouped_diag, 0, 0, 3, 0x1e
S_ONLY_FUNCTION asian_barrier_down_put_table_grouped_diag, 1, 0, 3, 0x1e
S_ONLY_FUNCTION asian_barrier_down_call_table_interleaved_diag, 0, 1, 3, 0x1e
S_ONLY_FUNCTION asian_barrier_down_put_table_interleaved_diag, 1, 1, 3, 0x1e

S_ONLY_FUNCTION asian_barrier_up_call_self_grouped_diag, 0, 0, 1, 0x11
S_ONLY_FUNCTION asian_barrier_up_put_self_grouped_diag, 1, 0, 1, 0x11

/* Unranked correctness probe: the same direct-D1/compact-route recurrence. */
.p2align 6
.globl asian_barrier_s_only_probe_diag
.type asian_barrier_s_only_probe_diag,@function
asian_barrier_s_only_probe_diag:
    kmovq BCTX_ROUTES(%rdi), %k2
    kmovq BCTX_D1(%rdi), %k3
    kmovd BCTX_ROUTE_COUNT(%rdi), %k5
    vbroadcastss BCTX_S0(%rdi), %zmm31
    vbroadcastss BCTX_BARRIER(%rdi), %zmm28
    kmovq %rsi, %k1
    kmovq %rdx, %k7
    xorq %rax, %rax
.Lprobe_packet:
    vmovaps %zmm31, %zmm4
    vmovaps %zmm31, %zmm5
    kxnorw %k4, %k4, %k4
    kxnorw %k6, %k6, %k6
    movq %rax, %rcx
    shrq $7, %rcx
    kmovq %k3, %rdx
    vmovaps 0(%rdx,%rax), %zmm12
    vmovaps 64(%rdx,%rax), %zmm13
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vcmpps $0x1e, %zmm28, %zmm4, %k4{%k4}
    vcmpps $0x1e, %zmm28, %zmm5, %k6{%k6}
    kmovd %k5, %eax
    testl %eax, %eax
    je .Lprobe_store
    kmovq %k2, %rdi
.Lprobe_route:
    GROWTH_ROUTE
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vcmpps $0x1e, %zmm28, %zmm4, %k4{%k4}
    vcmpps $0x1e, %zmm28, %zmm5, %k6{%k6}
    addq $16, %rdi
    decl %eax
    jne .Lprobe_route
.Lprobe_store:
    movq %rcx, %rax
    shlq $7, %rax
    kmovq %k1, %rsi
    vmovaps %zmm4, 0(%rsi,%rax)
    vmovaps %zmm5, 64(%rsi,%rax)
    kmovq %k7, %rdx
    kmovw %k4, 0(%rdx,%rcx,4)
    kmovw %k6, 2(%rdx,%rcx,4)
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Lprobe_packet
    vzeroupper
    ret
.size asian_barrier_s_only_probe_diag,.-asian_barrier_s_only_probe_diag

/* Unranked trace leaf: one S and mask snapshot after each future update. */
.p2align 6
.globl asian_barrier_s_only_trace_diag
.type asian_barrier_s_only_trace_diag,@function
asian_barrier_s_only_trace_diag:
    kmovq BCTX_ROUTES(%rdi), %k2
    kmovq BCTX_D1(%rdi), %k3
    kmovd BCTX_ROUTE_COUNT(%rdi), %k5
    vbroadcastss BCTX_S0(%rdi), %zmm31
    vbroadcastss BCTX_BARRIER(%rdi), %zmm28
    kmovq %rsi, %k1
    kmovq %rdx, %k7
    xorq %rax, %rax
.Ltrace_packet:
    vmovaps %zmm31, %zmm4
    vmovaps %zmm31, %zmm5
    kxnorw %k4, %k4, %k4
    kxnorw %k6, %k6, %k6
    movq %rax, %rcx
    shrq $7, %rcx
    kmovq %k3, %rdx
    vmovaps 0(%rdx,%rax), %zmm12
    vmovaps 64(%rdx,%rax), %zmm13
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vcmpps $0x1e, %zmm28, %zmm4, %k4{%k4}
    vcmpps $0x1e, %zmm28, %zmm5, %k6{%k6}
    kmovq %k1, %rsi
    vmovaps %zmm4, 0(%rsi,%rax)
    vmovaps %zmm5, 64(%rsi,%rax)
    kmovq %k7, %rdx
    kmovw %k4, 0(%rdx,%rcx,4)
    kmovw %k6, 2(%rdx,%rcx,4)
    kmovd %k5, %eax
    testl %eax, %eax
    je .Ltrace_next_packet
    kmovq %k2, %rdi
.Ltrace_route:
    GROWTH_ROUTE
    vmulps %zmm12, %zmm4, %zmm4
    vmulps %zmm13, %zmm5, %zmm5
    vcmpps $0x1e, %zmm28, %zmm4, %k4{%k4}
    vcmpps $0x1e, %zmm28, %zmm5, %k6{%k6}
    kmovq %k2, %r8
    movq %rdi, %r11
    subq %r8, %r11
    shlq $10, %r11                 /* route index * 16 KiB / 16. */
    addq $16384, %r11              /* D2 is date index one. */
    movq %rcx, %r10
    shlq $7, %r10
    addq %r10, %r11
    kmovq %k1, %rsi
    vmovaps %zmm4, 0(%rsi,%r11)
    vmovaps %zmm5, 64(%rsi,%r11)
    movq %rdi, %r11
    subq %r8, %r11
    shlq $5, %r11                  /* route index * 512 / 16. */
    addq $512, %r11
    leaq (%r11,%rcx,4), %r11
    kmovq %k7, %rdx
    kmovw %k4, 0(%rdx,%r11)
    kmovw %k6, 2(%rdx,%r11)
    addq $16, %rdi
    decl %eax
    jne .Ltrace_route
.Ltrace_next_packet:
    movq %rcx, %rax
    shlq $7, %rax
    addq $128, %rax
    cmpq $PATH_BYTES, %rax
    jb .Ltrace_packet
    vzeroupper
    ret
.size asian_barrier_s_only_trace_diag,.-asian_barrier_s_only_trace_diag

.section .note.GNU-stack,"",@progbits
