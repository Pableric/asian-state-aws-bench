# Provenance

- Exact base commit: `9c6146bb4e38d0aa5ebcf2ca0b026734e772804a`
- Base subject: `build: make hybrid benchmark reproducible`
- Branch: `research/asian-fixed-block-one-fma-source`
- Clean experiment worktree: `/tmp/asian-fixed-block-source`
- Original checkout: `/home/pablo/Projects/asian-option-engine`
- Original checkout commit: `1b199076ae9fa6db2258172587b59fef400e11ff`
- Original checkout branch: `package/asian-price-delta-strip-aws`
- Original checkout status entries: 244
- Original status SHA-256: `af73b7286c3e52b9be4dcd88b88520ca752ab9adb688a7587cf5a3e15109fab9`
- Joe--Kuo direction table SHA-256: `fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199`
- Frozen signed-z table SHA-256: `ecf3bb854e98bedcf724d0743438457ccf8b600e1264cb537741ce0b9d90d98d`
- Pre-existing Git blobs recorded: 221
- Base tree-manifest SHA-256: `c0a4b53a65db713dc6a2c81c3c570981eb965da7e57cb5d839f6bb3635a092d5`

`source_checkout_status_before.txt` is the complete original
`git status --short`. `preexisting_blobs.txt` is the complete
`git ls-tree -r` output at the exact base. `reused_artifact_sha256.txt` covers
the qualified producer/carrier, route preparation, Phase-1 full-risk basis,
multi-strike and price/Delta consumers, reference code, existing qualification
evidence, and the direction table.

The worktree was created directly from the latest qualified Asian commit that
contains the corrected X3 -> vector-exp -> shared-control pipeline and the
additive hybrid strip follow-up. No existing tracked file is changed. The
original dirty checkout was not cleaned, reset, checked out, built, or edited.
