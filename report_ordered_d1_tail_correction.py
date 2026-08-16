#!/usr/bin/env python3
"""Report the deterministic sparse-tail structure before assembly changes."""

from __future__ import annotations

import math

import numpy as np
from scipy.special import ndtri

import generate_ordered_d1_coeffs as ordered


EXP8 = np.asarray([
    1.00000000361, 0.999999559932, 0.499999873009,
    0.166670788605, 0.0416673696717, 0.00832308095835,
    0.0013875434074, 0.0002077216867, 2.58406812172e-05,
])


def standard_call_projection() -> tuple[float, float, float]:
    data = ordered.build_data()
    u = ordered.ordered_values()
    words = np.asarray(u * (1 << 32), dtype=np.uint64).astype(np.uint32)
    raw_x = (np.uint32(0x3F800000) | (words >> 9)).view(np.float32)
    index = np.arange(ordered.BLOCK)
    rows = (index // 32) & 127
    lanes = (index % 32) & 15

    s0 = strike = 100.0
    rate = 0.05
    sigma = 0.2
    discount = math.exp(-rate)
    mu = rate - 0.5 * sigma * sigma
    scale = discount * s0 * math.exp(mu)
    beta = -discount * strike
    weighted = EXP8 * sigma ** np.arange(9)

    c0 = np.empty((128, 16), dtype=np.float32)
    c1 = np.empty_like(c0)
    for row in range(128):
        moment = data.moments[:, row]
        mean_y = np.tensordot(weighted, moment[:9], axes=(0, 0))
        mean_zy = np.tensordot(weighted, moment[1:], axes=(0, 0))
        mean_z = moment[1]
        variance = moment[2] - mean_z * mean_z
        slope = (mean_zy - mean_z * mean_y) / variance
        intercept = mean_y - slope * mean_z
        payoff_slope = np.asarray(scale * slope, dtype=np.float32)
        payoff_intercept = np.asarray(scale * intercept + beta, dtype=np.float32)
        c1[row] = np.asarray(payoff_slope * data.gauss_c1[row], dtype=np.float32)
        c0[row] = np.asarray(
            payoff_slope * data.gauss_c0[row] + payoff_intercept,
            dtype=np.float32,
        )

    cheap = np.maximum(
        np.asarray(c1[rows, lanes] * raw_x + c0[rows, lanes], dtype=np.float32),
        0.0,
    ).astype(np.float64)
    z = ndtri(u)
    exact_payoff = np.maximum(scale * np.exp(sigma * z) + beta, 0.0)
    raw_range = np.minimum(np.floor(np.abs(u - 0.5) * 4096).astype(int), 2047)
    corrected = cheap.copy()
    corrected[raw_range >= ordered.HARD_RANGE_MIN] = exact_payoff[
        raw_range >= ordered.HARD_RANGE_MIN
    ]
    return float(exact_payoff.mean()), float(cheap.mean()), float(corrected.mean())


def main() -> int:
    records = ordered.hard_records()
    phases = sorted({
        (int(record["packet"]) % 16, int(record["lane32"]))
        for record in records
    })
    ranges = {raw_range: 0 for raw_range in range(2032, 2048)}
    for record in records:
        ranges[int(record["raw_range"])] += 1
    exact, cheap, corrected = standard_call_projection()
    print("ORDERED D1 SPARSE-TAIL REPORT")
    print(f"hard points per 8192 : {len(records)}")
    print(f"packet phase/lane     : {phases}")
    print(f"occurrences per range : {sorted(set(ranges.values()))}")
    print(f"exact QMC price       : {exact:.12f}")
    print(f"lane-fit cheap price  : {cheap:.12f}  bias={cheap - exact:+.3e}")
    print(f"corrected price       : {corrected:.12f}  bias={corrected - exact:+.3e}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
