# Seasoned Asian price/Delta strip

Local correctness is qualified.  Final status is
`SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING` until the committed
native AWS benchmark clears every paired timing gate.

The genuine matrix has maximum complete price error `2.5400982797e-5`, maximum
same-Q/G Delta error `5.73636498302e-8`, and maximum kink-adjusted complete
Delta residual `1.49483309943e-7`.  The unadjusted Delta difference reaches
`0.75` in deterministic exact-kink cases; those paths are retained and their
indicator contributions are recorded rather than hidden.

- `qualification.json` and `QUALIFICATION.md`: 32-shift long-double/CRN study,
  exact contract metadata, aggregate bias/RMSE/coverage, and worst cases.
- `kinks.jsonl`: all ambiguous observations in the randomized study.
- `local_correctness.json`: genuine-path principal and smoke matrices.
- `complete-kinks.jsonl`: all ambiguous observations from the genuine,
  irregular-count, rate-sign and sigma-zero matrix.
- `structural_audit.json` and `STRUCTURAL_AUDIT.md`: exact zero hot-loop delta,
  register/liveness, dependency, instruction, text and working-set evidence.

No production kernel or historical result was changed.  AWS instructions are
in `private/AWS_SEASONED_PRICE_DELTA_STRIP.md`; the AWS target is intentionally
performance-only.
