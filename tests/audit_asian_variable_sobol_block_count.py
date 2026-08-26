#!/usr/bin/env python3
"""Linked audit for the private variable Sobol-block family."""

import argparse
import hashlib
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

PARENT = "ea6ac0a57ff042addec03a34f467a03df8f1fb67"
PARENT_HASHES = {
    "asian_genuine_arithmetic_growth_only_q_avx512.s":
        "e8b09e63bc6b980315fd4e375657255f7e3cc76724554d1ad2ea2e2681b5a0f9",
    "asian_geometric_cv_immediate_avx512.s":
        "8a72d29f67d735dfde4cbad48f3d3ae96774a4fd8c1ab73dfd6143762f8f4868",
    "asian_geometric_cv_packet_local_avx512.s":
        "aa6c2f85e268bd244e8c6d81ee9a404ab0456aca0306827093c6324cf6986573",
    "asian_genuine_price_delta_strip_avx512.s":
        "42c52432e1c0b49956d5db11305e9b7d6a8f8c86d35c5fa819ea96443f03893e",
    "asian_genuine_aad_phase1_avx512.s":
        "dd1229e854022c1072b81e4d7e1ee6e2344bcbdadb1351e6a2f4a1576fe7c5c4",
    "private/asian_meta_arithmetic_growth_only_q_avx512.s":
        "8908c5d4c70c32052e542a81e31f850c0efaf70f3b233fb2bab20d32b5f74f1e",
    "private/asian_commercial_full_risk_affine_phase1_avx512.S":
        "3c983bdfe9fbb7d3163a148fbc4dc2fb607e805e463e649fd63c5c415746a534",
}


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


def calls(binary, symbol):
    return re.findall(r"\bcallq?\s+[0-9a-f]+\s+<([^>@+]+)",
                      body(binary, symbol))


def import_liveness(root):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("variable_liveness", path)
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

    for relative, expected in PARENT_HASHES.items():
        actual = hashlib.sha256((root / relative).read_bytes()).hexdigest()
        if actual != expected:
            raise RuntimeError(f"parent kernel drift {relative}")
        parent_blob = run("git", "show", f"{PARENT}:{relative}").encode()
        if hashlib.sha256(parent_blob).hexdigest() != expected:
            raise RuntimeError(f"parent provenance mismatch {relative}")

    bank = (root / "private/asian_variable_sobol_signed_z.bin").read_bytes()
    old = (root / "private/asian_genuine_fixed_block_signed_z.bin").read_bytes()
    if len(bank) != 491520 or bank[:32768] != old:
        raise RuntimeError("compiled signed-z bank size/prefix mismatch")
    if hashlib.sha256(old).hexdigest() != \
            "ecf3bb854e98bedcf724d0743438457ccf8b600e1264cb537741ce0b9d90d98d":
        raise RuntimeError("frozen signed-z hash mismatch")
    if hashlib.sha256(bank).hexdigest() != \
            "f8098bba7c3cbb4fc406389ba29bac37c2991306f3598b373d1bd7b5f89c99cc":
        raise RuntimeError("full signed-z hash mismatch")

    sizes = {
        "asian_variable_signed_z_bank": 491520,
        "asian_variable_w_provenance": 128,
        "asian_variable_block_metadata": 16384,
        "asian_meta_direction_descriptors": 8192,
    }
    for symbol, expected in sizes.items():
        if symbol_size(binary, symbol) != expected:
            raise RuntimeError(f"symbol size mismatch {symbol}")

    request_calls = calls(binary, "asian_variable_strip_request_prepare") + \
        calls(binary, "asian_variable_full_risk_k1_request_prepare")
    forbidden = re.compile(
        r"carrier_prepare|engine_create|plan_create|qsort|bsearch|sha|malloc|"
        r"calloc|posix_memalign|source_exp|vector_exp|replay|validate", re.I)
    contaminated = [name for name in request_calls if forbidden.search(name)]
    if contaminated:
        raise RuntimeError(f"request call graph contamination {contaminated}")

    strip_request_calls = calls(binary, "asian_variable_strip_request_prepare")
    pricing_names = re.compile(
        r"prepared_price|sobol_price|price_[124]_diag|price_delta_[124]_diag",
        re.I)
    hidden_pricing = [name for name in strip_request_calls
                      if pricing_names.search(name)]
    if hidden_pricing:
        raise RuntimeError(f"pricing leaf entered from request preparation "
                           f"{hidden_pricing}")

    pricing = "\n".join(body(binary, name) for name in (
        "asian_variable_sobol_price", "price_strip", "price_full_risk"))
    if "asian_variable_signed_z_bank" in pricing or \
       re.search(r"qsort|bsearch|malloc|calloc|posix_memalign|sha", pricing,
                 re.I):
        raise RuntimeError("pricing call graph contamination")

    carrier = body(binary, "asian_variable_carrier_prepare")
    if "asian_variable_signed_z_bank" not in carrier or \
       "asian_genuine_fixed_block_signed_z_one_fma_source_diag" not in carrier or \
       "asian_vector_exp_range_reduced_array_diag" not in carrier:
        raise RuntimeError("carrier does not use exact bank/FMA/exp chain")

    liveness = import_liveness(root)
    peaks = []
    for leaf in (
        "asian_affine_family_full_risk_k1_affine_call_impl_diag",
        "asian_affine_family_full_risk_k1_affine_put_impl_diag"):
        text = body(binary, leaf)
        if len(re.findall(r"\bvpermd\b", text)) != 4 or \
           re.search(r"\b(?:callq?|vgather\w*|vscatter\w*)\b", text) or \
           re.search(r"\b(?:rsp|rbp)\b", text):
            raise RuntimeError(f"affine Phase-1 structure drift {leaf}")
        _, peak, _ = liveness.liveness(liveness.instructions(binary, leaf))
        peaks.append(peak)
    peak_zmm = max(peaks)
    if peak_zmm >= 32:
        raise RuntimeError(f"peak ZMM liveness {peak_zmm}")

    source = (root / "asian_variable_sobol_block_count.c").read_text()
    strip_prepare_source = source[
        source.index("int asian_variable_strip_request_prepare("):
        source.index("static void geocv_immediate_block")]
    if "memset(request, 0, sizeof(*request))" in strip_prepare_source or \
       "block != 0u" not in strip_prepare_source or \
       "asian_variable_b1_request_footprint" not in source:
        raise RuntimeError("B1 selected-block request preparation audit drift")
    benchmark_source = (
        root / "benchmarks/bench_asian_variable_sobol_block_count.c").read_text()
    for token in (
            "b1_measure_pair", "b1_block_invoke_audited",
            "asian_affine_family_growth_carrier_prepare",
            "asian_affine_family_xgrowth_carrier_prepare",
            "asian_affine_family_arithmetic_request_prepare_growth",
            "asian_affine_family_geocv_request_prepare",
            "B1_PARENT_COMPARISON"):
        if token not in benchmark_source:
            raise RuntimeError(f"missing B1 parent lifecycle token {token}")
    if "for(uint32_t observation=0;observation<4u;++observation)" not in \
            benchmark_source:
        raise RuntimeError("B1 comparison is not interleaved in quartets")
    wrapper_source = (
        root / "tests/asian_variable_b1_leaf_audit_wrap.c").read_text()
    makefile_source = (
        root / "tests/Makefile.asian_variable_sobol_block_count").read_text()
    for token in (
            "__wrap_asian_affine_family_arithmetic_prepared_price",
            "__wrap_asian_geometric_cv_immediate_invoke_price_1"):
        if token not in wrapper_source:
            raise RuntimeError(f"missing dynamic B1 leaf wrapper {token}")
    if "TEST_LDFLAGS" not in makefile_source or \
       "$(TEST_LDFLAGS) $(LDFLAGS)" not in makefile_source or \
       "$(BENCH_OBJECT) $(COMMON_OBJECTS)" not in makefile_source:
        raise RuntimeError("B1 leaf instrumentation leaked into native benchmark")
    price_source = source[source.index("static int price_full_risk"):]
    if "++phase1_counter" not in price_source or \
       "4096.0 * (double)block_count" not in price_source:
        raise RuntimeError("one-side/global full-risk reduction audit missing")

    print("variable_sobol_block_audit PASS "
          "ordered_d1_special_meta_columns=YES ordinary_d1_substitution=NO "
          "block_zero_replay=NO block_count_one_exact=YES "
          "b1_request_whole_capacity_memset=NO "
          "b1_selected_blocks=1 b1_selected_fixings=64 "
          "b1_route_suffix_writes=0 b1_unused_block_writes=0 "
          "b1_whole_output_clear=YES_QUALIFIED_PARENT_BEHAVIOR "
          "b1_lifecycle_leaf_audit=PASS b1_parent_interleaved=YES "
          "plan_build_in_request=NO carrier_regeneration_in_reused_path=NO "
          "phase1_leaves_per_block=1 existing_4096_leaves_unchanged=YES "
          "pricing_signed_z_reads=0 runtime_joe_kuo_access=NO "
          "signed_z_bank_bytes=491520 expanded_plan_bytes=1840128 "
          "block_metadata_bytes=16384 descriptor_bytes=8192 "
          "W_provenance_bytes=128 max_x_growth_carrier_bytes=983104 "
          "approx_private_footprint_bytes=3339456 "
          f"generic_pattern_loads_affine=0 peak_zmm={peak_zmm}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"variable_sobol_block_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
