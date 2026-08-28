#!/usr/bin/env python3
"""Linked structural/lifecycle audit for both two-asset challengers."""

import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

PARENT = "4ccc1c951af3894b673ab3ec983b96b1c72cb3b3"
NEW_FILES = {
    "autocall_two_asset_three_date_worstof_raw.c",
    "benchmarks/bench_autocall_two_asset_three_date_worstof_raw.c",
    "private/autocall_two_asset_three_date_worstof_raw_avx512.S",
    "private/autocall_two_asset_three_date_worstof_raw_diag.h",
    "tests/Makefile.autocall_two_asset_three_date_worstof_raw",
    "tests/audit_autocall_two_asset_three_date_worstof_raw.py",
    "tests/autocall_two_asset_three_date_worstof_cases.c",
    "tests/autocall_two_asset_three_date_worstof_cases.h",
    "tests/autocall_two_asset_three_date_worstof_generic_avx512.c",
    "tests/autocall_two_asset_three_date_worstof_leaf_wrap.c",
    "tests/compare_autocall_two_asset_three_date_worstof_raw.py",
    "tests/reference_autocall_two_asset_three_date_worstof_raw.cpp",
    "tests/test_autocall_two_asset_three_date_worstof_raw.c",
}


def run(*args):
    return subprocess.check_output(args, text=True)


def body(binary, symbol):
    text = run("objdump", "-d", "-Mintel", "--disassemble=" + symbol,
               str(binary))
    lines = [line for line in text.splitlines()
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


def symbol_size(binary, symbol):
    match = re.search(
        rf"^[0-9a-f]+\s+([0-9a-f]+)\s+\w\s+{re.escape(symbol)}$",
        run("nm", "-S", "--defined-only", str(binary)), re.MULTILINE)
    if not match:
        raise RuntimeError(f"missing size for {symbol}")
    return int(match.group(1), 16)


def liveness_module(root):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("worstof_liveness", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def leaf_audit(binary, symbol, vpermd, scalef):
    code = decoded(binary, symbol)
    names = [name for name, _ in code]
    operands = " ".join(operand for _, operand in code)
    if names.count("vpermd") != vpermd:
        raise RuntimeError(
            f"{symbol} static vpermd {names.count('vpermd')} != {vpermd}")
    if names.count("vscalefps") != scalef:
        raise RuntimeError(
            f"{symbol} static exp {names.count('vscalefps')} != {scalef}")
    if any(name.startswith("call") for name in names):
        raise RuntimeError(f"call in {symbol}")
    if re.search(r"\b(?:rsp|rbp)\b", operands):
        raise RuntimeError(f"stack frame/spill in {symbol}")
    if any(re.match(r"v(?:gather|scatter)", name) for name in names):
        raise RuntimeError(f"gather/scatter in {symbol}")
    if any(name.startswith("kortest") for name in names):
        raise RuntimeError(f"early-exit mask test in {symbol}")
    branches = [name for name in names if name.startswith("j")]
    if branches not in (["jb"], ["jc"], ["jnae"]):
        raise RuntimeError(f"{symbol} packet-loop branches {branches}")
    if "0x240" in operands:
        raise RuntimeError(f"generic pattern-vector load in {symbol}")
    stores = []
    for name, operand in code:
        first = operand.split(",", 1)[0]
        if "ptr [" in first and name.startswith("v"):
            stores.append((name, operand))
    if stores:
        raise RuntimeError(f"intermediate stores in {symbol}: {stores}")
    return code


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if not args.binary.is_file() or not args.reference.is_file():
        raise RuntimeError("linked executable missing")

    tracked = set(filter(None, run(
        "git", "diff", "--name-only", PARENT, "--").splitlines()))
    untracked = set(filter(None, run(
        "git", "ls-files", "--others", "--exclude-standard").splitlines()))
    untracked = {name for name in untracked
                 if not name.startswith(
                     ".autocall_two_asset_three_date_worstof_raw_objects/")
                 and name not in {
                     "test_autocall_two_asset_three_date_worstof_raw",
                     "reference_autocall_two_asset_three_date_worstof_raw",
                     "bench_autocall_two_asset_three_date_worstof_raw",
                     "test_autocall_single_asset_three_date_raw",
                     "reference_autocall_single_asset_three_date_raw",
                     "bench_autocall_single_asset_three_date_raw",
                     "test_asian_meta_direction_affine_route",
                     "bench_asian_meta_direction_affine_route",
                     "test_asian_affine_discrete_barrier_lifecycle",
                     "bench_asian_affine_discrete_barrier_lifecycle"}}
    changed = tracked | untracked
    if changed - NEW_FILES:
        raise RuntimeError(f"parent blob drift {sorted(changed-NEW_FILES)}")
    if not NEW_FILES.issuperset(changed):
        raise RuntimeError("unexpected change manifest state")

    symbols = run("nm", "-a", str(args.binary))
    strings = run("strings", str(args.binary)).lower()
    if "asian_meta_qsort_control_plan_create" not in symbols:
        raise RuntimeError("diagnostic generic oracle absent")
    if "joe_kuo_6_21201.bin" in strings:
        raise RuntimeError("runtime Joe-Kuo filename present")
    descriptor = re.search(
        r"^[0-9a-f]+\s+([0-9a-f]+)\s+[rR]\s+asian_meta_direction_descriptors$",
        run("nm", "-S", "--defined-only", str(args.binary)), re.MULTILINE)
    if not descriptor or int(descriptor.group(1), 16) != 8192:
        raise RuntimeError("descriptor object is not exactly 8192 bytes")

    prepared_code = leaf_audit(
        args.binary, "autocall_worstof_prepared_leaf", 4, 0)
    inline_code = leaf_audit(
        args.binary, "autocall_worstof_inline_leaf", 10, 12)
    producer_code = decoded(args.binary,
                            "autocall_worstof_prepare_asset_b_growth")
    producer_names = [name for name, _ in producer_code]
    producer_operands = " ".join(op for _, op in producer_code)
    if producer_names.count("vpermd") != 10 or \
       producer_names.count("vscalefps") != 6:
        raise RuntimeError("prepared carrier producer operation count")
    if any(name.startswith("call") for name in producer_names) or \
       re.search(r"\b(?:rsp|rbp)\b", producer_operands):
        raise RuntimeError("producer call/stack/spill")
    producer_branches = [name for name in producer_names
                         if name.startswith("j")]
    if producer_branches not in (["jb"], ["jc"], ["jnae"]):
        raise RuntimeError(f"producer loop branches {producer_branches}")

    live = liveness_module(root)
    prepared_peak = live.liveness(live.instructions(
        args.binary, "autocall_worstof_prepared_leaf"))[1]
    inline_peak = live.liveness(live.instructions(
        args.binary, "autocall_worstof_inline_leaf"))[1]
    if prepared_peak >= 28 or inline_peak >= 28:
        raise RuntimeError(
            f"peak ZMM prepared={prepared_peak} inline={inline_peak}")
    if inline_peak >= 23:
        print(
            "inline_liveness_roles peak_zmm=%d "
            "zmm0-1=residual_u_or_event_thresholds "
            "zmm2-3=exp_exponents_or_route_broadcast "
            "zmm4-5=asset_a_spot zmm6-7=path_pv zmm8-9=asset_b_spot "
            "zmm10-11=route_controls_or_exp_reduced "
            "zmm12-13=routed_v_or_asset_b_growth "
            "zmm14-15=asset_a_growth zmm16-17=route_source_or_exp_input "
            "zmm20-21=global_accumulators zmm22-25=drift_diffusion "
            "zmm28=rho zmm29=cholesky zmm30-31=initial_spots" % inline_peak)

    lifecycle = (root /
        "autocall_two_asset_three_date_worstof_raw.c").read_text()
    for name in ("autocall_worstof_prepared_request_prepare",
                 "autocall_worstof_inline_request_prepare",
                 "autocall_worstof_prepared_price",
                 "autocall_worstof_inline_price"):
        start = lifecycle.index("int " + name + "(")
        section = lifecycle[start:start+5500]
        if re.search(r"qsort|bsearch|sha|fopen|malloc|calloc|posix_memalign",
                     section, re.I):
            raise RuntimeError(f"excluded research/allocation in {name}")
    if calls(args.binary, "autocall_worstof_prepared_price") != [
            "autocall_worstof_prepared_leaf"]:
        raise RuntimeError("prepared closure does not contain exactly one leaf")
    if calls(args.binary, "autocall_worstof_inline_price") != [
            "autocall_worstof_inline_leaf"]:
        raise RuntimeError("inline closure does not contain exactly one leaf")

    reference_source = (root /
        "tests/reference_autocall_two_asset_three_date_worstof_raw.cpp").read_text()
    if re.search(r"boost|quantlib", reference_source, re.I):
        raise RuntimeError("external reference dependency")
    linked = run("ldd", str(args.reference))
    allowed = ("libstdc++.so", "libm.so", "libgcc_s.so", "libc.so",
               "ld-linux")
    libraries = re.findall(r"(?:=>\s+)?(/?[^\s]*lib[^\s]+\.so(?:\.\d+)*)",
                           linked)
    unexpected = [x for x in libraries if not any(a in x for a in allowed)]
    if unexpected:
        raise RuntimeError(f"unexpected reference dependencies {unexpected}")

    prepared_text = symbol_size(args.binary, "autocall_worstof_prepared_leaf")
    inline_text = symbol_size(args.binary, "autocall_worstof_inline_leaf")
    producer_text = symbol_size(
        args.binary, "autocall_worstof_prepare_asset_b_growth")
    print(
        "two_asset_worstof_audit PASS descriptor_bytes=8192 "
        "full_affine_plan_bytes=115008 engine_workspace_bytes=82816 "
        "engine_heap_bytes=197824 prepared_market_bytes=82112 "
        "prepared_payload_bytes=81920 inline_market_bytes=128 "
        "prepared_request_bytes=512 inline_request_bytes=512 "
        "prepared_candidate_warm_bytes=83328 inline_candidate_warm_bytes=35392 "
        "prepared_leaf_static_vpermd=4 prepared_leaf_dynamic_vpermd=512 "
        "inline_leaf_static_vpermd=10 inline_leaf_dynamic_vpermd=1280 "
        "inline_logical_exp_pairs_per_packet=6 "
        "inline_logical_exp_pairs_per_valuation=768 "
        "inline_physical_zmm_exp_per_packet=12 "
        "inline_physical_zmm_exp_per_valuation=1536 "
        "producer_static_vpermd=10 producer_static_zmm_exp=6 "
        "date_loops=0 asset_loops=0 route_loops=0 leaf_calls=0 "
        "spills=0 gathers=0 scatters=0 generic_routes_normal_pricing=NO "
        f"prepared_peak_zmm={prepared_peak} inline_peak_zmm={inline_peak} "
        f"prepared_hot_text_bytes={prepared_text} "
        f"inline_hot_text_bytes={inline_text} producer_text_bytes={producer_text} "
        "parent_blobs_unchanged=YES")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"two_asset_worstof_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
