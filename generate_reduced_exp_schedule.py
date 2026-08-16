#!/usr/bin/env python3
"""Generate and validate D1 slot bounds for the reduced-FMA exp path."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

import numpy as np
from scipy.special import ndtri

import generate_gaussian_coeffs as gg


ROOT = Path(__file__).resolve().parent
DEFAULT_OUT = ROOT / "european_reduced_exp_slots.h"
SLOTS = 256
LANES = 16
CELL_COUNT = 8192
MAX_ALPHA = 0.20
# The production quadratic is gated at 5.94719955e-5.  Preserving its D1-cell
# mean with one FMA makes that exact bound infeasible; 8e-5 is the measured,
# independently enforced true-exp envelope for the reduced path.
TARGET_REL_ERR = 8.0e-5
SPECIAL_TAIL_PAIRS = frozenset((0, 42, 63, 85, 106, 121, 127))
EXP8 = (
    1.00000000361,
    0.999999559932,
    0.499999873009,
    0.166670788605,
    0.0416673696717,
    0.00832308095835,
    0.0013875434074,
    0.0002077216867,
    2.58406812172e-05,
)


def parse_exp_array(name: str) -> np.ndarray:
    text = (ROOT / "european_exp_table_64.h").read_text(encoding="ascii")
    match = re.search(
        rf"static const float {re.escape(name)}\[[^]]+\].*?=\s*\{{(.*?)\n\}};",
        text,
        flags=re.S,
    )
    if not match:
        raise RuntimeError(f"missing {name} in european_exp_table_64.h")
    return np.asarray([
        float(token[:-1] if token.endswith("f") else token)
        for token in re.findall(
            r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:e[-+]?\d+)?f?",
            match.group(1),
        )
    ], dtype=np.float32)


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true")
    return ap.parse_args()


def exp_poly8(x: float) -> float:
    value = EXP8[-1]
    for coeff in EXP8[-2::-1]:
        value = value * x + coeff
    return value


def slot_cells() -> np.ndarray:
    _block, _mem, _logical, values = gg.scheduled_values(
        gg.DEFAULT_SKIP_VALUES, gg.SOBOL_BLOCK_SIZE, 1
    )
    values = values[: gg.WORK_CHUNK_SIZE].reshape(SLOTS, LANES)
    return np.floor(values * CELL_COUNT).astype(np.int64)


def slot_bounds() -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    cell = slot_cells()
    lo = np.empty(SLOTS, dtype=np.float64)
    hi = np.empty(SLOTS, dtype=np.float64)
    tail = np.zeros(SLOTS, dtype=np.uint8)
    for slot in range(SLOTS):
        u_lo = float(np.min(cell[slot])) / CELL_COUNT
        u_hi = float(np.max(cell[slot]) + 1) / CELL_COUNT
        if not 0.0 < u_lo < u_hi < 1.0:
            raise RuntimeError(f"slot {slot} contains an unsupported endpoint cell")
        lo[slot] = ndtri(u_lo)
        hi[slot] = ndtri(u_hi)
        tail[slot] = int(slot // 2 in SPECIAL_TAIL_PAIRS)
    if int(np.sum(tail)) != 14:
        raise RuntimeError(f"expected 14 tail slots, got {int(np.sum(tail))}")
    return lo, hi, tail


def slot_normal_moments() -> np.ndarray:
    """Return E[z**k], k=0..9, over each slot's 16 exact D1 cells.

    The cell distribution is uniform in u, not in z.  Gauss-Legendre
    quadrature captures that distribution while keeping coefficient setup a
    small fixed-size moment evaluation.
    """
    cells = slot_cells()
    nodes, weights = np.polynomial.legendre.leggauss(8)
    moments = np.empty((SLOTS, 10), dtype=np.float64)
    for slot in range(SLOTS):
        samples = []
        sample_weights = []
        for cell in cells[slot]:
            u0 = float(cell) / CELL_COUNT
            u1 = float(cell + 1) / CELL_COUNT
            u = 0.5 * (u0 + u1) + 0.5 * (u1 - u0) * nodes
            samples.extend(ndtri(u))
            sample_weights.extend(weights)
        z = np.asarray(samples, dtype=np.float64)
        w = np.asarray(sample_weights, dtype=np.float64)
        w /= np.sum(w)
        moments[slot] = [float(np.sum(w * z**power)) for power in range(10)]
    return moments


def build_coefficients(
    lo_f32: np.ndarray, hi_f32: np.ndarray, tail: np.ndarray,
    moments: np.ndarray, alpha: float
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    coeff = np.zeros((SLOTS, 4), dtype=np.float32)
    q2 = parse_exp_array("european_exp64_c2")
    q1 = parse_exp_array("european_exp64_c1")
    q0 = parse_exp_array("european_exp64_c0")
    zmid = parse_exp_array("european_exp_regular_zmid")
    for slot in range(SLOTS):
        x_lo = alpha * float(lo_f32[slot])
        x_hi = alpha * float(hi_f32[slot])
        mid = 0.5 * (x_lo + x_hi)
        half = 0.5 * (x_hi - x_lo)
        e_mid = exp_poly8(mid)
        if tail[slot]:
            coeff[slot, 3] = np.float32(e_mid / 6.0)
            coeff[slot, 2] = np.float32(e_mid * (0.5 - 0.5 * mid))
            coeff[slot, 1] = np.float32(e_mid * (1.0 - mid + 0.5 * mid * mid))
            coeff[slot, 0] = np.float32(
                e_mid * (1.0 - mid + 0.5 * mid * mid - mid * mid * mid / 6.0)
            )
        else:
            bucket = max(0, min(63, int((alpha * float(zmid[slot]) + 6.0) * (16.0 / 3.0))))
            powers = alpha ** np.arange(10, dtype=np.float64)
            mean_y = sum(EXP8[k] * powers[k] * moments[slot, k] for k in range(9))
            mean_xy = sum(
                EXP8[k] * powers[k + 1] * moments[slot, k + 1]
                for k in range(9)
            )
            mean_x = alpha * moments[slot, 1]
            mean_x2 = alpha * alpha * moments[slot, 2]
            mean_x3 = alpha * alpha * alpha * moments[slot, 3]
            variance_x = alpha * alpha * (
                moments[slot, 2] - moments[slot, 1] * moments[slot, 1]
            )
            slope = (mean_xy - mean_x * mean_y) / variance_x
            baseline_mean = (
                float(q0[bucket]) + float(q1[bucket]) * mean_x
                + float(q2[bucket]) * mean_x2
            )
            intercept = baseline_mean - slope * mean_x
            coeff[slot, 1] = np.float32(slope)
            coeff[slot, 0] = np.float32(intercept)
    return coeff[:, 0], coeff[:, 1], coeff[:, 2], coeff[:, 3]


def fused(a: np.ndarray | np.float32, x: np.ndarray, b: np.ndarray | np.float32) -> np.ndarray:
    return np.asarray(
        np.asarray(a, dtype=np.float64) * x.astype(np.float64)
        + np.asarray(b, dtype=np.float64),
        dtype=np.float32,
    )


def validate(
    true_lo: np.ndarray,
    true_hi: np.ndarray,
    tail: np.ndarray,
    coeff: tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray],
    alpha: float,
) -> dict[str, float]:
    c0, c1, c2, c3 = coeff
    normal_worst = 0.0
    tail_worst = 0.0
    for slot in range(SLOTS):
        x = np.linspace(alpha * true_lo[slot], alpha * true_hi[slot], 8193)
        xf = np.asarray(x, dtype=np.float32)
        if tail[slot]:
            predicted = fused(c3[slot], xf, c2[slot])
            predicted = fused(predicted, xf, c1[slot])
            predicted = fused(predicted, xf, c0[slot])
        else:
            predicted = fused(c1[slot], xf, c0[slot])
        error = float(
            np.max(np.abs(predicted.astype(np.float64) - np.exp(x)) / np.exp(x))
        )
        if tail[slot]:
            tail_worst = max(tail_worst, error)
        else:
            normal_worst = max(normal_worst, error)
    if normal_worst > TARGET_REL_ERR or tail_worst > TARGET_REL_ERR:
        raise RuntimeError(
            f"reduced exp gate failed: normal={normal_worst}, tail={tail_worst}"
        )
    return {"normal_worst": normal_worst, "tail_worst": tail_worst}


def fmt_float(value: np.float32) -> str:
    body = f"{float(value):.9g}"
    if "." not in body and "e" not in body:
        body += ".0"
    return body + "f"


def build_header() -> tuple[str, dict[str, float]]:
    true_lo, true_hi, tail = slot_bounds()
    moments = slot_normal_moments()
    pair_moments = 0.5 * (moments[0::2] + moments[1::2])
    pair_slot_mean_z = np.stack((moments[0::2, 1], moments[1::2, 1]))
    pair_slot_mean_z2 = np.stack((moments[0::2, 2], moments[1::2, 2]))
    zmid = parse_exp_array("european_exp_regular_zmid")
    pair_zmid = np.stack((zmid[0::2], zmid[1::2]))
    tail_indices = np.flatnonzero(tail)
    tail_lo = np.zeros(16, dtype=np.float32)
    tail_hi = np.zeros(16, dtype=np.float32)
    tail_lo[:len(tail_indices)] = np.asarray(true_lo[tail_indices], dtype=np.float32)
    tail_hi[:len(tail_indices)] = np.asarray(true_hi[tail_indices], dtype=np.float32)
    lo = np.asarray(true_lo, dtype=np.float32)
    hi = np.asarray(true_hi, dtype=np.float32)
    coeff = build_coefficients(lo, hi, tail, moments, MAX_ALPHA)
    report = validate(true_lo, true_hi, tail, coeff, MAX_ALPHA)
    lines = [
        "/* Generated by generate_reduced_exp_schedule.py. */",
        "#ifndef EUROPEAN_REDUCED_EXP_SLOTS_H",
        "#define EUROPEAN_REDUCED_EXP_SLOTS_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define EUROPEAN_REDUCED_EXP_MAX_ALPHA {MAX_ALPHA:.9g}f",
        f"#define EUROPEAN_REDUCED_EXP_TARGET_REL_ERR {TARGET_REL_ERR:.9g}f",
        f"#define EUROPEAN_REDUCED_EXP_NORMAL_WORST_REL_ERR {report['normal_worst']:.9g}f",
        f"#define EUROPEAN_REDUCED_EXP_TAIL_WORST_REL_ERR {report['tail_worst']:.9g}f",
        "#define EUROPEAN_REDUCED_EXP_SCHEDULE_STREAMS 6u",
        f"#define EUROPEAN_REDUCED_EXP_TAIL_SLOTS {int(np.count_nonzero(tail))}u",
        "#define EUROPEAN_REDUCED_EXP_TAIL_STRIDE 16u",
        "#define EUROPEAN_REDUCED_EXP_TAIL_STREAMS 4u",
        "",
        "static const float european_reduced_exp_z_lo[256] __attribute__((aligned(64))) = {",
    ]
    for start in range(0, SLOTS, 8):
        lines.append("    " + ", ".join(fmt_float(v) for v in lo[start:start + 8]) + ",")
    lines.extend([
        "};",
        "",
        "static const float european_reduced_exp_z_hi[256] __attribute__((aligned(64))) = {",
    ])
    for start in range(0, SLOTS, 8):
        lines.append("    " + ", ".join(fmt_float(v) for v in hi[start:start + 8]) + ",")
    lines.extend([
        "};",
        "",
        "static const uint8_t european_reduced_exp_tail_slot[256] __attribute__((aligned(64))) = {",
    ])
    for start in range(0, SLOTS, 32):
        lines.append("    " + ", ".join(str(int(v)) for v in tail[start:start + 32]) + ",")
    lines.extend(["};", ""])
    lines.extend([
        "static const double european_reduced_pair_moment[10][128] __attribute__((aligned(64))) = {"
    ])
    for power_index in range(10):
        lines.append("    {" + ", ".join(
            f"{value:.17g}" for value in pair_moments[:, power_index]) + "},")
    lines.extend(["};", ""])
    for name, values in (
        ("european_reduced_pair_slot_mean_z", pair_slot_mean_z),
        ("european_reduced_pair_slot_mean_z2", pair_slot_mean_z2),
    ):
        lines.append(
            f"static const double {name}[2][128] __attribute__((aligned(64))) = {{")
        for row in values:
            lines.append("    {" + ", ".join(f"{value:.17g}" for value in row) + "},")
        lines.extend(["};", ""])
    lines.append(
        "static const float european_reduced_pair_zmid[2][128] __attribute__((aligned(64))) = {")
    for row in pair_zmid:
        lines.append("    {" + ", ".join(fmt_float(value) for value in row) + "},")
    lines.extend(["};", ""])
    for name, values in (
        ("european_reduced_tail_lo", tail_lo),
        ("european_reduced_tail_hi", tail_hi),
    ):
        lines.append(
            f"static const float {name}[16] __attribute__((aligned(64))) = {{")
        lines.append("    " + ", ".join(fmt_float(value) for value in values) + ",")
        lines.extend(["};", ""])
    lines.extend(["#endif", ""])
    return "\n".join(lines), report


def main() -> int:
    args = parse_args()
    text, report = build_header()
    if args.check:
        if not args.out.exists() or args.out.read_text(encoding="ascii") != text:
            raise SystemExit(f"error: {args.out} is not the deterministic generated header")
        action = "verified"
    else:
        args.out.write_text(text, encoding="ascii")
        action = "wrote"
    print(f"{action}: {args.out}")
    print(f"normal worst relative error: {report['normal_worst']:.12g}")
    print(f"tail worst relative error  : {report['tail_worst']:.12g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
