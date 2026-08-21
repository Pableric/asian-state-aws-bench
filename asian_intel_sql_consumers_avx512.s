.include "private/asian_exp_p8_18diag.inc"
.section .rodata
.p2align 6
.Lidx: .long 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
.Llog2e: .long 0x3fb8aa3b
.Lln2hi: .long 0x3f318000
.Lln2lo: .long 0xb95e8083
.section .text
.macro EX z,out,e,r
 vmulps .Llog2e(%rip){1to16},%zmm\z,%zmm\e
 vrndscaleps $0,%zmm\e,%zmm\e
 vmovaps %zmm\z,%zmm\r
 vfnmadd231ps .Lln2hi(%rip){1to16},%zmm\e,%zmm\r
 vfnmadd231ps .Lln2lo(%rip){1to16},%zmm\e,%zmm\r
 ASIAN_EXP_P8_18DIAG \r,\out
 vscalefps %zmm\e,%zmm\out,%zmm\out
.endm
.macro STATE_LOAD base,off
 vmovaps 0(%\base,%\off),%zmm4;vmovaps 64(%\base,%\off),%zmm5
 vmovaps 16384(%\base,%\off),%zmm6;vmovaps 16448(%\base,%\off),%zmm7
 vmovaps 32768(%\base,%\off),%zmm10;vmovaps 32832(%\base,%\off),%zmm11
.endm
.macro STEP x0,x1,g0,g1,w
 vmulps %zmm\g0,%zmm4,%zmm4;vmulps %zmm\g1,%zmm5,%zmm5
 vaddps %zmm4,%zmm6,%zmm6;vaddps %zmm5,%zmm7,%zmm7
 vfmadd231ps (%\w){1to16},%zmm\x0,%zmm10
 vfmadd231ps (%\w){1to16},%zmm\x1,%zmm11
.endm
.macro STATE_STORE base,off
 vmovaps %zmm4,0(%\base,%\off);vmovaps %zmm5,64(%\base,%\off)
 vmovaps %zmm6,16384(%\base,%\off);vmovaps %zmm7,16448(%\base,%\off)
 vmovaps %zmm10,32768(%\base,%\off);vmovaps %zmm11,32832(%\base,%\off)
.endm
.p2align 6
.globl asian_intel_dimension_major_sql_diag
.type asian_intel_dimension_major_sql_diag,@function
asian_intel_dimension_major_sql_diag:
 kmovd %edx,%k1;kmovq %rdi,%k2;kmovq %rsi,%k3;kmovq %rcx,%k4;kmovq %r8,%k5
 xorq %rax,%rax
1: kmovq %k5,%r8;STATE_LOAD r8,rax
 kmovq %k2,%rdi;kmovq %k3,%rsi;kmovq %k4,%rcx;kmovd %k1,%edx
2: vmovaps (%rdi,%rax),%zmm0;vmovaps 64(%rdi,%rax),%zmm1
 vmovaps (%rsi,%rax),%zmm12;vmovaps 64(%rsi,%rax),%zmm13
 STEP 0,1,12,13,rcx
 addq $16384,%rdi;addq $16384,%rsi;addq $4,%rcx;decl %edx;jne 2b
 kmovq %k5,%r8;STATE_STORE r8,rax
 addq $128,%rax;cmpq $16384,%rax;jb 1b;vzeroupper;ret
.size asian_intel_dimension_major_sql_diag,.-asian_intel_dimension_major_sql_diag

.p2align 6
.globl asian_intel_point_major_sql_diag
.type asian_intel_point_major_sql_diag,@function
asian_intel_point_major_sql_diag:
 kmovd %esi,%k1;kmovq %rdi,%k2;kmovq %rdx,%k3;kmovq %rcx,%k4
 movl %esi,%r9d;shll $2,%r9d;vpbroadcastd %r9d,%zmm24
 vpmulld .Lidx(%rip),%zmm24,%zmm25
 xorq %rax,%rax
3: kmovq %k4,%rcx;STATE_LOAD rcx,rax
 kmovq %k2,%rdi;kmovq %k3,%rdx;kmovd %k1,%esi
 movq %rax,%r8;shrq $7,%r8;imull %r9d,%r8d;shll $5,%r8d;addq %r8,%rdi
 xorl %r10d,%r10d
4: vpbroadcastd %r10d,%zmm26;vpaddd %zmm26,%zmm25,%zmm27
 kxnorw %k6,%k6,%k6;vgatherdps (%rdi,%zmm27),%zmm0{%k6}
 movq %r9,%r11;shlq $4,%r11;addq %rdi,%r11;kxnorw %k6,%k6,%k6;vgatherdps (%r11,%zmm27),%zmm1{%k6}
 EX 0,12,14,16;EX 1,13,15,17;STEP 0,1,12,13,rdx
 addl $4,%r10d;addq $4,%rdx;decl %esi;jne 4b
 kmovq %k4,%rcx;STATE_STORE rcx,rax
 addq $128,%rax;cmpq $16384,%rax;jb 3b;vzeroupper;ret
.size asian_intel_point_major_sql_diag,.-asian_intel_point_major_sql_diag
.section .note.GNU-stack,"",@progbits
