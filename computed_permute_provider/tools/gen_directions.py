#!/usr/bin/env python3
"""Produce data/joe_kuo_d1280.bin from two independent Joe-Kuo sources.

Source 1: the engine's committed binary table (base commit), rows of 33 LE
uint32 values: 32, V[0], ..., V[31].
Source 2: the upstream new-joe-kuo-6.21201 text (d, s, a, m_i), from which
V is re-derived with the standard Joe-Kuo recurrence.

Both must agree for every dimension written. The output keeps the row format
of source 1.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

UPSTREAM_BIN_SHA256 = "fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199"
UPSTREAM_BIN_DIMS = 21201
ROW_WORDS = 33
BITS = 32


def derive_from_text(text: Path, dims: int) -> list[list[int]]:
    rows = [[1 << (BITS - 1 - i) for i in range(BITS)]]
    lines = text.read_text().splitlines()
    if not lines[0].split()[0] == "d":
        raise SystemExit("text header missing")
    for line in lines[1:]:
        if len(rows) == dims:
            break
        fields = line.split()
        d, s, a = int(fields[0]), int(fields[1]), int(fields[2])
        m = [int(x) for x in fields[3:]]
        if d != len(rows) + 1 or len(m) != s:
            raise SystemExit(f"malformed text row for d={d}")
        v = [0] * BITS
        for i in range(min(s, BITS)):
            v[i] = m[i] << (BITS - 1 - i)
        for i in range(s, BITS):
            x = v[i - s] ^ (v[i - s] >> s)
            for k in range(1, s):
                if (a >> (s - 1 - k)) & 1:
                    x ^= v[i - k]
            v[i] = x & 0xFFFFFFFF
        rows.append(v)
    if len(rows) != dims:
        raise SystemExit(f"text provides {len(rows)} dims, need {dims}")
    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True, type=Path)
    ap.add_argument("--text", required=True, type=Path)
    ap.add_argument("--dims", type=int, default=1280)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    raw = args.bin.read_bytes()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != UPSTREAM_BIN_SHA256:
        raise SystemExit(f"upstream bin sha256 {digest} != {UPSTREAM_BIN_SHA256}")
    if len(raw) != UPSTREAM_BIN_DIMS * ROW_WORDS * 4:
        raise SystemExit("upstream bin size mismatch")

    derived = derive_from_text(args.text, args.dims)
    out = bytearray()
    for d in range(args.dims):
        row = struct.unpack_from(f"<{ROW_WORDS}I", raw, d * ROW_WORDS * 4)
        if row[0] != BITS:
            raise SystemExit(f"row {d + 1} header {row[0]}")
        if list(row[1:]) != derived[d]:
            raise SystemExit(f"D{d + 1}: binary table and text derivation disagree")
        out += struct.pack(f"<{ROW_WORDS}I", *row)
    args.out.write_bytes(bytes(out))
    print(f"wrote {args.out} dims={args.dims} bytes={len(out)} "
          f"sha256={hashlib.sha256(out).hexdigest()} upstream_bin_sha256={digest} "
          f"text_sha256={hashlib.sha256(args.text.read_bytes()).hexdigest()} "
          f"agreement=all_{args.dims}_dims")
    return 0


if __name__ == "__main__":
    sys.exit(main())
