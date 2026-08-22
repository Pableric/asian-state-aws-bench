# Correctness qualification

The dedicated SDE vector test passed with:

```text
asian_genuine_multistrike_full_risk_hybrid_dispatch=PASS K=1..32 policy=phase1,tile2,tile4 streaming=two_blocks N_k1=2,16,256 arbitrary_strike_order=yes bit_identity=yes
```

For every `K=1...32` and both arithmetic and geometric-CV estimators, the test
compares final two-sided price/Delta/Vega/Rho bits with both qualified uniform
tile candidates.  For `K>=2`, it also compares every real strike's raw double
sum bits.  It checks two successive block consumptions, accumulator path/block
counts, prepared-context immutability, basis immutability, padded-output
discard, and arbitrary input strike order.

The `K=1` route was exercised at `N=2,16,256` with call-direct, put-direct, and
ATM strikes.  Its existing Phase-1 normalized result is converted by an exact
power-of-two factor into the streaming raw-sum representation; one- and
two-block finalization both match the qualified output by bits.

The benchmark's bounded SDE preflight separately passed every dispatch shape
at `K=1,2,3,4,5,6,7,8,31,32`, both estimators, and the `K=1` fixing boundaries
`N=2,16,256`.
