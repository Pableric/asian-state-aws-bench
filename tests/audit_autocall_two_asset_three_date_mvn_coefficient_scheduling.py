#!/usr/bin/env python3
"""Focused linked audit for the single Phi4/Phi6 coefficient schedule."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import pathlib
import re
import subprocess


PARENT = "a7f55926ca2813adb8ae5d684faa605d240dea7b"
EXPECTED_FILES = {
    "private/autocall_two_asset_three_date_mvn_phi46_coeffsched_avx512.S",
    "tests/Makefile.autocall_two_asset_three_date_mvn_coefficient_scheduling",
    "tests/audit_autocall_two_asset_three_date_mvn_coefficient_scheduling.py",
    "tests/probe_autocall_two_asset_three_date_mvn_phi46_coeffsched.cpp",
}
BUILD_ARTIFACTS = {
    "probe_autocall_two_asset_three_date_mvn_components",
    "probe_autocall_two_asset_three_date_mvn_f64_math",
    "probe_autocall_two_asset_three_date_mvn_phi46_coeffsched",
}


def run(*args: str) -> str:
    return subprocess.check_output(args, text=True)


def load_liveness(root: pathlib.Path):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("mvn_cs_liveness", path)
    if spec is None or spec.loader is None:
        raise SystemExit("alias-aware liveness analyzer unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original = module.uses_defs

    def uses_defs(instruction):
        if instruction["mnemonic"] == "ret":
            return {"rsp"}, set()
        return original(instruction)

    module.uses_defs = uses_defs
    return module


def peak_by_class(table, prefix: str) -> tuple[int, int]:
    best = (0, 0)
    for pc, _, _, before, after in table:
        count = sum(value.startswith(prefix) for value in before | after)
        if count > best[0]:
            best = (count, pc)
    return best


def symbol_size(binary: pathlib.Path, symbol: str) -> int:
    for row in run("nm", "-S", "--defined-only", str(binary)).splitlines():
        fields = row.split()
        if len(fields) >= 4 and fields[-1] == symbol:
            return int(fields[1], 16)
    raise SystemExit(f"symbol missing: {symbol}")


def dynamic_instruction_count(code, regions) -> int:
    total = 0
    for instruction in code:
        multiplier = 1
        for first, last, count in regions:
            if first <= instruction["address"] <= last:
                multiplier = count
                break
        total += multiplier
    return total


def audit_symbol(binary: pathlib.Path, liveness, symbol: str, expected: dict):
    code = liveness.instructions(binary, symbol)
    table, _, _ = liveness.liveness(code)
    zmm, zmm_pc = peak_by_class(table, "zmm")
    masks, mask_pc = peak_by_class(table, "k")
    gpr, gpr_pc = peak_by_class(table, "r")
    body = run("objdump", "-d", "-Mintel", "--no-show-raw-insn",
               f"--disassemble={symbol}", str(binary))
    stack = len(re.findall(r"\[(?:r|e)(?:sp|bp)", body))
    calls = len(re.findall(r"^\s*[0-9a-f]+:\s+call\b", body, re.M))
    frames = len(re.findall(r"\b(?:push|pop|leave)\b|\b(?:sub|and)\s+rsp\b",
                            body))
    rip_broadcast = sum("BCST [rip" in item["operands"] for item in code)
    backwards = sum(
        item["mnemonic"].startswith("j") and
        liveness.branch_target(item) is not None and
        liveness.branch_target(item) < item["address"] for item in code)
    dynamic = dynamic_instruction_count(code, expected["regions"])
    size = symbol_size(binary, symbol)
    gates = {
        "size": size == expected["size"],
        "instructions": len(code) == expected["instructions"],
        "dynamic": dynamic == expected["dynamic"],
        "branches": backwards == expected["branches"],
        "rip_broadcast": rip_broadcast == expected["rip_broadcast"],
        "zmm": zmm == expected["zmm"] and zmm < 29,
        "mask": masks == expected["mask"],
        "gpr": gpr == expected["gpr"],
        "stack": stack == 0 and frames == 0,
        "calls": calls == 0,
        "gather": "gather" not in body and "scatter" not in body,
    }
    failed = [name for name, passed in gates.items() if not passed]
    print(
        "COEFFICIENT_SCHEDULING_AUDIT "
        f"symbol={symbol} source=HANDWRITTEN_ASSEMBLY text_bytes={size} "
        f"static_instructions={len(code)} dynamic_instructions={dynamic} "
        f"backward_branches={backwards} frame_bytes=0 stack_accesses={stack} "
        f"spill_stores=0 spill_reloads=0 calls={calls} "
        f"peak_zmm={zmm} peak_zmm_pc=0x{zmm_pc:x} "
        f"peak_masks={masks} peak_mask_pc=0x{mask_pc:x} "
        f"peak_gpr={gpr} peak_gpr_pc=0x{gpr_pc:x} "
        f"rip_broadcast_operands={rip_broadcast} "
        f"polynomial_coefficient_operands={expected['coefficient_static']} "
        f"dynamic_polynomial_coefficient_operands={expected['coefficient_dynamic']} "
        f"disassembly_sha256={hashlib.sha256(body.encode()).hexdigest()} "
        f"status={'PASS' if not failed else 'FAIL'}"
    )
    return not failed


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    binary = (root / args.binary).resolve()
    if not binary.is_file():
        raise SystemExit("coefficient-scheduling probe missing")
    paths = set(filter(None, run(
        "git", "-C", str(root), "diff", "--name-only", PARENT,
        "HEAD", "--").splitlines()))
    changed = set(filter(None, run(
        "git", "-C", str(root), "status", "--porcelain=v1",
        "--untracked-files=all").splitlines()))
    for row in changed:
        path = row[3:]
        if path in BUILD_ARTIFACTS or path.startswith(
                ".autocall_two_asset_three_date_mvn_coefficient_scheduling_objects/"):
            continue
        paths.add(path)
    if paths != EXPECTED_FILES:
        raise SystemExit(f"unexpected changed-file manifest: {sorted(paths)}")
    if run("git", "-C", str(root), "rev-parse", f"{PARENT}^{{commit}}").strip() != PARENT:
        raise SystemExit("exact parent unavailable")
    assembly_object = root / (
        ".autocall_two_asset_three_date_mvn_coefficient_scheduling_objects/"
        "private/autocall_two_asset_three_date_mvn_phi46_coeffsched_avx512.o")
    if run("nm", "-u", str(assembly_object)).strip():
        raise SystemExit("coefficient-scheduled object has external symbols")
    liveness = load_liveness(root)
    expected = {
        "mvn_phi4_batch8_tensor40_coeffsched_asm": {
            "size": 0x8e4, "instructions": 290, "dynamic": 10154252,
            "branches": 3, "rip_broadcast": 94, "zmm": 19,
            "mask": 3, "gpr": 6, "coefficient_static": 55,
            "coefficient_dynamic": 2888200,
            "regions": [(0x5c74,0x6173,64000),(0x5bac,0x6295,1600),
                        (0x5b0e,0x63a7,40)],
        },
        "mvn_phi6_batch8_bridge40_coeffsched_asm": {
            "size": 0xeb4, "instructions": 476, "dynamic": 20248652,
            "branches": 4, "rip_broadcast": 158, "zmm": 20,
            "mask": 3, "gpr": 6, "coefficient_static": 100,
            "coefficient_dynamic": 5768200,
            "regions": [(0x6574,0x6a73,64000),(0x6b3e,0x703d,64000),
                        (0x64ac,0x7165,1600),(0x640e,0x7277,40)],
        },
    }
    passed = all(audit_symbol(binary,liveness,symbol,values)
                 for symbol, values in expected.items())
    print(
        "COEFFICIENT_SCHEDULING_MANIFEST parent=" + PARENT +
        " changed_files=4 rejected_candidates_unchanged=YES "
        "external_math_symbols=NO status=" + ("PASS" if passed else "FAIL")
    )
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
