# Seasoned native-overhead confirmation

This directory is additive to frozen implementation commit
`d31ed2eafdaeb892fbbbf49f55765600548ab46d`. It changes only the native
performance-measurement protocol. No timing result or qualification status is
committed in the package before the AWS run.

The preserved original failed result remains
[`../asian_genuine_seasoned_price_delta_strip/aws.json`](../asian_genuine_seasoned_price_delta_strip/aws.json).
That file is a required, read-only input to final analysis and must retain
status `AWS_PERFORMANCE_GATE_FAILED`. It was produced after the frozen package
commit and is not present in a fresh checkout of that commit; fetch its
preserving AWS result commit before running the analyzer.

## Frozen comparison

- A: matched-f unseasoned.
- B: seasoned.
- Tile 8, price plus Delta only.
- Arithmetic and geometric-CV estimators.
- Candidate-specific warm and historical 32-KiB RMW pressure modes.
- 16 warmup and 201 measured ABBA/BAAB quartets per cell; a fixed-seed PRNG
  chooses the first order in each two-quartet pair and the second uses the
  opposite order.
- The two timings for each candidate are combined as
  `(B1+B2)/(A1+A2)`.

The complete pre-registration, including cases, seeds and gates, is in
[`preregistration.json`](preregistration.json).

## Performance-only AWS execution

The target builds one standalone binary with no oneMKL dependency and runs
only its built-in native prerequisite/correctness preflight:

```sh
make -f tests/Makefile.asian_genuine_seasoned_overhead_confirmation \
  -j2 aws-overhead-confirmation-native
```

Then run the measurement on the first permitted CPU selected by the binary:

```sh
./bench_asian_genuine_seasoned_overhead_confirmation \
  --json results/asian_genuine_seasoned_overhead_confirmation/raw_aws.json
```

The executable refuses to overwrite an existing raw result. Commit and push
`raw_aws.json` unchanged after AWS execution. The raw file contains all four
invocation timings for all warmup and measured quartets, output checksums, the
exact executable SHA-256, affinity, CPU, sibling and cache metadata.

The AWS target and measurement command invoke no Python, NumPy, MPFR,
coefficient generation, Intel SDE, report generation, research audit, oneMKL,
tile 4 or unrelated candidate.

## Deterministic local analysis

After fetching both the preserved failed AWS artifact and the new raw file,
run locally:

```sh
python3 tests/analyze_asian_genuine_seasoned_overhead_confirmation.py \
  --raw results/asian_genuine_seasoned_overhead_confirmation/raw_aws.json \
  --previous-failed results/asian_genuine_seasoned_price_delta_strip/aws.json \
  --json results/asian_genuine_seasoned_overhead_confirmation/analysis.json \
  --markdown results/asian_genuine_seasoned_overhead_confirmation/QUALIFICATION.md
```

The analyzer also refuses to overwrite outputs. Only when every pre-registered
gate passes does it emit:

- `SEASONED_NATIVE_OVERHEAD_CONFIRMED`
- `SEASONED_NATIVE_PERFORMANCE_QUALIFIED`
- `SEASONED_STRIP_QUALIFIED`

The old 400/400 rule was a multiple-testing defect: it required hundreds of
correlated, individually noisy intervals all to pass, so the family-wise
failure probability increased with the size of the benchmark matrix. This
protocol uses one stratified global statistic, retains a hard 2% cell-median
guard, and separately tests byte-identical negative controls for measurement
bias.
