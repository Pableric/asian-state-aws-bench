# Dim-from-D1 packet provider

This package is a dest-order permutation fragment: a 4096-value D1 block in, one 32-value destination packet out. It is not a random-number generator and does not include direction numbers, pricing, or an engine loop.

The private engine compiles **three kernels**. Each kernel names one packet macro. There is no runtime `#define DIM_PROVIDER_KIND` switch inside the packet loop.

## Macros

| Macro | Role |
| --- | --- |
| `DIM_PROVIDER_PACKET_GENERIC` | Per-packet generic path (no preload) |
| `DIM_PROVIDER_PRELOAD_AFFINE` | Load live `base_control` and `half_delta` |
| `DIM_PROVIDER_PACKET_AFFINE` | Per-packet affine path |
| `DIM_PROVIDER_PRELOAD_RES2XOR` | Load two live masks; `live_c = 0` |
| `DIM_PROVIDER_PACKET_RES2XOR` | Per-packet dest-order resident-2 XOR path |

Operands are explicit. Vector operands are ZMM numbers `0..31`. GPR operands are names without `%`. Affine and res2xor 32-bit accesses append `d` to the operand (`r8` → `%r8d`), so those GPRs must be `r8`–`r15`.

### Clobbers and aliases

**GENERIC packet** (`out_a, out_b, scratch_a, scratch_b, d1_base, packet, map, g0, g1, g2, g3`)

- Clobbers: `out_*`, `scratch_*`, `g0`–`g3`
- Preserves: `d1_base`, `packet`, `map`
- Distinct: all ZMM operands; `g0`–`g3` distinct and not `d1_base` / `packet` / `map`

**AFFINE preload** (`base_ctrl, half_delta, map`)

- Clobbers: `base_ctrl`, `half_delta`
- Preserves: `map`

**AFFINE packet** (`out_a, out_b, scratch_a, scratch_b, base_ctrl, half_delta, d1_base, packet, map, g0, g1`)

- Clobbers: `out_*`, `scratch_*`, `g0`, `g1`
- Preserves: `base_ctrl`, `half_delta`, `d1_base`, `packet`, `map`

**RES2XOR preload** (`mask_a, mask_b, live_c, map`)

- Clobbers: `mask_a`, `mask_b`, `live_c`
- Preserves: `map`

**RES2XOR packet** (`out_a, out_b, scratch_a, scratch_b, mask_a, mask_b, live_c, d1_base, packet, map, g0, g1`)

- Clobbers: `out_*`, `scratch_*`, `g0`, `g1`, `mask_*` (in place), `live_c`
- Preserves: `d1_base`, `packet`, `map`

Live bases (`d1_base`, `packet`, `map`) are never written. Do not alias a clobber onto a preserved base.

Fragments contain no calls, stores, gathers, stack operands, or hidden spills. Test and bench wrappers may store results after the fragment.

## Dest-order semantics

For every variant and every `p = 0..127`:

- Consume the caller 4096-float32 D1 block (64-byte aligned). Values are treated as 32-bit patterns.
- Produce destination values `32p .. 32p+31` in two caller-selected ZMMs (16+16 lanes).
- Bit-exact versus the source-index oracle: `out[lane] = d1[source[p*32 + lane]]`.
- Destination packet order, not a Gray walk of the block.
- Only explicitly supplied registers.

`resident2_xor` here is a **per-packet dest-order** fragment: two live masks, `vpermd`, and a D1 line from prepared `sel2[p]`. Mask XOR runs when `sel2[p].c_a` differs from the live scalar. Sequential `p = 0..127` is the supported hot order. Consecutive dest-window constancy of `c_a` is a performance property of some maps, not a prepare requirement.

## Prepare

Input is only `const uint16_t source[4096]`, a permutation of `0..4095`.

| Function | Context | Success condition |
| --- | --- | --- |
| `dim_provider_prepare_generic` | 1600 B | Each dest packet is two 16-lane D1 lines; at most 16 interned lane patterns |
| `dim_provider_prepare_affine` | 448 B | Generic structure plus `line_b = line_a XOR 1` and XOR-family controls |
| `dim_provider_prepare_res2xor` | 448 B (same layout) | Same two-mask affine representation. Dest-order `c_a` need not be constant on consecutive dest windows; the packet fragment XOR-updates live masks when `c_a` changes. |

On failure: return `-1`. Output context is **undefined**. Prepare never fabricates a nearby supported map. Unsupported dimensions are the engine’s problem.

Generic layout: `select[p] = {line_a, line_b, pat_a, pat_b}`, patterns at byte 576.

Affine / res2xor layout: `sel2[p] = {line_a, c_a}`, `base_control` at 320, broadcast `delta` at 384.

## Tests and audit

Correctness uses one sequence: `p = 0,1,...,127` for all three variants versus the scalar oracle, every lane. Additional checks: first/last packet, 16/32/64-value boundaries, repeated invocation, a second instantiation with different physical ZMM/GPR numbers, declared preservation, and prepare failure on maps that are not line-structured / not affine / not res2xor.

Object audit instantiates a one-packet probe and a complete sequential 128-packet loop per variant. It reports scalar loads and 64-byte ZMM loads separately and rejects calls, stores, gathers, stack references, and undeclared clobbers on those symbols.

## Performance

Time preload once plus 128 dest-order packets, not the two-load two-`vpermd` core alone. Native cycle numbers require `avx512f`. This packaging host may only run correctness under an emulator and static audit.

Historical Gray-block consume numbers (different walk) live in `AWS_NATIVE_REPORT.md` and are not dest-order measurements of this package.
