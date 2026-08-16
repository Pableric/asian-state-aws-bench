#!/usr/bin/env python3
"""Generate compact coefficients for the consecutive-32 D1 prototype."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.special import ndtri
from scipy.stats import qmc

import generate_dynamic_range_coeffs as dynamic
import generate_gaussian_coeffs as gaussian


ROOT = Path(__file__).resolve().parent
DEFAULT_OUT = ROOT / "private" / "european_ordered_d1_coeffs.h"
DEFAULT_ASM_OUT = ROOT / "private" / "european_ordered_d1_tail.inc"
SKIP = 8192
BLOCK = 8192
PAIRS = 128
LANES = 16
CELL_COUNT = 8192
QUADRATURE_ORDER = 8
HARD_RANGE_MIN = 2032
HARD_POINTS = 64


@dataclass(frozen=True)
class OrderedData:
    gauss_c0: np.ndarray
    gauss_c1: np.ndarray
    moments: np.ndarray
    adjusted_sign_fits: int
    sign_mismatches: int
    worst_gaussian_error: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--asm-out", type=Path, default=DEFAULT_ASM_OUT)
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def ordered_values() -> np.ndarray:
    sampler = qmc.Sobol(d=1, scramble=False)
    return sampler.random_base2(m=14)[SKIP : SKIP + BLOCK, 0]


def _fma_model(c1: np.float32, x: np.ndarray, c0: np.float32) -> np.ndarray:
    return np.asarray(
        c1.astype(np.float64) * x.astype(np.float64) + c0.astype(np.float64),
        dtype=np.float32,
    )


def _guard_sign(
    c0: np.float32,
    c1: np.float32,
    x: np.ndarray,
    negative: bool,
) -> tuple[np.float32, bool]:
    predicted = _fma_model(c1, x, c0)
    adjusted = False
    if negative and float(np.max(predicted)) >= 0.0:
        c0 = np.float32(c0 - (float(np.max(predicted)) + np.finfo(np.float32).eps))
        adjusted = True
    elif not negative and float(np.min(predicted)) <= 0.0:
        c0 = np.float32(c0 + (float(-np.min(predicted)) + np.finfo(np.float32).eps))
        adjusted = True
    return c0, adjusted


def build_data() -> OrderedData:
    values = ordered_values()
    packets = values.reshape(256, 32)
    raw_ranges = np.minimum(
        np.floor(np.abs(packets - 0.5) * 4096.0).astype(np.int64), 2047
    )
    negative = packets < 0.5

    gauss_c0 = np.empty((PAIRS, LANES), dtype=np.float32)
    gauss_c1 = np.empty((PAIRS, LANES), dtype=np.float32)
    lattice_cache: dict[tuple[bool, int], tuple[np.ndarray, np.ndarray]] = {}
    adjusted = 0
    sign_mismatches = 0
    worst_error = 0.0

    for pair in range(PAIRS):
        for lane in range(LANES):
            locations = (
                (pair, lane),
                (pair, lane + LANES),
                (pair + PAIRS, lane),
                (pair + PAIRS, lane + LANES),
            )
            sides = {bool(negative[p, column]) for p, column in locations}
            if len(sides) != 1:
                raise RuntimeError(f"pair {pair} lane {lane} changes Gaussian sign")
            side_negative = sides.pop()
            ranges = {int(raw_ranges[p, column]) for p, column in locations}
            c0, c1, _error = dynamic.fit_group(side_negative, ranges, lattice_cache)
            chunks = [dynamic.signed_lattice(side_negative, value) for value in ranges]
            x = np.concatenate([chunk[0] for chunk in chunks])
            y = np.concatenate([chunk[1] for chunk in chunks])
            c0, changed = _guard_sign(c0, c1, x, side_negative)
            adjusted += int(changed)
            predicted = _fma_model(c1, x, c0)
            sign_mismatches += int(np.count_nonzero((predicted < 0.0) != (y < 0.0)))
            worst_error = max(
                worst_error,
                float(np.max(np.abs(predicted.astype(np.float64) - y))),
            )
            gauss_c0[pair, lane] = c0
            gauss_c1[pair, lane] = c1

    cells = np.floor(packets * CELL_COUNT).astype(np.int64)
    nodes, weights = np.polynomial.legendre.leggauss(QUADRATURE_ORDER)
    # The payoff line is consumed lane-wise, so its moments must be lane-wise
    # too.  Hard cells are deliberately excluded: their cheap value is removed
    # again by the sparse correction pass, so allowing them to influence the
    # ordinary-cell fit only makes the remaining points worse.
    moments = np.empty((10, PAIRS, LANES), dtype=np.float64)
    for pair in range(PAIRS):
        for lane in range(LANES):
            locations = (
                (pair, lane),
                (pair, lane + LANES),
                (pair + PAIRS, lane),
                (pair + PAIRS, lane + LANES),
            )
            selected = [
                cells[packet, column]
                for packet, column in locations
                if raw_ranges[packet, column] < HARD_RANGE_MIN
            ]
            if len(selected) not in (2, 4):
                raise RuntimeError(
                    f"pair {pair} lane {lane}: expected 2 or 4 normal cells, "
                    f"got {len(selected)}"
                )
            samples = []
            sample_weights = []
            for cell in selected:
                u0 = float(cell) / CELL_COUNT
                u1 = float(cell + 1) / CELL_COUNT
                u = 0.5 * (u0 + u1) + 0.5 * (u1 - u0) * nodes
                samples.extend(ndtri(u))
                sample_weights.extend(weights)
            z = np.asarray(samples, dtype=np.float64)
            w = np.asarray(sample_weights, dtype=np.float64)
            w /= np.sum(w)
            for power in range(10):
                moments[power, pair, lane] = float(np.sum(w * z**power))

    if sign_mismatches:
        raise RuntimeError(f"ordered coefficient sign mismatches: {sign_mismatches}")
    return OrderedData(
        gauss_c0,
        gauss_c1,
        moments,
        adjusted,
        sign_mismatches,
        worst_error,
    )


def _fmt_f32(value: np.float32) -> str:
    body = f"{float(value):.9g}"
    if "." not in body and "e" not in body:
        body += ".0"
    return body + "f"


def build_header() -> tuple[str, OrderedData]:
    data = build_data()
    lines = [
        "/* Generated by generate_ordered_d1_coeffs.py. */",
        "#ifndef EUROPEAN_ORDERED_D1_COEFFS_H",
        "#define EUROPEAN_ORDERED_D1_COEFFS_H",
        "",
        "#define EUROPEAN_ORDERED_D1_ROWS 128u",
        "#define EUROPEAN_ORDERED_D1_LANES 16u",
        f"#define EUROPEAN_ORDERED_D1_SIGN_ADJUSTED {data.adjusted_sign_fits}u",
        f"#define EUROPEAN_ORDERED_D1_WORST_GAUSS_ERROR {data.worst_gaussian_error:.17g}",
        f"#define EUROPEAN_ORDERED_D1_HARD_RANGE_MIN {HARD_RANGE_MIN}u",
        f"#define EUROPEAN_ORDERED_D1_HARD_POINTS {HARD_POINTS}u",
        "",
    ]
    for name, matrix in (
        ("european_ordered_d1_gauss_c0", data.gauss_c0),
        ("european_ordered_d1_gauss_c1", data.gauss_c1),
    ):
        lines.append(
            f"static const float {name}[128][16] __attribute__((aligned(64))) = {{"
        )
        for row in matrix:
            lines.append("    { " + ", ".join(_fmt_f32(v) for v in row) + " },")
        lines.extend(("};", ""))
    lines.append(
        "static const double european_ordered_d1_moment[10][128][16] "
        "__attribute__((aligned(64))) = {"
    )
    for power in data.moments:
        lines.append("    {")
        for row in power:
            lines.append("        { " + ", ".join(f"{v:.17g}" for v in row) + " },")
        lines.append("    },")
    lines.extend(("};", ""))

    records = hard_records()
    lines.append(
        "static const uint16_t european_ordered_d1_hard_coeff_index[64] "
        "__attribute__((aligned(64))) = {"
    )
    for start in range(0, HARD_POINTS, 16):
        lines.append(
            "    " + ", ".join(
                str(int(record["coeff_index"]))
                for record in records[start:start + 16]
            ) + ","
        )
    lines.extend(("};", ""))

    z_lo: list[np.float32] = []
    z_hi: list[np.float32] = []
    for record in records[:48]:
        raw_range = int(record["raw_range"])
        d0 = raw_range / 4096.0
        d1 = (raw_range + 1) / 4096.0
        if int(record["sign"]):
            u0, u1 = 0.5 - d1, 0.5 - d0
        else:
            u0, u1 = 0.5 + d0, 0.5 + d1
        z_lo.append(np.float32(ndtri(u0)))
        z_hi.append(np.float32(ndtri(u1)))
    for name, values in (
        ("european_ordered_d1_hard_z_lo", z_lo),
        ("european_ordered_d1_hard_z_hi", z_hi),
    ):
        lines.append(
            f"static const float {name}[48] __attribute__((aligned(64))) = {{"
        )
        for start in range(0, 48, 8):
            lines.append("    " + ", ".join(_fmt_f32(v) for v in values[start:start + 8]) + ",")
        lines.extend(("};", ""))

    lines.extend(("#endif", ""))
    return "\n".join(lines), data


def _sobol_word(index: int) -> int:
    gray = index ^ (index >> 1)
    value = 0
    column = 0
    while gray:
        if gray & 1:
            value ^= 1 << (31 - column)
        gray >>= 1
        column += 1
    return value


def hard_records() -> list[dict[str, int | float]]:
    values = ordered_values().reshape(256, 32)
    raw_ranges = np.minimum(
        np.floor(np.abs(values - 0.5) * 4096.0).astype(np.int64), 2047
    )
    records: list[dict[str, int | float]] = []
    for raw_range in range(HARD_RANGE_MIN, 2048):
        locations = np.argwhere(raw_ranges == raw_range)
        if locations.shape != (4, 2):
            raise RuntimeError(
                f"range {raw_range}: expected four hard occurrences, "
                f"got {locations.shape[0]}"
            )
        for packet, lane32 in locations:
            packet_i = int(packet)
            lane_i = int(lane32)
            u = float(values[packet_i, lane_i])
            records.append({
                "raw_range": raw_range,
                "packet": packet_i,
                "lane32": lane_i,
                "coeff_index": (packet_i & 127) * 16 + (lane_i & 15),
                "word": _sobol_word(SKIP + packet_i * 32 + lane_i),
                "sign": 0x80000000 if u < 0.5 else 0,
            })
    if len(records) != HARD_POINTS:
        raise RuntimeError(f"expected {HARD_POINTS} hard records, got {len(records)}")
    return records


def _asm_u32_rows(label: str, values: list[int]) -> list[str]:
    lines = [".align 64", f"{label}:"]
    for start in range(0, len(values), 8):
        lines.append("    .long " + ", ".join(f"0x{v & 0xffffffff:08x}" for v in values[start:start + 8]))
    return lines


def _asm_f32_rows(label: str, values: list[np.float32]) -> list[str]:
    lines = [".align 64", f"{label}:"]
    for start in range(0, len(values), 8):
        lines.append(
            "    .float " + ", ".join(
                _fmt_f32(v)[:-1] for v in values[start:start + 8]
            )
        )
    return lines


def build_tail_asm() -> str:
    records = hard_records()
    coeff = np.zeros((6, HARD_POINTS), dtype=np.float32)
    for lane, record in enumerate(records):
        raw_range = int(record["raw_range"])
        if raw_range == 2047:
            continue
        fit = gaussian.fit_range(raw_range, 2048, 1.0e-5, 5)
        if not fit.passed:
            raise RuntimeError(f"range {raw_range} failed degree-5 tail fit")
        coeff[: fit.coeffs.size, lane] = fit.coeffs

    lines = [
        "# Generated by generate_ordered_d1_coeffs.py.",
        "# Four packed vectors, ordered by raw ranges 2032..2047.",
        "",
    ]
    lines += _asm_u32_rows("ordered_tail_base_words", [int(r["word"]) for r in records])
    lines.append("")
    lines += _asm_u32_rows("ordered_tail_packet_ids", [int(r["packet"]) for r in records])
    lines.append("")
    lines.extend((".align 64", "ordered_tail_active_masks:"))
    for count in range(257):
        packed = 0
        for vector in range(4):
            mask = 0
            for lane, record in enumerate(records[vector * 16:(vector + 1) * 16]):
                if int(record["packet"]) < count:
                    mask |= 1 << lane
            packed |= mask << (16 * vector)
        lines.append(f"    .quad 0x{packed:016x}")
    lines.append("")
    lines += _asm_u32_rows("ordered_tail_sign_bits", [int(r["sign"]) for r in records])
    lines.append("")
    for power in range(6):
        lines += _asm_f32_rows(
            f"ordered_tail_gauss_c{power}",
            [np.float32(v) for v in coeff[power]],
        )
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    args = parse_args()
    text, data = build_header()
    asm_text = build_tail_asm()
    if args.check:
        if not args.out.exists() or args.out.read_text(encoding="ascii") != text:
            raise SystemExit(f"error: {args.out} is not the deterministic generated header")
        if not args.asm_out.exists() or args.asm_out.read_text(encoding="ascii") != asm_text:
            raise SystemExit(f"error: {args.asm_out} is not the deterministic generated include")
        print(f"verified: {args.out}")
        print(f"verified: {args.asm_out}")
    else:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="ascii")
        args.asm_out.parent.mkdir(parents=True, exist_ok=True)
        args.asm_out.write_text(asm_text, encoding="ascii")
        print(f"wrote: {args.out}")
        print(f"wrote: {args.asm_out}")
    print(f"sign-adjusted fits: {data.adjusted_sign_fits}")
    print(f"sign mismatches: {data.sign_mismatches}")
    print(f"worst Gaussian error: {data.worst_gaussian_error:.12g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
