#!/usr/bin/env python3
"""Enforce the vectorized coefficient setup instruction-count gate."""

from __future__ import annotations

import argparse
from pathlib import Path

from check_dynamic_mix import function_counts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mix", type=Path)
    args = parser.parse_args()

    scalar = function_counts(
        args.mix, "european_build_composed_normal_schedule_scalar")
    vector = function_counts(
        args.mix, "european_build_composed_normal_schedule")
    scalar_total = scalar["*total"]
    vector_total = vector["*total"]
    ratio = vector_total / scalar_total
    print(
        f"composed_setup_instructions scalar={scalar_total} "
        f"vector={vector_total} ratio={ratio:.3f}"
    )
    if vector_total * 2 > scalar_total:
        raise SystemExit("vector setup did not cut composed-setup instructions by 50%")
    print("reduced_setup_mix_gate=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
