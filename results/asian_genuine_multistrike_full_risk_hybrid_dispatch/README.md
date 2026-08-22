# Genuine Asian full-risk hybrid dispatcher follow-up

This additive follow-up leaves the qualified Phase-1 and multi-strike ranked
leaves unchanged.  It adds an untimed/private dispatcher with this fixed
policy:

- `K=1`: the existing Phase-1 prepared direct-side forward full-risk leaf.
- `K=2`: tile 2.
- `K=3` or `K=4`: tile 4, with one prepared padded output for `K=3`.
- `K>4`: complete groups of four use tile 4.
- A final remainder of one or two uses tile 2; remainder one is padded once.
- A final remainder of three uses tile 4 and is padded once.

Padding reuses the qualified prepared padding records.  The dispatcher writes
groups in input strike order, the finalizer ignores padded slots, and path and
block counts advance once per complete 4,096-path block.  It introduces no
instruction or branch into the four frozen ranked consumers.

The separate benchmark executable is
`bench_asian_genuine_multistrike_full_risk_hybrid_dispatch`.  Its default
native matrix covers `N=16,32,64,128,256`, every `K=1...32`, both estimators,
and both established cache modes.  It refuses to overwrite an existing success
JSON.

No native result was generated on the local AMD Ryzen 5 2600 host, which lacks
AVX-512.  Therefore this package makes no arbitrary-`K` performance claim.
