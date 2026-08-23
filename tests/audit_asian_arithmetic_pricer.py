#!/usr/bin/env python3

import argparse
import re
import subprocess
import sys


PUBLIC = {
    "asian_arithmetic_route_plan_create",
    "asian_arithmetic_route_plan_destroy",
    "asian_arithmetic_prepare",
    "asian_arithmetic_price_prepared",
    "asian_arithmetic_prepared_destroy",
}


def output(*args):
    return subprocess.check_output(args, text=True)


def function(disassembly, name):
    match = re.search(
        rf"^[0-9a-f]+ <{re.escape(name)}>:$(.*?)(?=^[0-9a-f]+ <|\Z)",
        disassembly, re.MULTILINE | re.DOTALL)
    if not match:
        raise RuntimeError(f"missing function {name}")
    return match.group(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--library", required=True)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    dynamic = output("nm", "-D", "--defined-only", args.library)
    exported = set()
    for line in dynamic.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[-1] != "ASIAN_ARITHMETIC_1.0":
            exported.add(fields[-1].split("@@", 1)[0])
    if exported != PUBLIC:
        raise RuntimeError(f"public ABI mismatch: {sorted(exported)}")

    needed = output("readelf", "-d", args.library)
    if re.search(r"mkl|iomp|python|mpfr", needed, re.IGNORECASE):
        raise RuntimeError("forbidden production dependency")

    disassembly = output("objdump", "-d", "-Mintel", args.library)
    body = function(disassembly, "asian_arithmetic_price_prepared")
    required = {
        "asian_genuine_arithmetic_fused_source_exp_diag",
        "asian_genuine_arithmetic_growth_only_q_diag",
        "asian_genuine_arithmetic_growth_only_strip_consume_padded",
        "asian_genuine_arithmetic_growth_only_immediate_consume",
    }
    missing = {name for name in required if name not in body}
    if missing:
        raise RuntimeError(f"production call path missing: {sorted(missing)}")
    if re.search(r"onemkl|intel|sql_variable|dual_control|full_risk|cv_", body,
                 re.IGNORECASE):
        raise RuntimeError("forbidden production pricing call")

    print("production_link_audit PASS public_symbols=5 fixed_arithmetic_only=YES")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"production_link_audit FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
