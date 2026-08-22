# Ranked leaf lifetime summary

The machine-readable linked audit contains every disassembled instruction,
its purpose, and an instruction-by-instruction backward-liveness row. The
conservative true simultaneous ZMM-family peaks are:

| Leaf | ZMM families | Structural result |
|---|---:|---|
| strike-independent forward basis | 22 | pass |
| arithmetic tile 2 | 13 | pass |
| arithmetic tile 4 | 19 | pass |
| geometric-CV tile 2 | 14 | pass |
| geometric-CV tile 4 | 20 | pass |

The basis loop reuses the Phase-1 route controls after both half-packet
permutes. Routed x/growth values die after their state/sensitivity uses; the
eight basis outputs are written once after their packet values become final.
The old eight persistent single-strike payoff accumulators are absent.

Consumer basis vectors die after each risk-field traversal. Payoff masks feed
only the corresponding contribution, and lane accumulators survive until the
single post-traversal horizontal reduction. Source/Sobol state is not live in
any consumer. No ranked leaf has a call, stack reference/frame, vector spill,
gather, scatter, scalar libm, runtime side/estimator switch, or data-dependent
branch.

SDE dynamic counts are measured with one basis invocation and 64 consumer
invocations; the audit reports both totals and per-invocation counts. Repetition
is used only so SDE's function summary retains the compact consumers.
