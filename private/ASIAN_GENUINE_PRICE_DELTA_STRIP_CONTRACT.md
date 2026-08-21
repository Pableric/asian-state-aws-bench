# Private genuine Asian strip diagnostic contract

This additive diagnostic evolves 4,096 paths once and consumes the completed
S/Q/L state for a nested Asian price strip. Price-only results are qualified.
Delta results remain `KINK_AMBIGUITY_REPORTED_DIAGNOSTIC` until a future
replication-level accuracy study is complete.

## Frozen complete path

The only genuine source path is the corrected sequence:

1. `ordered_d1_diag_prepare(..., ORDERED_D1_DIAG_PREPARE_X3, N)`
2. `ordered_d1_x_only_diag(256, ...)`
3. two qualified `asian_vector_exp_range_reduced_array_diag` calls
4. `asian_genuine_sql_dual_control_diag`

Direct X3/GROWTH3 carrier growth is not accepted. The shared-control route,
maps, weights, and S/Q/L kernel are unchanged.

End-to-end evolution and native timing require `completed_fixings=0`, `Q0=0`,
and `past_log_sum=0`. Nonzero completed-fixing tests supply independent states
with `(f-k)/M` weighted L and validate only payoff isolation. Producing such
states in the route loop is a future route-plan extension, not part of this
diagnostic.

## Payoff state

Preparation stores immutable

```text
log_base = past_log_sum/M + (future_fixings/M) * log(S0)
```

and the vector leaf reconstructs one G array as `exp(log_base+L)`. The strip
does not modify Q, L, G, strikes, or prepared constants. Four- and eight-strike
leaves are the only tiled candidates.

The fixed float32 grid is

```text
70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100,
100.5,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130
```

Each prepared record retains the exact bits, call moneyness relative to the
N-specific expected arithmetic average, the option-correct put counterpart,
and the nearest-ATM marker.

## Delta interpretation

The identical completed float Q/G state is a hard correctness boundary. A
float64 consumer of those same states must agree within `1e-4`.

For comparison with independent float64 path evolution, a path is ambiguous
when its strike lies between the completed float-state value and independent
float64-path value. Reports retain arithmetic and geometric ambiguity counts,
the exact aggregate indicator-flip contribution, the unadjusted Delta
difference, and the difference after subtracting the flip contribution. Only
the residual is gated at `1e-4`; ambiguous paths are never hidden or discarded.

At N=128/K=98, path 471 is the required arithmetic ambiguity. The diagnostic
must print `KINK_AMBIGUITY_REPORTED` and show that its flip contribution
explains the approximately `2.32168028e-4` unadjusted difference.
