#!/usr/bin/env python3
"""
Rank scheduled zmm occurrences by distance from u=0.5 and test whether one
shared linear Gaussian coefficient pair is accurate for that occurrence.

This answers the phase question: which block/zmm iterations are in the flat
center region where coefficient sharing is plausible?
"""

import argparse
import csv
from pathlib import Path

import numpy as np
from scipy.special import ndtri

import generate_gaussian_coeffs as gg

LANES = 16
RANGES = 2048
MANTISSA_ONE = 0x3f800000


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument("--blocks", type=int, default=128)
    ap.add_argument("--skip-values", type=int, default=8192)
    ap.add_argument("--out", type=Path, default=Path("/tmp/center_phase_sharing.csv"))
    return ap.parse_args()


def raw_x_from_u(u: np.ndarray) -> np.ndarray:
    shifted = (np.asarray(u, dtype=np.float64) * (1 << 23)).astype(np.uint32)
    bits = shifted | np.uint32(MANTISSA_ONE)
    return bits.view(np.float32).astype(np.float64)


def fit_shared(x: np.ndarray, y: np.ndarray) -> tuple[float, float, float]:
    c1, c0 = np.polyfit(x, y, 1)
    err = float(np.max(np.abs((c1 * x + c0) - y)))
    return float(c1), float(c0), err


def bucket_summary(rows: list[dict[str, object]]) -> None:
    thresholds = [0, 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023, 1535, 2047]
    print("by max folded range:")
    for hi in thresholds:
        subset = [r for r in rows if int(r["max_range"]) <= hi]
        if not subset:
            continue
        errs = [float(r["shared_pair_max_abs_z_err"]) for r in subset]
        print(
            f"  <= {hi:4d}: count {len(subset):6d}, "
            f"<=1e-6 {sum(e <= 1e-6 for e in errs):6d}, "
            f"<=1e-5 {sum(e <= 1e-5 for e in errs):6d}, "
            f"<=1e-4 {sum(e <= 1e-4 for e in errs):6d}, "
            f"worst {max(errs):.6g}"
        )


def main() -> None:
    args = parse_args()
    block, mem_idx, logical, u = gg.scheduled_values(args.skip_values, gg.SOBOL_BLOCK_SIZE, args.blocks)
    x = raw_x_from_u(u)
    z = ndtri(u)
    folded = np.abs(u - 0.5) * (2.0 * RANGES)
    raw_range = np.minimum(np.floor(folded).astype(np.int64), RANGES - 1)

    rows = []
    total_zmms = args.blocks * (gg.SOBOL_BLOCK_SIZE // LANES)
    for n in range(total_zmms):
        lo = n * LANES
        hi = lo + LANES
        c1, c0, err = fit_shared(x[lo:hi], z[lo:hi])
        b = int(block[lo])
        m0 = int(mem_idx[lo])
        ranges = raw_range[lo:hi]
        us = u[lo:hi]
        rows.append({
            "block": b,
            "zmm_in_block": m0 // LANES,
            "mem_idx_start": m0,
            "logical_idx_start": int(logical[lo]),
            "internal_half": m0 // gg.WORK_CHUNK_SIZE,
            "coeff_zmm_slot": (m0 % gg.WORK_CHUNK_SIZE) // LANES,
            "min_range": int(np.min(ranges)),
            "max_range": int(np.max(ranges)),
            "mean_range": f"{float(np.mean(ranges)):.6f}",
            "u_min": f"{float(np.min(us)):.17g}",
            "u_max": f"{float(np.max(us)):.17g}",
            "shared_c1": f"{c1:.12g}",
            "shared_c0": f"{c0:.12g}",
            "shared_pair_max_abs_z_err": f"{err:.12g}",
        })

    rows.sort(key=lambda r: (int(r["max_range"]), float(r["shared_pair_max_abs_z_err"])))
    with args.out.open("w", encoding="ascii", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print(f"blocks              : {args.blocks}")
    print(f"zmm occurrences     : {len(rows)}")
    print(f"wrote               : {args.out}")
    bucket_summary(rows)
    print("best center examples:")
    for r in rows[:12]:
        print(
            f"  block {r['block']:>3} zmm {r['zmm_in_block']:>3} "
            f"range {r['min_range']}..{r['max_range']} "
            f"err {r['shared_pair_max_abs_z_err']}"
        )


if __name__ == "__main__":
    main()
