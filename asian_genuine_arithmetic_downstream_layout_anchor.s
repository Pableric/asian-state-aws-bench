.section .rodata.asian_genuine_arithmetic_downstream_layout_anchor,"a",@progbits
.type .Ldownstream_layout_anchor_data,@object
.Ldownstream_layout_anchor_data:
    .zero 296
.size .Ldownstream_layout_anchor_data,.-.Ldownstream_layout_anchor_data

/*
 * Keep the linked RIP-relative bytes of the already-qualified immediate
 * leaves stable while the diagnostic benchmark and fused objects grow.
 * Its cold 91-byte text and 296-byte read-only pad are fixed linked-layout
 * inputs.  The text is called once before any correctness or timing.
 */
.section .text.asian_genuine_arithmetic_downstream_layout_anchor,"ax",@progbits
.global asian_genuine_arithmetic_downstream_layout_anchor
.hidden asian_genuine_arithmetic_downstream_layout_anchor
.type asian_genuine_arithmetic_downstream_layout_anchor,@function
asian_genuine_arithmetic_downstream_layout_anchor:
    leaq .Ldownstream_layout_anchor_data(%rip),%rax
    .fill 83,1,0x90
    ret
.size asian_genuine_arithmetic_downstream_layout_anchor,.-asian_genuine_arithmetic_downstream_layout_anchor

.section .note.GNU-stack,"",@progbits
