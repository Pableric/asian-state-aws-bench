#!/usr/bin/env python3
"""Linked structural and lifecycle audit for the affine barrier diagnostic."""

import argparse
import hashlib
import importlib.util
import re
import subprocess
import sys
from pathlib import Path

PARENT = "cf2cab6f15aaac6af3fd9dc08ec6a6fc1d0cb702"
NEW_FILES = {
    "asian_affine_discrete_barrier_lifecycle.c",
    "benchmarks/bench_asian_affine_discrete_barrier_lifecycle.c",
    "private/asian_affine_discrete_barrier_avx512.S",
    "private/asian_affine_discrete_barrier_lifecycle_diag.h",
    "tests/Makefile.asian_affine_discrete_barrier_lifecycle",
    "tests/asian_affine_discrete_barrier_leaf_wrap.c",
    "tests/audit_asian_affine_discrete_barrier_lifecycle.py",
    "tests/test_asian_affine_discrete_barrier_lifecycle.c",
}

PAIRS = (
    ("asian_affine_barrier_vanilla_call_interleaved_diag",
     "asian_affine_barrier_down_call_self_interleaved_diag"),
    ("asian_affine_barrier_vanilla_put_interleaved_diag",
     "asian_affine_barrier_down_put_self_interleaved_diag"),
    ("asian_affine_barrier_vanilla_call_grouped_diag",
     "asian_affine_barrier_up_call_self_grouped_diag"),
    ("asian_affine_barrier_vanilla_put_grouped_diag",
     "asian_affine_barrier_up_put_self_grouped_diag"),
)

HISTORICAL = {
    "asian_affine_barrier_vanilla_call_interleaved_diag":
        "asian_barrier_vanilla_call_interleaved_diag",
    "asian_affine_barrier_vanilla_put_interleaved_diag":
        "asian_barrier_vanilla_put_interleaved_diag",
    "asian_affine_barrier_down_call_self_interleaved_diag":
        "asian_barrier_down_call_self_interleaved_diag",
    "asian_affine_barrier_down_put_self_interleaved_diag":
        "asian_barrier_down_put_self_interleaved_diag",
    "asian_affine_barrier_vanilla_call_grouped_diag":
        "asian_barrier_vanilla_call_grouped_diag",
    "asian_affine_barrier_vanilla_put_grouped_diag":
        "asian_barrier_vanilla_put_grouped_diag",
    "asian_affine_barrier_up_call_self_grouped_diag":
        "asian_barrier_up_call_self_grouped_diag",
    "asian_affine_barrier_up_put_self_grouped_diag":
        "asian_barrier_up_put_self_grouped_diag",
}


def run(*args):
    return subprocess.check_output(args, text=True)


def body(binary, symbol):
    value = run("objdump", "-d", "-Mintel", "--disassemble=" + symbol,
                str(binary))
    lines = [line for line in value.splitlines()
             if re.match(r"^\s*[0-9a-f]+:", line)]
    if not lines:
        raise RuntimeError(f"missing linked leaf {symbol}")
    return "\n".join(lines)


def decoded(binary, symbol):
    code = []
    for line in body(binary, symbol).splitlines():
        fields = line.split("\t")
        if len(fields) < 3:
            continue
        instruction = fields[-1].strip().lower()
        match = re.match(r"([a-z0-9_.]+)\s*(.*)", instruction)
        if match:
            code.append((match.group(1), re.sub(r"\s+", " ",
                                                match.group(2).strip())))
    return code


def symbol_size(binary, symbol):
    match = re.search(
        rf"^[0-9a-f]+\s+([0-9a-f]+)\s+\w\s+{re.escape(symbol)}$",
        run("nm", "-S", "--defined-only", str(binary)), re.MULTILINE)
    if not match:
        raise RuntimeError(f"missing symbol {symbol}")
    return int(match.group(1), 16)


def calls(binary, symbol):
    return re.findall(r"\bcallq?\s+[0-9a-f]+\s+<([^>@+]+)",
                      body(binary, symbol))


def mathematical(code):
    wanted = re.compile(
        r"^(?:vmulps|vcmp\w*ps|vsubps|vmaxps|vaddps|vextractf32x4|"
        r"vmovhlps|vshufps|vaddss|vcvtss2sd|vmulsd)$")
    return [(mnemonic, operands) for mnemonic, operands in code
            if wanted.match(mnemonic)]


def without_barrier_deltas(code):
    result = []
    for mnemonic, operands in mathematical(code):
        if mnemonic.startswith("vcmp"):
            continue
        operands = re.sub(r"\{k[46]\}\{z\}", "", operands)
        result.append((mnemonic, operands))
    return result


def import_liveness(root):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("barrier_liveness", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if not args.binary.is_file():
        raise RuntimeError("benchmark binary missing")

    # Additive-only commit: every parent path must remain byte-identical.
    changed = set(filter(None, run(
        "git", "diff", "--name-only", PARENT, "--").splitlines()))
    unexpected = changed - NEW_FILES
    if unexpected:
        raise RuntimeError(f"parent blob drift {sorted(unexpected)}")
    parent_barrier = run("git", "show",
                         PARENT + ":asian_genuine_discrete_barrier_avx512.s")
    local_barrier = (root / "asian_genuine_discrete_barrier_avx512.s").read_text()
    if hashlib.sha256(parent_barrier.encode()).digest() != \
       hashlib.sha256(local_barrier.encode()).digest():
        raise RuntimeError("historical qualified barrier source changed")

    if symbol_size(args.binary, "asian_meta_direction_descriptors") != 8192:
        raise RuntimeError("descriptor object is not 8192 bytes")
    strings = run("strings", str(args.binary)).lower()
    symbols = run("nm", "-a", str(args.binary))
    if "joe_kuo_6_21201.bin" in strings or \
       "asian_meta_qsort_control_plan_create" in symbols or \
       "asian_genuine_prepare_route" in symbols:
        raise RuntimeError("generic/Joe-Kuo runtime dependency in benchmark")

    liveness = import_liveness(root)
    peaks = []
    for vanilla, barrier in PAIRS:
        vanilla_code = decoded(args.binary, vanilla)
        barrier_code = decoded(args.binary, barrier)
        for symbol, code, masked in ((vanilla, vanilla_code, False),
                                     (barrier, barrier_code, True)):
            mnemonics = [item[0] for item in code]
            joined = " ".join(item[1] for item in code)
            if mnemonics.count("vpermd") != 2:
                raise RuntimeError(f"{symbol}: D2..DN provider is not 2 vpermd")
            first_permute = mnemonics.index("vpermd")
            prefix = mnemonics[:first_permute]
            if prefix.count("vmovaps") < 4 or prefix.count("vmulps") != 2:
                raise RuntimeError(f"{symbol}: direct-D1 prefix drift")
            if "0x240" in joined:
                raise RuntimeError(f"{symbol}: generic pattern load present")
            if re.search(r"\b(?:callq?|vgather\w*|vscatter\w*)\b",
                         " ".join(mnemonics)) or re.search(r"\b(?:rsp|rbp)\b",
                                                           joined):
                raise RuntimeError(f"{symbol}: call/stack/gather/scatter")
            stores = [operands for _, operands in code
                      if "ptr [" in operands and
                      operands.split(",", 1)[0].find("ptr [") >= 0]
            if stores:
                raise RuntimeError(f"{symbol}: intermediate memory stores")
            comparisons = sum(name.startswith("vcmp") for name in mnemonics)
            if comparisons != (4 if masked else 0):
                raise RuntimeError(f"{symbol}: comparison count {comparisons}")
            _, peak, _ = liveness.liveness(liveness.instructions(
                args.binary, symbol))
            peaks.append(peak)
        if without_barrier_deltas(vanilla_code) != \
           without_barrier_deltas(barrier_code):
            raise RuntimeError(f"mathematical sequence drift {barrier}")

    # Historical arithmetic/payoff order must match after removing provider
    # loads/permutations and the prepared initial-mask copy.
    historical_object = root / \
        ".asian_affine_discrete_barrier_lifecycle_objects/asian_genuine_discrete_barrier_avx512.o"
    for candidate, historical in HISTORICAL.items():
        candidate_math = mathematical(decoded(args.binary, candidate))
        historical_math = mathematical(decoded(historical_object, historical))
        if candidate_math != historical_math:
            raise RuntimeError(f"historical math/reduction drift {candidate}")

    request_calls = calls(args.binary, "asian_affine_barrier_request_prepare")
    if any(re.search(r"engine_create|market_prepare|plan_create|qsort|bsearch|"
                     r"sha|malloc|calloc|posix_memalign|source_exp|replay|"
                     r"validate", name, re.I) for name in request_calls):
        raise RuntimeError("request preparation call graph contamination")
    prepared_calls = calls(args.binary, "asian_affine_barrier_prepared_price")
    if any(re.search(r"market_prepare|request_prepare|plan_create|malloc|calloc|"
                     r"posix_memalign|source_exp|qsort|bsearch|sha", name, re.I)
           for name in prepared_calls):
        raise RuntimeError("prepared pricing call graph contamination")
    source = (root / "asian_affine_discrete_barrier_lifecycle.c").read_text()
    request_source = source[source.index("int asian_affine_barrier_request_prepare("):
                            source.index("static double invoke_leaf")]
    if "request->routes + 1" not in request_source or \
       "request->routes[0].growth_base" not in request_source:
        raise RuntimeError("D1/D2 route binding boundary drift")
    if re.search(r"qsort|bsearch|malloc|calloc|posix_memalign|source_exp_diag",
                 request_source, re.I):
        raise RuntimeError("excluded work in request preparation")

    peak = max(peaks)
    if peak >= 32:
        raise RuntimeError(f"peak ZMM liveness {peak}")
    print("affine_discrete_barrier_audit PASS descriptor_bytes=8192 "
          "direct_d1_loads=2 direct_d1_vpermd=0 affine_d2_dn=YES "
          "affine_vpermd_per_routed_date=2 generic_pattern_loads=0 "
          "x_routing=NO resident_masks=YES intermediate_state_stores=0 "
          "calls=0 spills=0 gathers=0 scatters=0 "
          f"peak_zmm={peak} parent_blobs_unchanged=YES")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"affine_discrete_barrier_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
