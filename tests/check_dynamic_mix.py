#!/usr/bin/env python3
"""Enforce the dynamic kernel's no-extra-instruction/load/FMA gates."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def function_counts(path: Path, function: str) -> dict[str, int]:
    text = path.read_text(errors="replace")
    marker = re.search(
        rf"^# \$dynamic-counts-for-function: {re.escape(function)}\b.*?$",
        text,
        flags=re.M,
    )
    if marker is None:
        raise RuntimeError(f"{path}: missing function counts for {function}")
    section = text[marker.end() :]
    total = re.search(r"^\*total\s+(\d+)\s*$", section, flags=re.M)
    if total is None:
        raise RuntimeError(f"{path}: missing function total")
    section = section[: total.end()]
    counts = {
        name: int(value)
        for name, value in re.findall(
            r"^([*A-Za-z][A-Za-z0-9*_-]*)\s+(\d+)\s*$", section, re.M
        )
    }
    return counts


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline", type=Path)
    ap.add_argument("dynamic", type=Path)
    args = ap.parse_args()
    baseline = function_counts(args.baseline, "price_european_sequence")
    dynamic = function_counts(args.dynamic, "price_european_sequence_dynamic_ranges")

    baseline_fma = baseline.get("VFMADD132PS", 0) + baseline.get("VFMADD213PS", 0)
    dynamic_fma = dynamic.get("VFMADD132PS", 0) + dynamic.get("VFMADD213PS", 0)
    gates = {
        "no_extra_instructions": dynamic["*total"] <= baseline["*total"],
        "same_vector_fmas": dynamic_fma == baseline_fma,
        "fewer_64byte_reads": dynamic["*mem-read-64"] < baseline["*mem-read-64"],
        "same_sign_xors": dynamic.get("VXORPS", 0) == baseline.get("VXORPS", 0),
    }
    print(
        f"instructions baseline={baseline['*total']} dynamic={dynamic['*total']} "
        f"difference={dynamic['*total'] - baseline['*total']}"
    )
    print(f"vector_fmas baseline={baseline_fma} dynamic={dynamic_fma}")
    print(
        f"mem_read_64 baseline={baseline['*mem-read-64']} "
        f"dynamic={dynamic['*mem-read-64']} "
        f"difference={dynamic['*mem-read-64'] - baseline['*mem-read-64']}"
    )
    print(
        f"sign_xors baseline={baseline.get('VXORPS', 0)} "
        f"dynamic={dynamic.get('VXORPS', 0)}"
    )
    failed = [name for name, passed in gates.items() if not passed]
    if failed:
        raise SystemExit("failed gates: " + ", ".join(failed))
    print("all_dynamic_mix_gates=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
