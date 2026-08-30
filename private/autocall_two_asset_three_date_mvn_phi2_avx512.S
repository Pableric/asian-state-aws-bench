.intel_syntax noprefix
.text

/*
 * Frozen eight-instance conditional-Genz Phi2 kernel.  Segment boundaries
 * are prepared cold.  The two fixed loops are the eight bounded segments and
 * twenty Gauss-Legendre nodes; there is no lane-dependent work.
 */

.macro MVN_EXP_F64 input
    vbroadcastsd zmm4, QWORD PTR [rip + .Lhalf]
    vfmadd231pd zmm4, \input, QWORD PTR [rip + .Llog2e]{1to8}
    vrndscalepd zmm4, zmm4, 9
    vxorpd zmm5, zmm5, zmm5
    vsubpd zmm5, zmm5, zmm4
    vmovapd zmm6, \input
    vfmadd231pd zmm6, zmm5, QWORD PTR [rip + .Lln2_hi]{1to8}
    vfmadd231pd zmm6, zmm5, QWORD PTR [rip + .Lln2_lo]{1to8}
    vmulpd zmm7, zmm6, zmm6
    vbroadcastsd zmm8, QWORD PTR [rip + .Lexp_p0]
    vfmadd213pd zmm8, zmm7, QWORD PTR [rip + .Lexp_p1]{1to8}
    vfmadd213pd zmm8, zmm7, QWORD PTR [rip + .Lone]{1to8}
    vmulpd zmm8, zmm8, zmm6
    vbroadcastsd zmm9, QWORD PTR [rip + .Lexp_q0]
    vfmadd213pd zmm9, zmm7, QWORD PTR [rip + .Lexp_q1]{1to8}
    vfmadd213pd zmm9, zmm7, QWORD PTR [rip + .Lexp_q2]{1to8}
    vfmadd213pd zmm9, zmm7, QWORD PTR [rip + .Ltwo]{1to8}
    vsubpd zmm9, zmm9, zmm8
    vdivpd zmm8, zmm8, zmm9
    vaddpd zmm8, zmm8, zmm8
    vaddpd zmm8, zmm8, QWORD PTR [rip + .Lone]{1to8}
    vcvttpd2qq zmm10, zmm4
    vpsllq zmm10, zmm10, 52
    vpbroadcastq zmm11, QWORD PTR [rip + .Lone_bits]
    vpaddq zmm10, zmm10, zmm11
    vmulpd \input, zmm8, zmm10
.endm

.macro MVN_CDF_F64
    /* input/result zmm1; preserves zmm0 and zmm16..zmm31 */
    vcmppd k2, zmm1, zmm15, 0x11
    vpandq zmm2, zmm1, ZMMWORD PTR [rip + .Labs_mask]
    vcmppd k1, zmm2, QWORD PTR [rip + .Lnine]{1to8}, 0x1d
    vmulpd zmm3, zmm2, zmm2
    vmulpd zmm3, zmm3, QWORD PTR [rip + .Lminus_half]{1to8}
    MVN_EXP_F64 zmm3
    vbroadcastsd zmm4, QWORD PTR [rip + .Lcdf_n0]
    vbroadcastsd zmm5, QWORD PTR [rip + .Lcdf_d0]
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcdf_n1]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcdf_d1]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcdf_n2]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcdf_d2]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcdf_n3]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcdf_d3]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcdf_n4]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcdf_d4]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcdf_n5]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcdf_d5]{1to8}
    vfmadd213pd zmm4, zmm2, QWORD PTR [rip + .Lcdf_n6]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcdf_d6]{1to8}
    vfmadd213pd zmm5, zmm2, QWORD PTR [rip + .Lcdf_d7]{1to8}
    vmulpd zmm3, zmm3, zmm4
    vdivpd zmm3, zmm3, zmm5
    vbroadcastsd zmm6, QWORD PTR [rip + .Lone]
    vsubpd zmm6, zmm6, zmm3
    vmovapd zmm1, zmm6
    vmovapd zmm1{k2}, zmm3
    vbroadcastsd zmm6, QWORD PTR [rip + .Lone]
    vmovapd zmm1{k1}, zmm6
    kandb k3, k1, k2
    vmovapd zmm1{k3}, zmm15
.endm

.p2align 6
.globl mvn_phi2_batch8_order20_asm
.type mvn_phi2_batch8_order20_asm,@function
mvn_phi2_batch8_order20_asm:
    /* Persistent batch state: zmm16..zmm23. */
    vxorpd zmm15, zmm15, zmm15
    vxorpd zmm16, zmm16, zmm16
    vmovapd zmm20, ZMMWORD PTR [rdi + 256]
    vmovapd zmm21, ZMMWORD PTR [rdi + 320]
    vmovapd zmm22, ZMMWORD PTR [rdi + 128]
    vmovapd zmm23, ZMMWORD PTR [rdi + 192]
    lea r8, [rip + .Lnode20]
    lea r9, [rip + .Lweight20]
    xor edx, edx
.Lphi2_segment:
    vmovapd zmm18, ZMMWORD PTR [rdi + 384 + rdx]
    vmovapd zmm19, ZMMWORD PTR [rdi + 896 + rdx]
    vaddpd zmm18, zmm18, zmm19
    vmulpd zmm18, zmm18, QWORD PTR [rip + .Lhalf]{1to8}
    vsubpd zmm19, zmm19, ZMMWORD PTR [rdi + 384 + rdx]
    vmulpd zmm19, zmm19, QWORD PTR [rip + .Lhalf]{1to8}
    vxorpd zmm17, zmm17, zmm17
    xor ecx, ecx
.Lphi2_node:
    vmovapd zmm0, zmm18
    vfmadd231pd zmm0, zmm19, QWORD PTR [r8 + rcx]{1to8}

    vmovapd zmm1, zmm23
    vfnmadd231pd zmm1, zmm20, zmm0
    vmulpd zmm1, zmm1, zmm21
    MVN_CDF_F64
    vmovapd zmm24, zmm1

    vmovapd zmm1, zmm22
    vfnmadd231pd zmm1, zmm20, zmm0
    vmulpd zmm1, zmm1, zmm21
    MVN_CDF_F64
    vsubpd zmm24, zmm24, zmm1
    vmaxpd zmm24, zmm24, zmm15

    vmulpd zmm3, zmm0, zmm0
    vmulpd zmm3, zmm3, QWORD PTR [rip + .Lminus_half]{1to8}
    MVN_EXP_F64 zmm3
    vmulpd zmm25, zmm3, QWORD PTR [rip + .Linv_sqrt_2pi]{1to8}
    vmulpd zmm25, zmm25, zmm24
    vfmadd231pd zmm17, zmm25, QWORD PTR [r9 + rcx]{1to8}
    add ecx, 8
    cmp ecx, 160
    jne .Lphi2_node
    vfmadd231pd zmm16, zmm19, zmm17
    add edx, 64
    cmp edx, 512
    jne .Lphi2_segment
    vmaxpd zmm16, zmm16, zmm15
    vminpd zmm16, zmm16, QWORD PTR [rip + .Lone]{1to8}
    vmovupd ZMMWORD PTR [rsi], zmm16
    vzeroupper
    ret
.size mvn_phi2_batch8_order20_asm,.-mvn_phi2_batch8_order20_asm

/* Forty-node Plackett angle lower-orthant kernel, eight bound pairs. */
.p2align 6
.globl mvn_phi2_angle_batch8_order40_asm
.type mvn_phi2_angle_batch8_order40_asm,@function
mvn_phi2_angle_batch8_order40_asm:
    vxorpd zmm15, zmm15, zmm15
    vmovapd zmm20, ZMMWORD PTR [rdi + 0]
    vmovapd zmm21, ZMMWORD PTR [rdi + 64]
    vmovapd zmm22, ZMMWORD PTR [rdi + 128]
    vmovapd zmm23, ZMMWORD PTR [rdi + 192]
    vmovapd zmm1, zmm20
    MVN_CDF_F64
    vmovapd zmm16, zmm1
    vmovapd zmm1, zmm21
    MVN_CDF_F64
    vmovapd zmm18, zmm1
    vxorpd zmm17, zmm17, zmm17
    xor edx, edx
.Lphi2_angle_node:
    vmovapd zmm1, zmm22
    vfnmadd231pd zmm1, zmm23, QWORD PTR [rdi + 256 + rdx]{1to8}
    vmulpd zmm3, zmm1, QWORD PTR [rdi + 576 + rdx]{1to8}
    vcmppd k4, zmm3, QWORD PTR [rip + .Lminus_700]{1to8}, 0x11
    MVN_EXP_F64 zmm3
    vmovapd zmm3{k4}, zmm15
    vfmadd231pd zmm17, zmm3, QWORD PTR [rdi + 896 + rdx]{1to8}
    add edx, 8
    cmp edx, 320
    jne .Lphi2_angle_node
    vfmadd231pd zmm17, zmm16, zmm18
    vmaxpd zmm17, zmm17, zmm15
    vminpd zmm17, zmm17, QWORD PTR [rip + .Lone]{1to8}
    vmovupd ZMMWORD PTR [rsi], zmm17
    vzeroupper
    ret
.size mvn_phi2_angle_batch8_order40_asm,.-mvn_phi2_angle_batch8_order40_asm

.section .rodata
.p2align 6
.Labs_mask:
    .quad 0x7fffffffffffffff,0x7fffffffffffffff,0x7fffffffffffffff,0x7fffffffffffffff
    .quad 0x7fffffffffffffff,0x7fffffffffffffff,0x7fffffffffffffff,0x7fffffffffffffff
.Lone_bits: .quad 0x3ff0000000000000
.Lone: .quad 0x3ff0000000000000
.Ltwo: .quad 0x4000000000000000
.Lhalf: .quad 0x3fe0000000000000
.Lminus_half: .quad 0xbfe0000000000000
.Lnine: .quad 0x4022000000000000
.Lminus_700: .quad 0xc085e00000000000
.Llog2e: .quad 0x3ff71547652b82fe
.Lln2_hi: .quad 0x3fe6300000000000
.Lln2_lo: .quad 0xbf2bd0105c610ca8
.Lexp_p0: .quad 0x3f2089cdd5e44be8
.Lexp_p1: .quad 0x3f9f06d10cca2c7e
.Lexp_q0: .quad 0x3ec92eb6bc365fa0
.Lexp_q1: .quad 0x3f64ae39b508b6c0
.Lexp_q2: .quad 0x3fcd17099887e074
.Linv_sqrt_2pi: .quad 0x3fd9884533d43651
.Lcdf_n0: .quad 0x3fa20ded0b57fbdd
.Lcdf_n1: .quad 0x3fe66989be8ea720
.Lcdf_n2: .quad 0x40197eeff2a86f23
.Lcdf_n3: .quad 0x4040f4d8cbb02431
.Lcdf_n4: .quad 0x405c05131ca58d3c
.Lcdf_n5: .quad 0x406ba6d5c7a28cf2
.Lcdf_n6: .quad 0x406b869ea974c7e5
.Lcdf_d0: .quad 0x3fb6a09e667f3bcd
.Lcdf_d1: .quad 0x3ffc173673887d1a
.Lcdf_d2: .quad 0x4030106df11bd49d
.Lcdf_d3: .quad 0x4055b1f78433a59a
.Lcdf_d4: .quad 0x4072890729ba7820
.Lcdf_d5: .quad 0x4083eaab47fa1777
.Lcdf_d6: .quad 0x4088ce9cb298974a
.Lcdf_d7: .quad 0x407b869ea974c7e5
.p2align 6
.Lnode20:
    .quad 0xbfefc7b5a0c71ce1,0xbfeed8dba7bd769f,0xbfed31064173fd92
    .quad 0xbfeada0bd5efd6e7,0xbfe7e1f37346a54e,0xbfe45a8d3fa710db
    .quad 0xbfe05905c13f7ff7,0xbfd7eaccf15652c4,0xbfcd281636928bc0
    .quad 0xbfb3973df98b86b0,0x3fb3973df98b86b0,0x3fcd281636928bc0
    .quad 0x3fd7eaccf15652c4,0x3fe05905c13f7ff7,0x3fe45a8d3fa710db
    .quad 0x3fe7e1f37346a54e,0x3feada0bd5efd6e7,0x3fed31064173fd92
    .quad 0x3feed8dba7bd769f,0x3fefc7b5a0c71ce1
.Lweight20:
    .quad 0x3f9209680274e74e,0x3fa4c9b5ea53b638,0x3fb00b467df7e461
    .quad 0x3fb5519fe196e247,0x3fba1817a317a834,0x3fbe41ff31573b56
    .quad 0x3fc0db2c5db26e08,0x3fc230348f34a542,0x3fc31819b52c59a4
    .quad 0x3fc38d6c490a3380,0x3fc38d6c490a3380,0x3fc31819b52c59a4
    .quad 0x3fc230348f34a542,0x3fc0db2c5db26e08,0x3fbe41ff31573b56
    .quad 0x3fba1817a317a834,0x3fb5519fe196e247,0x3fb00b467df7e461
    .quad 0x3fa4c9b5ea53b638,0x3f9209680274e74e

.section .note.GNU-stack,"",@progbits
