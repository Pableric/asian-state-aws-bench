# Asian strip Delta replication qualification contract

This contract was frozen before the first aggregate randomized qualification
run.  It is additive to package commit
`1b199076ae9fa6db2258172587b59fef400e11ff` and does not authorize a change to
either strip pricing source.

## Frozen implementation and inputs

The production sources under test and their required SHA-256 values are:

```text
907c4a05807de27d8ec3c226382e6b640048a87fcd31f9b97d82c5c37668dced  asian_genuine_price_delta_strip_setup.c
42c52432e1c0b49956d5db11305e9b7d6a8f8c86d35c5fa819ea96443f03893e  asian_genuine_price_delta_strip_avx512.s
fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199  direction_numbers/joe_kuo_6_21201.bin
```

All production-route cases use `S0=100`, `r=0.03`, `q=0`, `sigma=0.20`,
`T=1`, `completed_fixings=0`, `Q0=0`, and `past_log_sum=0`.  The fixing grid is
`N={16,32,64,128,256}`.  Strike counts are `{1,4,8,16,32}` with the exact
float32 nested grids already frozen by the strip ABI.  Arithmetic and beta-one
geometric-control estimators, calls, and puts are all tested.

The nested point prefixes are `{512,1024,2048,4096}` from Sobol point 8192.
The AVX-512 strip leaf is checked at its native 4096-path block size; scalar
float-state and independent long-double references are checked at every
prefix.

## Digital-shift replications

There are exactly 32 statistically evaluated replications.  For replication
`r=0..31`, initialize a 64-bit SplitMix state as

```text
state = 0xd1e17a5eedc0ffee + 0x9e3779b97f4a7c15 * (r + 1)  (mod 2^64)
```

For dimensions `d=0..255`, advance SplitMix64 once and use the low 32 bits of
its output as the dimension's digital XOR shift.  The report records every
initial state and a hash of every resulting 256-word shift vector.  An
additional unshifted run is reserved for the canonical N=128/K=98 path-471
kink check and is excluded from randomized statistical estimates.

For every dimension, replication, and tested point, direct Gray-code Joe--Kuo
reconstruction must equal an independent recurrence reconstruction before the
shift is applied.  The shifted D1 coordinate must be unique over 4096 points;
the four prefixes must be literal nested expansions with no changed earlier
word.

## Independent numerical references

The reference normal inverse starts from the Acklam rational approximation and
is refined with long-double `erfcl`/`expl` Newton steps.  Path evolution,
arithmetic/geometric states, payoff comparisons, pathwise derivatives, exact
geometric controls, and bump prices are evaluated independently in long
double.  The randomized float state uses the exact shifted words, float
increments, a scalar instruction-order transcription of the already-qualified
range-reduced exponential, and the declared float S/Q/L operation order.  It
writes a frozen Q/G corpus.  A separate verifier makes the unchanged production
price and price-plus-Delta leaves consume that corpus.  The AWS target executes
the verifier natively; a non-AVX-512 development host may use SDE only for this
small verifier and the pre-existing package tests, never for the statistical
generator.  This execution split was fixed before the first completed
aggregate run and changes none of the gates below.

Common-random-number bump-and-revalue uses the same reconstructed shocks on
both sides.  Call and put bump prices are each evaluated directly; neither
bump reference is derived from parity.  The three spot bump sizes are

```text
h/S0 = 2^-12, 2^-14, 2^-16
```

The smallest bump is the reported bump reference.  Finite-difference
uncertainty for a contract is the larger absolute mean change between adjacent
bump levels plus five standard errors of that adjacent-level change.  No
finite difference is used inside the production estimator.

A float/reference path is kink-ambiguous when the strike lies inclusively
between its completed float-state value and independent long-double value.
Arithmetic and geometric counts and indicator-flip contributions are recorded
separately.  Paths are never removed, perturbed, smoothed, or assigned a
nonzero derivative at equality.

## Pre-registered gates

All gates below must pass.  The existing absolute price and smooth-Delta gate
of `1e-4` is retained and is never relaxed.

1. **Frozen sources:** all three hashes above match and the qualification
   commit changes neither pricing source nor any existing production kernel.
2. **Sobol exactness:** zero direct-versus-recurrence mismatches, zero duplicate
   shifted D1 words, and zero prefix-expansion violations in every tested
   replication and N.
3. **Price preservation:** tile-4 and tile-8 prices are bit-identical; prices
   emitted by price-only and price-plus-Delta leaves are bit-identical; the
   package's existing complete price tests remain passing.
4. **Same-state Delta:** every 4096-path AVX output differs from the independent
   float-Q/G oracle by at most `1e-6`; absolute overall signed mean error is at
   most `1e-7`.
5. **Independent pathwise Delta:** after subtracting the exactly reported
   arithmetic/geometric indicator-flip contribution, maximum absolute smooth
   residual is at most `1e-4` and absolute overall signed smooth residual is at
   most `1e-6`.  Among residuals larger than `1e-8`, neither sign may exceed
   95 percent when at least 20 observations are present.
6. **CRN bump agreement:** for every N/count/strike/estimator/side/prefix
   contract, absolute mean paired pathwise-minus-smallest-bump difference must
   be no larger than `5*SE + finite_difference_uncertainty + 1e-6`.  Thus the
   simultaneous expanded confidence-interval coverage must be 100 percent.
7. **Convergence:** pooled pathwise-versus-bump RMSE at 4096 paths must be no
   more than 80 percent of its 512-path value plus `1e-6`; the median
   across-replication estimator standard error at 4096 paths must be no more
   than 80 percent of its 512-path value plus `1e-6`; and the median contract
   standard error using 32 replications must be no more than 80 percent of the
   corresponding 8-replication value plus `1e-8`.
8. **Parity/direct-side validation:** price and Delta parity identities retain
   the `1e-4` bound, and randomized parity-derived estimates show no signed
   discrepancy outside the same simultaneous sampling/finite-difference band.
9. **Canonical kink:** the unshifted N=128/K=98 arithmetic ambiguity remains
   path 471, its unadjusted difference and flip contribution are both printed,
   and its non-kink residual is at most `1e-4`.
10. **Analytic and baseline regressions:** the independent exact-geometric
    price/Delta central-difference test and the ordered-D1 baseline remain
    passing.

The decision is `DELTA_QUALIFIED` only if every gate passes.  Any failed gate
produces `DELTA_REMAINS_DIAGNOSTIC` and identifies the precise worst contract.
Completed-fixing route production remains outside this decision.
