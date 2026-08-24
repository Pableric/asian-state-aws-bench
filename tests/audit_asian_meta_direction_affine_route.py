#!/usr/bin/env python3
"""Compact linked audit for the private meta-direction diagnostic."""

import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path


def output(*args):
    return subprocess.check_output(args, text=True)


def symbol_size(binary, symbol):
    match = re.search(
        rf"^[0-9a-f]+\s+([0-9a-f]+)\s+\w\s+{re.escape(symbol)}$",
        output("nm", "-S", "--defined-only", str(binary)), re.MULTILINE)
    if not match:
        raise RuntimeError(f"missing symbol {symbol}")
    return int(match.group(1), 16)


def body(binary, symbol):
    text = output("objdump", "-d", "-M", "intel",
                  "--disassemble=" + symbol, str(binary))
    return "\n".join(line for line in text.splitlines()
                     if re.match(r"^\s*[0-9a-f]+:", line))


def load_liveness(root):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("growth_audit", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--test-binary", required=True)
    parser.add_argument("--fused-object", required=True)
    parser.add_argument("--growth-object", required=True)
    parser.add_argument("--strip-object", required=True)
    args = parser.parse_args()
    binary = Path(args.binary)
    test_binary = Path(args.test_binary)
    root = Path(__file__).resolve().parent.parent

    if symbol_size(binary, "asian_meta_direction_descriptors") != 8192:
        raise RuntimeError("descriptor symbol is not exactly 8192 bytes")

    descriptor_source = (root /
        "private/asian_meta_direction_descriptors.c").read_text()
    records = re.findall(
        r"\{\d+,\s*([01]),\s*ASIAN_META_DESCRIPTOR_ABI_VERSION,\s*\{",
        descriptor_source)
    if len(records) != 256 or set(records) != {"0", "1"}:
        raise RuntimeError("descriptor dimensions/donor regions incomplete")

    builder_source = (root /
        "private/asian_meta_direction_affine_route.c").read_text()
    begin = builder_source.index("int asian_meta_affine_context_build")
    end = builder_source.index("int asian_meta_affine_plan_create", begin)
    builder = builder_source[begin:end]
    forbidden = re.compile(r"qsort|bsearch|sobol|4096|ASIAN_META_PATHS|fopen|open\(",
                           re.IGNORECASE)
    if forbidden.search(builder):
        raise RuntimeError("production builder contains a full-path/oracle operation")
    required_builder = [
        "ASIAN_META_PACKETS", "ASIAN_META_PACKET_COLUMNS",
        "ASIAN_META_FIRST_PACKET_COLUMN", "__builtin_ctz(packet + 1u)",
        "out->sel2[packet][0]", "out->sel2[packet][1]",
    ]
    if not all(token in builder for token in required_builder):
        raise RuntimeError("packet-native production recurrence is incomplete")

    whole_disassembly = output("objdump", "-d", "-M", "intel", str(binary))
    functions = {}
    marks = list(re.finditer(r"^([0-9a-f]+) <([^>]+)>:$", whole_disassembly,
                             re.MULTILINE))
    for index, mark in enumerate(marks):
        stop = marks[index + 1].start() if index + 1 < len(marks) else \
               len(whole_disassembly)
        functions[mark.group(2)] = whole_disassembly[mark.end():stop]
    meta_roots = {"asian_meta_affine_context_build",
                  "asian_meta_affine_plan_create"}
    calls = set()
    pending = list(meta_roots)
    while pending:
        name = pending.pop()
        if name in calls:
            continue
        calls.add(name)
        for target in re.findall(r"\bcallq?\s+[0-9a-f]+\s+<([^>@+]+)",
                                 functions.get(name, "")):
            if target not in calls:
                pending.append(target)
    bad_calls = sorted(name for name in calls if re.search(
        r"qsort|bsearch|sobol|prepare_route|joe_kuo|fopen|open64?", name,
        re.IGNORECASE))
    if bad_calls:
        raise RuntimeError(f"forbidden meta-builder call graph {bad_calls}")
    undefined = output("nm", "-u", str(binary))
    if re.search(r"\b(?:fopen|open|open64)@", undefined):
        raise RuntimeError("native benchmark has runtime file-open dependency")

    growth_symbols = [
        "asian_meta_arithmetic_growth_only_q_diag",
        "asian_meta_arithmetic_growth_only_price_1_diag",
        "asian_meta_arithmetic_growth_only_price_delta_1_diag",
        "asian_meta_arithmetic_growth_only_price_2_diag",
        "asian_meta_arithmetic_growth_only_price_delta_2_diag",
        "asian_meta_arithmetic_growth_only_price_4_diag",
        "asian_meta_arithmetic_growth_only_price_delta_4_diag",
    ]
    sql_symbol = "asian_meta_affine_sql_dual_control_diag"
    provider_symbol = "asian_meta_affine_dual_provider_diag"
    candidate_bodies = [(binary, name, 2) for name in growth_symbols]
    candidate_bodies += [(test_binary, sql_symbol, 4),
                         (test_binary, provider_symbol, 4)]
    for owner, name, permutations in candidate_bodies:
        code = body(owner, name)
        mnemonics = re.findall(r"\b([a-z][a-z0-9]+)\s+", code)
        if sum(value == "vpermd" for value in mnemonics) != permutations:
            raise RuntimeError(f"wrong payload permutation count in {name}")
        if re.search(r"\b(?:callq?|vgather\w*|vscatter\w*)\b", code) or \
           re.search(r"\b(?:rsp|rbp)\b", code):
            raise RuntimeError(f"call/stack/gather/spill in {name}")
        if re.search(r"\[r9\+0x240|\[r9\+.*0x240", code):
            raise RuntimeError(f"generic pattern load in {name}")

    sql = body(test_binary, sql_symbol)
    if len(re.findall(r"\bmovzx\b.*BYTE PTR \[r9\+rcx\*2", sql)) != 2:
        raise RuntimeError("affine SQL route does not use exactly two selectors")
    sql_vector_stores = [line for line in sql.splitlines()
                         if re.search(r"\bvmovaps\b\s+ZMMWORD PTR \[rdx", line)]
    if len(sql_vector_stores) != 6:
        raise RuntimeError("S/Q/L state-store schedule changed")

    live = load_liveness(root)
    peaks = []
    for owner, name, _ in candidate_bodies:
        _, peak, _ = live.liveness(live.instructions(owner, name))
        peaks.append(peak)
    peak = max(peaks)
    if peak >= 32:
        raise RuntimeError(f"peak ZMM liveness is {peak}")

    lifecycle = subprocess.run([
        sys.executable, str(root / "tests/audit_asian_n64_lifecycle.py"),
        "--binary", str(binary),
        "--fused-object", args.fused_object,
        "--growth-object", args.growth_object,
        "--strip-object", args.strip_object,
    ], text=True, capture_output=True)
    if lifecycle.returncode != 0:
        raise RuntimeError("parent lifecycle/kernel audit failed: " +
                           lifecycle.stderr.strip())

    print("meta_descriptor_bytes=8192 all_dimensions_affine=YES "
          "both_donor_regions=YES production_builder_packet_steps=128 "
          "production_builder_full_path_walk=NO "
          "packet_jump_counts=64,32,16,8,4,2,1 "
          f"generic_pattern_loads=0 peak_zmm={peak}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"meta_affine_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
