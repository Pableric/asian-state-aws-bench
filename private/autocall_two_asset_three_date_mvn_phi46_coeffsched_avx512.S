.intel_syntax noprefix
.text

/*
 * One coefficient-scheduling challenger for the rejected tensor/bridge
 * probes.  The probability topology and 40^3 operation order are unchanged.
 * Polynomial coefficients are consumed directly from RIP-relative memory;
 * zmm4/zmm5 and zmm8/zmm9 are polynomial accumulators, not persistent
 * coefficient registers.
 */

.macro MVN_CS_EXP_F64 input
    vbroadcastsd zmm4, QWORD PTR [rip + .Lcs_half]
    vfmadd231pd zmm4, \input, QWORD PTR [rip + .Lcs_log2e]{1to8}
    vrndscalepd zmm4, zmm4, 9
    vxorpd zmm5, zmm5, zmm5
    vsubpd zmm5, zmm5, zmm4
    vmovapd zmm6, \input
    vfmadd231pd zmm6, zmm5, QWORD PTR [rip + .Lcs_ln2_hi]{1to8}
    vfmadd231pd zmm6, zmm5, QWORD PTR [rip + .Lcs_ln2_lo]{1to8}
    vmulpd zmm7, zmm6, zmm6
    vbroadcastsd zmm8, QWORD PTR [rip + .Lcs_exp_p0]
    vfmadd213pd zmm8, zmm7, QWORD PTR [rip + .Lcs_exp_p1]{1to8}
    vfmadd213pd zmm8, zmm7, QWORD PTR [rip + .Lcs_one]{1to8}
    vmulpd zmm8, zmm8, zmm6
    vbroadcastsd zmm9, QWORD PTR [rip + .Lcs_exp_q0]
    vfmadd213pd zmm9, zmm7, QWORD PTR [rip + .Lcs_exp_q1]{1to8}
    vfmadd213pd zmm9, zmm7, QWORD PTR [rip + .Lcs_exp_q2]{1to8}
    vfmadd213pd zmm9, zmm7, QWORD PTR [rip + .Lcs_two]{1to8}
    vsubpd zmm9, zmm9, zmm8
    vdivpd zmm8, zmm8, zmm9
    vaddpd zmm8, zmm8, zmm8
    vaddpd zmm8, zmm8, QWORD PTR [rip + .Lcs_one]{1to8}
    vcvttpd2qq zmm10, zmm4
    vpsllq zmm10, zmm10, 52
    vpbroadcastq zmm11, QWORD PTR [rip + .Lcs_one_bits]
    vpaddq zmm10, zmm10, zmm11
    vmulpd \input, zmm8, zmm10
.endm

.macro MVN_CS_CDF_F64
    /* input/result zmm1; preserves zmm0 and zmm12..zmm31. */
    vcmppd k2, zmm1, zmm15, 0x11
    vpandq zmm2, zmm1, ZMMWORD PTR [rip + .Lcs_abs_mask]
    vcmppd k1, zmm2, QWORD PTR [rip + .Lcs_nine]{1to8}, 0x1d
    vmulpd zmm3, zmm2, zmm2
    vmulpd zmm3, zmm3, QWORD PTR [rip + .Lcs_minus_half]{1to8}
    MVN_CS_EXP_F64 zmm3
    vbroadcastsd zmm4, QWORD PTR [rip + .Lcs_cdf_n0]
    vbroadcastsd zmm5, QWORD PTR [rip + .Lcs_cdf_d0]
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcs_cdf_n1]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcs_cdf_d1]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcs_cdf_n2]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcs_cdf_d2]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcs_cdf_n3]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcs_cdf_d3]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcs_cdf_n4]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcs_cdf_d4]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcs_cdf_n5]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcs_cdf_d5]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcs_cdf_n6]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcs_cdf_d6]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcs_cdf_d7]{1to8}
    vmulpd zmm3, zmm3, zmm4
    vdivpd zmm3, zmm3, zmm5
    vbroadcastsd zmm6, QWORD PTR [rip + .Lcs_one]
    vsubpd zmm6, zmm6, zmm3
    vmovapd zmm1, zmm6
    vmovapd zmm1{k2}, zmm3
    vbroadcastsd zmm6, QWORD PTR [rip + .Lcs_one]
    vmovapd zmm1{k1}, zmm6
    kandb k3, k1, k2
    vmovapd zmm1{k3}, zmm15
.endm

/*
 * Conditional two-dimensional target.  x0 is zmm18, x1 is zmm31.
 * zmm15..zmm20 and zmm31 survive; result is returned in zmm30.
 */
.macro MVN_CS_CONDITIONAL target
    vmulpd zmm1, zmm18, ZMMWORD PTR [rdi + \target + 0x100]
    vfmadd231pd zmm1, zmm31, ZMMWORD PTR [rdi + \target + 0x140]
    vmulpd zmm2, zmm18, ZMMWORD PTR [rdi + \target + 0x180]
    vfmadd231pd zmm2, zmm31, ZMMWORD PTR [rdi + \target + 0x1c0]

    vmovapd zmm3, ZMMWORD PTR [rdi + \target + 0x000]
    vsubpd zmm3, zmm3, zmm1
    vmulpd zmm3, zmm3, ZMMWORD PTR [rdi + \target + 0x200]
    vmovapd zmm4, ZMMWORD PTR [rdi + \target + 0x040]
    vsubpd zmm4, zmm4, zmm1
    vmulpd zmm4, zmm4, ZMMWORD PTR [rdi + \target + 0x200]
    vmaxpd zmm3, zmm3, QWORD PTR [rip + .Lcs_minus_nine]{1to8}
    vminpd zmm4, zmm4, QWORD PTR [rip + .Lcs_nine]{1to8}

    vmovapd zmm23, ZMMWORD PTR [rdi + \target + 0x080]
    vsubpd zmm23, zmm23, zmm2
    vmulpd zmm23, zmm23, ZMMWORD PTR [rdi + \target + 0x240]
    vmovapd zmm24, ZMMWORD PTR [rdi + \target + 0x0c0]
    vsubpd zmm24, zmm24, zmm2
    vmulpd zmm24, zmm24, ZMMWORD PTR [rdi + \target + 0x240]

    vaddpd zmm21, zmm3, zmm4
    vmulpd zmm21, zmm21, QWORD PTR [rip + .Lcs_half]{1to8}
    vsubpd zmm22, zmm4, zmm3
    vmulpd zmm22, zmm22, QWORD PTR [rip + .Lcs_half]{1to8}
    vxorpd zmm25, zmm25, zmm25
    xor r8d, r8d
.Lcs_cond_node_\@:
    vmovapd zmm26, zmm21
    vfmadd231pd zmm26, zmm22, QWORD PTR [rdi + r8 + 0x7c0]{1to8}

    vmovapd zmm1, zmm24
    vfnmadd231pd zmm1, zmm26, ZMMWORD PTR [rdi + \target + 0x280]
    vmulpd zmm1, zmm1, ZMMWORD PTR [rdi + \target + 0x2c0]
    MVN_CS_CDF_F64
    vmovapd zmm27, zmm1

    vmovapd zmm1, zmm23
    vfnmadd231pd zmm1, zmm26, ZMMWORD PTR [rdi + \target + 0x280]
    vmulpd zmm1, zmm1, ZMMWORD PTR [rdi + \target + 0x2c0]
    MVN_CS_CDF_F64
    vsubpd zmm27, zmm27, zmm1
    vmaxpd zmm27, zmm27, zmm15

    vmulpd zmm3, zmm26, zmm26
    vmulpd zmm3, zmm3, QWORD PTR [rip + .Lcs_minus_half]{1to8}
    MVN_CS_EXP_F64 zmm3
    vmulpd zmm3, zmm3, QWORD PTR [rip + .Lcs_inv_sqrt_2pi]{1to8}
    vmulpd zmm3, zmm3, zmm27
    vfmadd231pd zmm25, zmm3, QWORD PTR [rdi + r8 + 0x900]{1to8}
    add r8d, 8
    cmp r8d, 320
    jne .Lcs_cond_node_\@
    vmulpd zmm30, zmm22, zmm25
    vmaxpd zmm30, zmm30, zmm15
    vminpd zmm30, zmm30, QWORD PTR [rip + .Lcs_one]{1to8}
.endm

.macro MVN_CS_OUTER_SETUP
    vmovapd zmm1, ZMMWORD PTR [rdi + 0x000]
    vmovapd zmm2, ZMMWORD PTR [rdi + 0x040]
    vaddpd zmm18, zmm1, zmm2
    vmulpd zmm18, zmm18, QWORD PTR [rip + .Lcs_half]{1to8}
    vsubpd zmm2, zmm2, zmm1
    vmulpd zmm2, zmm2, QWORD PTR [rip + .Lcs_half]{1to8}
    vfmadd231pd zmm18, zmm2, QWORD PTR [rdi + rdx + 0x7c0]{1to8}

    vmovapd zmm1, ZMMWORD PTR [rdi + 0x080]
    vfnmadd231pd zmm1, zmm18, ZMMWORD PTR [rdi + 0x100]
    vmulpd zmm1, zmm1, ZMMWORD PTR [rdi + 0x180]
    vmaxpd zmm1, zmm1, QWORD PTR [rip + .Lcs_minus_nine]{1to8}
    vmovapd zmm2, ZMMWORD PTR [rdi + 0x0c0]
    vfnmadd231pd zmm2, zmm18, ZMMWORD PTR [rdi + 0x100]
    vmulpd zmm2, zmm2, ZMMWORD PTR [rdi + 0x180]
    vminpd zmm2, zmm2, QWORD PTR [rip + .Lcs_nine]{1to8}
    vaddpd zmm19, zmm1, zmm2
    vmulpd zmm19, zmm19, QWORD PTR [rip + .Lcs_half]{1to8}
    vsubpd zmm20, zmm2, zmm1
    vmulpd zmm20, zmm20, QWORD PTR [rip + .Lcs_half]{1to8}
    vxorpd zmm17, zmm17, zmm17
    xor ecx, ecx
.endm

.macro MVN_CS_MIDDLE_POINT
    vmovapd zmm0, zmm19
    vfmadd231pd zmm0, zmm20, QWORD PTR [rdi + rcx + 0x7c0]{1to8}
    vmulpd zmm31, zmm18, ZMMWORD PTR [rdi + 0x100]
    vfmadd231pd zmm31, zmm0, ZMMWORD PTR [rdi + 0x140]
.endm

.macro MVN_CS_MIDDLE_ACCUMULATE
    vmulpd zmm3, zmm0, zmm0
    vmulpd zmm3, zmm3, QWORD PTR [rip + .Lcs_minus_half]{1to8}
    MVN_CS_EXP_F64 zmm3
    vmulpd zmm3, zmm3, QWORD PTR [rip + .Lcs_inv_sqrt_2pi]{1to8}
    vmulpd zmm3, zmm3, zmm30
    vfmadd231pd zmm17, zmm3, QWORD PTR [rdi + rcx + 0x900]{1to8}
    add ecx, 8
.endm

.macro MVN_CS_OUTER_ACCUMULATE
    vmulpd zmm1, zmm20, zmm17
    vmulpd zmm3, zmm18, zmm18
    vmulpd zmm3, zmm3, QWORD PTR [rip + .Lcs_minus_half]{1to8}
    MVN_CS_EXP_F64 zmm3
    vmulpd zmm3, zmm3, QWORD PTR [rip + .Lcs_inv_sqrt_2pi]{1to8}
    vmulpd zmm3, zmm3, zmm1
    vfmadd231pd zmm16, zmm3, QWORD PTR [rdi + rdx + 0x900]{1to8}
    add edx, 8
.endm

.macro MVN_CS_FINALIZE
    vmovapd zmm1, ZMMWORD PTR [rdi + 0x040]
    vsubpd zmm1, zmm1, ZMMWORD PTR [rdi + 0x000]
    vmulpd zmm1, zmm1, QWORD PTR [rip + .Lcs_half]{1to8}
    vmulpd zmm16, zmm16, zmm1
    vmaxpd zmm16, zmm16, zmm15
    vminpd zmm16, zmm16, QWORD PTR [rip + .Lcs_one]{1to8}
    vmovupd ZMMWORD PTR [rsi], zmm16
    vzeroupper
    ret
.endm

.p2align 6
.globl mvn_phi4_batch8_tensor40_coeffsched_asm
.type mvn_phi4_batch8_tensor40_coeffsched_asm,@function
mvn_phi4_batch8_tensor40_coeffsched_asm:
    vxorpd zmm15, zmm15, zmm15
    vxorpd zmm16, zmm16, zmm16
    xor edx, edx
.Lcs_phi4_outer:
    MVN_CS_OUTER_SETUP
.Lcs_phi4_middle:
    MVN_CS_MIDDLE_POINT
    MVN_CS_CONDITIONAL 0x1c0
    MVN_CS_MIDDLE_ACCUMULATE
    cmp ecx, 320
    jne .Lcs_phi4_middle
    MVN_CS_OUTER_ACCUMULATE
    cmp edx, 320
    jne .Lcs_phi4_outer
    MVN_CS_FINALIZE
.size mvn_phi4_batch8_tensor40_coeffsched_asm,.-mvn_phi4_batch8_tensor40_coeffsched_asm

.p2align 6
.globl mvn_phi6_batch8_bridge40_coeffsched_asm
.type mvn_phi6_batch8_bridge40_coeffsched_asm,@function
mvn_phi6_batch8_bridge40_coeffsched_asm:
    vxorpd zmm15, zmm15, zmm15
    vxorpd zmm16, zmm16, zmm16
    xor edx, edx
.Lcs_phi6_outer:
    MVN_CS_OUTER_SETUP
.Lcs_phi6_middle:
    MVN_CS_MIDDLE_POINT
    MVN_CS_CONDITIONAL 0x1c0
    vmovapd zmm12, zmm30
    MVN_CS_CONDITIONAL 0x4c0
    vmulpd zmm30, zmm30, zmm12
    MVN_CS_MIDDLE_ACCUMULATE
    cmp ecx, 320
    jne .Lcs_phi6_middle
    MVN_CS_OUTER_ACCUMULATE
    cmp edx, 320
    jne .Lcs_phi6_outer
    MVN_CS_FINALIZE
.size mvn_phi6_batch8_bridge40_coeffsched_asm,.-mvn_phi6_batch8_bridge40_coeffsched_asm

.section .rodata
.p2align 6
.Lcs_abs_mask:
    .quad 0x7fffffffffffffff,0x7fffffffffffffff,0x7fffffffffffffff,0x7fffffffffffffff
    .quad 0x7fffffffffffffff,0x7fffffffffffffff,0x7fffffffffffffff,0x7fffffffffffffff
.Lcs_one_bits: .quad 0x3ff0000000000000
.Lcs_one: .quad 0x3ff0000000000000
.Lcs_two: .quad 0x4000000000000000
.Lcs_half: .quad 0x3fe0000000000000
.Lcs_minus_half: .quad 0xbfe0000000000000
.Lcs_nine: .quad 0x4022000000000000
.Lcs_minus_nine: .quad 0xc022000000000000
.Lcs_log2e: .quad 0x3ff71547652b82fe
.Lcs_ln2_hi: .quad 0x3fe6300000000000
.Lcs_ln2_lo: .quad 0xbf2bd0105c610ca8
.Lcs_exp_p0: .quad 0x3f2089cdd5e44be8
.Lcs_exp_p1: .quad 0x3f9f06d10cca2c7e
.Lcs_exp_q0: .quad 0x3ec92eb6bc365fa0
.Lcs_exp_q1: .quad 0x3f64ae39b508b6c0
.Lcs_exp_q2: .quad 0x3fcd17099887e074
.Lcs_inv_sqrt_2pi: .quad 0x3fd9884533d43651
.Lcs_cdf_n0: .quad 0x3fa20ded0b57fbdd
.Lcs_cdf_n1: .quad 0x3fe66989be8ea720
.Lcs_cdf_n2: .quad 0x40197eeff2a86f23
.Lcs_cdf_n3: .quad 0x4040f4d8cbb02431
.Lcs_cdf_n4: .quad 0x405c05131ca58d3c
.Lcs_cdf_n5: .quad 0x406ba6d5c7a28cf2
.Lcs_cdf_n6: .quad 0x406b869ea974c7e5
.Lcs_cdf_d0: .quad 0x3fb6a09e667f3bcd
.Lcs_cdf_d1: .quad 0x3ffc173673887d1a
.Lcs_cdf_d2: .quad 0x4030106df11bd49d
.Lcs_cdf_d3: .quad 0x4055b1f78433a59a
.Lcs_cdf_d4: .quad 0x4072890729ba7820
.Lcs_cdf_d5: .quad 0x4083eaab47fa1777
.Lcs_cdf_d6: .quad 0x4088ce9cb298974a
.Lcs_cdf_d7: .quad 0x407b869ea974c7e5

.section .note.GNU-stack,"",@progbits
