#!/usr/bin/env python3
"""Generate immutable variable-block affine bases from frozen Joe--Kuo rows."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import struct

DIMENSIONS = 256
PATHS = 4096
TARGET_BLOCKS = 16
FIRST_TARGET = 8192
FIRST_DONOR_BLOCK = 2
LAST_DONOR_BLOCK = 31


def load_rows(path: pathlib.Path) -> list[tuple[int, ...]]:
    raw = path.read_bytes()
    record = 33 * 4
    if len(raw) < DIMENSIONS * record:
        raise SystemExit("Joe--Kuo asset is truncated")
    rows = []
    for dimension in range(DIMENSIONS):
        values = struct.unpack_from("<33I", raw, dimension * record)
        if values[0] != 32:
            raise SystemExit(f"bad Joe--Kuo row D{dimension + 1}")
        rows.append(values[1:])
    expected_d1 = tuple(1 << (31 - bit) for bit in range(32))
    if rows[0] != expected_d1:
        raise SystemExit("D1 is not the qualified ordinary first dimension")
    return rows


def word(index: int, directions: tuple[int, ...]) -> int:
    gray = index ^ (index >> 1)
    out = 0
    bit = 0
    while gray:
        if gray & 1:
            out ^= directions[bit]
        gray >>= 1
        bit += 1
    return out


def parse_descriptors(path: pathlib.Path) -> list[tuple[int, int, tuple[int, ...]]]:
    pattern = re.compile(
        r"\{(\d+),\s*(\d+),\s*ASIAN_META_DESCRIPTOR_ABI_VERSION,\s*"
        r"\{([^}]+)\},\s*ASIAN_META_DESCRIPTOR_MAGIC\}"
    )
    out = []
    for match in pattern.finditer(path.read_text(encoding="ascii")):
        columns = tuple(int(item) for item in match.group(3).split(","))
        if len(columns) != 12:
            raise SystemExit("descriptor has the wrong column count")
        out.append((int(match.group(1)), int(match.group(2)), columns))
    if len(out) != DIMENSIONS:
        raise SystemExit(f"parsed {len(out)} descriptors, expected {DIMENSIONS}")
    return out


def build(rows: list[tuple[int, ...]], descriptors):
    donor_lookup: dict[int, tuple[int, int]] = {}
    for donor_block in range(FIRST_DONOR_BLOCK, LAST_DONOR_BLOCK + 1):
        for local in range(PATHS):
            key = word(donor_block * PATHS + local, rows[0])
            if key in donor_lookup:
                raise SystemExit("D1 donor bank repeats a Sobol word")
            donor_lookup[key] = (donor_block, local)

    records: list[list[tuple[int, int]]] = []
    reuse_sets: list[set[int]] = []
    for target_block in range(TARGET_BLOCKS):
        start = FIRST_TARGET + target_block * PATHS
        block_records = []
        donors = set()
        for dimension, directions in enumerate(rows):
            mapping = []
            donor = None
            for path in range(PATHS):
                match = donor_lookup.get(word(start + path, directions))
                if match is None:
                    raise SystemExit(
                        f"D{dimension + 1} block {target_block} has no D1 donor"
                    )
                donor_block, local = match
                if donor is None:
                    donor = donor_block
                elif donor != donor_block:
                    raise SystemExit(
                        f"D{dimension + 1} block {target_block} spans donor blocks"
                    )
                mapping.append(local)
            if len(set(mapping)) != PATHS:
                raise SystemExit("route is not a permutation")
            base = mapping[0]
            columns = tuple(mapping[1 << bit] ^ base for bit in range(12))
            if columns != descriptors[dimension][2]:
                raise SystemExit(
                    f"affine columns changed at block {target_block} D{dimension + 1}"
                )
            for path, local in enumerate(mapping):
                reconstructed = base
                for bit in range(12):
                    if path & (1 << bit):
                        reconstructed ^= columns[bit]
                if reconstructed != local:
                    raise SystemExit("affine reconstruction failed")
            if target_block == 0:
                old_base, old_region, _ = descriptors[dimension]
                if base != old_base or donor != FIRST_DONOR_BLOCK + old_region:
                    raise SystemExit("block zero differs from frozen descriptors")
            block_records.append((base, donor))
            donors.add(donor)
        records.append(block_records)
        reuse_sets.append(donors)
    return records, reuse_sets


def emit(output: pathlib.Path, source: pathlib.Path, records, reuse_sets):
    source_hash = hashlib.sha256(source.read_bytes()).digest()
    directions = tuple(1 << (31 - bit) for bit in range(32))
    special_w = [directions[0]]
    special_w.extend(directions[bit] ^ directions[bit - 1] for bit in range(1, 17))
    lines = [
        '#include "asian_variable_sobol_block_count_diag.h"',
        "",
        "const asian_variable_w_provenance_t",
        '__attribute__((aligned(64), visibility("hidden")))',
        "asian_variable_w_provenance = {",
        "    ASIAN_VARIABLE_W_MAGIC, 1, 17, 2, 31, 0,",
        "    {" + ",".join(f"UINT32_C(0x{x:08x})" for x in special_w) + "},",
        "    {" + ",".join(f"0x{x:02x}" for x in source_hash) + "},",
        "    {0}",
        "};",
        "",
        "const asian_variable_block_meta_t",
        '__attribute__((aligned(64), visibility("hidden")))',
        "asian_variable_block_metadata[ASIAN_VARIABLE_MAX_BLOCKS]"
        "[ASIAN_META_DIRECTIONS] = {",
    ]
    for target_block, block_records in enumerate(records):
        lines.append(f"    {{ /* target block {target_block} */")
        for offset in range(0, DIMENSIONS, 8):
            entries = ",".join(
                f"{{{base},{donor},0}}"
                for base, donor in block_records[offset : offset + 8]
            )
            lines.append("        " + entries + ",")
        lines.append("    },")
    lines.extend(["};", "", "const uint32_t asian_variable_donor_masks[16] = {"])
    for donors in reuse_sets:
        mask = 0
        for donor in donors:
            mask |= 1 << (donor - FIRST_DONOR_BLOCK)
        lines.append(f"    UINT32_C(0x{mask:08x}),")
    lines.extend(["};", ""])
    output.write_text("\n".join(lines), encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--joe-kuo", type=pathlib.Path, required=True)
    parser.add_argument("--descriptors", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    rows = load_rows(args.joe_kuo)
    descriptors = parse_descriptors(args.descriptors)
    records, reuse = build(rows, descriptors)
    emit(args.output, args.joe_kuo, records, reuse)


if __name__ == "__main__":
    main()
