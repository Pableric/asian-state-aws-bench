# Provenance

- Exact base commit: `abbfd87d27c56be6851aa845a284f9fd943e2ff9`
- Branch: `research/asian-multistrike-full-risk`
- Clean experiment worktree: `/tmp/asian-multistrike-full-risk.EtcWgv`
- Original checkout: `/home/pablo/Projects/asian-option-engine`
- Original checkout status entries: 244
- Original status SHA-256: `af73b7286c3e52b9be4dcd88b88520ca752ab9adb688a7587cf5a3e15109fab9`
- Joe--Kuo direction table SHA-256: `fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199`
- Pre-existing Git blobs recorded: 182

The complete original `git status --short` was captured before worktree edits
and is stored in `source_checkout_status_before.txt`. Re-reading the original
checkout at packaging time produced the identical SHA-256 above, proving it
was not changed by this work.

`preexisting_blobs.txt` is the complete `git ls-tree -r` output for the exact
base. `reused_artifact_sha256.txt` covers the direction table, producer/carrier,
route preparation, Phase-1 forward machinery and qualification results,
price/Delta strip, payoff/control helpers, vector exponential coefficients,
and matched oneMKL consumer dependencies.

A path-scoped `git diff --exit-code` against the exact base passed for every
reused/frozen artifact. Only uniquely named private multi-strike diagnostic files,
tests, benchmark/build code, and this results directory are added.

The original status was persisted after its initial observation using the
patch mechanism; its byte hash is identical to the final re-observation. No
clean, reset, checkout, build, or edit was performed in the original checkout.
