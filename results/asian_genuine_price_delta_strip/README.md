# Genuine Asian price-and-Delta strip diagnostic

This result path is additive. Price-only candidates are `QUALIFIED`. Delta
candidates are reported separately as `KINK_AMBIGUITY_REPORTED_DIAGNOSTIC`
until replication-level Delta accuracy is tested.

Completed-fixing route production is not claimed. Nonzero completed-fixing
coverage is payoff-state isolation only; route support remains a future plan.

The native AWS run writes `aws.json` here. No AWS run is performed while
packaging the diagnostic.

```sh
make -f tests/Makefile.asian_genuine_price_delta_strip -j2 aws-ready
MKL_THREADING_LAYER=SEQUENTIAL MKL_NUM_THREADS=1 MKL_DYNAMIC=FALSE \
  ./bench_asian_genuine_price_delta_strip \
  --json results/asian_genuine_price_delta_strip/aws.json
```
