#!/usr/bin/env python3
"""Final-linked structural audit for the private affine-route family."""

import argparse
import hashlib
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

PARENT = "439a837710f4fc9aa07634832bbe10dcc9d6147e"
ALLOWED = {
    "asian_affine_route_family.c",
    "benchmarks/bench_asian_affine_route_family.c",
    "private/asian_affine_route_family_diag.h",
    "private/asian_affine_route_family_generic_immediate_avx512.S",
    "private/asian_affine_route_family_generic_packet_avx512.s",
    "tests/Makefile.asian_affine_route_family",
    "tests/audit_asian_affine_route_family.py",
    "tests/test_asian_affine_route_family.c",
}

AFFINE_ARITH = [
    "asian_meta_arithmetic_growth_only_q_diag",
    "asian_meta_arithmetic_growth_only_price_1_diag",
    "asian_meta_arithmetic_growth_only_price_delta_1_diag",
    "asian_meta_arithmetic_growth_only_price_2_diag",
    "asian_meta_arithmetic_growth_only_price_delta_2_diag",
    "asian_meta_arithmetic_growth_only_price_4_diag",
    "asian_meta_arithmetic_growth_only_price_delta_4_diag",
]
AFFINE_GEOCV = [
    "asian_geometric_cv_immediate_price_1_diag",
    "asian_geometric_cv_immediate_price_delta_1_diag",
    "asian_geometric_cv_immediate_price_2_diag",
    "asian_geometric_cv_immediate_price_delta_2_diag",
    "asian_geometric_cv_immediate_price_4_diag",
    "asian_geometric_cv_immediate_price_delta_4_diag",
]
GENERIC_GEOCV = [name.replace("asian_geometric_cv_immediate",
    "asian_affine_family_generic_geocv") for name in AFFINE_GEOCV]


def run(*args):
    return subprocess.check_output(args, text=True)


def symbol_size(binary, symbol):
    match = re.search(
        rf"^[0-9a-f]+\s+([0-9a-f]+)\s+\w\s+{re.escape(symbol)}$",
        run("nm", "-S", "--defined-only", str(binary)), re.MULTILINE)
    if not match:
        raise RuntimeError(f"missing symbol {symbol}")
    return int(match.group(1), 16)


def body(binary, symbol):
    text = run("objdump", "-d", "-M", "intel",
               "--disassemble=" + symbol, str(binary))
    lines = [line for line in text.splitlines()
             if re.match(r"^\s*[0-9a-f]+:", line)]
    if not lines:
        raise RuntimeError(f"empty symbol {symbol}")
    return "\n".join(lines)


def calls(binary, symbol):
    return re.findall(r"\bcallq?\s+[0-9a-f]+\s+<([^>@+]+)",
                      body(binary, symbol))


def import_audit(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def no_hot_hazards(code, symbol):
    if re.search(r"\b(?:callq?|vgather\w*|vscatter\w*)\b", code):
        raise RuntimeError(f"call/gather/scatter in {symbol}")
    if re.search(r"\b(?:rsp|rbp)\b", code):
        raise RuntimeError(f"stack frame or spill in {symbol}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    binary = Path(args.binary)
    root = Path(__file__).resolve().parent.parent
    if not binary.is_file():
        raise RuntimeError("linked benchmark missing")

    if symbol_size(binary, "asian_meta_direction_descriptors") != 8192:
        raise RuntimeError("descriptor object is not 8192 bytes")

    changed = set(run("git", "diff", "--name-only", PARENT, "--").splitlines())
    unexpected = sorted(changed - ALLOWED)
    if unexpected:
        raise RuntimeError(f"parent blob drift: {unexpected}")

    undefined = run("nm", "-u", str(binary))
    strings = run("strings", str(binary))
    if re.search(r"\b(?:fopen|open|open64)@", undefined):
        raise RuntimeError("runtime file-open dependency")
    if "joe_kuo_6_21201.bin" in strings or "direction_numbers/" in strings:
        raise RuntimeError("Joe-Kuo runtime file reference")

    engine_calls = calls(binary, "asian_affine_family_engine_create")
    if "asian_meta_affine_plan_create" not in engine_calls or any(
            re.search(r"qsort|generic|oracle", name, re.I)
            for name in engine_calls):
        raise RuntimeError(f"normal engine call graph {engine_calls}")
    oracle_calls = calls(binary, "asian_affine_family_generic_oracle_create")
    if "asian_meta_qsort_control_plan_create" not in oracle_calls:
        raise RuntimeError("diagnostic oracle does not own qsort construction")

    growth_calls = calls(binary, "asian_affine_family_growth_carrier_prepare")
    xgrowth_calls = calls(binary, "asian_affine_family_xgrowth_carrier_prepare")
    if "asian_genuine_arithmetic_fused_source_exp_diag" not in growth_calls or \
       any("one_fma" in name or "vector_exp" in name for name in growth_calls):
        raise RuntimeError(f"growth-only carrier call graph {growth_calls}")
    if "asian_genuine_fixed_block_signed_z_one_fma_source_diag" not in xgrowth_calls or \
       "asian_vector_exp_range_reduced_array_diag" not in xgrowth_calls:
        raise RuntimeError(f"x/growth carrier call graph {xgrowth_calls}")

    request_symbols = [
        "asian_affine_family_arithmetic_request_prepare_growth",
        "asian_affine_family_arithmetic_request_prepare_xgrowth",
        "asian_affine_family_geocv_request_prepare",
    ]
    forbidden = re.compile(r"plan_create|qsort|source_exp_diag|one_fma|vector_exp|sha",
                           re.I)
    for symbol in request_symbols:
        closure = calls(binary, symbol)
        if any(forbidden.search(name) for name in closure):
            raise RuntimeError(f"request preparation contamination {symbol}:{closure}")

    liveness = import_audit(
        root / "tests/audit_asian_genuine_arithmetic_growth_only.py",
        "family_liveness")
    peaks = []
    for symbol in AFFINE_ARITH:
        code = body(binary, symbol)
        no_hot_hazards(code, symbol)
        count = len(re.findall(r"\bvpermd\b", code))
        if count != 2:
            raise RuntimeError(f"{symbol} has {count} growth permutations")
        if "0x240" in code:
            raise RuntimeError(f"generic pattern load in {symbol}")
        _, peak, _ = liveness.liveness(liveness.instructions(binary, symbol))
        peaks.append(peak)

    for symbol in AFFINE_GEOCV:
        code = body(binary, symbol)
        no_hot_hazards(code, symbol)
        if len(re.findall(r"\bvpermd\b", code)) != 4:
            raise RuntimeError(f"wrong affine dual permutation count in {symbol}")
        if len(re.findall(r"\bmovzx\b", code)) != 2 or \
           len(re.findall(r"\bvpbroadcastd\b", code)) != 1 or \
           len(re.findall(r"\bvpxord\b", code)) != 2:
            raise RuntimeError(f"controls reconstructed more than once in {symbol}")
        if "0x240" in code:
            raise RuntimeError(f"generic pattern load in {symbol}")
        _, peak, _ = liveness.liveness(liveness.instructions(binary, symbol))
        peaks.append(peak)

    affine_packet = body(binary, "asian_geometric_cv_packet_local_qg_diag")
    no_hot_hazards(affine_packet, "asian_geometric_cv_packet_local_qg_diag")
    if len(re.findall(r"\bvpermd\b", affine_packet)) != 4 or \
       "0x240" in affine_packet:
        raise RuntimeError("affine packet-local provider shape")
    _, peak, _ = liveness.liveness(liveness.instructions(
        binary, "asian_geometric_cv_packet_local_qg_diag"))
    peaks.append(peak)

    for symbol in GENERIC_GEOCV + ["asian_affine_family_generic_packet_qg_diag"]:
        code = body(binary, symbol)
        no_hot_hazards(code, symbol)
        if len(re.findall(r"\bvpermd\b", code)) != 4 or "0x240" not in code:
            raise RuntimeError(f"generic oracle provider shape in {symbol}")

    immediate_source = (root /
        "private/asian_affine_route_family_generic_immediate_avx512.S").read_text()
    if '#include "asian_n64_generic_geocv_immediate_avx512.s"' not in \
       immediate_source:
        raise RuntimeError("generic immediate donor is not imported exactly")

    cv_audit = import_audit(root / "tests/audit_asian_geometric_cv_packet_local.py",
                            "family_cv_canonical")
    observed_generic_packet = hashlib.sha256(cv_audit.canonical_disassembly(
        cv_audit.instructions(binary,
            "asian_affine_family_generic_packet_qg_diag"))).hexdigest()
    if observed_generic_packet != \
            "d31f2ec6c63ea8a6485eef16cb948afabb3edf20149e1e9e2b3a0a62b176eaaa":
        raise RuntimeError("generic packet donor canonical hash drift")

    peak_zmm = max(peaks)
    if peak_zmm >= 32:
        raise RuntimeError(f"peak ZMM liveness {peak_zmm}")
    print("family_audit PASS meta_descriptor_bytes=8192 "
          "normal_engine_generic_oracle=NO growth_only_produces_x=NO "
          "both_donor_regions=YES affine_growth_vpermd=2 "
          "affine_dual_vpermd=4 controls_reused=YES "
          f"generic_pattern_loads_affine=0 peak_zmm={peak_zmm} "
          "parent_blobs_unchanged=YES")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"family_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
