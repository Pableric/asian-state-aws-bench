#!/usr/bin/env python3
"""
Generate folded inverse-normal coefficient tables for the Sobol Gaussian path.

The intended kernel input is the pre-subtract normalization intermediate:

    m = 1.0 + u
    x = m - 1.5
    d = abs(x)

For each folded range, this script fits z_abs = inv_norm(0.5 + d) as a local
polynomial in t:

    range = floor(d * 4096)
    t = (d - range / 4096) * 4096

The script validates the final float32 coefficients against the actual Sobol
float lattice, excluding the infinite endpoints.
"""

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from numpy.polynomial import Chebyshev, Polynomial
from scipy.special import ndtri
from scipy.stats import qmc


DEFAULT_RANGES = 2048
DEFAULT_TARGET = 1.0e-7
DEFAULT_MAX_DEGREE = 3
DEFAULT_REPORT_MAX_DEGREE = 5
DEFAULT_REPORT_BLOCKS = 1
SOBOL_BLOCK_SIZE = 8192
WORK_CHUNK_SIZE = 4096
ZMM_LANES = 16
TWO_ZMM_LANES = 32
DEFAULT_SKIP_VALUES = 8192
LATTICE_BITS = 23
LATTICE_STEP = 2.0 ** -LATTICE_BITS
HOT_UINT32_COUNT = 64
L1D_BYTES = 32 * 1024


@dataclass
class RangeFit:
    index: int
    degree: int
    error: float
    coeffs: np.ndarray
    count: int
    passed: bool


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Generate Gaussian inverse-CDF coefficients for folded Sobol ranges."
    )
    ap.add_argument("--ranges", type=int, default=DEFAULT_RANGES)
    ap.add_argument("--target", type=float, default=DEFAULT_TARGET)
    ap.add_argument("--max-degree", type=int,
                    help="Maximum polynomial degree. Defaults to 3 for header generation and 5 for report-only mode.")
    ap.add_argument("--out", type=Path, default=Path("gaussian_coeffs_2048.h"))
    ap.add_argument("--report-scheduled-cells", type=Path,
                    help="Write a CSV of post-skip scheduled cells requiring more than 2 FMAs.")
    ap.add_argument("--report-only", action="store_true",
                    help="Only write the scheduled-cell report; do not write the coefficient header.")
    ap.add_argument("--skip-values", type=int, default=DEFAULT_SKIP_VALUES,
                    help="Sobol values to skip before scheduled-cell reporting. Default: 8192")
    ap.add_argument("--block-size", type=int, default=SOBOL_BLOCK_SIZE,
                    help="Scheduled Sobol block size to analyze for reporting. Default: 8192")
    ap.add_argument("--report-blocks", type=int, default=DEFAULT_REPORT_BLOCKS,
                    help="Post-skip scheduled blocks to analyze for reporting. Default: 1")
    ap.add_argument("--report-subranges", type=Path,
                    help="Write a CSV showing whether folded subranges are deterministic by mem_idx.")
    ap.add_argument("--report-split-schedule", type=Path,
                    help="Write focused phase schedule CSV for degree-heavy folded lanes.")
    ap.add_argument("--report-endpoint-strategy", type=Path,
                    help="Write a CSV comparing range-2047 endpoint bundle/sub-subrange fits.")
    ap.add_argument("--emit-split-tail-header", type=Path,
                    help="Read --report-split-schedule CSV and emit a C header for the split-tail kernel.")
    ap.add_argument("--split-min-degree", type=int, default=6,
                    help="Minimum passed degree to include in --report-split-schedule. Unsupported lanes are always included. Default: 6")
    ap.add_argument("--subranges", type=int, default=8,
                    help="Equal-width subranges per folded range for --report-subranges. Default: 8")
    ap.add_argument("--valid-prefix-only", action="store_true", default=True,
                    help="Emit only the consecutive ranges that pass validation.")
    ap.add_argument("--allow-fail", action="store_true",
                    help="Write the best table even if target/L1 validation fails.")
    return ap.parse_args()


def range_lattice_points(range_index: int, ranges: int):
    width = 0.5 / ranges
    lo = range_index * width
    hi = (range_index + 1) * width

    k0 = max(1, int(np.ceil(lo / LATTICE_STEP)))
    k1 = min((1 << (LATTICE_BITS - 1)) - 1, int(np.ceil(hi / LATTICE_STEP)) - 1)
    if k1 < k0:
        return np.array([], dtype=np.float64), np.array([], dtype=np.float64)

    k = np.arange(k0, k1 + 1, dtype=np.float64)
    d = k * LATTICE_STEP
    t = (d - lo) / width
    y = ndtri(0.5 + d)
    return t, y


def eval_power_f64(coeffs: np.ndarray, t: np.ndarray) -> np.ndarray:
    out = np.zeros_like(t, dtype=np.float64)
    for c in coeffs[::-1]:
        out = out * t + float(c)
    return out


def fit_chebyshev_power(t: np.ndarray, y: np.ndarray, degree: int) -> np.ndarray:
    # Chebyshev.fit is numerically stable for constructing a smooth initial
    # polynomial; convert to power basis because that is what the planned kernel
    # will evaluate with FMAs.
    cheb = Chebyshev.fit(t, y, degree, domain=[0.0, 1.0])
    poly = cheb.convert(kind=Polynomial)
    coeffs = np.asarray(poly.coef, dtype=np.float64)
    if coeffs.size < degree + 1:
        coeffs = np.pad(coeffs, (0, degree + 1 - coeffs.size))
    return coeffs[:degree + 1].astype(np.float32)


def validate_coeffs(coeffs: np.ndarray, t: np.ndarray, y: np.ndarray) -> float:
    pred = eval_power_f64(coeffs.astype(np.float32), t)
    return float(np.max(np.abs(y - pred))) if y.size else 0.0


def fit_range(range_index: int, ranges: int, target: float, max_degree: int) -> RangeFit:
    t, y = range_lattice_points(range_index, ranges)
    best = None

    for degree in range(2, max_degree + 1):
        coeffs = fit_chebyshev_power(t, y, degree)
        error = validate_coeffs(coeffs, t, y)
        fit = RangeFit(range_index, degree, error, coeffs, int(t.size), error <= target)
        if best is None or fit.error < best.error:
            best = fit
        if error <= target:
            return fit

    return best


def logical_for_mem(mem_idx: int, block_size: int) -> int:
    if block_size != SOBOL_BLOCK_SIZE:
        raise ValueError("scheduled mapping currently supports only 8192-value blocks")

    in_block = mem_idx % SOBOL_BLOCK_SIZE
    internal_half = in_block // WORK_CHUNK_SIZE
    pos = in_block % WORK_CHUNK_SIZE
    step = pos // TWO_ZMM_LANES
    lane32 = pos % TWO_ZMM_LANES
    lane = lane32 if lane32 < ZMM_LANES else lane32 - ZMM_LANES

    if internal_half == 0:
        if lane32 < ZMM_LANES:
            return step + lane * 256
        return WORK_CHUNK_SIZE + step + lane * 256

    if lane32 < ZMM_LANES:
        return (WORK_CHUNK_SIZE - 1 - step) - lane * 256
    return (SOBOL_BLOCK_SIZE - 1 - step) - lane * 256


def scheduled_values(skip_values: int, block_size: int, report_blocks: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    if skip_values < 0:
        raise ValueError("--skip-values must be >= 0")
    if block_size != SOBOL_BLOCK_SIZE:
        raise ValueError("--block-size must be 8192 for the current AVX-512 schedule")
    if report_blocks <= 0:
        raise ValueError("--report-blocks must be > 0")

    sampler = qmc.Sobol(d=1, scramble=False)
    total_values = report_blocks * block_size
    needed_values = skip_values + total_values
    generated_values = 1 << (needed_values - 1).bit_length()
    u_all = sampler.random_base2(m=generated_values.bit_length() - 1)[:needed_values, 0]
    logical_in_block = np.array([logical_for_mem(i, block_size) for i in range(block_size)], dtype=np.int64)
    block_index = np.repeat(np.arange(report_blocks, dtype=np.int64), block_size)
    mem_idx = np.tile(np.arange(block_size, dtype=np.int64), report_blocks)
    logical = np.tile(logical_in_block, report_blocks)
    absolute_sobol_index = skip_values + block_index * block_size + logical
    values = u_all[absolute_sobol_index]
    return block_index, mem_idx, logical, values


def write_scheduled_cell_report(path: Path, args: argparse.Namespace) -> tuple[list[dict[str, object]], dict[int, set[object]]]:
    block_index, mem_idx, logical, values = scheduled_values(
        args.skip_values,
        args.block_size,
        args.report_blocks,
    )
    raw_ranges = np.minimum(
        np.floor(np.abs(values - 0.5) * (2.0 * args.ranges)).astype(np.int64),
        args.ranges - 1,
    )

    fit_cache: dict[int, RangeFit] = {}
    all_cell_degrees: dict[int, set[object]] = {}
    rows = []
    for block, cell, logical_idx, u, raw_range in zip(block_index, mem_idx, logical, values, raw_ranges):
        raw_range_int = int(raw_range)
        fit = fit_cache.get(raw_range_int)
        if fit is None:
            fit = fit_range(raw_range_int, args.ranges, args.target, args.max_degree)
            fit_cache[raw_range_int] = fit
        cell_int = int(cell)
        degree_label = fit.degree if fit.passed else "unsupported"
        all_cell_degrees.setdefault(cell_int, set()).add(degree_label)
        if fit.degree <= 2 and fit.passed:
            continue

        lane32 = cell_int % TWO_ZMM_LANES
        rows.append({
            "block": int(block),
            "mem_idx": cell_int,
            "abs_sobol_idx": args.skip_values + int(block) * args.block_size + int(logical_idx),
            "logical_idx": int(logical_idx),
            "two_zmm_group": cell_int // TWO_ZMM_LANES,
            "lane32": lane32,
            "zmm_half": "a" if lane32 < ZMM_LANES else "b",
            "lane16": lane32 if lane32 < ZMM_LANES else lane32 - ZMM_LANES,
            "u": f"{float(u):.17g}",
            "side": "negative" if u < 0.5 else "positive",
            "raw_range": raw_range_int,
            "degree": degree_label,
            "fmas": degree_label,
            "max_abs_error": f"{fit.error:.12g}",
        })

    with path.open("w", encoding="ascii", newline="") as f:
        fieldnames = [
            "block",
            "mem_idx",
            "abs_sobol_idx",
            "logical_idx",
            "two_zmm_group",
            "lane32",
            "zmm_half",
            "lane16",
            "u",
            "side",
            "raw_range",
            "degree",
            "fmas",
            "max_abs_error",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    return rows, all_cell_degrees


def print_report_summary(rows: list[dict[str, object]], all_cell_degrees: dict[int, set[object]], path: Path):
    degree_counts: dict[object, int] = {}
    range_counts: dict[int, int] = {}
    for row in rows:
        degree = row["degree"]
        raw_range = int(row["raw_range"])
        degree_counts[degree] = degree_counts.get(degree, 0) + 1
        range_counts[raw_range] = range_counts.get(raw_range, 0) + 1

    print(f"extra-FMA cells      : {len(rows)}")
    print(f"degree counts        : {dict(sorted(degree_counts.items(), key=lambda item: str(item[0])))}")
    print(f"ranges needing extra : {len(range_counts)}")
    varying = {cell: degrees for cell, degrees in all_cell_degrees.items() if len(degrees) > 1}
    print(f"mem cells with varied degree: {len(varying)}")
    if varying:
        preview = ", ".join(
            f"{cell}:{sorted(degrees, key=str)}" for cell, degrees in sorted(varying.items())[:16]
        )
        print(f"first varied cells   : {preview}")
    if range_counts:
        preview = ", ".join(
            f"{raw_range}:{count}" for raw_range, count in sorted(range_counts.items())[:16]
        )
        print(f"first range counts   : {preview}")
    print(f"wrote report         : {path}")


def write_subrange_report(path: Path, args: argparse.Namespace) -> list[dict[str, object]]:
    if args.subranges <= 0:
        raise ValueError("--subranges must be > 0")

    block_index, mem_idx, logical, values = scheduled_values(
        args.skip_values,
        args.block_size,
        args.report_blocks,
    )
    folded = np.abs(values - 0.5) * (2.0 * args.ranges)
    raw_ranges = np.minimum(np.floor(folded).astype(np.int64), args.ranges - 1)
    local = np.clip(folded - raw_ranges.astype(np.float64), 0.0, np.nextafter(1.0, 0.0))
    subranges = np.floor(local * args.subranges).astype(np.int64)

    by_mem: dict[int, dict[str, object]] = {}
    for block, cell, logical_idx, u, raw_range, local_t, subrange in zip(
        block_index, mem_idx, logical, values, raw_ranges, local, subranges
    ):
        cell_int = int(cell)
        entry = by_mem.setdefault(cell_int, {
            "mem_idx": cell_int,
            "logical_idx_first": int(logical_idx),
            "blocks": 0,
            "raw_ranges": set(),
            "subranges": set(),
            "range_subpairs": set(),
            "u_min": float(u),
            "u_max": float(u),
            "local_min": float(local_t),
            "local_max": float(local_t),
            "first_u": float(u),
            "first_raw_range": int(raw_range),
            "first_subrange": int(subrange),
        })
        entry["blocks"] = int(entry["blocks"]) + 1
        entry["raw_ranges"].add(int(raw_range))
        entry["subranges"].add(int(subrange))
        entry["range_subpairs"].add((int(raw_range), int(subrange)))
        entry["u_min"] = min(float(entry["u_min"]), float(u))
        entry["u_max"] = max(float(entry["u_max"]), float(u))
        entry["local_min"] = min(float(entry["local_min"]), float(local_t))
        entry["local_max"] = max(float(entry["local_max"]), float(local_t))

    rows = []
    for entry in by_mem.values():
        raw_range_values = sorted(entry["raw_ranges"])
        subrange_values = sorted(entry["subranges"])
        pair_values = sorted(entry["range_subpairs"])
        rows.append({
            "mem_idx": entry["mem_idx"],
            "logical_idx_first": entry["logical_idx_first"],
            "blocks": entry["blocks"],
            "raw_range_count": len(raw_range_values),
            "subrange_count": len(subrange_values),
            "range_subpair_count": len(pair_values),
            "deterministic_raw_range": int(len(raw_range_values) == 1),
            "deterministic_subrange": int(len(subrange_values) == 1),
            "deterministic_range_subpair": int(len(pair_values) == 1),
            "raw_ranges": " ".join(str(x) for x in raw_range_values),
            "subranges": " ".join(str(x) for x in subrange_values),
            "range_subpairs": " ".join(f"{r}:{s}" for r, s in pair_values),
            "u_min": f"{float(entry['u_min']):.17g}",
            "u_max": f"{float(entry['u_max']):.17g}",
            "local_min": f"{float(entry['local_min']):.17g}",
            "local_max": f"{float(entry['local_max']):.17g}",
            "first_u": f"{float(entry['first_u']):.17g}",
            "first_raw_range": entry["first_raw_range"],
            "first_subrange": entry["first_subrange"],
        })

    rows.sort(key=lambda row: int(row["mem_idx"]))
    with path.open("w", encoding="ascii", newline="") as f:
        fieldnames = [
            "mem_idx", "logical_idx_first", "blocks",
            "raw_range_count", "subrange_count", "range_subpair_count",
            "deterministic_raw_range", "deterministic_subrange", "deterministic_range_subpair",
            "raw_ranges", "subranges", "range_subpairs",
            "u_min", "u_max", "local_min", "local_max",
            "first_u", "first_raw_range", "first_subrange",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return rows


def print_subrange_summary(rows: list[dict[str, object]], path: Path):
    varied_raw = [row for row in rows if int(row["deterministic_raw_range"]) == 0]
    varied_sub = [row for row in rows if int(row["deterministic_subrange"]) == 0]
    varied_pair = [row for row in rows if int(row["deterministic_range_subpair"]) == 0]
    print(f"subrange rows       : {len(rows)}")
    print(f"varied raw ranges   : {len(varied_raw)}")
    print(f"varied subranges    : {len(varied_sub)}")
    print(f"varied range+sub    : {len(varied_pair)}")
    if varied_pair:
        preview = ", ".join(str(row["mem_idx"]) for row in varied_pair[:16])
        print(f"first varied cells  : {preview}")
    print(f"wrote subranges     : {path}")


def period_of(seq: list[int]) -> int:
    for period in range(1, len(seq) + 1):
        if all(value == seq[i % period] for i, value in enumerate(seq)):
            return period
    return len(seq)


def write_split_schedule_report(path: Path, args: argparse.Namespace) -> list[dict[str, object]]:
    if args.subranges <= 0:
        raise ValueError("--subranges must be > 0")
    if args.split_min_degree < 2:
        raise ValueError("--split-min-degree must be >= 2")

    block_index, mem_idx, logical, values = scheduled_values(
        args.skip_values,
        args.block_size,
        args.report_blocks,
    )
    folded = np.abs(values - 0.5) * (2.0 * args.ranges)
    raw_ranges = np.minimum(np.floor(folded).astype(np.int64), args.ranges - 1)
    local = np.clip(folded - raw_ranges.astype(np.float64), 0.0, np.nextafter(1.0, 0.0))
    subranges = np.floor(local * args.subranges).astype(np.int64)

    fit_cache: dict[int, RangeFit] = {}
    mem_degree: dict[int, object] = {}
    mem_range: dict[int, int] = {}
    for cell, raw_range in zip(mem_idx, raw_ranges):
        cell_int = int(cell)
        if cell_int in mem_degree:
            continue
        raw_range_int = int(raw_range)
        fit = fit_cache.get(raw_range_int)
        if fit is None:
            fit = fit_range(raw_range_int, args.ranges, args.target, args.max_degree)
            fit_cache[raw_range_int] = fit
        degree_label: object = fit.degree if fit.passed else "unsupported"
        mem_degree[cell_int] = degree_label
        mem_range[cell_int] = raw_range_int

    selected = sorted(
        cell for cell, degree in mem_degree.items()
        if degree == "unsupported" or int(degree) >= args.split_min_degree
    )

    pattern_ids: dict[tuple[int, ...], int] = {}
    rows: list[dict[str, object]] = []
    for cell in selected:
        seq = [int(subranges[block * args.block_size + cell]) for block in range(args.report_blocks)]
        period = period_of(seq)
        pattern = tuple(seq[:period])
        pattern_id = pattern_ids.setdefault(pattern, len(pattern_ids))
        lane32 = cell % TWO_ZMM_LANES
        rows.append({
            "mem_idx": cell,
            "raw_range": mem_range[cell],
            "degree": mem_degree[cell],
            "period": period,
            "pattern_id": pattern_id,
            "two_zmm_group": cell // TWO_ZMM_LANES,
            "lane32": lane32,
            "zmm_half": "a" if lane32 < ZMM_LANES else "b",
            "lane16": lane32 if lane32 < ZMM_LANES else lane32 - ZMM_LANES,
            "subrange_sequence": " ".join(str(v) for v in seq),
            "period_sequence": " ".join(str(v) for v in pattern),
        })

    with path.open("w", encoding="ascii", newline="") as f:
        fieldnames = [
            "mem_idx",
            "raw_range",
            "degree",
            "period",
            "pattern_id",
            "two_zmm_group",
            "lane32",
            "zmm_half",
            "lane16",
            "subrange_sequence",
            "period_sequence",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return rows


def print_split_schedule_summary(rows: list[dict[str, object]], args: argparse.Namespace, path: Path):
    degree_counts: dict[object, int] = {}
    range_counts: dict[int, int] = {}
    period_counts: dict[int, int] = {}
    pattern_ids = set()
    for row in rows:
        degree = row["degree"]
        degree_counts[degree] = degree_counts.get(degree, 0) + 1
        raw_range = int(row["raw_range"])
        range_counts[raw_range] = range_counts.get(raw_range, 0) + 1
        period = int(row["period"])
        period_counts[period] = period_counts.get(period, 0) + 1
        pattern_ids.add(int(row["pattern_id"]))

    max_degree = 0
    for degree in degree_counts:
        if degree != "unsupported":
            max_degree = max(max_degree, int(degree))
    coeffs_per_range = max_degree + 1 if max_degree else args.max_degree + 1
    schedule_bytes = args.report_blocks * len(rows)
    coeff_bytes = len(range_counts) * args.subranges * coeffs_per_range * 4

    print(f"split lanes/block   : {len(rows)}")
    print(f"split min degree    : {args.split_min_degree}")
    print(f"subranges           : {args.subranges}")
    print(f"degree counts       : {dict(sorted(degree_counts.items(), key=lambda item: str(item[0])))}")
    print(f"raw ranges          : {len(range_counts)}")
    print(f"movement patterns   : {len(pattern_ids)}")
    print(f"period counts       : {dict(sorted(period_counts.items()))}")
    print(f"schedule bytes      : {schedule_bytes}")
    print(f"coeff bytes est     : {coeff_bytes}")
    print(f"hot bytes est       : {schedule_bytes + coeff_bytes}")
    print(f"wrote split schedule: {path}")


def table_bytes(fits: list[RangeFit]) -> int:
    # Prefix layout: degree-2 base coefficients for every valid range, a degree
    # byte and sparse cubic offset per range, and cubic coefficients only for
    # valid ranges that actually require degree 3.
    ranges = len(fits)
    base = ranges * 3 * 4
    cubic = sum(1 for fit in fits if fit.degree > 2) * 4
    metadata = ranges * (1 + 2)
    return base + cubic + metadata


def format_float32(value: np.float32) -> str:
    return f"{float(value):.9g}f"


def write_header(path: Path, fits: list[RangeFit], ranges: int, target: float):
    max_degree = max(fit.degree for fit in fits)
    worst = max(fits, key=lambda fit: fit.error)
    valid_ranges = len(fits)
    degrees = []
    cubic_offsets = []
    cubic = []

    for fit in fits:
        degrees.append(fit.degree)
        cubic_offsets.append(len(cubic))
        if fit.degree > 2:
            cubic.append(fit.coeffs[3])

    with path.open("w", encoding="ascii") as f:
        f.write("/* Generated by generate_gaussian_coeffs.py. */\n")
        f.write("#ifndef GAUSSIAN_COEFFS_2048_H\n")
        f.write("#define GAUSSIAN_COEFFS_2048_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define GAUSS_RANGE_COUNT {ranges}\n")
        f.write(f"#define GAUSS_VALID_RANGE_COUNT {valid_ranges}\n")
        f.write(f"#define GAUSS_FIRST_UNSUPPORTED_RANGE {valid_ranges}\n")
        f.write(f"#define GAUSS_RANGE_SCALE {2 * ranges}.0f\n")
        f.write("#define GAUSS_CENTER_PRE_SUB 1.5f\n")
        f.write(f"#define GAUSS_TARGET_ABS_ERR {target:.9g}f\n")
        f.write(f"#define GAUSS_MAX_DEGREE {max_degree}\n")
        f.write(f"#define GAUSS_WORST_RANGE {worst.index}\n")
        f.write(f"#define GAUSS_WORST_ABS_ERR {worst.error:.9g}f\n\n")

        f.write("static const float gauss_coeff2[GAUSS_VALID_RANGE_COUNT][3] __attribute__((aligned(64))) = {\n")
        for fit in fits:
            c = np.pad(fit.coeffs[:3], (0, max(0, 3 - fit.coeffs[:3].size))).astype(np.float32)
            f.write("    { " + ", ".join(format_float32(v) for v in c) + " },\n")
        f.write("};\n\n")

        f.write("static const uint8_t gauss_degree[GAUSS_VALID_RANGE_COUNT] __attribute__((aligned(64))) = {\n")
        for i in range(0, valid_ranges, 16):
            f.write("    " + ", ".join(str(v) for v in degrees[i:i + 16]) + ",\n")
        f.write("};\n\n")

        f.write("static const uint16_t gauss_cubic_offset[GAUSS_VALID_RANGE_COUNT] __attribute__((aligned(64))) = {\n")
        for i in range(0, valid_ranges, 12):
            f.write("    " + ", ".join(str(v) for v in cubic_offsets[i:i + 12]) + ",\n")
        f.write("};\n\n")

        f.write("static const float gauss_cubic_coeff[] __attribute__((aligned(64))) = {\n")
        for i in range(0, len(cubic), 8):
            f.write("    " + ", ".join(format_float32(v) for v in cubic[i:i + 8]) + ",\n")
        f.write("};\n\n")
        f.write("#endif\n")


def parse_int_sequence(text: str) -> list[int]:
    return [int(x) for x in text.split()] if text else []


def fit_interval(range_index: int, start: int, stop: int, local_splits: int, ranges: int, degree: int) -> tuple[int, float]:
    width = 0.5 / ranges
    lo = range_index * width
    sub_lo = lo + start * width / local_splits
    sub_hi = lo + stop * width / local_splits

    k0 = max(1, int(np.ceil(sub_lo / LATTICE_STEP)))
    k1 = min((1 << (LATTICE_BITS - 1)) - 1, int(np.ceil(sub_hi / LATTICE_STEP)) - 1)
    if k1 < k0:
        return 0, 0.0

    k = np.arange(k0, k1 + 1, dtype=np.float64)
    d = k * LATTICE_STEP
    t = (d - sub_lo) / (sub_hi - sub_lo)
    y = ndtri(0.5 + d)
    coeffs = fit_chebyshev_power(t, y, degree)
    return int(k.size), validate_coeffs(coeffs, t, y)


def write_endpoint_strategy_report(path: Path, args: argparse.Namespace) -> list[dict[str, object]]:
    range_index = args.ranges - 1
    candidates = [
        ("bundle_0_7", 0, 8, 16, range(2, 13)),
        ("bundle_8_11", 8, 12, 16, range(2, 13)),
        ("bundle_12_13", 12, 14, 16, range(2, 13)),
        ("bundle_14_15", 14, 16, 16, range(2, 13)),
    ]
    for idx in range(16):
        candidates.append((f"subsub_14_15_{idx}", 14 * 16 + idx, 14 * 16 + idx + 1, 16 * 16, range(2, 11)))

    rows: list[dict[str, object]] = []
    for name, start, stop, splits, degrees in candidates:
        best_degree = None
        best_error = None
        best_points = 0
        for degree in degrees:
            points, error = fit_interval(range_index, start, stop, splits, args.ranges, degree)
            if best_error is None or error < best_error:
                best_error = error
            if best_degree is None and error <= args.target:
                best_degree = degree
            best_points = points
        rows.append({
            "name": name,
            "range": range_index,
            "local_start": int(start * ((1 << (LATTICE_BITS - 1)) // args.ranges) // splits),
            "local_stop_exclusive": int(stop * ((1 << (LATTICE_BITS - 1)) // args.ranges) // splits),
            "points": best_points,
            "target": args.target,
            "best_pass_degree": "" if best_degree is None else best_degree,
            "best_error_seen": best_error,
            "selected": int(
                name in ("bundle_0_7", "bundle_8_11", "bundle_12_13") and best_degree is not None
            ),
            "fallback_exact_lut": int(best_degree is None or name.startswith("bundle_14_15") or name.startswith("subsub_14_15_")),
        })

    with path.open("w", newline="", encoding="ascii") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "name", "range", "local_start", "local_stop_exclusive", "points",
            "target", "best_pass_degree", "best_error_seen", "selected",
            "fallback_exact_lut",
        ])
        writer.writeheader()
        writer.writerows(rows)
    return rows


def print_endpoint_strategy_summary(rows: list[dict[str, object]], path: Path):
    selected = [row for row in rows if int(row["selected"]) != 0]
    fallback = [row for row in rows if int(row["fallback_exact_lut"]) != 0]
    print(f"endpoint candidates : {len(rows)}")
    print(f"selected polynomials: {len(selected)}")
    print(f"fallback entries    : {len(fallback)}")
    print(f"wrote endpoint report: {path}")


def fit_subrange(range_index: int, subrange: int, ranges: int, subranges: int, degree: int) -> tuple[np.ndarray, float]:
    width = 0.5 / ranges
    lo = range_index * width
    sub_lo = lo + subrange * width / subranges
    sub_hi = lo + (subrange + 1) * width / subranges

    k0 = max(1, int(np.ceil(sub_lo / LATTICE_STEP)))
    k1 = min((1 << (LATTICE_BITS - 1)) - 1, int(np.ceil(sub_hi / LATTICE_STEP)) - 1)
    if k1 < k0:
        coeffs = np.zeros(degree + 1, dtype=np.float32)
        return coeffs, 0.0

    k = np.arange(k0, k1 + 1, dtype=np.float64)
    d = k * LATTICE_STEP
    t = (d - sub_lo) / (sub_hi - sub_lo)
    y = ndtri(0.5 + d)
    coeffs = fit_chebyshev_power(t, y, degree)
    error = validate_coeffs(coeffs, t, y)
    if coeffs.size < degree + 1:
        coeffs = np.pad(coeffs, (0, degree + 1 - coeffs.size))
    return coeffs[:degree + 1].astype(np.float32), error


def write_split_tail_header(path: Path, split_csv: Path, args: argparse.Namespace):
    if split_csv is None:
        raise SystemExit("error: --emit-split-tail-header requires --report-split-schedule CSV")
    if args.subranges != 16:
        raise SystemExit("error: split-tail header currently expects --subranges 16")

    with split_csv.open(newline="", encoding="ascii") as f:
        rows = list(csv.DictReader(f))
    if len(rows) != 88:
        raise SystemExit(f"error: expected 88 split-tail rows, got {len(rows)}")

    raw_ranges = sorted({int(row["raw_range"]) for row in rows})
    raw_slot = {raw_range: i for i, raw_range in enumerate(raw_ranges)}
    patterns: dict[int, list[int]] = {}
    for row in rows:
        pattern_id = int(row["pattern_id"])
        seq = parse_int_sequence(row["subrange_sequence"])
        if len(seq) != args.report_blocks:
            raise SystemExit(f"error: pattern {pattern_id} has {len(seq)} phases, expected {args.report_blocks}")
        existing = patterns.get(pattern_id)
        if existing is None:
            patterns[pattern_id] = seq
        elif existing != seq:
            raise SystemExit(f"error: inconsistent sequence for pattern {pattern_id}")
    if sorted(patterns) != [0, 1, 2, 3]:
        raise SystemExit(f"error: expected pattern ids 0..3, got {sorted(patterns)}")

    degree = 7
    relaxed_target = max(args.target, 1.25e-7)
    coeffs: dict[tuple[int, int], np.ndarray] = {}
    errors: dict[tuple[int, int], float] = {}
    fallback: dict[tuple[int, int], int] = {}
    for raw_range in raw_ranges:
        for subrange in range(args.subranges):
            c, err = fit_subrange(raw_range, subrange, args.ranges, args.subranges, degree)
            coeffs[(raw_range, subrange)] = c
            errors[(raw_range, subrange)] = err
            fallback[(raw_range, subrange)] = int(err > relaxed_target or (raw_range == 2047 and subrange >= 14))

    worst_poly = max(errors.items(), key=lambda item: item[1])
    fallback_count = sum(fallback.values())

    rows.sort(key=lambda row: int(row["mem_idx"]))
    with path.open("w", encoding="ascii") as f:
        f.write("/* Generated by generate_gaussian_coeffs.py. */\n")
        f.write("#ifndef GAUSSIAN_SPLIT_TAIL_2048_H\n")
        f.write("#define GAUSSIAN_SPLIT_TAIL_2048_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("#define GAUSS_SPLIT_TAIL_LANES 88u\n")
        f.write("#define GAUSS_SPLIT_TAIL_PATTERNS 4u\n")
        f.write("#define GAUSS_SPLIT_TAIL_PHASES 128u\n")
        f.write("#define GAUSS_SPLIT_TAIL_RANGES 22u\n")
        f.write("#define GAUSS_SPLIT_TAIL_SUBRANGES 16u\n")
        f.write("#define GAUSS_SPLIT_TAIL_DEGREE 7u\n")
        f.write("#define GAUSS_SPLIT_TAIL_COEFFS 8u\n")
        f.write(f"#define GAUSS_SPLIT_TAIL_RELAXED_TARGET {relaxed_target:.9g}f\n")
        f.write(f"#define GAUSS_SPLIT_TAIL_FALLBACK_PAIRS {fallback_count}u\n")
        f.write(f"#define GAUSS_SPLIT_TAIL_WORST_RAW_RANGE {worst_poly[0][0]}u\n")
        f.write(f"#define GAUSS_SPLIT_TAIL_WORST_SUBRANGE {worst_poly[0][1]}u\n")
        f.write(f"#define GAUSS_SPLIT_TAIL_WORST_ERROR {worst_poly[1]:.9g}f\n\n")

        f.write("typedef struct {\n")
        f.write("    uint16_t mem_idx;\n")
        f.write("    uint8_t raw_slot;\n")
        f.write("    uint8_t pattern_id;\n")
        f.write("    uint8_t lane32;\n")
        f.write("    uint8_t zmm_half;\n")
        f.write("    uint8_t lane16;\n")
        f.write("    uint8_t degree;\n")
        f.write("} gauss_split_tail_lane_desc_t;\n\n")

        f.write("static const uint16_t gauss_split_tail_raw_ranges[GAUSS_SPLIT_TAIL_RANGES] __attribute__((aligned(64))) = {\n    ")
        f.write(", ".join(str(v) for v in raw_ranges))
        f.write("\n};\n\n")

        f.write("static const uint32_t gauss_split_tail_pattern[GAUSS_SPLIT_TAIL_PATTERNS][GAUSS_SPLIT_TAIL_PHASES] __attribute__((aligned(64))) = {\n")
        for pattern_id in range(4):
            f.write("    { ")
            f.write(", ".join(str(v) for v in patterns[pattern_id]))
            f.write(" },\n")
        f.write("};\n\n")

        f.write("static const gauss_split_tail_lane_desc_t gauss_split_tail_lane_desc[GAUSS_SPLIT_TAIL_LANES] __attribute__((aligned(64))) = {\n")
        for row in rows:
            degree_label = row["degree"]
            degree_value = 0 if degree_label == "unsupported" else int(degree_label)
            f.write(
                "    { %su, %su, %su, %su, %su, %su, %su },\n" % (
                    row["mem_idx"],
                    raw_slot[int(row["raw_range"])],
                    row["pattern_id"],
                    row["lane32"],
                    0 if row["zmm_half"] == "a" else 1,
                    row["lane16"],
                    degree_value,
                )
            )
        f.write("};\n\n")

        f.write("static const uint32_t gauss_split_tail_fallback_mask[GAUSS_SPLIT_TAIL_RANGES][GAUSS_SPLIT_TAIL_SUBRANGES] __attribute__((aligned(64))) = {\n")
        for raw_range in raw_ranges:
            f.write("    { ")
            f.write(", ".join(str(fallback[(raw_range, sub)]) + "u" for sub in range(args.subranges)))
            f.write(" },\n")
        f.write("};\n\n")

        f.write("static const float gauss_split_tail_coeff[GAUSS_SPLIT_TAIL_COEFFS][GAUSS_SPLIT_TAIL_RANGES][GAUSS_SPLIT_TAIL_SUBRANGES] __attribute__((aligned(64))) = {\n")
        for coeff_idx in range(degree + 1):
            f.write("    {\n")
            for raw_range in raw_ranges:
                f.write("        { ")
                f.write(", ".join(format_float32(coeffs[(raw_range, sub)][coeff_idx]) for sub in range(args.subranges)))
                f.write(" },\n")
            f.write("    },\n")
        f.write("};\n\n")
        f.write("#endif\n")


def main() -> int:
    args = parse_args()
    if args.ranges <= 0:
        raise SystemExit("error: --ranges must be > 0")
    if args.target <= 0:
        raise SystemExit("error: --target must be > 0")
    if args.report_only and not args.report_scheduled_cells and not args.report_subranges and not args.report_split_schedule and not args.report_endpoint_strategy and not args.emit_split_tail_header:
        raise SystemExit("error: --report-only requires --report-scheduled-cells, --report-subranges, --report-split-schedule, --report-endpoint-strategy, or --emit-split-tail-header")
    if args.max_degree is None:
        args.max_degree = DEFAULT_REPORT_MAX_DEGREE
        if not args.report_only:
            args.max_degree = DEFAULT_MAX_DEGREE
    if args.max_degree < 2:
        raise SystemExit("error: --max-degree must be >= 2")
    if not args.report_only and args.max_degree > 3:
        raise SystemExit("error: header generation supports --max-degree <= 3; use --report-only for higher degrees")

    if args.report_scheduled_cells:
        rows, all_cell_degrees = write_scheduled_cell_report(args.report_scheduled_cells, args)
        print_report_summary(rows, all_cell_degrees, args.report_scheduled_cells)

    if args.report_subranges:
        rows = write_subrange_report(args.report_subranges, args)
        print_subrange_summary(rows, args.report_subranges)

    if args.report_split_schedule:
        rows = write_split_schedule_report(args.report_split_schedule, args)
        print_split_schedule_summary(rows, args, args.report_split_schedule)

    if args.report_endpoint_strategy:
        rows = write_endpoint_strategy_report(args.report_endpoint_strategy, args)
        print_endpoint_strategy_summary(rows, args.report_endpoint_strategy)

    if args.emit_split_tail_header:
        write_split_tail_header(args.emit_split_tail_header, args.report_split_schedule, args)

    if args.report_only:
        return 0

    fits = []
    first_unsupported = args.ranges
    for i in range(args.ranges):
        fit = fit_range(i, args.ranges, args.target, args.max_degree)
        if args.valid_prefix_only and fit.error > args.target:
            first_unsupported = i
            break
        fits.append(fit)

    if not fits:
        print("error: no valid ranges generated")
        return 1

    worst = max(fits, key=lambda fit: fit.error)
    failing = [fit for fit in fits if fit.error > args.target]
    max_degree = max(fit.degree for fit in fits)
    coeff_bytes = table_bytes(fits)
    hot_bytes = coeff_bytes + HOT_UINT32_COUNT * 4
    degree2_count = sum(1 for fit in fits if fit.degree == 2)
    degree3_count = sum(1 for fit in fits if fit.degree == 3)

    print(f"ranges              : {args.ranges}")
    print(f"valid prefix ranges : {len(fits)}")
    print(f"first unsupported   : {first_unsupported}")
    print(f"target abs error    : {args.target:.9g}")
    print(f"max degree required : {max_degree}")
    print(f"degree-2 ranges     : {degree2_count}")
    print(f"degree-3 ranges     : {degree3_count}")
    print(f"coefficient bytes   : {coeff_bytes}")
    print(f"hot bytes + 64 u32  : {hot_bytes}")
    print(f"worst range         : {worst.index}")
    print(f"worst error         : {worst.error:.12g}")
    print(f"failing ranges      : {len(failing)}")
    if failing:
        preview = ", ".join(f"{fit.index}:{fit.error:.3g}" for fit in failing[:12])
        print(f"first failures      : {preview}")

    if (failing or hot_bytes > L1D_BYTES) and not args.allow_fail:
        if hot_bytes > L1D_BYTES:
            print(f"error: hot table size exceeds {L1D_BYTES} bytes")
        if failing:
            print("error: generated prefix contains failing ranges")
        return 1

    write_header(args.out, fits, args.ranges, args.target)
    print(f"wrote               : {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
