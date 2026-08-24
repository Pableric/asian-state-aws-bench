# Asian Meta-Direction Affine Route Contract

This private diagnostic is additive to production commit
`8e41ee4a918fddd0df1e1685038fdbcc7079f65a`. It does not alter public
dispatch or any parent mathematical kernel.

## Handoff provenance

The 448-byte context ABI and branch-free affine packet schedule were imported
from the audited, untracked provider handoff in the original research checkout.
The source inputs were read only and had these SHA-256 values:

- `dim_provider_handoff/dim_provider.h`:
  `d1647ec129b7221b264b645cbbfd26205285c6f49c2aa6e61e4473f90492fd7c`
- `dim_provider_handoff/dim_provider_fragments.inc`:
  `1b1c8cbc4214c8bd2ecd42f396507745a520d69eb24ca5bc0e20433fcc487827`
- `asian_affine_x_growth_1dim_avx512.s`:
  `40b77d91889e959bdcd900c5bd1bc0b6cc8d84637d9a9ff6d58798448a5efeea`
- `ASIAN_AFFINE_X_GROWTH_1DIM_CONTRACT.md`:
  `24e739b49a34556454f2ea4ee3565305b309474e34bf44caf1f9041f4a60cbed`

The imported ABI remains byte-for-byte compatible: selectors at bytes
0...255, scalar delta at 256, normalized base controls at 320 and repeated
half delta at 384, for a total of 448 bytes.

## Production construction boundary

The compiled D1...D256 descriptor object is exactly 8 KiB. Each 32-byte
record stores donor region, affine base and twelve affine columns.

The production builder never constructs a 4,096-entry mapping. It derives
seven cumulative jumps from columns 5...11 and emits exactly 128 first-half
packet states. Transitions use `ctz(q+1)`, whose exact jump counts are
64,32,16,8,4,2,1. Columns 0...3 form the normalized lane controls and column
4 forms the adjacent-line/half-control delta.

The complete 4,096-path recurrence, with full-jump counts
2048,1024,...,2,1, exists only in the correctness test. The native benchmark
contains the frozen Joe--Kuo records for the qsort control and provenance, but
does not open or parse a direction-number file at runtime.

## Candidate route boundary

Donor-region selection occurs while binding a route, outside every packet
loop. The standalone x+growth provider preloads the normalized base control
and half delta once, uses two selector bytes per packet and performs exactly
four payload `vpermd` instructions. The growth-only arithmetic candidate uses
the same controls, exactly two payload permutations, and retains the parent
S/Q recurrence, payoff, reductions and output order.

The diagnostic never changes public dispatch. Its benchmark decision is only
`AFFINE_ROUTE` when every measured wall/TSC cell is at least 0.99 and both
cross-cell medians exceed 1.01; otherwise it reports `GENERIC_ROUTE`.
