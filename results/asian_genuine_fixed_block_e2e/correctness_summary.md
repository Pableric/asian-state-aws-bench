# Correctness and isolation summary

The base commit already qualifies the fixed source against the independent
high-accuracy source matrix and the complete downstream price/Greek matrix.
Those frozen files are reused unchanged and are hashed in
`reused_artifact_sha256.txt`.

The additive preflight runs all three complete pipelines at N=16 and N=256 for
price, price plus Delta, one-strike full risk, and K=32 full risk, with both
arithmetic and geometric-CV estimators. It requires:

- prepared exact-x and prepared fixed-block results to be bit-identical;
- fixed-block versus X3 maximum raw output difference at most `1e-3`;
- immutable route and signed-z payload hashes;
- exactly one source, two vector-exp calls, one evolution, and one consumer API
  call per complete valuation;
- exact underlying SQL/basis, geometric conversion, strip, accumulator,
  finalizer, and tile-4 invocation counts.

The SDE preflight passed. Observed fixed-block versus X3 raw differences ranged
from zero to `1.90734863e-06`. The earlier independent qualification remains
unchanged: maximum kink-adjusted float64 residual `8.25790821e-06`, same-state
Greek error zero, and largest absolute signed-mean smooth residual
`3.50820149e-07`.

The final-linked audit passes. It finds one linked source wrapper and one source
leaf call site for each candidate, one shared downstream wrapper sequence, two
vector-exp call sites, and one call site for each existing route/evolution and
consumer API. The common linked-symbol hash map is identical for all three
candidates. `ldd` and undefined-symbol inspection find no oneMKL dependency.

Performance qualification is intentionally deferred to native Sapphire Rapids.
No timing result from SDE is used or retained as evidence.
