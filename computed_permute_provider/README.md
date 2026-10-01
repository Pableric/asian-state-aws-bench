# Computed-permute provider, handoff v1

This is a self-contained package for one component: **donor ZMMs → destination-ordered
dimension ZMMs**. It replaces the per-(block, dimension) 448-byte affine context of the existing
provider with a 192-byte record per dimension. The record is valid for every block. The routing
for block B and packet p is computed in registers.

Nothing in the engine is modified. The existing affine provider is included as a frozen,
hash-identified copy. It serves as the correctness oracle and as benchmark variant A.

Out of scope, neither changed nor decided here: banked vs streaming donor generation, the
Gaussian approximation, direction numbers, autocall payoff, financial reduction, and production
integration.

## Layout

| Path | Content |
|---|---|
| `include/cpp_provider.h` | Public C ABI: record layout, prepare, block/packet seek, scalar record model |
| `asm/cpp_provider_fragments.inc` | The provider: five GAS macros (register contract below) |
| `src/cpp_provider_prepare.c` | Record generator (direction numbers → record constants) |
| `src/cpp_directions.c`, `include/cpp_directions.h` | Loader for `data/joe_kuo_d1280.bin` |
| `tools/gen_directions.py` | Regenerates `data/joe_kuo_d1280.bin` and cross-checks the engine's bin |
| `data/` | Joe–Kuo `new-joe-kuo-6.21201` rows D2..D1280 (text) and the derived 1280 × 33 u32 bin |
| `baseline_affine_frozen/` | Exact copy of the 448-byte affine provider and its upstream `SHA256SUMS` |
| `tests/oracle_scalar.c` | Independent scalar source-index oracle (brute-force inverse, no record) |
| `tests/test_correctness.c`, `tests/test_wrappers.S` | Correctness test, asm wrappers, register-contract probes |
| `tools/run_extended.py`, `tools/run_negative_controls.py` | Sharded D1..D1280 × 256-block run; injected-fault run |
| `bench/` | Matched A/B benchmark (driver, kernels, consumers) |
| `audit/` | Boundary instantiation and objdump-based instruction audit |
| `tools/collect_results.py` | Merges everything into `results/handoff_results.json` with file hashes |
| `results/` | Outputs of the runs listed below |

## Build and test

Requirements: x86-64 Linux, gcc (C11, GNU as), Python ≥ 3.9, objdump. Without native AVX-512F,
all execution goes through Intel SDE (`SDE=/opt/intel-sde/sde64`, CPU `-spr`). The Makefile
selects this automatically.

```sh
make all                  # build/test_correctness, build/bench_ab, build/audit_instantiate.o
make test                 # contract + canonical (D1..D256 x 16 blocks) + far blocks + 2^20-block state sweep
make test-extended JOBS=10  # D1..D1280 x blocks 0..255, every lane, sharded
make negative             # 7 injected faults, each must fail
make audit                # objdump instruction/memory/register audit
make bench-verify         # A == B bitwise through both bench consumers (no timing; SDE is fine)
make bench                # native A/B timing; refuses (status.json, timed=false) without native avx512f
make results              # results/handoff_results.json
make check                # all of the above in order
```

Regenerating the constants:

```sh
python3 tools/gen_directions.py --bin ../direction_numbers/joe_kuo_6_21201.bin \
    --text data/new-joe-kuo-6.21201.d2-d1280.txt --dims 1280 --out data/joe_kuo_d1280.bin
(cd baseline_affine_frozen && sha256sum -c --ignore-missing UPSTREAM_SHA256SUMS)
```

`gen_directions.py` derives V from the Joe–Kuo text through the standard recurrence. It requires
bit-exact agreement with the engine's `direction_numbers/joe_kuo_6_21201.bin` (sha256
`fa6418f2…`, present at the base commit) for all 1280 dimensions. The per-dimension records are
then built at run time by `cpp_prepare_dimension`. They are not stored files. The test writes
them out once as `results/records_d1280.bin` for hashing.

## Mathematics

Notation: \(g(n) = n \oplus (n \gg 1)\). The unscrambled Joe–Kuo Sobol word of dimension d is
\(X_d(n) = \bigoplus_j g(n)_j V_d[j]\), and its 1/4096 cell is \(\mathrm{cell}_d(n) = X_d(n) \gg 20\).
D1 is van der Corput: \(V_1[j] = 2^{31-j}\).

**Ordering implemented.** Target block B of dimension d is a permutation of D1 block B, the
donor for every dimension. Destination k (0..4095), i.e. point 4096B + k, receives the D1 block-B
value whose 1/4096 cell equals \(\mathrm{cell}_d(4096B+k)\). Every output therefore lies in the
same 1/4096 cell as the canonical Sobol value. Below 1/4096 the value comes from D1, so the
result is not canonical Sobol there.

**Donor-side inverse.** For \(n = 4096B + o\):
\(\mathrm{cell}_1(n) = \mathrm{bitrev}_{12}(g_{12}(o) \oplus [B\ \mathrm{odd}]\,2^{11})\).
This is a bijection on 0..4095, so each D1 block holds exactly one value per cell. Inverting
gives \(o = F(c) \oplus [B\ \mathrm{odd}] \cdot \mathtt{0xFFF}\), with
\(F(c) = G_{12}^{-1}(\mathrm{bitrev}_{12}(c))\). (\(G^{-1}\) is the prefix XOR, and
\(G^{-1}(2^{11}) = \mathtt{0xFFF}\).)

**Source rule, linear in k and in B.** Because 4096B and k have disjoint bits,
\(g(4096B + k) = g(4096B) \oplus g(k)\), so \(X_d(4096B+k) = X_d(4096B) \oplus X_d(k)\). Hence

\[ s_{d,B}(k) = A_d k \oplus c_d(B), \qquad A_d k = F(\mathrm{top}_{12} X_d(k)), \qquad
c_d(B) = F(\mathrm{top}_{12} X_d(4096B)) \oplus [B\ \mathrm{odd}] \cdot \mathtt{0xFFF}. \]

Both \(A_d\) (12×12 over GF(2)) and \(c_d\) (GF(2)-linear in the 20 bits of B) are linear.
Column i of \(A_d\) is \(\mathrm{col}_i = F(\mathrm{top}_{12}(V_d[i] \oplus V_d[i-1]))\), because
\(g(2^i) = 2^i \oplus 2^{i-1}\).

**Structure lemma.** For Joe–Kuo numbers, \(V_d[j] = m_j 2^{31-j}\) with \(m_j\) odd, so the lowest
set bit of \(V_d[j]\) is bit 31−j. Hence the top-12 cell of \(V[i] \oplus V[i-1]\) has its lowest
set bit at 11−i. After bitrev12 its highest set bit is i, and \(G^{-1}\) preserves the highest
bit. So \(\mathrm{col}_i\) has highest set bit exactly i. In particular
\(\mathrm{col}_0..\mathrm{col}_3 < 16\) and \(\mathrm{col}_4 = \mathtt{0x10} \mid \delta\) with
\(\delta < 16\). `cpp_prepare_dimension` checks these facts for each dimension and returns −1 if
they fail. All of D1..D1280 pass.

**Packet decomposition.** Write k = 32p + 16h + l (packet p, half h, lane l). Let
\(\mathrm{ctl}[l] = \bigoplus_{b<4} l_b\,\mathrm{col}_b\) (< 16) and
\(u_B(p) = A_d(32p) \oplus c_d(B)\). Split u into line = u ≫ 4 (a 64-byte donor line) and
lane_xor = u & 15. Then

\[ s(32p + 16h + l) = 16\,(\mathrm{line} \oplus h) + (\mathrm{ctl}[l] \oplus \mathrm{lane\_xor} \oplus h\delta). \]

Each half of a packet therefore reads one aligned 16-float donor line and permutes it within the
line. That is one `vpermd` with a memory table operand: line ga = 64·line for h = 0 and
gb = ga ⊕ 64 for h = 1, with index vector ctl ⊕ lane_xor (⊕ δ for h = 1). There are no gathers
and no cross-line traffic.

**Packet recurrence.** \(u(p) \oplus u(p-1) = A_d(32 (p \oplus (p-1)))\), and
\(p \oplus (p-1) = 2^{t+1}-1\) with t = ctz p. So entering packet p XORs
\(D[t] = \mathrm{col}_5 \oplus \dots \oplus \mathrm{col}_{5+t}\), for t = 0..6. These are seven
per-dimension constants.

**Block recurrence.** \(c(B) \oplus c(B-1) = c(2^{t+1}-1) =: C[t]\), with t = ctz B, for t = 0..19.
Here \(C[t] = \bigoplus_{j \le t} c(2^j)\) and \(c(2^j) = F(\mathrm{top}_{12}(V[12+j] \oplus V[11+j]))
\oplus [j = 0] \cdot \mathtt{0xFFF}\). These are twenty per-dimension constants. Block 0 has
state 0. For random entry, \(c(B) = \bigoplus_{j:\ g(B)_j = 1} C[j]\) (`cpp_block_state`). This
holds because \(\bigoplus_j g(B)_j (2^{j+1}-1) = B\). The packet seek (`cpp_packet_state`) is
analogous.

**Encodings (all GF(2)-linear, so the XOR recurrences hold on them).**
`daddr[t] = (D[t] >> 4) << 6` (donor byte offset), `dlane[t] = D[t] & 15`, and block state
`cst = (((c >> 4) << 6) << 16) | (c & 15)`. `CPP_BLOCK_ENTER` takes `ga = cst >> 16` and
broadcasts `cst` into the lane vector. The resulting bits 16..31 of each lane are ignored, because
`vpermd` uses only the low 4 index bits.

**Why storage does not grow with N.** \(c_d\) is linear in B, so 20 constants \(C[0..19]\)
determine \(c_d(B)\) for every B < 2^20. The same holds for 7 constants per packet and 16 + 1
lane constants. Every block uses the same immutable record. Block state is one 32-bit register
value that follows the recurrence above. There are no per-block or per-(block, dimension)
constants or tables. The bound B < 2^20 comes from 32-bit direction numbers (2^32 points). It
is fixed in the record format; it is not a function of N.

## Exact contract

### Inputs and outputs

| Item | Form |
|---|---|
| Donor base | 64-bit GPR `d1` → D1 block B: 4096 FP32 in natural Sobol index order (offset o = point 4096B + o), 64-byte aligned, 16 KiB contiguous. The caller selects block B; the provider never routes to another block. |
| Dimension | 64-bit GPR `rec` → `cpp_dim_record_t` of that dimension |
| Target block ordinal B | 32-bit GPR `blk` (1 ≤ B < 2^20) consumed by `CPP_BLOCK_ADVANCE`; or `cpp_block_state(rec, B)` in C for random entry |
| Packet ordinal p | 0: `CPP_PACKET_FIRST`; 1..127: 32-bit GPR `p` in `CPP_PACKET_NEXT`, which must follow packet p−1 of the same (dimension, block) |
| Compact per-dimension state | the 192-byte immutable record, plus the 32-bit block state `cst` (mutable, caller-owned) |
| Output | ZMM `out_a` = destinations 32p+0..15 (lane l = destination 32p+l); ZMM `out_b` = destinations 32p+16..31. Values are FP32 bit patterns copied exactly from the donor; no arithmetic is applied. |

### Record layout and alignment

`cpp_dim_record_t`: `ctl[16]` u8 @0, `half` u32 @16, `dimension` @20, `version` @24
(`0x43505031`), `daddr[8]` u32 @32, `dlane[8]` u32 @64, `cstep[20]` u32 @96, reserved to 192.
`daddr[7]` and `dlane[7]` are zero and unused.

Each record is **individually 64-byte aligned**, and `cpp_prepare_dimension` rejects a misaligned
`out`. Arrays of records are **packed back to back with a stride of 192 bytes**, which is a
multiple of 64, so every element stays 64-byte aligned. Each record spans three cache lines:

- the per-packet loads touch line 0 (`daddr`) and line 1 (`dlane`);
- `PRELOAD` touches line 0;
- `ADVANCE` touches lines 1–2.

### Register roles per macro

ZMM operands are numbers 0..31. `d1` and `rec` are any 64-bit GPR. `p`, `blk`, `cst`, `ga`, `gb`
and `t` must be r8..r15, because they are used through their `d` names. All 32-bit writes zero
bits 63..32. `ADVANCE`, `ENTER` and `NEXT` write RFLAGS; `PRELOAD` and `FIRST` do not. No macro
touches k0–k7, MXCSR, memory (no stores) or the stack, and none uses `vzeroupper`. Any register
not listed is preserved.

| Macro | Reads (preserved) | Writes | Updates (read+write) | Clobbers |
|---|---|---|---|---|
| `CPP_PRELOAD_DIM base, half, rec` | `rec` | `zmm base` = zx(ctl[0..15]), `zmm half` = bcast(half) | — | — |
| `CPP_BLOCK_ADVANCE cst, rec, blk, t` (B ≥ 1) | `rec`, `blk` | — | `cst ^= cstep[ctz B]` | `t`, flags |
| `CPP_BLOCK_ENTER live, base, cst, ga, gb` | `zmm base`, `cst` | `zmm live`, `ga`, `gb` | — | flags |
| `CPP_PACKET_FIRST oa, ob, live, half, d1, ga, gb` (p = 0) | `zmm live`, `zmm half`, `d1`, `ga`, `gb` | `zmm oa`, `zmm ob` | — | — |
| `CPP_PACKET_NEXT oa, ob, live, half, d1, rec, p, ga, gb, t` (p = 1..127) | `zmm half`, `d1`, `rec`, `p` | `zmm oa`, `zmm ob` | `zmm live`, `ga`, `gb` | `t`, flags |

Aliasing rules:

- Within one invocation, all GPR operands must be distinct and all ZMM operands must be
  distinct. For example, `live` ≠ `base` in `ENTER`, `ga`/`gb` ≠ `cst`, and `t` ≠ `cst` in
  `ADVANCE`.
- Across the sequence, `live`, `half`, `ga` and `gb` must survive from `ENTER` / `FIRST` until
  the last `NEXT` of the block.
- `base` may be reused after `ENTER` if the dimension's next block re-runs `PRELOAD`.
- `cst` may be reused as `t` after `ENTER` if the caller has saved it (the bench does this).

Typical sequence for one (dimension, block):

```asm
CPP_BLOCK_ADVANCE r10, r15, r8, r9        # only if B >= 1; r10 holds cst of block B-1
CPP_PRELOAD_DIM   16, 17, r15             # may be hoisted when blocks are the inner loop
CPP_BLOCK_ENTER   18, 16, r10, r9, r11
CPP_PACKET_FIRST  0, 1, 18, 17, r14, r9, r11
#   consume zmm0, zmm1
movl $1, %r8d
1: CPP_PACKET_NEXT 0, 1, 18, 17, r14, r15, r8, r9, r11, r10
#   consume zmm0, zmm1
addl $1, %r8d ; cmpl $128, %r8d ; jne 1b
```

The baseline (A) is `DIM_PROVIDER_PRELOAD_AFFINE` / `DIM_PROVIDER_PACKET_AFFINE` in
`baseline_affine_frozen/`, with the contract in its `CONTRACT.md`. In this package A receives
contexts that `dim_provider_prepare_affine` prepares from the oracle's same-block source orders.

## Storage

| | fixed | per dimension | per block | per block × dimension |
|---|---:|---:|---:|---:|
| B computed (this provider) | 0 B | 192 B immutable (+4 B mutable block state, caller-owned, only when dimensions are the inner loop) | **0 B** | **0 B** |
| A affine baseline | 0 B | 0 B | 0 B | 448 B |

Totals:

| Scope | B | A |
|---|---:|---:|
| D256 × 16 blocks | 49,152 B | 1,835,008 B |
| D1280 × 256 blocks | 245,760 B | 146,800,640 B |

For any N ≤ 2^32 points, B stays at 192 B per dimension. Donor blocks are caller input for both
A and B and are excluded from these figures. Code bytes are listed below.

## Instruction accounting (compiled disassembly)

All counts come from `objdump -d -w` of `build/audit_instantiate.o` and the marked loop bodies in
`build/bench_kernels.o` (`results/audit.json`, `results/audit_disassembly.txt`). Each boundary is
assembled as its own symbol with the bench's register assignment; the closing `ret` is excluded.

| Boundary | Instr. | Vector | Scalar | Loads (logical bytes) | Stores | Code bytes | RAW chain | ZMM | GPR |
|---|---:|---:|---:|---|---:|---:|---:|---|---|
| A preload (per context) | 2 | 2 | 0 | 2 (128) | 0 | 14 | 1 | 16,17 | r15 |
| A context select (caller) | 5 | 0 | 5 | 2 (16) | 0 | 22 | 5 | — | rbx,r12,r13,r15 |
| **A packet** | **11** | 7 | 4 | 4 (130) | 0 | 63 | 5 | 0,1,2,3,16,17 | r14,r15,r8,r9,rsi |
| B preload (per dimension) | 2 | 2 | 0 | 2 (20) | 0 | 13 | 1 | 16,17 | r15 |
| B record select (caller) | 2 | 0 | 2 | 1 (8) | 0 | 11 | 2 | — | rbx,r13,r15 |
| B block advance | 2 | 0 | 2 | 1 (4) | 0 | 10 | 2 | — | r8,r9,r10,r15 |
| B block enter | 6 | 2 | 4 | 0 | 0 | 26 | 4 | 16,18 | r9,r10,r11 |
| B packet 0 | 3 | 3 | 0 | 2 (128) | 0 | 20 | 2 | 0,1,17,18 | r9,r11,r14 |
| **B packet 1..127** | **7** | 4 | 3 | 5 (140) | 0 | 43 | 4 | 0,1,17,18 | r8..r11,r14,r15 |
| consumer: sink | 2 | 2 | 0 | 0 | 0 | 12 | 1 | 0,1,20,21 | — |
| consumer: fused | 13 | 12 | 1 | 6 (384) | 6 (384) | 113 | 3 | 0,1,4–7,22 | rdx |

None of the provider boundaries contains a call, branch, gather, stack operation, spill, store
or mask-register use.

Per (block, dimension) unit of 128 packets, provider only:

- **A:** 5 (context select) + 2 (preload) + 128 × 11 = **1,415** instructions.
- **B:** 2 (record select) + 2 (preload) + 2 (advance) + 6 (enter) + 3 + 127 × 7 = **904**
  instructions. With dimensions as the inner loop, add 2 caller instructions to load and store
  the block state.

Per-packet loop bodies in the bench, as compiled (provider + consumer + 3 loop-control
instructions):

| Loop body | A | B |
|---|---|---|
| sink | 16 instr., 4 loads / 130 B | 12 instr., 5 loads / 140 B |
| fused | 27 instr., 10 loads / 514 B, 6 stores / 384 B | 23 instr., 11 loads / 524 B, 6 stores / 384 B |

The block-transition region in the B kernels (advance + state store) is 3 instructions and 14
bytes. Whole-kernel code size: A sink 226 B, B sink 290 B, A fused 346 B, B fused 511 B.

**Why B differs from the affine reference (11 packet + 2 preload).** The preload is the same
count: 2 instructions, but B loads 20 B where A loads 128 B. The packet differs as follows:

- A reads a per-packet table entry `sel2[p]`: 2 byte loads, a shift, an `xor $64`, and a
  GPR→ZMM broadcast.
- B instead carries the packet state in registers (`ga`, `gb`, `live`). It updates them with one
  `tzcnt` and three XORs against a 7-entry step table indexed by ctz(p). Two of those XORs are
  scalar memory XORs and one is a `{1to16}` broadcast-memory `vpxord`.
- B folds the two donor-line loads into the `vpermd` memory operands. A uses separate
  `vmovdqa32` loads. This fold accounts for 2 of the 4 fewer instructions and is equally
  applicable to A, which is kept frozen here.

What B pays for this:

- B issues 5 loads per packet where A issues 4.
- B introduces loop-carried 1-instruction chains on `ga`, `gb` and `live` from packet to packet,
  where A's packets are independent apart from the counter.
- The longest RAW chain inside a packet is 4 for B (`tzcnt → vpxord{1to16} → vpxord → vpermd`)
  and 5 for A (`movzbl → shl → xor → vmovdqa32 → vpermd`).

ZMM needed during packets: A 6 (2 out, 2 scratch, base, half); B 4 (2 out, live, half), plus
`base` until `ENTER`. GPR: A 5; B 6 per packet, plus `cst` and `blk` at block transitions.

Fewer instructions is not evidence of fewer cycles. See the benchmark section.

## Correctness evidence (this host, under SDE `-spr`)

Each destination value is checked bit for bit against `donor[src]`, where `src` comes from the
scalar oracle. The oracle builds an independent brute-force inverse of the D1 block B (cell →
offset), fails if it is not a bijection, and maps the canonical cell of each dim-d point through
it. It never touches the record or the derivation above.

A (frozen affine, prepared from the oracle sources) and B (computed) are each compared against
that oracle, so A = B follows bitwise. Donor values are tagged 4096·B + o, so donor selection and
permutation are checked from the outputs themselves. Records are made read-only with `mprotect`
during all runs.

| Scope | Size | Result |
|---|---|---|
| Register contract | 3 physical register maps × D1, D2, D200 × all 5 macros (blocks 1, 6, 64, 2^19; packets 1, 2, 3, 32, 64, 127). All 32 ZMM + 15 GPR are sentinel-filled (high halves of 32-bit inputs set) and compared after each macro. | 117 probes, 5,499 register comparisons, 0 unexpected changes, 0 wrong outputs |
| Canonical: D1..D256 × blocks 0..15 × packets 0..127 × lanes 0..31 | 4,096 pairs, 16,777,216 values each for A and B | 0 / 0 mismatches. Every one of these sub-checks was also 0: first packet (131,072 values), last packet (131,072 values), block-state checks (4,096; asm = C seek = oracle), 3,840 asm block transitions, donor selection, duplicate/omitted destinations, 4,096 repeated invocations, 8,192 runs on two alternate register maps, 4,096 reverse-order runs, 16.7 M values of the C record model |
| Extended: D1..D1280 × blocks 0..255 (points 0..1,048,575) | 327,680 pairs, 1,342,177,280 values each for A and B | 0 / 0 mismatches. First/last packet (10,485,760 values each), donor selection, duplicates, 327,680 state checks, 326,400 asm transitions and 327,680 repeat runs: all 0 failures |
| Far blocks: D1..D1280 × 56 blocks (2^k−1 and 2^k for k = 1..19, 2^20−2, 2^20−1, 16 pseudo-random) | 71,680 pairs, 293,601,280 values each | 0 / 0 mismatches |
| Block-state sweep: D1..D1280 × all blocks 0..2^20−1 | asm `CPP_BLOCK_ADVANCE` recurrence vs incremental oracle X(4096B): 1,342,177,280 checks; C seek on every 61st block: 22,004,480 checks | 0 failures |
| Negative controls | the 7 faults: `daddr`, `dlane`, `half`, `ctl` and `cstep` bit flips, wrong donor block, skipped advance | 7 / 7 detected (exit 1) |
| Bench consumption | A vs B pass kernels, sink and fused consumers, d256_b16 and d1280_b256 | consumer state and sink bitwise equal |

The wrong-donor fault is caught by the donor-selection check, not by the value check. This is by
design: the provider copies whatever donor it is handed, so the caller must choose the right
block.

Output and context hashes per scope and shard are in `results/correctness_*.json`.

## Benchmark

`build/bench_ab` runs A and B with identical donor banks, dimensions, blocks, packet order
(blocks outer, dimensions inner, packets 0..127) and consumers. A and B are interleaved per
sample. Before any timing, A = B is verified bitwise.

| Mode | Meaning |
|---|---|
| consumer `sink` | provider only: `zmm20 ^= out_a`, `zmm21 ^= out_b` (keeps outputs live) |
| consumer `fused` | representative consumer per destination: `S *= x; Q += S; L = w·x + L` on 3 × 16 KiB state |
| `pass_warm` | full pass, repeated; caches warm |
| `pass_cold_evict` | full pass after streaming 64 MiB (untimed) |
| `unit_warm` | one (block, dimension) unit, after one untimed run of the same unit |
| `unit_l1_pressure_32KiB` | one unit after touching 32 KiB (untimed) |
| scope `d256_b16` | D1..D256 × blocks 0..15: pass and unit modes |
| scope `d1280_b256` | D1..D1280 × blocks 0..255: pass modes (supported, and tested in the extended scope) |

Each sample records rdtscp ticks, CLOCK_MONOTONIC_RAW ns, and user-space core cycles and
instructions (perf_event_open). Per-sample values are written to
`results/bench_native/raw_<scope>_<mode>_<consumer>_<variant>.csv`, and medians, p10, p90 and
per-packet values to `summary.json`. Defaults: 15 samples after 2 discarded warm-ups
(`--samples N`).

The bench refuses to time unless both CPUID and `/proc/cpuinfo` report avx512f, so emulator runs
cannot produce cycle numbers. Suggested invocation on an AVX-512 host:
`taskset -c 2 make bench`. If perf counters are unavailable, see
`kernel.perf_event_paranoid`; TSC and ns are recorded regardless.

Note on working set in the d1280_b256 pass: A streams 146.8 MB of contexts; B reads 245,760 B of
records.

## Hashes and provenance

Full per-file SHA-256 values for every file in this directory are in
`results/handoff_results.json`.

- **Base commit** of the worktree: `b4b03df4eac1639e8b449a432b7543d66539c181` (asian-option-engine).
- **Frozen affine baseline.** The upstream is `asian-option-engine/dim_provider_handoff/`, an
  untracked directory in the engine checkout that is not in any commit. Its own `SHA256SUMS`
  (sha256 `0e7bd5ce6261445d3163f4f8b93caaea70657e3d2c6291dbc71bec366a257bfb`) is copied as
  `UPSTREAM_SHA256SUMS`. The copied files verify against it:
  - `dim_provider.h`: `d1647ec129b7221b264b645cbbfd26205285c6f49c2aa6e61e4473f90492fd7c`
  - `dim_provider_fragments.inc`: `1b1c8cbc4214c8bd2ecd42f396507745a520d69eb24ca5bc0e20433fcc487827`
  - `dim_provider_prepare.c`: `e9752e7b0fd201c8b13ee1d70c2a1a86af361fe1231ddc3d2d85e471692c939f`
  - `CONTRACT.md`: `ceba8e87ebc5995b722f409418b1f41f1f26542e9e12ba900657f2a532ebe946`
- **Direction numbers.**
  - Full Joe–Kuo `new-joe-kuo-6.21201`: `68eedd2a4e3b659b9695e7aff0f8ac68718bcf620730fc3d3a8c65df2a067441`.
  - Subset D2..D1280 as included: `c1d098e97996159b83949bd90b057af18901038d7f52375941ce4e3b39613265`.
  - Engine bin `direction_numbers/joe_kuo_6_21201.bin`: `fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199`.
  - Derived `data/joe_kuo_d1280.bin`: `f61aaf7fe3defc80843d712985fadbd848fbe01aa626916e9ff9ae8f700c938e`.
- **Prepared records** D1..D1280 (245,760 B): `5aa96e59411a2163a0c2053020c5633cca30505fa6f3fd37fccec7e2fd32894f`.
- **Toolchain used:** gcc 16.2.1, GNU as 2.47, Python 3.14.7, Intel SDE 10.13.1.
- **Host:** AMD Ryzen 5 2600, which has no AVX-512.

## Known limitations

1. **No native performance data.** The host has no AVX-512, so `make bench` wrote
   `status.json` with `timed: false`. SDE was used only for functional correctness. Whether
   B's 7 instructions and 5 loads per packet beat A's 11 instructions and 4 loads is unmeasured.
2. **Ordering is same-block range-matched, not canonical Sobol below 1/4096.** It is not a
   drop-in replacement for maps that implement canonical routing to other donor blocks.
   Switching would change engine outputs below 1/4096. The numerical effect on estimators was
   not studied here (out of scope).
3. **Packets must run sequentially** within a (dimension, block): `CPP_PACKET_NEXT` is a
   recurrence, not idempotent. Random packet entry needs `cpp_packet_state` (C); there is no asm
   seek macro.
4. **Block range.** 1 ≤ B for `CPP_BLOCK_ADVANCE`, because tzcnt(0) = 32 would index past
   `cstep`. B < 2^20 (2^32 points, 32-bit direction numbers).
5. **Coverage limits.** Outputs are exhaustive for D1..D256 × blocks 0..15 and for
   D1..D1280 × blocks 0..255. Beyond block 255, outputs are verified only on the 56 far blocks;
   the block state is verified for every block below 2^20. Alternate register maps, the
   reverse-order run and the C record model run only in the canonical scope. The contract probes
   cover three register maps.
6. **Direction-number structure.** `cpp_prepare_dimension` requires the Joe–Kuo structure
   (lowest set bit of V[j] at 31−j for j < 12). It is verified for D1..D1280; other direction sets
   are rejected (−1).
7. **Encoding constraints.** State/scratch GPRs must be r8..r15. tzcnt requires BMI1, which every
   AVX-512 CPU has.
8. **The fused consumer is representative only.** It is not the production SQL kernel: it uses
   the provider value as both growth and x.
9. **The A baseline is benchmarked as frozen**, with separate `vmovdqa32` donor loads. The
   load-folding part of B's instruction saving could also be applied to A.
10. **Block state with dimensions inner.** In the bench order (dimensions inner), each dimension's
    block state is 4 B of caller memory, loaded and stored once per (block, dimension). With
    blocks inner it stays in a register.
