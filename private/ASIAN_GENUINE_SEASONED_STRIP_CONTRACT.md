# Seasoned Asian strip qualification contract

This contract is frozen before aggregate seasoned results are examined.  The
additive work is based on commit
`0482aaea01c258ccfc15de2d2927744d10980e1c` and must not change any qualified
producer, route, payoff, Delta, benchmark, or historical-result file.

## Frozen dependencies

```text
907c4a05807de27d8ec3c226382e6b640048a87fcd31f9b97d82c5c37668dced  asian_genuine_price_delta_strip_setup.c
42c52432e1c0b49956d5db11305e9b7d6a8f8c86d35c5fa819ea96443f03893e  asian_genuine_price_delta_strip_avx512.s
e2e54ebca65f6586cbb2fbfa0621e20a63dbaafec3cbbdb2515f5a9a432a0301  asian_genuine_permute_setup.c
08f685f29e86a1480269797ff4ee4313088f33c3275af727faa72c12d1957709  asian_genuine_sql_variable_avx512.s
886bcc7bdd71ff7355c4b7b278a7d19b5608c2fbbed7b9983cd98ff1608f7f08  ordered_d1_x_growth_handoff/ordered_d1_x_growth_setup.c
bbc08b0348309e47852b550d53f9008e05c880475f6ce4b1899ab69844aa5b89  ordered_d1_x_growth_handoff/sobol_ordered_d1_x_growth_diag_avx512.s
78331e0b544b983d3b32d3fabc33745e858463aebca53928d7b6843f60802ee6  asian_geometric_cv_payoff_avx512.s
fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199  direction_numbers/joe_kuo_6_21201.bin
5afe2cdb676f3cfc6c5862ba45a4ed395f9d918cd2cf9ae4dc6a8a89e427f78b  results/asian_genuine_delta_qualification/qualification.json
0200d1abd9c9bbe40751d7582473bce13115fba8ffcb7d10ad7a6dca921bbd2e  results/asian_genuine_delta_qualification/audit.json
```

Routed `q` is the future-only sum and is initialized to positive zero.  The
completed sum Q0 exists only in `asian_genuine_strip_context_t.initial_q`.
Neither Q0 nor completed-fixing logic may enter source production or S/Q/L
evolution.  The existing route symbol consumes prepared weight bits for D1 and
all later dimensions, so no new assembly symbol is permitted unless that audit
finding is disproved.

## Fixed inputs

Total counts are `M={16,32,64,128,256}` and completed counts are
`c={0,1,M/4,M/2,M-1}`, with `f=M-c`.  Runtime smoke pairs are
`(c,f)={(0,1),(1,1),(1,2),(2,15),(7,26),(63,64),(128,127),
(1,255),(255,1)}`.  Rates are `{-0.02,0,0.03}`, volatilities are `{0,0.20}`, and
remaining maturity is one year at `S0=100`, dividend yield zero.  Every fixed
strike count, tile, estimator, and option side is tested.

Completed histories are chronological float64 values:

1. flat `100`;
2. increasing `80 + 40*(i+1)/(c+1)`;
3. decreasing `120 - 40*(i+1)/(c+1)`;
4. the repeating cycle `{82,117.5,94.25,108,76.5,123.75,101,89.5}`.

The replication study reuses the 32 digital shifts, prefixes
`{512,1024,2048,4096}`, and CRN spot bumps `2^-12`, `2^-14`, and `2^-16` from
the committed Delta qualification.

## Numerical gates

All gates must pass in stage order.

1. Every `(f-k)/M` bit is independently verified for every valid
   `1 <= f <= M <= 256`.  Maps, source pointers, and fixing indices do not
   change.  `c=0` routes and payoff contexts are bit-identical.
2. Stage-2 prices have maximum absolute error at most `1e-4`, absolute signed
   mean error at most `1e-6`, and no sign above 95 percent among errors larger
   than `1e-8`.  Tile outputs and unseasoned outputs are exact by bits.
3. Stage-3 prices equal Stage 2 by bits.  Same-Q/G Delta has maximum error at
   most `1e-6` and absolute signed mean at most `1e-7`.  Complete Delta after
   subtracting reported indicator flips has maximum error at most `1e-4`,
   absolute signed mean at most `1e-6`, and the existing 95-percent sign gate.
4. Direct call/put parity and all CRN bump contracts have 100-percent expanded
   confidence-interval coverage.  Prefix and replication convergence retain
   the committed Delta-qualification policy.
5. Every ambiguous arithmetic or geometric path is retained and reported with
   its exact flip contribution.  Equality uses zero derivative.

## Structural and native gates

The seasoned and matched-f unseasoned paths execute the identical producer,
route, L-to-G, and payoff symbols.  Per-route and complete-leaf dynamic
instruction, load, store, permute, arithmetic, and branch deltas must be zero.
Calls, gathers, stack traffic, spills, intermediate state stores, and new hot
dependencies are forbidden.

For each 51-sample native seasoned/unseasoned pair, both TSC and wall-time
ratios must have median at most `1.01` and 33rd ordered observation (the exact
95.11-percent nonparametric median-CI upper endpoint) at most `1.02`.

Before AWS timing the decision is
`SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING`.  Final
`SEASONED_STRIP_QUALIFIED` requires the native performance gates.
