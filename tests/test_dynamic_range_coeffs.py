#!/usr/bin/env python3
"""Regression checks for the deterministic D1 dynamic-range schedule."""

from __future__ import annotations

import importlib.util
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "generate_dynamic_range_coeffs.py"
HEADER = ROOT / "private" / "gaussian_dynamic_range_coeff_values.h"


def load_generator():
    spec = importlib.util.spec_from_file_location("dynamic_coeffs", GENERATOR)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class DynamicRangeCoefficientTests(unittest.TestCase):
    def test_committed_header_is_reproducible(self) -> None:
        subprocess.run(
            ["python3", str(GENERATOR), "--check"], cwd=ROOT, check=True
        )

    def test_schedule_keeps_error_and_shared_phase_gates(self) -> None:
        module = load_generator()
        _header, report = module.build_header()
        self.assertEqual(report["world_counts"], module.EXPECTED_WORLD_COUNTS)
        self.assertEqual(
            tuple(report["forced_shared_pairs"]), module.EXPECTED_FORCED_SHARED_PAIRS
        )
        self.assertEqual(set(report["special_tail_pairs"]), set(module.SPECIAL_TAIL_PAIRS))
        self.assertLessEqual(
            report["worst_selected_error"], module.FORCED_SHARED_TARGET
        )
        self.assertEqual(len(report["forced_shared_pairs"]), 2)

    def test_header_has_one_shared_pair_per_phase(self) -> None:
        text = HEADER.read_text()
        self.assertIn("gauss_dynamic_c0[128][16]", text)
        self.assertIn("gauss_dynamic_c1[128][16]", text)
        self.assertIn("GAUSS_DYNAMIC_FORCED_SHARED_PAIRS 2u", text)


if __name__ == "__main__":
    unittest.main()
