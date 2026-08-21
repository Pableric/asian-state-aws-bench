# Asian strip Delta replication qualification

This additive result path qualifies, or retains diagnostic status for, the
unchanged price/Delta strip from package commit
`1b199076ae9fa6db2258172587b59fef400e11ff`.

The statistical contract is frozen in
`private/ASIAN_GENUINE_DELTA_QUALIFICATION_CONTRACT.md`.  The runner uses exact
Joe--Kuo reconstruction, 32 fixed digital shifts, nested path prefixes, an
independent long-double pathwise oracle, and three CRN bump-and-revalue levels.
The portable generator writes a temporary Q/G corpus and the unchanged AVX-512
leaves verify it in a second process.  The native AWS target invokes no Intel
SDE.

```sh
make -f tests/Makefile.asian_genuine_delta_qualification -j2 qualification-native
python3 tests/audit_asian_genuine_delta_qualification.py
```

The first command produces `replication_raw.json`, `qualification.json`, and
`qualification.md`.  The second produces `audit.json` and checks that the
decision exactly reflects the pre-registered gates.
