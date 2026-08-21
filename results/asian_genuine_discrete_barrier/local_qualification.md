# Genuine discrete-barrier local qualification

Status: `LOCAL_CORRECTNESS_AND_STRUCTURE_QUALIFIED`; native Sapphire Rapids performance is pending.

The persistent two-mask update is exact. Every S value and alive bit after every monitoring date matches the independent scalar float32 recurrence, resident and 512-byte table masks are identical, and the copied S/Q/L state remains bit-identical to the frozen carrier.

The final linked N=256 instruction counts agree exactly between the disassembly formula and Intel SDE:

| Leaf | Dynamic instructions | Routed `vpermd` | S `vmulps` | Barrier comparisons | `kmovw` |
|---|---:|---:|---:|---:|---:|
| Matched vanilla | 688,538 | 65,280 | 65,536 | 0 | 0 |
| Resident self-mask | 754,330 | 65,280 | 65,536 | 65,536 | 0 |
| Resident explicit | 819,866 | 65,280 | 65,536 | 65,536 | 0 |
| 512-byte mask table | 950,683 | 65,280 | 65,536 | 65,536 | 131,072 |

The ordinary D2…DN route is 21 instructions for vanilla and 23 for the resident self-mask leaf. Its exact recurring delta is the required two ordered-quiet self-masked comparisons. There are no calls, gathers, stack references, spills, intermediate S stores, or payoff stores in any ranked leaf.

The independent float64 chronological comparison retains all 16,430 barrier-comparison disagreement events, including 46 events whose paths are later knocked out on both recurrences and therefore contribute zero to the final price difference. The largest raw case is the deterministic exact date-1 barrier: all 4,096 paths flip relative to float64, the raw `-2.9554466451494172` difference is exactly the reported indicator-flip contribution, and the smooth residual is zero. The maximum smooth residual over the complete matrix is `7.412193542366552e-05`, passing the fixed `1e-4` gate.

Current answers:

1. Persistent masks are exact: **yes**.
2. Native overhead over vanilla: **pending AWS paired evidence**.
3. Resident versus table performance: **pending AWS paired evidence**.
4. Complete oneMKL speedup: **pending AWS paired evidence**.
5. Continuous-barrier/Brownian-bridge work: **not yet justified until the hard native performance gate passes**.

Up-and-out call/put leaves passed the separate correctness stage but remain explicitly correctness-only because the pre-registered native ranking is down-and-out.
