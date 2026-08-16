#!/usr/bin/env python3
"""Verify the checked-in Joe--Kuo 6.21201 direction-number table."""

from __future__ import annotations

import hashlib
import struct
from pathlib import Path


TABLE = Path(__file__).with_name("joe_kuo_6_21201.bin")
DIMENSIONS = 21_201
BITS = 32
WORDS_PER_DIMENSION = BITS + 1
EXPECTED_BYTES = DIMENSIONS * WORDS_PER_DIMENSION * 4
EXPECTED_SHA256 = "fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199"


def main() -> int:
    data = TABLE.read_bytes()
    if len(data) != EXPECTED_BYTES:
        raise SystemExit(
            f"wrong size: {len(data)} bytes; expected {EXPECTED_BYTES}"
        )

    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"wrong SHA-256: {digest}")

    for dimension in range(DIMENSIONS):
        offset = dimension * WORDS_PER_DIMENSION * 4
        count = struct.unpack_from("<I", data, offset)[0]
        if count != BITS:
            raise SystemExit(
                f"dimension {dimension + 1}: column count {count}, expected {BITS}"
            )

    first = struct.unpack_from("<32I", data, 4)
    canonical_d1 = tuple(1 << (31 - bit) for bit in range(BITS))
    if first != canonical_d1:
        raise SystemExit("dimension 1 is not canonical Sobol D1")

    print(
        "joe_kuo_6_21201=PASS "
        f"dimensions={DIMENSIONS} bits={BITS} sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

