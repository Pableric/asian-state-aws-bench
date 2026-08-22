# AWS execution

Build only the additive native executable and existing C/assembly dependencies,
then run its bounded native preflight:

```sh
make -f tests/Makefile.asian_genuine_fixed_block_e2e \
  -j2 aws-benchmark-native
```

Run the complete matrix with a new output path:

```sh
mkdir -p results/asian_genuine_fixed_block_e2e

MKL_THREADING_LAYER=SEQUENTIAL \
MKL_NUM_THREADS=1 \
MKL_DYNAMIC=FALSE \
./bench_asian_genuine_fixed_block_e2e \
  --json results/asian_genuine_fixed_block_e2e/aws.json
```

To import frozen oneMKL evidence, add
`--onemkl-reference /path/to/qualified.json`. The importer is read-only. It
requires an exact CPU, kernel, physical CPU, timing, market, N, K, estimator,
cache, output-set, threading, and one-matrix/one-evolution semantics match.
Nonmatching rows are reported as `ONEMKL_REFERENCE_UNAVAILABLE`; they are not
interpolated or timed again.

The success JSON is written to a sibling temporary file, flushed and synced,
then published with a no-replace atomic operation. An existing output is never
overwritten. Failures preserve it and use a separate non-overwriting failure
artifact.

The AWS dependency graph contains no oneMKL object, library, header, benchmark,
or test, and no Python, NumPy, MPFR, SDE, generator, audit, or network step.
TSC output is reported only as TSC units.
