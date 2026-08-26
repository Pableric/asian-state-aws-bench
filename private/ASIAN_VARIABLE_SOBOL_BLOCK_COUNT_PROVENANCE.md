# Variable Sobol-Block Provenance

This private diagnostic extends the qualified materialized D1 donor bank; it
does not use the live approximate X3 producer.

- Parent: `ea6ac0a57ff042addec03a34f467a03df8f1fb67`
- Joe--Kuo asset SHA-256:
  `fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199`
- Frozen donor-block 2--3 prefix SHA-256:
  `ecf3bb854e98bedcf724d0743438457ccf8b600e1264cb537741ce0b9d90d98d`
- Complete donor-block 2--31 bank SHA-256:
  `f8098bba7c3cbb4fc406389ba29bac37c2991306f3598b373d1bd7b5f89c99cc`
- Generated block-metadata source SHA-256:
  `1f14c2a998c0b7ee4946100c0e85275e0ee91db7e52c331fb5ffda1da1b26f71`

The primary D1 construction uses the transformed binary-index columns
`W[0..16]`, with `W[0]=V[0]` and `W[k]=V[k] XOR V[k-1]`.  Sequential values
use cumulative jumps through `ctz(next_index)`.  Every generated word is
independently compared with the ordinary Gray-code Joe--Kuo D1 formula before
its MPFR inverse-normal value is rounded to binary32.

The qualified materialized bank order is the final consecutive packet/lane
order, so its external physical permutation is `pi(j)=j`.  Tests prove this
against every donor entry and preserve the original first-8,192-float hash.

For all target blocks 0--15 and dimensions D1--D256, the original twelve
affine columns are unchanged.  The compiled 4-byte record per dimension and
target block stores only the new 12-bit base and absolute D1 donor-block
identity.
