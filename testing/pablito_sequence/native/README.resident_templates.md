# Native ZMM resident-template benchmark

This additive benchmark compares direct Joe--Kuo template loads with exact
rolling/hybrid resident-ZMM schedules. It does not call or modify a pricing
engine.

Build the checked-in generated kernels:

```sh
make -f Makefile.resident_templates -j2 all
```

The NumPy-based generator remains an offline research/development tool. It is
not part of the AWS measurement path; the executable's exact reconstruction
gate validates the checked-in assembly before timing.

On an AVX-512 host, run the exact gate and randomized-order benchmark:

```sh
make -f Makefile.resident_templates native-run
```

The native result is written to
`testing/pablito_sequence/results/zmm_resident_native_benchmark_20260821/native.json`.
Analyze it from the repository root:

```sh
python3 testing/pablito_sequence/analyze_zmm_resident_native.py \
  --native-json testing/pablito_sequence/results/zmm_resident_native_benchmark_20260821/native.json \
  --binary testing/pablito_sequence/native/bench_zmm_resident_templates \
  --out-dir testing/pablito_sequence/results/zmm_resident_native_analysis_20260821
```

An AVX2-only development host may use `sde-check` for exactness, but SDE timing
is not accepted as native performance evidence.
