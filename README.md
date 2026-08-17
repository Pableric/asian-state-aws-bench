# AWS bench carrier

This repository benchmarks isolated AVX-512 components. It does not
include Sobol generation, Gaussian inversion, exponential evaluation as
a priced kernel, or a complete pricing engine.

The tree is a test carrier: stripped x86-64 executables, compile-time
ELF audits in JSON, and a stdlib Python runner. It is not an SDK and
not a copy of any private pricing engine. The binaries may be
disassembled.

## Suites

1. **Asian S/Q state** — `bin/asian_state_test` / `bin/asian_state_bench`,
   audit in `BUILD_METADATA.json`.
2. **Dim-from-D1 permute** — `bin/dim_permute_test` / `bin/dim_permute_bench`,
   audit in `BUILD_METADATA_dim.json`. `real_block_maps.bin` contains the
   validated source-index permutations for the 18 exact dimensions; it does
   not contain Sobol, Gaussian, exponential, state, or pricing code.
3. **Private affine 18-step fusion diagnostic** —
   `bin/asian_affine_18diag_bench`, using the public Joe--Kuo table at
   `direction_numbers/joe_kuo_6_21201.bin`. This is explicitly incomplete:
   D1 plus 17 affine-routable dimensions, `dt=T/32`, no payoff and no missing
   dimensions.
4. **Growth-payload affine 18-step diagnostic** —
   `bin/asian_affine_growth_18diag_bench`. This tests the algebraic rewrite
   that converts corrected D1 Gaussian Z to growth once and then permutes the
   growth payload. It is the same incomplete 18-step scope as `affine18`.

`scripts/run_aws.py` hashes every listed file, including both metadata
files, and merges audits with native measurements.

```sh
git clone git@github.com:Pableric/asian-state-aws-bench.git
cd asian-state-aws-bench
PYTHONDONTWRITEBYTECODE=1 python3 scripts/run_aws.py
# or: python3 scripts/run_aws.py --suite asian
# or: python3 scripts/run_aws.py --suite dim
# or: python3 scripts/run_aws.py --suite affine18
# or: python3 scripts/run_aws.py --suite growth18
```

The runner verifies `SHA256SUMS` (every file except `SHA256SUMS`
itself), prints CPU model, AVX-512 flags, sysfs L1D/L1I, kernel,
Python, arch, and `ldd`, pins to CPU 0 when that CPU is in the process
affinity set, runs each selected test binary first, and stops on
failure. It parses Asian `RESULT` JSONL or the dimension benchmark's validated
key/value records, then writes `results/aws_<cpu>_<UTC>.json` (gitignored).

The 32 KiB cache-pressure scan runs immediately before `t0` of each of
the 51 samples and is never inside `t0`–`t1`. The `pressure_32KiB`
environment times one kernel call per sample so that scan applies to
the timed workload.

The runner does not select a production winner.

## Affine 18-step fusion diagnostic

The `affine18` suite runs its complete vector correctness comparison before
printing the native timing banner. It then measures provider-only, exact-Z
exp/SQ, structurally matching unfused paths, fused dimension-major and
packet-major paths, corrected D1 production, and genuinely combined
D1-plus-fused paths. Each row contains 51 raw samples and median/p10/p90 under
matched-frequency warm and competing-32-KiB protocols.

This suite answers whether affine permutation cost is hidden by fusion. It is
not a complete 32-fixing Asian price and makes no Brownian-bridge claim.

## Growth-payload 18-step diagnostic

The `growth18` suite first runs a standalone correctness-only invocation and
then repeats the same bit-exact state comparison before native timing. The
conversion uses the qualified private p0--p8 sequence once on the corrected
D1 block. The hot affine kernels contain no Gaussian-exp polynomial: they
permute final float32 growth bits, then perform separate `S *= growth` and
`Q += S` operations.

The timing rows deliberately keep three denominators separate:

- `d1_z_to_growth_inplace`: cycles per 4,096-value block;
- `growth_affine_provider_17`: cycles per routed dimension;
- all state candidates: cycles per 18-step simulation and derived per-step,
  per-path-step, and per-packet-step values.

The runner validates all 51 samples and the candidate-specific denominators.
It also checks the printed comparisons against the frozen warm packet-major
Z-to-exp references (31,612 cycles kernel-only and 33,232 cycles including
corrected D1 production). These are historical same-host references embedded
by the diagnostic, not universal performance baselines.

Correctness covers all 18 dimensions, all 4,096 values, corrected hard
head/tail positions, every intermediate S/Q state, and both loop
organizations. This still is not a complete 32-fixing price: it has no payoff,
no unresolved dimensions, and no Brownian bridge.

| Item | Value |
| --- | --- |
| `bin/asian_affine_growth_18diag_bench` SHA-256 | `b910c03efacd351bd77a5534e853944126e24f062ec8bea04166f2e04a22ba55` |
| Growth dimension-major hot text | 366 bytes |
| Growth packet-major hot text | 259 bytes |
| Hot payload work | 4,352 `vpermd`, 4,608 `vmulps`, 4,608 `vaddps` |
| Hot payload exclusions | zero FMA, calls, stack operations, spills, gathers, or intermediate payload stores |

## Host requirements

- Linux x86-64
- CPU flag `avx512f` (the runner does not launch the binaries without it)
- glibc providing `GLIBC_2.34` (`__libc_start_main`); the Asian binaries
  also need `GLIBC_2.29` (`exp`) and `GLIBC_2.27` (`expf`)
- Python 3 with the standard library only

## Asian S/Q state

`asian_state_test` checks the isolated `(S, Q)` update against a
float32 reference and a double `exp` oracle, including AVX-512 packet
and timestep wrappers when the ISA is present.

`asian_state_bench` times only that state-transition. Reported cycle
counts exclude Sobol, Gaussian conversion, exponential generation of
growth, and payoff integration.

The printed Asian table uses cycles per 32-path fixing:

| cycles / 32-path fixing | verdict |
| --- | --- |
| ≤ 6 | EXCELLENT |
| ≤ 8 | REVIEW |
| > 8 | INVESTIGATE |
| no native AVX-512 | UNSUPPORTED |

Build record (Asian) at export `2026-08-17T01:37:10Z`. Full compiler,
audit, and glibc details are in `BUILD_METADATA.json`.

| Item | Value |
| --- | --- |
| Compiler | gcc version 16.2.1 20260810 (GCC) |
| Linker | GNU ld (GNU Binutils) 2.47 |
| C flags | `-O2 -std=c23 -Wall -Wextra -ffp-contract=off -fno-fast-math -fno-tree-vectorize -fno-builtin-sqrtf -fno-math-errno -g0 -fno-ident` plus prefix maps |
| Assembler flags | `-mavx512f -g0` plus prefix maps |
| Linker flags | `-Wl,--build-id=none` |
| `bin/asian_state_test` SHA-256 | `bed5ba33bb2b15e78b29f7033951eb577e048dd30e442173d301fb53aef67fb8` |
| `bin/asian_state_bench` SHA-256 | `00e0113be837d2cfc31d09cc4ed866c4fd6c53d4226fe053cc70899a1d2ba082` |

Highest glibc symbol versions in the Asian ELFs: `GLIBC_2.34`
(`__libc_start_main`), `GLIBC_2.29` (`exp`), `GLIBC_2.27` (`expf`).
There is no `GLIBC_2.43` dependency.

## Dim-from-D1 permute

`dim_permute_test` checks every candidate against a scalar integer
oracle (bit-exact) when AVX-512F is present. A native run must report exactly
`dim_provider_test PASS tests=6101 native_avx512=1`; prepare-only coverage is
not accepted as the AWS correctness result.

The benchmark validates `real_block_maps.bin`, checks every real map/candidate
against the scalar oracle before timing, and measures the complete sequential
destination-order path: preload once, then packets `p=0..127`. It reports each
real dimension separately under warm-L1D and competing-32-KiB pressure, plus
the median across dimensions and the worst dimension. Synthetic A8 results are
a separate control and are excluded from the aggregate.

Only three candidates are shipped:

| name | packet probe | notes |
| --- | --- | --- |
| `generic` | 14 instructions | 1600 B context; 4 scalar + 4 ZMM loads, 2 `vpermd` |
| `affine` | 11 instructions | 448 B context; 2 scalar + 2 payload ZMM loads, resident controls |
| `res2xor` | 15 instructions | 448 B context; 2 live masks with XOR/swap updates |

| cycles / packet | verdict |
| --- | --- |
| ≤ 3 | EXCELLENT |
| ≤ 5 | GOOD |
| ≤ 7 | REVIEW |
| > 8 | INVESTIGATE |
| no native AVX-512 | UNSUPPORTED |

The fragment audit reports zero calls, stores, gathers, and stack spills. The
benchmark itself consumes the two output ZMMs; it does not time a theoretical
two-load/two-permute floor. Context warmup uses the exact sizes: 1600 B for
generic and 448 B for affine/RES2XOR.

Build record (dim) at export `2026-08-17T19:18:17Z`. Full compiler,
audit, and glibc details are in `BUILD_METADATA_dim.json`.

| Item | Value |
| --- | --- |
| Compiler | gcc (GCC) 16.2.1 20260810 |
| Linker | GNU ld (GNU Binutils) 2.47 |
| C flags | `-O2 -std=c11 -Wall -Wextra -Werror` plus prefix maps |
| Assembler flags | `-O2 -Wall -Werror -mavx512f` plus prefix maps |
| `bin/dim_permute_test` SHA-256 | `d7170dd491013e9d6705c8fe77dd04e20c38bf91a24cc6ec92146a34ae5477f3` |
| `bin/dim_permute_bench` SHA-256 | `88f0456ae1bcd4d894dabaef31cd10ccae8c7aa5fadea9915ee47d45b84e73e5` |
| `real_block_maps.bin` SHA-256 | `37c1a5c5b95053f52662dc56801e867ac93060eb7bbee9daa77588945a022bfb` |
