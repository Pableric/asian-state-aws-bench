# Consumers shared by the A/B pass kernels and the audit instantiation.
# Inputs: zmm0 (destinations 32p..32p+15), zmm1 (32p+16..32p+31).
# sink:  zmm20 ^= zmm0, zmm21 ^= zmm1.
# fused: per destination  S *= g;  Q += S;  L = w * x + L  with g = x = provider
#        value, w = zmm22, state at rdx (S), rdx+16384 (Q), rdx+32768 (L);
#        rdx advances by one packet (128 bytes).
.macro CONSUME kind
.ifc \kind,sink
	vpxord	%zmm0, %zmm20, %zmm20
	vpxord	%zmm1, %zmm21, %zmm21
.else
	vmulps	(%rdx), %zmm0, %zmm4
	vmovaps	%zmm4, (%rdx)
	vaddps	16384(%rdx), %zmm4, %zmm5
	vmovaps	%zmm5, 16384(%rdx)
	vfmadd213ps	32768(%rdx), %zmm22, %zmm0
	vmovaps	%zmm0, 32768(%rdx)
	vmulps	64(%rdx), %zmm1, %zmm6
	vmovaps	%zmm6, 64(%rdx)
	vaddps	16448(%rdx), %zmm6, %zmm7
	vmovaps	%zmm7, 16448(%rdx)
	vfmadd213ps	32832(%rdx), %zmm22, %zmm1
	vmovaps	%zmm1, 32832(%rdx)
	addq	$128, %rdx
.endif
.endm
