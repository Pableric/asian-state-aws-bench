.include "private/asian_exp_p8_18diag.inc"

.section .rodata.autocall_d1_native_math,"a",@progbits
.p2align 6
.LWestA:
    .long 0x3d106f68,0x3f334c4e,0x40cbf780,0x4207a6c6
    .long 0x42e02899,0x435d36ae,0x435c34f5
.LWestB:
    .long 0x3db504f3,0x3fe0b9b4,0x41808370,0x42ad8fbc
    .long 0x43944839,0x441f555a,0x444674e6,0x43dc34f5
/* Offline weighted rational fit to Q(x)/exp(-x*x/2), u=x/8.  The
 * numerator/denominator degrees are 3/4; coefficients are frozen f32. */
.LR34P: .long 0x3f000000,0x3fc55bf4,0x401529e3,0xbd6f39ed
.LR34Q: .long 0x3f800000,0x4117783b,0x42045798,0x4231a9aa
.LMathOneEighth: .long 0x3e000000
.LMathNegHalf: .long 0xbf000000
.LMathOne:    .long 0x3f800000
.LMathTwo:    .long 0x40000000
.LMathEight:  .long 0x41000000
.LMathLog2E:  .long 0x3fb8aa3b
.LMathLn2Hi:  .long 0x3f318000
.LMathLn2Lo:  .long 0xb95e8083
.LMathLn2:    .long 0x3f317218

.section .text.autocall_d1_native_math,"ax",@progbits

.macro D1_NATIVE_EXP input, output, exponent, reduced
    vmulps .LMathLog2E(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vmovaps %zmm\input, %zmm\reduced
    vfnmadd231ps .LMathLn2Hi(%rip){1to16}, %zmm\exponent, %zmm\reduced
    vfnmadd231ps .LMathLn2Lo(%rip){1to16}, %zmm\exponent, %zmm\reduced
    ASIAN_EXP_P8_18DIAG \reduced, \output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

.macro D1_NATIVE_CDF_EXP input, output, exponent, reduced
    vmulps .LMathLog2E(%rip){1to16}, %zmm\input, %zmm\exponent
    vrndscaleps $0, %zmm\exponent, %zmm\exponent
    vfnmadd231ps .LMathLn2(%rip){1to16}, %zmm\exponent, %zmm\input
    vbroadcastss .LAsianExpP8_18diag+24(%rip), %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+20(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+16(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+12(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+8(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag+4(%rip){1to16}, %zmm\input, %zmm\output
    vfmadd213ps .LAsianExpP8_18diag(%rip){1to16}, %zmm\input, %zmm\output
    vscalefps %zmm\exponent, %zmm\output, %zmm\output
.endm

.macro D1_NATIVE_R34_PHI input, output, exact=0
    vpmovd2m %zmm\input, %k1
    vandps .LabsMask(%rip){1to16}, %zmm\input, %zmm1
    vcmpps $0x1d, .LMathEight(%rip){1to16}, %zmm1, %k2
    vminps .LMathEight(%rip){1to16}, %zmm1, %zmm1
    vmulps .LMathOneEighth(%rip){1to16}, %zmm1, %zmm2
    vbroadcastss .LR34P+12(%rip), %zmm3
    vfmadd213ps .LR34P+8(%rip){1to16}, %zmm2, %zmm3
    vfmadd213ps .LR34P+4(%rip){1to16}, %zmm2, %zmm3
    vfmadd213ps .LR34P(%rip){1to16}, %zmm2, %zmm3
    vbroadcastss .LR34Q+12(%rip), %zmm4
    vfmadd213ps .LR34Q+8(%rip){1to16}, %zmm2, %zmm4
    vfmadd213ps .LR34Q+4(%rip){1to16}, %zmm2, %zmm4
    vfmadd213ps .LR34Q(%rip){1to16}, %zmm2, %zmm4
    .if \exact
        vdivps %zmm4, %zmm3, %zmm3
    .else
        vrcp14ps %zmm4, %zmm8
        vmulps %zmm4, %zmm8, %zmm9
        vbroadcastss .LMathTwo(%rip), %zmm10
        vsubps %zmm9, %zmm10, %zmm9
        vmulps %zmm9, %zmm8, %zmm8
        vmulps %zmm8, %zmm3, %zmm3
    .endif
    vmulps %zmm1, %zmm1, %zmm7
    vmulps .LMathNegHalf(%rip){1to16}, %zmm7, %zmm7
    D1_NATIVE_CDF_EXP 7,4,5,6
    vmulps %zmm4, %zmm3, %zmm3
    vxorps %zmm10, %zmm10, %zmm10
    vbroadcastss .LMathOne(%rip), %zmm11
    vsubps %zmm3, %zmm11, %zmm\output
    vmovaps %zmm3, %zmm\output{%k1}
    vmovaps %zmm10, %zmm\output{%k2}
    knotw %k1, %k3
    kandw %k2, %k3, %k3
    vmovaps %zmm11, %zmm\output{%k3}
.endm

/* West positive-tail rational.  exact=0 uses vrcp14ps plus one Newton step;
 * exact=1 uses correctly-rounded vdivps.  Input/output may alias. */
.macro D1_NATIVE_WEST_PHI input, output, exact=0
    vandps .LabsMask(%rip){1to16}, %zmm\input, %zmm1
    vcmpps $0x1d, .LMathEight(%rip){1to16}, %zmm1, %k2
    vminps .LMathEight(%rip){1to16}, %zmm1, %zmm1

    vbroadcastss .LWestA(%rip), %zmm2
    vfmadd213ps .LWestA+4(%rip){1to16}, %zmm1, %zmm2
    vfmadd213ps .LWestA+8(%rip){1to16}, %zmm1, %zmm2
    vfmadd213ps .LWestA+12(%rip){1to16}, %zmm1, %zmm2
    vfmadd213ps .LWestA+16(%rip){1to16}, %zmm1, %zmm2
    vfmadd213ps .LWestA+20(%rip){1to16}, %zmm1, %zmm2
    vfmadd213ps .LWestA+24(%rip){1to16}, %zmm1, %zmm2

    vbroadcastss .LWestB(%rip), %zmm3
    vfmadd213ps .LWestB+4(%rip){1to16}, %zmm1, %zmm3
    vfmadd213ps .LWestB+8(%rip){1to16}, %zmm1, %zmm3
    vfmadd213ps .LWestB+12(%rip){1to16}, %zmm1, %zmm3
    vfmadd213ps .LWestB+16(%rip){1to16}, %zmm1, %zmm3
    vfmadd213ps .LWestB+20(%rip){1to16}, %zmm1, %zmm3
    vfmadd213ps .LWestB+24(%rip){1to16}, %zmm1, %zmm3
    vfmadd213ps .LWestB+28(%rip){1to16}, %zmm1, %zmm3

    .if \exact
        vdivps %zmm3, %zmm2, %zmm2
    .else
        vrcp14ps %zmm3, %zmm8
        vmulps %zmm3, %zmm8, %zmm9
        vbroadcastss .LMathTwo(%rip), %zmm10
        vsubps %zmm9, %zmm10, %zmm9
        vmulps %zmm9, %zmm8, %zmm8
        vmulps %zmm8, %zmm2, %zmm2
    .endif

    vmulps %zmm1, %zmm1, %zmm7
    vmulps .LMathNegHalf(%rip){1to16}, %zmm7, %zmm7
    D1_NATIVE_EXP 7,4,5,6
    vmulps %zmm4, %zmm2, %zmm2

    vxorps %zmm10, %zmm10, %zmm10
    vcmpps $0x11, %zmm10, %zmm\input, %k1
    vbroadcastss .LMathOne(%rip), %zmm11
    vsubps %zmm2, %zmm11, %zmm\output
    vmovaps %zmm2, %zmm\output{%k1}

    vmovaps %zmm10, %zmm\output{%k2}
    knotw %k1, %k3
    kandw %k2, %k3, %k3
    vmovaps %zmm11, %zmm\output{%k3}
.endm

.section .rodata.autocall_d1_native_math,"a",@progbits
.LabsMask: .long 0x7fffffff

.section .text.autocall_d1_native_math,"ax",@progbits
.p2align 6
.globl autocall_d1_native_west_phi_rcp_array
.type autocall_d1_native_west_phi_rcp_array,@function
autocall_d1_native_west_phi_rcp_array:
    xorq %rax,%rax
.Lphi_rcp_loop:
    vmovaps (%rdi,%rax,4),%zmm0
    D1_NATIVE_WEST_PHI 0,0,0
    vmovaps %zmm0,(%rsi,%rax,4)
    addq $16,%rax
    cmpq %rdx,%rax
    jb .Lphi_rcp_loop
    vzeroupper
    ret
.size autocall_d1_native_west_phi_rcp_array,.-autocall_d1_native_west_phi_rcp_array

.p2align 6
.globl autocall_d1_native_west_phi_div_array
.type autocall_d1_native_west_phi_div_array,@function
autocall_d1_native_west_phi_div_array:
    xorq %rax,%rax
.Lphi_div_loop:
    vmovaps (%rdi,%rax,4),%zmm0
    D1_NATIVE_WEST_PHI 0,0,1
    vmovaps %zmm0,(%rsi,%rax,4)
    addq $16,%rax
    cmpq %rdx,%rax
    jb .Lphi_div_loop
    vzeroupper
    ret
.size autocall_d1_native_west_phi_div_array,.-autocall_d1_native_west_phi_div_array

.p2align 6
.globl autocall_d1_native_r34_phi_rcp_array
.type autocall_d1_native_r34_phi_rcp_array,@function
autocall_d1_native_r34_phi_rcp_array:
    xorq %rax,%rax
.Lphi_r34_loop:
    vmovaps (%rdi,%rax,4),%zmm0
    D1_NATIVE_R34_PHI 0,0,0
    vmovaps %zmm0,(%rsi,%rax,4)
    addq $16,%rax
    cmpq %rdx,%rax
    jb .Lphi_r34_loop
    vzeroupper
    ret
.size autocall_d1_native_r34_phi_rcp_array,.-autocall_d1_native_r34_phi_rcp_array

.p2align 6
.globl autocall_d1_native_exp_array
.type autocall_d1_native_exp_array,@function
autocall_d1_native_exp_array:
    xorq %rax,%rax
.Lexp_array_loop:
    vmovaps (%rdi,%rax,4),%zmm0
    D1_NATIVE_EXP 0,1,2,3
    vmovaps %zmm1,(%rsi,%rax,4)
    addq $16,%rax
    cmpq %rdx,%rax
    jb .Lexp_array_loop
    vzeroupper
    ret
.size autocall_d1_native_exp_array,.-autocall_d1_native_exp_array

.section .note.GNU-stack,"",@progbits
