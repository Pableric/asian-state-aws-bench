# Native follow-up run

Build and run the separate hybrid benchmark on Sapphire Rapids:

```bash
make -f tests/Makefile.asian_genuine_multistrike_full_risk_hybrid_dispatch \
  -j2 aws-benchmark-native

mkdir -p results/asian_genuine_multistrike_full_risk_hybrid_dispatch

MKL_THREADING_LAYER=SEQUENTIAL \
MKL_NUM_THREADS=1 \
MKL_DYNAMIC=FALSE \
./bench_asian_genuine_multistrike_full_risk_hybrid_dispatch \
  --json results/asian_genuine_multistrike_full_risk_hybrid_dispatch/native.json
```

For a bounded diagnostic run, add `--N 256 --K 32` and use a distinct JSON
path.  The executable writes through a temporary file and publishes with a
no-replace operation.  If the requested success path already exists, it refuses
the run rather than overwriting valid evidence.

The JSON records all 51 raw TSC-unit and wall-time samples, p10/median/p90,
paired hybrid/tile ratios, the exact qualified and follow-up commits, the
contract, the fixed dispatch policy, and the imported evidence scope.  TSC
units are never labeled CPU cycles.
