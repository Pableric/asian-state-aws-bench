# AWS bench carrier

This repository benchmarks two isolated AVX-512 components. It does not
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
   audit in `BUILD_METADATA_dim.json`. Maps are synthetic (LCG-seeded
   structural twins), not production tables.

`scripts/run_aws.py` hashes every listed file, including both metadata
files, and merges audits with native measurements.

```sh
git clone git@github.com:Pableric/asian-state-aws-bench.git
cd asian-state-aws-bench
PYTHONDONTWRITEBYTECODE=1 python3 scripts/run_aws.py
# or: python3 scripts/run_aws.py --suite asian
# or: python3 scripts/run_aws.py --suite dim
```

The runner verifies `SHA256SUMS` (every file except `SHA256SUMS`
itself), prints CPU model, AVX-512 flags, sysfs L1D/L1I, kernel,
Python, arch, and `ldd`, pins to CPU 0 when that CPU is in the process
affinity set, runs each selected test binary first, and stops on
failure. It then runs the matching bench binary, parses `RESULT` JSONL,
and writes `results/aws_<cpu>_<UTC>.json` (gitignored).

The 32 KiB cache-pressure scan runs immediately before `t0` of each of
the 51 samples and is never inside `t0`–`t1`. The `pressure_32KiB`
environment times one kernel call per sample so that scan applies to
the timed workload.

The runner does not select a production winner.

## Host requirements

- Linux x86-64
- CPU flag `avx512f` (the runner does not launch the binaries without it)
- `avx512bw` for the dim `gen20` candidate
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
oracle (bit-exact) when AVX-512F is present.

The runner prints aligned tables only (no raw 51-batch dumps). W1 is not scored.
W3 uses derived cycles/packet. `resident2_xor` is the 2-ZMM XOR/swap walk.

| name | insns / packet | notes |
| --- | --- | --- |
| `permd_floor` | 4 | controls already live; throughput floor |
| `resident2_xor` | ~6 inner | 2 live masks; XOR/swap between Gray groups |
| `resident8` | ~6 inner | zmm16–23 live |
| `resident16` | ~6 inner | zmm16–31 live |
| `affine` | 11 | 448 B map |
| `affine_w` | 12 | packed selector |
| `generic` | 14 | 1600 B map; always valid |
| `gen20` | 20 | 0 loads; AVX-512BW; compute reference |

| cycles / packet | verdict |
| --- | --- |
| ≤ 3 | EXCELLENT |
| ≤ 5 | GOOD |
| ≤ 7 | REVIEW |
| > 8 | INVESTIGATE |
| no native AVX-512 | UNSUPPORTED |

Cache budgets: D1 block 16 KiB shared; stored output 16 KiB/dim;
generic map 1600 B/dim; affine map 448 B/dim; resident schedule 256 B.
Worst unroll `dpb_affine_unrolled_block` is 8650 B ≪ 32 KiB L1I.

Build record (dim) at export `2026-08-17T16:03:07Z`. Full compiler,
audit, and glibc details are in `BUILD_METADATA_dim.json`.

| Item | Value |
| --- | --- |
| Compiler | gcc version 16.2.1 20260810 (GCC) |
| Linker | GNU ld (GNU Binutils) 2.47 |
| C flags | `-O2 -std=c23 -Wall -Wextra -g0 -fno-ident` plus prefix maps |
| Assembler flags | `-mavx512f -mavx512bw -g0` plus prefix maps |
| Linker flags | `-Wl,--build-id=none` |
| `bin/dim_permute_test` SHA-256 | `5f0cf18fc60c563940f70858fb4ac71997f1b1755691a1da3509672c3f65b7db` |
| `bin/dim_permute_bench` SHA-256 | `994dec9f03582745b1bd416878c5ef3e2efd037aae7da891e28fcdadeaeb76fc` |
