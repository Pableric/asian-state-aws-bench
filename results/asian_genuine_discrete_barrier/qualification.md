# Genuine Discrete-Barrier Native Qualification

Status: `DISCRETE_BARRIER_NATIVE_PERFORMANCE_FAILED`

Selected leaf: `resident_self_interleaved`.

## Decision gates

- TSC global ratio `1.029587`, 95% upper `1.029641`.
- Wall global ratio `1.029322`, 95% upper `1.029378`.
- Worst cell medians: TSC `1.103891`, wall `1.101701`.
- Byte-identical A/A method control: `PASS`.
- Resident masks materially beat the table: `YES`.
- Ours beats matched oneMKL in both cache modes: `YES`.

The preserved local correctness and linked-object audits are prerequisites identified by SHA-256 in the JSON report.
The raw float32-versus-float64 barrier-side differences remain reported in `correctness.json`; only the explicitly decomposed indicator-flip contribution is removed from the smooth residual gate.

Continuous-barrier/Brownian-bridge follow-up justified: `NO`.
