# ZMM resident-template native benchmark package

Status: **AWS_NATIVE_TIMING_PENDING**.

This additive package benchmarks exact Joe--Kuo D1--D256 template generation;
it does not call or modify either pricing engine.

Local qualification completed before packaging:

- generated-file check: PASS;
- clean package build: PASS;
- AVX-512 SDE exact check: PASS for 16/32 lanes, D1--D256, block counts
  1/2/4/8/16/32/64 and independent per-dimension digital shifts;
- NewDirNumbers regression suite: 248 tests PASS, 2 unrelated skips;
- generated object audit: 130/130 symbols found and no vector stack
  spills/reloads.

Run on the native AVX-512 AWS host from the repository root:

```sh
cd testing/pablito_sequence/native
make -f Makefile.resident_templates -j2 all
make -f Makefile.resident_templates native-run
```

Commit only the generated native result:

```sh
git add testing/pablito_sequence/results/zmm_resident_native_benchmark_20260821/native.json
git commit -m "Add native ZMM resident-template benchmark result"
git push
```

SDE timing is not accepted as performance evidence. The native result must be
reduced with `analyze_zmm_resident_native.py` before any speedup is claimed.
