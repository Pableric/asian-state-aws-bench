# Contracted Asian Sensitivity Basis — Phase 1

Local mathematical, SDE correctness, and linked-symbol structural gates pass.
This host is not AVX-512 capable, so no native timing is represented as a
result. `aws.json` is intentionally absent until the native command is run.

## Current seven-question decision

1. Contracted suffix reverse fits without spills: **yes**. The final linked
   ranked symbols have no calls, stack references, gathers, or spills.
2. The 32-KiB packet tape is affordable: **native result pending**. Nominal
   size is reported without an L1-residency claim.
3. x-only reverse routing works as expected: **yes**. The reverse fragment has
   two x payload loads, two control loads, two `vpermd`, and no growth route.
4. Suffix versus generic reverse: **native result pending**; both complete
   competitors are built.
5. Suffix versus targeted forward for scalar Vega/Rho: **native result
   pending**.
6. Either candidate versus CRN bump-and-revalue: **native result pending**.
7. Evidence for Phase 2: **pending the native comparisons**. Phase 2 is not
   implemented here.

## Evidence

- [math_correctness.txt](math_correctness.txt)
- [vector_correctness.txt](vector_correctness.txt)
- [linked_structural_audit.json](linked_structural_audit.json)
- [register_lifetimes.md](register_lifetimes.md)
- [provenance.md](provenance.md)
- [benchmark_preflight.txt](benchmark_preflight.txt)
- [binary_reproducibility.txt](binary_reproducibility.txt)
- [changed_file_manifest.txt](changed_file_manifest.txt)

The unranked generic reverse is one basis kernel with one shared payoff
consumer; it is not cloned by side or estimator. The supported runtime domain
is N=2..256. Every ranked leaf consumes direct D1 and then at least one routed
fixing; N<2 is rejected during preparation.

Benchmark preparation reports the failing stage and underlying status before
returning. Native JSON is written to an adjacent temporary file, flushed and
atomically renamed only after all rows complete; a failed preparation or
measurement removes the temporary file and preserves any prior result.

Ranked contracts require positive sigma inside the established producer
envelope. Sigma-zero mathematics is tested, but production sigma-zero Vega is
reported unsupported until a separately qualified `z` or `dx/dsigma` source
exists; no per-path sigma branch is added.

## Native AWS commands

```sh
make -f tests/Makefile.asian_genuine_aad_phase1 -j2 aws-benchmark-native

mkdir -p results/asian_genuine_aad_phase1

MKL_THREADING_LAYER=SEQUENTIAL \
MKL_NUM_THREADS=1 \
MKL_DYNAMIC=FALSE \
./bench_asian_genuine_aad_phase1 \
  --json results/asian_genuine_aad_phase1/aws.json
```

The standalone AWS target builds only the native benchmark and runs its
bounded built-in preflight. It does not invoke Python, NumPy, MPFR, SDE,
generators, research scripts, or network access.

After native timing, `tests/report_asian_genuine_aad_phase1.py` emits only the
seven Phase-1 decisions. For the otherwise qualitative word “materially”, it
pre-registers a 20% complete-wall-time improvement over the matched CRN
baseline.
