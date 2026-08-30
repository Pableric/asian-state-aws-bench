#!/usr/bin/env python3
"""Linked/additive audit for the private MVN probability-program diagnostic."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import pathlib
import re
import subprocess
import sys


PARENT = "4441a105e7827015b64d81f1fb6c316832555462"
ALLOWED_PREFIXES = ("private/", "tests/", "benchmarks/")
FORBIDDEN_NEW_SUFFIXES = (".json", ".csv")
BUILD_ARTIFACTS = {
    "reference_autocall_two_asset_three_date_mvn_program",
    "probe_autocall_two_asset_three_date_mvn_components",
    "probe_autocall_two_asset_three_date_mvn_f64_math",
    "probe_autocall_two_asset_three_date_mvn_phi2",
    "probe_autocall_two_asset_three_date_mvn_phi46",
    "test_autocall_two_asset_three_date_mvn_program",
    "benchmark_autocall_two_asset_three_date_mvn_program",
}


def run(*args: str) -> str:
    return subprocess.check_output(args, text=True).strip()


def import_liveness(root: pathlib.Path):
    path = root / "tests/audit_asian_genuine_arithmetic_growth_only.py"
    spec = importlib.util.spec_from_file_location("mvn_program_liveness", path)
    if spec is None or spec.loader is None:
        raise SystemExit("alias-aware liveness analyzer unavailable")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", required=True)
    parser.add_argument("--f64-math-probe", required=True)
    parser.add_argument("--phi2-probe", required=True)
    parser.add_argument("--phi46-probe", required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]

    parent = run("git", "-C", str(root), "rev-parse", f"{PARENT}^{{commit}}")
    if parent != PARENT:
        raise SystemExit("parent commit unavailable")

    changed = run("git", "-C", str(root), "diff", "--name-only", PARENT, "--")
    staged_or_untracked = run(
        "git", "-C", str(root), "status", "--porcelain=v1", "--untracked-files=all"
    )
    names = set(filter(None, changed.splitlines()))
    for line in staged_or_untracked.splitlines():
        if line.startswith("?? "):
            name = line[3:]
            if name in BUILD_ARTIFACTS or name.startswith(
                ".autocall_two_asset_three_date_mvn_program_objects/"
            ):
                continue
            names.add(name)
    for name in sorted(names):
        if not name.startswith(ALLOWED_PREFIXES):
            raise SystemExit(f"non-private path changed: {name}")
        if name.endswith(FORBIDDEN_NEW_SUFFIXES):
            raise SystemExit(f"result artifact in manifest: {name}")

    reference = root / args.reference
    if not reference.is_file():
        raise SystemExit("reference executable missing")
    data = reference.read_bytes()
    linked = run("nm", "-u", str(reference))
    forbidden = ("QuantLib", "boost", "mkl", "sobol", "inverse_normal")
    for token in forbidden:
        if token.lower() in linked.lower():
            raise SystemExit(f"forbidden reference dependency: {token}")

    source = (root / "tests/reference_autocall_two_asset_three_date_mvn_program.cpp").read_text()
    if "--holdout" in source:
        raise SystemExit("development reference unexpectedly exposes holdout evaluation")

    math_probe = root / args.f64_math_probe
    if not math_probe.is_file():
        raise SystemExit("f64 math probe missing")
    disassembly = run("objdump", "-d", "-M", "intel", str(math_probe))
    marker = "<_ZN12mvn_f64_math12probe_batch8EPKdPdS2_S2_>:"
    if marker not in disassembly:
        raise SystemExit("native f64 batch probe symbol missing")
    body = disassembly.split(marker, 1)[1].split("\n\n", 1)[0]
    forbidden_instruction = ("\tcall", "\tpush", "\tpop", "[rsp", " rsp,")
    for token in forbidden_instruction:
        if token in body:
            raise SystemExit(f"f64 batch probe structural failure: {token}")
    liveness = import_liveness(root)
    _, peak_zmm, math_peak_pc = liveness.liveness(
        liveness.instructions(math_probe, "_ZN12mvn_f64_math12probe_batch8EPKdPdS2_S2_")
    )
    if peak_zmm > 24:
        raise SystemExit(f"f64 batch probe zmm liveness ceiling: {peak_zmm}")

    dependencies = run("ldd", str(math_probe))
    for token in ("boost", "mkl", "sleef", "quantlib"):
        if token in dependencies.lower():
            raise SystemExit(f"f64 batch probe external dependency: {token}")

    phi2_probe = root / args.phi2_probe
    phi2_disassembly = run("objdump", "-d", "-M", "intel", str(phi2_probe))
    phi2_marker = "<mvn_phi2_batch8_order20_asm>:"
    if phi2_marker not in phi2_disassembly:
        raise SystemExit("selected Phi2 native symbol missing")
    phi2_body = phi2_disassembly.split(phi2_marker, 1)[1].split("\n\n", 1)[0]
    for token in forbidden_instruction:
        if token in phi2_body:
            raise SystemExit(f"Phi2 microkernel structural failure: {token}")
    if "vgather" in phi2_body or "vscatter" in phi2_body:
        raise SystemExit("Phi2 microkernel gather/scatter")
    _, phi2_peak_zmm, phi2_peak_pc = liveness.liveness(
        liveness.instructions(phi2_probe, "mvn_phi2_batch8_order20_asm")
    )
    if phi2_peak_zmm >= 28:
        raise SystemExit(f"Phi2 peak ZMM liveness gate failed: {phi2_peak_zmm}")
    symbol_rows = run("nm", "-S", str(phi2_probe)).splitlines()
    phi2_text = 0
    for row in symbol_rows:
        if row.endswith(" mvn_phi2_batch8_order20_asm"):
            phi2_text = int(row.split()[1], 16)
    if not phi2_text or phi2_text > 12 * 1024:
        raise SystemExit(f"Phi2 text gate failed: {phi2_text}")

    phi46_probe = root / args.phi46_probe
    if not phi46_probe.is_file():
        raise SystemExit("Phi4/Phi6 research probe missing")
    phi46_disassembly = run("objdump", "-d", "-M", "intel", str(phi46_probe))
    rejected = []
    phi46_text = 0
    for symbol in ("mvn_phi4_batch8_tensor40_candidate",
                   "mvn_phi6_batch8_bridge40_candidate"):
        marker = f"<{symbol}>:"
        if marker not in phi46_disassembly:
            raise SystemExit(f"native high-dimensional research symbol missing: {symbol}")
        candidate_body = phi46_disassembly.split(marker, 1)[1].split("\n\n", 1)[0]
        has_stack = bool(re.search(r"\b(?:push|pop)\b|\brsp\b|\brbp\b",
                                   candidate_body))
        has_spill = bool(re.search(r"vmov[a-z]*\s+[^\n]*\[rsp", candidate_body))
        if not has_stack or not has_spill:
            raise SystemExit(f"expected rejected research schedule evidence missing: {symbol}")
        rejected.append(f"{symbol}:stack+spill")
    for row in run("nm", "-S", str(phi46_probe)).splitlines():
        if row.endswith((" mvn_phi4_batch8_tensor40_candidate",
                         " mvn_phi6_batch8_bridge40_candidate")):
            phi46_text += int(row.split()[1], 16)

    forbidden_full_sources = (
        root / "private/autocall_two_asset_three_date_mvn_program.cpp",
        root / "benchmarks/benchmark_autocall_two_asset_three_date_mvn_program.cpp",
    )
    if any(path.exists() for path in forbidden_full_sources):
        raise SystemExit("unqualified native math entered full-program source")

    print(
        "MVN_PROGRAM_AUDIT "
        f"parent={PARENT} additive_files={len(names)} "
        f"reference_sha256={hashlib.sha256(data).hexdigest()} "
        f"f64_math_sha256={hashlib.sha256(math_probe.read_bytes()).hexdigest()} "
        f"f64_math_peak_zmm={peak_zmm} f64_math_peak_pc={math_peak_pc} "
        "f64_math_calls=0 f64_math_stack=NO "
        f"phi2_text_bytes={phi2_text} phi2_peak_zmm={phi2_peak_zmm} "
        f"phi2_peak_pc={phi2_peak_pc} "
        "phi2_calls=0 phi2_stack=NO phi2_spills=NO "
        f"phi46_research_text_bytes={phi46_text} "
        f"phi46_rejection={','.join(rejected)} "
        "near_singular_native_route=NOT_QUALIFIED holdout_evaluated=NO "
        "native_phi4_phi6_candidate=REJECTED full_program_linked=NO status=PASS"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
