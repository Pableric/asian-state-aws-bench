# Frozen-leaf structural audit

Qualified base commit:

```text
538840542de2380aa0423684aa89da5ff0d748d8
```

The frozen assembly source remains byte-identical:

```text
asian_genuine_multistrike_full_risk_avx512.s
blob    80c3c9ffbffa94f781d79b92f16d4be53b0acad1
sha256  ea2303cb89ef4c3a3f7544ebb37a8c5e98b4c2b5462f417f998f03a1a980aa98
```

Reassembling that source independently in the qualified checkout and the
follow-up checkout produced identical objects:

```text
sha256  5e8d8cbb7d337e51b579451685e957f1676121633d07ca250348f67b44e9cd8c
```

Frozen ranked symbol sizes remain:

```text
asian_genuine_msfr_arithmetic_tile2_diag  0x0700
asian_genuine_msfr_arithmetic_tile4_diag  0x0d18
asian_genuine_msfr_cv_tile2_diag          0x09f2
asian_genuine_msfr_cv_tile4_diag          0x1262
```

`git diff` from the qualified commit is empty for the ranked assembly, its
private ABI header, and the qualified setup/finalizer.  All policy branches and
leaf calls are in the new C dispatcher.  The ranked symbols neither call nor
reference that dispatcher, so no dispatcher instruction, branch, stack use, or
padding logic enters a hot AVX-512 leaf.

Other frozen hashes checked during this follow-up:

```text
asian_genuine_multistrike_full_risk_setup.c
  sha256 6b24e27d3080b05d8837d985a0fb4845b0b6b8093154eee082a456c843cb644c
private/asian_genuine_multistrike_full_risk_diag.h
  sha256 86f2c48fed44e183a30a7c81a3430d0b175d8ca94830e6f68c13b69c23115bd6
asian_genuine_aad_phase1_avx512.s
  sha256 dd1229e854022c1072b81e4d7e1ee6e2344bcbdadb1351e6a2f4a1576fe7c5c4
direction_numbers/joe_kuo_6_21201.bin
  sha256 fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199
```
