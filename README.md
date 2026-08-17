# Asian State AWS Bench

This repository benchmarks only the arithmetic-Asian S/Q
state-transition using supplied growth. It does not include Sobol
generation, Gaussian inversion, exponential evaluation as a priced
kernel, or a complete Asian engine.

The tree is a test carrier: two stripped x86-64 executables and a
stdlib Python runner. It is not an SDK and not a copy of any private
pricing engine. The binaries may be disassembled.

## Scope

`asian_state_test` checks the isolated `(S, Q)` update against a
float32 reference and a double `exp` oracle, including AVX-512 packet
and timestep wrappers when the ISA is present.

`asian_state_bench` times only that state-transition on native
AVX-512. Reported cycle counts exclude Sobol, Gaussian conversion,
exponential generation of growth, and payoff integration.

## Host requirements

- Linux x86-64
- CPU flag `avx512f` (the runner does not launch the binaries without it)
- glibc providing `GLIBC_2.34` (`__libc_start_main`), `GLIBC_2.29`
  (`exp`), and `GLIBC_2.27` (`expf`)
- Python 3 with the standard library only

## Run on AWS

```sh
git clone <repository-url>
cd asian-state-aws-bench
PYTHONDONTWRITEBYTECODE=1 python3 scripts/run_aws.py
```

The runner verifies `SHA256SUMS` (every file except `SHA256SUMS`
itself), prints CPU model, AVX-512 flags, sysfs L1D/L1I, kernel,
Python, arch, and `ldd`, pins to CPU 0 when that CPU is in the process
affinity set, runs the test binary first, and stops unless stdout
contains `ASIAN_STATE_TEST PASS`. It then runs the bench binary, parses
`RESULT` JSONL, and writes `results/aws_<cpu>_<UTC>.json` (gitignored).

The printed decision table uses cycles per 32-path fixing from the
1-packet 32-fixing latency series:

| cycles / 32-path fixing | verdict |
| --- | --- |
| ≤ 6 | EXCELLENT |
| ≤ 8 | REVIEW |
| > 8 | INVESTIGATE |
| no native AVX-512 | UNSUPPORTED |

The runner does not select a production winner.

## Build record

Recorded at export time `2026-08-17T01:20:46Z`.

| Item | Value |
| --- | --- |
| Compiler | GCC 16.2.1 20260810 (GNU), target `x86_64-pc-linux-gnu` |
| Linker | GNU ld (GNU Binutils) 2.47 |
| C flags | `-O2 -std=c23 -Wall -Wextra -ffp-contract=off -fno-fast-math -fno-tree-vectorize -fno-builtin-sqrtf -fno-math-errno -g0 -fno-ident` plus `-ffile-prefix-map`, `-fmacro-prefix-map`, `-fdebug-prefix-map` |
| Assembler flags | `-mavx512f -g0` plus the same prefix maps |
| Linker flags | `-Wl,--build-id=none` |
| ISA | AVX-512F in the assembled wrappers; C compiled without `-march=native` |
| `file` (both ELFs) | `ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux 4.4.0, stripped` |
| `ldd` | `linux-vdso.so.1`, `libm.so.6`, `libc.so.6`, `/lib64/ld-linux-x86-64.so.2` |
| Source identity | `source_manifest_sha256` `0ffdf2fc3e9c4dd0df98ba685b08411a32c5cba35abec92df1b56027296b2031` |
| `bin/asian_state_test` SHA-256 | `88afc0dbaa20dbb56c6c3e4e4bdef31676e6f6c0a76c989d227dbad7d3b13ca2` |
| `bin/asian_state_bench` SHA-256 | `9da99502d0dfff1424c385a4932589480b4f31310f21ad78e7e957c6756dff12` |

Highest glibc symbol versions in the stripped ELFs: `GLIBC_2.34`
(`__libc_start_main`), `GLIBC_2.29` (`exp`), `GLIBC_2.27` (`expf`).
There is no `GLIBC_2.43` dependency.

Embedded `build_time_static_audit` is a compile-time ELF instruction
audit of the linked wrappers. It is not a runtime re-audit on AWS.
Static instruction occurrences in that audit are distinct from the
dynamic load/store counts per 32-path transition reported in `RESULT`
lines of kind `static`.
