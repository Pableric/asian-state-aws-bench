# Native ZMM resident-template benchmark

This additive benchmark compares direct Joe--Kuo template loads with exact
rolling/hybrid resident-ZMM schedules. It does not call or modify a pricing
engine.

Generate and build:

```sh
python3 ../generate_zmm_resident_native_benchmark.py --check
make -f Makefile.resident_templates -j2 all
```

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
