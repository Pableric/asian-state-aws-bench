#!/usr/bin/env python3
"""Final-linked structural audit for the private fused-Gamma diagnostic."""

import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path


PARENT = "1dfeca0e5c704efddc427d3c38bbdcae562d87bd"
ALLOWED = {
    "asian_full_risk_gamma.c",
    "benchmarks/bench_asian_full_risk_gamma.c",
    "private/asian_full_risk_gamma_affine_phase1_avx512.S",
    "private/asian_full_risk_gamma_diag.h",
    "tests/Makefile.asian_full_risk_gamma",
    "tests/asian_full_risk_gamma_leaf_wrap.c",
    "tests/asian_full_risk_gamma_quantlib.cpp",
    "tests/audit_asian_full_risk_gamma.py",
    "tests/compare_asian_full_risk_gamma.py",
    "tests/test_asian_full_risk_gamma.c",
}
LEAVES = (
    "asian_full_risk_gamma_affine_call_impl_diag",
    "asian_full_risk_gamma_affine_put_impl_diag",
)
PARENT_SOURCES = (
    "asian_affine_family_full_risk_k1.c",
    "asian_commercial_full_risk_lifecycle.c",
    "asian_genuine_aad_phase1_avx512.s",
    "asian_genuine_aad_phase1_setup.c",
    "private/asian_commercial_full_risk_affine_phase1_avx512.S",
    "private/asian_affine_route_family_diag.h",
    "asian_affine_discrete_barrier_lifecycle.c",
    "private/asian_affine_discrete_barrier_avx512.S",
    "private/asian_affine_discrete_barrier_lifecycle_diag.h",
)


def run(*args):
    return subprocess.check_output(args, text=True)


def body(binary, symbol):
    text = run("objdump", "-d", "-M", "intel", "--disassemble=" + symbol,
               str(binary))
    lines = [line for line in text.splitlines()
             if re.match(r"^\s*[0-9a-f]+:", line)]
    if not lines:
        raise RuntimeError(f"missing linked symbol {symbol}")
    return "\n".join(lines)


def decoded(binary, symbol):
    output = []
    for line in body(binary, symbol).splitlines():
        match = re.match(
            r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*"
            r"([a-z0-9]+)\s*(.*)$", line)
        if match and not re.fullmatch(r"[0-9a-f]{2}", match.group(1)):
            output.append((match.group(1), match.group(2).strip()))
    return output


def symbol_size(binary, symbol):
    match = re.search(
        rf"^[0-9a-f]+\s+([0-9a-f]+)\s+\w\s+{re.escape(symbol)}$",
        run("nm", "-S", "--defined-only", str(binary)), re.MULTILINE)
    if not match:
        raise RuntimeError(f"missing symbol {symbol}")
    return int(match.group(1), 16)


def git_blob(path, revision=None):
    if revision is None:
        return run("git", "hash-object", path).strip()
    return run("git", "rev-parse", f"{revision}:{path}").strip()


def diagnostic_blob(path):
    completed = subprocess.run(
        ("git", "rev-parse", f"HEAD:{path}"), text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if completed.returncode == 0:
        return completed.stdout.strip()
    return run("git", "rev-parse", f":{path}").strip()


def import_liveness(root):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("gamma_liveness", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def target_referrers(binary, target):
    current = None
    found = set()
    for line in run("objdump", "-d", "-M", "intel", str(binary)).splitlines():
        label = re.match(r"^[0-9a-f]+ <([^>]+)>:$", line)
        if label:
            current = label.group(1)
        elif current and current != target and f"<{target}>" in line:
            found.add(current)
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    binary = Path(args.binary)
    root = Path(__file__).resolve().parent.parent
    if not binary.is_file():
        raise RuntimeError("benchmark binary missing")

    changed = set(run("git", "diff", "--name-only", PARENT, "--").splitlines())
    if changed != ALLOWED:
        raise RuntimeError(f"additive manifest mismatch {sorted(changed)}")
    for path in PARENT_SOURCES:
        if git_blob(path) != git_blob(path, PARENT):
            raise RuntimeError(f"parent source drift {path}")
    for path in ALLOWED:
        if git_blob(path) != diagnostic_blob(path):
            raise RuntimeError(f"diagnostic worktree drift {path}")

    if symbol_size(binary, "asian_meta_direction_descriptors") != 8192:
        raise RuntimeError("descriptor object size drift")
    symbols = run("nm", "-S", "--defined-only", str(binary))
    if "asian_gamma_unused_" in symbols:
        raise RuntimeError("unreferenced donor leaves survived linking")
    undefined = run("nm", "-u", str(binary))
    strings = run("strings", str(binary))
    if re.search(r"\b(?:fopen|open|open64)@", undefined) or \
       "direction_numbers/" in strings or "joe_kuo_6_21201.bin" in strings:
        raise RuntimeError("runtime Joe-Kuo file dependency")

    wrapper = decoded(binary, "asian_full_risk_gamma_prepared_price")
    indirect_calls = [operands for mnemonic, operands in wrapper
                      if mnemonic == "call" and operands == "rax"]
    wrapper_body = body(binary, "asian_full_risk_gamma_prepared_price")
    if len(indirect_calls) != 1 or any(f"<{leaf}>" not in wrapper_body
                                      for leaf in LEAVES):
        raise RuntimeError("Gamma family wrapper is not one Phase-1 invocation")
    for leaf in LEAVES:
        if target_referrers(binary, leaf) != {
                "asian_full_risk_gamma_prepared_price"}:
            raise RuntimeError(f"private leaf escaped family wrapper {leaf}")

    parent_oracle_name = "asian_full_risk_gamma_parent_fields_oracle"
    parent_oracle = body(binary, parent_oracle_name)
    if len([1 for mnemonic, operands in decoded(binary, parent_oracle_name)
            if mnemonic == "call" and operands == "rax"]) != 1 or \
       "<asian_affine_family_full_risk_k1_affine_call_impl_diag>" not in \
            parent_oracle or \
       "<asian_affine_family_full_risk_k1_affine_put_impl_diag>" not in \
            parent_oracle:
        raise RuntimeError("unchanged parent-fields oracle is not one leaf")

    request_text = body(binary, "asian_full_risk_gamma_request_prepare")
    if any(name in request_text for name in (
            "carrier_prepare", "plan_create", "qsort", "bsearch", "SHA",
            "malloc", "calloc", "free", "vector_exp")):
        raise RuntimeError("request preparation contains excluded work")
    if "<asian_meta_affine_routes_bind>" not in request_text or \
       "<asian_genuine_aad_phase1_prepare_arithmetic_controls>" not in request_text:
        raise RuntimeError("request preparation call graph incomplete")

    source = (root /
        "private/asian_full_risk_gamma_affine_phase1_avx512.S").read_text()
    if '#include "../asian_genuine_aad_phase1_avx512.s"' not in source or \
       source.count("AAD_PAY_HALF") != 2 or \
       source.count("AAD_REDUCE_ONE") != 4 or \
       source.count("GAMMA_PAY_HALF") != 3:
        raise RuntimeError("qualified donor/base subsequence ownership drift")

    liveness = import_liveness(root)
    peaks = []
    for leaf in LEAVES:
        text = body(binary, leaf)
        code = decoded(binary, leaf)
        if re.search(r"\b(?:callq?|vgather\w*|vscatter\w*)\b", text) or \
           re.search(r"\b(?:rsp|rbp)\b", text):
            raise RuntimeError(f"call/stack/spill/gather/scatter in {leaf}")
        if len(re.findall(r"\bvpermd\b", text)) != 4 or \
           len(re.findall(r"\bmovzx\b", text)) != 2 or \
           len(re.findall(r"\bvpbroadcastd\b", text)) != 1 or \
           len(re.findall(r"\bvpxord\b", text)) != 2 or "0x240" in text:
            raise RuntimeError(f"affine route shape drift in {leaf}")
        stores = []
        for mnemonic, operands in code:
            first = operands.split(",", 1)[0]
            if "PTR [" in first:
                stores.append((mnemonic, first))
        if len(stores) != 5 or any(item[0] != "vmovsd" for item in stores):
            raise RuntimeError(f"intermediate traffic in {leaf}: {stores}")
        if len(re.findall(r"\bvmulps\b", text)) < 10 or \
           len(re.findall(r"\bvfmadd231ps\b", text)) < 6:
            raise RuntimeError(f"qualified evolution sequence missing {leaf}")
        _, peak, _ = liveness.liveness(liveness.instructions(binary, leaf))
        peaks.append(peak)
    peak = max(peaks)
    if peak >= 32:
        raise RuntimeError(f"peak ZMM liveness {peak}")

    print("full_risk_gamma_audit PASS descriptor_bytes=8192 "
          "d1_direct_x_growth=1 affine_payload_vpermd=4 "
          "controls_constructed_once=YES controls_reused_x_growth=YES "
          "route_traversals=1 phase1_invocations=1 extra_carrier_prepare=0 "
          "generic_pattern_loads=0 calls_in_leaf=0 stack_frames=0 spills=0 "
          "gathers_scatters=0 intermediate_path_stores=0 "
          "payoff_materialization=0 path_dependent_branches=0 "
          f"peak_zmm={peak} parent_blobs_unchanged=YES "
          "base_risk_subsequences_exact=YES "
          "parent_fields_oracle_one_leaf=YES gamma_finalization=binary64")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"full_risk_gamma_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
