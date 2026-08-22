# Follow-up provenance and manifest

Qualified parent:

```text
commit  538840542de2380aa0423684aa89da5ff0d748d8
branch  research/asian-multistrike-full-risk
```

Additive follow-up branch and worktree:

```text
branch    research/asian-multistrike-full-risk-hybrid-dispatch
worktree  /tmp/asian-multistrike-full-risk.EtcWgv
```

The complete original dirty-checkout status was already frozen in the
qualified package's provenance record.  A read-only status observation during
this follow-up confirmed that it remains dirty; no command in this follow-up
built, edited, cleaned, reset, or checked out that source directory.

All follow-up paths are new:

```text
asian_genuine_multistrike_full_risk_hybrid_dispatch.c
benchmarks/bench_asian_genuine_multistrike_full_risk_hybrid_dispatch.c
private/asian_genuine_multistrike_full_risk_hybrid_dispatch_diag.h
tests/Makefile.asian_genuine_multistrike_full_risk_hybrid_dispatch
tests/audit_asian_genuine_multistrike_full_risk_hybrid_dispatch.py
tests/test_asian_genuine_multistrike_full_risk_hybrid_dispatch.c
results/asian_genuine_multistrike_full_risk_hybrid_dispatch/README.md
results/asian_genuine_multistrike_full_risk_hybrid_dispatch/aws_run.md
results/asian_genuine_multistrike_full_risk_hybrid_dispatch/correctness.md
results/asian_genuine_multistrike_full_risk_hybrid_dispatch/native_evidence.md
results/asian_genuine_multistrike_full_risk_hybrid_dispatch/provenance.md
results/asian_genuine_multistrike_full_risk_hybrid_dispatch/structural_audit.md
```

The benchmark embeds the checked-out follow-up `HEAD` at build time and records
it in JSON.  The final additive commit and clean-checkout binary hash are
reported at handoff after the commit exists; embedding a commit's own hash in
that same commit is intentionally avoided.
