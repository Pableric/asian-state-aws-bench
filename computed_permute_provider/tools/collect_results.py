#!/usr/bin/env python3
"""Merge test, audit and bench outputs with exact file hashes into one JSON."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
EXCLUDE_DIRS = {"build", "__pycache__"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path: Path):
    return json.loads(path.read_text()) if path.exists() else None


def cmd(args: list[str]) -> str:
    try:
        return subprocess.run(args, capture_output=True, text=True, cwd=HERE).stdout.strip()
    except OSError:
        return "unavailable"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--base-commit", required=True)
    ap.add_argument("--sde", default="/opt/intel-sde/sde64")
    args = ap.parse_args()
    res = HERE / "results"
    out_path = (HERE / args.out).resolve() if not args.out.is_absolute() else args.out

    files = {}
    for p in sorted(HERE.rglob("*")):
        rel = p.relative_to(HERE)
        if p.is_dir() or set(rel.parts) & EXCLUDE_DIRS or p.resolve() == out_path:
            continue
        files[str(rel)] = {"bytes": p.stat().st_size, "sha256": sha256(p)}

    upstream = {}
    for line in (HERE / "baseline_affine_frozen/UPSTREAM_SHA256SUMS").read_text().splitlines():
        digest, name = line.split()
        local = HERE / "baseline_affine_frozen" / name
        if local.exists():
            upstream[name] = {"upstream_sha256": digest, "frozen_copy_sha256": sha256(local),
                              "identical": sha256(local) == digest}
    jk = HERE.parent / "direction_numbers/joe_kuo_6_21201.bin"

    core = load(res / "correctness_core.json")
    ext = load(res / "correctness_extended.json")
    neg = load(res / "negative_controls.json")
    audit = load(res / "audit.json")
    verify = load(res / "bench_verify/verify.json")
    native_summary = load(res / "bench_native/summary.json")
    native_status = load(res / "bench_native/status.json")

    b = audit["boundaries"] if audit else {}
    acct = audit["per_unit_accounting"] if audit else {}

    def brief(name: str) -> dict:
        m = b[name]
        keys = ("instructions", "vector_instructions", "scalar_instructions", "loads",
                "load_bytes", "stores", "store_bytes", "code_bytes", "zmm_registers",
                "gpr_registers", "zmm_written", "gpr_written", "dependency_chain_instructions",
                "calls", "branches", "gathers", "stack_operations", "spills",
                "mask_register_uses", "vzeroupper", "flags_writers")
        return {k: m[k] for k in keys}

    record_bytes = 192
    report = {
        "handoff": "computed-permute-provider-handoff-v1",
        "branch": cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"]),
        "base_commit": args.base_commit,
        "host": {"cpu": next((l.split(":", 1)[1].strip() for l in
                              Path("/proc/cpuinfo").read_text().splitlines()
                              if l.startswith("model name")), "unknown"),
                 "native_avx512f": " avx512f" in Path("/proc/cpuinfo").read_text(),
                 "kernel": platform.release(),
                 "sde": cmd([args.sde, "--version"]).splitlines()[0:2] if Path(args.sde).exists()
                 else "absent"},
        "correctness": {
            "core_run": core,
            "extended_run": ext,
            "negative_controls": neg,
            "summary": {
                "core_pass": bool(core and core["pass"]),
                "extended_pass": bool(ext and ext["pass"]),
                "negative_controls_all_detected": bool(neg and neg["all_faults_detected"]),
                "bench_a_equals_b": bool(verify and verify["pass"]),
            },
        },
        "bytes": {
            "B_computed": {
                "fixed_bytes": 0,
                "bytes_per_dimension": record_bytes,
                "bytes_per_dimension_mutable_optional": 4,
                "bytes_per_block": 0,
                "bytes_per_block_x_dimension": 0,
                "note": "192-byte immutable record per dimension. The optional 4 bytes are the "
                        "caller-owned block state of each dimension when blocks are the outer "
                        "loop and dimensions the inner loop; with blocks inner it stays in a "
                        "register. Donor blocks are caller input and excluded for A and B.",
                "totals": {"D256_16_blocks": 256 * record_bytes,
                           "D1280_256_blocks": 1280 * record_bytes},
            },
            "A_affine448": {
                "fixed_bytes": 0,
                "bytes_per_dimension": 0,
                "bytes_per_block": 0,
                "bytes_per_block_x_dimension": 448,
                "totals": {"D256_16_blocks": 256 * 16 * 448,
                           "D1280_256_blocks": 1280 * 256 * 448},
            },
        },
        "instructions": {
            "source": "objdump of build/audit_instantiate.o and build/bench_kernels.o",
            "per_unit_accounting": acct,
            "boundaries": {k: brief(k) for k in b},
            "loop_bodies": {k: {kk: v[kk] for kk in ("instructions", "vector_instructions",
                                                     "scalar_instructions", "loads",
                                                     "load_bytes", "stores", "store_bytes",
                                                     "code_bytes", "branches")
                                } | ({"loop_carried_chain_instructions":
                                      v["loop_carried_chain_instructions"],
                                      "kernel_code_bytes": v["kernel_code_bytes"]}
                                     if "kernel_code_bytes" in v else {})
                            for k, v in (audit["loop_bodies"].items() if audit else [])},
            "gates": audit["gates"] if audit else None,
        },
        "benchmark": {
            "verify_only_under_emulation": verify,
            "native_timing": native_summary if native_summary else native_status,
        },
        "hashes": {
            "files": files,
            "frozen_affine_baseline_vs_upstream_SHA256SUMS": upstream,
            "frozen_affine_baseline_provenance": {
                "upstream": "asian-option-engine/dim_provider_handoff (untracked directory in "
                            "the engine checkout at the base commit; not in any commit)",
                "upstream_SHA256SUMS_sha256":
                    sha256(HERE / "baseline_affine_frozen/UPSTREAM_SHA256SUMS"),
                "copied": sorted(upstream),
            },
            "base_commit_direction_numbers/joe_kuo_6_21201.bin":
                sha256(jk) if jk.exists() else "absent",
            "records_d1280.bin": sha256(res / "records_d1280.bin")
            if (res / "records_d1280.bin").exists() else "absent",
        },
    }
    out_path.write_text(json.dumps(report, indent=2) + "\n")
    s = report["correctness"]["summary"]
    print(f"wrote {out_path} core={s['core_pass']} extended={s['extended_pass']} "
          f"negative={s['negative_controls_all_detected']} bench_ab_equal={s['bench_a_equals_b']} "
          f"audit={audit['pass'] if audit else None} files={len(files)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
