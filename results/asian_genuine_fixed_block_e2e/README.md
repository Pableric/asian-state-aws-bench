# Fixed-block end-to-end benchmark

This additive package compares three sources inside otherwise identical
complete Asian valuations:

1. qualified X3 general generated-source baseline;
2. prepared fixed-block source consumption;
3. `prepared_exact_x_lookup_ceiling`.

The matrix covers `N=16/32/64/128/256`, arithmetic and beta=1 geometric-CV
estimators, candidate-warm and historical 32-KiB RMW modes, and these output
sets:

- one-strike price;
- one-strike price plus Delta;
- one-strike price plus Delta, Vega and Rho;
- full risk for `K=4/8/16/32`.

Each timed complete valuation invokes exactly one selected source, two existing
4,096-value vector-exponential calls, one existing route/evolution API, and one
existing consumer API. The source is not repeated by fixing, dimension,
strike, estimator, or Greek. Runtime instrumentation verifies the dynamic
counts before every native cell. The final-linked audit records the source and
common-consumer symbol hashes and call sites.

Complete measurements use alternating balanced ABBA/BAAB order, 16 warmup
quartets, 201 measured quartets, fenced TSC units, and
`CLOCK_MONOTONIC_RAW`. The JSON contains all raw quartet observations,
component profiles, numerical outputs, invocation counts, per-cell ratios, and
the global paired qualification statistic. Native Sapphire Rapids evidence is
not included in this commit.

No oneMKL implementation is built, linked, invoked, or tested. An optional
read-only importer accepts a frozen historical JSON only when its complete
contract and execution environment match exactly. Otherwise every cell says
`ONEMKL_REFERENCE_UNAVAILABLE`.

The package does not revise the earlier fixed-source promotion result. It
measures the effect of that already-qualified source on complete pipelines.
