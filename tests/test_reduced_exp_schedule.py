#!/usr/bin/env python3
"""Regression tests for deterministic reduced-exp slot bounds."""

from __future__ import annotations

import importlib.util
import subprocess
import unittest
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "generate_reduced_exp_schedule.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("reduced_exp", GENERATOR)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ReducedExpScheduleTests(unittest.TestCase):
    def test_header_is_reproducible(self) -> None:
        subprocess.run(["python3", str(GENERATOR), "--check"], cwd=ROOT, check=True)

    def test_complete_interval_error_gate(self) -> None:
        module = load_generator()
        _header, report = module.build_header()
        self.assertLessEqual(report["normal_worst"], module.TARGET_REL_ERR)
        self.assertLessEqual(report["tail_worst"], module.TARGET_REL_ERR)
        self.assertEqual(module.MAX_ALPHA, 0.20)

    def test_exact_tail_slot_count(self) -> None:
        module = load_generator()
        _lo, _hi, tail = module.slot_bounds()
        self.assertEqual(int(tail.sum()), 14)

    def test_pair_moments_preserve_both_slots(self) -> None:
        module = load_generator()
        moments = module.slot_normal_moments()
        pair_moments = 0.5 * (moments[0::2] + moments[1::2])
        self.assertEqual(pair_moments.shape, (128, 10))
        self.assertTrue(
            np.array_equal(
                pair_moments,
                0.5 * (moments[0::2] + moments[1::2]),
            )
        )

    def test_tail_schedule_is_padded_without_adding_work(self) -> None:
        module = load_generator()
        lo, hi, tail = module.slot_bounds()
        indices = np.flatnonzero(tail)
        padded_lo = np.zeros(16, dtype=np.float32)
        padded_hi = np.zeros(16, dtype=np.float32)
        padded_lo[:len(indices)] = lo[indices]
        padded_hi[:len(indices)] = hi[indices]
        self.assertEqual(len(indices), 14)
        self.assertTrue((padded_lo[14:] == 0.0).all())
        self.assertTrue((padded_hi[14:] == 0.0).all())


if __name__ == "__main__":
    unittest.main()
