#!/usr/bin/env python3
"""Linked audit for the bounded N64 arithmetic/GeoCV comparison."""

import argparse
import hashlib
import importlib.util
import re
import subprocess
import sys
from pathlib import Path


PARENT = "8a8205017eead21f7c555cc7059b570ff1ebd59a"
PARENT_SOURCES = (
    "asian_genuine_fixed_block_source_setup.c",
    "asian_genuine_fixed_block_source_avx512.s",
    "asian_genuine_arithmetic_fused_source_exp_avx512.s",
    "asian_genuine_arithmetic_growth_only_strip_adapter.c",
    "private/asian_meta_direction_descriptors.c",
    "private/asian_meta_direction_affine_route.c",
    "private/asian_meta_direction_qsort_control.c",
    "private/asian_meta_arithmetic_growth_only_q_avx512.s",
    "asian_geometric_cv_packet_local_setup.c",
    "asian_geometric_cv_immediate_setup.c",
    "asian_geometric_cv_immediate_avx512.s",
    "asian_geocv_affine_lifecycle.c",
)
CANONICAL = {
    "asian_genuine_fixed_block_signed_z_one_fma_source_diag":
        "dad5e1c70b10ce57514e58d0b51555060072b074a7ca19453ed4bf5b654fd0f3",
    "asian_vector_exp_range_reduced_array_diag":
        "d37e9402bcb0e7d02b6b61d8a85230286bf79bcaf83a2ca52750d6c125066178",
    "asian_genuine_arithmetic_fused_source_exp_diag":
        "d000343f6427e0d48461dff94a26526cce55059f2c71fbefdd02a12e1e930c53",
    "asian_meta_arithmetic_growth_only_price_1_diag":
        "94356f21602746d8617b8ca3fa62190590354f7abaa6aa791e132d3caa263945",
    "asian_geometric_cv_immediate_price_1_diag":
        "8b3d57123e3e0ae6a715995a1d2e0f25b89cef8cbfb60c8236492ac909d2b361",
}
DONOR_SHA256 = "1cd7854d811bc81d266a1201a006b4bf4b3d960f26db3aa6b784dd1cd52fe62c"


def output(*args):
    return subprocess.check_output(args, text=True)


def load_instruction_audit(root):
    path = root / "tests/audit_asian_geometric_cv_immediate.py"
    spec = importlib.util.spec_from_file_location("immediate_audit", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def functions(binary):
    text = output("objdump", "-d", "-M", "intel", str(binary))
    marks = list(re.finditer(r"^([0-9a-f]+) <([^>]+)>:$", text, re.MULTILINE))
    result = {}
    for index, mark in enumerate(marks):
        end = marks[index + 1].start() if index + 1 < len(marks) else len(text)
        result[mark.group(2)] = text[mark.end():end]
    return result


def direct_calls(body):
    return set(re.findall(r"\bcallq?\s+[0-9a-f]+\s+<([^>@+]+)", body))


def reachable(graph, roots):
    seen = set()
    pending = list(roots)
    while pending:
        name = pending.pop()
        if name in seen:
            continue
        seen.add(name)
        pending.extend(graph.get(name, ()))
    return seen


def normalize_suffix(code):
    result = []
    for item in code:
        operands = re.sub(r"\s+#.*$", "", item["operands"])
        operands = re.sub(r"\[rip[+-]0x[0-9a-f]+\]", "[rip+REL]", operands)
        if item["mnemonic"].startswith("j"):
            operands = "branch_target"
        operands = re.sub(r"\b[0-9a-f]+\s+<", "<", operands)
        result.append((item["mnemonic"], operands))
    return result


def unchanged_parent_sources(root):
    for relative in PARENT_SOURCES:
        parent = subprocess.check_output(["git", "show", f"{PARENT}:{relative}"])
        if (root / relative).read_bytes() != parent:
            raise RuntimeError(f"parent source changed: {relative}")


def audit(binary):
    root = Path(__file__).resolve().parent.parent
    unchanged_parent_sources(root)
    helper = load_instruction_audit(root)

    generic_source = (root /
        "private/asian_n64_generic_geocv_immediate_avx512.s").read_text()
    restored = generic_source.replace(
        "asian_n64_generic_geocv_immediate_",
        "asian_geometric_cv_immediate_")
    one_leaf = "IMMEDIATE_LEAF asian_geometric_cv_immediate_price_1_diag,1,0\n"
    full_leaf_set = one_leaf + \
        "IMMEDIATE_LEAF asian_geometric_cv_immediate_price_delta_1_diag,1,1\n" + \
        "IMMEDIATE_LEAF asian_geometric_cv_immediate_price_2_diag,2,0\n" + \
        "IMMEDIATE_LEAF asian_geometric_cv_immediate_price_delta_2_diag,2,1\n" + \
        "IMMEDIATE_LEAF asian_geometric_cv_immediate_price_4_diag,4,0\n" + \
        "IMMEDIATE_LEAF asian_geometric_cv_immediate_price_delta_4_diag,4,1\n"
    restored = restored.replace(one_leaf, full_leaf_set).encode()
    if hashlib.sha256(restored).hexdigest() != DONOR_SHA256:
        raise RuntimeError("generic immediate source is not the exact donor")

    bodies = functions(binary)
    roots = {
        "asian_n64_arithmetic_carrier_prepare",
        "asian_n64_arithmetic_request_prepare",
        "asian_n64_arithmetic_prepared_price",
        "asian_n64_arithmetic_fresh_total",
        "asian_n64_arithmetic_reuse_total",
        "asian_geocv_affine_carrier_prepare",
        "asian_n64_geocv_request_prepare",
        "asian_n64_geocv_prepared_price",
        "asian_n64_geocv_fresh_total",
        "asian_n64_geocv_reuse_total",
    }
    missing = roots - bodies.keys()
    if missing:
        raise RuntimeError(f"missing timed roots: {sorted(missing)}")
    graph = {name: direct_calls(body) for name, body in bodies.items()}
    timed = reachable(graph, roots)
    forbidden = re.compile(
        r"sha256|route_plan_create|qsort_control_plan_create|"
        r"affine_plan_create|malloc|calloc|posix_memalign|free|"
        r"prepare_route|map_source|packet_local_prepare|"
        r"fixed_block_source_prepare|fused_source_exp_prepare",
        re.IGNORECASE)
    bad = sorted(name for name in timed if forbidden.search(name))
    if bad:
        raise RuntimeError(f"forbidden timed call graph: {bad}")

    fresh_calls = direct_calls(bodies["asian_n64_geocv_fresh_total"])
    reuse_calls = direct_calls(bodies["asian_n64_geocv_reuse_total"])
    if "asian_geocv_affine_carrier_prepare" not in fresh_calls or \
       "asian_n64_geocv_request_prepare" not in fresh_calls or \
       "asian_n64_geocv_request_prepare" not in reuse_calls:
        raise RuntimeError("GeoCV candidates do not share lifecycle preparation")
    request_calls = direct_calls(bodies["asian_n64_geocv_request_prepare"])
    if "asian_geometric_cv_packet_local_strip_prepare_padded" not in request_calls:
        raise RuntimeError("shared GeoCV request preparation is missing")
    if "asian_geometric_cv_immediate_prepare" in request_calls:
        raise RuntimeError("candidate-specific validation entered request timing")

    generic_name = "asian_n64_generic_geocv_immediate_price_1_diag"
    affine_name = "asian_geometric_cv_immediate_price_1_diag"
    arithmetic_name = "asian_meta_arithmetic_growth_only_price_1_diag"
    generic = helper.instructions(binary, generic_name)
    affine = helper.instructions(binary, affine_name)
    arithmetic = helper.instructions(binary, arithmetic_name)
    for name, code, permutations in (
            (generic_name, generic, 4),
            (affine_name, affine, 4),
            (arithmetic_name, arithmetic, 2)):
        mnemonics = [item["mnemonic"] for item in code]
        if mnemonics.count("vpermd") != permutations:
            raise RuntimeError(f"payload permutation count changed in {name}")
        if any(item["mnemonic"].startswith("call") or
               "gather" in item["mnemonic"] or
               "scatter" in item["mnemonic"] for item in code):
            raise RuntimeError(f"call/gather/scatter in timed leaf {name}")
        if any(re.search(r"\b(?:rsp|rbp)\b", item["operands"])
               for item in code):
            raise RuntimeError(f"stack spill/reference in timed leaf {name}")
        stores = helper.memory_stores(code)
        if len(stores) != 2 or any(item["mnemonic"] != "vmovsd"
                                   for item in stores):
            raise RuntimeError(f"intermediate state traffic in {name}")

    generic_fourth = [i for i, item in enumerate(generic)
                      if item["mnemonic"] == "vpermd"][3]
    affine_fourth = [i for i, item in enumerate(affine)
                     if item["mnemonic"] == "vpermd"][3]
    if normalize_suffix(generic[generic_fourth + 1:]) != \
       normalize_suffix(affine[affine_fourth + 1:]):
        raise RuntimeError("GeoCV arithmetic/payoff/reduction sequence differs")

    generic_m = [item["mnemonic"] for item in generic]
    affine_m = [item["mnemonic"] for item in affine]
    if generic_m.count("movzx") != 4 or affine_m.count("movzx") != 2:
        raise RuntimeError("generic/affine provider selector shapes are wrong")
    if not any("+0x240" in item["operands"] for item in generic) or \
       any("+0x240" in item["operands"] for item in affine):
        raise RuntimeError("generic pattern-load separation is not exact")

    peaks = [helper.liveness(code) for code in (generic, affine, arithmetic)]
    peak = max(peaks)
    if peak >= 32:
        raise RuntimeError(f"timed leaf peak ZMM liveness is {peak}")

    for symbol, expected in CANONICAL.items():
        observed = hashlib.sha256(helper.canonical_disassembly(
            helper.instructions(binary, symbol))).hexdigest()
        if observed != expected:
            raise RuntimeError(
                f"parent canonical hash changed: {symbol}={observed}")

    undefined = output("nm", "-u", str(binary))
    if re.search(r"\b(?:fopen|open|open64)@", undefined):
        raise RuntimeError("runtime file access linked into diagnostic")

    print("comparison_audit PASS shared_x_growth_carrier=YES "
          "shared_geocv_request_prepare=YES provider_only_difference=YES "
          "math_sequences_unchanged=YES intermediate_state_stores=0 "
          f"peak_zmm={peak} parent_hashes_unchanged=YES")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    try:
        audit(args.binary)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"comparison_audit FAIL {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
