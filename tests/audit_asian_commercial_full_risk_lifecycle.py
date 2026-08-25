#!/usr/bin/env python3
"""Linked structural audit for the private commercial lifecycle diagnostic."""

import argparse
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

PARENT = "bea7101d99085615b9448075f27bb64949508fe8"
ALLOWED = {
    "asian_commercial_full_risk_lifecycle.c",
    "benchmarks/bench_asian_commercial_full_risk_lifecycle.c",
    "private/asian_commercial_full_risk_affine_phase1_avx512.S",
    "private/asian_commercial_full_risk_lifecycle_diag.h",
    "tests/Makefile.asian_commercial_full_risk_lifecycle",
    "tests/audit_asian_commercial_full_risk_lifecycle.py",
}

PAIRS = (
    ("asian_genuine_aad_phase1_forward_arithmetic_call_diag",
     "asian_commercial_full_risk_affine_call_diag"),
    ("asian_genuine_aad_phase1_forward_arithmetic_put_diag",
     "asian_commercial_full_risk_affine_put_diag"),
)

PARENT_KERNELS = (
    "asian_genuine_aad_phase1_avx512.s",
    "asian_genuine_arithmetic_growth_only_q_avx512.s",
    "asian_genuine_price_delta_strip_avx512.s",
    "private/asian_meta_arithmetic_growth_only_q_avx512.s",
    "private/asian_meta_direction_descriptors.c",
    "private/asian_meta_direction_affine_route.c",
    "asian_affine_route_family.c",
)


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
    text = run("objdump", "-d", "-M", "intel", "--disassemble=" + symbol,
               str(binary))
    lines = [line for line in text.splitlines()
             if re.match(r"^\s*[0-9a-f]+:", line)]
    if not lines:
        raise RuntimeError(f"empty symbol {symbol}")
    return "\n".join(lines)


def decoded(binary, symbol):
    code = []
    for line in body(binary, symbol).splitlines():
        match = re.match(
            r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*"
            r"([a-z0-9]+)\s*(.*)$", line)
        if match and not re.fullmatch(r"[0-9a-f]{2}", match.group(1)):
            code.append((match.group(1), match.group(2).strip()))
    return code


def normalized(item):
    mnemonic, operands = item
    if mnemonic.startswith("j"):
        operands = "BRANCH"
    operands = re.sub(r"\s+#.*$", "", operands)
    operands = re.sub(r"\[rip[+-]0x[0-9a-f]+\]", "[rip+DISP]", operands)
    return mnemonic + " " + operands


def math_slices(code):
    starts = [index for index, item in enumerate(code)
              if normalized(item) == "vmulps zmm4,zmm4,zmm12"]
    if len(starts) != 2:
        raise RuntimeError("cannot locate direct and routed math boundaries")
    provider_end = starts[1]
    provider_start = max(index for index, item in enumerate(code[:provider_end])
                         if normalized(item) == "kmovd eax,k5") + 1
    prefix = [normalized(item) for item in code[:provider_start]]
    suffix = [normalized(item) for item in code[provider_end:]]
    return prefix, suffix


def calls(binary, symbol):
    return re.findall(r"\bcallq?\s+[0-9a-f]+\s+<([^>@+]+)",
                      body(binary, symbol))


def git_blob(path, revision=None):
    if revision is None:
        return run("git", "hash-object", path).strip()
    return run("git", "rev-parse", f"{revision}:{path}").strip()


def import_liveness(root):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("commercial_liveness", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    binary = Path(args.binary)
    root = Path(__file__).resolve().parent.parent
    if not binary.is_file():
        raise RuntimeError("linked benchmark missing")

    changed = set(run("git", "diff", "--name-only", PARENT, "--").splitlines())
    changed.update(run("git", "ls-files", "--others", "--exclude-standard")
                   .splitlines())
    changed.discard("bench_asian_commercial_full_risk_lifecycle")
    if changed != ALLOWED:
        raise RuntimeError(f"additive manifest mismatch {sorted(changed)}")
    for path in PARENT_KERNELS:
        if git_blob(path) != git_blob(path, PARENT):
            raise RuntimeError(f"parent kernel blob drift {path}")

    if symbol_size(binary, "asian_meta_direction_descriptors") != 8192:
        raise RuntimeError("descriptor object is not 8192 bytes")
    symbols = run("nm", "-S", "--defined-only", str(binary))
    if "asian_commercial_unused_" in symbols:
        raise RuntimeError("unreferenced donor leaves survived linking")
    undefined = run("nm", "-u", str(binary))
    strings = run("strings", str(binary))
    if re.search(r"\b(?:fopen|open|open64)@", undefined) or \
       "joe_kuo_6_21201.bin" in strings or "direction_numbers/" in strings:
        raise RuntimeError("runtime Joe-Kuo file dependency")

    engine_calls = calls(binary, "asian_affine_family_engine_create")
    if "asian_meta_affine_plan_create" not in engine_calls or any(
            re.search(r"qsort|oracle|generic", call, re.I)
            for call in engine_calls):
        raise RuntimeError(f"normal engine builds generic plan {engine_calls}")
    oracle_calls = calls(binary, "asian_affine_family_generic_oracle_create")
    if "asian_meta_qsort_control_plan_create" not in oracle_calls:
        raise RuntimeError("generic diagnostic oracle is not explicit")

    request_calls = calls(binary, "asian_commercial_full_risk_request_prepare")
    forbidden_request = re.compile(
        r"plan_create|qsort|bsearch|carrier_prepare|source|vector_exp|sha|"
        r"malloc|calloc|free|replay|validate", re.I)
    if any(forbidden_request.search(call) for call in request_calls):
        raise RuntimeError(f"full-risk request contamination {request_calls}")
    if "asian_meta_affine_routes_bind" not in request_calls or \
       "asian_genuine_aad_phase1_prepare_controls" not in request_calls:
        raise RuntimeError(f"full-risk request boundary incomplete {request_calls}")

    carrier_calls = calls(binary, "asian_affine_family_xgrowth_carrier_prepare")
    if "asian_genuine_fixed_block_signed_z_one_fma_source_diag" not in \
            carrier_calls or \
       "asian_vector_exp_range_reduced_array_diag" not in carrier_calls:
        raise RuntimeError(f"x/growth carrier is not qualified {carrier_calls}")

    liveness = import_liveness(root)
    peaks = []
    for generic, affine in PAIRS:
        generic_code = decoded(binary, generic)
        affine_code = decoded(binary, affine)
        if math_slices(generic_code) != math_slices(affine_code):
            raise RuntimeError(f"qualified arithmetic sequence drift {affine}")
        text = body(binary, affine)
        if re.search(r"\b(?:callq?|vgather\w*|vscatter\w*)\b", text) or \
           re.search(r"\b(?:rsp|rbp)\b", text):
            raise RuntimeError(f"call/stack/spill/gather/scatter in {affine}")
        if len(re.findall(r"\bvpermd\b", text)) != 4 or \
           len(re.findall(r"\bmovzx\b", text)) != 2 or \
           len(re.findall(r"\bvpbroadcastd\b", text)) != 1 or \
           len(re.findall(r"\bvpxord\b", text)) != 2 or "0x240" in text:
            raise RuntimeError(f"affine provider shape drift {affine}")
        stores = []
        for mnemonic, operands in affine_code:
            first = operands.split(",", 1)[0]
            if "PTR [" in first:
                stores.append((mnemonic, first))
        if len(stores) != 4 or any(name != "vmovsd" for name, _ in stores):
            raise RuntimeError(f"intermediate state traffic in {affine}:{stores}")
        _, peak, _ = liveness.liveness(liveness.instructions(binary, affine))
        peaks.append(peak)

    peak_zmm = max(peaks)
    if peak_zmm >= 32:
        raise RuntimeError(f"affine Phase-1 peak ZMM liveness {peak_zmm}")

    source = (root /
        "private/asian_commercial_full_risk_affine_phase1_avx512.S").read_text()
    if '#include "../asian_genuine_aad_phase1_avx512.s"' not in source:
        raise RuntimeError("qualified Phase-1 donor is not imported verbatim")

    print("commercial_full_risk_audit PASS descriptor_bytes=8192 "
          "normal_engine_generic_oracle=NO arithmetic_policy_N64_N256=AFFINE "
          "full_risk_affine_vpermd=4 selector_loads=2 controls_reused=YES "
          "generic_pattern_loads_affine=0 intermediate_state_traffic=0 "
          f"peak_zmm={peak_zmm} parent_kernels_unchanged=YES "
          "mathematical_sequence_exact=YES")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"commercial_full_risk_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
