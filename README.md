# Asian Option Engine

Fresh development base for the next arithmetic Asian engine.

The executable source is an exact snapshot of the corrected ordered-D1
European engine at commit `4aa404d`. It is intentionally still a European
pricer: this gives the project a tested AVX-512 Sobol/Gaussian/payoff baseline
before Asian state and multidimensional work begins.

## Starting architecture

- Canonical consecutive D1 packets, 32 paths at a time in two ZMM registers.
- Prepared contract coefficients and sparse 64-point tail correction.
- Corrected 8,192-point call result: `10.4501428604`.
- Ordered kernel machine shape at 8,192 points: 4,206 instructions, 546 FMAs,
  one gather, zero permutes, and a 3,731-byte kernel symbol.
- Last native AWS result: `0.059418 ns/value` at 131,072 points.

## Multidimensional inputs

`direction_numbers/joe_kuo_6_21201.bin` contains the complete 21,201-dimension
Joe--Kuo 6.21201 table and is the standard baseline.

`direction_numbers/openevolve.json` preserves the 32-dimension OpenEvolve input
used by the previous Asian research harness.

The useful August 2026 Asian prototype is preserved under
`research/previous_asian/`. It is reference material only and is not linked by
the current build. The original complete directory remains recoverable at:

```text
/home/pablo/Projects/asian-option-engine-archive-20260804
```

## Build and verify the untouched base

```sh
make -j4
make test-ordered-d1-sde
python3 direction_numbers/verify_joe_kuo.py
```

The first implementation step is to add caller-selected D2/other-dimension
construction around the existing D1 packet schedule. Keep intermediate Sobol
values in registers unless native measurements prove materialization faster.

