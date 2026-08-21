# Genuine Asian price-and-Delta strip AWS benchmark

The AWS workflow is performance-only. Price qualification and replication-level
Delta qualification must be completed and committed locally before this package
is transferred to AWS. The prerequisite qualification commit is
`058d97fe2442580d0eb7904fa18aadad96955450`; its decision is
`DELTA_QUALIFIED` and its audit status is `PASS`.

The committed prerequisite evidence is identified by these SHA-256 hashes:

```text
5afe2cdb676f3cfc6c5862ba45a4ed395f9d918cd2cf9ae4dc6a8a89e427f78b  results/asian_genuine_delta_qualification/qualification.json
0200d1abd9c9bbe40751d7582473bce13115fba8ffcb7d10ad7a6dca921bbd2e  results/asian_genuine_delta_qualification/audit.json
6ba4bbe2d4afb4cf1cd2582dc32612c55f950a4d3269ee3de8d09b6552f3dcc2  results/asian_genuine_delta_qualification/replication_raw.json
e4b800276b052397b52a12ce6c5e9783817b807da4940109850625dbba2d29af  results/asian_genuine_delta_qualification/production_verify.json
```

Completed-fixing route production is not claimed. Nonzero completed-fixing
coverage is payoff-state isolation only; route support remains a future plan.

On AWS, `aws-benchmark-native` builds only the frozen benchmark and its compiled
dependencies, then runs only the benchmark's built-in native `--check-only`
preflight. It does not invoke coefficient generators, NumPy, Intel SDE, MPFR
research audits, replication qualification, or report generation. Do not use
`aws-ready`, `benchmark-check`, or the Delta qualification targets on AWS.

After the preflight passes, run the benchmark separately. The example output
path below preserves the committed historical `aws.json` result.

```sh
make -f tests/Makefile.asian_genuine_price_delta_strip -j2 aws-benchmark-native
MKL_THREADING_LAYER=SEQUENTIAL MKL_NUM_THREADS=1 MKL_DYNAMIC=FALSE \
  ./bench_asian_genuine_price_delta_strip \
  --json /tmp/asian_genuine_price_delta_strip_aws_rerun.json
```
