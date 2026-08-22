# Correctness qualification

## Gate 1: provenance

The exact base, all 221 pre-existing blobs, the Joe--Kuo table, and every
reused qualified artifact are recorded. The untouched ordered-D1 regression
passed under SDE; see `baseline_regression.txt`.

## Gate 2: canonical source identity

The deterministic table manifest has 8,192 unique payload and donor entries,
two regions of 4,096 values, packets 0--255, both 16-lane halves, and every
lane. Donors cover 8192--16383 without omission, duplication, or reordering.
The downstream point oracle separately verifies exact canonical target words
at indices 8192--12287. All 64 qualified hard-tail locations are tagged in the
manifest and contain finite values directly; there is no separate correction
payload.

## Gate 3: numerical source qualification

The independent MPFR-384 verifier reproduced every binary32 table bit. Its
maximum Gaussian rounding error is `1.1920402088343405e-07`, with signed mean
`-9.3573691225962335e-11`. Across all `N=2..256` and four frozen contracts, the
maximum affine-x error against exact inverse-normal plus prepared binary32
drift/diffusion is `3.4889912070343779e-08`; the signed mean over the full
matrix is `-1.0297403259935077e-12`. The largest raw ULP distance is 171653 near
zero and is retained in `independent_x_matrix.csv` rather than hidden.

The corresponding fixed-table versus X3 matrix contains all 1,020 N/contract
cells. Its maximum absolute difference is `1.1920928955078125e-07`, and the
maximum absolute cell signed mean is `2.6199330899201456e-10`. Positive and
negative counts, ULP distances, worst locations, and former-hard-tail status
are preserved per cell in `source_matrix.csv`. The ranked output and prepared
exact-x ceiling are bit-identical.

The new preparation delegates admissibility to the existing qualified
producer/full-risk domain. It adds no universal diffusion or drift bound.
Sigma zero is rejected for this ranked experiment.

## Gate 4: downstream qualification

The unchanged vector exponential, routes, S/Q/L evolution, single-strike and
multi-strike price/Delta/Vega/Rho consumers passed for N=16/32/64/128/256,
K=1/4/8/16/32, calls and puts, and arithmetic and beta=1 CV estimators. The
maximum kink-adjusted float64 residual was `8.25790821e-06`, maximum same-state
Greek error was zero, and the largest absolute signed-mean smooth residual was
`3.50820149e-07`. Raw indicator disagreements and residual decompositions are
retained in `downstream_validation.txt`.

## Gate 5: fixed-block enforcement

Preparation rejects N outside 2--256, another start index, another block,
continuation, scrambling, digital shifts, sigma zero, a misaligned table, and
a hash-invalid table. Domain-delegation tests cover all supported N values and
boundary-oriented market inputs.
