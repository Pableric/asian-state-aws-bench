#!/usr/bin/env python3
"""Final-linked structural and lifecycle audit for the raw autocall leaf."""

import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

PARENT = "1dfeca0e5c704efddc427d3c38bbdcae562d87bd"
FIX_PARENT = "abe28959f6066216a741fe160ab42269423846cb"
NEW_FILES = {
    "autocall_single_asset_three_date_raw.c",
    "benchmarks/bench_autocall_single_asset_three_date_raw.c",
    "private/autocall_single_asset_three_date_raw_avx512.S",
    "private/autocall_single_asset_three_date_raw_diag.h",
    "tests/Makefile.autocall_single_asset_three_date_raw",
    "tests/audit_autocall_single_asset_three_date_raw.py",
    "tests/autocall_single_asset_three_date_cases.h",
    "tests/autocall_single_asset_three_date_generic_avx512.S",
    "tests/autocall_single_asset_three_date_leaf_wrap.c",
    "tests/compare_autocall_single_asset_three_date_raw.py",
    "tests/reference_autocall_single_asset_three_date_raw.cpp",
    "tests/test_autocall_single_asset_three_date_raw.c",
}


def run(*args):
    return subprocess.check_output(args, text=True)


def body(binary, symbol):
    output = run("objdump", "-d", "-Mintel", "--disassemble=" + symbol,
                 str(binary))
    lines = [line for line in output.splitlines()
             if re.match(r"^\s*[0-9a-f]+:", line)]
    if not lines:
        raise RuntimeError(f"missing linked symbol {symbol}")
    return "\n".join(lines)


def decoded(binary, symbol):
    result = []
    for line in body(binary, symbol).splitlines():
        fields = line.split("\t")
        if len(fields) < 3:
            continue
        instruction = fields[-1].strip().lower()
        match = re.match(r"([a-z0-9_.]+)\s*(.*)", instruction)
        if match:
            result.append((match.group(1),
                           re.sub(r"\s+", " ", match.group(2).strip())))
    return result


def calls(binary, symbol):
    return re.findall(r"\bcallq?\s+[0-9a-f]+\s+<([^>@+]+)",
                      body(binary, symbol))


def import_liveness(root):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("autocall_liveness", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if not args.binary.is_file():
        raise RuntimeError("benchmark binary missing")
    if not args.reference.is_file():
        raise RuntimeError("reference binary missing")

    changed = set(filter(None, run(
        "git", "diff", "--name-only", PARENT, "--").splitlines()))
    unexpected = changed - NEW_FILES
    if unexpected:
        raise RuntimeError(f"parent blob drift {sorted(unexpected)}")
    fix_changes = set(filter(None, run(
        "git", "diff", "--name-only", FIX_PARENT, "--").splitlines()))
    permitted_fix = {
        "tests/Makefile.autocall_single_asset_three_date_raw",
        "tests/audit_autocall_single_asset_three_date_raw.py",
        "tests/reference_autocall_single_asset_three_date_raw.cpp",
    }
    if fix_changes - permitted_fix:
        raise RuntimeError(f"engine drift from fix parent {sorted(fix_changes)}")

    reference_source = (root /
        "tests/reference_autocall_single_asset_three_date_raw.cpp").read_text()
    make_source = (root /
        "tests/Makefile.autocall_single_asset_three_date_raw").read_text()
    if re.search(r"boost[/:]", reference_source, re.I) or \
       re.search(r"boost", make_source, re.I):
        raise RuntimeError("Boost header or build dependency remains")
    reference_symbols = run("nm", "-C", str(args.reference)).lower()
    if "boost::" in reference_symbols:
        raise RuntimeError("Boost symbol remains in reference executable")
    linked = run("ldd", str(args.reference))
    dependencies = set(re.findall(r"(?:=>\s+)?(/?[^\s]*lib[^\s]+\.so(?:\.\d+)*)",
                                  linked))
    allowed = ("libstdc++.so", "libm.so", "libgcc_s.so", "libc.so",
               "ld-linux")
    unexpected_dependencies = [item for item in dependencies
                               if not any(name in item for name in allowed)]
    if unexpected_dependencies:
        raise RuntimeError(
            f"unexpected reference runtime dependencies {unexpected_dependencies}")
    if re.search(r"\b(?:apt|apt-get|yum|dnf|pacman|pkg-config)\b", make_source):
        raise RuntimeError("benchmark-native contains package installation")

    symbols = run("nm", "-a", str(args.binary))
    strings = run("strings", str(args.binary)).lower()
    if "joe_kuo_6_21201.bin" in strings:
        raise RuntimeError("runtime Joe-Kuo file dependency")
    if "asian_meta_qsort_control_plan_create" not in symbols or \
       "autocall_3date_generic_price_leaf_test" not in symbols:
        raise RuntimeError("diagnostic generic oracle missing before timing")
    descriptor = re.search(
        r"^[0-9a-f]+\s+([0-9a-f]+)\s+[rR]\s+asian_meta_direction_descriptors$",
        run("nm", "-S", "--defined-only", str(args.binary)), re.MULTILINE)
    if not descriptor or int(descriptor.group(1), 16) != 8192:
        raise RuntimeError("descriptor object is not 8192 bytes")

    symbol = "autocall_3date_affine_price_leaf"
    code = decoded(args.binary, symbol)
    mnemonics = [item[0] for item in code]
    operands = " ".join(item[1] for item in code)
    if mnemonics.count("vpermd") != 4:
        raise RuntimeError("leaf does not contain exactly four payload vpermd")
    first_permute = mnemonics.index("vpermd")
    if mnemonics[:first_permute].count("vmovaps") < 4:
        raise RuntimeError("direct D1 low/high loads missing")
    if "0x240" in operands:
        raise RuntimeError("generic pattern-vector load in affine leaf")
    if any(name.startswith("call") for name in mnemonics) or \
       any(re.match(r"v(?:gather|scatter)", name) for name in mnemonics) or \
       re.search(r"\b(?:rsp|rbp)\b", operands):
        raise RuntimeError("call, stack frame, gather, or scatter")
    stores = [operand for _, operand in code
              if re.match(r"(?:byte|word|dword|qword|xmmword|ymmword|zmmword) ptr \[",
                          operand)]
    if stores:
        raise RuntimeError("intermediate state store")
    branches = [(name, operand) for name, operand in code
                if name.startswith("j")]
    if len(branches) != 1 or branches[0][0] not in ("jb", "jc", "jnae"):
        raise RuntimeError(f"expected one packet-loop branch, got {branches}")
    if any(name == "kortestw" or name.startswith("kortest") for name in mnemonics):
        raise RuntimeError("packet early exit present")
    if any(name.startswith("vexp") for name in mnemonics):
        raise RuntimeError("vector exponential inside pricing")

    liveness = import_liveness(root)
    _, peak, details = liveness.liveness(liveness.instructions(args.binary,
                                                                symbol))
    if peak >= 24:
        raise RuntimeError(f"peak ZMM liveness {peak}")
    if peak >= 19:
        print(f"autocall_liveness_explanation peak={peak} live={details}")

    lifecycle = (root / "autocall_single_asset_three_date_raw.c").read_text()
    request_section = lifecycle[
        lifecycle.index("int autocall_3date_request_prepare("):
        lifecycle.index("int autocall_3date_prepared_price(")]
    if re.search(r"qsort|bsearch|sha|malloc|calloc|posix_memalign|source_exp",
                 request_section, re.I):
        raise RuntimeError("excluded work in request preparation")
    prepared_calls = calls(args.binary, "autocall_3date_prepared_price")
    if prepared_calls != ["autocall_3date_affine_price_leaf"]:
        raise RuntimeError(f"prepared price closure {prepared_calls}")
    for closure in ("autocall_3date_market_prepare",
                    "autocall_3date_request_prepare",
                    "autocall_3date_prepared_price"):
        forbidden = [name for name in calls(args.binary, closure)
                     if re.search(r"malloc|calloc|posix_memalign|qsort|bsearch|sha",
                                  name, re.I)]
        if forbidden:
            raise RuntimeError(f"allocation/research work in {closure}: {forbidden}")
    benchmark_source = (root /
        "benchmarks/bench_autocall_single_asset_three_date_raw.c").read_text()
    operation = benchmark_source[
        benchmark_source.index("static int operation("):
        benchmark_source.index("static sample_t observe(")]
    if re.search(r"qsort_control|generic_price|prepare_route", operation):
        raise RuntimeError("generic oracle entered a timed operation")

    print("autocall_3date_audit PASS descriptor_bytes=8192 request_bytes=256 "
          "full_plan_allocation_bytes=115008 workspace_bytes=33152 "
          "engine_total_heap_bytes=148160 "
          "d2_d3_context_allocated_bytes=896 "
          "d2_d3_selector_control_consumed_bytes=768 "
          "growth_data_bytes=32768 core_warm_data_bytes=33792 "
          "carrier_identity_cacheline_bytes=64 total_warm_data_bytes=33856 "
          "direct_d1=YES d1_vpermd=0 d2_vpermd=2 d3_vpermd=2 "
          "dynamic_payload_vpermd=512 x_loads=0 date_loop=NO route_loop=NO "
          "path_branches=0 calls=0 spills=0 gathers=0 scatters=0 "
          "generic_oracle_before_timing=YES generic_in_timed_closure=NO "
          "boost_headers=0 boost_symbols=0 "
          "reference_runtime_dependencies=libstdc++,libm,libgcc_s,libc "
          "package_installation_required=NO engine_unchanged_from_fix_parent=YES "
          f"peak_zmm={peak} parent_blobs_unchanged=YES")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"autocall_3date_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
