#!/usr/bin/env python3
"""Screen the expanded float P0/P1 ordered-D1 Asian growth proposal.

This is deliberately a numerical preparation report, not a production kernel.
It constructs the universal 144 KiB degree-eight basis, emulates float32 FMA
Horner evaluation, repairs all deterministic hard points optimistically with
the exact reference growth, and then runs the actual chronological Asian S/Q
recurrence for the fixed Joe--Kuo block.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys

import numpy as np
from scipy.special import ndtri

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import generate_ordered_d1_coeffs as ordered


DIRECTIONS = ROOT / "direction_numbers" / "joe_kuo_6_21201.bin"
FIXING_COUNTS = (16, 32, 64, 128, 256)
PATHS = 4096
SOURCE_VALUES = 8192
PRICE_ABS_GATE = 1.0e-4
QUALIFIED_GROWTH_REL_GATE = 3.0e-7
EXP8 = np.asarray(
    [
        1.00000000361,
        0.999999559932,
        0.499999873009,
        0.166670788605,
        0.0416673696717,
        0.00832308095835,
        0.0013875434074,
        0.0002077216867,
        2.58406812172e-05,
    ],
    dtype=np.float64,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def fma32(a: np.ndarray, b: np.ndarray | np.float32, c: np.ndarray) -> np.ndarray:
    """One correctly rounded float32 a*b+c, emulating vfmadd213ps."""
    return np.asarray(
        np.asarray(a, dtype=np.float64) * np.asarray(b, dtype=np.float64)
        + np.asarray(c, dtype=np.float64),
        dtype=np.float32,
    )


def horner32(basis: np.ndarray, alpha: np.float32) -> np.ndarray:
    value = basis[8].copy()
    for coefficient in range(7, -1, -1):
        value = fma32(value, alpha, basis[coefficient])
    return value


def build_basis() -> tuple[np.ndarray, np.ndarray]:
    data = ordered.build_data()
    moments = data.moments
    mean_z = moments[1]
    variance = moments[2] - mean_z * mean_z
    slope = (
        moments[1:10] - mean_z[None, :, :] * moments[0:9]
    ) / variance[None, :, :]
    intercept = moments[0:9] - mean_z[None, :, :] * slope
    weighted_slope = EXP8[:, None, None] * slope
    p1 = np.asarray(
        weighted_slope * data.gauss_c1[None, :, :], dtype=np.float32
    )
    p0 = np.asarray(
        weighted_slope * data.gauss_c0[None, :, :]
        + EXP8[:, None, None] * intercept,
        dtype=np.float32,
    )
    return p0, p1


def sobol_words(indices: np.ndarray, directions: np.ndarray) -> np.ndarray:
    result = np.zeros(indices.shape, dtype=np.uint32)
    gray = indices ^ (indices >> np.uint32(1))
    for bit in range(32):
        result ^= np.where(
            ((gray >> np.uint32(bit)) & np.uint32(1)) != 0,
            directions[bit],
            np.uint32(0),
        )
    return result


def load_directions() -> np.ndarray:
    raw = np.fromfile(DIRECTIONS, dtype=np.uint32)
    if raw.size % 33 != 0:
        raise RuntimeError("invalid Joe--Kuo binary length")
    rows = raw.reshape(-1, 33)
    if np.any(rows[:, 0] != 32):
        raise RuntimeError("invalid Joe--Kuo row header")
    return rows[:, 1:]


def source_route(target: np.ndarray, blocks: tuple[np.ndarray, np.ndarray]) -> tuple[int, np.ndarray]:
    matches: list[tuple[int, np.ndarray]] = []
    for block_index, block in enumerate(blocks):
        lookup = {int(word): index for index, word in enumerate(block)}
        if len(lookup) != PATHS:
            raise RuntimeError("D1 source block is not unique")
        try:
            positions = np.fromiter(
                (lookup[int(word)] for word in target),
                dtype=np.int64,
                count=PATHS,
            )
        except KeyError:
            continue
        if np.unique(positions).size == PATHS:
            matches.append((block_index, positions))
    if len(matches) != 1:
        raise RuntimeError(f"expected one source block match, got {len(matches)}")
    return matches[0]


def candidate_source(
    p0: np.ndarray,
    p1: np.ndarray,
    words: np.ndarray,
    drift: np.float32,
    diffusion: np.float32,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    scale = np.float32(math.exp(float(drift)))
    c0 = np.asarray(horner32(p0, diffusion) * scale, dtype=np.float32)
    c1 = np.asarray(horner32(p1, diffusion) * scale, dtype=np.float32)
    raw = (
        np.uint32(0x3F800000) | (words >> np.uint32(9))
    ).view(np.float32)
    index = np.arange(SOURCE_VALUES)
    rows = (index // 32) & 127
    lanes = (index % 32) & 15
    growth = fma32(c1[rows, lanes], raw, c0[rows, lanes])

    u = (words.astype(np.float64) + 0.5) * 2.0**-32
    exact = np.asarray(
        np.exp(float(drift) + float(diffusion) * ndtri(u)), dtype=np.float32
    )
    raw_range = np.minimum(
        np.floor(np.abs(u - 0.5) * 4096.0).astype(np.int64), 2047
    )
    hard = raw_range >= ordered.HARD_RANGE_MIN
    if int(np.count_nonzero(hard)) != ordered.HARD_POINTS:
        raise RuntimeError("unexpected hard-point count")
    # This is intentionally optimistic: production would use the qualified
    # fixed polynomial repair rather than exact reference values.
    growth[hard] = exact[hard]
    return growth, exact, hard


def fixing_result(
    fixing_count: int,
    p0: np.ndarray,
    p1: np.ndarray,
    directions: np.ndarray,
    source_words: np.ndarray,
) -> dict[str, object]:
    drift = np.float32((0.03 - 0.5 * 0.20 * 0.20) / fixing_count)
    diffusion = np.float32(0.20 / math.sqrt(fixing_count))
    source, exact_source, hard = candidate_source(
        p0, p1, source_words, drift, diffusion
    )
    relative = np.abs(
        source.astype(np.float64) - exact_source.astype(np.float64)
    ) / exact_source.astype(np.float64)

    source_blocks = (source_words[:PATHS], source_words[PATHS:])
    growth_blocks = (source[:PATHS], source[PATHS:])
    target_indices = np.arange(8192, 8192 + PATHS, dtype=np.uint32)
    candidate_s = np.full(PATHS, np.float32(100.0))
    candidate_q = np.zeros(PATHS, dtype=np.float32)
    exact_s = candidate_s.copy()
    exact_q = candidate_q.copy()
    max_s_error = 0.0
    max_q_error = 0.0
    selected_blocks: list[int] = []

    for fixing in range(fixing_count):
        target_words = sobol_words(target_indices, directions[fixing])
        block_index, positions = source_route(target_words, source_blocks)
        selected_blocks.append(block_index)
        candidate_growth = growth_blocks[block_index][positions]
        u = (target_words.astype(np.float64) + 0.5) * 2.0**-32
        exact_growth = np.asarray(
            np.exp(float(drift) + float(diffusion) * ndtri(u)),
            dtype=np.float32,
        )
        candidate_s = np.asarray(candidate_s * candidate_growth, dtype=np.float32)
        candidate_q = np.asarray(candidate_q + candidate_s, dtype=np.float32)
        exact_s = np.asarray(exact_s * exact_growth, dtype=np.float32)
        exact_q = np.asarray(exact_q + exact_s, dtype=np.float32)
        max_s_error = max(
            max_s_error,
            float(np.max(np.abs(candidate_s.astype(np.float64) - exact_s))),
        )
        max_q_error = max(
            max_q_error,
            float(np.max(np.abs(candidate_q.astype(np.float64) - exact_q))),
        )

    candidate_average = candidate_q.astype(np.float64) / fixing_count
    exact_average = exact_q.astype(np.float64) / fixing_count
    discount = math.exp(-0.03)
    candidate_call = discount * np.maximum(candidate_average - 100.0, 0.0).mean()
    exact_call = discount * np.maximum(exact_average - 100.0, 0.0).mean()
    candidate_put = discount * np.maximum(100.0 - candidate_average, 0.0).mean()
    exact_put = discount * np.maximum(100.0 - exact_average, 0.0).mean()
    call_error = float(candidate_call - exact_call)
    put_error = float(candidate_put - exact_put)
    price_gate = abs(call_error) <= PRICE_ABS_GATE and abs(put_error) <= PRICE_ABS_GATE
    growth_gate = float(np.max(relative)) <= QUALIFIED_GROWTH_REL_GATE
    return {
        "fixing_count": fixing_count,
        "drift": float(drift),
        "diffusion": float(diffusion),
        "hard_points_repaired_exactly": int(np.count_nonzero(hard)),
        "growth_max_relative": float(np.max(relative)),
        "growth_rms_relative": float(math.sqrt(np.mean(relative * relative))),
        "ordinary_points_over_growth_gate": int(
            np.count_nonzero(relative > QUALIFIED_GROWTH_REL_GATE)
        ),
        "growth_mean_signed": float(
            np.mean(source.astype(np.float64) - exact_source.astype(np.float64))
        ),
        "growth_gate": growth_gate,
        "chronological_max_s_absolute": max_s_error,
        "chronological_max_q_absolute": max_q_error,
        "call_error": call_error,
        "put_error": put_error,
        "exercise_decision_flips": int(
            np.count_nonzero((candidate_average > 100.0) != (exact_average > 100.0))
        ),
        "price_gate": price_gate,
        "source_block_0_fixings": selected_blocks.count(0),
        "source_block_1_fixings": selected_blocks.count(1),
    }


def main() -> int:
    args = parse_args()
    p0, p1 = build_basis()
    directions = load_directions()
    source_indices = np.arange(8192, 16384, dtype=np.uint32)
    source_words = sobol_words(source_indices, directions[0])
    results = [
        fixing_result(count, p0, p1, directions, source_words)
        for count in FIXING_COUNTS
    ]
    accepted = all(item["growth_gate"] and item["price_gate"] for item in results)
    payload = {
        "status": "PASS" if accepted else "REJECTED_ACCURACY",
        "candidate": "float_jit_degree8_p0_p1_dual_half",
        "scope": "one 8192-value D1 source block; chronological arithmetic Asian",
        "basis": {
            "slots": ordered.PAIRS * ordered.LANES,
            "polynomials": 2,
            "coefficients_per_polynomial": 9,
            "bytes": int(p0.nbytes + p1.nbytes),
            "kib": (p0.nbytes + p1.nbytes) / 1024.0,
            "estimated_vector_fmas_basis": 2048,
            "estimated_vector_fmas_consumption": 512,
        },
        "gates": {
            "qualified_growth_max_relative": QUALIFIED_GROWTH_REL_GATE,
            "price_absolute": PRICE_ABS_GATE,
            "hard_repair": "optimistic exact reference values",
        },
        "results": results,
        "conclusion": (
            "eligible for assembly performance work"
            if accepted
            else "stop before assembly optimization; individual growth and/or price gate failed"
        ),
    }
    text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    print(text, end="")
    if args.output is not None:
        output = args.output if args.output.is_absolute() else ROOT / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
