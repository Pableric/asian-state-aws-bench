# Direction numbers

## Joe--Kuo 6.21201

`joe_kuo_6_21201.bin` is the complete established Joe--Kuo baseline copied
unchanged from `NewDirNumbers/helpers/sobol_joe_kuo.bin`.

It contains all 21,201 dimensions with 32 columns. Each dimension is one
little-endian row of 33 `uint32_t` values:

```text
32, V[0], V[1], ..., V[31]
```

The dimension is its one-based row number. The scaled direction words `V` can
be consumed directly by a 32-bit Sobol generator.

```text
bytes:  2,798,532
SHA256: fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199
```

Verify the complete file, every row header, and canonical D1 with:

```sh
python3 direction_numbers/verify_joe_kuo.py
```

## OpenEvolve

`openevolve.json` is copied from
`NewDirNumbers/testing/numbers/openevolve.json`. It remains available for the
32-dimension Asian research comparison, but Joe--Kuo is the standard baseline.

