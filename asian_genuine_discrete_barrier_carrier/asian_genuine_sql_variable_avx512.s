.include "tools/fragment_service/public_harness/dim_from_d1_frags.inc"
.text
.p2align 6
.globl asian_genuine_sql_variable_diag
.type asian_genuine_sql_variable_diag,@function
asian_genuine_sql_variable_diag:
 kmovq %rdi,%k2
 kmovq %rdx,%k3
 kmovd %esi,%k1
 xorq %rax,%rax
.Lgp_packet:
 kmovq %k3,%rdx
 vmovaps 0(%rdx,%rax),%zmm4
 vmovaps 64(%rdx,%rax),%zmm5
 vmovaps 16384(%rdx,%rax),%zmm6
 vmovaps 16448(%rdx,%rax),%zmm7
 vmovaps 32768(%rdx,%rax),%zmm10
 vmovaps 32832(%rdx,%rax),%zmm11
 movq %rax,%rcx
 shrq $7,%rcx
 kmovq %rax,%k7
 kmovq %k2,%rdi
 kmovd %k1,%eax
.Lgp_route:
 movq 0(%rdi),%r8
 movq 8(%rdi),%rsi
 kmovq %rsi,%k5
 movq 16(%rdi),%r9
 DIM_FROM_D1_FRAG 0,1,2,3,r8,rcx,r9,r10,r11,rsi,rdx
 kmovq %k5,%r8
 DIM_FROM_D1_FRAG 12,13,14,15,r8,rcx,r9,r10,r11,rsi,rdx
 vmulps %zmm12,%zmm4,%zmm4
 vmulps %zmm13,%zmm5,%zmm5
 vaddps %zmm4,%zmm6,%zmm6
 vaddps %zmm5,%zmm7,%zmm7
 vfmadd231ps 24(%rdi){1to16},%zmm0,%zmm10
 vfmadd231ps 24(%rdi){1to16},%zmm1,%zmm11
 addq $32,%rdi
 decl %eax
 jne .Lgp_route
 kmovq %k3,%rdx
 kmovq %k7,%rax
 vmovaps %zmm4,0(%rdx,%rax)
 vmovaps %zmm5,64(%rdx,%rax)
 vmovaps %zmm6,16384(%rdx,%rax)
 vmovaps %zmm7,16448(%rdx,%rax)
 vmovaps %zmm10,32768(%rdx,%rax)
 vmovaps %zmm11,32832(%rdx,%rax)
 addq $128,%rax
 cmpq $16384,%rax
 jb .Lgp_packet
 vzeroupper
 ret
.size asian_genuine_sql_variable_diag,.-asian_genuine_sql_variable_diag

.p2align 6
.globl asian_genuine_sql_dual_control_diag
.type asian_genuine_sql_dual_control_diag,@function
asian_genuine_sql_dual_control_diag:
 kmovq %rdi,%k2
 kmovq %rdx,%k3
 kmovd %esi,%k1
 xorq %rax,%rax
.Lgd_packet:
 kmovq %k3,%rdx
 vmovaps 0(%rdx,%rax),%zmm4
 vmovaps 64(%rdx,%rax),%zmm5
 vmovaps 16384(%rdx,%rax),%zmm6
 vmovaps 16448(%rdx,%rax),%zmm7
 vmovaps 32768(%rdx,%rax),%zmm10
 vmovaps 32832(%rdx,%rax),%zmm11
 movq %rax,%rcx
 shrq $7,%rcx
 kmovq %rax,%k7
 kmovq %k2,%rdi
 kmovd %k1,%eax
.Lgd_route:
 movq 0(%rdi),%r8
 movq 8(%rdi),%rsi
 kmovq %rsi,%k5
 movq 16(%rdi),%r9
 movzbq (%r9,%rcx,4),%r11
 movzbq 1(%r9,%rcx,4),%rsi
 movzbq 2(%r9,%rcx,4),%rdx
 movzbq 3(%r9,%rcx,4),%r10
 shlq $6,%r11
 shlq $6,%rsi
 shlq $6,%rdx
 shlq $6,%r10
 vmovdqa32 (%r8,%r11),%zmm2
 vmovdqa32 (%r8,%rsi),%zmm3
	vmovdqa32 576(%r9,%rdx),%zmm18
	vmovdqa32 576(%r9,%r10),%zmm19
	vpermd %zmm2,%zmm18,%zmm0
	vpermd %zmm3,%zmm19,%zmm1
 kmovq %k5,%r8
 vmovdqa32 (%r8,%r11),%zmm14
 vmovdqa32 (%r8,%rsi),%zmm15
	vpermd %zmm14,%zmm18,%zmm12
	vpermd %zmm15,%zmm19,%zmm13
 vmulps %zmm12,%zmm4,%zmm4
 vmulps %zmm13,%zmm5,%zmm5
 vaddps %zmm4,%zmm6,%zmm6
 vaddps %zmm5,%zmm7,%zmm7
 vfmadd231ps 24(%rdi){1to16},%zmm0,%zmm10
 vfmadd231ps 24(%rdi){1to16},%zmm1,%zmm11
 addq $32,%rdi
 decl %eax
 jne .Lgd_route
 kmovq %k3,%rdx
 kmovq %k7,%rax
 vmovaps %zmm4,0(%rdx,%rax)
 vmovaps %zmm5,64(%rdx,%rax)
 vmovaps %zmm6,16384(%rdx,%rax)
 vmovaps %zmm7,16448(%rdx,%rax)
 vmovaps %zmm10,32768(%rdx,%rax)
 vmovaps %zmm11,32832(%rdx,%rax)
 addq $128,%rax
 cmpq $16384,%rax
 jb .Lgd_packet
 vzeroupper
 ret
.size asian_genuine_sql_dual_control_diag,.-asian_genuine_sql_dual_control_diag

.p2align 6
.globl asian_genuine_sql_growth_log5_diag
.type asian_genuine_sql_growth_log5_diag,@function
asian_genuine_sql_growth_log5_diag:
	kmovq %rdi,%k2
	kmovq %rdx,%k3
	kmovd %esi,%k1
	xorq %rax,%rax
.Lgl5_packet:
	kmovq %k3,%rdx
	vmovaps 0(%rdx,%rax),%zmm4
	vmovaps 64(%rdx,%rax),%zmm5
	vmovaps 16384(%rdx,%rax),%zmm6
	vmovaps 16448(%rdx,%rax),%zmm7
	vmovaps 32768(%rdx,%rax),%zmm10
	vmovaps 32832(%rdx,%rax),%zmm11
	movq %rax,%rcx
	shrq $7,%rcx
	kmovq %rax,%k7
	kmovq %k2,%rdi
	kmovd %k1,%eax
.Lgl5_route:
	movq 8(%rdi),%r8
	movq 16(%rdi),%r9
	movzbq (%r9,%rcx,4),%r11
	movzbq 1(%r9,%rcx,4),%rsi
	movzbq 2(%r9,%rcx,4),%rdx
	movzbq 3(%r9,%rcx,4),%r10
	shlq $6,%r11
	shlq $6,%rsi
	shlq $6,%rdx
	shlq $6,%r10
	vmovdqa32 (%r8,%r11),%zmm14
	vmovdqa32 (%r8,%rsi),%zmm15
	vmovdqa32 576(%r9,%rdx),%zmm18
	vmovdqa32 576(%r9,%r10),%zmm19
	vpermd %zmm14,%zmm18,%zmm12
	vpermd %zmm15,%zmm19,%zmm13
	vsubps .Llog_one(%rip){1to16},%zmm12,%zmm0
	vsubps .Llog_one(%rip){1to16},%zmm13,%zmm1
	vbroadcastss .Llog_c5(%rip),%zmm2
	vmovaps %zmm2,%zmm3
	vfmadd213ps .Llog_c4(%rip){1to16},%zmm0,%zmm2
	vfmadd213ps .Llog_c4(%rip){1to16},%zmm1,%zmm3
	vfmadd213ps .Llog_c3(%rip){1to16},%zmm0,%zmm2
	vfmadd213ps .Llog_c3(%rip){1to16},%zmm1,%zmm3
	vfmadd213ps .Llog_c2(%rip){1to16},%zmm0,%zmm2
	vfmadd213ps .Llog_c2(%rip){1to16},%zmm1,%zmm3
	vmulps %zmm0,%zmm0,%zmm8
	vmulps %zmm1,%zmm1,%zmm9
	vfmadd231ps %zmm2,%zmm8,%zmm0
	vfmadd231ps %zmm3,%zmm9,%zmm1
	vmulps %zmm12,%zmm4,%zmm4
	vmulps %zmm13,%zmm5,%zmm5
	vaddps %zmm4,%zmm6,%zmm6
	vaddps %zmm5,%zmm7,%zmm7
	vfmadd231ps 24(%rdi){1to16},%zmm0,%zmm10
	vfmadd231ps 24(%rdi){1to16},%zmm1,%zmm11
	addq $32,%rdi
	decl %eax
	jne .Lgl5_route
	kmovq %k3,%rdx
	kmovq %k7,%rax
	vmovaps %zmm4,0(%rdx,%rax)
	vmovaps %zmm5,64(%rdx,%rax)
	vmovaps %zmm6,16384(%rdx,%rax)
	vmovaps %zmm7,16448(%rdx,%rax)
	vmovaps %zmm10,32768(%rdx,%rax)
	vmovaps %zmm11,32832(%rdx,%rax)
	addq $128,%rax
	cmpq $16384,%rax
	jb .Lgl5_packet
	vzeroupper
	ret
.size asian_genuine_sql_growth_log5_diag,.-asian_genuine_sql_growth_log5_diag

.p2align 6
.globl asian_genuine_sql_x_expm1_4_diag
.type asian_genuine_sql_x_expm1_4_diag,@function
asian_genuine_sql_x_expm1_4_diag:
	kmovq %rdi,%k2
	kmovq %rdx,%k3
	kmovq %rcx,%k4
	kmovd %esi,%k1
	xorq %rax,%rax
.Lxe4_packet:
	kmovq %k3,%rdx
	vmovaps 0(%rdx,%rax),%zmm4
	vmovaps 64(%rdx,%rax),%zmm5
	vmovaps 16384(%rdx,%rax),%zmm6
	vmovaps 16448(%rdx,%rax),%zmm7
	vmovaps 32768(%rdx,%rax),%zmm10
	vmovaps 32832(%rdx,%rax),%zmm11
	movq %rax,%rcx
	shrq $7,%rcx
	kmovq %rax,%k7
	kmovq %k2,%rdi
	kmovd %k1,%eax
.Lxe4_route:
	movq 0(%rdi),%r8
	movq 16(%rdi),%r9
	movzbq (%r9,%rcx,4),%r11
	movzbq 1(%r9,%rcx,4),%rsi
	movzbq 2(%r9,%rcx,4),%rdx
	movzbq 3(%r9,%rcx,4),%r10
	shlq $6,%r11
	shlq $6,%rsi
	shlq $6,%rdx
	shlq $6,%r10
	vmovdqa32 (%r8,%r11),%zmm14
	vmovdqa32 (%r8,%rsi),%zmm15
	vmovdqa32 576(%r9,%rdx),%zmm18
	vmovdqa32 576(%r9,%r10),%zmm19
	vpermd %zmm14,%zmm18,%zmm0
	vpermd %zmm15,%zmm19,%zmm1
	kmovq %k4,%r8
	vsubps 16(%r8){1to16},%zmm0,%zmm12
	vsubps 16(%r8){1to16},%zmm1,%zmm13
	vbroadcastss 36(%r8),%zmm2
	vmovaps %zmm2,%zmm3
	vfmadd213ps 32(%r8){1to16},%zmm12,%zmm2
	vfmadd213ps 32(%r8){1to16},%zmm13,%zmm3
	vfmadd213ps 28(%r8){1to16},%zmm12,%zmm2
	vfmadd213ps 28(%r8){1to16},%zmm13,%zmm3
	vfmadd213ps 24(%r8){1to16},%zmm12,%zmm2
	vfmadd213ps 24(%r8){1to16},%zmm13,%zmm3
	vfmadd213ps 20(%r8){1to16},%zmm12,%zmm2
	vfmadd213ps 20(%r8){1to16},%zmm13,%zmm3
	vfmadd231ps %zmm2,%zmm4,%zmm4
	vfmadd231ps %zmm3,%zmm5,%zmm5
	vaddps %zmm4,%zmm6,%zmm6
	vaddps %zmm5,%zmm7,%zmm7
	vfmadd231ps 24(%rdi){1to16},%zmm0,%zmm10
	vfmadd231ps 24(%rdi){1to16},%zmm1,%zmm11
	addq $32,%rdi
	decl %eax
	jne .Lxe4_route
	kmovq %k3,%rdx
	kmovq %k7,%rax
	vmovaps %zmm4,0(%rdx,%rax)
	vmovaps %zmm5,64(%rdx,%rax)
	vmovaps %zmm6,16384(%rdx,%rax)
	vmovaps %zmm7,16448(%rdx,%rax)
	vmovaps %zmm10,32768(%rdx,%rax)
	vmovaps %zmm11,32832(%rdx,%rax)
	addq $128,%rax
	cmpq $16384,%rax
	jb .Lxe4_packet
	vzeroupper
	ret
.size asian_genuine_sql_x_expm1_4_diag,.-asian_genuine_sql_x_expm1_4_diag

.section .rodata
.p2align 2
.Llog_one: .long 0x3f800000
.Llog_c2:  .long 0xbf000000
.Llog_c3:  .long 0x3eaaaaab
.Llog_c4:  .long 0xbe800000
.Llog_c5:  .long 0x3e4ccccd
.section .note.GNU-stack,"",@progbits
