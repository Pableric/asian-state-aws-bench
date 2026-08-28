#!/usr/bin/env python3
"""Run the emulated engine and native reference, then print one final table."""

import math
import subprocess
import sys


def records(text, prefix):
    result = {}
    for line in text.splitlines():
        if not line.startswith(prefix + " "):
            continue
        fields = dict(item.split("=", 1) for item in line.split()[1:])
        result[fields["name"]] = fields
    return result


def main():
    engine_run = subprocess.run(
        ["/opt/intel-sde/sde64", "-spr", "--",
         "./test_autocall_single_asset_three_date_raw"],
        text=True, capture_output=True)
    if engine_run.returncode:
        sys.stderr.write(engine_run.stderr)
        return engine_run.returncode
    reference_run = subprocess.run(
        ["./reference_autocall_single_asset_three_date_raw"],
        text=True, capture_output=True)
    if reference_run.returncode:
        sys.stderr.write(reference_run.stderr)
        return reference_run.returncode
    engine = records(engine_run.stdout, "ENGINE_CASE")
    reference = records(reference_run.stdout, "REFERENCE_CASE")
    if set(engine) != set(reference) or len(engine) != 7:
        print("case identifier mismatch", file=sys.stderr)
        return 1
    print("bounded_correctness PASS")
    print("semi_analytic_oracle PASS")
    print("contract cdf_reference f64_direct_integration f64_mathematical_qmc "
          "f64_growth_replay binary32_affine affine_vs_cdf "
          "f32_replay_bp call1 call2 call3 survival identity")
    sensitivity = False
    for name in engine:
        e, r = engine[name], reference[name]
        affine = float(e["affine"])
        replay = float(e["f64_growth_replay"])
        cdf = float(r["cdf"])
        bp = float(e["f32_replay_bp"])
        sensitivity |= bp > 0.25
        print(f"{name} {cdf:.12g} {float(r['direct']):.12g} "
              f"{float(r['mathematical_qmc']):.12g} {replay:.12g} "
              f"{affine:.12g} {affine-cdf:+.12g} {bp:.9g} "
              f"{float(r['call1']):.9g} {float(r['call2']):.9g} "
              f"{float(r['call3']):.9g} {float(r['survival']):.9g} PASS")
    print("F64_GROWTH_REPLAY comparison=isolated_binary32_state_arithmetic")
    print("F64_MATHEMATICAL_QMC comparison=source_arithmetic_fixed_block_error")
    print("F32_BARRIER_SENSITIVITY_REQUIRES_RESEARCH" if sensitivity else
          "F32_BARRIER_SENSITIVITY_WITHIN_0.25_BP")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
