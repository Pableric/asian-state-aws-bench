# Standalone float32 exp(x) polynomial used only by the incomplete 18-step
# affine diagnostic.  The bit patterns are copied from the corrected European
# p0..p8 constants and are verified independently before fusion is allowed.

.section .rodata
.p2align 6
.LAsianExpP8_18diag:
    .long 0x3f800000, 0x3f7ffff9, 0x3efffffc, 0x3e2aabbf
    .long 0x3d2aab67, 0x3c085d88, 0x3ab5de3b, 0x3959cfde
    .long 0x37d8c471

.section .text

# ASIAN_EXP_P8_18DIAG x, out
#
# Input:     x is one ZMM of finite float32 values in [-1,1].
# Output:    out = p0 + x*(p1 + ... + x*p8), using exactly eight FMAs.
# Clobbers:  out only.
# Preserves: x.  x and out must be distinct.  No hidden register operands.
.macro ASIAN_EXP_P8_18DIAG x, out
    vbroadcastss .LAsianExpP8_18diag+32(%rip), %zmm\out
    vfmadd213ps .LAsianExpP8_18diag+28(%rip){1to16}, %zmm\x, %zmm\out
    vfmadd213ps .LAsianExpP8_18diag+24(%rip){1to16}, %zmm\x, %zmm\out
    vfmadd213ps .LAsianExpP8_18diag+20(%rip){1to16}, %zmm\x, %zmm\out
    vfmadd213ps .LAsianExpP8_18diag+16(%rip){1to16}, %zmm\x, %zmm\out
    vfmadd213ps .LAsianExpP8_18diag+12(%rip){1to16}, %zmm\x, %zmm\out
    vfmadd213ps .LAsianExpP8_18diag+8(%rip){1to16}, %zmm\x, %zmm\out
    vfmadd213ps .LAsianExpP8_18diag+4(%rip){1to16}, %zmm\x, %zmm\out
    vfmadd213ps .LAsianExpP8_18diag(%rip){1to16}, %zmm\x, %zmm\out
.endm
