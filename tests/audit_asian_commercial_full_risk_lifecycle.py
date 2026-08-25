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
    "asian_affine_family_full_risk_k1.c",
    "asian_commercial_full_risk_lifecycle.c",
    "asian_genuine_aad_phase1_setup.c",
    "asian_genuine_multistrike_full_risk_setup.c",
    "benchmarks/bench_asian_commercial_full_risk_lifecycle.c",
    "private/asian_affine_route_family_diag.h",
    "private/asian_commercial_full_risk_affine_phase1_avx512.S",
    "private/asian_commercial_full_risk_lifecycle_diag.h",
    "private/asian_genuine_aad_phase1_diag.h",
    "private/asian_genuine_multistrike_full_risk_diag.h",
    "tests/Makefile.asian_commercial_full_risk_lifecycle",
    "tests/audit_asian_commercial_full_risk_lifecycle.py",
}

PAIRS = (
    ("asian_genuine_aad_phase1_forward_arithmetic_call_diag",
     "asian_affine_family_full_risk_k1_affine_call_impl_diag"),
    ("asian_genuine_aad_phase1_forward_arithmetic_put_diag",
     "asian_affine_family_full_risk_k1_affine_put_impl_diag"),
)

PARENT_KERNELS = (
    "asian_genuine_aad_phase1_avx512.s",
    "asian_genuine_arithmetic_growth_only_q_avx512.s",
    "asian_genuine_multistrike_full_risk_avx512.s",
    "asian_genuine_multistrike_full_risk_hybrid_dispatch.c",
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


def target_referrers(binary, target):
    current = None
    referrers = set()
    for line in run("objdump", "-d", "-M", "intel", str(binary)).splitlines():
        label = re.match(r"^[0-9a-f]+ <([^>]+)>:$", line)
        if label:
            current = label.group(1)
        elif current is not None and f"<{target}>" in line and \
                current != target:
            referrers.add(current)
    return referrers


def source_function(text, name):
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", text,
                      re.DOTALL)
    if not match:
        raise RuntimeError(f"cannot locate source function {name}")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():index + 1]
    raise RuntimeError(f"unterminated source function {name}")


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

    # Audit the committed diagnostic manifest, not unrelated untracked files
    # left by other benchmark branches in a long-lived AWS transport checkout.
    changed = set(run("git", "diff", "--name-only", PARENT, "HEAD", "--")
                  .splitlines())
    if changed != ALLOWED:
        raise RuntimeError(f"additive manifest mismatch {sorted(changed)}")
    for path in ALLOWED:
        if git_blob(path) != git_blob(path, "HEAD"):
            raise RuntimeError(f"diagnostic worktree drift {path}")
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

    request_calls = calls(
        binary, "asian_affine_family_full_risk_k1_request_prepare")
    forbidden_request = re.compile(
        r"plan_create|qsort|bsearch|carrier_prepare|source|vector_exp|sha|"
        r"malloc|calloc|free|replay|validate", re.I)
    if any(forbidden_request.search(call) for call in request_calls):
        raise RuntimeError(f"full-risk request contamination {request_calls}")
    if "asian_meta_affine_routes_bind" not in request_calls or \
       "asian_genuine_aad_phase1_prepare_arithmetic_controls" not in \
            request_calls or \
       "asian_genuine_msfr_prepare_arithmetic_strike" not in request_calls or \
       "asian_genuine_aad_phase1_prepare_controls" in request_calls:
        raise RuntimeError(f"full-risk request boundary incomplete {request_calls}")

    family_value = "asian_affine_family_full_risk_k1_prepared_price"
    affine_call = \
        "asian_affine_family_full_risk_k1_affine_call_impl_diag"
    affine_put = "asian_affine_family_full_risk_k1_affine_put_impl_diag"
    value_body = body(binary, family_value)
    value_calls = [(mnemonic, operands) for mnemonic, operands in
                   decoded(binary, family_value) if mnemonic == "call"]
    if value_calls != [("call", "rax")] or \
       f"<{affine_call}>" not in value_body or \
       f"<{affine_put}>" not in value_body:
        raise RuntimeError(
            f"family K=1 wrapper is not one implementation invocation "
            f"{value_calls}")

    independent = "asian_commercial_full_risk_independent_call_plus_put_diag"
    independent_code = decoded(binary, independent)
    independent_calls = [(mnemonic, operands) for mnemonic, operands in
                         independent_code if mnemonic == "call"]
    independent_jumps = [(mnemonic, operands) for mnemonic, operands in
                         independent_code if mnemonic == "jmp"]
    if len(independent_calls) != 1 or len(independent_jumps) != 1 or \
       f"<{affine_call}>" not in body(binary, independent) or \
       f"<{affine_put}>" not in body(binary, independent):
        raise RuntimeError("independent two-side diagnostic shape drift")

    allowed_impl_referrers = {family_value, independent}
    if target_referrers(binary, affine_call) != allowed_impl_referrers or \
       target_referrers(binary, affine_put) != allowed_impl_referrers:
        raise RuntimeError("direct affine implementation leaf escaped wrapper")

    generic_value = \
        "asian_commercial_full_risk_generic_one_side_oracle_diag"
    generic_body = body(binary, generic_value)
    generic_calls = [(mnemonic, operands) for mnemonic, operands in
                     decoded(binary, generic_value) if mnemonic == "call"]
    if generic_calls != [("call", "rax")] or \
       "<asian_genuine_aad_phase1_forward_arithmetic_call_diag>" not in \
            generic_body or \
       "<asian_genuine_aad_phase1_forward_arithmetic_put_diag>" not in \
            generic_body:
        raise RuntimeError("generic oracle is not one direct-side invocation")

    if symbol_size(binary, "forward_tape_sentinel") != 64 or \
       symbol_size(binary, "generic_forward_tape_sentinel") != 64:
        raise RuntimeError("forward tape sentinel ABI drift")

    arithmetic_control_body = body(
        binary, "asian_genuine_aad_phase1_prepare_arithmetic_controls")
    if re.search(r"erfc|geometric_exact|normal_cdf", arithmetic_control_body,
                 re.I):
        raise RuntimeError("arithmetic controls compute geometric Greeks")

    family_header = (root / "private/asian_affine_route_family_diag.h").read_text()
    commercial_header = (
        root / "private/asian_commercial_full_risk_lifecycle_diag.h").read_text()
    if affine_call in family_header or affine_put in family_header or \
       affine_call in commercial_header or affine_put in commercial_header:
        raise RuntimeError("implementation leaves exposed in private family ABI")
    if "ASIAN_AFFINE_FAMILY_FULL_RISK_K1_FORWARD_REQUEST_TAPE_BYTES = 0" \
            not in family_header or re.search(r"\bfloat\s+s_tape\s*\[",
                                               family_header):
        raise RuntimeError("dead forward request tape remains in family ABI")

    bench_source = (
        root / "benchmarks/bench_asian_commercial_full_risk_lifecycle.c"
        ).read_text()
    observe_source = source_function(bench_source, "observe")
    compare_source = source_function(bench_source, "compare_full_risk_case")
    condition_source = source_function(bench_source, "condition")
    if independent in observe_source or independent not in compare_source:
        raise RuntimeError("independent two-side diagnostic entered timing")
    if "generic_tape" in bench_source or "s_tape" in condition_source or \
       "sizeof(workspace->full_risk[0])" in condition_source:
        raise RuntimeError("candidate-warm touches unused forward request data")

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
          "mathematical_sequence_exact=YES family_k1_abi=CALL_AND_PUT "
          "phase1_leaves_per_prepared_valuation=1 "
          "generic_phase1_leaves_per_prepared_valuation=1 "
          "direct_leaves=PRIVATE_IMPLEMENTATION "
          "forward_request_tape_bytes=0 sentinel_bytes=64 "
          "arithmetic_controls_geometric_greeks=SKIPPED "
          "candidate_warm_unused_tape=NO "
          "independent_call_plus_put=RETAINED_NOT_TIMED")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"commercial_full_risk_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
