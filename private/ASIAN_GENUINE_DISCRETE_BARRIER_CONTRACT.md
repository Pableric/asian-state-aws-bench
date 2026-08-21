# Genuine discrete-barrier diagnostic contract

This private diagnostic prices discretely monitored, zero-rebate knock-out calls and puts over 4,096 genuine Joe–Kuo paths. It supports runtime monitoring counts `1..256`; the benchmark matrix uses `16, 32, 64, 128, 256`.

The barrier is observed after each future GBM update at dates `1..N`. `S0` at time zero is not an observation. A directed initial knockout can be supplied only to the untimed mask-table test through its prepared initial mask.

Source production is frozen as:

1. `ordered_d1_x_only_diag` from the imported carrier.
2. Two calls to the imported qualified range-reduced vector exponential.
3. Direct D1 growth loads, the D1 S update, and the date-1 observation.
4. Compact routed entry zero for D2, followed by D3…DN.

The ranked packet-major leaf holds two S ZMMs and two alive masks for all monitoring dates. Down survival is strict ordered-quiet `S > barrier`; up survival is strict ordered-quiet `S < barrier`. Equality and NaNs knock out. There is no time-zero comparison, early exit, compaction, lane recycling, x routing, intermediate state store, payoff materialization, or public API.

Calls use `max(S-K,0)` and puts use `max(K-S,0)`, masked to zero at payoff. Knock-in exists only as an untimed exact parity oracle.

The primary resident self-mask route adds exactly two comparisons per 32 paths per routed date over matched vanilla. The explicit comparison and 512-byte mask table remain bounded native challengers. AWS selection and qualification are pre-registered in the result README and analyzer.
