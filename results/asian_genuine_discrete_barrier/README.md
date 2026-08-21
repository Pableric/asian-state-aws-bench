# Genuine discrete-barrier AVX-512 diagnostic

Local correctness is `PASS` and the complete linked-object audit is `PASS`. Native Sapphire Rapids performance remains pending; no performance status is emitted before the committed raw AWS result is analyzed.

The package is additive and builds only against byte-identical copies in `asian_genuine_discrete_barrier_carrier/`. It does not claim equivalence to the unavailable historical commit `396a8a0`, and it does not modify any production Asian or European source.

## Frozen semantics

- Barrier observations occur after future updates at dates `1..N`; `S0` is not observed.
- D1 is consumed directly and observed once before the routed loop. Compact entry zero is D2.
- Down survival is strict ordered-quiet `S > barrier`; equality and NaNs knock out.
- Candidate-warm conditioning occurs immediately before timing after reset.
- Historical pressure performs the exact 32-KiB `pressure[i] += i + 3` recurrence immediately before timing after reset, with no subsequent candidate warm.

## Local evidence

- `correctness.json`: exact Joe–Kuo/source routing, every-date S/masks, copied S/Q/L identity, same-float price bits, parity, immutability, and float64 kink decomposition.
- `kink_decomposition.json`: all 16,430 float32-versus-float64 barrier-comparison disagreement events, including events with zero eventual price contribution.
- `price_errors.json`: all 188 tested call/put contracts with same-float price, float64 price, raw difference, flip contribution and smooth residual.
- `object_audit.json`: complete linked symbols, ordinary-route instruction contracts, dynamic counts, register liveness, working sets and structural gates.
- `package_audit.json`: performance-only AWS target and imported-carrier hash verification.
- `source_snapshot_status.txt` and `SOURCE_SHA256SUMS`: frozen checkout provenance and imported-file identities.

## Performance-only AWS execution

The AWS target invokes no NumPy, MPFR, coefficient generation, Intel SDE, or research/report generator. It builds the native binary and runs only the binary's bounded correctness preflight.

```sh
make -f tests/Makefile.asian_genuine_discrete_barrier -j2 aws-benchmark-native

mkdir -p results/asian_genuine_discrete_barrier

MKL_THREADING_LAYER=SEQUENTIAL \
MKL_NUM_THREADS=1 \
MKL_DYNAMIC=FALSE \
./bench_asian_genuine_discrete_barrier \
  --json results/asian_genuine_discrete_barrier/aws.json
```

`--json` uses exclusive creation and refuses to overwrite an existing result. After the raw JSON is committed and fetched locally, generate the deterministic decision without rewriting history:

```sh
python3 tests/analyze_asian_genuine_discrete_barrier.py \
  --input results/asian_genuine_discrete_barrier/aws.json \
  --json results/asian_genuine_discrete_barrier/qualification.json \
  --markdown results/asian_genuine_discrete_barrier/qualification.md
```

The analyzer gives all 80 cells equal weight, selects the bounded schedule/mask challenger from independent timing streams, applies the global 5% upper-bound gate and 10% cell guard, validates byte-identical A/A controls, and reports resident/table and oneMKL ratios. It emits qualification statuses only if every pre-registered gate passes.
