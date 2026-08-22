# Fixed-Block Asian One-FMA Source Diagnostic

This private additive diagnostic compares the qualified X3 general
generated-source baseline with a fixed-table challenger for exactly one
4,096-path canonical Asian block. The supported target paths are Joe--Kuo
indices 8192--12287 and runtime fixing counts are `N=2..256`. The challenger
does not support continuation, another starting index, scrambling, or a
digital shift.

This source is specialized and qualified only for the fixed 4,096-path
canonical Joe--Kuo block at indices 8192--12287. It does not implement Sobol
continuation, arbitrary-block generation or scrambling.

The existing carrier consumes 8,192 source values in two 4,096-float regions.
The frozen 32-KiB table therefore holds the exact signed binary32 Gaussian
values for donor D1 indices 8192--16383 in qualified consumption order. Region
zero is donors 8192--12287 and region one is the auxiliary carrier region
12288--16383. `signed_z_provenance.csv` records every payload index, donor
index, region offset, packet, half, lane, Sobol word, float bits, and qualified
hard-tail ordinal. The downstream route maps still produce the canonical
target paths 8192--12287 exactly.

The table uses `u=(word+0.5)*2^-32`, MPFR-256 inverse-normal generation, and
IEEE-754 binary32 round-to-nearest-even. An independent MPFR-384 monotone
bisection verifier reproduced every bit. All 64 locations handled by the X3
hard-tail pass are ordinary entries in this table; the ranked leaf has no
correction path.

The ranked leaf is `prepared fixed-block source consumption`: for every 32
values it performs two aligned signed-z loads, two affine ZMM FMAs, and two
aligned stores into the existing qualified x payload. It neither generates a
runtime Gaussian stream nor creates additional x/growth copies. The existing
qualified vector exponential consumes that x payload unchanged. The table and
x output total 64 KiB, so no L1D-residency claim is made.

Never describe this candidate as Sobol generation or inverse-CDF throughput.
It is prepared fixed-block source consumption. X3 remains the general
generated-source baseline.

`prepared_exact_x_lookup_ceiling` is an unranked copy ceiling. Its exact x
payload is prepared with the same binary32 FMA semantics outside timing, and
its cold preparation cost is reported separately. It is not a source
replacement and is never described as generation.

Local SDE correctness and final-linked structural evidence pass. Native
Sapphire Rapids evidence is intentionally absent: the local host lacks
AVX-512. No promotion or performance conclusion is made until the AWS run
meets the stated source and complete-pipeline gates in both cache modes.
