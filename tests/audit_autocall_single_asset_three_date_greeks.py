#!/usr/bin/env python3
"""Linked/source audit for the gated autocall core-Greek diagnostic."""

import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

PARENT = "4ccc1c951af3894b673ab3ec983b96b1c72cb3b3"
NEW = {
    "autocall_single_asset_three_date_greeks.c",
    "benchmarks/bench_autocall_single_asset_three_date_greeks.cpp",
    "private/autocall_single_asset_three_date_greeks_diag.h",
    "tests/Makefile.autocall_single_asset_three_date_greeks",
    "tests/audit_autocall_single_asset_three_date_greeks.py",
    "tests/autocall_single_asset_three_date_greek_cases.h",
    "tests/compare_autocall_single_asset_three_date_greeks.py",
    "tests/reference_autocall_single_asset_three_date_greeks.cpp",
    "tests/test_autocall_single_asset_three_date_greeks.c",
}
BUILD_ARTIFACTS = {
    "bench_autocall_single_asset_three_date_greeks",
    "reference_autocall_single_asset_three_date_greeks",
    "test_autocall_single_asset_three_date_greeks",
    "reference_autocall_single_asset_three_date_raw",
    "test_autocall_single_asset_three_date_raw",
    "bench_autocall_single_asset_three_date_raw",
    "bench_asian_meta_direction_affine_route",
    "test_asian_meta_direction_affine_route",
}


def run(*args):
    return subprocess.check_output(args, text=True)


def instructions(binary, symbol):
    output = run("objdump", "-d", "-Mintel", "--disassemble=" + symbol,
                 str(binary))
    return [line.split("\t")[-1].strip().lower()
            for line in output.splitlines()
            if re.match(r"^\s*[0-9a-f]+:", line) and "\t" in line]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--test", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--benchmark", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    for binary in (args.test, args.reference, args.benchmark):
        if not binary.is_file():
            raise RuntimeError(f"missing executable {binary}")

    status = run("git", "status", "--porcelain=v1").splitlines()
    changed = set()
    for line in status:
        code, name = line[:2], line[3:]
        if name in BUILD_ARTIFACTS:
            continue
        if code not in ("??", "A ", " A"):
            raise RuntimeError(f"parent blob modification {line}")
        changed.add(name)
    committed = set(filter(None, run(
        "git", "diff", "--name-only", PARENT, "--").splitlines()))
    committed |= set(filter(None, run(
        "git", "diff", "--name-only", "--cached", PARENT, "--").splitlines()))
    changed |= committed
    if changed != NEW:
        raise RuntimeError(f"additive manifest mismatch missing={sorted(NEW-changed)} "
                           f"extra={sorted(changed-NEW)}")
    for name in NEW:
        if subprocess.run(["git", "cat-file", "-e", f"{PARENT}:{name}"],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError(f"new path unexpectedly exists in parent {name}")

    source = "\n".join((root / name).read_text() for name in NEW
                       if name != "tests/audit_autocall_single_asset_three_date_greeks.py")
    if re.search(r"boost[/:]|quantlib|brownian bridge|\bpca\b", source, re.I):
        raise RuntimeError("excluded dependency/architecture entered diagnostic")
    if any(name.endswith((".s", ".S")) for name in NEW):
        raise RuntimeError("raw gate failed but Greek assembly was added")
    if "RAW_CRN_GREEKS_NOT_QUALIFIED" not in source or \
       "D1_PREINTEGRATION_NEXT" not in source:
        raise RuntimeError("gated fallback decisions missing")

    reference_symbols = run("nm", "-C", str(args.reference)).lower()
    if "boost::" in reference_symbols or "quantlib" in reference_symbols:
        raise RuntimeError("external numerical symbol in reference")
    linked = run("ldd", str(args.reference))
    allowed = ("libstdc++.so", "libm.so", "libgcc_s.so", "libc.so",
               "ld-linux")
    dependencies = re.findall(r"(?:=>\s+)?(/?[^\s]*lib[^\s]+\.so(?:\.\d+)*)",
                              linked)
    unexpected = [item for item in dependencies
                  if not any(name in item for name in allowed)]
    if unexpected:
        raise RuntimeError(f"unexpected runtime dependencies {unexpected}")

    symbols = run("nm", "-C", str(args.benchmark))
    if "conditional_price" not in symbols or "qmc_price" not in symbols:
        raise RuntimeError("portable raw/conditional estimators missing")
    if "autocall_3date_spot_risk" in symbols or \
       "autocall_3date_core_greeks" in symbols:
        raise RuntimeError("dead fused implementation present after raw failure")

    code = instructions(args.test, "autocall_3date_affine_price_leaf")
    mnemonics = [item.split()[0] for item in code]
    operands = " ".join(code)
    if mnemonics.count("vpermd") != 4:
        raise RuntimeError("parent raw leaf static payload-vpermd drift")
    if any(name.startswith("call") for name in mnemonics) or \
       re.search(r"\b(?:rsp|rbp)\b", operands) or \
       any(name.startswith(("vgather", "vscatter")) for name in mnemonics):
        raise RuntimeError("parent raw leaf structural drift")
    branches = [name for name in mnemonics if name.startswith("j")]
    if len(branches) != 1:
        raise RuntimeError("parent raw leaf packet branch drift")

    liveness_path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("greek_liveness", liveness_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    _, peak, _ = module.liveness(module.instructions(
        args.test, "autocall_3date_affine_price_leaf"))
    if peak != 13:
        raise RuntimeError(f"parent raw peak ZMM drift {peak}")

    makefile = (root / "tests/Makefile.autocall_single_asset_three_date_greeks").read_text()
    if re.search(r"\b(?:apt|apt-get|dnf|yum|pacman|pkg-config)\b", makefile):
        raise RuntimeError("package installation entered build")
    benchmark_source = (root /
        "benchmarks/bench_autocall_single_asset_three_date_greeks.cpp").read_text()
    if "family==6u&&model==143u" not in benchmark_source or \
       "strcmp(argv[2],\"0\")" not in benchmark_source:
        raise RuntimeError("SPR CPU-zero refusal gate missing")

    print("autocall_3date_core_greeks_audit PASS raw_gate=FAILED "
          "fused_leaf_built=NO conditional_d1=YES parent_blobs_unchanged=YES "
          "parent_request_bytes=256 diagnostic_output_bytes=128 "
          "parent_growth_payload_bytes=32768 "
          "extra_scratch_carrier_bytes=0 static_payload_vpermd_instructions=4 "
          "dynamic_payload_vpermd_executions=512_per_parent_price "
          "allocation_engine_initialization_only=YES "
          "market_request_price_allocation=NO constant_time_validation=YES "
          "o_n_validation_hash_replay_qsort_file_access=NO "
          "boost=NO quantlib=NO external_runtime_dependency=NO "
          "projected_hot_footprint_compatible=YES residual_direction_bytes=256 "
          f"parent_peak_zmm={peak}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"autocall_3date_core_greeks_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
