# Ranked leaf register lifetimes

This table was derived backward across each recurring instruction group. A
register family is counted once even when an XMM/YMM name is used during final
reduction. Physical assignment is fixed only in the private additive symbol.

| Recurring instruction/stage | Values live before | Defined/clobbered | Dies immediately after | Live-family peak |
|---|---|---|---|---:|
| packet entry | output sums `zmm20..27`, zero `zmm31` | S `zmm4..5`; clears Q/cumulative/L/weighted sums | none | 21 forward / 15 suffix |
| direct D1 x/growth loads | forward state plus packet offset | x `zmm0..1`, growth `zmm12..13` | loaded growth after S multiply | 25 |
| shared selector decode | persistent recurrence and output sums | payloads `zmm2..3,zmm14..15`, controls `zmm18..19`, results `zmm0..1,zmm12..13` | controls after both x/growth permutes; payloads after permutes | 31 forward / 25 suffix |
| forward state update | S/Q/L plus forward contractions | S multiply, Q add, L FMA, cumulative-x add, rho-weighted and x-weighted FMAs | routed x after L/cumulative/weighted use; routed growth after S multiply | 31 |
| suffix tape store | S/Q/L and output sums | two 64-byte stores at the fixing-major tape offset | taped-S store operands remain S recurrence | 25 |
| geometric exp | A/L and output sums | exponent/reduced scratch `zmm0..3`, exp input `zmm14..15`, G `zmm12..13` or `zmm16..17` | exponent/reduced/temp input immediately after exp | 25 |
| reverse taped-S loads | output sums, A/G/G-vega, suffix/rho/x-dot | taped S is added directly into `zmm4..5` suffix | taped S memory operand immediately after suffix addition | 21 |
| x-only reverse selector decode | prior reverse state | x payload `zmm2..3`, controls/results `zmm0..1` | controls after both permutes | 25 |
| reverse contraction | suffix `zmm4..5`, rho `zmm6..7`, x-dot `zmm28..29` | rho additions and x-dot FMAs | routed x immediately after x-dot FMA | 21 |
| direct D1 reverse tail | same reverse contractions | direct x `zmm0..1`; final suffix/rho/x-dot | direct x after x-dot FMA | 23 |
| payoff consumption | A/A-rho/A-vega, G/G-vega, output sums | masks `k1/k7`, scratch `zmm0..3`; output sums | each basis value after its price/Greek sample; no per-path output | 25 |
| final reduction | eight float accumulators | `xmm0..3` tree, scalar double scale/add/store | each accumulator pair after its field reduction | 12 |

Forward reaches 31 of 32 ZMM families during shared x/growth routing. Suffix
reaches 25 because the tape removes the forward sensitivity contractions from
the routed recurrence. No ranked leaf references `rsp`/`rbp`, and no vector
value is transferred to a spill slot.
