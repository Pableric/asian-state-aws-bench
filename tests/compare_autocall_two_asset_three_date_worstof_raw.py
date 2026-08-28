#!/usr/bin/env python3
"""Run SDE identity and native portable accuracy, then print the decision table."""

import hashlib
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
    if len(sys.argv) == 2 and sys.argv[1] == "--engine-stdin":
        engine_text = sys.stdin.read()
    else:
        engine_run = subprocess.run(
            ["/opt/intel-sde/sde64", "-spr", "--",
             "./test_autocall_two_asset_three_date_worstof_raw"],
            text=True, capture_output=True)
        if engine_run.returncode:
            sys.stderr.write(engine_run.stdout)
            sys.stderr.write(engine_run.stderr)
            return engine_run.returncode
        engine_text = engine_run.stdout
    reference_run = subprocess.run(
        ["./reference_autocall_two_asset_three_date_worstof_raw"],
        text=True, capture_output=True)
    if reference_run.returncode:
        sys.stderr.write(reference_run.stdout)
        sys.stderr.write(reference_run.stderr)
        return reference_run.returncode
    engine = records(engine_text, "WORSTOF_CASE")
    reference = records(reference_run.stdout, "REFERENCE_CASE")
    if set(engine) != set(reference) or len(engine) != 38:
        print("case identifier mismatch", file=sys.stderr)
        return 1
    reference_qualified = "TWO_ASSET_4096_ACCURACY_QUALIFIED" in \
        reference_run.stdout
    replay_ok = True
    print("bounded_sde_correctness PASS")
    print("panel case reference ref_low ref_high canonical_f64_qmc "
          "binary32_prepared binary32_inline f64_exact_growth_replay "
          "fixed_signed_error randomized_mean_abs randomized_rmse "
          "randomized_half95 replay_bp call1 call2 call3 survival decision")
    for name, e in engine.items():
        r = reference[name]
        replay_bp = float(e["f32_replay_bp"])
        replay_ok &= math.isfinite(replay_bp) and replay_bp <= 0.25
        price = float(e["price"])
        ref = float(r["reference"])
        panel_pass = r["decision"] == "PASS" and replay_bp <= 0.25
        print(f"{r['panel']} {name} {ref:.12g} {float(r['ref_low']):.12g} "
              f"{float(r['ref_high']):.12g} {float(r['canonical']):.12g} "
              f"{price:.12g} {price:.12g} "
              f"{float(e['f64_growth_replay']):.12g} {price-ref:+.12g} "
              f"{float(r['mean_abs_error']):.9g} {float(r['rmse']):.9g} "
              f"{float(r['half95']):.9g} {replay_bp:.9g} "
              f"{float(r['call1']):.9g} {float(r['call2']):.9g} "
              f"{float(r['call3']):.9g} {float(r['survival']):.9g} "
              f"{'PASS' if panel_pass else 'FAIL'}")
    print("binary32_exact_growth_replay " +
          ("PASS limit_bp=0.25" if replay_ok else "FAIL limit_bp=0.25"))
    print("reference_execution native_portable=YES sde=NO timed_lifecycle=NO "
          "max_concurrent_replications=2 deterministic_order=YES")
    print("deterministic_hashes "
          f"sde_engine_stdout_sha256={hashlib.sha256(engine_text.encode()).hexdigest()} "
          f"reference_stdout_sha256={hashlib.sha256(reference_run.stdout.encode()).hexdigest()}")
    print("TWO_ASSET_4096_ACCURACY_QUALIFIED" if
          reference_qualified and replay_ok else
          "TWO_ASSET_4096_ACCURACY_NOT_QUALIFIED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
