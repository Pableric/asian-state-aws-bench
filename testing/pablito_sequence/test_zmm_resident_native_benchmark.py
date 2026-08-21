from __future__ import annotations

import json
import unittest

from audit_zmm_sobol_blocks import DEFAULT_TABLE, gray_words, load_direction_table, template_audit
from generate_zmm_resident_native_benchmark import RESIDENT_BUDGETS, generate, optimize_residents, recipes


class ZmmResidentNativeBenchmarkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.table = load_direction_table(DEFAULT_TABLE)
        raw = template_audit(cls.table, 16)
        cls.masks = [int(value, 16) for value in raw["gf2"]["representation_masks_hex_by_dimension"]]
        cls.templates = [gray_words(row, 0, 16) for row in cls.table]

    def test_every_hybrid_transition_is_exact_or_direct(self) -> None:
        for budget in RESIDENT_BUDGETS:
            resident = optimize_residents(self.masks, budget)
            pair = recipes(resident)
            current = self.templates[0].copy()
            for dimension in range(1, 256):
                delta = self.masks[dimension - 1] ^ self.masks[dimension]
                if delta == 0:
                    pass
                elif delta in pair:
                    left, right = pair[delta]
                    left_template = self.templates[self.masks.index(left)]
                    right_template = self.templates[self.masks.index(right)]
                    current ^= left_template ^ right_template
                else:
                    current = self.templates[dimension].copy()
                self.assertEqual(current.tolist(), self.templates[dimension].tolist())

    def test_14_register_schedule_covers_all_changed_transitions(self) -> None:
        resident = optimize_residents(self.masks, 14)
        pair = recipes(resident)
        changed = [left ^ right for left, right in zip(self.masks, self.masks[1:]) if left != right]
        self.assertEqual(len(changed), 250)
        self.assertTrue(all(delta in pair for delta in changed))

    def test_generated_outputs_are_self_consistent(self) -> None:
        assembly, header, schedule = generate()
        self.assertIn("vpternlogd $0x96", assembly)
        self.assertIn("direct32_two_loads", header)
        self.assertEqual(schedule["resident_budgets"], list(RESIDENT_BUDGETS))
        self.assertEqual(len(schedule["kernels"]), 130)
        json.dumps(schedule)


if __name__ == "__main__":
    unittest.main()
