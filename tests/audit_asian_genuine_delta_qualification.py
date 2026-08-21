#!/usr/bin/env python3
from __future__ import annotations

import json
import math
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results/asian_genuine_delta_qualification"
REPORT = RESULTS / "qualification.json"
AUDIT = RESULTS / "audit.json"
BASE = "1b199076ae9fa6db2258172587b59fef400e11ff"


def main() -> int:
    payload = json.loads(REPORT.read_text())
    raw = payload["replication"]
    changed_kernels = subprocess.check_output([
        "git", "diff", "--name-only", BASE, "--",
        "asian_genuine_price_delta_strip_setup.c",
        "asian_genuine_price_delta_strip_avx512.s",
    ], cwd=ROOT, text=True).splitlines()
    makefile = (ROOT / "tests/Makefile.asian_genuine_delta_qualification").read_text()
    native_recipe = makefile.split("qualification-native:", 1)[1].split(
        "qualification-local-sde:", 1
    )[0].lower()
    expected_groups = {
        (n, estimator, side, prefix)
        for n in (16, 32, 64, 128, 256)
        for estimator in ("arithmetic", "geometric_cv")
        for side in ("call", "put")
        for prefix in (512, 1024, 2048, 4096)
    }
    observed_groups = {
        (row["N"], row["estimator"], row["side"], row["prefix"])
        for row in raw["aggregate_tables"]
    }
    gates = payload["gates"]
    expected_decision = "DELTA_QUALIFIED" if all(gates.values()) else "DELTA_REMAINS_DIAGNOSTIC"
    checks = {
        "production_kernels_unchanged": not changed_kernels,
        "native_runner_has_no_sde": "intel-sde" not in native_recipe and "sde64" not in native_recipe,
        "decision_matches_gates": payload["decision"] == expected_decision,
        "failed_gate_manifest_exact": sorted(payload["failed_gates"]) ==
            sorted(name for name, passed in gates.items() if not passed),
        "all_32_shifts_reported": len(raw["shifts"]) == 32 and
            len({row["initial_state"] for row in raw["shifts"]}) == 32 and
            len({row["vector_fnv1a64"] for row in raw["shifts"]}) == 32,
        "inverse_normal_residual_reported":
            math.isfinite(raw["sobol"]["max_inverse_cdf_residual"]) and
            raw["sobol"]["max_inverse_cdf_residual"] <= 1.0e-15,
        "contract_grid_exact": raw["contract"] == {
            "replications": 32,
            "N": [16, 32, 64, 128, 256],
            "strike_counts": [1, 4, 8, 16, 32],
            "prefixes": [512, 1024, 2048, 4096],
            "bump_relative_powers": [-12, -14, -16],
            "shift_master": "0xd1e17a5eedc0ffee",
            "shift_stride": "0x9e3779b97f4a7c15",
        },
        "aggregate_grid_complete": observed_groups == expected_groups,
        "separate_estimators_and_sides": all(
            value in {row[key] for row in raw["aggregate_tables"]}
            for key, value in (("estimator", "arithmetic"),
                               ("estimator", "geometric_cv"),
                               ("side", "call"), ("side", "put"))
        ),
        "worst_cases_reported": set(raw["worst_cases"]) ==
            {"same_state", "smooth_residual", "unadjusted_kink"},
        "canonical_kink_reported": payload["canonical_kink"] is not None and
            payload["canonical_kink"]["first_path"] == 471,
        "markdown_report_present": (RESULTS / "qualification.md").is_file(),
        "raw_report_present": (RESULTS / "replication_raw.json").is_file(),
        "production_verification_present": (RESULTS / "production_verify.json").is_file(),
    }
    audit = {
        "status": "PASS" if all(checks.values()) else "FAIL",
        "decision": payload["decision"],
        "checks": checks,
        "changed_production_kernels": changed_kernels,
        "included_reports": [
            "results/asian_genuine_delta_qualification/replication_raw.json",
            "results/asian_genuine_delta_qualification/production_verify.json",
            "results/asian_genuine_delta_qualification/qualification.json",
            "results/asian_genuine_delta_qualification/qualification.md",
        ],
    }
    AUDIT.write_text(json.dumps(audit, indent=2, sort_keys=True) + "\n")
    print(json.dumps(audit, indent=2, sort_keys=True))
    return 0 if audit["status"] == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
