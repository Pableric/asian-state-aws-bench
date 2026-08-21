#!/usr/bin/env python3
"""Local packaging audit; never part of the performance-only AWS target."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FROZEN = "d31ed2eafdaeb892fbbbf49f55765600548ab46d"
OUT = ROOT / "results/asian_genuine_seasoned_overhead_confirmation/package_audit.json"
ADDITIVE = {
    "benchmarks/bench_asian_genuine_seasoned_overhead_confirmation.c",
    "tests/Makefile.asian_genuine_seasoned_overhead_confirmation",
    "tests/analyze_asian_genuine_seasoned_overhead_confirmation.py",
    "tests/audit_asian_genuine_seasoned_overhead_confirmation.py",
    "results/asian_genuine_seasoned_overhead_confirmation/README.md",
    "results/asian_genuine_seasoned_overhead_confirmation/preregistration.json",
    "results/asian_genuine_seasoned_overhead_confirmation/package_audit.json",
}
FROZEN_FILES = (
    "asian_genuine_price_delta_strip_avx512.s",
    "asian_genuine_price_delta_strip_setup.c",
    "asian_genuine_sql_variable_avx512.s",
    "asian_genuine_permute_setup.c",
    "asian_geometric_cv_payoff_avx512.s",
    "private/asian_genuine_price_delta_strip_diag.h",
    "private/asian_genuine_permute.h",
    "private/asian_genuine_seasoned_strip_setup.c",
    "private/asian_genuine_seasoned_strip_diag.h",
    "benchmarks/bench_asian_genuine_seasoned_price_delta_strip.c",
    "tests/Makefile.asian_genuine_seasoned_strip",
    "ordered_d1_x_growth_handoff/ordered_d1_x_growth_setup.c",
    "ordered_d1_x_growth_handoff/sobol_ordered_d1_x_growth_diag_avx512.s",
    "direction_numbers/joe_kuo_6_21201.bin",
    "results/asian_genuine_seasoned_price_delta_strip/qualification.json",
    "results/asian_genuine_seasoned_price_delta_strip/local_correctness.json",
    "results/asian_genuine_seasoned_price_delta_strip/structural_audit.json",
)


def git(*args: str) -> bytes:
    return subprocess.run(
        ["git", *args], cwd=ROOT, check=True, stdout=subprocess.PIPE
    ).stdout


def main() -> int:
    checks: dict[str, bool] = {}
    failures: list[str] = []
    changed = {
        line.decode().strip()
        for line in git("diff", "--name-only", FROZEN, "--") .splitlines()
        if line.strip()
    }
    untracked = {
        line.decode().strip()
        for line in git("ls-files", "--others", "--exclude-standard").splitlines()
        if line.strip()
    }
    untracked.discard("bench_asian_genuine_seasoned_overhead_confirmation")
    package_paths = changed | untracked | ({str(OUT.relative_to(ROOT))} if OUT.exists() else set())
    checks["only_additive_paths"] = package_paths <= ADDITIVE
    if not checks["only_additive_paths"]:
        failures.append("non-additive path changed: " + ", ".join(sorted(package_paths - ADDITIVE)))

    hashes: dict[str, str] = {}
    checks["frozen_files_unchanged"] = True
    for path in FROZEN_FILES:
        frozen_bytes = git("show", f"{FROZEN}:{path}")
        hashes[path] = hashlib.sha256(frozen_bytes).hexdigest()
        if (ROOT / path).read_bytes() != frozen_bytes:
            checks["frozen_files_unchanged"] = False
            failures.append("frozen file differs: " + path)

    prereg = json.loads(
        (ROOT / "results/asian_genuine_seasoned_overhead_confirmation/preregistration.json").read_text()
    )
    checks["preregistered_matrix"] = (
        prereg["frozen_commit"] == FROZEN
        and len(prereg["cases"]) == 7
        and prereg["estimators"] == ["arithmetic", "geometric_cv"]
        and prereg["cache_modes"]
        == ["candidate_specific_warm", "historical_32KiB_rmw"]
        and prereg["protocol"]["warmup_quartets_per_cell"] == 16
        and prereg["protocol"]["measured_quartets_per_cell"] == 201
        and prereg["frozen_candidate"] == {
            "tile": 8,
            "output": "price_delta",
            "selection_from_confirmation_results": False,
        }
    )
    if not checks["preregistered_matrix"]:
        failures.append("pre-registration matrix mismatch")

    benchmark = (
        ROOT / "benchmarks/bench_asian_genuine_seasoned_overhead_confirmation.c"
    ).read_text()
    checks["benchmark_scope_bounded"] = (
        "ASIAN_GENUINE_STRIP_TILE8" in benchmark
        and "ASIAN_GENUINE_STRIP_TILE4" not in benchmark
        and "mkl" not in benchmark.lower()
        and "price_delta_diag" in benchmark
        and "asian_genuine_strip_price_diag(" not in benchmark
        and "WARMUP_QUARTETS = 16" in benchmark
        and "MEASURED_QUARTETS = 201" in benchmark
    )
    if not checks["benchmark_scope_bounded"]:
        failures.append("benchmark contains an out-of-scope candidate or protocol")

    dry_run = subprocess.run(
        [
            "make",
            "-B",
            "-n",
            "-f",
            "tests/Makefile.asian_genuine_seasoned_overhead_confirmation",
            "aws-overhead-confirmation-native",
        ],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
        text=True,
    ).stdout.lower()
    checks["aws_target_performance_only"] = all(
        forbidden not in dry_run
        for forbidden in ("python", "numpy", "mpfr", "sde64", "mkl", "coefficient")
    ) and "--check-only" in dry_run
    if not checks["aws_target_performance_only"]:
        failures.append("AWS target reaches a forbidden dependency")

    checks["committed_correctness_prerequisite"] = (
        prereg["prerequisites"]["seasoned_correctness"]
        == "SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING"
        and prereg["prerequisites"]["matched_f_route_dynamic_instruction_delta"] == 0
        and prereg["prerequisites"]["c0_route_and_context_bytes"] == "identical"
    )
    if not checks["committed_correctness_prerequisite"]:
        failures.append("frozen correctness/structure prerequisite mismatch")

    binary = ROOT / "bench_asian_genuine_seasoned_overhead_confirmation"
    checks["benchmark_built"] = binary.is_file()
    if not checks["benchmark_built"]:
        failures.append("benchmark binary is not built")

    report = {
        "schema": "asian-genuine-seasoned-overhead-package-audit-v1",
        "frozen_commit": FROZEN,
        "status": "PASS" if not failures else "FAIL",
        "checks": checks,
        "failures": failures,
        "additive_manifest": sorted(package_paths),
        "frozen_sha256": hashes,
        "benchmark_binary_hash_source": "self-hashed AWS executable recorded in raw_aws.json",
        "aws_measurement_pending": True,
    }
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print("asian_genuine_seasoned_overhead package_audit=" + report["status"])
    return 0 if not failures else 2


if __name__ == "__main__":
    sys.exit(main())
