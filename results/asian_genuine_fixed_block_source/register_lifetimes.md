# Final-linked register lifetimes

The complete instruction-by-instruction reverse-liveness tables are embedded
in `linked_structural_audit.json` for all three final linked symbols.

For `asian_genuine_fixed_block_signed_z_one_fma_source_diag`, `zmm2` holds the
prepared drift and `zmm3` holds diffusion for the loop lifetime. `zmm0` and
`zmm1` become live at their two signed-z loads, are updated independently by
the affine FMAs, and die at their corresponding x stores. The true peak is
four ZMM families. GPRs are `rdi`, `rsi`, `rax`, and `rcx`; no mask register is
used.

For `asian_genuine_fixed_block_prepared_exact_x_lookup_diag`, `zmm0` and
`zmm1` live only from the two prepared-x loads through their stores. The true
peak is two ZMM families. Its GPR set is the same and no mask is used.

The qualified X3 leaf is reported unchanged. Its established simultaneous
peak is ten ZMM families. The audit records every static instruction, purpose,
register set, static and SDE-dynamic class, memory width, branch, FMA, permute,
and final-linked text size. The new ranked leaf has no calls, stack references,
spills, gathers/scatters, scalar hard-tail path, or data-dependent branch.

On Sapphire Rapids the recurring ranked packet has two 64-byte loads, two
independent FMAs, and two 64-byte stores. It shortens the X3 polynomial plus
hard-tail dependency chain to one affine FMA. The dynamic audit removes 1,024
vector FMAs, 424 scalar hard-tail FMAs, and 11,494 instructions per source
call. It replaces 128 KiB of recurring X3 coefficient reads with 32 KiB of
signed-z reads, a modeled 96 KiB net reduction in recurring wide-read traffic.
Native port and cache behavior still require AWS measurement.
