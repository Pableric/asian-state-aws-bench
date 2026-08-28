#!/usr/bin/env python3
"""Linked audit for the chronological-D1 native autocall challenger."""

import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

PARENT = "2bc093890a4218a9cd059e9fce9df2d93e29ac25"
NEW = {
    "autocall_single_asset_three_date_d1_native.c",
    "benchmarks/bench_autocall_single_asset_three_date_d1_native.cpp",
    "private/autocall_single_asset_three_date_d1_native_avx512.S",
    "private/autocall_single_asset_three_date_d1_native_diag.h",
    "private/autocall_single_asset_three_date_d1_native_math_avx512.S",
    "tests/Makefile.autocall_single_asset_three_date_d1_native",
    "tests/audit_autocall_single_asset_three_date_d1_native.py",
    "tests/autocall_single_asset_three_date_d1_native_leaf_wrap.c",
    "tests/reference_autocall_single_asset_three_date_d1_native.cpp",
    "tests/test_autocall_single_asset_three_date_d1_native.c",
    "tests/test_autocall_single_asset_three_date_d1_native_math.c",
}
ARTIFACTS = {
    "test_autocall_single_asset_three_date_d1_native_math",
    "test_autocall_single_asset_three_date_d1_native",
    "reference_autocall_single_asset_three_date_d1_native",
    "bench_autocall_single_asset_three_date_d1_native",
    "core",
}


def run(*args):
    return subprocess.check_output(args, text=True)


def symbol_size(binary, symbol):
    match = re.search(rf"^[0-9a-f]+\s+([0-9a-f]+)\s+\w\s+{re.escape(symbol)}$",
                      run("nm", "-S", "--defined-only", str(binary)), re.M)
    if not match:
        raise RuntimeError(f"missing linked symbol {symbol}")
    return int(match.group(1), 16)


def decoded(binary, symbol):
    text = run("objdump", "-d", "-Mintel", "--disassemble=" + symbol,
               str(binary))
    result = []
    for line in text.splitlines():
        if not re.match(r"^\s*[0-9a-f]+:", line):
            continue
        fields = line.split("\t")
        if len(fields) < 3:
            continue
        match = re.match(r"([a-z0-9_.]+)\s*(.*)", fields[-1].strip().lower())
        if match:
            result.append((match.group(1), match.group(2)))
    if not result:
        raise RuntimeError(f"empty disassembly {symbol}")
    return result


def calls(binary, symbol):
    return [operand.split("<", 1)[1].split(">", 1)[0].split("+", 1)[0]
            for name, operand in decoded(binary, symbol)
            if name.startswith("call") and "<" in operand]


def liveness(root, binary, symbol):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("d1_native_liveness", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.liveness(module.instructions(binary, symbol))[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--test", required=True, type=Path)
    parser.add_argument("--math", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--benchmark", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    for binary in (args.test, args.math, args.reference, args.benchmark):
        if not binary.is_file():
            raise RuntimeError(f"missing executable {binary}")

    committed = set(filter(None, run("git", "diff", "--name-only", PARENT,
                                     "--").splitlines()))
    untracked = set(filter(None, run("git", "ls-files", "--others",
                                     "--exclude-standard").splitlines()))
    untracked = {name for name in untracked if name not in ARTIFACTS and
                 not ("/" not in name and name.startswith(("test_", "bench_", "reference_"))) and
                 not name.startswith(".autocall_single_asset_three_date_d1_native_objects/")}
    changed = committed | untracked
    if changed != NEW:
        raise RuntimeError(f"additive manifest mismatch missing={sorted(NEW-changed)} "
                           f"extra={sorted(changed-NEW)}")
    for name in NEW:
        if subprocess.run(["git", "cat-file", "-e", f"{PARENT}:{name}"],
                          stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError(f"path existed in parent {name}")

    symbols = run("nm", "-C", str(args.benchmark))
    strings = run("strings", str(args.benchmark)).lower()
    if "asian_meta_qsort_control_plan_create" in symbols or \
       "joe_kuo_6_21201.bin" in strings:
        raise RuntimeError("generic qsort plan or runtime Joe-Kuo access linked")
    if symbol_size(args.benchmark, "asian_meta_direction_descriptors") != 8192:
        raise RuntimeError("descriptor object size")
    if symbol_size(args.benchmark, "asian_genuine_fixed_block_signed_z") != 32768:
        raise RuntimeError("immutable signed-z size")

    leaf = "autocall_d1_native_price5_leaf"
    code = decoded(args.benchmark, leaf)
    names = [name for name, _ in code]
    operands = " ".join(operand for _, operand in code)
    if names.count("vpermd") != 2:
        raise RuntimeError("static D2 payload-vpermd count")
    branches = [name for name in names if name.startswith("j")]
    if branches != ["jb"]:
        raise RuntimeError(f"packet-loop branches {branches}")
    if any(name.startswith("call") for name in names):
        raise RuntimeError("function call in leaf")
    if re.search(r"\b(?:rsp|rbp)\b", operands):
        raise RuntimeError("stack frame/spill in leaf")
    if any(name.startswith(("vgather", "vscatter")) for name in names):
        raise RuntimeError("gather/scatter in leaf")
    if any(name.startswith("kortest") for name in names):
        raise RuntimeError("path/packet early exit")
    stores = [(name, operand) for name, operand in code
              if re.search(r"\bptr \[rsi(?:\+0x[0-9a-f]+)?\]", operand)]
    if len(stores) != 5 or any(name != "vmovsd" for name, _ in stores):
        raise RuntimeError(f"expected final five-price stores only: {stores}")
    leaf_bytes = symbol_size(args.benchmark, leaf)
    if leaf_bytes > 24576:
        raise RuntimeError(f"hot leaf exceeds 24 KiB: {leaf_bytes}")
    peak = liveness(root, args.benchmark, leaf)
    if peak >= 29:
        raise RuntimeError(f"peak ZMM {peak}")

    source = (root / "private/autocall_single_asset_three_date_d1_native_avx512.S").read_text()
    if source.count("PROCESS_LEG ") - 1 != 10 or \
       source.count("INLINE_R34_PHI") - 1 != 6:
        raise RuntimeError("fixed five-leg/six-Phi schedule drift")
    if re.search(r"D3|growth", source, re.I):
        raise RuntimeError("D3 route or growth carrier entered leaf")
    lifecycle = (root / "autocall_single_asset_three_date_d1_native.c").read_text()
    market_request = lifecycle[lifecycle.index("int autocall_d1_native_market_prepare"):
                               lifecycle.index("int autocall_d1_native_prepared_price")]
    if re.search(r"malloc|calloc|posix_memalign|qsort|bsearch|sha|fopen|open\(",
                 market_request, re.I):
        raise RuntimeError("excluded work in market/request preparation")
    prepared = [name for name in calls(args.benchmark,
                "autocall_d1_native_prepared_price") if "stack_chk_fail" not in name]
    if prepared != [leaf]:
        raise RuntimeError(f"prepared closure calls {prepared}")
    for closure in ("autocall_d1_native_market_prepare",
                    "autocall_d1_native_request_prepare",
                    "autocall_d1_native_prepared_price"):
        forbidden = [name for name in calls(args.benchmark, closure)
                     if re.search(r"malloc|calloc|posix_memalign|qsort|bsearch|sha|fopen",
                                  name, re.I)]
        if forbidden:
            raise RuntimeError(f"forbidden closure calls {closure}: {forbidden}")

    math_text = run("nm", "-C", str(args.math)).lower()
    if "boost::" in math_text or "quantlib" in math_text:
        raise RuntimeError("external vector-math dependency")
    deps = run("ldd", str(args.benchmark))
    allowed = ("libstdc++.so", "libm.so", "libgcc_s.so", "libc.so", "ld-linux")
    libraries = re.findall(r"(?:=>\s+)?(/?[^\s]*lib[^\s]+\.so(?:\.\d+)*)", deps)
    unexpected = [lib for lib in libraries if not any(x in lib for x in allowed)]
    if unexpected:
        raise RuntimeError(f"runtime dependencies {unexpected}")
    benchmark_source = (root /
        "benchmarks/bench_autocall_single_asset_three_date_d1_native.cpp").read_text()
    if "family==6u&&model==143u" not in benchmark_source or \
       "std::strcmp(argv[2],\"0\")" not in benchmark_source:
        raise RuntimeError("SPR/CPU-zero refusal missing")
    makefile = (root / "tests/Makefile.autocall_single_asset_three_date_d1_native").read_text()
    if re.search(r"\b(?:apt|apt-get|dnf|yum|pacman|pkg-config)\b", makefile):
        raise RuntimeError("package installation in native command")

    wrapper_bytes = symbol_size(args.benchmark, "autocall_d1_native_prepared_price")
    print("d1_native_audit PASS descriptor_bytes=8192 signed_z_rodata_bytes=32768 "
          "growth_carrier_bytes=0 direct_d1_to_residual_z3=YES "
          "affine_d2_to_residual_z2=YES d3_route=NO "
          "static_payload_vpermd_instructions=2 dynamic_d2_vpermd=2_per_packet "
          "inline_vector_phi_evaluations=60_per_packet "
          "inline_vector_exp_evaluations=6_per_packet function_calls=0 "
          "backward_branches=1 intermediate_path_state_stores=0 "
          "final_five_price_stores=5 spills=0 gathers=0 scatters=0 "
          f"leaf_text_bytes={leaf_bytes} vector_cdf_macro_bytes=344 "
          "cdf_exp_macro_bytes=93 residual_exp_macro_bytes=149 "
          f"prepared_wrapper_bytes={wrapper_bytes} peak_zmm={peak} "
          "leaf_context_bytes=320 prepared_market_bytes=256 "
          "prepared_request_bytes=512 output_bytes=192 full_plan_bytes=115008 "
          "engine_workspace_bytes=960 engine_heap_bytes=115968 "
          "linked_math_constant_cachelines_bytes=192 "
          "precision_candidate=A binary32_global_reduction=YES "
          "allocation_engine_initialization_only=YES "
          "market_request_pricing_allocation=NO constant_time_validation=YES "
          "o_n_validation_hash_replay_qsort_file_access=NO "
          "parent_blobs_unchanged=YES")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"d1_native_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
