# AWS bench carrier

This repository benchmarks isolated AVX-512 components. Except for the
explicit `onemkl_x` producer-throughput suite, it does not include Sobol
generation or Gaussian inversion. It does not contain a complete pricing
engine.

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
5. **Exact first-increment conditional diagnostic** —
   `bin/asian_affine_conditional_18diag_bench`. This keeps `dt=T/32`, uses D1
   as the canonical growth source without applying it as a stochastic first
   update, and executes the 17 certified residual routes. It separately times
   residual accumulation, accurate scalar research `log`/CDF payoff work, and
   complete research diagnostics. It is not a complete 32-fixing price.
6. **Vector conditional-payoff diagnostic** —
   `bin/asian_conditional_payoff_18diag_bench`. This compares the qualified
   vector log and Φ-table kernels (1024/2048/4096 entries), fused conditional
   payoff paths, and the accurate scalar `libm` research oracle. It remains
   the incomplete 18-route canonical block and is not a complete 32-fixing
   production price.
7. **D5 stored-payload x/growth diagnostic** —
   `bin/asian_affine_x_growth_1dim_bench`. This produces completed canonical
   D1 x/growth arrays first, applies the same prepared D5 affine source map to
   both payloads, and compares growth-only S/Q with dual S/Q/L. It is one
   fixing only and is not a complete Asian price.
8. **Weighted S/Q/L and geometric-control diagnostic** —
   `bin/asian_affine_dual_sql_18diag_bench`. This compares the frozen S/Q
   historical path and a structurally matched unrolled S/Q comparator with
   memory-broadcast, decrementing-weight, explicit-broadcast, and general-loop
   S/Q/L paths. It separately times the combined geometric-CV payoff. The path
   has only the 18 certified chronological routes; no result is a complete
   Asian price and no benchmark winner is embedded.
9. **oneMKL Sobol-to-x throughput comparison** —
   `bin/onemkl_sobol_x_bench`. This strictly compares the frozen corrected-Z
   then affine-x path with the qualified position-aware ordered-D1 x producer,
   both over the same 4,096 canonical values. A third row measures oneMKL's
   dimension-1 Sobol-plus-Gaussian path from the corresponding native skip.
   oneMKL enters a strict value/ratio comparison only when its untimed raw-word
   probe proves exact D1 identity; otherwise it remains native-throughput
   context with no permutation adapter.
10. **Synthetic all-permute scaling ceiling** —
   `bin/synthetic_all_permute_scaling_bench`. This compares N=16/32/64/128/256
   scaling for materialized x, fused S/Q/L, and a proven custom-direction
   oneMKL stream. D2–D256 deliberately cycle the seventeen certified affine
   maps. These rows are hardware diagnostics only: they are not valid
   multidimensional Asian paths, prices, or discrepancy results.

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
# or: python3 scripts/run_aws.py --suite conditional18
# or: python3 scripts/run_aws.py --suite conditional_payoff
# or: python3 scripts/run_aws.py --suite xgrowth1
# or: python3 scripts/run_aws.py --suite sql18
# or: python3 scripts/run_aws.py --suite onemkl_x
# or: python3 scripts/run_aws.py --suite synthetic_all_permute
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

## Exact first-increment conditional diagnostic

The `conditional18` suite runs a standalone correctness gate before timing.
Its canonical provider block is deliberately separate from the randomized
statistical experiment: no claim is made that the affine maps survive a
digital shift or scramble.

The candidate initializes the residual first fixing deterministically, then
consumes 17 affine growth routes. The expensive first Brownian factor is
integrated analytically in the research payoff. Complete timings include the
deterministic initialization. The scalar `log` plus two Gaussian CDFs use
accurate research math and are explicitly not a projection of a future
vectorized production implementation.

The output validates all nine candidates in both warm-L1D and
competing-32-KiB modes, with 51 raw samples and median/p10/p90. Frozen growth
and Z-exp medians are printed only as immutable same-host comparison metadata.

## Vector conditional-payoff diagnostic

The `conditional_payoff` suite runs the fused LUT candidates only after their
pre-timing correctness check. It reports 51 native samples in warm-L1D and
competing-32-KiB modes for vector log alone, paired Φ lookup, the 1024/2048/4096
fused paths, D1-plus-fused paths, and an accurate scalar `libm` research oracle.
The scalar row is a timing reference, not a production contender. LUT accuracy
is checked against the 4096-entry candidate before timing; the benchmark does
not claim that the canonical affine maps survive a future Sobol randomization.

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

## D5 stored-payload x/growth diagnostic

The `xgrowth1` suite runs a standalone correctness gate before timing and
validates every emitted JSON row, raw 51-sample batch, percentile, denominator,
incremental median, and reported footprint. The benchmark measures ten explicit
candidates under warm and competing-32-KiB protocols:

- canonical growth and dual x/growth producers;
- growth-only and dual affine provider loops;
- S/Q and S/Q/L recurrence floors;
- fused provider-plus-recurrence loops;
- complete producer-plus-consumer routes.

The deciding comparison is `fused_dual_provider_sql` against
`fused_growth_provider_sq`. The experiment uses only Joe--Kuo D5 and one state
transition. It does not implement missing dimensions, a complete chronological
path, payoff, conditioning, or production routing.

A second, separately shuffled cohort answers the source-production question
under the same native protocol:

```text
old_growth_source: corrected D1 Z producer -> frozen P8 growth conversion
new_growth_source: position-aware Sobol -> growth directly
new_dual_source:   position-aware Sobol -> x and growth directly
```

The runner validates both accuracy contracts independently. The frozen path's
P8 result is checked relative to its certified approximate Z payload, while the
new paths are checked against true correctly rounded MPFR growth with the
complete 3e-7 relative-error gate. It also reports the frozen path's true-MPFR
and normalized call/put proxy errors; none of that correctness work is timed.

## Weighted S/Q/L and geometric-control diagnostic

The `sql18` suite runs a standalone correctness gate before timing and then
validates all 22 native candidate/mode records. Its primary kernel keeps two
halves each of S, Q, and weighted-log state L live across the complete partial
chronology. Exact zero-based weights are `(32-k)/32` for `k=0..17`.

Incremental SQL cost is measured against `path_sq_matched_unrolled`, generated
from the same route macro and packet-loop structure. The frozen S/Q row remains
historical context only. Warm conditioning is candidate-specific; the pressure
mode reproduces the historical sequential 32-KiB uint32 read-modify-write scan.

Three otherwise matched weight schedules are measured: EVEX scalar-memory
broadcast operands, a bit-qualified decrementing vector, and explicit scalar
broadcast. A general route-count loop is reported separately. Payoff-only and
producer-plus-consumer rows remain partial mechanics; the executable emits
`complete_price_cycles: null` because 14 future dimensions are unresolved.
The scalar libm oracle is excluded from candidate ranking.

Build-time audits are carried in `BUILD_METADATA_sql18.json` and
`BUILD_METADATA_geometric_cv.json`. Native AWS timing, not static counts,
selects among weight schedules.

## oneMKL Sobol-to-x throughput comparison

The `onemkl_x` suite compares exactly 4,096 materialized float outputs per
timed call under two nonzero contracts. The old candidate is the frozen
corrected ordered-D1 Z producer followed by an audited in-place AVX-512 affine
FMA. The new candidate produces x directly with the qualified position-aware
leaf. Both cover the same canonical D1 indices beginning at 8192. oneMKL uses
a dimension-1 Sobol stream skipped to its native element 8192 and one
`vsRngGaussian(ICDF)` call.

An untimed cloned oneMKL stream attempts to expose raw Sobol words. Exact words
enable pointwise compatibility and a strict vendor ratio. If that API is
unsupported or the words differ, the report records the reason and keeps the
oneMKL number as native-layout throughput only. Old/new remains the strict
comparison in every run.

Both candidates run single-threaded. Stream restoration, output reset, cache
conditioning, status checks, and checksums stay outside the timed region. The
third zero-diffusion contract validates the ordered-D1 producer but is reported
as unsupported by `vsRngGaussian` and is not timed as a vendor ratio.

The suite requires Intel oneAPI MKL 2026 or another installation providing
`libmkl_rt.so.3`. With the default installation, run:

```sh
source /opt/intel/oneapi/setvars.sh
MKL_THREADING_LAYER=SEQUENTIAL MKL_NUM_THREADS=1 MKL_DYNAMIC=FALSE \
  PYTHONDONTWRITEBYTECODE=1 python3 scripts/run_aws.py --suite onemkl_x
```

Build provenance and the frozen carrier hashes are in
`BUILD_METADATA_onemkl_x.json`.

## Synthetic all-permute scaling ceiling

The `synthetic_all_permute` suite measures 4,096 paths at
N=16/32/64/128/256. It reports complete and component timings for canonical
x/growth production, materialized affine routing, fused S/Q/L routing, and a
native point-major oneMKL Sobol-to-x call using the exact Joe–Kuo direction
table.

Before timing each N, the executable proves oneMKL's custom stream by comparing
all `4096*N` underlying integer Sobol components with an independent Gray-code
reference. Stream initialization success alone is not accepted. The stream
skips `8192*N` scalar components, so oneMKL's u1-based recurrence returns
indices 8193–12288; this convention is recorded in the native JSON.

The reused affine routes are intentionally synthetic. The suite answers only
how the certified permutation mechanism scales when reused; it cannot support
any multidimensional correctness, pricing, convergence, or variance claim.
Native timing uses fenced TSC deltas with 16 warmups and 51 shuffled samples;
the reported values are hardware TSC ticks, not SDE estimates.

Run it with:

```sh
source /opt/intel/oneapi/setvars.sh
MKL_THREADING_LAYER=SEQUENTIAL MKL_NUM_THREADS=1 MKL_DYNAMIC=FALSE \
  PYTHONDONTWRITEBYTECODE=1 python3 scripts/run_aws.py \
  --suite synthetic_all_permute
```

Build provenance and the source audit hashes are in
`BUILD_METADATA_synthetic_all_permute.json`.

## Host requirements

- Linux x86-64
- CPU flag `avx512f` (the runner does not launch the binaries without it)
- glibc providing `GLIBC_2.34` (`__libc_start_main`); the Asian binaries
  also need `GLIBC_2.29` (`exp`) and `GLIBC_2.27` (`expf`)
- Python 3 with the standard library only
- Intel oneAPI MKL runtime providing `libmkl_rt.so.3` for `--suite onemkl_x`
  and `--suite synthetic_all_permute`

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
