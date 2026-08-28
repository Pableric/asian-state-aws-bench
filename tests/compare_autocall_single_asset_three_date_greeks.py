#!/usr/bin/env python3
"""Run the frozen raw-risk gate and the exact fixed-block engine bridge."""

import math
import subprocess
import sys


def rows(text, prefix):
    result = []
    for line in text.splitlines():
        if not line.startswith(prefix + " "):
            continue
        result.append(dict(item.split("=", 1) for item in line.split()[1:]))
    return result


def main():
    reference = subprocess.run(
        ["./reference_autocall_single_asset_three_date_greeks"],
        text=True, capture_output=True)
    if reference.returncode != 3:
        sys.stderr.write(reference.stderr)
        print(f"unexpected reference gate status {reference.returncode}",
              file=sys.stderr)
        return 1
    if "RAW_CRN_GREEKS_NOT_QUALIFIED" not in reference.stdout or \
       "D1_PREINTEGRATION_ACCURACY_PASS" not in reference.stdout:
        print("missing gated estimator decisions", file=sys.stderr)
        return 1

    fixed = subprocess.run(
        ["/opt/intel-sde/sde64", "-spr", "--",
         "./test_autocall_single_asset_three_date_greeks"],
        text=True, capture_output=True)
    if fixed.returncode:
        sys.stderr.write(fixed.stderr)
        return fixed.returncode
    fixed_rows = rows(fixed.stdout, "FIXED_BLOCK")
    if len(fixed_rows) != 72 or "FIXED_BLOCK_ENGINE PASS" not in fixed.stdout:
        print("fixed-block row/count gate failed", file=sys.stderr)
        return 1

    references = {(row["case"], row["bump"]): row
                  for row in rows(reference.stdout, "FIXED_REFERENCE")}
    if len(references) != 72:
        print("fixed reference row/count gate failed", file=sys.stderr)
        return 1
    max_amplification = 0.0
    fixed_accuracy = []
    for row in fixed_rows:
        s0 = float.fromhex(row["S0"])
        delta = abs(float(row["delta"]) - float(row["replay_delta"]))
        gamma = abs(float(row["gamma"]) - float(row["replay_gamma"]))
        vega = abs(float(row["vega_per_unit_sigma"]) -
                   float(row["replay_vega"]))
        # Every frozen diagnostic has notional 100.
        values = (delta * 0.01 * s0 / 100.0 * 1e4,
                  0.5 * gamma * (0.01 * s0) ** 2 / 100.0 * 1e4,
                  vega * 0.01 / 100.0 * 1e4)
        if not all(math.isfinite(value) for value in values):
            print("nonfinite fixed-block amplification", file=sys.stderr)
            return 1
        max_amplification = max(max_amplification, *values)
        ref = references[(row["case"], row["bump"])]
        fixed_accuracy.append((row, ref))
    if max_amplification > 0.25:
        print(f"binary32 amplification {max_amplification} bp", file=sys.stderr)
        return 1

    random_rows = rows(reference.stdout, "RANDOM")
    failures = []
    for row in random_rows:
        if row["panel"] != "CORE" or row["bump"] != "1":
            continue
        scale = 100.0 if row["greek"] == "DELTA" else \
            50.0 if row["greek"] == "GAMMA" else 1.0
        half_bp = float(row["raw_half"]) * scale
        if abs(float(row["raw_bias_bp"])) > 1.0 or half_bp > 0.5:
            failures.append((row["case"], row["greek"],
                             float(row["raw_bias_bp"]), half_bp))
    if not failures:
        print("raw gate unexpectedly has no failing cell", file=sys.stderr)
        return 1

    print(reference.stdout, end="")
    print(fixed.stdout, end="")
    print("FIXED_ACCURACY panel case bump greek cdf_reference "
          "f64_mathematical_qmc f64_growth_replay binary32_raw "
          "raw_signed_error replay_contribution")
    for row, ref in fixed_accuracy:
        fields = (("DELTA", "delta", "mathematical_delta", "replay_delta"),
                  ("GAMMA", "gamma", "mathematical_gamma", "replay_gamma"),
                  ("VEGA", "vega_per_unit_sigma", "mathematical_vega",
                   "replay_vega"))
        for greek, actual_key, math_key, replay_key in fields:
            actual = float(row[actual_key])
            replay_value = float(row[replay_key])
            cdf_value = float(ref[actual_key])
            print(f"FIXED_ACCURACY {row['panel']} {row['case']} {row['bump']} "
                  f"{greek} {cdf_value:.17g} {float(ref[math_key]):.17g} "
                  f"{replay_value:.17g} {actual:.17g} "
                  f"{actual-cdf_value:+.17g} {actual-replay_value:+.17g}")
    print("RAW_GATE_FAILURES case greek bias_bp half_width_bp")
    for case, greek, bias, half in failures:
        print(f"RAW_GATE_FAILURE {case} {greek} {bias:.9g} {half:.9g}")
    print(f"FIXED_BLOCK_BRIDGE PASS rows={len(fixed_rows)} "
          f"max_f32_amplification_bp={max_amplification:.9g}")
    print("ACCURACY_VERDICT RAW_CRN_GREEKS_NOT_QUALIFIED")
    print("ARCHITECTURE_CANDIDATE D1_PREINTEGRATION_NEXT_PENDING_SPR_TIMING")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
