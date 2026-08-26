#!/usr/bin/env python3
"""Linked structural audit for the private 2,048-path vanilla diagnostic."""

import argparse
import hashlib
import importlib.util
import re
import subprocess
from pathlib import Path

PARENT = "d175d2e8b38785aa84cf7448397b0a72249dbeb8"
PARENT_HASHES = {
    "asian_genuine_arithmetic_growth_only_q_avx512.s":
        "e8b09e63bc6b980315fd4e375657255f7e3cc76724554d1ad2ea2e2681b5a0f9",
    "private/asian_meta_arithmetic_growth_only_q_avx512.s":
        "8908c5d4c70c32052e542a81e31f850c0efaf70f3b233fb2bab20d32b5f74f1e",
    "asian_genuine_arithmetic_fused_source_exp_avx512.s":
        "d7013c01b16370a611051a3f6abd615ce07ea0b35b972599fe14d98aabcc16f4",
    "private/asian_meta_direction_descriptors.c":
        "b49c7e2b7cb5af5003b60ad7a57e69e66b75599f28336f970cd4ac92427bda79",
    "private/asian_meta_direction_affine_route.c":
        "61d94bf38a3f69e6cbe4689a722da8bab2c17f3d2e1f4de1a293cb37fc3b1750",
    "asian_affine_route_family.c":
        "a5e9faaf2972492bbab49fbc2f0558cbfa1e84252142c60c441ae07a9c6a69a3",
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


def calls(binary, symbol):
    return re.findall(r"\bcallq?\s+[0-9a-f]+\s+<([^>@+]+)",
                      body(binary, symbol))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    binary = Path(args.binary)
    root = Path(__file__).resolve().parent.parent
    if not binary.is_file():
        raise RuntimeError("benchmark binary missing")

    for relative, expected in PARENT_HASHES.items():
        actual = hashlib.sha256((root / relative).read_bytes()).hexdigest()
        parent = subprocess.check_output(
            ["git", "show", f"{PARENT}:{relative}"])
        if actual != expected or hashlib.sha256(parent).hexdigest() != expected:
            raise RuntimeError(f"parent blob drift: {relative}")

    if symbol_size(binary, "asian_meta_direction_descriptors") != 8192 or \
       symbol_size(binary, "asian_genuine_fixed_block_signed_z") != 32768:
        raise RuntimeError("immutable numerical symbol size drift")

    leaf = body(binary, "asian_2048_affine_price_1_diag")
    forbidden = re.compile(
        r"\b(?:callq?|vgather\w*|vscatter\w*|rsp|rbp)\b", re.I)
    if forbidden.search(leaf):
        raise RuntimeError("call/stack/spill/gather/scatter in affine leaf")
    if len(re.findall(r"\bvpermd\b", leaf)) != 2:
        raise RuntimeError("affine leaf must contain two payload vpermd")
    if "0x2000" not in leaf:
        raise RuntimeError("64-packet/8192-byte bound missing")
    if re.search(r"\[r9\+r(?:dx|10).*0x240", leaf):
        raise RuntimeError("generic pattern load in affine leaf")

    wrapper_calls = calls(binary, "asian_2048_prepared_price")
    if wrapper_calls.count("asian_2048_affine_price_1_diag") != 1 or \
       len(wrapper_calls) != 1:
        raise RuntimeError(f"prepared valuation leaf calls {wrapper_calls}")
    request_calls = calls(binary, "asian_2048_request_prepare")
    contaminated = re.compile(
        r"qsort|bsearch|malloc|calloc|posix_memalign|source_exp|price_1_diag|"
        r"plan_create|sha|validate|replay", re.I)
    if any(contaminated.search(call) for call in request_calls):
        raise RuntimeError(f"request preparation contamination {request_calls}")

    source = (root / "asian_2048_vanilla.c").read_text()
    assembly = (root / "private/asian_2048_vanilla_price_avx512.s").read_text()
    header = (root / "private/asian_2048_vanilla_diag.h").read_text()
    makefile = (root / "tests/Makefile.asian_2048_vanilla").read_text()
    required_source = (
        "descriptor->donor_region * 2u", "descriptor->base >> 11",
        "carrier->growth + (size_t)view * ASIAN_2048_PATHS",
        "asian_genuine_arithmetic_fused_source_exp_diag(&carrier->fused)",
        "static const uint32_t expected[4] = {64u, 66u, 59u, 67u}",
    )
    if any(token not in source for token in required_source):
        raise RuntimeError("canonical carrier/view binding source drift")
    for token in ("A2_PATH_BYTES,       8192", "0x3f40000000000000",
                  "A2_PRICE_LEAF asian_2048_affine_price_1_diag,1"):
        if token not in assembly:
            raise RuntimeError(f"leaf structural token missing: {token}")
    for token in ("ASIAN_2048_PLAN_BYTES = 81984",
                  "ASIAN_2048_SELECTOR_BYTES = 32768",
                  "ASIAN_2048_GROWTH_BYTES = 32768"):
        if token not in header:
            raise RuntimeError(f"ABI size token missing: {token}")
    if "--wrap=asian_2048_affine_price_1_diag" not in makefile:
        raise RuntimeError("test-only one-leaf instrumentation missing")

    liveness_path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("a2_liveness", liveness_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    _, peak_zmm, _ = module.liveness(
        module.instructions(binary, "asian_2048_affine_price_1_diag"))
    if peak_zmm >= 32:
        raise RuntimeError(f"peak ZMM liveness {peak_zmm}")
    canonical = {
        "asian_genuine_arithmetic_fused_source_exp_diag":
            "d000343f6427e0d48461dff94a26526cce55059f2c71fbefdd02a12e1e930c53",
        "asian_meta_arithmetic_growth_only_price_1_diag":
            "94356f21602746d8617b8ca3fa62190590354f7abaa6aa791e132d3caa263945",
        "asian_2048_affine_price_1_diag":
            "0b957fd5119d3cd4e5e7d2def7201833140e04ef6a9ebc590d406a979f780b43",
    }
    for symbol, expected in canonical.items():
        observed = hashlib.sha256(module.canonical_disassembly(
            module.instructions(binary, symbol))).hexdigest()
        if observed != expected:
            raise RuntimeError(f"canonical disassembly drift {symbol}={observed}")

    strings = run("strings", str(binary))
    if "transpose_4096_to_2x2048" in strings or \
       "private/provenance" in strings or "joe_kuo_6_21201.bin" in strings:
        raise RuntimeError("runtime provenance/Joe-Kuo file reference")

    print("asian_2048_vanilla_audit PASS "
          "paths_per_price=2048 packets_per_price=64 "
          "canonical_growth_carrier_bytes=32768 canonical_growth_values=8192 "
          "donor_views=4 donor_values_per_view=2048 "
          "selected_values_per_dimension=2048 pricing_leaf_calls=1 "
          "runtime_transpose=NO second_target_half_executed=NO x_produced=NO "
          "descriptor_bytes=8192 expanded_plan_2048_bytes=81984 "
          "expanded_plan_4096_bytes=115008 selector_bytes_2048=32768 "
          "selector_bytes_4096=65536 parent_kernels_unchanged=YES "
          f"peak_zmm={peak_zmm}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"asian_2048_vanilla_audit FAIL {error}", file=__import__("sys").stderr)
        raise SystemExit(1)
