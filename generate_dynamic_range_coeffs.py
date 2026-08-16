#!/usr/bin/env python3
"""Generate compact D1-scheduled linear Gaussian coefficients.

The European kernel skips the first 8192 Sobol values and processes dimension
one in a fixed two-zmm schedule.  For normal pairs, the two zmm halves have the
same coarse-range pattern.  This generator fits one signed linear coefficient
vector for both halves and emits it in the exact lane order consumed by the
kernel.  The existing special/tail pairs are deliberately left untouched.
"""

from __future__ import annotations

import argparse
import re
from collections import Counter
from pathlib import Path

import numpy as np
from scipy.special import ndtri

import generate_gaussian_coeffs as gg


ROOT = Path(__file__).resolve().parent
DEFAULT_OUT = ROOT / "private" / "gaussian_dynamic_range_coeff_values.h"
BASELINE_HEADER = ROOT / "private" / "gaussian_linear_coeff_values_2048.h"
WORLDS = (64, 128, 256, 512, 1024)
TARGET = 1.0e-5
FORCED_SHARED_TARGET = 1.5e-5
PAIRS = 128
LANES = 16
# Exact GAUSS_STORE_PAIR_TAIL locations in sobol_european_avx512.s.
SPECIAL_TAIL_PAIRS = frozenset((0, 42, 63, 85, 106, 121, 127))
EXPECTED_WORLD_COUNTS = {64: 29, 128: 38, 256: 30, 512: 14, 1024: 8, 2048: 2}
EXPECTED_FORCED_SHARED_PAIRS = (21, 64)


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true", help="fail unless --out already matches")
    return ap.parse_args()


def parse_float_array(text: str, name: str) -> np.ndarray:
    match = re.search(
        rf"static const float {re.escape(name)}\[4096\].*?=\s*\{{(.*?)\n\}};",
        text,
        flags=re.S,
    )
    if not match:
        raise RuntimeError(f"missing {name} in {BASELINE_HEADER}")
    values = [
        float(token[:-1] if token.endswith("f") else token)
        for token in re.findall(
            r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:e[-+]?\d+)?f?",
            match.group(1),
        )
    ]
    if len(values) != 4096:
        raise RuntimeError(f"{name}: expected 4096 values, got {len(values)}")
    return np.asarray(values, dtype=np.float32).reshape(PAIRS, 2, LANES)


def signed_lattice(side_negative: bool, raw_range: int) -> tuple[np.ndarray, np.ndarray]:
    width = 0.5 / 2048.0
    lo = raw_range * width
    hi = (raw_range + 1) * width
    k0 = max(1, int(np.ceil(lo / gg.LATTICE_STEP)))
    k1 = min((1 << (gg.LATTICE_BITS - 1)) - 1, int(np.ceil(hi / gg.LATTICE_STEP)) - 1)
    k = np.arange(k0, k1 + 1, dtype=np.float64)
    d = k * gg.LATTICE_STEP
    u = 0.5 - d if side_negative else 0.5 + d
    x = np.asarray(1.0 + u, dtype=np.float32)
    return x, ndtri(u)


def fit_group(
    side_negative: bool,
    raw_ranges: set[int],
    lattice_cache: dict[tuple[bool, int], tuple[np.ndarray, np.ndarray]],
) -> tuple[np.float32, np.float32, float]:
    chunks = []
    for raw_range in sorted(raw_ranges):
        key = (side_negative, raw_range)
        if key not in lattice_cache:
            lattice_cache[key] = signed_lattice(side_negative, raw_range)
        chunks.append(lattice_cache[key])
    x = np.concatenate([chunk[0] for chunk in chunks])
    y = np.concatenate([chunk[1] for chunk in chunks])
    c1, c0 = np.polyfit(x.astype(np.float64), y, 1)
    c1f = np.float32(c1)
    c0f = np.float32(c0)
    # Float64 is exact enough for the float32 product/add before the one final
    # rounding performed by vfmadd132ps; cast only once to model that FMA.
    predicted = np.asarray(c1f.astype(np.float64) * x.astype(np.float64) + c0f, dtype=np.float32)
    error = float(np.max(np.abs(predicted.astype(np.float64) - y)))
    return c0f, c1f, error


def scheduled_ranges() -> tuple[np.ndarray, np.ndarray]:
    _block, _mem, _logical, u = gg.scheduled_values(8192, gg.SOBOL_BLOCK_SIZE, 1)
    hot = np.asarray(u[: gg.WORK_CHUNK_SIZE], dtype=np.float64)
    raw = np.minimum(np.floor(np.abs(hot - 0.5) * 4096.0).astype(np.int64), 2047)
    negative = hot < 0.5
    return raw.reshape(PAIRS, 2, LANES), negative.reshape(PAIRS, 2, LANES)


def fmt_f32(value: np.float32) -> str:
    body = f"{float(value):.9g}"
    if "e" not in body and "." not in body:
        body += ".0"
    return body + "f"


def write_float_rows(lines: list[str], name: str, values: np.ndarray) -> None:
    lines.append(f"static const float {name}[{values.shape[0]}][16] __attribute__((aligned(64))) = {{")
    for row in values:
        lines.append("    { " + ", ".join(fmt_f32(value) for value in row) + " },")
    lines.append("};")
    lines.append("")


def build_header() -> tuple[str, dict[str, object]]:
    baseline = BASELINE_HEADER.read_text(encoding="ascii")
    base_c0 = parse_float_array(baseline, "gauss_linear_c0")
    base_c1 = parse_float_array(baseline, "gauss_linear_c1")
    raw, negative = scheduled_ranges()

    c0 = np.zeros((PAIRS, LANES), dtype=np.float32)
    c1 = np.zeros((PAIRS, LANES), dtype=np.float32)
    world_by_pair = np.zeros(PAIRS, dtype=np.uint16)
    sign_by_pair = np.zeros(PAIRS, dtype=np.uint8)
    forced_shared_pairs = []
    worst_by_pair = np.zeros(PAIRS, dtype=np.float64)
    lattice_cache: dict[tuple[bool, int], tuple[np.ndarray, np.ndarray]] = {}
    counts: Counter[int] = Counter()

    for pair in range(PAIRS):
        sides = np.unique(negative[pair])
        if sides.size != 1:
            raise RuntimeError(f"pair {pair} is not sign homogeneous")
        side_negative = bool(sides[0])
        sign_by_pair[pair] = int(side_negative)
        if pair in SPECIAL_TAIL_PAIRS:
            continue

        chosen_world = 2048
        chosen_coeffs: dict[int, tuple[np.float32, np.float32, float]] | None = None
        for world in WORLDS:
            bucket_a = (raw[pair, 0] * world) // 2048
            bucket_b = (raw[pair, 1] * world) // 2048
            if not np.array_equal(bucket_a, bucket_b):
                continue
            groups: dict[int, set[int]] = {}
            for half in (0, 1):
                for raw_range, bucket in zip(raw[pair, half], (raw[pair, half] * world) // 2048):
                    groups.setdefault(int(bucket), set()).add(int(raw_range))
            fitted = {
                bucket: fit_group(side_negative, ranges, lattice_cache)
                for bucket, ranges in groups.items()
            }
            if max(coeff[2] for coeff in fitted.values()) <= TARGET:
                chosen_world = world
                chosen_coeffs = fitted
                break

        world_by_pair[pair] = chosen_world
        counts[chosen_world] += 1
        if chosen_coeffs is None:
            # The two halves do not share a 2048 bucket in these rare phases.
            # Fit the union of the two raw ranges lane by lane.  This keeps the
            # kernel branchless and instruction-neutral; the relaxed bound is
            # still checked over every representable input in both ranges.
            forced_shared_pairs.append(pair)
            lane_fits = [
                fit_group(
                    side_negative,
                    {int(raw[pair, 0, lane]), int(raw[pair, 1, lane])},
                    lattice_cache,
                )
                for lane in range(LANES)
            ]
            c0[pair] = [fit[0] for fit in lane_fits]
            c1[pair] = [fit[1] for fit in lane_fits]
            worst_by_pair[pair] = max(fit[2] for fit in lane_fits)
            if worst_by_pair[pair] > FORCED_SHARED_TARGET:
                raise RuntimeError(
                    f"forced shared pair {pair} exceeds {FORCED_SHARED_TARGET}: "
                    f"{worst_by_pair[pair]}"
                )
            continue

        buckets = (raw[pair, 0] * chosen_world) // 2048
        c0[pair] = [chosen_coeffs[int(bucket)][0] for bucket in buckets]
        c1[pair] = [chosen_coeffs[int(bucket)][1] for bucket in buckets]
        worst_by_pair[pair] = max(coeff[2] for coeff in chosen_coeffs.values())

    if dict(sorted(counts.items())) != EXPECTED_WORLD_COUNTS:
        raise RuntimeError(f"unexpected world counts: {dict(sorted(counts.items()))}")
    actual_forced = tuple(forced_shared_pairs)
    if actual_forced != EXPECTED_FORCED_SHARED_PAIRS:
        raise RuntimeError(f"unexpected forced shared pairs: {actual_forced}")
    lines = [
        "/* Generated by generate_dynamic_range_coeffs.py. */",
        "#ifndef GAUSSIAN_DYNAMIC_RANGE_COEFF_VALUES_H",
        "#define GAUSSIAN_DYNAMIC_RANGE_COEFF_VALUES_H",
        "",
        "#include <stdint.h>",
        "",
        f"#define GAUSS_DYNAMIC_TARGET {TARGET:.9g}f",
        f"#define GAUSS_DYNAMIC_FORCED_SHARED_TARGET {FORCED_SHARED_TARGET:.9g}f",
        f"#define GAUSS_DYNAMIC_PAIRS {PAIRS}u",
        f"#define GAUSS_DYNAMIC_FORCED_SHARED_PAIRS {len(actual_forced)}u",
        f"#define GAUSS_DYNAMIC_SPECIAL_TAIL_PAIRS {len(SPECIAL_TAIL_PAIRS)}u",
        "#define GAUSS_DYNAMIC_COEFF_BYTES 16384u",
        "",
    ]
    write_float_rows(lines, "gauss_dynamic_c0", c0)
    write_float_rows(lines, "gauss_dynamic_c1", c1)
    lines.append("static const uint16_t gauss_dynamic_world_by_pair[128] __attribute__((aligned(64))) = {")
    for start in range(0, PAIRS, 16):
        lines.append("    " + ", ".join(f"{int(v)}u" for v in world_by_pair[start:start + 16]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("static const uint8_t gauss_dynamic_negative_by_pair[128] __attribute__((aligned(64))) = {")
    for start in range(0, PAIRS, 16):
        lines.append("    " + ", ".join(f"{int(v)}u" for v in sign_by_pair[start:start + 16]) + ",")
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")
    report = {
        "world_counts": dict(sorted(counts.items())),
        "forced_shared_pairs": list(actual_forced),
        "special_tail_pairs": sorted(SPECIAL_TAIL_PAIRS),
        "worst_selected_error": float(np.max(worst_by_pair)),
    }
    return "\n".join(lines), report


def main() -> int:
    args = parse_args()
    text, report = build_header()
    if args.check:
        if not args.out.exists() or args.out.read_text(encoding="ascii") != text:
            raise SystemExit(f"error: {args.out} is not the deterministic generated header")
        print(f"verified             : {args.out}")
    else:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="ascii")
        print(f"wrote                : {args.out}")
    print(f"world counts         : {report['world_counts']}")
    print(f"forced shared pairs  : {report['forced_shared_pairs']}")
    print(f"special tail pairs   : {report['special_tail_pairs']}")
    print(f"worst selected error : {report['worst_selected_error']:.12g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
