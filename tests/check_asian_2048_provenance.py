#!/usr/bin/env python3
"""Verify the frozen junior package without importing its identity arrays."""

import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGE = ROOT / "private/provenance/asian_2048_virtual_block"
RESULTS = PACKAGE / "virtual_block/results"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


manifest = json.loads((RESULTS / "transpose_2048_hashes.json").read_text())
if manifest.get("GLOBAL_2048_TRANSPOSE") != "YES":
    raise SystemExit("GLOBAL_2048_TRANSPOSE missing")
for relative, expected in manifest["input_hashes"].items():
    if digest(PACKAGE / relative) != expected:
        raise SystemExit(f"input provenance hash mismatch: {relative}")
for relative, expected in manifest["output_hashes"].items():
    if digest(RESULTS / relative) != expected:
        raise SystemExit(f"output provenance hash mismatch: {relative}")
verification = json.loads(
    (RESULTS / "transpose_2048_verification.json").read_text())
required = {
    "GLOBAL_2048_TRANSPOSE": "YES",
    "identical_transpose_all_dimensions": True,
    "tile_unique_paths": 2048,
    "bijection": True,
    "D1_D256_inverse_restores_original_words": True,
    "no_duplicate_or_omitted_path": True,
}
for key, expected in required.items():
    if verification.get(key) != expected:
        raise SystemExit(f"provenance field mismatch: {key}")
for name in ("transpose_4096_to_2x2048.bin",
             "inverse_transpose_4096_to_2x2048.bin"):
    data = (RESULTS / name).read_bytes()
    values = struct.unpack("<4096I", data)
    if values != tuple(range(4096)):
        raise SystemExit(f"non-identity permutation: {name}")
closure = json.loads((RESULTS / "global_2048_route_closure.json").read_text())
if closure.get("GLOBAL_2048_ROUTE_CLOSURE") != "YES":
    raise SystemExit("global route closure missing")
bank = (ROOT / "private/asian_genuine_fixed_block_signed_z.bin").read_bytes()
if len(bank) != 32768 or digest(
        ROOT / "private/asian_genuine_fixed_block_signed_z.bin") != \
        "ecf3bb854e98bedcf724d0743438457ccf8b600e1264cb537741ce0b9d90d98d":
    raise SystemExit("canonical 32-KiB signed-z bank mismatch")
print("asian_2048_provenance PASS GLOBAL_2048_TRANSPOSE=YES "
      "transpose=identity inverse_transpose=identity")
