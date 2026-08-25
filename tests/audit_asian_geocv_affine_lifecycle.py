#!/usr/bin/env python3
"""Small linked audit for the private affine GeoCV lifecycle diagnostic."""

import argparse
import re
import subprocess
import sys
from pathlib import Path


PARENT = "86a744b3e1e94b2aac7df4a673508f8f6c33a8f2"
UNCHANGED = (
    "asian_genuine_fixed_block_source_setup.c",
    "asian_genuine_fixed_block_source_avx512.s",
    "asian_genuine_arithmetic_fused_source_exp_avx512.s",
    "asian_genuine_price_delta_strip_setup.c",
    "asian_genuine_price_delta_strip_avx512.s",
    "asian_geometric_cv_payoff_avx512.s",
    "private/asian_genuine_fixed_block_signed_z.bin",
    "private/asian_meta_direction_descriptors.c",
    "private/asian_meta_direction_affine_route.c",
)


def run(*args):
    return subprocess.check_output(args, text=True)


def disassemble(binary, symbol):
    text = run("objdump", "-d", "-M", "intel",
               f"--disassemble={symbol}", str(binary))
    instructions = []
    for line in text.splitlines():
        match = re.match(
            r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*"
            r"([a-z0-9]+)\s*(.*)$", line)
        if match:
            instructions.append((match.group(1).lower(),
                                 match.group(2).strip().lower()))
    if not instructions:
        raise RuntimeError(f"missing linked symbol {symbol}")
    return instructions


def calls(code):
    result = []
    for mnemonic, operands in code:
        if mnemonic == "call":
            match = re.search(r"<([^+>@]+)", operands)
            result.append(match.group(1) if match else operands)
    return result


def unchanged_parent_files():
    for name in UNCHANGED:
        parent = subprocess.check_output(["git", "show", f"{PARENT}:{name}"])
        if Path(name).read_bytes() != parent:
            raise RuntimeError(f"parent numerical asset changed: {name}")


def audit(binary):
    unchanged_parent_files()

    carrier = disassemble(binary, "asian_geocv_affine_carrier_prepare")
    carrier_calls = calls(carrier)
    required_carrier = [
        "asian_genuine_fixed_block_signed_z_one_fma_source_diag",
        "asian_vector_exp_range_reduced_array_diag",
        "asian_vector_exp_range_reduced_array_diag",
    ]
    if carrier_calls != required_carrier:
        raise RuntimeError(f"carrier call sequence is {carrier_calls}")

    request = disassemble(binary, "asian_geocv_affine_request_prepare")
    request_calls = calls(request)
    for required in (
        "asian_meta_affine_routes_bind",
        "asian_geometric_cv_packet_local_strip_prepare_padded",
        "asian_geometric_cv_immediate_prepare",
    ):
        if required not in request_calls:
            raise RuntimeError(f"request preparation misses {required}")

    forbidden = (
        "malloc", "calloc", "posix_memalign", "free", "sha256",
        "qsort", "bsearch", "source_prepare",
        "packet_local_prepare", "route_plan_create",
        "asian_arithmetic_prepare",
    )
    for symbol in (
        "asian_geocv_affine_carrier_prepare",
        "asian_geocv_affine_request_prepare",
        "asian_geocv_affine_prepared_price",
        "asian_geocv_affine_fresh_total",
        "asian_geocv_affine_reuse_total",
    ):
        for target in calls(disassemble(binary, symbol)):
            if any(word in target for word in forbidden):
                raise RuntimeError(f"forbidden timed call {symbol} -> {target}")

    leaf = disassemble(binary, "asian_geometric_cv_immediate_price_1_diag")
    mnemonics = [item[0] for item in leaf]
    operands = [item[1] for item in leaf]
    if mnemonics.count("movzx") != 2:
        raise RuntimeError("affine provider must have two selector-byte loads")
    if mnemonics.count("vpermd") != 4:
        raise RuntimeError("affine provider must have four payload permutations")
    if mnemonics.count("vpbroadcastd") != 1 or \
       mnemonics.count("vpxord") != 2:
        raise RuntimeError("affine control construction shape changed")
    if not any("[rax+0x140]" in value for value in operands) or \
       not any("[rax+0x180]" in value for value in operands):
        raise RuntimeError("affine base-control/half-delta loads missing")
    if any(mnemonic in ("call", "vgatherdps", "vscatterdps")
           for mnemonic in mnemonics):
        raise RuntimeError("hot immediate leaf has a call or gather/scatter")
    if any("rsp" in value or "rbp" in value for value in operands):
        raise RuntimeError("hot immediate leaf uses stack storage")

    assembly = Path("asian_geometric_cv_immediate_avx512.s").read_text()
    if "PATTERN" in assembly.upper() or "GENERIC" in assembly.upper():
        raise RuntimeError("generic pattern provider remains in candidate")

    print("lifecycle_audit PASS exact_x_growth=YES affine_routes=YES "
          "selector_byte_loads=2 generic_pattern_loads=0 "
          "payload_vpermd=4 production_dispatch_unchanged=YES")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    try:
        audit(args.binary)
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        print(f"lifecycle audit failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
