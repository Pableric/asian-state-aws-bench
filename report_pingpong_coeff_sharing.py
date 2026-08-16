#!/usr/bin/env python3
"""
Report whether the scheduled linear Gaussian coefficients can be compressed by
zmm slot or by the 4096-value ping/pong half.

This is analysis-only tooling. It does not emit kernel headers and does not
change the normal Gaussian path.
"""

import argparse
import csv
import re
from pathlib import Path

import numpy as np
from scipy.special import ndtri

import generate_gaussian_coeffs as gg


HEADER = Path("private/gaussian_linear_coeff_values_2048.h")
LANES = 16
COEFF_VALUES = 4096
ZMM_SLOTS = COEFF_VALUES // LANES
MANTISSA_ONE = 0x3f800000


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser()
    ap.add_argument("--blocks", type=int, default=128)
    ap.add_argument("--skip-values", type=int, default=8192)
    ap.add_argument("--out", type=Path, default=Path("/tmp/pingpong_coeff_sharing.csv"))
    ap.add_argument("--max-report", type=int, default=12)
    return ap.parse_args()


def parse_float_array(text: str, name: str) -> np.ndarray:
    match = re.search(
        rf"static const float {re.escape(name)}\[\d+\].*?=\s*\{{(.*?)\n\}};",
        text,
        flags=re.S,
    )
    if not match:
        raise ValueError(f"could not find {name}")
    values = [float(token[:-1] if token.endswith("f") else token)
              for token in re.findall(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:e[-+]?\d+)?f?", match.group(1))]
    if len(values) != COEFF_VALUES:
        raise ValueError(f"{name}: expected {COEFF_VALUES}, got {len(values)}")
    return np.asarray(values, dtype=np.float64)


def raw_x_from_u(u: np.ndarray) -> np.ndarray:
    shifted = (np.asarray(u, dtype=np.float64) * (1 << 23)).astype(np.uint32)
    bits = shifted | np.uint32(MANTISSA_ONE)
    return bits.view(np.float32).astype(np.float64)


def fit_shared(x: np.ndarray, y: np.ndarray) -> tuple[float, float, float]:
    # y ~= c1*x + c0
    a, b = np.polyfit(x, y, 1)
    err = float(np.max(np.abs((a * x + b) - y)))
    return float(a), float(b), err


def max_err_shared_c0(x: np.ndarray, y: np.ndarray, c1: np.ndarray) -> tuple[float, float]:
    b = float(np.mean(y - c1 * x))
    return b, float(np.max(np.abs((c1 * x + b) - y)))


def max_err_shared_c1(x: np.ndarray, y: np.ndarray, c0: np.ndarray) -> tuple[float, float]:
    denom = float(np.sum(x * x))
    a = float(np.sum(x * (y - c0)) / denom)
    return a, float(np.max(np.abs((a * x + c0) - y)))


def grouped_shared_err(x: np.ndarray, y: np.ndarray, lanes: np.ndarray, group_size: int) -> float:
    errs = []
    for group_start in range(0, LANES, group_size):
        group_end = group_start + group_size
        m = (lanes >= group_start) & (lanes < group_end)
        _, _, err = fit_shared(x[m], y[m])
        errs.append(err)
    return float(max(errs))


def threshold_counts(errors: list[float]) -> dict[str, int]:
    return {
        "1e-7": sum(err <= 1e-7 for err in errors),
        "1e-6": sum(err <= 1e-6 for err in errors),
        "1e-5": sum(err <= 1e-5 for err in errors),
        "1e-4": sum(err <= 1e-4 for err in errors),
        "1e-3": sum(err <= 1e-3 for err in errors),
    }


def print_group_summary(name: str, rows: list[dict[str, str]], key: str) -> None:
    vals = [float(row[key]) for row in rows]
    if not vals:
        print(f"{name:22s}: no rows")
        return
    print(f"{name:22s}: thresholds {threshold_counts(vals)} worst {max(vals):.12g}")


def main() -> None:
    args = parse_args()
    text = HEADER.read_text(encoding="ascii")
    c0 = parse_float_array(text, "gauss_linear_c0")
    c1 = parse_float_array(text, "gauss_linear_c1")

    block, mem_idx, _logical, u = gg.scheduled_values(args.skip_values, gg.SOBOL_BLOCK_SIZE, args.blocks)
    coeff_idx = mem_idx % COEFF_VALUES
    zmm_slot = coeff_idx // LANES
    lane = coeff_idx % LANES
    half = mem_idx // gg.WORK_CHUNK_SIZE
    x = raw_x_from_u(u)
    true_z = ndtri(u)
    baseline_z = c1[coeff_idx] * x + c0[coeff_idx]

    rows = []
    errors = {
        "shared_zmm": [],
        "shared_zmm_pingpong": [],
        "shared_lane2": [],
        "shared_lane4": [],
        "shared_lane8": [],
        "shared_c0": [],
        "shared_c1": [],
    }

    for slot in range(ZMM_SLOTS):
        slot_mask = zmm_slot == slot
        xs = x[slot_mask]
        ys = true_z[slot_mask]
        lanes = lane[slot_mask]
        exact = baseline_z[slot_mask]
        exact_err = float(np.max(np.abs(exact - ys)))

        _, _, shared_err = fit_shared(xs, ys)
        lane2_err = grouped_shared_err(xs, ys, lanes, 2)
        lane4_err = grouped_shared_err(xs, ys, lanes, 4)
        lane8_err = grouped_shared_err(xs, ys, lanes, 8)

        ping_errs = []
        for h in (0, 1):
            m = slot_mask & (half == h)
            _, _, err = fit_shared(x[m], true_z[m])
            ping_errs.append(err)
        ping_err = max(ping_errs)

        lane_c1 = c1[slot * LANES + lanes]
        lane_c0 = c0[slot * LANES + lanes]
        _, shared_c0_err = max_err_shared_c0(xs, ys, lane_c1)
        _, shared_c1_err = max_err_shared_c1(xs, ys, lane_c0)

        errors["shared_zmm"].append(shared_err)
        errors["shared_zmm_pingpong"].append(ping_err)
        errors["shared_lane2"].append(lane2_err)
        errors["shared_lane4"].append(lane4_err)
        errors["shared_lane8"].append(lane8_err)
        errors["shared_c0"].append(shared_c0_err)
        errors["shared_c1"].append(shared_c1_err)

        rows.append({
            "zmm_slot": slot,
            "coeff_start": slot * LANES,
            "exact_current_max_abs_z_err": f"{exact_err:.12g}",
            "shared_zmm_max_abs_z_err": f"{shared_err:.12g}",
            "shared_zmm_pingpong_max_abs_z_err": f"{ping_err:.12g}",
            "shared_lane2_max_abs_z_err": f"{lane2_err:.12g}",
            "shared_lane4_max_abs_z_err": f"{lane4_err:.12g}",
            "shared_lane8_max_abs_z_err": f"{lane8_err:.12g}",
            "shared_c0_lane_c1_max_abs_z_err": f"{shared_c0_err:.12g}",
            "shared_c1_lane_c0_max_abs_z_err": f"{shared_c1_err:.12g}",
        })

    with args.out.open("w", encoding="ascii", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print(f"blocks              : {args.blocks}")
    print(f"zmm slots           : {ZMM_SLOTS}")
    print(f"wrote               : {args.out}")
    for name, vals in errors.items():
        print(f"{name:22s}: thresholds {threshold_counts(vals)} worst {max(vals):.12g}")

    exact_good_1e6 = [row for row in rows if float(row["exact_current_max_abs_z_err"]) <= 1e-6]
    exact_good_1e5 = [row for row in rows if float(row["exact_current_max_abs_z_err"]) <= 1e-5]
    print(f"current exact <=1e-6 : {len(exact_good_1e6)}")
    print(f"current exact <=1e-5 : {len(exact_good_1e5)}")
    print_group_summary("good1e-6 shared_zmm", exact_good_1e6, "shared_zmm_max_abs_z_err")
    print_group_summary("good1e-6 pingpong", exact_good_1e6, "shared_zmm_pingpong_max_abs_z_err")
    print_group_summary("good1e-6 lane2", exact_good_1e6, "shared_lane2_max_abs_z_err")
    print_group_summary("good1e-6 lane4", exact_good_1e6, "shared_lane4_max_abs_z_err")
    print_group_summary("good1e-6 lane8", exact_good_1e6, "shared_lane8_max_abs_z_err")
    print_group_summary("good1e-5 shared_zmm", exact_good_1e5, "shared_zmm_max_abs_z_err")
    print_group_summary("good1e-5 pingpong", exact_good_1e5, "shared_zmm_pingpong_max_abs_z_err")
    print_group_summary("good1e-5 lane2", exact_good_1e5, "shared_lane2_max_abs_z_err")
    print_group_summary("good1e-5 lane4", exact_good_1e5, "shared_lane4_max_abs_z_err")
    print_group_summary("good1e-5 lane8", exact_good_1e5, "shared_lane8_max_abs_z_err")

    for name, vals in errors.items():
        worst = sorted(zip(vals, range(ZMM_SLOTS)), reverse=True)[:args.max_report]
        preview = ", ".join(f"{slot}:{err:.3g}" for err, slot in worst)
        print(f"worst {name:16s}: {preview}")


if __name__ == "__main__":
    main()
