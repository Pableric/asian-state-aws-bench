# Arithmetic Growth-Only and Immediate-Consumer Diagnostic Contract

This private diagnostic consumes the existing qualified 32-byte route records
without modifying their maps or selection semantics.  D1 is the direct first
4,096-value growth donor.  D2 through DN read only `growth_base` at route offset
8 and `map` at route offset 16.  The x pointer, weight and fixing payloads are
not inputs to the ranked leaf.

For every path, using distinct binary32 operations:

```text
S[0] = f32(S0 * growth[0])
Q[0] = S[0]
S[k] = f32(S[k-1] * growth[k])
Q[k] = f32(Q[k-1] + S[k])
```

The qualified source and exponential ranges make every D1 state finite and
positive.  IEEE-754 addition of positive finite `S` to positive zero is exact,
so the register move used for `Q[0]` is bit-identical to the qualified dual
leaf's `f32(+0 + S)`.

The Stage-1 leaf processes 128 canonical packets of 32 paths, never stores
intermediate state, stores exactly 16 KiB of terminal Q, has no stack frame or
calls, and uses two `vpermd` per routed fixing.  The test-only packet probe is
outside the ranked surface and records growth/S/Q after each fixing.

Six additional private leaves implement K=1, tile-2 and tile-4 arithmetic
price and price+Delta.  They use the identical D1 and routed recurrence, keep
terminal Q packet-local, and preserve the qualified consumer's two half-packet
accumulator streams and scalar reduction order.  Their four arguments are the
growth context, prepared arithmetic strip context, prepared strike records and
final output records.  They never read the retained Stage-1 `q_out` field at
context offset 16.  Tests poison that field with address 1 before every direct
immediate call.

The immediate leaves have no Q or intermediate vector-state stores or reloads.
Their required final scalar call/put price and Delta output-record stores are
not vector-state stores.  K=3 is prepared outside the leaf by duplicating the
last strike into tile 4 and discarding the padded output.  K>=5 and any rejected
tile-4 price+Delta shape retain the Stage-1 materialization pipeline.

Preparation rejects buffers outside the exact 8,192-value donor and 4,096-value
Q contracts, N outside 2..256, alignment/overlap failures, non-qualified maps,
non-positive or non-finite growth, or a recurrence that leaves the finite
positive domain.  Arbitrary non-null addresses remain subject to the ordinary
C requirement that the declared spans be accessible.

The private fused source/exp context is one cache line.  Its ranked leaf reads
only the canonical signed-z pointer, aligned growth-output pointer, binary32
drift and binary32 diffusion.  Preparation delegates market admissibility to
the existing qualified fixed-source preparation and vector-exp domain checks;
it adds no universal drift or diffusion bound.  It additionally requires the
canonical signed-z address and SHA-256, exact fixed-block metadata, disjoint
aligned spans, and every rounded `fmaf(diffusion,z,drift)` result to be finite
and inside the unchanged `[-87,88]` vector-exp input domain.

The fused leaf performs 256 fixed bottom-tested iterations.  Each iteration
loads two signed-z ZMMs, applies the existing affine FMA operand order, applies
the existing range-reduction, polynomial and scaling order, and stores two
growth ZMMs.  It has no x pointer and never stores or reloads x.  The linked
audit freezes the existing Stage-1 and six immediate-consumer instruction-byte
hashes and proves the fused leaf is call-free, stack-free, spill-free and below
32 simultaneously live ZMM values.

The benchmark's fixed private downstream policy is K=1 price through Stage 1,
K=1 price+Delta and K=2/3/4 through the immediate leaves, and K>=5 through
Stage 1.  Both front-end candidates use that identical policy.  A timing cell
is LOSE if either speedup is below 0.99, WIN if both exceed 1.01, and TIE
otherwise.  No performance or L1-residency claim follows from the memory
footprint; native family-6/model-143 Sapphire Rapids evidence is required.
The diagnostic prints only correctness, audit, timing, and selection lines to
stdout and creates no performance artifact.  Compact routes, memory-source
permutes, interleaving, geometric CV, growth-log5 and full-risk bases remain
deferred.
