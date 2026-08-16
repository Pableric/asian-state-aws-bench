#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

from check_dynamic_mix import function_counts


def fmas(counts: dict[str, int]) -> int:
    return sum(v for k, v in counts.items() if k.startswith("VFMADD") and k.endswith("PS"))


def symbol_size(path: Path, symbol: str) -> int:
    output = subprocess.check_output(["nm", "-S", "--size-sort", str(path)], text=True)
    for line in output.splitlines():
        fields = line.split()
        if fields and fields[-1] == symbol:
            return int(fields[1], 16)
    raise RuntimeError(f"missing symbol {symbol}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("ordered", type=Path)
    parser.add_argument("object", type=Path)
    parser.add_argument("boundary", type=Path)
    args = parser.parse_args()
    baseline = function_counts(args.baseline, "price_european_sequence_reduced_fma")
    ordered = function_counts(args.ordered, "price_european_sequence_ordered_d1")
    boundary = function_counts(args.boundary, "price_european_sequence_ordered_d1")
    size = symbol_size(args.object, "price_european_sequence_ordered_d1")
    permutes = sum(
        value for name, value in ordered.items()
        if name.startswith("VPERM")
    )
    gathers = sum(value for name, value in ordered.items() if name.startswith("VGATHER"))
    boundary_gathers = sum(
        value for name, value in boundary.items() if name.startswith("VGATHER")
    )
    gates = {
        "smaller_dynamic_instruction_count": ordered["*total"] < baseline["*total"],
        "main_plus_sparse_correction_fmas": fmas(ordered) == 546,
        "no_tzcnt_at_8192": ordered.get("TZCNT", 0) == 0,
        "one_tzcnt_per_followed_block": boundary.get("TZCNT", 0) == 15,
        "boundary_fma_count": fmas(boundary) == 8736,
        "one_gather_per_block": gathers == 1 and boundary_gathers == 16,
        "no_permutes": permutes == 0,
        "text_at_most_5k": size <= 5120,
    }
    print(f"instructions baseline={baseline['*total']} ordered={ordered['*total']}")
    print(f"fmas ordered={fmas(ordered)} tzcnt={ordered.get('TZCNT', 0)}")
    print(f"boundary_fmas={fmas(boundary)} boundary_tzcnt={boundary.get('TZCNT', 0)}")
    print(
        f"permutes={permutes} gathers={gathers} "
        f"boundary_gathers={boundary_gathers} text_bytes={size}"
    )
    failed = [name for name, passed in gates.items() if not passed]
    if failed:
        raise SystemExit("failed gates: " + ", ".join(failed))
    print("all_ordered_d1_mix_gates=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
