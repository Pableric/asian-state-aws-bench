# Frozen 2,048-path virtual-block provenance

This directory is a read-only copy of the junior virtual-block package used to
qualify the exact 4,096-to-two-2,048 split. It is diagnostic provenance only;
production pricing must not load the identity transpose arrays.

The authoritative manifest is:

`virtual_block/results/transpose_2048_hashes.json`

The package records `GLOBAL_2048_TRANSPOSE=YES`, with both permutations equal
to the identity. The first qualified 2,048-path block is chronological Sobol
indices 8192 through 10239.

Source verification can be run from `virtual_block/` with:

```sh
make test
```

The Joe–Kuo header and its original source data are included under
`d1_gf2_workbench/` so the manifest can be checked without external files.
