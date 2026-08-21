# Seasoned strip structural audit

Status: `PASS`.

Seasoned and matched-f unseasoned execution use the same qualified symbols and
the same future-fixing trip count.  The dynamic routed-loop delta is therefore
exactly zero for instructions, loads, stores, four `vpermd` operations,
arithmetic, branches, dependencies, and text.  There is no D1 increment: D1
already uses the same `24(%rdi){1to16}` prepared-weight FMA as every later
route.

The route body is 32 static instructions.  Complete dynamic count is
`6 + 128*(23 + 32*f)`.  Each ordinary route has four permutes, two multiplies,
two adds, two FMAs, six ZMM loads, four map-byte loads, three route-pointer
loads, two weight broadcast operands, one loop branch, and zero stores.  It
has no call, gather, stack reference, spill, or intermediate state store.

The register allocation uses 16 architectural ZMM names with peak liveness 12,
nine GPR names, and five mask names.  Persistent chains are `S` in ZMM4/5,
`Q` in ZMM6/7, and `L` in ZMM10/11.  On Skylake-X the unchanged pressure is
four port-5 permutes, the vector/scalar load stream on ports 2/3, and two each
of multiply, add, and FMA on the vector arithmetic ports.  Native paired timing
is the performance acceptance authority.

No ranked payoff or Delta leaf changed.  Their detailed instruction,
broadcast, reduction, load/store and incremental-strike accounting remains in
the frozen qualified object audit whose hash is recorded in JSON.
