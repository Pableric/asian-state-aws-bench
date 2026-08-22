# Imported native evidence and selection scope

The selection input for this follow-up is the Sapphire Rapids evidence supplied
by the user after the qualified Phase-1 strip commit:

- At `K=32`, tile 4 won 14 of 20 measured cells.
- Tile 4 won every measured `N=256` cell.
- Paired TSC-unit and wall-time ratios agreed closely.
- Some cells favored tile 2.

Consequently tile 4 is recorded only as the Sapphire Rapids large-strip
default.  Tile 2 remains the small/remainder consumer.  Selection does not
change with cache mode, and this follow-up does not claim that tile 4 wins
universally.

This repository checkout contained no successful AWS JSON at qualified commit
`538840542de2380aa0423684aa89da5ff0d748d8`; the imported facts above are the
user-provided summary, not reconstructed samples.  No prior result file was
changed or replaced.  The new hybrid must be run through its separate native
benchmark before any performance claim is made for arbitrary `K`.
