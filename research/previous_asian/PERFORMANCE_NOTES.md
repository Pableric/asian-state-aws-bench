# Performance status

The only available machine is not AVX-512 capable.  Intel SDE validates the
instruction stream, but its wall-clock time is not a hardware throughput
measurement.  A one-block (`8192` paths) rank1 call produced this dynamic mix:

- kernel: 1,263,711 dynamic instructions;
- full process: 2,872,391 dynamic instructions;
- 8,192 `vpermi2ps` + 8,192 `vpermt2ps`: exactly two coefficient
  permutations per dimension/phase for both ZMM halves together;
- 16,384 `vrndscaleps` and 16,384 `vscalefps` for the two vector exponentials;
- no `vgatherdps` in the hot object.

The exact masked Acklam tail path currently dominates the avoidable cost:
`asian_inverse_normal_u32` plus libm helpers account for roughly 47% of the
full SDE instruction count.  Therefore the next optimization is clear: repair
and qualify the generated shared degree-5 tail bank, then replace this exact
fallback except for the three rejected endpoint cells.  Until that is done and
measured on Sapphire Rapids, no cycles/path or speedup claim is valid.

The central path's coefficient cost for 32 paths and one dimension is four
64-byte line loads, two coefficient permutations, two raw-value FMAs, and the
cheap selector broadcast/XOR.  The two path ZMMs share the selected c0/c1
vectors; coefficient selection is not repeated for the second half.
