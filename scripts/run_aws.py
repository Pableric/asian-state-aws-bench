#!/usr/bin/env python3
"""Run isolated Asian S/Q and dim-permute tests/benches. Stdlib only."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shlex
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ALLOWLIST = (
    ".gitignore",
    "BUILD_METADATA.json",
    "BUILD_METADATA_dim.json",
    "BUILD_METADATA_geometric_cv.json",
    "BUILD_METADATA_sql18.json",
    "BUILD_METADATA_onemkl_x.json",
    "BUILD_METADATA_synthetic_all_permute.json",
    "BUILD_METADATA_genuine_complete.json",
    "BUILD_METADATA_genuine_multicore.json",
    "BUILD_METADATA_genuine_dual_control.json",
    "LICENSE",
    "README.md",
    "bin/asian_state_bench",
    "bin/asian_state_test",
    "bin/asian_affine_18diag_bench",
    "bin/asian_affine_conditional_18diag_bench",
    "bin/asian_affine_dual_sql_18diag_bench",
    "bin/asian_conditional_payoff_18diag_bench",
    "bin/asian_affine_growth_18diag_bench",
    "bin/asian_affine_x_growth_1dim_bench",
    "bin/dim_permute_bench",
    "bin/dim_permute_test",
    "bin/onemkl_sobol_x_bench",
    "bin/synthetic_all_permute_scaling_bench",
    "bin/asian_genuine_complete_bench",
    "bin/asian_genuine_multicore_bench",
    "bin/asian_genuine_dual_control_bench",
    "direction_numbers/joe_kuo_6_21201.bin",
    "real_block_maps.bin",
    "scripts/run_aws.py",
)

ASIAN_CANDIDATES = (
    "packet_1x",
    "packet_2x",
    "packet_4x",
    "timestep_1x",
    "timestep_2x",
    "timestep_4x",
)

DIM_CANDIDATES = (
    "generic",
    "affine",
    "res2xor",
)

DIM_REAL_DIMS = (
    1,
    5,
    6,
    7,
    8,
    9,
    11,
    13,
    15,
    17,
    19,
    21,
    23,
    26,
    27,
    28,
    29,
    31,
)

AFFINE18_CANDIDATES = (
    "affine_provider_only_one_4096_block",
    "affine_provider_only_17",
    "exp_sq_exact_z_dimension_major",
    "exp_sq_exact_z_packet_major",
    "unfused_temp16k_dimension_major",
    "unfused_temp128_packet_major",
    "fused_dimension_major_18diag",
    "fused_packet_major_18diag",
    "d1_producer_corrected",
    "combined_d1_fused_dimension_major_18diag",
    "combined_d1_fused_packet_major_18diag",
)

AFFINE18_MODES = ("warm_L1D", "competing_32KiB")

GROWTH18_CANDIDATES = (
    "d1_z_to_growth_inplace",
    "growth_affine_provider_17",
    "growth_packet_major_18diag",
    "growth_dimension_major_18diag",
    "growth_unfused_temp128_packet_major",
    "growth_unfused_temp16k_dimension_major",
    "combined_d1_convert_growth_packet_major",
    "combined_d1_convert_growth_dimension_major",
    "frozen_zexp_packet_major_18diag",
    "frozen_combined_d1_zexp_packet_major_18diag",
)

GROWTH18_DENOMINATORS = {
    "d1_z_to_growth_inplace": "cycles_per_block",
    "growth_affine_provider_17": "cycles_per_dimension",
    **{
        candidate: "cycles_per_step"
        for candidate in GROWTH18_CANDIDATES[2:]
    },
}

CONDITIONAL18_CANDIDATES = (
    "frozen_growth_packet_major_18diag",
    "conditional_residual_packet_major_18diag",
    "conditional_residual_dimension_major_18diag",
    "conditional_deterministic_initialization",
    "corrected_d1_producer",
    "d1_convert_initialize_residual",
    "conditional_scalar_accurate_payoff",
    "initialize_residual_scalar_payoff",
    "d1_initialize_residual_scalar_payoff",
)

CONDITIONAL_PAYOFF_CANDIDATES = (
    "frozen_growth_packet_major_18diag",
    "vector_log_degree6",
    "paired_phi_lut2048",
    "conditional_lut1024_kernel",
    "conditional_lut2048_kernel",
    "conditional_lut4096_kernel",
    "combined_d1_conditional_lut1024",
    "combined_d1_conditional_lut2048",
    "combined_d1_conditional_lut4096",
    "scalar_accurate_research_oracle",
)

XGROWTH1_CANDIDATES = (
    "canonical_growth_producer",
    "canonical_dual_x_growth_producer",
    "growth_affine_provider",
    "dual_affine_provider",
    "recurrence_sq",
    "recurrence_sql",
    "fused_growth_provider_sq",
    "fused_dual_provider_sql",
    "combined_growth_producer_fused_sq",
    "combined_dual_producer_fused_sql",
)

XGROWTH1_MODES = ("warm", "competing_32KiB")

XGROWTH1_SOURCE_CANDIDATES = (
    "old_growth_source",
    "new_growth_source",
    "new_dual_source",
)

SQL18_CANDIDATES = (
    "path_frozen_sq_historical",
    "path_sq_matched_unrolled",
    "path_sql_memory_bcst",
    "path_sql_decrement",
    "path_sql_explicit_bcst",
    "path_sql_general_loop",
    "payoff_arithmetic_only",
    "payoff_geometric_only",
    "payoff_geometric_cv_combined",
    "partial_complete_arithmetic_diag",
    "partial_complete_geometric_cv_diag",
)

SQL18_MODES = ("warm_candidate_specific", "historical_32KiB_rmw_pressure")

ONEMKL_X_CANDIDATES = (
    "old_corrected_z_then_affine_x",
    "our_canonical_sobol_to_x",
    "oneMKL_d1_sobol_to_x",
)

ONEMKL_X_MODES = ("warm", "competing_32KiB")

ONEMKL_X_CONTRACTS = (
    (0.03, 0.20),
    (-0.07, 0.10),
    (-0.25, 0.00),
)

SYNTHETIC_ALL_PERMUTE_NS = (16, 32, 64, 128, 256)
SYNTHETIC_ALL_PERMUTE_CANDIDATES = (
    "synthetic_all_permute_materialized_x",
    "synthetic_all_permute_fused_sql",
    "onemkl_native_nd_x",
    "canonical_x_source",
    "canonical_dual_source",
    "materialized_x_routes_only",
    "fused_sql_routes_only",
)
SYNTHETIC_ALL_PERMUTE_MODES = (
    "warm_candidate_specific",
    "historical_32KiB_rmw_pressure",
)

GENUINE_COMPLETE_NS = (16, 32, 64, 128, 256)
GENUINE_COMPLETE_CANDIDATES = (
    "our_source",
    "our_routed_sql",
    "our_payoff_arithmetic",
    "our_payoff_geometric_cv",
    "our_complete_arithmetic",
    "our_complete_geometric_cv",
    "onemkl_gaussian",
    "intel_point_consumer",
    "intel_tiled_dimension_consumer",
    "intel_payoff_arithmetic",
    "intel_payoff_geometric_cv",
    "intel_point_complete_arithmetic",
    "intel_tiled_complete_arithmetic",
    "intel_point_complete_geometric_cv",
    "intel_tiled_complete_geometric_cv",
)
GENUINE_COMPLETE_MODES = (
    "warm_candidate_specific",
    "historical_32KiB_rmw",
)


def fail(message: str, code: int = 1) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(code)


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 16), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_sha256sums(path: Path) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2:
            fail(f"malformed SHA256SUMS line: {raw}")
        digest, rel = parts[0], parts[-1]
        if rel.startswith("*") or rel.startswith("./"):
            rel = rel.lstrip("*").lstrip("./")
        mapping[rel] = digest
    return mapping


def verify_checksums(root: Path) -> dict[str, str]:
    sums_path = root / "SHA256SUMS"
    if not sums_path.is_file():
        fail("SHA256SUMS missing")
    listed = parse_sha256sums(sums_path)
    expected = set(ALLOWLIST)
    if set(listed.keys()) != expected:
        fail(f"SHA256SUMS set mismatch: {sorted(listed)} vs {sorted(expected)}")
    hashes: dict[str, str] = {}
    for rel, expected_digest in listed.items():
        path = (root / rel).resolve()
        try:
            path.relative_to(root.resolve())
        except ValueError:
            fail(f"checksum path escaped root: {rel}")
        if path.is_symlink():
            fail(f"symlink rejected: {rel}")
        if not path.is_file():
            fail(f"missing artifact: {rel}")
        actual = sha256_file(path)
        if actual != expected_digest:
            fail(f"checksum mismatch: {rel}")
        hashes[rel] = actual
    return hashes


def cpu_info() -> tuple[str, list[str]]:
    model = "unknown"
    flags: list[str] = []
    try:
        text = Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return model, flags
    for line in text.splitlines():
        if line.startswith("model name") and ":" in line and model == "unknown":
            model = line.split(":", 1)[1].strip()
        if line.startswith("flags") and ":" in line and not flags:
            flags = line.split(":", 1)[1].split()
    return model, flags


def avx512_flags(flags: list[str]) -> list[str]:
    return [f for f in flags if f.startswith("avx512")]


def read_cache() -> list[dict[str, str]]:
    caches = []
    base = Path("/sys/devices/system/cpu/cpu0/cache")
    if not base.is_dir():
        return caches
    for index in sorted(base.glob("index*")):
        def one(name: str) -> str:
            p = index / name
            try:
                return p.read_text(encoding="utf-8").strip()
            except OSError:
                return "unsupported"

        caches.append(
            {
                "index": index.name,
                "level": one("level"),
                "type": one("type"),
                "size": one("size"),
            }
        )
    return caches


def ldd(path: Path) -> str:
    try:
        return subprocess.run(
            ["ldd", str(path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unavailable"


def pin_cpu() -> int:
    if not hasattr(os, "sched_getaffinity") or not hasattr(os, "sched_setaffinity"):
        print("cpu_affinity=unsupported")
        return -1
    allowed = os.sched_getaffinity(0)
    if not allowed:
        fail("empty CPU affinity set")
    selected = 0 if 0 in allowed else min(allowed)
    os.sched_setaffinity(0, {selected})
    print(f"affinity_allowed={sorted(allowed)}")
    print(f"selected_cpu={selected}")
    return selected


def run_bin(
    path: Path, root: Path, args: tuple[str, ...] = ()
) -> subprocess.CompletedProcess[str]:
    resolved = path.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        fail(f"refusing to execute binary outside repo: {path}")
    if resolved.is_symlink():
        fail(f"refusing to execute symlink: {path}")
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        fail(f"binary not executable: {path}")
    return subprocess.run(
        [str(resolved), *args],
        cwd=str(root),
        capture_output=True,
        text=True,
    )


def parse_results(stdout: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in stdout.splitlines():
        if not line.startswith("RESULT "):
            continue
        blob = line[len("RESULT ") :].strip()
        try:
            obj = json.loads(blob)
        except json.JSONDecodeError as exc:
            fail(f"malformed RESULT line: {exc}: {blob[:200]}")
        if not isinstance(obj, dict) or "kind" not in obj:
            fail(f"RESULT missing kind: {blob[:200]}")
        rows.append(obj)
    return rows


def parse_kv_line(line: str) -> dict[str, object]:
    fields: dict[str, object] = {}
    for token in shlex.split(line):
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def parse_dim_results(stdout: str) -> list[dict[str, object]]:
    """Parse and validate Junior's complete dest-order real-map benchmark."""
    rows: list[dict[str, object]] = []
    aggregates: dict[tuple[str, str], dict[str, object]] = {}
    real_rows: set[tuple[int, str, str]] = set()
    native_banner = False

    for line in stdout.splitlines():
        if line.startswith("dim_provider_bench native_avx512=1 "):
            native_banner = True
        if line.startswith("map="):
            row = parse_kv_line(line)
            row["kind"] = "dim_native"
            rows.append(row)
            if row.get("map") == "real" and row.get("mode") in {
                "warm_L1D",
                "pressure_32KiB",
            }:
                try:
                    key = (int(str(row["dim"])), str(row["variant"]), str(row["mode"]))
                    median = float(str(row["median_cyc"]))
                    p10 = float(str(row["p10"]))
                    p90 = float(str(row["p90"]))
                    float(str(row["cyc_per_128_block"]))
                    float(str(row["cyc_per_packet"]))
                except (KeyError, TypeError, ValueError) as exc:
                    fail(f"malformed real dimension row: {line}: {exc}")
                if not p10 <= median <= p90:
                    fail(f"invalid percentile order: {line}")
                if key in real_rows:
                    fail(f"duplicate real dimension row: {key}")
                real_rows.add(key)
        elif line.startswith("aggregate "):
            row = parse_kv_line(line)
            row["kind"] = "dim_aggregate_native"
            rows.append(row)
            try:
                key = (str(row["variant"]), str(row["mode"]))
                float(str(row["real_median_cyc"]))
                float(str(row["real_median_cyc_per_packet"]))
                worst_dim = int(str(row["worst_dim"]))
                float(str(row["worst_median_cyc"]))
                float(str(row["worst_cyc_per_packet"]))
                n_dims = int(str(row["n_dims"]))
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"malformed dimension aggregate: {line}: {exc}")
            if row.get("map") != "real":
                fail(f"non-real map included in aggregate: {line}")
            if worst_dim not in DIM_REAL_DIMS or n_dims != len(DIM_REAL_DIMS):
                fail(f"invalid aggregate dimension coverage: {line}")
            if key in aggregates:
                fail(f"duplicate dimension aggregate: {key}")
            aggregates[key] = row
        elif line.startswith(("host ", "cache ", "timing_envelope ")):
            row = parse_kv_line(line)
            row["kind"] = line.split()[0]
            rows.append(row)

    if not native_banner:
        fail("dimension benchmark missing native AVX-512 banner")
    expected_rows = {
        (dim, candidate, env)
        for dim in DIM_REAL_DIMS
        for candidate in DIM_CANDIDATES
        for env in ("warm_L1D", "pressure_32KiB")
    }
    if real_rows != expected_rows:
        missing = sorted(expected_rows - real_rows)
        extra = sorted(real_rows - expected_rows)
        fail(f"dimension real-map coverage mismatch: missing={missing} extra={extra}")
    expected_aggregates = {
        (candidate, env)
        for candidate in DIM_CANDIDATES
        for env in ("warm_L1D", "pressure_32KiB")
    }
    if set(aggregates) != expected_aggregates:
        fail(
            "dimension aggregate coverage mismatch: "
            f"got={sorted(aggregates)} expected={sorted(expected_aggregates)}"
        )
    return rows


def parse_affine18_results(stdout: str) -> list[dict[str, object]]:
    pattern = re.compile(
        r"^candidate=(\S+) mode=(\S+) median=(\d+) p10=(\d+) p90=(\d+) "
        r"cycles_per_step=([0-9.]+) cycles_per_path_step=([0-9.]+) "
        r"raw=\[([0-9,]+)\]$"
    )
    rows: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    native_banner = False
    layout_banner = False
    checksum_banner = False

    for line in stdout.splitlines():
        if line.startswith("asian_affine_18diag_bench native_avx512=1 "):
            native_banner = True
            continue
        if line.startswith("layouts z_dimension_major="):
            layout_banner = True
            continue
        if line.startswith("checksum="):
            checksum_banner = True
            continue
        match = pattern.match(line)
        if not match:
            continue
        candidate, mode = match.group(1), match.group(2)
        median, p10, p90 = (int(match.group(i)) for i in (3, 4, 5))
        raw = [int(value) for value in match.group(8).split(",")]
        if len(raw) != 51:
            fail(f"affine18 {candidate}/{mode} has {len(raw)} samples, expected 51")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"affine18 percentile mismatch for {candidate}/{mode}")
        if not p10 <= median <= p90:
            fail(f"affine18 percentile order invalid for {candidate}/{mode}")
        key = (candidate, mode)
        if key in seen:
            fail(f"duplicate affine18 result: {key}")
        seen.add(key)
        rows.append(
            {
                "kind": "affine18_native",
                "candidate": candidate,
                "mode": mode,
                "median": median,
                "p10": p10,
                "p90": p90,
                "cycles_per_step": float(match.group(6)),
                "cycles_per_path_step": float(match.group(7)),
                "raw_batches": raw,
            }
        )

    expected = {
        (candidate, mode)
        for candidate in AFFINE18_CANDIDATES
        for mode in AFFINE18_MODES
    }
    if not native_banner or not layout_banner or not checksum_banner:
        fail("affine18 benchmark missing correctness/layout/checksum banner")
    if seen != expected:
        fail(
            "affine18 result coverage mismatch: "
            f"missing={sorted(expected - seen)} extra={sorted(seen - expected)}"
        )
    return rows


def parse_growth18_results(stdout: str) -> list[dict[str, object]]:
    """Parse the self-checking growth-payload experiment without mixing units."""
    rows: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    native_banner = False
    frozen_reference = False
    reset_banner = False
    checksum_banner = False
    growth_summary: tuple[int, float] | None = None
    combined_summary: tuple[int, float] | None = None
    warm_medians: dict[str, int] = {}

    for line in stdout.splitlines():
        if line.startswith("asian_affine_growth_18diag_bench native_avx512=1 "):
            native_banner = True
            continue
        if line == (
            "frozen_warm_reference_zexp_packet_major=31612 "
            "frozen_warm_reference_combined_d1_zexp_packet_major=33232"
        ):
            frozen_reference = True
            continue
        if line.startswith("state_reset=outside_timing "):
            reset_banner = True
            continue
        if line.startswith("primary_growth_packet_vs_frozen_reference "):
            row = parse_kv_line(line)
            try:
                growth_summary = (int(str(row["delta"])), float(str(row["ratio"])))
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"malformed growth18 primary summary: {line}: {exc}")
            continue
        if line.startswith("primary_combined_growth_packet_vs_frozen_reference "):
            row = parse_kv_line(line)
            try:
                combined_summary = (int(str(row["delta"])), float(str(row["ratio"])))
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"malformed growth18 combined summary: {line}: {exc}")
            continue
        if line.startswith("checksum="):
            row = parse_kv_line(line)
            try:
                int(str(row["checksum"]))
                if int(str(row["nominal_payload_bytes"])) != 16384:
                    fail("growth18 payload footprint is not 16 KiB")
                if int(str(row["nominal_state_bytes"])) != 32768:
                    fail("growth18 state footprint is not 32 KiB")
                int(str(row["nominal_maps_bytes"]))
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"malformed growth18 checksum/footprint line: {line}: {exc}")
            checksum_banner = True
            continue
        if not line.startswith("candidate="):
            continue

        row = parse_kv_line(line)
        try:
            candidate = str(row["candidate"])
            mode = str(row["mode"])
            median = int(str(row["median"]))
            p10 = int(str(row["p10"]))
            p90 = int(str(row["p90"]))
            raw_text = str(row["raw"])
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed growth18 result: {line}: {exc}")
        if candidate not in GROWTH18_CANDIDATES or mode not in AFFINE18_MODES:
            fail(f"unexpected growth18 candidate/mode: {candidate}/{mode}")
        if not (raw_text.startswith("[") and raw_text.endswith("]")):
            fail(f"malformed growth18 raw batches: {candidate}/{mode}")
        try:
            raw = [int(value) for value in raw_text[1:-1].split(",")]
        except ValueError as exc:
            fail(f"malformed growth18 raw value: {candidate}/{mode}: {exc}")
        if len(raw) != 51:
            fail(f"growth18 {candidate}/{mode} has {len(raw)} samples, expected 51")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"growth18 percentile mismatch for {candidate}/{mode}")

        denominator = GROWTH18_DENOMINATORS[candidate]
        try:
            metric_value = float(str(row[denominator]))
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"growth18 missing {denominator} for {candidate}/{mode}: {exc}")
        expected_value = {
            "cycles_per_block": float(median),
            "cycles_per_dimension": median / 17.0,
            "cycles_per_step": median / 18.0,
        }[denominator]
        if abs(metric_value - expected_value) > 0.000001:
            fail(f"growth18 denominator mismatch for {candidate}/{mode}")
        if denominator == "cycles_per_step":
            try:
                path_step = float(str(row["cycles_per_path_step"]))
                packet_step = float(str(row["cycles_per_packet_step"]))
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"growth18 missing state denominators for {candidate}/{mode}: {exc}")
            if abs(path_step - median / (18.0 * 4096.0)) > 0.000000001:
                fail(f"growth18 path-step denominator mismatch for {candidate}/{mode}")
            if abs(packet_step - median / (18.0 * 128.0)) > 0.000001:
                fail(f"growth18 packet-step denominator mismatch for {candidate}/{mode}")

        key = (candidate, mode)
        if key in seen:
            fail(f"duplicate growth18 result: {key}")
        seen.add(key)
        if mode == "warm_L1D":
            warm_medians[candidate] = median
        rows.append(
            {
                "kind": "growth18_native",
                "candidate": candidate,
                "mode": mode,
                "median": median,
                "p10": p10,
                "p90": p90,
                "metric": denominator,
                "metric_value": metric_value,
                "raw_batches": raw,
            }
        )

    expected = {
        (candidate, mode)
        for candidate in GROWTH18_CANDIDATES
        for mode in AFFINE18_MODES
    }
    if not all((native_banner, frozen_reference, reset_banner, checksum_banner)):
        fail("growth18 benchmark missing native/reference/reset/checksum banner")
    if seen != expected:
        fail(
            "growth18 result coverage mismatch: "
            f"missing={sorted(expected - seen)} extra={sorted(seen - expected)}"
        )
    if growth_summary is None or combined_summary is None:
        fail("growth18 benchmark missing frozen-reference comparisons")
    growth_median = warm_medians["growth_packet_major_18diag"]
    combined_median = warm_medians["combined_d1_convert_growth_packet_major"]
    expected_growth = (growth_median - 31612, growth_median / 31612.0)
    expected_combined = (combined_median - 33232, combined_median / 33232.0)
    if growth_summary[0] != expected_growth[0] or abs(growth_summary[1] - expected_growth[1]) > 0.000000001:
        fail("growth18 primary frozen-reference comparison mismatch")
    if combined_summary[0] != expected_combined[0] or abs(combined_summary[1] - expected_combined[1]) > 0.000000001:
        fail("growth18 combined frozen-reference comparison mismatch")
    rows.extend(
        (
            {
                "kind": "growth18_comparison",
                "candidate": "growth_packet_major_18diag",
                "reference_cycles": 31612,
                "delta": growth_summary[0],
                "ratio": growth_summary[1],
            },
            {
                "kind": "growth18_comparison",
                "candidate": "combined_d1_convert_growth_packet_major",
                "reference_cycles": 33232,
                "delta": combined_summary[0],
                "ratio": combined_summary[1],
            },
        )
    )
    return rows


def parse_conditional18_results(stdout: str) -> list[dict[str, object]]:
    """Validate the complete first-increment conditional diagnostic output."""
    rows: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    native_banner = False
    scope_banner = False
    frozen_banner = False
    checksum_banner = False

    for line in stdout.splitlines():
        if line.startswith("asian_affine_conditional_18diag_bench cpu="):
            native_banner = True
            fields = parse_kv_line(line)
            if fields.get("canonical_provider_block_only") != "1":
                fail("conditional18 benchmark did not declare canonical-only provider scope")
            if fields.get("randomized_statistics") != "separate":
                fail("conditional18 benchmark mixed randomized statistics into provider timing")
            continue
        if line == "scalar payoff is accurate research math, not projected vectorized production performance":
            scope_banner = True
            continue
        if line == (
            "frozen_aws growth_pm=12288 growth_dm=14510 "
            "combined_growth_pm=14376 combined_growth_dm=17442 "
            "zexp_pm=31176 combined_zexp_pm=32036"
        ):
            frozen_banner = True
            continue
        if line.startswith("checksum="):
            fields = parse_kv_line(line)
            try:
                int(str(fields["checksum"]))
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"malformed conditional18 checksum: {line}: {exc}")
            checksum_banner = True
            continue
        if not line.startswith("candidate="):
            continue

        fields = parse_kv_line(line)
        try:
            candidate = str(fields["candidate"])
            label = str(fields["label"])
            mode = str(fields["mode"])
            median = int(str(fields["median"]))
            p10 = int(str(fields["p10"]))
            p90 = int(str(fields["p90"]))
            raw_text = str(fields["raw"])
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed conditional18 result: {line}: {exc}")
        if candidate not in CONDITIONAL18_CANDIDATES or mode not in AFFINE18_MODES:
            fail(f"unexpected conditional18 candidate/mode: {candidate}/{mode}")
        if not (raw_text.startswith("[") and raw_text.endswith("]")):
            fail(f"malformed conditional18 raw samples: {candidate}/{mode}")
        try:
            raw = [int(value) for value in raw_text[1:-1].split(",")]
        except ValueError as exc:
            fail(f"malformed conditional18 raw value: {candidate}/{mode}: {exc}")
        if len(raw) != 51:
            fail(f"conditional18 {candidate}/{mode} has {len(raw)} samples, expected 51")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"conditional18 percentile mismatch for {candidate}/{mode}")
        key = (candidate, mode)
        if key in seen:
            fail(f"duplicate conditional18 result: {key}")
        seen.add(key)
        rows.append(
            {
                "kind": "conditional18_native",
                "candidate": candidate,
                "label": label,
                "mode": mode,
                "median": median,
                "p10": p10,
                "p90": p90,
                "raw_batches": raw,
            }
        )

    expected = {
        (candidate, mode)
        for candidate in CONDITIONAL18_CANDIDATES
        for mode in AFFINE18_MODES
    }
    if not all((native_banner, scope_banner, frozen_banner, checksum_banner)):
        fail("conditional18 benchmark missing native/scope/reference/checksum banner")
    if seen != expected:
        fail(
            "conditional18 result coverage mismatch: "
            f"missing={sorted(expected - seen)} extra={sorted(seen - expected)}"
        )
    return rows


def parse_conditional_payoff_results(stdout: str) -> list[dict[str, object]]:
    """Validate vector-log/CDF and fused conditional-payoff timing output."""
    rows: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    native_banner = False
    checksum_banner = False
    accuracy: dict[str, float] | None = None

    for line in stdout.splitlines():
        if line.startswith("cpu="):
            fields = parse_kv_line(line)
            if fields.get("canonical_only") != "1":
                fail("conditional payoff benchmark did not declare canonical-only scope")
            if fields.get("scalar_oracle_research_only") != "1":
                fail("conditional payoff benchmark did not label scalar oracle scope")
            if fields.get("samples") != "51":
                fail("conditional payoff benchmark did not use 51 samples")
            native_banner = True
            continue
        if line.startswith("checksum="):
            fields = parse_kv_line(line)
            try:
                int(str(fields["checksum"]))
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"malformed conditional payoff checksum: {line}: {exc}")
            checksum_banner = True
            continue
        if line.startswith("accuracy "):
            fields = parse_kv_line(line)
            try:
                parsed = {
                    "libm_reference": float(str(fields["libm_reference"])),
                    "lut1024_abs_error": float(str(fields["lut1024_abs_error"])),
                    "lut2048_abs_error": float(str(fields["lut2048_abs_error"])),
                    "lut4096_abs_error": float(str(fields["lut4096_abs_error"])),
                    "threshold": float(str(fields["threshold"])),
                }
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"malformed conditional payoff accuracy line: {line}: {exc}")
            if any(value != value for value in parsed.values()):
                fail(f"non-finite conditional payoff accuracy line: {line}")
            if parsed["threshold"] != 1e-4:
                fail(f"unexpected conditional payoff accuracy threshold: {line}")
            if any(
                parsed[name] > parsed["threshold"]
                for name in ("lut1024_abs_error", "lut2048_abs_error", "lut4096_abs_error")
            ):
                fail(f"conditional payoff accuracy threshold failed: {line}")
            accuracy = parsed
            continue
        if not line.startswith("candidate="):
            continue

        fields = parse_kv_line(line)
        try:
            candidate = str(fields["candidate"])
            label = str(fields["label"])
            mode = str(fields["mode"])
            median = int(str(fields["median"]))
            p10 = int(str(fields["p10"]))
            p90 = int(str(fields["p90"]))
            cycles_per_path = float(str(fields["cycles_per_path"]))
            raw_text = str(fields["raw"])
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed conditional payoff result: {line}: {exc}")
        if candidate not in CONDITIONAL_PAYOFF_CANDIDATES or mode not in AFFINE18_MODES:
            fail(f"unexpected conditional payoff candidate/mode: {candidate}/{mode}")
        if not (raw_text.startswith("[") and raw_text.endswith("]")):
            fail(f"malformed conditional payoff raw samples: {candidate}/{mode}")
        try:
            raw = [int(value) for value in raw_text[1:-1].split(",")]
        except ValueError as exc:
            fail(f"malformed conditional payoff raw value: {candidate}/{mode}: {exc}")
        if len(raw) != 51:
            fail(f"conditional payoff {candidate}/{mode} has {len(raw)} samples, expected 51")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"conditional payoff percentile mismatch for {candidate}/{mode}")
        if not p10 <= median <= p90:
            fail(f"conditional payoff percentile order invalid for {candidate}/{mode}")
        if abs(cycles_per_path - median / 4096.0) > 0.000001:
            fail(f"conditional payoff path denominator mismatch for {candidate}/{mode}")
        key = (candidate, mode)
        if key in seen:
            fail(f"duplicate conditional payoff result: {key}")
        seen.add(key)
        rows.append(
            {
                "kind": "conditional_payoff_native",
                "candidate": candidate,
                "label": label,
                "mode": mode,
                "median": median,
                "p10": p10,
                "p90": p90,
                "cycles_per_path": cycles_per_path,
                "raw_batches": raw,
            }
        )

    expected = {
        (candidate, mode)
        for candidate in CONDITIONAL_PAYOFF_CANDIDATES
        for mode in AFFINE18_MODES
    }
    if not native_banner or not checksum_banner:
        fail("conditional payoff benchmark missing native/checksum banner")
    if accuracy is None:
        fail("conditional payoff benchmark missing libm accuracy line")
    if seen != expected:
        fail(
            "conditional payoff result coverage mismatch: "
            f"missing={sorted(expected - seen)} extra={sorted(seen - expected)}"
        )
    rows.append({"kind": "conditional_payoff_accuracy", **accuracy})
    return rows


def parse_sql18_results(stdout: str) -> list[dict[str, object]]:
    """Validate the partial weighted S/Q/L and geometric-payoff diagnostic."""
    try:
        payload = json.loads(stdout)
    except json.JSONDecodeError as exc:
        fail(f"sql18 benchmark did not emit valid JSON: {exc}")
    if not isinstance(payload, dict):
        fail("sql18 benchmark JSON must be an object")
    if payload.get("status") != "PASS" or payload.get("native_avx512") is not True:
        fail("sql18 benchmark did not report native PASS")
    if payload.get("warmups") != 16 or payload.get("samples") != 51:
        fail("sql18 benchmark warmup/sample contract mismatch")
    if payload.get("scope") != "partial_18diag_path_and_payoff_mechanics; not an Asian price":
        fail("sql18 benchmark did not preserve partial-price scope")
    if payload.get("complete_price_cycles") is not None or payload.get("winner") is not None:
        fail("sql18 benchmark made a forbidden complete-price or winner claim")
    if payload.get("complete_price_reason") != "14 future dimensions unresolved":
        fail("sql18 benchmark omitted unresolved-dimension reason")

    candidates = payload.get("candidates")
    if not isinstance(candidates, list):
        fail("sql18 benchmark candidates must be a list")
    rows: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    medians: dict[tuple[str, str], int] = {}
    for item in candidates:
        if not isinstance(item, dict):
            fail("sql18 candidate record must be an object")
        try:
            candidate = str(item["name"])
            mode = str(item["mode"])
            median = int(item["median"])
            p10 = int(item["p10"])
            p90 = int(item["p90"])
            packet_fixing = float(item["cycles_per_packet_fixing"])
            path_fixing = float(item["cycles_per_path_fixing"])
            raw = [int(value) for value in item["raw"]]
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed sql18 candidate record: {item}: {exc}")
        if candidate not in SQL18_CANDIDATES or mode not in SQL18_MODES:
            fail(f"unexpected sql18 candidate/mode: {candidate}/{mode}")
        if len(raw) != 51:
            fail(f"sql18 {candidate}/{mode} has {len(raw)} samples, expected 51")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"sql18 percentile mismatch for {candidate}/{mode}")
        if abs(packet_fixing - median / (128.0 * 18.0)) > 0.000001:
            fail(f"sql18 packet-fixing denominator mismatch for {candidate}/{mode}")
        if abs(path_fixing - median / (4096.0 * 18.0)) > 0.000000001:
            fail(f"sql18 path-fixing denominator mismatch for {candidate}/{mode}")
        key = (candidate, mode)
        if key in seen:
            fail(f"duplicate sql18 result: {key}")
        seen.add(key)
        medians[key] = median
        rows.append(
            {
                "kind": "sql18_native",
                "candidate": candidate,
                "mode": mode,
                "median": median,
                "p10": p10,
                "p90": p90,
                "cycles_per_packet_fixing": packet_fixing,
                "cycles_per_path_fixing": path_fixing,
                "raw_batches": raw,
            }
        )

    expected = {
        (candidate, mode)
        for candidate in SQL18_CANDIDATES
        for mode in SQL18_MODES
    }
    if seen != expected:
        fail(
            "sql18 result coverage mismatch: "
            f"missing={sorted(expected - seen)} extra={sorted(seen - expected)}"
        )

    increments = payload.get("incremental_vs_matched_sq")
    if not isinstance(increments, dict):
        fail("sql18 benchmark missing incremental comparison")
    for mode in SQL18_MODES:
        reported = increments.get(mode)
        if not isinstance(reported, dict):
            fail(f"sql18 benchmark missing {mode} incremental comparison")
        baseline = medians[("path_sq_matched_unrolled", mode)]
        for candidate in SQL18_CANDIDATES[2:6]:
            try:
                delta = int(reported[candidate])
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"malformed sql18 increment for {candidate}/{mode}: {exc}")
            if delta != medians[(candidate, mode)] - baseline:
                fail(f"sql18 increment mismatch for {candidate}/{mode}")

    scalar = payload.get("scalar_libm_oracle_excluded")
    setup = payload.get("setup")
    if not isinstance(scalar, dict) or not isinstance(setup, dict):
        fail("sql18 benchmark missing scalar-oracle/setup records")
    try:
        scalar_p10 = int(scalar["p10"])
        scalar_median = int(scalar["median"])
        scalar_p90 = int(scalar["p90"])
        route_setup = int(setup["route_and_producer_median"])
        payoff_setup = int(setup["payoff_median"])
        equivalent = float(setup["equivalent_partial_cv_blocks"])
    except (KeyError, TypeError, ValueError) as exc:
        fail(f"malformed sql18 scalar-oracle/setup records: {exc}")
    if not scalar_p10 <= scalar_median <= scalar_p90:
        fail("sql18 scalar-oracle percentile order invalid")
    if route_setup < 0 or payoff_setup < 0 or equivalent < 0:
        fail("sql18 setup record contains negative values")
    rows.append(
        {
            "kind": "sql18_summary",
            "scalar_libm_oracle_excluded": scalar,
            "setup": setup,
            "incremental_vs_matched_sq": increments,
        }
    )
    return rows


def parse_onemkl_x_results(payload: object) -> list[dict[str, object]]:
    """Validate the native 4,096-value Sobol-to-x throughput comparison."""
    if not isinstance(payload, dict):
        fail("oneMKL Sobol-to-x report must be an object")
    if payload.get("benchmark") != "minimal_onemkl_sobol_to_x":
        fail("oneMKL Sobol-to-x benchmark identity mismatch")
    if payload.get("status") != "PASS" or payload.get("native_avx512f") is not True:
        fail("oneMKL Sobol-to-x benchmark did not report native PASS")

    parameters = payload.get("parameters")
    if not isinstance(parameters, dict):
        fail("oneMKL Sobol-to-x report is missing parameters")
    expected_parameters = {
        "values": 4096,
        "sobol_start_point": 8192,
        "oneMKL_skip_elements": 8192,
        "warmups": 16,
        "samples": 51,
    }
    for key, expected in expected_parameters.items():
        if parameters.get(key) != expected:
            fail(f"oneMKL Sobol-to-x parameter mismatch: {key}")
    if parameters.get("our_layout") != "4096 consecutive canonical D1 points":
        fail("oneMKL comparison changed the canonical D1 layout")
    if parameters.get("oneMKL_layout") != "4096 consecutive oneMKL D1 points":
        fail("oneMKL comparison changed the native vendor layout")
    if payload.get("sobol_output_permutation_or_adapter") is not False:
        fail("oneMKL comparison unexpectedly added an output adapter")
    compatibility = payload.get("direction_order_compatibility")
    allowed_compatibility = {
        "exact_raw_d1_words": True,
        "different_raw_d1_words_native_layout": False,
        "raw_word_probe_unavailable_native_layout": False,
    }
    if not isinstance(compatibility, dict) or compatibility.get("status") not in allowed_compatibility:
        fail("oneMKL comparison omitted the raw-D1 compatibility result")
    if compatibility.get("value_by_value_comparison") is not allowed_compatibility[compatibility["status"]]:
        fail("oneMKL comparison compatibility/value-by-value fields disagree")
    if payload.get("candidates") != list(ONEMKL_X_CANDIDATES):
        fail("oneMKL comparison candidate list changed")
    if payload.get("threading") != {"layer": "sequential", "dynamic": False, "threads": 1}:
        fail("oneMKL comparison is not single-threaded sequential")
    if payload.get("candidate_order_shuffle_only") is not True:
        fail("oneMKL comparison did not preserve shuffled candidate ordering")

    correctness = payload.get("correctness")
    if not isinstance(correctness, dict) or correctness.get("status") != "PASS":
        fail("oneMKL Sobol-to-x correctness gate did not pass")
    contracts = correctness.get("contracts")
    if not isinstance(contracts, list) or len(contracts) != len(ONEMKL_X_CONTRACTS):
        fail("oneMKL Sobol-to-x correctness contract coverage mismatch")
    for index, ((drift, diffusion), record) in enumerate(zip(ONEMKL_X_CONTRACTS, contracts)):
        if not isinstance(record, dict):
            fail(f"oneMKL correctness contract {index} is malformed")
        if abs(float(record.get("drift", 99.0)) - drift) > 1e-7:
            fail(f"oneMKL correctness drift mismatch for contract {index}")
        if abs(float(record.get("diffusion", 99.0)) - diffusion) > 1e-7:
            fail(f"oneMKL correctness diffusion mismatch for contract {index}")
        old = record.get("old")
        ours = record.get("ours")
        mkl = record.get("oneMKL")
        if not isinstance(old, dict) or old.get("deterministic") is not True:
            fail(f"old corrected-Z x producer is not deterministic for contract {index}")
        if not isinstance(ours, dict) or ours.get("deterministic") is not True:
            fail(f"our x producer is not deterministic for contract {index}")
        if not isinstance(mkl, dict):
            fail(f"oneMKL correctness result missing for contract {index}")
        if diffusion == 0.0:
            if mkl.get("status") != "UNSUPPORTED_ZERO_STANDARD_DEVIATION":
                fail("oneMKL zero-standard-deviation behavior changed")
        elif mkl.get("status") != "PASS" or mkl.get("deterministic") is not True:
            fail(f"oneMKL correctness failed for contract {index}")

    timings = payload.get("timings")
    if not isinstance(timings, list):
        fail("oneMKL Sobol-to-x timings must be a list")
    rows: list[dict[str, object]] = []
    seen: set[tuple[int, str, str]] = set()
    medians: dict[tuple[int, str, str], int] = {}
    for item in timings:
        if not isinstance(item, dict):
            fail("oneMKL Sobol-to-x timing record must be an object")
        try:
            contract = int(item["contract"])
            mode = str(item["mode"])
            candidate = str(item["candidate"])
            median = int(item["median_cycles_per_block"])
            p10 = int(item["p10"])
            p90 = int(item["p90"])
            per_value = float(item["median_cycles_per_value"])
            raw = [int(value) for value in item["raw_cycles"]]
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed oneMKL Sobol-to-x timing record: {item}: {exc}")
        if contract not in range(len(ONEMKL_X_CONTRACTS)):
            fail(f"unexpected oneMKL Sobol-to-x contract: {contract}")
        if mode not in ONEMKL_X_MODES or candidate not in ONEMKL_X_CANDIDATES:
            fail(f"unexpected oneMKL Sobol-to-x candidate/mode: {candidate}/{mode}")
        if contract == 2 and candidate == "oneMKL_d1_sobol_to_x":
            fail("oneMKL zero-standard-deviation case was timed despite being unsupported")
        if len(raw) != 51:
            fail(f"oneMKL Sobol-to-x {contract}/{candidate}/{mode} has {len(raw)} samples")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"oneMKL Sobol-to-x percentile mismatch for {contract}/{candidate}/{mode}")
        if abs(per_value - median / 4096.0) > 0.000001:
            fail(f"oneMKL Sobol-to-x denominator mismatch for {contract}/{candidate}/{mode}")
        expected_drift, expected_diffusion = ONEMKL_X_CONTRACTS[contract]
        if abs(float(item.get("drift", 99.0)) - expected_drift) > 1e-7:
            fail(f"oneMKL timing drift mismatch for contract {contract}")
        if abs(float(item.get("diffusion", 99.0)) - expected_diffusion) > 1e-7:
            fail(f"oneMKL timing diffusion mismatch for contract {contract}")
        key = (contract, mode, candidate)
        if key in seen:
            fail(f"duplicate oneMKL Sobol-to-x timing: {key}")
        seen.add(key)
        medians[key] = median
        rows.append({"kind": "onemkl_sobol_x_native", "contract": contract,
                     "drift": expected_drift, "diffusion": expected_diffusion,
                     "candidate": candidate, "mode": mode, "median": median,
                     "p10": p10, "p90": p90, "cycles_per_value": per_value,
                     "raw_batches": raw})

    expected = {
        (contract, mode, candidate)
        for contract in range(len(ONEMKL_X_CONTRACTS))
        for mode in ONEMKL_X_MODES
        for candidate in ONEMKL_X_CANDIDATES
        if not (contract == 2 and candidate == "oneMKL_d1_sobol_to_x")
    }
    if seen != expected:
        fail("oneMKL Sobol-to-x coverage mismatch: "
             f"missing={sorted(expected - seen)} extra={sorted(seen - expected)}")

    ratios = payload.get("ratios")
    if not isinstance(ratios, list) or len(ratios) != len(ONEMKL_X_CONTRACTS) * len(ONEMKL_X_MODES):
        fail("oneMKL Sobol-to-x ratio coverage mismatch")
    ratio_seen: set[tuple[int, str]] = set()
    for item in ratios:
        if not isinstance(item, dict):
            fail("oneMKL Sobol-to-x ratio record must be an object")
        try:
            contract = int(item["contract"])
            mode = str(item["mode"])
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed oneMKL Sobol-to-x ratio record: {item}: {exc}")
        key = (contract, mode)
        if key in ratio_seen:
            fail(f"duplicate oneMKL Sobol-to-x ratio: {key}")
        ratio_seen.add(key)
        expected_old = (medians[(contract, mode, "old_corrected_z_then_affine_x")]
                        / medians[(contract, mode, "our_canonical_sobol_to_x")])
        try:
            actual_old = float(item.get("old_over_new"))
        except (TypeError, ValueError) as exc:
            fail(f"malformed old/new Sobol-to-x ratio: {item}: {exc}")
        if abs(actual_old - expected_old) > 0.000000001:
            fail(f"old/new Sobol-to-x ratio mismatch for {contract}/{mode}")

        native_reported = item.get("oneMKL_native_layout_over_new")
        strict_reported = item.get("oneMKL_over_new")
        if contract == 2:
            if native_reported is not None or strict_reported is not None:
                fail("oneMKL comparison reported a zero-sigma vendor ratio")
        else:
            expected_native = (medians[(contract, mode, "oneMKL_d1_sobol_to_x")]
                               / medians[(contract, mode, "our_canonical_sobol_to_x")])
            try:
                actual_native = float(native_reported)
            except (TypeError, ValueError) as exc:
                fail(f"malformed oneMKL native-layout ratio: {item}: {exc}")
            if abs(actual_native - expected_native) > 0.000000001:
                fail(f"oneMKL native-layout ratio mismatch for {contract}/{mode}")
            if compatibility["status"] == "exact_raw_d1_words":
                try:
                    actual_strict = float(strict_reported)
                except (TypeError, ValueError) as exc:
                    fail(f"malformed strict oneMKL ratio: {item}: {exc}")
                if abs(actual_strict - expected_native) > 0.000000001:
                    fail(f"strict oneMKL ratio mismatch for {contract}/{mode}")
            elif strict_reported is not None:
                fail("oneMKL strict ratio reported without exact raw-D1 identity")
    if not isinstance(payload.get("setup_cycles"), dict):
        fail("oneMKL Sobol-to-x report omitted setup timings")
    return rows


def parse_xgrowth1_results(stdout: str) -> dict[str, object]:
    """Validate the complete D5 stored-payload x/growth benchmark report."""
    try:
        report = json.loads(stdout)
    except json.JSONDecodeError as exc:
        fail(f"malformed xgrowth1 JSON output: {exc}")
    if not isinstance(report, dict):
        fail("xgrowth1 output must be a JSON object")
    if report.get("status") != "PASS" or report.get("native_avx512") is not True:
        fail("xgrowth1 benchmark did not report native PASS")
    if report.get("samples") != 51 or report.get("warmups") != 16:
        fail("xgrowth1 benchmark used unexpected sample/warmup counts")
    if report.get("protocols") != list(XGROWTH1_MODES):
        fail("xgrowth1 benchmark used unexpected cache protocols")

    raw_rows = report.get("candidates")
    if not isinstance(raw_rows, list):
        fail("xgrowth1 candidate rows missing")
    rows: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    medians: dict[tuple[str, str], int] = {}
    for raw_row in raw_rows:
        if not isinstance(raw_row, dict):
            fail("xgrowth1 candidate row must be an object")
        try:
            candidate = str(raw_row["name"])
            mode = str(raw_row["mode"])
            median = int(raw_row["median"])
            p10 = int(raw_row["p10"])
            p90 = int(raw_row["p90"])
            cycles_per_packet = float(raw_row["cycles_per_packet"])
            cycles_per_dimension = int(raw_row["cycles_per_dimension"])
            raw = [int(value) for value in raw_row["raw"]]
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed xgrowth1 row: {exc}")
        if candidate not in XGROWTH1_CANDIDATES or mode not in XGROWTH1_MODES:
            fail(f"unexpected xgrowth1 candidate/mode: {candidate}/{mode}")
        key = (candidate, mode)
        if key in seen:
            fail(f"duplicate xgrowth1 row: {key}")
        if len(raw) != 51:
            fail(f"xgrowth1 {candidate}/{mode} has {len(raw)} samples")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"xgrowth1 percentile mismatch: {candidate}/{mode}")
        if abs(cycles_per_packet - median / 128.0) > 0.000001:
            fail(f"xgrowth1 packet denominator mismatch: {candidate}/{mode}")
        if cycles_per_dimension != median:
            fail(f"xgrowth1 dimension denominator mismatch: {candidate}/{mode}")
        seen.add(key)
        medians[key] = median
        rows.append(
            {
                "kind": "xgrowth1_native",
                "candidate": candidate,
                "mode": mode,
                "median": median,
                "p10": p10,
                "p90": p90,
                "cycles_per_packet": cycles_per_packet,
                "cycles_per_dimension": cycles_per_dimension,
                "raw_batches": raw,
            }
        )

    expected = {
        (candidate, mode)
        for candidate in XGROWTH1_CANDIDATES
        for mode in XGROWTH1_MODES
    }
    if seen != expected:
        fail(
            "xgrowth1 coverage mismatch: "
            f"missing={sorted(expected - seen)} extra={sorted(seen - expected)}"
        )

    setup = report.get("setup")
    if not isinstance(setup, dict) or set(setup) != {"growth", "dual"}:
        fail("xgrowth1 setup measurements missing")
    for name in ("growth", "dual"):
        row = setup[name]
        if not isinstance(row, dict):
            fail(f"xgrowth1 {name} setup row malformed")
        try:
            p10, median, p90 = int(row["p10"]), int(row["median"]), int(row["p90"])
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"xgrowth1 {name} setup row malformed: {exc}")
        if not p10 <= median <= p90:
            fail(f"xgrowth1 {name} setup percentile order invalid")

    incremental = report.get("incremental_median_cycles")
    if not isinstance(incremental, dict):
        fail("xgrowth1 incremental medians missing")
    expected_incremental = {
        "warm_x_producer": medians[("canonical_dual_x_growth_producer", "warm")]
        - medians[("canonical_growth_producer", "warm")],
        "warm_x_provider": medians[("dual_affine_provider", "warm")]
        - medians[("growth_affine_provider", "warm")],
        "warm_l_recurrence": medians[("recurrence_sql", "warm")]
        - medians[("recurrence_sq", "warm")],
        "warm_fused_dual_minus_growth": medians[("fused_dual_provider_sql", "warm")]
        - medians[("fused_growth_provider_sq", "warm")],
        "warm_combined_dual_minus_growth": medians[("combined_dual_producer_fused_sql", "warm")]
        - medians[("combined_growth_producer_fused_sq", "warm")],
        "competing_fused_dual_minus_growth": medians[("fused_dual_provider_sql", "competing_32KiB")]
        - medians[("fused_growth_provider_sq", "competing_32KiB")],
        "dual_setup_minus_growth": int(setup["dual"]["median"])
        - int(setup["growth"]["median"]),
    }
    for name, expected_value in expected_incremental.items():
        try:
            actual = int(incremental[name])
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"xgrowth1 incremental field {name} malformed: {exc}")
        if actual != expected_value:
            fail(f"xgrowth1 incremental field mismatch: {name}")

    source = report.get("source_production")
    if not isinstance(source, dict):
        fail("xgrowth1 matched source-production cohort missing")
    if source.get("candidate_order") != list(XGROWTH1_SOURCE_CANDIDATES):
        fail("xgrowth1 matched source candidate order changed")
    if source.get("scope") != (
        "canonical indices 8192..12287; source production only; "
        "no D5 permutation, recurrence, setup, or payoff timed"
    ):
        fail("xgrowth1 matched source scope changed")

    source_raw_rows = source.get("candidates")
    if not isinstance(source_raw_rows, list):
        fail("xgrowth1 matched source rows missing")
    source_seen: set[tuple[str, str]] = set()
    source_medians: dict[tuple[str, str], int] = {}
    for raw_row in source_raw_rows:
        if not isinstance(raw_row, dict):
            fail("xgrowth1 matched source row must be an object")
        try:
            candidate = str(raw_row["name"])
            mode = str(raw_row["mode"])
            median = int(raw_row["median"])
            p10 = int(raw_row["p10"])
            p90 = int(raw_row["p90"])
            cycles_per_block = int(raw_row["cycles_per_block"])
            cycles_per_value = float(raw_row["cycles_per_value"])
            raw = [int(value) for value in raw_row["raw"]]
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed xgrowth1 matched source row: {exc}")
        if candidate not in XGROWTH1_SOURCE_CANDIDATES or mode not in XGROWTH1_MODES:
            fail(f"unexpected xgrowth1 matched source row: {candidate}/{mode}")
        key = (candidate, mode)
        if key in source_seen:
            fail(f"duplicate xgrowth1 matched source row: {key}")
        if len(raw) != 51:
            fail(f"xgrowth1 matched source {candidate}/{mode} has {len(raw)} samples")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"xgrowth1 matched source percentile mismatch: {candidate}/{mode}")
        if cycles_per_block != median:
            fail(f"xgrowth1 matched source block denominator mismatch: {candidate}/{mode}")
        if abs(cycles_per_value - median / 4096.0) > 0.000001:
            fail(f"xgrowth1 matched source value denominator mismatch: {candidate}/{mode}")
        source_seen.add(key)
        source_medians[key] = median
        rows.append(
            {
                "kind": "xgrowth1_source_native",
                "candidate": candidate,
                "mode": mode,
                "median": median,
                "p10": p10,
                "p90": p90,
                "cycles_per_block": cycles_per_block,
                "cycles_per_value": cycles_per_value,
                "raw_batches": raw,
            }
        )
    source_expected = {
        (candidate, mode)
        for candidate in XGROWTH1_SOURCE_CANDIDATES
        for mode in XGROWTH1_MODES
    }
    if source_seen != source_expected:
        fail(
            "xgrowth1 matched source coverage mismatch: "
            f"missing={sorted(source_expected - source_seen)} "
            f"extra={sorted(source_seen - source_expected)}"
        )

    ratios = source.get("ratios_and_delta")
    if not isinstance(ratios, dict) or set(ratios) != set(XGROWTH1_MODES):
        fail("xgrowth1 matched source ratios missing")
    for mode in XGROWTH1_MODES:
        row = ratios[mode]
        if not isinstance(row, dict):
            fail(f"xgrowth1 matched source ratio row malformed: {mode}")
        old = source_medians[("old_growth_source", mode)]
        new_growth = source_medians[("new_growth_source", mode)]
        new_dual = source_medians[("new_dual_source", mode)]
        try:
            old_over_growth = float(row["old_over_new_growth"])
            old_over_dual = float(row["old_over_new_dual"])
            dual_delta = int(row["new_dual_minus_new_growth"])
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"xgrowth1 matched source ratio row malformed: {mode}: {exc}")
        if abs(old_over_growth - old / new_growth) > 0.000000001:
            fail(f"xgrowth1 old/new-growth ratio mismatch: {mode}")
        if abs(old_over_dual - old / new_dual) > 0.000000001:
            fail(f"xgrowth1 old/new-dual ratio mismatch: {mode}")
        if dual_delta != new_dual - new_growth:
            fail(f"xgrowth1 dual-growth delta mismatch: {mode}")

    correctness = source.get("correctness")
    if not isinstance(correctness, dict):
        fail("xgrowth1 matched source correctness report missing")
    limits = correctness.get("growth_relative_limits")
    if limits != {
        "old_p8_vs_exp_of_frozen_z": 4e-7,
        "new_growth_complete": 3e-7,
        "new_dual_complete": 3e-7,
    }:
        fail("xgrowth1 matched source growth limits changed")
    frozen = correctness.get("frozen_z_limits")
    if not isinstance(frozen, dict) or frozen != {
        "all_max_absolute": 0.013,
        "hard_max_absolute": 5e-7,
        "runtime_bit_mismatches_vs_fixture": 0,
    }:
        fail("xgrowth1 frozen Z contract or fixture identity failed")
    try:
        dual_x_limit = float(correctness["dual_x_absolute_limit"])
        dual_x_max = float(correctness["dual_x_max_absolute"])
    except (KeyError, TypeError, ValueError) as exc:
        fail(f"xgrowth1 dual x correctness malformed: {exc}")
    if not math.isfinite(dual_x_max) or dual_x_max > dual_x_limit:
        fail("xgrowth1 dual x correctness gate failed")

    pairwise = correctness.get("pairwise")
    if not isinstance(pairwise, dict):
        fail("xgrowth1 pairwise correctness missing")
    new_pair = pairwise.get("new_growth_vs_new_dual")
    if new_pair != {"bit_identical": True, "mismatches": 0, "max_ulp": 0}:
        fail("xgrowth1 new growth and dual growth are not bit-identical")
    for name in ("old_vs_new_growth", "old_vs_new_dual"):
        item = pairwise.get(name)
        if not isinstance(item, dict) or item.get("bit_identical") is not False:
            fail(f"xgrowth1 expected old/new distinction missing: {name}")
        if int(item.get("mismatches", 0)) <= 0 or int(item.get("max_ulp", 0)) <= 0:
            fail(f"xgrowth1 expected old/new error metrics missing: {name}")

    accuracy_rows = correctness.get("candidates")
    if not isinstance(accuracy_rows, list):
        fail("xgrowth1 source accuracy rows missing")
    accuracy_by_name: dict[str, dict[str, object]] = {}
    for row in accuracy_rows:
        if not isinstance(row, dict) or str(row.get("name")) in accuracy_by_name:
            fail("xgrowth1 source accuracy row malformed or duplicated")
        accuracy_by_name[str(row.get("name"))] = row
        for field in (
            "complete_growth_max_relative_vs_true_mpfr",
            "local_exp_max_relative",
            "hard_growth_max_relative_vs_true_mpfr",
            "call_price",
            "call_reference",
            "call_error",
            "put_price",
            "put_reference",
            "put_error",
        ):
            try:
                value = float(row[field])
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"xgrowth1 source accuracy field malformed: {field}: {exc}")
            if not math.isfinite(value):
                fail(f"xgrowth1 source accuracy field non-finite: {field}")
        if int(row.get("exercise_decision_flips", -1)) != 0:
            fail(f"xgrowth1 source exercise flips: {row.get('name')}")
    if set(accuracy_by_name) != set(XGROWTH1_SOURCE_CANDIDATES):
        fail("xgrowth1 source accuracy candidate coverage mismatch")
    if float(accuracy_by_name["old_growth_source"]["local_exp_max_relative"]) > 4e-7:
        fail("xgrowth1 old local P8 gate failed")
    for name in ("new_growth_source", "new_dual_source"):
        if float(accuracy_by_name[name]["complete_growth_max_relative_vs_true_mpfr"]) > 3e-7:
            fail(f"xgrowth1 complete true-MPFR growth gate failed: {name}")

    nominal = report.get("nominal_bytes")
    expected_nominal = {
        "growth_source": 16384,
        "dual_sources": 32768,
        "affine_map": 448,
        "sq_state": 32768,
        "sql_state": 49152,
        "carrier_context": 181568,
        "diagnostic_context": 182080,
    }
    if nominal != expected_nominal:
        fail("xgrowth1 nominal footprint report mismatch")
    return {"report": report, "rows": rows}


def parse_synthetic_all_permute_results(payload: object) -> list[dict[str, object]]:
    """Validate the bounded synthetic scaling report without upgrading its claim."""
    if not isinstance(payload, dict) or payload.get("status") != "PASS":
        fail("synthetic all-permute benchmark did not report PASS")
    expected_scope = (
        "synthetic_all_permute hardware scaling only; repeated certified maps "
        "are not valid D2-D256 Asian simulation"
    )
    if payload.get("scope") != expected_scope:
        fail("synthetic all-permute scope/disclaimer changed")
    if payload.get("direction_table_sha256") != (
        "fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199"
    ):
        fail("synthetic all-permute direction table changed")
    if payload.get("oneMKL_skip_scalar_components") != "8192*N":
        fail("synthetic all-permute oneMKL skip contract changed")
    if payload.get("oneMKL_reference_gray_indices_after_skip") != (
        "8193..12288 (oneMKL u1-based recurrence)"
    ):
        fail("synthetic all-permute oneMKL index convention changed")
    if payload.get("warmups") != 16 or payload.get("samples") != 51:
        fail("synthetic all-permute timing protocol changed")
    dimension_hashes = payload.get("dimension_word_sha256")
    if not isinstance(dimension_hashes, list) or len(dimension_hashes) != 256:
        fail("synthetic all-permute dimension hash coverage mismatch")
    if any(
        not isinstance(value, str)
        or len(value) != 64
        or any(ch not in "0123456789abcdef" for ch in value)
        for value in dimension_hashes
    ):
        fail("synthetic all-permute dimension hash is malformed")

    cases = payload.get("cases")
    if not isinstance(cases, list) or len(cases) != len(SYNTHETIC_ALL_PERMUTE_NS):
        fail("synthetic all-permute case coverage mismatch")
    for n, case in zip(SYNTHETIC_ALL_PERMUTE_NS, cases):
        if not isinstance(case, dict) or case.get("N") != n:
            fail(f"synthetic all-permute malformed case N={n}")
        if case.get("label") != "synthetic_all_permute":
            fail(f"synthetic all-permute disclaimer missing for N={n}")
        if case.get("mkl_available") is not True or case.get("mkl_proof") not in {
            "raw_uniformbits",
            "float_confirmed_x_over_2pow32",
        }:
            fail(f"synthetic all-permute oneMKL proof failed for N={n}")
        expected_bytes = {
            "output_bytes": n * 16384,
            "hot_schedule_bytes": (n - 1) * 16,
            "cold_context_bytes": (n - 1) * 448,
        }
        for key, expected in expected_bytes.items():
            if case.get(key) != expected:
                fail(f"synthetic all-permute {key} mismatch for N={n}")

    records = payload.get("results")
    if not isinstance(records, list):
        fail("synthetic all-permute results must be a list")
    rows: list[dict[str, object]] = []
    seen: set[tuple[int, str, str]] = set()
    for item in records:
        if not isinstance(item, dict):
            fail("synthetic all-permute result is malformed")
        try:
            n = int(item["N"])
            candidate = str(item["candidate"])
            mode = str(item["mode"])
            median = int(item["median"])
            p10 = int(item["p10"])
            p90 = int(item["p90"])
            cycles_per_path_fixing = float(item["cycles_per_path_fixing"])
            raw = [int(value) for value in item["raw"]]
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed synthetic all-permute result: {item}: {exc}")
        if n not in SYNTHETIC_ALL_PERMUTE_NS:
            fail(f"unexpected synthetic all-permute N={n}")
        if candidate not in SYNTHETIC_ALL_PERMUTE_CANDIDATES:
            fail(f"unexpected synthetic all-permute candidate: {candidate}")
        if mode not in SYNTHETIC_ALL_PERMUTE_MODES:
            fail(f"unexpected synthetic all-permute mode: {mode}")
        key = (n, mode, candidate)
        if key in seen:
            fail(f"duplicate synthetic all-permute result: {key}")
        seen.add(key)
        if len(raw) != 51:
            fail(f"synthetic all-permute {key} has {len(raw)} samples")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"synthetic all-permute percentile mismatch: {key}")
        expected_rate = median / (4096.0 * n)
        if abs(cycles_per_path_fixing - expected_rate) > 0.000000001:
            fail(f"synthetic all-permute denominator mismatch: {key}")
        if abs(float(item.get("cycles_per_x", -1.0)) - expected_rate) > 0.000000001:
            fail(f"synthetic all-permute cycles-per-x mismatch: {key}")
        rows.append(
            {
                "kind": "synthetic_all_permute_native",
                "N": n,
                "candidate": candidate,
                "mode": mode,
                "median": median,
                "p10": p10,
                "p90": p90,
                "cycles_per_path_fixing": cycles_per_path_fixing,
                "raw_batches": raw,
            }
        )
    expected = {
        (n, mode, candidate)
        for n in SYNTHETIC_ALL_PERMUTE_NS
        for mode in SYNTHETIC_ALL_PERMUTE_MODES
        for candidate in SYNTHETIC_ALL_PERMUTE_CANDIDATES
    }
    if seen != expected:
        fail(
            "synthetic all-permute coverage mismatch: "
            f"missing={sorted(expected - seen)} extra={sorted(seen - expected)}"
        )
    fit = payload.get("descriptive_warm_fit")
    if not isinstance(fit, dict) or fit.get("raw_medians_authoritative") is not True:
        fail("synthetic all-permute fit displaced authoritative raw medians")
    return rows


def parse_genuine_complete_results(payload: object) -> list[dict[str, object]]:
    """Validate the genuine D1--D256 complete Asian benchmark report."""
    if not isinstance(payload, dict) or payload.get("status") != "PASS":
        fail("genuine complete benchmark did not report PASS")
    if payload.get("warmups") != 16 or payload.get("samples") != 51:
        fail("genuine complete timing protocol changed")
    results = payload.get("results")
    if not isinstance(results, list):
        fail("genuine complete results must be a list")

    rows: list[dict[str, object]] = []
    ratios: list[dict[str, object]] = []
    seen: set[tuple[int, str, str]] = set()
    medians: dict[tuple[int, str, str], int] = {}
    for item in results:
        if not isinstance(item, dict):
            fail("genuine complete result row must be an object")
        try:
            n = int(item["N"])
            mode = str(item["mode"])
            candidate = str(item["candidate"])
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed genuine complete row: {item}: {exc}")
        if n not in GENUINE_COMPLETE_NS or mode not in GENUINE_COMPLETE_MODES:
            fail(f"unexpected genuine complete N/mode: {n}/{mode}")
        if candidate == "complete_ratios":
            ratios.append(item)
            continue
        if candidate not in GENUINE_COMPLETE_CANDIDATES:
            fail(f"unexpected genuine complete candidate: {candidate}")
        try:
            p10 = int(item["p10"])
            median = int(item["median"])
            p90 = int(item["p90"])
            ticks_per_path = float(item["ticks_per_path"])
            ticks_per_path_fixing = float(item["ticks_per_path_fixing"])
            raw = [int(value) for value in item["raw"]]
        except (KeyError, TypeError, ValueError) as exc:
            fail(f"malformed genuine complete timing: {item}: {exc}")
        if len(raw) != 51:
            fail(f"genuine complete {n}/{mode}/{candidate} has {len(raw)} samples")
        ordered = sorted(raw)
        if (p10, median, p90) != (ordered[5], ordered[25], ordered[45]):
            fail(f"genuine complete percentile mismatch: {n}/{mode}/{candidate}")
        if abs(ticks_per_path - median / 4096.0) > 0.000001:
            fail(f"genuine complete path denominator mismatch: {n}/{mode}/{candidate}")
        if abs(ticks_per_path_fixing - median / 4096.0 / n) > 0.000001:
            fail(f"genuine complete fixing denominator mismatch: {n}/{mode}/{candidate}")
        key = (n, mode, candidate)
        if key in seen:
            fail(f"duplicate genuine complete timing: {key}")
        seen.add(key)
        medians[key] = median
        rows.append(
            {
                "kind": "genuine_complete_native",
                "N": n,
                "candidate": candidate,
                "mode": mode,
                "median": median,
                "p10": p10,
                "p90": p90,
                "ticks_per_path": ticks_per_path,
                "ticks_per_path_fixing": ticks_per_path_fixing,
                "raw_batches": raw,
            }
        )

    expected = {
        (n, mode, candidate)
        for n in GENUINE_COMPLETE_NS
        for mode in GENUINE_COMPLETE_MODES
        for candidate in GENUINE_COMPLETE_CANDIDATES
    }
    if seen != expected:
        fail(
            "genuine complete coverage mismatch: "
            f"missing={sorted(expected - seen)} extra={sorted(seen - expected)}"
        )
    if len(ratios) != len(GENUINE_COMPLETE_NS) * len(GENUINE_COMPLETE_MODES):
        fail("genuine complete ratio coverage mismatch")
    ratio_seen: set[tuple[int, str]] = set()
    for item in ratios:
        n = int(item["N"])
        mode = str(item["mode"])
        key = (n, mode)
        if key in ratio_seen:
            fail(f"duplicate genuine complete ratio: {key}")
        ratio_seen.add(key)
        ours = medians[(n, mode, "our_complete_geometric_cv")]
        point = medians[(n, mode, "intel_point_complete_geometric_cv")]
        tiled = medians[(n, mode, "intel_tiled_complete_geometric_cv")]
        if abs(float(item["intel_point_over_ours"]) - point / ours) > 0.000000001:
            fail(f"genuine complete point ratio mismatch: {key}")
        if abs(float(item["intel_tiled_over_ours"]) - tiled / ours) > 0.000000001:
            fail(f"genuine complete tiled ratio mismatch: {key}")
        if abs(float(item["our_price_error"])) > 0.0001:
            fail(f"our genuine complete price error exceeds gate: {key}")
        if abs(float(item["intel_price_error"])) > 0.0001:
            fail(f"Intel genuine complete price error exceeds gate: {key}")
        if int(item["source_payload_bytes"]) != 32768:
            fail(f"genuine complete source footprint changed: {key}")
        if int(item["map_bytes"]) != n * 1600:
            fail(f"genuine complete map footprint changed: {key}")
        if int(item["hot_route_bytes"]) != n * 32:
            fail(f"genuine complete route footprint changed: {key}")
        rows.append({"kind": "genuine_complete_ratio", **item})
    return rows


def sanitize(name: str) -> str:
    out = []
    for ch in name.lower():
        if ch.isalnum():
            out.append(ch)
        else:
            out.append("_")
    text = "".join(out).strip("_")
    while "__" in text:
        text = text.replace("__", "_")
    return text or "cpu"


def load_metadata(path: Path) -> dict[str, object]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"{path.name} unreadable: {exc}")
    if not isinstance(payload, dict):
        fail(f"{path.name} must be an object")
    return payload


def pick_metric(
    rows: list[dict[str, object]],
    env: str,
    candidate: str,
    metric: str,
    kind: str | None = None,
) -> float | None:
    for row in rows:
        if kind is not None and row.get("kind") != kind:
            continue
        if (
            row.get("kind") in {"measured_native", "derived_native"}
            and row.get("env") == env
            and row.get("candidate") == candidate
            and row.get("metric") == metric
        ):
            try:
                return float(row["value"])
            except (KeyError, TypeError, ValueError):
                return None
    return None


def fmt(x: float | None) -> str:
    return "n/a" if x is None else f"{x:.3f}"


def asian_verdict(cycles: float | None) -> str:
    if cycles is None:
        return "UNSUPPORTED"
    if cycles <= 6:
        return "EXCELLENT"
    if cycles <= 8:
        return "REVIEW"
    return "INVESTIGATE"


def dim_verdict(cycles: float | None) -> str:
    if cycles is None:
        return "UNSUPPORTED"
    if cycles <= 3:
        return "EXCELLENT"
    if cycles <= 5:
        return "GOOD"
    if cycles <= 7:
        return "REVIEW"
    return "INVESTIGATE"


def print_asian_table(rows: list[dict[str, object]] | None) -> None:
    print("ASIAN W2 (cycles / 32-path-fixing; not a winner pick)")
    hdr = f"{'variant':<14} {'env':<16} {'cyc/32fix':>10} {'cyc/4096':>10} {'cyc/4096x32':>12} {'verdict':<12}"
    print(hdr)
    if rows is None:
        print(f"{'all':<14} {'n/a':<16} {'n/a':>10} {'n/a':>10} {'n/a':>12} {'UNSUPPORTED':<12}")
        return
    for env in ("warm_L1D", "pressure_32KiB"):
        for cand in ASIAN_CANDIDATES:
            c32 = pick_metric(rows, env, cand, "cycles_per_32path_fixing", kind="derived_native")
            if c32 is None:
                c32 = pick_metric(rows, env, cand, "cycles_per_32path_fixing")
            c4096 = pick_metric(rows, env, cand, "cycles_per_4096_state_fixing")
            call = pick_metric(rows, env, cand, "cycles_4096_by_32fix")
            print(
                f"{cand:<14} {env:<16} {fmt(c32):>10} {fmt(c4096):>10} {fmt(call):>12} {asian_verdict(c32):<12}"
            )


def print_dim_table(rows: list[dict[str, object]] | None) -> None:
    print("DIM dest-order real-map aggregate (18 dimensions; not a winner pick)")
    print(
        f"{'candidate':<10} {'env':<16} {'median block':>13} {'median pkt':>11} "
        f"{'worst dim':>9} {'worst block':>12} {'worst pkt':>10} {'verdict':<12}"
    )
    if rows is None:
        print(
            f"{'all':<10} {'n/a':<16} {'n/a':>13} {'n/a':>11} "
            f"{'n/a':>9} {'n/a':>12} {'n/a':>10} {'UNSUPPORTED':<12}"
        )
        return
    for env in ("warm_L1D", "pressure_32KiB"):
        for cand in DIM_CANDIDATES:
            matches = [
                row
                for row in rows
                if row.get("kind") == "dim_aggregate_native"
                and row.get("variant") == cand
                and row.get("mode") == env
            ]
            if len(matches) != 1:
                fail(f"missing dimension aggregate for {cand}/{env}")
            row = matches[0]
            block = float(str(row["real_median_cyc"]))
            pkt = float(str(row["real_median_cyc_per_packet"]))
            worst_dim = int(str(row["worst_dim"]))
            worst_block = float(str(row["worst_median_cyc"]))
            worst_pkt = float(str(row["worst_cyc_per_packet"]))
            print(
                f"{cand:<10} {env:<16} {block:>13.1f} {pkt:>11.3f} "
                f"{worst_dim:>9} {worst_block:>12.1f} {worst_pkt:>10.3f} {dim_verdict(pkt):<12}"
            )


def print_affine18_table(rows: list[dict[str, object]] | None) -> None:
    print("AFFINE18 fused chronological diagnostic (incomplete 18 steps)")
    print(
        f"{'candidate':<45} {'mode':<18} {'median':>10} {'p10':>10} "
        f"{'p90':>10} {'cyc/step':>10}"
    )
    if rows is None:
        print(f"{'all':<45} {'n/a':<18} {'n/a':>10} {'n/a':>10} {'n/a':>10} {'n/a':>10}")
        return
    for mode in AFFINE18_MODES:
        for candidate in AFFINE18_CANDIDATES:
            matches = [
                row
                for row in rows
                if row.get("candidate") == candidate and row.get("mode") == mode
            ]
            if len(matches) != 1:
                fail(f"missing affine18 result for {candidate}/{mode}")
            row = matches[0]
            print(
                f"{candidate:<45} {mode:<18} {int(row['median']):>10} "
                f"{int(row['p10']):>10} {int(row['p90']):>10} "
                f"{float(row['cycles_per_step']):>10.3f}"
            )


def print_growth18_table(rows: list[dict[str, object]] | None) -> None:
    print("GROWTH18 affine payload-commutation diagnostic (incomplete 18 steps)")
    print(
        f"{'candidate':<48} {'mode':<18} {'median':>10} {'p10':>10} "
        f"{'p90':>10} {'normalized metric':>22}"
    )
    if rows is None:
        print(
            f"{'all':<48} {'n/a':<18} {'n/a':>10} {'n/a':>10} "
            f"{'n/a':>10} {'UNSUPPORTED':>22}"
        )
        return
    for mode in AFFINE18_MODES:
        for candidate in GROWTH18_CANDIDATES:
            matches = [
                row
                for row in rows
                if row.get("kind") == "growth18_native"
                and row.get("candidate") == candidate
                and row.get("mode") == mode
            ]
            if len(matches) != 1:
                fail(f"missing growth18 result for {candidate}/{mode}")
            row = matches[0]
            metric = str(row["metric"]).removeprefix("cycles_per_")
            normalized = f"{float(row['metric_value']):.6f} cyc/{metric}"
            print(
                f"{candidate:<48} {mode:<18} {int(row['median']):>10} "
                f"{int(row['p10']):>10} {int(row['p90']):>10} {normalized:>22}"
            )


def print_conditional18_table(rows: list[dict[str, object]] | None) -> None:
    print("CONDITIONAL18 exact first-increment diagnostic (incomplete 18 routes, dt=T/32)")
    print(
        f"{'candidate':<48} {'mode':<18} {'median':>10} {'p10':>10} {'p90':>10}"
    )
    if rows is None:
        print(f"{'all':<48} {'n/a':<18} {'n/a':>10} {'n/a':>10} {'n/a':>10}")
        return
    for mode in AFFINE18_MODES:
        for candidate in CONDITIONAL18_CANDIDATES:
            matches = [
                row
                for row in rows
                if row.get("candidate") == candidate and row.get("mode") == mode
            ]
            if len(matches) != 1:
                fail(f"missing conditional18 result for {candidate}/{mode}")
            row = matches[0]
            print(
                f"{candidate:<48} {mode:<18} {int(row['median']):>10} "
                f"{int(row['p10']):>10} {int(row['p90']):>10}"
            )


def print_conditional_payoff_table(rows: list[dict[str, object]] | None) -> None:
    print("CONDITIONAL PAYOFF vector log/CDF and fused diagnostic (incomplete 18 routes)")
    print(
        f"{'candidate':<44} {'mode':<18} {'median':>10} {'p10':>10} "
        f"{'p90':>10} {'cyc/path':>12}"
    )
    if rows is None:
        print(f"{'all':<44} {'n/a':<18} {'n/a':>10} {'n/a':>10} {'n/a':>10} {'n/a':>12}")
        return
    for row in rows:
        if row.get("kind") == "conditional_payoff_accuracy":
            print(
                "accuracy "
                f"libm_reference={float(row['libm_reference']):.9g} "
                f"lut1024_abs_error={float(row['lut1024_abs_error']):.9g} "
                f"lut2048_abs_error={float(row['lut2048_abs_error']):.9g} "
                f"lut4096_abs_error={float(row['lut4096_abs_error']):.9g} "
                f"threshold={float(row['threshold']):.9g}"
            )
    for mode in AFFINE18_MODES:
        for candidate in CONDITIONAL_PAYOFF_CANDIDATES:
            matches = [
                row
                for row in rows
                if row.get("candidate") == candidate and row.get("mode") == mode
            ]
            if len(matches) != 1:
                fail(f"missing conditional payoff result for {candidate}/{mode}")
            row = matches[0]
            print(
                f"{candidate:<44} {mode:<18} {int(row['median']):>10} "
                f"{int(row['p10']):>10} {int(row['p90']):>10} "
                f"{float(row['cycles_per_path']):>12.6f}"
            )


def print_sql18_table(rows: list[dict[str, object]] | None) -> None:
    print("SQL18 weighted S/Q/L and geometric-control diagnostic (partial 18 routes)")
    print(
        f"{'candidate':<42} {'mode':<18} {'median':>10} {'p10':>10} "
        f"{'p90':>10} {'cyc/pkt-fix':>12}"
    )
    if rows is None:
        print(f"{'all':<42} {'n/a':<18} {'n/a':>10} {'n/a':>10} {'n/a':>10} {'n/a':>12}")
        return
    for mode in SQL18_MODES:
        for candidate in SQL18_CANDIDATES:
            matches = [
                row
                for row in rows
                if row.get("kind") == "sql18_native"
                and row.get("candidate") == candidate
                and row.get("mode") == mode
            ]
            if len(matches) != 1:
                fail(f"missing sql18 result for {candidate}/{mode}")
            row = matches[0]
            print(
                f"{candidate:<42} {mode:<18} {int(row['median']):>10} "
                f"{int(row['p10']):>10} {int(row['p90']):>10} "
                f"{float(row['cycles_per_packet_fixing']):>12.6f}"
            )


def print_onemkl_x_table(rows: list[dict[str, object]] | None) -> None:
    print("Matched old/new ordered-D1 x production with oneMKL D1 native context")
    print(f"{'contract':<9} {'candidate':<30} {'mode':<18} {'median':>10} "
          f"{'p10':>10} {'p90':>10} {'cyc/value':>12}")
    if rows is None:
        print(f"{'all':<9} {'all':<30} {'n/a':<18} {'n/a':>10} "
              f"{'n/a':>10} {'n/a':>10} {'n/a':>12}")
        return
    for contract in range(len(ONEMKL_X_CONTRACTS)):
        for mode in ONEMKL_X_MODES:
            for candidate in ONEMKL_X_CANDIDATES:
                matches = [row for row in rows
                           if row.get("kind") == "onemkl_sobol_x_native"
                           and row.get("contract") == contract
                           and row.get("candidate") == candidate
                           and row.get("mode") == mode]
                if contract == 2 and candidate == "oneMKL_d1_sobol_to_x":
                    if matches:
                        fail("oneMKL zero-standard-deviation timing unexpectedly present")
                    continue
                if len(matches) != 1:
                    fail(f"missing oneMKL Sobol-to-x result for {contract}/{candidate}/{mode}")
                row = matches[0]
                print(f"{contract:<9} {candidate:<30} {mode:<18} "
                      f"{int(row['median']):>10} {int(row['p10']):>10} "
                      f"{int(row['p90']):>10} {float(row['cycles_per_value']):>12.6f}")


def print_synthetic_all_permute_table(rows: list[dict[str, object]] | None) -> None:
    print("Synthetic all-permute scaling ceiling — NOT a valid multidimensional Asian simulation")
    print(
        f"{'N':>4} {'candidate':<42} {'mode':<32} {'median':>10} "
        f"{'p10':>10} {'p90':>10} {'TSC/path-fix':>14}"
    )
    if rows is None:
        print(
            f"{'n/a':>4} {'n/a':<42} {'n/a':<32} {'n/a':>10} "
            f"{'n/a':>10} {'n/a':>10} {'n/a':>14}"
        )
        return
    for n in SYNTHETIC_ALL_PERMUTE_NS:
        for mode in SYNTHETIC_ALL_PERMUTE_MODES:
            for candidate in SYNTHETIC_ALL_PERMUTE_CANDIDATES:
                matches = [
                    row
                    for row in rows
                    if row.get("N") == n
                    and row.get("candidate") == candidate
                    and row.get("mode") == mode
                ]
                if len(matches) != 1:
                    fail(f"missing synthetic all-permute result: {n}/{mode}/{candidate}")
                row = matches[0]
                print(
                    f"{n:>4} {candidate:<42} {mode:<32} "
                    f"{int(row['median']):>10} {int(row['p10']):>10} "
                    f"{int(row['p90']):>10} "
                    f"{float(row['cycles_per_path_fixing']):>14.9f}"
                )


def print_xgrowth1_table(rows: list[dict[str, object]] | None) -> None:
    print("XGROWTH1 D5 stored-payload x/growth and S/Q/L diagnostic")
    print(
        f"{'candidate':<44} {'mode':<18} {'median':>10} {'p10':>10} "
        f"{'p90':>10} {'cyc/packet':>12}"
    )
    if rows is None:
        print(f"{'all':<44} {'n/a':<18} {'n/a':>10} {'n/a':>10} {'n/a':>10} {'n/a':>12}")
        return
    for mode in XGROWTH1_MODES:
        for candidate in XGROWTH1_CANDIDATES:
            matches = [
                row
                for row in rows
                if row.get("candidate") == candidate and row.get("mode") == mode
            ]
            if len(matches) != 1:
                fail(f"missing xgrowth1 result for {candidate}/{mode}")
            row = matches[0]
            print(
                f"{candidate:<44} {mode:<18} {int(row['median']):>10} "
                f"{int(row['p10']):>10} {int(row['p90']):>10} "
                f"{float(row['cycles_per_packet']):>12.6f}"
            )
    print()
    print("XGROWTH1 matched canonical source production")
    print(
        f"{'candidate':<28} {'mode':<18} {'median':>10} {'p10':>10} "
        f"{'p90':>10} {'cyc/value':>12}"
    )
    for mode in XGROWTH1_MODES:
        mode_rows: dict[str, dict[str, object]] = {}
        for candidate in XGROWTH1_SOURCE_CANDIDATES:
            matches = [
                row
                for row in rows
                if row.get("kind") == "xgrowth1_source_native"
                and row.get("candidate") == candidate
                and row.get("mode") == mode
            ]
            if len(matches) != 1:
                fail(f"missing xgrowth1 matched source result for {candidate}/{mode}")
            row = matches[0]
            mode_rows[candidate] = row
            print(
                f"{candidate:<28} {mode:<18} {int(row['median']):>10} "
                f"{int(row['p10']):>10} {int(row['p90']):>10} "
                f"{float(row['cycles_per_value']):>12.6f}"
            )
        old = int(mode_rows["old_growth_source"]["median"])
        growth = int(mode_rows["new_growth_source"]["median"])
        dual = int(mode_rows["new_dual_source"]["median"])
        print(
            f"source_summary mode={mode} old/new_growth={old / growth:.6f} "
            f"old/new_dual={old / dual:.6f} dual-growth={dual - growth}"
        )


def print_genuine_complete_table(rows: list[dict[str, object]] | None) -> None:
    print("Genuine Joe-Kuo complete Asian benchmark")
    print(
        f"{'N':>4} {'candidate':<42} {'mode':<30} {'median':>10} "
        f"{'p10':>10} {'p90':>10} {'ticks/path-fix':>15}"
    )
    if rows is None:
        print(f"{'n/a':>4} {'n/a':<42} {'n/a':<30} {'n/a':>10} {'n/a':>10} {'n/a':>10} {'n/a':>15}")
        return
    for n in GENUINE_COMPLETE_NS:
        for mode in GENUINE_COMPLETE_MODES:
            for candidate in GENUINE_COMPLETE_CANDIDATES:
                matches = [
                    row
                    for row in rows
                    if row.get("kind") == "genuine_complete_native"
                    and row.get("N") == n
                    and row.get("candidate") == candidate
                    and row.get("mode") == mode
                ]
                if len(matches) != 1:
                    fail(f"missing genuine complete result: {n}/{mode}/{candidate}")
                row = matches[0]
                print(
                    f"{n:>4} {candidate:<42} {mode:<30} "
                    f"{int(row['median']):>10} {int(row['p10']):>10} "
                    f"{int(row['p90']):>10} {float(row['ticks_per_path_fixing']):>15.9f}"
                )


def run_asian_suite(
    name: str,
    test_bin: Path,
    bench_bin: Path,
    pass_banner: str,
    root: Path,
) -> tuple[str, str, list[dict[str, object]]]:
    print(f"ldd {test_bin.name}:")
    print(ldd(test_bin))
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))
    test = run_bin(test_bin, root)
    sys.stderr.write(test.stderr)
    if test.returncode != 0 or pass_banner not in test.stdout.splitlines():
        sys.stdout.write(test.stdout)
        fail(f"{name} correctness binary failed")
    for line in test.stdout.splitlines():
        if line.startswith("DIM_PERMUTE_TEST") or line.startswith("ASIAN_STATE_TEST") or line.startswith("tests="):
            print(line)
    bench = run_bin(bench_bin, root)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail(f"{name} benchmark binary failed")
    rows = parse_results(bench.stdout)
    if not rows:
        fail(f"{name} benchmark produced no RESULT lines")
    print(f"{name} bench: {len(rows)} RESULT rows (tables below)")
    return test.stdout, bench.stdout, rows


def run_dim_suite(root: Path) -> tuple[str, str, list[dict[str, object]]]:
    test_bin = root / "bin" / "dim_permute_test"
    bench_bin = root / "bin" / "dim_permute_bench"
    print(f"ldd {test_bin.name}:")
    print(ldd(test_bin))
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))

    test = run_bin(test_bin, root)
    sys.stderr.write(test.stderr)
    expected = "dim_provider_test PASS tests=6101 native_avx512=1"
    if test.returncode != 0 or expected not in test.stdout.splitlines():
        sys.stdout.write(test.stdout)
        fail("dim correctness binary failed or reported prepare-only coverage")
    print(expected)

    bench = run_bin(bench_bin, root)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("dim benchmark binary failed")
    rows = parse_dim_results(bench.stdout)
    print(f"dim bench: validated {len(rows)} native records (table below)")
    return test.stdout, bench.stdout, rows


def run_affine18_suite(
    root: Path, selected_cpu: int
) -> tuple[str, list[dict[str, object]]]:
    bench_bin = root / "bin" / "asian_affine_18diag_bench"
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))
    args = () if selected_cpu < 0 else (str(selected_cpu),)
    bench = run_bin(bench_bin, root, args)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("affine18 benchmark or its pre-timing correctness checks failed")
    rows = parse_affine18_results(bench.stdout)
    print(f"affine18 bench: validated {len(rows)} native records (table below)")
    return bench.stdout, rows


def run_growth18_suite(
    root: Path, selected_cpu: int
) -> tuple[str, list[dict[str, object]]]:
    bench_bin = root / "bin" / "asian_affine_growth_18diag_bench"
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))

    check = run_bin(bench_bin, root, ("--check-only",))
    sys.stderr.write(check.stderr)
    expected = "asian_affine_growth_18diag_bench correctness=PASS timing=SKIPPED"
    if check.returncode != 0 or expected not in check.stdout.splitlines():
        sys.stdout.write(check.stdout)
        fail("growth18 standalone correctness gate failed")
    print(expected)

    args = () if selected_cpu < 0 else (str(selected_cpu),)
    bench = run_bin(bench_bin, root, args)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("growth18 benchmark or its repeated pre-timing correctness checks failed")
    rows = parse_growth18_results(bench.stdout)
    native_rows = [row for row in rows if row.get("kind") == "growth18_native"]
    print(f"growth18 bench: validated {len(native_rows)} native records (table below)")
    return bench.stdout, rows


def run_conditional18_suite(
    root: Path,
) -> tuple[str, list[dict[str, object]]]:
    bench_bin = root / "bin" / "asian_affine_conditional_18diag_bench"
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))

    check = run_bin(bench_bin, root, ("--check-only",))
    sys.stderr.write(check.stderr)
    expected = "asian_affine_conditional_18diag_bench correctness=PASS timing=SKIPPED"
    if check.returncode != 0 or expected not in check.stdout.splitlines():
        sys.stdout.write(check.stdout)
        fail("conditional18 standalone correctness gate failed")
    print(expected)

    bench = run_bin(bench_bin, root)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("conditional18 benchmark or its pre-timing correctness checks failed")
    rows = parse_conditional18_results(bench.stdout)
    print(f"conditional18 bench: validated {len(rows)} native records (table below)")
    return bench.stdout, rows


def run_conditional_payoff_suite(
    root: Path,
) -> tuple[str, list[dict[str, object]]]:
    bench_bin = root / "bin" / "asian_conditional_payoff_18diag_bench"
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))

    check = run_bin(bench_bin, root, ("--check-only",))
    sys.stderr.write(check.stderr)
    expected = "asian_conditional_payoff_18diag_bench correctness=PASS timing=SKIPPED"
    if check.returncode != 0 or expected not in check.stdout.splitlines():
        sys.stdout.write(check.stdout)
        fail("conditional payoff standalone correctness gate failed")
    print(expected)

    bench = run_bin(bench_bin, root)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("conditional payoff benchmark or its pre-timing correctness checks failed")
    rows = parse_conditional_payoff_results(bench.stdout)
    native_rows = [row for row in rows if row.get("kind") == "conditional_payoff_native"]
    print(f"conditional payoff bench: validated {len(native_rows)} native records (table below)")
    return bench.stdout, rows


def run_sql18_suite(root: Path) -> tuple[str, list[dict[str, object]]]:
    bench_bin = root / "bin" / "asian_affine_dual_sql_18diag_bench"
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))

    check = run_bin(bench_bin, root, ("--check-only",))
    sys.stderr.write(check.stderr)
    expected = "asian_sql18_bench correctness=PASS timing=SKIPPED"
    if check.returncode != 0 or expected not in check.stdout.splitlines():
        sys.stdout.write(check.stdout)
        fail("sql18 standalone correctness gate failed")
    print(expected)

    bench = run_bin(bench_bin, root)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("sql18 benchmark or its pre-timing correctness checks failed")
    rows = parse_sql18_results(bench.stdout)
    native_rows = [row for row in rows if row.get("kind") == "sql18_native"]
    print(f"sql18 bench: validated {len(native_rows)} native records (table below)")
    return bench.stdout, rows


def run_onemkl_x_suite(
    root: Path,
) -> tuple[str, dict[str, object], list[dict[str, object]]]:
    bench_bin = root / "bin" / "onemkl_sobol_x_bench"
    dependencies = ldd(bench_bin)
    print(f"ldd {bench_bin.name}:")
    print(dependencies)
    if "libmkl_rt.so" not in dependencies or "not found" in dependencies:
        fail("oneMKL runtime is unavailable; install/source oneAPI MKL so "
             "libmkl_rt.so.3 resolves")
    results_dir = root / "results"
    results_dir.mkdir(exist_ok=True)
    native_path = results_dir / "matched_sobol_to_x_native.json"
    os.environ["MKL_THREADING_LAYER"] = "SEQUENTIAL"
    os.environ["MKL_NUM_THREADS"] = "1"
    os.environ["MKL_DYNAMIC"] = "FALSE"
    bench = run_bin(bench_bin, root, ("--json", str(native_path)))
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("oneMKL Sobol-to-x benchmark failed")
    if not any(line.startswith("onemkl_sobol_x benchmark PASS")
               for line in bench.stdout.splitlines()):
        sys.stdout.write(bench.stdout)
        fail("oneMKL Sobol-to-x benchmark omitted its native PASS banner")
    try:
        payload = json.loads(native_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"oneMKL Sobol-to-x native JSON is unreadable: {exc}")
    rows = parse_onemkl_x_results(payload)
    print(f"oneMKL Sobol-to-x bench: validated {len(rows)} native records (table below)")
    return bench.stdout, payload, rows


def run_synthetic_all_permute_suite(
    root: Path,
) -> tuple[str, dict[str, object], list[dict[str, object]]]:
    bench_bin = root / "bin" / "synthetic_all_permute_scaling_bench"
    dependencies = ldd(bench_bin)
    print(f"ldd {bench_bin.name}:")
    print(dependencies)
    if "libmkl_rt.so" not in dependencies or "not found" in dependencies:
        fail("synthetic all-permute benchmark cannot resolve oneMKL")
    if "libcrypto.so.3" not in dependencies:
        fail("synthetic all-permute benchmark cannot resolve libcrypto.so.3")
    os.environ["MKL_THREADING_LAYER"] = "SEQUENTIAL"
    os.environ["MKL_NUM_THREADS"] = "1"
    os.environ["MKL_DYNAMIC"] = "FALSE"
    check = run_bin(bench_bin, root, ("--check-only",))
    sys.stderr.write(check.stderr)
    if check.returncode != 0 or "synthetic_all_permute_benchmark_correctness=PASS" not in check.stdout:
        sys.stdout.write(check.stdout)
        fail("synthetic all-permute native correctness preflight failed")
    results_dir = root / "results" / "synthetic_all_permute_scaling"
    results_dir.mkdir(parents=True, exist_ok=True)
    native_path = results_dir / "native_spr.json"
    bench = run_bin(bench_bin, root, ("--json", str(native_path)))
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0 or "synthetic_all_permute_scaling PASS" not in bench.stdout:
        sys.stdout.write(bench.stdout)
        fail("synthetic all-permute native benchmark failed")
    try:
        payload = json.loads(native_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"synthetic all-permute native JSON is unreadable: {exc}")
    rows = parse_synthetic_all_permute_results(payload)
    print(f"synthetic all-permute bench: validated {len(rows)} native records (table below)")
    return check.stdout + bench.stdout, payload, rows


def run_xgrowth1_suite(
    root: Path,
) -> tuple[str, list[dict[str, object]], dict[str, object]]:
    bench_bin = root / "bin" / "asian_affine_x_growth_1dim_bench"
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))

    check = run_bin(bench_bin, root, ("--check-only",))
    sys.stderr.write(check.stderr)
    expected = "asian_affine_x_growth_1dim_bench correctness=PASS timing=SKIPPED"
    if check.returncode != 0 or expected not in check.stdout.splitlines():
        sys.stdout.write(check.stdout)
        fail("xgrowth1 standalone correctness gate failed")
    print(expected)

    bench = run_bin(bench_bin, root)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("xgrowth1 benchmark or its repeated pre-timing correctness checks failed")
    parsed = parse_xgrowth1_results(bench.stdout)
    rows = parsed["rows"]
    if not isinstance(rows, list):
        fail("internal xgrowth1 row parsing failure")
    print(f"xgrowth1 bench: validated {len(rows)} native records (table below)")
    return bench.stdout, rows, parsed["report"]


def run_genuine_complete_suite(
    root: Path,
) -> tuple[str, dict[str, object], list[dict[str, object]]]:
    bench_bin = root / "bin" / "asian_genuine_complete_bench"
    dependencies = ldd(bench_bin)
    print(f"ldd {bench_bin.name}:")
    print(dependencies)
    if "libmkl_rt.so" not in dependencies or "not found" in dependencies:
        fail("genuine complete benchmark cannot resolve oneMKL")
    os.environ["MKL_THREADING_LAYER"] = "SEQUENTIAL"
    os.environ["MKL_NUM_THREADS"] = "1"
    os.environ["MKL_DYNAMIC"] = "FALSE"
    check = run_bin(bench_bin, root, ("--check-only",))
    sys.stderr.write(check.stderr)
    expected = (
        "asian_genuine_complete correctness=PASS calls_puts "
        "N=16,32,64,128,256 raw_mkl_words=PASS"
    )
    if check.returncode != 0 or expected not in check.stdout.splitlines():
        sys.stdout.write(check.stdout)
        fail("genuine complete native correctness preflight failed")
    results_dir = root / "results" / "asian_genuine_complete"
    results_dir.mkdir(parents=True, exist_ok=True)
    native_path = results_dir / "aws.json"
    bench = run_bin(bench_bin, root, ("--json", str(native_path)))
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("genuine complete native benchmark failed")
    try:
        payload = json.loads(native_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"genuine complete native JSON is unreadable: {exc}")
    rows = parse_genuine_complete_results(payload)
    native_rows = [row for row in rows if row.get("kind") == "genuine_complete_native"]
    print(f"genuine complete bench: validated {len(native_rows)} native records")
    return check.stdout + bench.stdout, payload, rows


def main() -> int:
    os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
    parser = argparse.ArgumentParser(description="Run isolated AWS bench carriers")
    parser.add_argument(
        "--suite",
        choices=(
            "all",
            "asian",
            "dim",
            "affine18",
            "growth18",
            "conditional18",
            "conditional_payoff",
            "xgrowth1",
            "sql18",
            "onemkl_x",
            "synthetic_all_permute",
            "genuine_complete",
        ),
        default="all",
        help="which component to run (default: all)",
    )
    args = parser.parse_args()

    root = repo_root()
    print(f"repo_root={root}")
    if platform.system() != "Linux" or platform.machine() not in {"x86_64", "AMD64"}:
        fail("requires Linux x86-64")
    hashes = verify_checksums(root)
    asian_meta = load_metadata(root / "BUILD_METADATA.json")
    dim_meta = load_metadata(root / "BUILD_METADATA_dim.json")
    sql18_meta = load_metadata(root / "BUILD_METADATA_sql18.json")
    geometric_cv_meta = load_metadata(root / "BUILD_METADATA_geometric_cv.json")
    onemkl_x_meta = load_metadata(root / "BUILD_METADATA_onemkl_x.json")
    synthetic_all_permute_meta = load_metadata(
        root / "BUILD_METADATA_synthetic_all_permute.json"
    )
    genuine_complete_meta = load_metadata(root / "BUILD_METADATA_genuine_complete.json")
    model, flags = cpu_info()
    avx = avx512_flags(flags)
    print(f"cpu_model={model}")
    print(f"avx512_flags={' '.join(avx) if avx else '(none)'}")
    print(f"kernel={platform.release()}")
    print(f"python={sys.version.split()[0]}")
    print(f"arch={platform.machine()}")
    caches = read_cache()
    l1d = "unsupported"
    l1i = "unsupported"
    for cache in caches:
        print(f"cache {cache['index']} level={cache['level']} type={cache['type']} size={cache['size']}")
        if cache["level"] == "1" and cache["type"] == "Data":
            l1d = cache["size"]
        if cache["level"] == "1" and cache["type"] == "Instruction":
            l1i = cache["size"]
    print(f"l1d={l1d}")
    print(f"l1i={l1i}")
    selected = pin_cpu()
    want_asian = args.suite in {"all", "asian"}
    want_dim = args.suite in {"all", "dim"}
    want_affine18 = args.suite in {"all", "affine18"}
    want_growth18 = args.suite in {"all", "growth18"}
    want_conditional18 = args.suite in {"all", "conditional18"}
    want_conditional_payoff = args.suite in {"all", "conditional_payoff"}
    want_xgrowth1 = args.suite in {"all", "xgrowth1"}
    want_sql18 = args.suite in {"all", "sql18"}
    want_onemkl_x = args.suite in {"all", "onemkl_x"}
    want_synthetic_all_permute = args.suite in {"all", "synthetic_all_permute"}
    want_genuine_complete = args.suite in {"all", "genuine_complete"}

    asian_rows: list[dict[str, object]] | None = None
    dim_rows: list[dict[str, object]] | None = None
    asian_test = ""
    asian_bench = ""
    dim_test = ""
    dim_bench = ""
    affine18_rows: list[dict[str, object]] | None = None
    affine18_bench = ""
    growth18_rows: list[dict[str, object]] | None = None
    growth18_bench = ""
    conditional18_rows: list[dict[str, object]] | None = None
    conditional18_bench = ""
    conditional_payoff_rows: list[dict[str, object]] | None = None
    conditional_payoff_bench = ""
    xgrowth1_rows: list[dict[str, object]] | None = None
    xgrowth1_report: dict[str, object] | None = None
    xgrowth1_bench = ""
    sql18_rows: list[dict[str, object]] | None = None
    sql18_bench = ""
    onemkl_x_rows: list[dict[str, object]] | None = None
    onemkl_x_bench = ""
    onemkl_x_report: dict[str, object] | None = None
    synthetic_all_permute_rows: list[dict[str, object]] | None = None
    synthetic_all_permute_bench = ""
    synthetic_all_permute_report: dict[str, object] | None = None
    genuine_complete_rows: list[dict[str, object]] | None = None
    genuine_complete_bench = ""
    genuine_complete_report: dict[str, object] | None = None

    if "avx512f" not in flags:
        print("UNSUPPORTED: CPU lacks avx512f; not launching binaries")
        if want_asian:
            print_asian_table(None)
        if want_dim:
            print_dim_table(None)
        if want_affine18:
            print_affine18_table(None)
        if want_growth18:
            print_growth18_table(None)
        if want_conditional18:
            print_conditional18_table(None)
        if want_conditional_payoff:
            print_conditional_payoff_table(None)
        if want_xgrowth1:
            print_xgrowth1_table(None)
        if want_sql18:
            print_sql18_table(None)
        if want_onemkl_x:
            print_onemkl_x_table(None)
        if want_synthetic_all_permute:
            print_synthetic_all_permute_table(None)
        if want_genuine_complete:
            print_genuine_complete_table(None)
        print("NO NATIVE DATA COLLECTED — AVX-512F HOST REQUIRED")
        return 2

    if want_asian:
        asian_test, asian_bench, asian_rows = run_asian_suite(
            "asian",
            root / "bin" / "asian_state_test",
            root / "bin" / "asian_state_bench",
            "ASIAN_STATE_TEST PASS",
            root,
        )
    if want_dim:
        dim_test, dim_bench, dim_rows = run_dim_suite(root)
    if want_affine18:
        affine18_bench, affine18_rows = run_affine18_suite(root, selected)
    if want_growth18:
        growth18_bench, growth18_rows = run_growth18_suite(root, selected)
    if want_conditional18:
        conditional18_bench, conditional18_rows = run_conditional18_suite(root)
    if want_conditional_payoff:
        conditional_payoff_bench, conditional_payoff_rows = run_conditional_payoff_suite(root)
    if want_xgrowth1:
        xgrowth1_bench, xgrowth1_rows, xgrowth1_report = run_xgrowth1_suite(root)
    if want_sql18:
        sql18_bench, sql18_rows = run_sql18_suite(root)
    if want_onemkl_x:
        onemkl_x_bench, onemkl_x_report, onemkl_x_rows = run_onemkl_x_suite(root)
    if want_synthetic_all_permute:
        (
            synthetic_all_permute_bench,
            synthetic_all_permute_report,
            synthetic_all_permute_rows,
        ) = run_synthetic_all_permute_suite(root)
    if want_genuine_complete:
        (
            genuine_complete_bench,
            genuine_complete_report,
            genuine_complete_rows,
        ) = run_genuine_complete_suite(root)

    results_dir = root / "results"
    results_dir.mkdir(exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out_path = results_dir / f"aws_{sanitize(model)}_{stamp}.json"
    payload = {
        "cpu_model": model,
        "cpu_flags": flags,
        "avx512_flags": avx,
        "l1_caches": read_cache(),
        "kernel": platform.release(),
        "python": sys.version.split()[0],
        "arch": platform.machine(),
        "selected_cpu": selected,
        "suite": args.suite,
        "binary_hashes": hashes,
        "build_metadata": asian_meta,
        "build_metadata_dim": dim_meta,
        "build_metadata_sql18": sql18_meta,
        "build_metadata_geometric_cv": geometric_cv_meta,
        "build_metadata_onemkl_x": onemkl_x_meta,
        "build_metadata_synthetic_all_permute": synthetic_all_permute_meta,
        "build_metadata_genuine_complete": genuine_complete_meta,
        "build_time_static_audit": asian_meta.get("build_time_static_audit"),
        "build_time_static_audit_dim": dim_meta.get("object_audit"),
        "correctness_status": "PASS",
        "asian": None
        if asian_rows is None
        else {
            "benchmark_measurements": asian_rows,
            "raw_batches": {
                f"{row.get('env')}/{row.get('candidate')}/{row.get('metric')}": row["raw_batches"]
                for row in asian_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "test_stdout": asian_test,
            "bench_stdout": asian_bench,
        },
        "dim": None
        if dim_rows is None
        else {
            "benchmark_measurements": dim_rows,
            "raw_batches": {
                f"{row.get('env')}/{row.get('candidate')}/{row.get('metric')}": row["raw_batches"]
                for row in dim_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "test_stdout": dim_test,
            "bench_stdout": dim_bench,
        },
        "affine18": None
        if affine18_rows is None
        else {
            "scope": "incomplete_18_steps_dt_T_over_32",
            "benchmark_measurements": affine18_rows,
            "raw_batches": {
                f"{row.get('mode')}/{row.get('candidate')}": row["raw_batches"]
                for row in affine18_rows
            },
            "bench_stdout": affine18_bench,
        },
        "growth18": None
        if growth18_rows is None
        else {
            "scope": "incomplete_18_steps_dt_T_over_32_growth_payload_commutation",
            "benchmark_measurements": growth18_rows,
            "raw_batches": {
                f"{row.get('mode')}/{row.get('candidate')}": row["raw_batches"]
                for row in growth18_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "bench_stdout": growth18_bench,
        },
        "conditional18": None
        if conditional18_rows is None
        else {
            "scope": "incomplete_18_routes_dt_T_over_32_exact_first_increment_conditioning",
            "benchmark_measurements": conditional18_rows,
            "raw_batches": {
                f"{row.get('mode')}/{row.get('candidate')}": row["raw_batches"]
                for row in conditional18_rows
            },
            "bench_stdout": conditional18_bench,
        },
        "conditional_payoff": None
        if conditional_payoff_rows is None
        else {
            "scope": "incomplete_18_routes_vector_log_cdf_and_fused_payoff",
            "benchmark_measurements": conditional_payoff_rows,
            "raw_batches": {
                f"{row.get('mode')}/{row.get('candidate')}": row["raw_batches"]
                for row in conditional_payoff_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "bench_stdout": conditional_payoff_bench,
        },
        "xgrowth1": None
        if xgrowth1_rows is None
        else {
            "scope": "one_D5_stored_payload_transition_not_complete_asian_pricing",
            "benchmark_measurements": xgrowth1_rows,
            "raw_batches": {
                f"{row.get('mode')}/{row.get('candidate')}": row["raw_batches"]
                for row in xgrowth1_rows
            },
            "native_report": xgrowth1_report,
            "bench_stdout": xgrowth1_bench,
        },
        "sql18": None
        if sql18_rows is None
        else {
            "scope": "partial_18_routes_weighted_sql_and_geometric_control_mechanics",
            "benchmark_measurements": sql18_rows,
            "raw_batches": {
                f"{row.get('mode')}/{row.get('candidate')}": row["raw_batches"]
                for row in sql18_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "bench_stdout": sql18_bench,
        },
        "onemkl_x": None
        if onemkl_x_rows is None
        else {
            "scope": "strict old/new ordered-D1 comparison plus oneMKL D1 native throughput; no adapter",
            "benchmark_measurements": onemkl_x_rows,
            "raw_batches": {
                f"contract{row.get('contract')}/{row.get('mode')}/{row.get('candidate')}": row["raw_batches"]
                for row in onemkl_x_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "native_report": onemkl_x_report,
            "bench_stdout": onemkl_x_bench,
        },
        "synthetic_all_permute": None
        if synthetic_all_permute_rows is None
        else {
            "scope": (
                "synthetic hardware-scaling ceiling only; repeated certified maps "
                "are not a valid multidimensional Asian simulation or price"
            ),
            "benchmark_measurements": synthetic_all_permute_rows,
            "raw_batches": {
                f"N{row.get('N')}/{row.get('mode')}/{row.get('candidate')}": row["raw_batches"]
                for row in synthetic_all_permute_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "native_report": synthetic_all_permute_report,
            "bench_stdout": synthetic_all_permute_bench,
        },
        "genuine_complete": None
        if genuine_complete_rows is None
        else {
            "scope": "genuine Joe-Kuo complete arithmetic and beta-one geometric-control Asian prices",
            "benchmark_measurements": genuine_complete_rows,
            "raw_batches": {
                f"N{row.get('N')}/{row.get('mode')}/{row.get('candidate')}": row["raw_batches"]
                for row in genuine_complete_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "native_report": genuine_complete_report,
            "bench_stdout": genuine_complete_bench,
        },
        "utc_timestamp": stamp,
    }
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {out_path}")
    print()
    if want_asian:
        print_asian_table(asian_rows)
        print()
    if want_dim:
        print_dim_table(dim_rows)
        print()
    if want_affine18:
        print_affine18_table(affine18_rows)
        print()
    if want_growth18:
        print_growth18_table(growth18_rows)
        print()
    if want_conditional18:
        print_conditional18_table(conditional18_rows)
        print()
    if want_conditional_payoff:
        print_conditional_payoff_table(conditional_payoff_rows)
        print()
    if want_xgrowth1:
        print_xgrowth1_table(xgrowth1_rows)
        print()
    if want_sql18:
        print_sql18_table(sql18_rows)
        print()
    if want_onemkl_x:
        print_onemkl_x_table(onemkl_x_rows)
        print()
    if want_synthetic_all_permute:
        print_synthetic_all_permute_table(synthetic_all_permute_rows)
        print()
    if want_genuine_complete:
        print_genuine_complete_table(genuine_complete_rows)
        print()
    print("NATIVE DATA COLLECTED — PRODUCTION SELECTION REQUIRES REVIEW")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        raise SystemExit(1)
