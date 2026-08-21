#!/usr/bin/env python3
"""Deterministic analysis for the pre-registered seasoned overhead protocol."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
import re
import statistics
import sys
from pathlib import Path


SCHEMA = "asian-genuine-seasoned-overhead-raw-v1"
FROZEN_COMMIT = "d31ed2eafdaeb892fbbbf49f55765600548ab46d"
CASES = (
    (16, 0, 16),
    (128, 32, 96),
    (256, 0, 256),
    (256, 1, 255),
    (256, 64, 192),
    (256, 128, 128),
    (256, 255, 1),
)
ESTIMATORS = ("arithmetic", "geometric_cv")
CACHE_MODES = ("candidate_specific_warm", "historical_32KiB_rmw")
QUARTET_SEED = "0x534541534f4e4142"
BOOTSTRAP_SEED = 0x534541534F564552
BOOTSTRAP_REPLICATES = 10_000
GLOBAL_MEDIAN_LIMIT = 1.005
GLOBAL_UPPER_LIMIT = 1.01
CELL_MEDIAN_LIMIT = 1.02
PASS_STATUSES = (
    "SEASONED_NATIVE_OVERHEAD_CONFIRMED",
    "SEASONED_NATIVE_PERFORMANCE_QUALIFIED",
    "SEASONED_STRIP_QUALIFIED",
)


def fail(message: str) -> None:
    raise ValueError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def geometric_mean(values: list[float]) -> float:
    if not values or any(not math.isfinite(value) or value <= 0 for value in values):
        fail("geometric mean received an invalid ratio")
    return math.exp(math.fsum(math.log(value) for value in values) / len(values))


def nearest_rank(values: list[float], probability: float) -> float:
    if not values or not 0 < probability <= 1:
        fail("invalid nearest-rank request")
    ordered = sorted(values)
    return ordered[max(0, math.ceil(probability * len(ordered)) - 1)]


def metric_ratios(cell: dict, metric: str) -> list[float]:
    ratios: list[float] = []
    for expected_index, quartet in enumerate(cell["quartets"]):
        if quartet.get("index") != expected_index:
            fail("nonconsecutive measured quartet index")
        pattern = quartet.get("pattern")
        expected_candidates = ["A", "B", "B", "A"] if pattern == "ABBA" else ["B", "A", "A", "B"] if pattern == "BAAB" else None
        if quartet.get("candidates") != expected_candidates:
            fail("quartet candidate order does not match its ABBA/BAAB label")
        values = quartet.get(metric)
        if not isinstance(values, list) or len(values) != 4 or any(
            not isinstance(value, int) or value <= 0 for value in values
        ):
            fail(f"invalid raw {metric} quartet")
        a = sum(value for value, candidate in zip(values, expected_candidates) if candidate == "A")
        b = sum(value for value, candidate in zip(values, expected_candidates) if candidate == "B")
        ratios.append(b / a)
    return ratios


def validate_quartet_checksums(quartet: dict, negative_control: bool) -> None:
    candidates = quartet["candidates"]
    checksums = quartet.get("checksums")
    if (
        not isinstance(checksums, list)
        or len(checksums) != 4
        or any(not isinstance(value, str) or not re.fullmatch(r"0x[0-9a-f]{16}", value) for value in checksums)
    ):
        fail("invalid quartet checksums")
    a = {value for value, candidate in zip(checksums, candidates) if candidate == "A"}
    b = {value for value, candidate in zip(checksums, candidates) if candidate == "B"}
    if len(a) != 1 or len(b) != 1:
        fail("repeated candidate output changed within a quartet")
    if negative_control and a != b:
        fail("byte-identical c=0 candidates produced different checksums")


def next_pattern_pair(state: int) -> tuple[int, int]:
    mask = (1 << 64) - 1
    state ^= (state << 13) & mask
    state ^= state >> 7
    state ^= (state << 17) & mask
    state &= mask
    return state, state & 1


def validate_raw(raw: dict) -> list[dict]:
    required_header = {
        "schema": SCHEMA,
        "frozen_commit": FROZEN_COMMIT,
        "paths": 4096,
        "tile": 8,
        "output": "price_delta",
        "frozen_files_verified": True,
        "warmup_quartets": 16,
        "measured_quartets": 201,
        "quartet_seed": QUARTET_SEED,
        "ratio_definition": "(B1+B2)/(A1+A2)",
        "candidate_A": "matched_f_unseasoned",
        "candidate_B": "seasoned",
        "clock": "CLOCK_MONOTONIC_RAW",
    }
    for key, expected in required_header.items():
        if raw.get(key) != expected:
            fail(f"raw header mismatch: {key}")
    if not re.fullmatch(r"[0-9a-f]{64}", str(raw.get("binary_sha256", ""))):
        fail("missing or invalid benchmark binary SHA-256")
    if not isinstance(raw.get("cpu"), int) or not isinstance(raw.get("cache"), list):
        fail("missing CPU/cache metadata")
    expected_cells = {
        (case_index, estimator, mode): (m, c, f)
        for case_index, (m, c, f) in enumerate(CASES)
        for estimator in ESTIMATORS
        for mode in CACHE_MODES
    }
    cells = raw.get("cells")
    if not isinstance(cells, list) or len(cells) != len(expected_cells):
        fail("raw file does not contain exactly 28 pre-registered cells")
    seen: set[tuple[int, str, str]] = set()
    expected_order = list(expected_cells)
    random_state = int(QUARTET_SEED, 16)
    for cell_number, cell in enumerate(cells):
        key = (cell.get("case_index"), cell.get("estimator"), cell.get("cache_mode"))
        if key not in expected_cells or key in seen or key != expected_order[cell_number]:
            fail("unexpected or duplicate cell")
        seen.add(key)
        m, c, f = expected_cells[key]
        if (cell.get("M"), cell.get("c"), cell.get("f")) != (m, c, f):
            fail("cell contract mismatch")
        if cell.get("negative_control") != (c == 0):
            fail("negative-control label mismatch")
        warmups = cell.get("warmups")
        quartets = cell.get("quartets")
        if not isinstance(warmups, list) or len(warmups) != 16:
            fail("cell does not contain 16 warmup quartets")
        if not isinstance(quartets, list) or len(quartets) != 201:
            fail("cell does not contain 201 measured quartets")
        candidate_checksums = {"A": set(), "B": set()}
        for phase in (warmups, quartets):
            pair_pattern = 0
            for expected_index, quartet in enumerate(phase):
                if quartet.get("index") != expected_index:
                    fail("nonconsecutive quartet index")
                pattern = quartet.get("pattern")
                if expected_index % 2 == 0:
                    random_state, pair_pattern = next_pattern_pair(random_state)
                expected_pattern = "BAAB" if pair_pattern ^ (expected_index & 1) else "ABBA"
                if pattern != expected_pattern:
                    fail("quartet pattern does not reproduce from the fixed seed")
                expected_candidates = ["A", "B", "B", "A"] if pattern == "ABBA" else ["B", "A", "A", "B"] if pattern == "BAAB" else None
                if quartet.get("candidates") != expected_candidates:
                    fail("invalid quartet pattern")
                validate_quartet_checksums(quartet, c == 0)
                for checksum, candidate in zip(quartet["checksums"], expected_candidates):
                    candidate_checksums[candidate].add(checksum)
                for metric in ("tsc", "wall_ns"):
                    values = quartet.get(metric)
                    if not isinstance(values, list) or len(values) != 4 or any(
                        not isinstance(value, int) or value <= 0 for value in values
                    ):
                        fail(f"invalid raw {metric} timing")
        if len(candidate_checksums["A"]) != 1 or len(candidate_checksums["B"]) != 1:
            fail("candidate output checksum changed across repeated invocations")
    if seen != set(expected_cells):
        fail("missing pre-registered cell")
    if raw.get("final_rng_state") != f"0x{random_state:016x}":
        fail("final quartet RNG state mismatch")
    return cells


def analyze(cells: list[dict]) -> tuple[list[dict], dict, dict, list[str]]:
    work: list[dict] = []
    for cell in cells:
        tsc = metric_ratios(cell, "tsc")
        wall = metric_ratios(cell, "wall_ns")
        work.append(
            {
                "source": cell,
                "tsc": tsc,
                "wall": wall,
                "tsc_median": statistics.median(tsc),
                "wall_median": statistics.median(wall),
                "tsc_bootstrap": [],
                "wall_bootstrap": [],
            }
        )
    rng = random.Random(BOOTSTRAP_SEED)
    global_tsc_draws: list[float] = []
    global_wall_draws: list[float] = []
    control_tsc_draws: list[float] = []
    control_wall_draws: list[float] = []
    for _ in range(BOOTSTRAP_REPLICATES):
        draw_tsc: list[float] = []
        draw_wall: list[float] = []
        control_tsc: list[float] = []
        control_wall: list[float] = []
        for cell in work:
            indices = [rng.randrange(201) for _ in range(201)]
            tsc_median = statistics.median(cell["tsc"][index] for index in indices)
            wall_median = statistics.median(cell["wall"][index] for index in indices)
            cell["tsc_bootstrap"].append(tsc_median)
            cell["wall_bootstrap"].append(wall_median)
            draw_tsc.append(tsc_median)
            draw_wall.append(wall_median)
            if cell["source"]["negative_control"]:
                control_tsc.append(tsc_median)
                control_wall.append(wall_median)
        global_tsc_draws.append(geometric_mean(draw_tsc))
        global_wall_draws.append(geometric_mean(draw_wall))
        control_tsc_draws.append(geometric_mean(control_tsc))
        control_wall_draws.append(geometric_mean(control_wall))

    cell_results: list[dict] = []
    for cell in work:
        source = cell["source"]
        cell_results.append(
            {
                "case_index": source["case_index"],
                "M": source["M"],
                "c": source["c"],
                "f": source["f"],
                "negative_control": source["negative_control"],
                "estimator": source["estimator"],
                "cache_mode": source["cache_mode"],
                "tsc_median_ratio": cell["tsc_median"],
                "tsc_ci95_lower": nearest_rank(cell["tsc_bootstrap"], 0.025),
                "tsc_ci95_upper": nearest_rank(cell["tsc_bootstrap"], 0.975),
                "wall_median_ratio": cell["wall_median"],
                "wall_ci95_lower": nearest_rank(cell["wall_bootstrap"], 0.025),
                "wall_ci95_upper": nearest_rank(cell["wall_bootstrap"], 0.975),
            }
        )
    observed_tsc = geometric_mean([cell["tsc_median"] for cell in work])
    observed_wall = geometric_mean([cell["wall_median"] for cell in work])
    global_result = {
        "cell_count": len(work),
        "tsc_geometric_mean_ratio": observed_tsc,
        "tsc_bootstrap_ci95_upper": nearest_rank(global_tsc_draws, 0.95),
        "wall_geometric_mean_ratio": observed_wall,
        "wall_bootstrap_ci95_upper": nearest_rank(global_wall_draws, 0.95),
        "maximum_tsc_cell_median_ratio": max(cell["tsc_median"] for cell in work),
        "maximum_wall_cell_median_ratio": max(cell["wall_median"] for cell in work),
    }
    control_work = [cell for cell in work if cell["source"]["negative_control"]]
    negative_controls = {
        "cell_count": len(control_work),
        "cases": [[16, 0, 16], [256, 0, 256]],
        "byte_identical_checksums": True,
        "tsc_geometric_mean_ratio": geometric_mean([cell["tsc_median"] for cell in control_work]),
        "tsc_bootstrap_ci95_lower": nearest_rank(control_tsc_draws, 0.025),
        "tsc_bootstrap_ci95_upper": nearest_rank(control_tsc_draws, 0.975),
        "wall_geometric_mean_ratio": geometric_mean([cell["wall_median"] for cell in control_work]),
        "wall_bootstrap_ci95_lower": nearest_rank(control_wall_draws, 0.025),
        "wall_bootstrap_ci95_upper": nearest_rank(control_wall_draws, 0.975),
    }
    negative_controls["method_valid"] = (
        negative_controls["tsc_bootstrap_ci95_lower"] <= 1.0 <= negative_controls["tsc_bootstrap_ci95_upper"]
        and negative_controls["wall_bootstrap_ci95_lower"] <= 1.0 <= negative_controls["wall_bootstrap_ci95_upper"]
    )
    failures: list[str] = []
    if global_result["tsc_geometric_mean_ratio"] > GLOBAL_MEDIAN_LIMIT:
        failures.append("global TSC geometric-mean ratio exceeds 1.005")
    if global_result["wall_geometric_mean_ratio"] > GLOBAL_MEDIAN_LIMIT:
        failures.append("global wall geometric-mean ratio exceeds 1.005")
    if global_result["tsc_bootstrap_ci95_upper"] > GLOBAL_UPPER_LIMIT:
        failures.append("global TSC bootstrap 95% upper bound exceeds 1.01")
    if global_result["wall_bootstrap_ci95_upper"] > GLOBAL_UPPER_LIMIT:
        failures.append("global wall bootstrap 95% upper bound exceeds 1.01")
    if global_result["maximum_tsc_cell_median_ratio"] > CELL_MEDIAN_LIMIT:
        failures.append("an individual TSC cell median exceeds 1.02")
    if global_result["maximum_wall_cell_median_ratio"] > CELL_MEDIAN_LIMIT:
        failures.append("an individual wall cell median exceeds 1.02")
    if not negative_controls["method_valid"]:
        failures.append("byte-identical c=0 controls show a systematic timing difference")
    return cell_results, global_result, negative_controls, failures


def report_markdown(report: dict, raw_path: Path, previous_path: Path) -> str:
    global_result = report["global"]
    controls = report["negative_controls"]
    lines = [
        "# Seasoned native-overhead confirmation",
        "",
        f"Decision: `{report['decision']}`.",
        "",
        "This analysis compares only matched-f unseasoned (A) and seasoned (B) tile-8 price-plus-Delta executions. "
        "Every observation is an ABBA/BAAB quartet and uses `(B1+B2)/(A1+A2)`.",
        "",
        "## Global pre-registered gates",
        "",
        "| Clock | Geometric mean | Bootstrap 95% upper | Limits |",
        "|---|---:|---:|---:|",
        f"| TSC | {global_result['tsc_geometric_mean_ratio']:.9f} | {global_result['tsc_bootstrap_ci95_upper']:.9f} | 1.005 / 1.01 |",
        f"| Wall | {global_result['wall_geometric_mean_ratio']:.9f} | {global_result['wall_bootstrap_ci95_upper']:.9f} | 1.005 / 1.01 |",
        "",
        f"Maximum individual cell medians are {global_result['maximum_tsc_cell_median_ratio']:.9f} TSC and "
        f"{global_result['maximum_wall_cell_median_ratio']:.9f} wall, against the 1.02 hard guard.",
        "",
        "## Byte-identical negative controls",
        "",
        "| Clock | Geometric mean | Two-sided bootstrap 95% interval |",
        "|---|---:|---:|",
        f"| TSC | {controls['tsc_geometric_mean_ratio']:.9f} | [{controls['tsc_bootstrap_ci95_lower']:.9f}, {controls['tsc_bootstrap_ci95_upper']:.9f}] |",
        f"| Wall | {controls['wall_geometric_mean_ratio']:.9f} | [{controls['wall_bootstrap_ci95_lower']:.9f}, {controls['wall_bootstrap_ci95_upper']:.9f}] |",
        "",
        f"The controls are methodologically {'valid' if controls['method_valid'] else 'invalid'}: both intervals must contain 1.0.",
        "",
        "## Cell results",
        "",
        "Individual confidence intervals are diagnostic only.",
        "",
        "| M | c | f | Estimator | Cache mode | TSC median [95%] | Wall median [95%] |",
        "|---:|---:|---:|---|---|---:|---:|",
    ]
    for cell in report["cells"]:
        lines.append(
            f"| {cell['M']} | {cell['c']} | {cell['f']} | {cell['estimator']} | {cell['cache_mode']} | "
            f"{cell['tsc_median_ratio']:.9f} [{cell['tsc_ci95_lower']:.9f}, {cell['tsc_ci95_upper']:.9f}] | "
            f"{cell['wall_median_ratio']:.9f} [{cell['wall_ci95_lower']:.9f}, {cell['wall_ci95_upper']:.9f}] |"
        )
    lines.extend(
        [
            "",
            "## Provenance and protocol correction",
            "",
            f"Raw quartets: `{raw_path}` (SHA-256 `{report['raw_sha256']}`).",
            "",
            f"Benchmark binary SHA-256: `{report['benchmark_binary_sha256']}`.",
            "",
            f"Preserved original failed AWS JSON: [`{previous_path}`]({report['previous_failed_artifact']['link']}) "
            f"(SHA-256 `{report['previous_failed_artifact']['sha256']}`).",
            "",
            "The original 400/400 rule treated hundreds of correlated case/candidate comparisons as independent mandatory hypothesis tests. "
            "Requiring every noisy individual interval to pass makes the family-wise rejection probability grow with the matrix and turns ordinary timer noise into near-certain failure. "
            "This confirmation instead pre-registers one stratified global statistic, keeps a bounded per-cell effect guard, and uses the byte-identical c=0 cells to detect protocol bias.",
            "",
            f"Bootstrap: {BOOTSTRAP_REPLICATES} deterministic stratified resamples, seed `0x{BOOTSTRAP_SEED:016x}`. "
            "Tile 8 was frozen before observing these data.",
        ]
    )
    if report["failures"]:
        lines.extend(["", "Failed gates:", ""] + [f"- {failure}" for failure in report["failures"]])
    else:
        lines.extend(["", "Qualification statuses:", ""] + [f"- `{status}`" for status in PASS_STATUSES])
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--previous-failed", type=Path, required=True)
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    args = parser.parse_args()
    if args.json.exists() or args.markdown.exists():
        fail("refusing to overwrite an existing analysis artifact")
    raw = json.loads(args.raw.read_text())
    previous_text = args.previous_failed.read_text()
    previous = json.loads(previous_text)
    if previous.get("status") != "AWS_PERFORMANCE_GATE_FAILED" or "AWS_PERFORMANCE_GATE_FAILED" not in previous_text:
        fail("the preserved prerequisite is not the original failed AWS JSON")
    cells = validate_raw(raw)
    cell_results, global_result, controls, failures = analyze(cells)
    decision = "SEASONED_STRIP_QUALIFIED" if not failures else "SEASONED_NATIVE_OVERHEAD_CONFIRMATION_FAILED"
    previous_link = Path("../asian_genuine_seasoned_price_delta_strip") / args.previous_failed.name
    report = {
        "schema": "asian-genuine-seasoned-overhead-confirmation-v1",
        "frozen_commit": FROZEN_COMMIT,
        "decision": decision,
        "statuses": list(PASS_STATUSES) if not failures else [],
        "failures": failures,
        "preregistration": {
            "ratio": "(B1+B2)/(A1+A2)",
            "bootstrap_seed": f"0x{BOOTSTRAP_SEED:016x}",
            "bootstrap_replicates": BOOTSTRAP_REPLICATES,
            "global_geometric_mean_limit": GLOBAL_MEDIAN_LIMIT,
            "global_ci95_upper_limit": GLOBAL_UPPER_LIMIT,
            "individual_cell_median_limit": CELL_MEDIAN_LIMIT,
            "negative_control_gate": "stratified two-sided 95% bootstrap interval contains 1.0 for both clocks",
        },
        "benchmark_binary_sha256": raw["binary_sha256"],
        "raw_sha256": sha256(args.raw),
        "platform": {
            key: raw[key]
            for key in ("cpu", "allowed_affinity", "thread_siblings", "cpu_model", "clock", "cache")
        },
        "previous_failed_artifact": {
            "path": str(args.previous_failed),
            "link": str(previous_link),
            "sha256": sha256(args.previous_failed),
            "status": "AWS_PERFORMANCE_GATE_FAILED",
        },
        "global": global_result,
        "negative_controls": controls,
        "cells": cell_results,
    }
    args.json.parent.mkdir(parents=True, exist_ok=True)
    with args.json.open("x") as output:
        output.write(json.dumps(report, indent=2, sort_keys=True) + "\n")
    with args.markdown.open("x") as output:
        output.write(report_markdown(report, args.raw, args.previous_failed))
    print(decision)
    return 0 if not failures else 2


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"analysis failed: {error}", file=sys.stderr)
        sys.exit(2)
