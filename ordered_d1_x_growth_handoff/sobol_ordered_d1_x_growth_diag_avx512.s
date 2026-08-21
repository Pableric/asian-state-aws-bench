# Private position-aware ordered-D1 x/growth diagnostics.
#
# The array symbols use the canonical D1 stream beginning at Sobol index 8192.
# They are leaf functions and deliberately use only caller-saved GPRs.

.section .rodata
.align 64
diag_one_bits:       .long 0x3f800000
diag_pair_mask:      .long 0x00000fff
diag_range_mask:     .long 0x000007ff
diag_center:         .long 0x00400000
.align 64
diag_reverse_index:
    .long 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
diag_exp_p0: .float 1.00000000361
diag_exp_p1: .float 0.999999559932
diag_exp_p2: .float 0.499999873009
diag_exp_p3: .float 0.166670788605
diag_exp_p4: .float 0.0416673696717
diag_exp_p5: .float 0.00832308095835
diag_exp_p6: .float 0.0013875434074
diag_exp_p7: .float 0.0002077216867
diag_exp_p8: .float 2.58406812172e-05
# Degree-8 Chebyshev-interpolated hard correction for exp(x) on [-1.2,1.2].
# This is evaluated on the already-rounded float32 x, not as exp(alpha*z)*exp(drift).
diag_hard_exp_p0: .float 1.0
diag_hard_exp_p1: .float 0.999999582767
diag_hard_exp_p2: .float 0.499999970198
diag_hard_exp_p3: .float 0.166670635343
diag_hard_exp_p4: .float 0.0416670627892
diag_hard_exp_p5: .float 0.0083234384656
diag_hard_exp_p6: .float 0.00138790358324
diag_hard_exp_p7: .float 0.000207518649404
diag_hard_exp_p8: .float 2.5709201509e-05

.include "private/ordered_d1_x_growth_diag_data.inc"

.equ CTX_JUMPS,        128
.equ CTX_DRIFT,        256
.equ CTX_DIFFUSION,    260
.equ CTX_EXP_DRIFT,    264
.equ CTX_X2,           320
.equ CTX_X3,           24896
.equ CTX_GROWTH3,      57664
.equ CTX_GROWTH3_FULL, 90432
.equ CTX_GROWTH2,      155968
.equ CTX_WEIGHTS,      180544
.equ COEFF_STRIDE,     8192
.equ FULL_STRIDE,      16384

.macro DIAG_LOAD_CONSTANTS
    vpbroadcastd diag_one_bits(%rip), %zmm0
    vpbroadcastd diag_center(%rip), %zmm1
    vpbroadcastd diag_pair_mask(%rip), %zmm2
    vmovdqa32 diag_reverse_index(%rip), %zmm3
.endm

.macro DIAG_PAIR_T state, out
    vmovdqa32 %\state, %\out
    vpsrld       $9, %\out, %\out
    vpsubd    %zmm1, %\out, %\out
    vpabsd    %\out, %\out
    vpandd    %zmm2, %\out, %\out
    vpslld      $11, %\out, %\out
    vpord     %zmm0, %\out, %\out
    vsubps    %zmm0, %\out, %\out
.endm

.macro DIAG_EVAL_COMPACT3 base, t, off, out
    vmovaps 3*COEFF_STRIDE+\base(%rsi,%\off), %\out
    vfmadd213ps 2*COEFF_STRIDE+\base(%rsi,%\off), %\t, %\out
    vfmadd213ps 1*COEFF_STRIDE+\base(%rsi,%\off), %\t, %\out
    vfmadd213ps 0*COEFF_STRIDE+\base(%rsi,%\off), %\t, %\out
.endm

.macro DIAG_EVAL_COMPACT2 base, t, off, out
    vmovaps 2*COEFF_STRIDE+\base(%rsi,%\off), %\out
    vfmadd213ps 1*COEFF_STRIDE+\base(%rsi,%\off), %\t, %\out
    vfmadd213ps 0*COEFF_STRIDE+\base(%rsi,%\off), %\t, %\out
.endm

.macro DIAG_PREP_PACKET
    movq %r8, %r9
    andq $127, %r9
    shlq $6, %r9
    movl $8128, %r10d
    subq %r9, %r10
    DIAG_PAIR_T zmm12, zmm14
    DIAG_PAIR_T zmm13, zmm15
    vpermps %zmm15, %zmm3, %zmm15
.endm

.macro DIAG_STORE_PAIR rega, regb, base
    movq %r8, %r11
    subq $256, %r11
    shlq $7, %r11
    vmovaps %\rega, (%\base,%r11)
    vpermps %\regb, %zmm3, %\regb
    vmovaps %\regb, 64(%\base,%r11)
.endm

.macro DIAG_ADVANCE loop
    decq %rdi
    jz .Ldone_\@
    incq %r8
    tzcntq %r8, %r11
    vpbroadcastd CTX_JUMPS(%rsi,%r11,4), %zmm11
    vpxord %zmm11, %zmm12, %zmm12
    vpxord %zmm11, %zmm13, %zmm13
    jmp \loop
.Ldone_\@:
.endm

# Evaluate the deterministic hard position selected by rdi in block r8.
# Returns z in xmm20. The four range-2047 locations use the generated
# exact table; the other sixty use fixed degree-4/5/6 Horner schedules.
.macro DIAG_HARD_Z
    cmpq $60, %rdi
    jae .Lhard_exact_\@
    leaq ordered_d1_diag_hard_base_words(%rip), %r11
    movl (%r11,%rdi,4), %r10d
    leaq ordered_d1_diag_block_xor(%rip), %r11
    xorl (%r11,%r8,4), %r10d
    shrl $9, %r10d
    subl $0x00400000, %r10d
    movl %r10d, %r11d
    sarl $31, %r11d
    xorl %r11d, %r10d
    subl %r11d, %r10d
    andl $2047, %r10d
    shll $12, %r10d
    orl $0x3f800000, %r10d
    vmovd %r10d, %xmm21
    vsubss diag_one_bits(%rip), %xmm21, %xmm21
    leaq ordered_d1_diag_hard_z_c6(%rip), %r11
    vmovss (%r11,%rdi,4), %xmm20
    leaq ordered_d1_diag_hard_z_c5(%rip), %r11
    vfmadd213ss (%r11,%rdi,4), %xmm21, %xmm20
    leaq ordered_d1_diag_hard_z_c4(%rip), %r11
    vfmadd213ss (%r11,%rdi,4), %xmm21, %xmm20
    leaq ordered_d1_diag_hard_z_c3(%rip), %r11
    vfmadd213ss (%r11,%rdi,4), %xmm21, %xmm20
    leaq ordered_d1_diag_hard_z_c2(%rip), %r11
    vfmadd213ss (%r11,%rdi,4), %xmm21, %xmm20
    leaq ordered_d1_diag_hard_z_c1(%rip), %r11
    vfmadd213ss (%r11,%rdi,4), %xmm21, %xmm20
    leaq ordered_d1_diag_hard_z_c0(%rip), %r11
    vfmadd213ss (%r11,%rdi,4), %xmm21, %xmm20
    jmp .Lhard_ready_\@
.Lhard_exact_\@:
    movq %r8, %r11
    shlq $2, %r11
    addq %rdi, %r11
    subq $60, %r11
    leaq ordered_d1_diag_range2047_exact(%rip), %r10
    vmovss (%r10,%r11,4), %xmm20
.Lhard_ready_\@:
.endm

.macro DIAG_HARD_X_STORE base
    vmovss CTX_DIFFUSION(%rsi), %xmm22
    vfmadd213ss CTX_DRIFT(%rsi), %xmm20, %xmm22
    vmovss %xmm22, (%\base,%r9,4)
.endm

.macro DIAG_HARD_GROWTH_STORE base
    vmovss CTX_DIFFUSION(%rsi), %xmm22
    vfmadd213ss CTX_DRIFT(%rsi), %xmm20, %xmm22
    vmovss diag_hard_exp_p8(%rip), %xmm23
    vfmadd213ss diag_hard_exp_p7(%rip), %xmm22, %xmm23
    vfmadd213ss diag_hard_exp_p6(%rip), %xmm22, %xmm23
    vfmadd213ss diag_hard_exp_p5(%rip), %xmm22, %xmm23
    vfmadd213ss diag_hard_exp_p4(%rip), %xmm22, %xmm23
    vfmadd213ss diag_hard_exp_p3(%rip), %xmm22, %xmm23
    vfmadd213ss diag_hard_exp_p2(%rip), %xmm22, %xmm23
    vfmadd213ss diag_hard_exp_p1(%rip), %xmm22, %xmm23
    vfmadd213ss diag_hard_exp_p0(%rip), %xmm22, %xmm23
    vmovss %xmm23, (%\base,%r9,4)
.endm

.macro DIAG_CORRECT_ARRAYS xbase, gbase
    shlq $5, %rax
    xorq %r8, %r8
.Lhard_block_\@:
    xorq %rdi, %rdi
.Lhard_position_\@:
    leaq ordered_d1_diag_hard_output_indices(%rip), %r11
    movl (%r11,%rdi,4), %r9d
    movq %r8, %r10
    shlq $13, %r10
    addq %r10, %r9
    cmpq %rax, %r9
    jae .Lhard_skip_\@
    DIAG_HARD_Z
    .ifnb \xbase
        DIAG_HARD_X_STORE \xbase
    .endif
    .ifnb \gbase
        DIAG_HARD_GROWTH_STORE \gbase
    .endif
.Lhard_skip_\@:
    incq %rdi
    cmpq $64, %rdi
    jne .Lhard_position_\@
    incq %r8
    movq %r8, %r9
    shlq $13, %r9
    cmpq %rax, %r9
    jb .Lhard_block_\@
.endm

# Correct the single hard lane in the current first-block packet, if present.
# This is a position-indexed deterministic schedule: the branch never depends
# on a Sobol value.  rax owns the consumer loop count, so rdi is scratch here.
.macro DIAG_CORRECT_PACKET xa, xb, ga, gb, with_growth
    movq %r8, %r9
    andq $255, %r9
    leaq ordered_d1_diag_hard_index_by_packet(%rip), %r11
    movzwl (%r11,%r9,2), %edi
    cmpl $65535, %edi
    je .Lpacket_ordinary_\@
    movq %r8, %r9
    subq $256, %r8
    shrq $8, %r8
    DIAG_HARD_Z
    movq %r9, %r8
    vmovss CTX_DIFFUSION(%rsi), %xmm22
    vfmadd213ss CTX_DRIFT(%rsi), %xmm20, %xmm22
    leaq ordered_d1_diag_hard_output_indices(%rip), %r11
    movl (%r11,%rdi,4), %r10d
    andl $31, %r10d
    movl $1, %r11d
    cmpl $16, %r10d
    jb .Lpacket_a_\@
    subl $16, %r10d
    xorl $15, %r10d
    shlx %r10d, %r11d, %r11d
    kmovw %r11d, %k1
    vbroadcastss %xmm22, %zmm22
    vmovaps %zmm22, %\xb{%k1}
    .if \with_growth
        vmovss diag_hard_exp_p8(%rip), %xmm24
        vfmadd213ss diag_hard_exp_p7(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p6(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p5(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p4(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p3(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p2(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p1(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p0(%rip), %xmm22, %xmm24
        vbroadcastss %xmm24, %zmm24
        vmovaps %zmm24, %\gb{%k1}
    .endif
    jmp .Lpacket_ordinary_\@
.Lpacket_a_\@:
    shlx %r10d, %r11d, %r11d
    kmovw %r11d, %k1
    vbroadcastss %xmm22, %zmm22
    vmovaps %zmm22, %\xa{%k1}
    .if \with_growth
        vmovss diag_hard_exp_p8(%rip), %xmm24
        vfmadd213ss diag_hard_exp_p7(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p6(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p5(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p4(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p3(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p2(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p1(%rip), %xmm22, %xmm24
        vfmadd213ss diag_hard_exp_p0(%rip), %xmm22, %xmm24
        vbroadcastss %xmm24, %zmm24
        vmovaps %zmm24, %\ga{%k1}
    .endif
.Lpacket_ordinary_\@:
.endm

.section .text

.global ordered_d1_x_only_diag
.type ordered_d1_x_only_diag,@function
ordered_d1_x_only_diag:
    movq %rdi, %rax
    DIAG_LOAD_CONSTANTS
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movl $256, %r8d
.Lx_loop:
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT3 CTX_X3, zmm14, r9, zmm16
    DIAG_EVAL_COMPACT3 CTX_X3, zmm15, r10, zmm17
    DIAG_STORE_PAIR zmm16, zmm17, rdx
    DIAG_ADVANCE .Lx_loop
    DIAG_CORRECT_ARRAYS rdx,
    vzeroupper
    ret
.size ordered_d1_x_only_diag,.-ordered_d1_x_only_diag

.global ordered_d1_x_quadratic_diag
.type ordered_d1_x_quadratic_diag,@function
ordered_d1_x_quadratic_diag:
    movq %rdi, %rax
    DIAG_LOAD_CONSTANTS
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movl $256, %r8d
.Lx2_loop:
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT2 CTX_X2, zmm14, r9, zmm16
    DIAG_EVAL_COMPACT2 CTX_X2, zmm15, r10, zmm17
    DIAG_STORE_PAIR zmm16, zmm17, rdx
    DIAG_ADVANCE .Lx2_loop
    DIAG_CORRECT_ARRAYS rdx,
    vzeroupper
    ret
.size ordered_d1_x_quadratic_diag,.-ordered_d1_x_quadratic_diag

.global ordered_d1_growth_local_diag
.type ordered_d1_growth_local_diag,@function
ordered_d1_growth_local_diag:
    movq %rdi, %rax
    DIAG_LOAD_CONSTANTS
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movl $256, %r8d
.Lg_loop:
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm14, r9, zmm18
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm15, r10, zmm19
    DIAG_STORE_PAIR zmm18, zmm19, rdx
    DIAG_ADVANCE .Lg_loop
    DIAG_CORRECT_ARRAYS ,rdx
    vzeroupper
    ret
.size ordered_d1_growth_local_diag,.-ordered_d1_growth_local_diag

.global ordered_d1_x_growth_local_diag
.type ordered_d1_x_growth_local_diag,@function
ordered_d1_x_growth_local_diag:
    movq %rdi, %rax
    DIAG_LOAD_CONSTANTS
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movl $256, %r8d
.Lxg_loop:
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT3 CTX_X3, zmm14, r9, zmm16
    DIAG_EVAL_COMPACT3 CTX_X3, zmm15, r10, zmm17
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm14, r9, zmm18
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm15, r10, zmm19
    movq %r8, %r11
    subq $256, %r11
    shlq $7, %r11
    vmovaps %zmm16, (%rdx,%r11)
    vpermps %zmm17, %zmm3, %zmm17
    vmovaps %zmm17, 64(%rdx,%r11)
    vmovaps %zmm18, (%rcx,%r11)
    vpermps %zmm19, %zmm3, %zmm19
    vmovaps %zmm19, 64(%rcx,%r11)
    DIAG_ADVANCE .Lxg_loop
    DIAG_CORRECT_ARRAYS rdx,rcx
    vzeroupper
    ret
.size ordered_d1_x_growth_local_diag,.-ordered_d1_x_growth_local_diag

# Exploratory 56-KiB dual context: 24-KiB quadratic x + 32-KiB cubic growth.
# It is intentionally reported separately because it does not pass the x gate.
.global ordered_d1_x_growth_compact56_diag
.type ordered_d1_x_growth_compact56_diag,@function
ordered_d1_x_growth_compact56_diag:
    movq %rdi, %rax
    DIAG_LOAD_CONSTANTS
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movl $256, %r8d
.Lxg56_loop:
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT2 CTX_X2, zmm14, r9, zmm16
    DIAG_EVAL_COMPACT2 CTX_X2, zmm15, r10, zmm17
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm14, r9, zmm18
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm15, r10, zmm19
    movq %r8, %r11
    subq $256, %r11
    shlq $7, %r11
    vmovaps %zmm16, (%rdx,%r11)
    vpermps %zmm17, %zmm3, %zmm17
    vmovaps %zmm17, 64(%rdx,%r11)
    vmovaps %zmm18, (%rcx,%r11)
    vpermps %zmm19, %zmm3, %zmm19
    vmovaps %zmm19, 64(%rcx,%r11)
    DIAG_ADVANCE .Lxg56_loop
    DIAG_CORRECT_ARRAYS rdx,rcx
    vzeroupper
    ret
.size ordered_d1_x_growth_compact56_diag,.-ordered_d1_x_growth_compact56_diag

.global ordered_d1_growth_local_full_diag
.type ordered_d1_growth_local_full_diag,@function
ordered_d1_growth_local_full_diag:
    movq %rdi, %rax
    DIAG_LOAD_CONSTANTS
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movl $256, %r8d
.Lgf_loop:
    movq %r8, %r9
    andq $127, %r9
    shlq $6, %r9
    leaq 8192(%r9), %r10
    DIAG_PAIR_T zmm12, zmm14
    DIAG_PAIR_T zmm13, zmm15
    vmovaps 3*FULL_STRIDE+CTX_GROWTH3_FULL(%rsi,%r9), %zmm18
    vfmadd213ps 2*FULL_STRIDE+CTX_GROWTH3_FULL(%rsi,%r9), %zmm14, %zmm18
    vfmadd213ps 1*FULL_STRIDE+CTX_GROWTH3_FULL(%rsi,%r9), %zmm14, %zmm18
    vfmadd213ps 0*FULL_STRIDE+CTX_GROWTH3_FULL(%rsi,%r9), %zmm14, %zmm18
    vmovaps 3*FULL_STRIDE+CTX_GROWTH3_FULL(%rsi,%r10), %zmm19
    vfmadd213ps 2*FULL_STRIDE+CTX_GROWTH3_FULL(%rsi,%r10), %zmm15, %zmm19
    vfmadd213ps 1*FULL_STRIDE+CTX_GROWTH3_FULL(%rsi,%r10), %zmm15, %zmm19
    vfmadd213ps 0*FULL_STRIDE+CTX_GROWTH3_FULL(%rsi,%r10), %zmm15, %zmm19
    movq %r8, %r11
    subq $256, %r11
    shlq $7, %r11
    vmovaps %zmm18, (%rdx,%r11)
    vmovaps %zmm19, 64(%rdx,%r11)
    DIAG_ADVANCE .Lgf_loop
    DIAG_CORRECT_ARRAYS ,rdx
    vzeroupper
    ret
.size ordered_d1_growth_local_full_diag,.-ordered_d1_growth_local_full_diag

# Private register ABI probe: ctx is in rsi; xA/xB return in zmm16/zmm17 and
# growthA/growthB return in zmm18/zmm19.  There are no architectural stores.
.global ordered_d1_x_growth_packet_probe_diag
.type ordered_d1_x_growth_packet_probe_diag,@function
ordered_d1_x_growth_packet_probe_diag:
    DIAG_LOAD_CONSTANTS
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movl $256, %r8d
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT3 CTX_X3, zmm14, r9, zmm16
    DIAG_EVAL_COMPACT3 CTX_X3, zmm15, r10, zmm17
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm14, r9, zmm18
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm15, r10, zmm19
    DIAG_CORRECT_PACKET zmm16,zmm17,zmm18,zmm19,1
    vpermps %zmm17, %zmm3, %zmm17
    vpermps %zmm19, %zmm3, %zmm19
    ret
.size ordered_d1_x_growth_packet_probe_diag,.-ordered_d1_x_growth_packet_probe_diag

# Private register ABI ordinary probe: packet 1 has no hard lane.  Output
# registers match ordered_d1_x_growth_packet_probe_diag.
.global ordered_d1_x_growth_packet_ordinary_probe_diag
.type ordered_d1_x_growth_packet_ordinary_probe_diag,@function
ordered_d1_x_growth_packet_ordinary_probe_diag:
    DIAG_LOAD_CONSTANTS
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    vpbroadcastd CTX_JUMPS(%rsi), %zmm11
    vpxord %zmm11, %zmm12, %zmm12
    vpxord %zmm11, %zmm13, %zmm13
    movl $257, %r8d
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT3 CTX_X3, zmm14, r9, zmm16
    DIAG_EVAL_COMPACT3 CTX_X3, zmm15, r10, zmm17
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm14, r9, zmm18
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm15, r10, zmm19
    DIAG_CORRECT_PACKET zmm16,zmm17,zmm18,zmm19,1
    vpermps %zmm17, %zmm3, %zmm17
    vpermps %zmm19, %zmm3, %zmm19
    ret
.size ordered_d1_x_growth_packet_ordinary_probe_diag,.-ordered_d1_x_growth_packet_ordinary_probe_diag

.global ordered_d1_x_growth_packet_probe_call_diag
.type ordered_d1_x_growth_packet_probe_call_diag,@function
ordered_d1_x_growth_packet_probe_call_diag:
    movq %rdi, %rsi
    jmp ordered_d1_x_growth_packet_probe_diag
.size ordered_d1_x_growth_packet_probe_call_diag,.-ordered_d1_x_growth_packet_probe_call_diag

.global ordered_d1_x_growth_packet_ordinary_probe_call_diag
.type ordered_d1_x_growth_packet_ordinary_probe_call_diag,@function
ordered_d1_x_growth_packet_ordinary_probe_call_diag:
    movq %rdi, %rsi
    jmp ordered_d1_x_growth_packet_ordinary_probe_diag
.size ordered_d1_x_growth_packet_ordinary_probe_call_diag,.-ordered_d1_x_growth_packet_ordinary_probe_call_diag

.global ordered_d1_x_growth_fused_consumer_diag
.type ordered_d1_x_growth_fused_consumer_diag,@function
ordered_d1_x_growth_fused_consumer_diag:
    DIAG_LOAD_CONSTANTS
    vmovaps   0(%rdx), %zmm4
    vmovaps  64(%rdx), %zmm5
    vmovaps 128(%rdx), %zmm6
    vmovaps 192(%rdx), %zmm7
    vmovaps 256(%rdx), %zmm8
    vmovaps 320(%rdx), %zmm9
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movl %edi, %eax
    movl $256, %r8d
.Lfused_loop:
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT3 CTX_X3, zmm14, r9, zmm16
    DIAG_EVAL_COMPACT3 CTX_X3, zmm15, r10, zmm17
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm14, r9, zmm18
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm15, r10, zmm19
    DIAG_CORRECT_PACKET zmm16,zmm17,zmm18,zmm19,1
    vpermps %zmm17, %zmm3, %zmm17
    vpermps %zmm19, %zmm3, %zmm19
    movq %r8, %r9
    subq $256, %r9
    vbroadcastss CTX_WEIGHTS(%rsi,%r9,4), %zmm25
    vmulps %zmm18, %zmm4, %zmm4
    vmulps %zmm19, %zmm5, %zmm5
    vaddps %zmm4, %zmm6, %zmm6
    vaddps %zmm5, %zmm7, %zmm7
    vfmadd231ps %zmm16, %zmm25, %zmm8
    vfmadd231ps %zmm17, %zmm25, %zmm9
    decl %eax
    jz .Lfused_done
    incq %r8
    tzcntq %r8, %r11
    vpbroadcastd CTX_JUMPS(%rsi,%r11,4), %zmm11
    vpxord %zmm11, %zmm12, %zmm12
    vpxord %zmm11, %zmm13, %zmm13
    jmp .Lfused_loop
.Lfused_done:
    vmovaps %zmm4,   0(%rcx)
    vmovaps %zmm5,  64(%rcx)
    vmovaps %zmm6, 128(%rcx)
    vmovaps %zmm7, 192(%rcx)
    vmovaps %zmm8, 256(%rcx)
    vmovaps %zmm9, 320(%rcx)
    vzeroupper
    ret
.size ordered_d1_x_growth_fused_consumer_diag,.-ordered_d1_x_growth_fused_consumer_diag

.global ordered_d1_x_sumx_stress_diag
.type ordered_d1_x_sumx_stress_diag,@function
ordered_d1_x_sumx_stress_diag:
    DIAG_LOAD_CONSTANTS
    vxorps %zmm4, %zmm4, %zmm4
    vxorps %zmm5, %zmm5, %zmm5
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movl %edi, %eax
    movl $256, %r8d
.Lsumx_loop:
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT3 CTX_X3, zmm14, r9, zmm16
    DIAG_EVAL_COMPACT3 CTX_X3, zmm15, r10, zmm17
    DIAG_CORRECT_PACKET zmm16,zmm17,zmm18,zmm19,0
    vpermps %zmm17, %zmm3, %zmm17
    vaddps %zmm16, %zmm4, %zmm4
    vaddps %zmm17, %zmm5, %zmm5
    decl %eax
    jz .Lsumx_done
    incq %r8
    tzcntq %r8, %r11
    vpbroadcastd CTX_JUMPS(%rsi,%r11,4), %zmm11
    vpxord %zmm11, %zmm12, %zmm12
    vpxord %zmm11, %zmm13, %zmm13
    jmp .Lsumx_loop
.Lsumx_done:
    vmovaps %zmm4,  0(%rdx)
    vmovaps %zmm5, 64(%rdx)
    vzeroupper
    ret
.size ordered_d1_x_sumx_stress_diag,.-ordered_d1_x_sumx_stress_diag

# Complete synthetic European consumer. payoff_scale_beta contains the two
# already-discounted float coefficients for max(scale*growth + beta, 0):
# call=(+S0*D,-K*D), put=(-S0*D,+K*D). Returns the double-precision mean.
.global ordered_d1_price_growth_local_diag
.type ordered_d1_price_growth_local_diag,@function
ordered_d1_price_growth_local_diag:
    DIAG_LOAD_CONSTANTS
    vbroadcastss 0(%rdx), %zmm28
    vbroadcastss 4(%rdx), %zmm29
    vxorps %zmm26, %zmm26, %zmm26
    vxorps %zmm30, %zmm30, %zmm30
    vmovdqa32   0(%rsi), %zmm12
    vmovdqa32  64(%rsi), %zmm13
    movq %rdi, %rax
    movq %rdi, %rcx
    movl $256, %r8d
.Lprice_loop:
    DIAG_PREP_PACKET
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm14, r9, zmm18
    DIAG_EVAL_COMPACT3 CTX_GROWTH3, zmm15, r10, zmm19
    DIAG_CORRECT_PACKET zmm16,zmm17,zmm18,zmm19,1
    vpermps %zmm19, %zmm3, %zmm19
    vfmadd213ps %zmm29, %zmm28, %zmm18
    vfmadd213ps %zmm29, %zmm28, %zmm19
    vmaxps %zmm30, %zmm18, %zmm18
    vmaxps %zmm30, %zmm19, %zmm19
    vaddps %zmm18, %zmm26, %zmm26
    vaddps %zmm19, %zmm26, %zmm26
    decq %rcx
    jz .Lprice_done
    incq %r8
    tzcntq %r8, %r11
    vpbroadcastd CTX_JUMPS(%rsi,%r11,4), %zmm11
    vpxord %zmm11, %zmm12, %zmm12
    vpxord %zmm11, %zmm13, %zmm13
    jmp .Lprice_loop
.Lprice_done:
    vextractf32x8 $1, %zmm26, %ymm0
    vaddps %ymm0, %ymm26, %ymm0
    vextractf128 $1, %ymm0, %xmm1
    vaddps %xmm1, %xmm0, %xmm0
    vhaddps %xmm0, %xmm0, %xmm0
    vhaddps %xmm0, %xmm0, %xmm0
    vcvtss2sd %xmm0, %xmm0, %xmm0
    shlq $5, %rax
    vcvtsi2sd %rax, %xmm31, %xmm31
    vdivsd %xmm31, %xmm0, %xmm0
    vzeroupper
    ret
.size ordered_d1_price_growth_local_diag,.-ordered_d1_price_growth_local_diag

.global ordered_d1_exp_p8_array_diag
.type ordered_d1_exp_p8_array_diag,@function
# rdi: number of 16-float vectors, rsi: x, xmm0: scale, rdx: output
ordered_d1_exp_p8_array_diag:
    vbroadcastss %xmm0, %zmm10
    xorq %rax, %rax
.Lp8_loop:
    vmovaps (%rsi,%rax), %zmm11
    vbroadcastss diag_exp_p8(%rip), %zmm12
    vfmadd213ps diag_exp_p7(%rip){1to16}, %zmm11, %zmm12
    vfmadd213ps diag_exp_p6(%rip){1to16}, %zmm11, %zmm12
    vfmadd213ps diag_exp_p5(%rip){1to16}, %zmm11, %zmm12
    vfmadd213ps diag_exp_p4(%rip){1to16}, %zmm11, %zmm12
    vfmadd213ps diag_exp_p3(%rip){1to16}, %zmm11, %zmm12
    vfmadd213ps diag_exp_p2(%rip){1to16}, %zmm11, %zmm12
    vfmadd213ps diag_exp_p1(%rip){1to16}, %zmm11, %zmm12
    vfmadd213ps diag_exp_p0(%rip){1to16}, %zmm11, %zmm12
    vmulps %zmm10, %zmm12, %zmm12
    vmovaps %zmm12, (%rdx,%rax)
    addq $64, %rax
    decq %rdi
    jnz .Lp8_loop
    vzeroupper
    ret
.size ordered_d1_exp_p8_array_diag,.-ordered_d1_exp_p8_array_diag

.section .note.GNU-stack,"",@progbits
