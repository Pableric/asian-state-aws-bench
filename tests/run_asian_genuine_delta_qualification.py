#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results/asian_genuine_delta_qualification"
RAW = RESULTS / "replication_raw.json"
PRODUCTION = RESULTS / "production_verify.json"
FINAL_JSON = RESULTS / "qualification.json"
FINAL_MD = RESULTS / "qualification.md"
PACKAGE_COMMIT = "1b199076ae9fa6db2258172587b59fef400e11ff"

EXPECTED_HASHES = {
    "asian_genuine_price_delta_strip_setup.c":
        "907c4a05807de27d8ec3c226382e6b640048a87fcd31f9b97d82c5c37668dced",
    "asian_genuine_price_delta_strip_avx512.s":
        "42c52432e1c0b49956d5db11305e9b7d6a8f8c86d35c5fa819ea96443f03893e",
    "direction_numbers/joe_kuo_6_21201.bin":
        "fa6418f236d4667b5deb5b62e6d5fcd6385c64dd60ef2cd1f06fed0e8ea74199",
}


LOCAL_SDE = False


def run(command: list[str], *, avx512: bool = False) -> str:
    if avx512 and LOCAL_SDE:
        command = ["/opt/intel-sde/sde64", "-skx", "--", *command]
    completed = subprocess.run(
        command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=True
    )
    print(completed.stdout, end="")
    return completed.stdout


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def scientific(value: float) -> str:
    return f"{value:.9e}"


def main() -> int:
    global LOCAL_SDE
    if len(sys.argv) == 2 and sys.argv[1] == "--local-sde":
        LOCAL_SDE = True
    elif len(sys.argv) != 1:
        return 2
    RESULTS.mkdir(parents=True, exist_ok=True)
    corpus = RESULTS / "replication_states.bin"
    observed_hashes = {name: file_hash(ROOT / name) for name in EXPECTED_HASHES}
    hash_gate = observed_hashes == EXPECTED_HASHES

    price_output = run(["./test_asian_genuine_price_delta_strip", "--price-only"], avx512=True)
    complete_output = run(["./test_asian_genuine_price_delta_strip_complete"], avx512=True)
    analytic_output = run(["./test_asian_genuine_price_delta_strip_analytic"], avx512=True)
    generator_output = run(["python3", "generate_ordered_d1_coeffs.py", "--check"])
    layout_output = run(["python3", "-m", "unittest", "tests.test_ordered_d1_layout"])
    ordered_output = run(["./test_ordered_d1_kernel"], avx512=True)
    replication_output = run([
        "./asian_genuine_delta_qualification", "--json", str(RAW.relative_to(ROOT)),
        "--corpus", str(corpus.relative_to(ROOT))
    ])
    production_output = run([
        "./asian_genuine_delta_production_verify", "--corpus", str(corpus.relative_to(ROOT)),
        "--json", str(PRODUCTION.relative_to(ROOT))
    ], avx512=True)
    corpus.unlink()

    canonical_pattern = re.compile(
        r"KINK_AMBIGUITY_REPORTED N=128 K=98 arithmetic_paths=(\d+) "
        r"first_path=(\d+) unadjusted_delta_difference=([^ ]+) "
        r"kink_flip_contribution=([^ ]+) non_kink_residual=([^ ]+) "
        r"geometric_paths=(\d+)"
    )
    match = canonical_pattern.search(complete_output)
    canonical = None
    canonical_gate = False
    if match:
        canonical = {
            "N": 128,
            "strike": 98,
            "arithmetic_ambiguous_paths": int(match.group(1)),
            "first_path": int(match.group(2)),
            "unadjusted_delta_difference": float(match.group(3)),
            "kink_flip_contribution": float(match.group(4)),
            "non_kink_residual": float(match.group(5)),
            "geometric_ambiguous_paths": int(match.group(6)),
        }
        canonical_gate = (
            canonical["arithmetic_ambiguous_paths"] == 1
            and canonical["first_path"] == 471
            and canonical["geometric_ambiguous_paths"] == 0
            and abs(canonical["non_kink_residual"]) <= 1e-4
        )

    package_gates = {
        "frozen_source_hashes": hash_gate,
        "price_qualification": "stage1_price=PASS" in price_output,
        "complete_price_and_delta_decomposition": (
            "stage1_complete=PASS" in complete_output
            and "delta_status=KINK_AMBIGUITY_REPORTED" in complete_output
        ),
        "canonical_kink_path_471": canonical_gate,
        "analytic_geometric_price_delta": (
            "analytic_geometric_prices=PASS" in analytic_output
            and "analytic_geometric_deltas=PASS" in analytic_output
        ),
        "ordered_d1_generator": "verified:" in generator_output,
        "ordered_d1_layout": "OK" in layout_output,
        "ordered_d1_kernel": "ordered_d1_kernel_reference=PASS" in ordered_output,
    }
    raw = json.loads(RAW.read_text())
    production = json.loads(PRODUCTION.read_text())
    replication_gates = dict(raw["gates"])
    replication_gates["price_and_tile_bits"] = production["gates"]["price_and_tile_bits"]
    replication_gates["same_state"] = production["gates"]["same_state"]
    replication_gates["smooth_residual"] = production["gates"]["smooth_residual"]
    combined_gates = {**package_gates, **replication_gates}
    decision = (
        "DELTA_QUALIFIED" if all(combined_gates.values())
        else "DELTA_REMAINS_DIAGNOSTIC"
    )
    failed = [name for name, passed in combined_gates.items() if not passed]
    payload = {
        "schema": 1,
        "decision": decision,
        "failed_gates": failed,
        "package_commit": PACKAGE_COMMIT,
        "preregistration": "private/ASIAN_GENUINE_DELTA_QUALIFICATION_CONTRACT.md",
        "source_hashes": {
            name: {"expected": EXPECTED_HASHES[name], "observed": observed_hashes[name]}
            for name in EXPECTED_HASHES
        },
        "gates": combined_gates,
        "canonical_kink": canonical,
        "package_test_output": {
            "price": price_output.strip(),
            "complete": complete_output.strip(),
            "analytic": analytic_output.strip(),
            "ordered_d1_kernel": ordered_output.strip(),
        },
        "replication": raw,
        "production_verification": production,
        "native_aws_commands": [
            "make -f tests/Makefile.asian_genuine_delta_qualification -j2 qualification-native",
            "python3 tests/audit_asian_genuine_delta_qualification.py",
        ],
    }
    FINAL_JSON.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")

    lines = [
        "# Asian strip Delta qualification",
        "",
        f"Decision: **{decision}**",
        "",
        f"Package base: `{PACKAGE_COMMIT}`",
        "",
        "## Pre-registered gates",
        "",
        "| Gate | Result |",
        "|---|---:|",
    ]
    lines.extend(f"| `{name}` | {'PASS' if passed else 'FAIL'} |"
                 for name, passed in combined_gates.items())
    if failed:
        lines.extend(["", "Failed gates: " + ", ".join(f"`{name}`" for name in failed)])
    production_evidence = production
    pathwise = raw["pathwise"]
    bump = raw["bump_agreement"]
    parity = raw["parity"]
    lines.extend([
        "",
        "## Core evidence",
        "",
        f"- Same-Q/G maximum Delta error: `{scientific(production_evidence['max_same_state_error'])}`.",
        f"- Signed mean same-Q/G error: `{scientific(production_evidence['signed_mean_same_state_error'])}`.",
        f"- Maximum actual-leaf kink-adjusted smooth residual: `{scientific(production_evidence['max_smooth_residual'])}`.",
        f"- Signed mean actual-leaf smooth residual: `{scientific(production_evidence['signed_mean_smooth_residual'])}`.",
        f"- Arithmetic/geometric ambiguous paths: `{pathwise['arithmetic_ambiguous_paths']}` / `{pathwise['geometric_ambiguous_paths']}`.",
        f"- Maximum inverse-normal CDF residual: `{scientific(raw['sobol']['max_inverse_cdf_residual'])}`.",
        f"- CRN bump expanded-CI coverage: `{bump['covered']}/{bump['contracts']}` (`{bump['coverage']:.6%}`).",
        f"- Direct/parity expanded-CI coverage: `{parity['covered']}/{parity['contracts']}` (`{parity['coverage']:.6%}`).",
        "",
        "## Aggregate paired pathwise-minus-bump results",
        "",
        "| N | Estimator | Side | Prefix | Bias | RMSE | Coverage |",
        "|---:|---|---|---:|---:|---:|---:|",
    ])
    for row in raw["aggregate_tables"]:
        lines.append(
            f"| {row['N']} | {row['estimator']} | {row['side']} | {row['prefix']} "
            f"| {scientific(row['bias'])} | {scientific(row['rmse'])} "
            f"| {row['covered']}/{row['contracts']} |"
        )
    lines.extend([
        "",
        "## Convergence",
        "",
        "| Prefix | Pooled bias | Pooled RMSE | Median estimator SE |",
        "|---:|---:|---:|---:|",
    ])
    for row in raw["convergence"]["pooled"]:
        lines.append(
            f"| {row['prefix']} | {scientific(row['bias'])} "
            f"| {scientific(row['rmse'])} | {scientific(row['median_estimator_se'])} |"
        )
    lines.extend([
        "",
        "## Worst cases and kink decomposition",
        "",
        "| Kind | N | Count | Strike | Estimator | Side | Prefix | Rep | Unadjusted | Flip | Residual |",
        "|---|---:|---:|---:|---|---|---:|---:|---:|---:|---:|",
    ])
    for name, case in raw["worst_cases"].items():
        lines.append(
            f"| {name} | {case['N']} | {case['strike_count']} | {case['strike']} "
            f"| {case['estimator']} | {case['side']} | {case['prefix']} "
            f"| {case['replication']} | {scientific(case['unadjusted'])} "
            f"| {scientific(case['indicator_flip'])} | {scientific(case['smooth_residual'])} |"
        )
    if canonical:
        lines.extend([
            "",
            "Canonical unshifted N=128/K=98 remains explicit: path 471, "
            f"unadjusted `{scientific(canonical['unadjusted_delta_difference'])}`, "
            f"flip `{scientific(canonical['kink_flip_contribution'])}`, and "
            f"residual `{scientific(canonical['non_kink_residual'])}`.",
        ])
    lines.extend([
        "",
        "## Reproduction on native AWS",
        "",
        "```sh",
        "make -f tests/Makefile.asian_genuine_delta_qualification -j2 qualification-native",
        "python3 tests/audit_asian_genuine_delta_qualification.py",
        "```",
        "",
        "The runner invokes no Intel SDE and performs no performance tuning.",
    ])
    FINAL_MD.write_text("\n".join(lines) + "\n")
    print(f"asian_genuine_delta_qualification decision={decision}")
    if failed:
        print("failed_gates=" + ",".join(failed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
