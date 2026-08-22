# Genuine Asian Multi-Strike Full-Risk Strip

This additive private diagnostic combines the qualified Phase-1 targeted-forward
basis with a two-stage multi-strike strip for 4,096 genuine Joe--Kuo paths.
The supported domain is runtime `N=2..256` and runtime `K=1..32`; native ranking
is restricted to `N=16,32,64,128,256` and `K=1,4,8,16,32`.

The ranked basis leaf consumes the existing qualified 64-KiB x/growth source
payloads exactly once. It does not materialize Gaussians and creates no
additional x/growth copies. It writes eight float32 SoA basis arrays exactly
once (nominally 128 KiB). The tile-2 and tile-4 consumers traverse that basis
without re-running source production or path evolution.

Every repeated Phase-1 benchmark produces the same two-sided outputs as the
strip: it runs only the prepared direct-side leaf for each strike and derives
the opposite side through the shared price/Delta/Vega/Rho parity finalizer.
Thus it uses K evolutions, not 2K. Independent call and put evaluation remains
correctness-only. The original Phase-1 leaf is a K=1 component row only.

The accumulator/finalizer ABI holds unnormalized double sums, completed path
count, completed block count, and finalization metadata. Re-consuming the same
block twice is normalization invariant and adds the analytic control/parity
only once. This establishes an ABI boundary for future streaming but does not
claim Sobol continuation. A later producer may load and store two continuation
ZMMs outside the consumer lifetime; exact cross-block words, maps, identity,
and chunking invariance remain unqualified here.

The registered contract is `S0=K_ATM=100`, `r=0.03`, `q=0`, `sigma=0.20`,
`T=1`. Native CRN uses relative epsilon `1e-3`: spot +/-0.1, rate
+/-0.00003, and sigma +/-0.0002. Our CRN uses five source productions and seven
evolutions. oneMKL CRN produces one Gaussian matrix and performs seven matched
evolutions; the matched oneMKL forward-basis candidate produces one Gaussian
matrix and one evolution.

`No calls` is a ranked-leaf structural rule. Complete-pipeline timing includes
the existing qualified source-production/vector-exponential calls, audited as
separate pipeline boundaries rather than as one monolithic call-free binary.

Tile selection is deliberately not made by local static/SDE evidence. Both
spill-free tiles remain in the AWS package; the native JSON reports paired TSC
and wall evidence in both cache modes. If the modes disagree, there is no
universal winner.

The benchmark refuses to replace an existing success JSON. Success publication
uses an atomic no-replace rename/link operation. Failure artifacts are valid
JSON, use the same no-replace publication rule, and select a unique path on a
collision.

Files in this directory are deterministic local qualification evidence. Native
AWS performance JSON is intentionally absent until run on the registered
Sapphire Rapids host.
