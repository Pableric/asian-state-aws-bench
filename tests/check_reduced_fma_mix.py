#!/usr/bin/env python3
"""Enforce reduced-FMA kernel instruction-count gates from Intel SDE mix files."""

from __future__ import annotations

import argparse
from pathlib import Path

from check_dynamic_mix import function_counts


def vector_fmas(counts: dict[str, int]) -> int:
    return sum(value for name, value in counts.items() if name.startswith("VFMADD") and name.endswith("PS"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline", type=Path)
    ap.add_argument("reduced", type=Path)
    args = ap.parse_args()
    baseline = function_counts(args.baseline, "price_european_sequence")
    reduced = function_counts(args.reduced, "price_european_sequence_reduced_fma")
    baseline_fma = vector_fmas(baseline)
    reduced_fma = vector_fmas(reduced)
    gates = {
        "fewer_instructions": reduced["*total"] < baseline["*total"],
        "instruction_ceiling": reduced["*total"] <= 5200,
        "expected_fma_ceiling": reduced_fma <= 708,
        "at_least_50_percent_fewer_fmas": reduced_fma * 2 <= baseline_fma,
        "no_vector_multiplies": reduced.get("VMULPS", 0) == 0,
        "memory_read64_ceiling": reduced.get("*mem-read-64", 0) <= 684,
        "same_sign_xors": reduced.get("VXORPS", 0) == baseline.get("VXORPS", 0),
    }
    print(f"instructions baseline={baseline['*total']} reduced={reduced['*total']}")
    print(f"vector_fmas baseline={baseline_fma} reduced={reduced_fma}")
    print(f"vector_multiplies baseline={baseline.get('VMULPS', 0)} reduced={reduced.get('VMULPS', 0)}")
    print(f"memory_read64 baseline={baseline.get('*mem-read-64', 0)} reduced={reduced.get('*mem-read-64', 0)}")
    failed = [name for name, passed in gates.items() if not passed]
    if failed:
        raise SystemExit("failed gates: " + ", ".join(failed))
    print("all_reduced_fma_mix_gates=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
