from __future__ import annotations

import unittest

import numpy as np

import generate_ordered_d1_coeffs as ordered


W = [1 << (31 - column) for column in range(32)]
J = [W[4] ^ W[column + 5] for column in range(27)] + [0] * 5


def sobol_word(index: int) -> int:
    gray = index ^ (index >> 1)
    value = 0
    column = 0
    while gray:
        if gray & 1:
            value ^= W[column]
        gray >>= 1
        column += 1
    return value


class OrderedD1LayoutTests(unittest.TestCase):
    def test_jump_words_advance_all_32_lanes(self) -> None:
        for packet in (256, 257, 511, 512, 767, 1023, 65535):
            k = ((packet + 1) & -(packet + 1)).bit_length() - 1
            before = np.asarray(
                [sobol_word(32 * packet + lane) for lane in range(32)],
                dtype=np.uint32,
            )
            after = np.asarray(
                [sobol_word(32 * (packet + 1) + lane) for lane in range(32)],
                dtype=np.uint32,
            )
            np.testing.assert_array_equal(before ^ np.uint32(J[k]), after)

    def test_static_31_group_schedule(self) -> None:
        schedule = [3 + ((group & -group).bit_length() - 1) for group in range(1, 32)]
        self.assertEqual(
            schedule,
            [3, 4, 3, 5, 3, 4, 3, 6, 3, 4, 3, 5, 3, 4, 3, 7,
             3, 4, 3, 5, 3, 4, 3, 6, 3, 4, 3, 5, 3, 4, 3],
        )
        for block in range(1, 33):
            packet_base = block * 256
            for group, expected in enumerate(schedule, start=1):
                actual = ((packet_base + 8 * group) &
                          -(packet_base + 8 * group)).bit_length() - 1
                self.assertEqual(actual, expected)

    def test_generated_coefficients_are_deterministic_and_signed(self) -> None:
        text, data = ordered.build_header()
        self.assertEqual(
            text,
            ordered.DEFAULT_OUT.read_text(encoding="ascii"),
        )
        self.assertEqual(data.gauss_c0.shape, (128, 16))
        self.assertEqual(data.gauss_c1.shape, (128, 16))
        self.assertEqual(data.moments.shape, (10, 128, 16))
        self.assertEqual(data.adjusted_sign_fits, 4)
        self.assertEqual(data.sign_mismatches, 0)

    def test_hard_tail_schedule_is_exact_and_compact(self) -> None:
        records = ordered.hard_records()
        self.assertEqual(len(records), 64)
        ranges = [int(record["raw_range"]) for record in records]
        self.assertEqual(sorted(set(ranges)), list(range(2032, 2048)))
        self.assertTrue(all(ranges.count(raw_range) == 4 for raw_range in set(ranges)))
        locations = {
            (int(record["packet"]) % 16, int(record["lane32"]))
            for record in records
        }
        self.assertEqual(locations, {(0, 0), (5, 10), (10, 21), (15, 31)})
        self.assertEqual(
            ordered.build_tail_asm(),
            ordered.DEFAULT_ASM_OUT.read_text(encoding="ascii"),
        )


if __name__ == "__main__":
    unittest.main()
