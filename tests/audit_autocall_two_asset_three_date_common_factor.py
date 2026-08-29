#!/usr/bin/env python3
"""Additive and dependency audit for the common-factor staged diagnostic."""

import argparse
import hashlib
import subprocess
from pathlib import Path

PARENT = "f9598a016a5c5cf8a12cced46d657f07a5c8646c"
DONOR = "2cc8e08263f79a3db540aba90f5c912808f0e9f7"
NEW_FILES = {
    "tests/Makefile.autocall_two_asset_three_date_common_factor",
    "tests/audit_autocall_two_asset_three_date_common_factor.py",
    "tests/reference_autocall_two_asset_three_date_common_factor.cpp",
}


def output(*args):
    return subprocess.check_output(args, text=True)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--portable", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    binary = (root / args.portable).resolve()
    if not binary.is_file():
        raise RuntimeError("portable executable missing")

    tracked = set(filter(None, output(
        "git", "diff", "--name-only", PARENT, "--").splitlines()))
    untracked = set(filter(None, output(
        "git", "ls-files", "--others", "--exclude-standard").splitlines()))
    untracked = {name for name in untracked
                 if not name.startswith(
                     ".autocall_two_asset_three_date_common_factor_objects/")
                 and name != binary.name}
    changed = tracked | untracked
    if changed != NEW_FILES:
        raise RuntimeError(f"additive manifest mismatch: {sorted(changed)}")

    for line in output("git", "diff", "--name-status", PARENT, "--").splitlines():
        if line and not line.startswith("A\t"):
            raise RuntimeError(f"parent blob changed: {line}")

    source = (root /
        "tests/reference_autocall_two_asset_three_date_common_factor.cpp"
    ).read_text(encoding="utf-8").lower()
    required = (
        "0x54574f4153534554", "0x3257484f4c444f55", "low32", "high32",
        "order_map", "0.95*r0_median", "global[count]",
        "path_counts[count]", "direct_integral", "conditional_value",
    )
    for token in required:
        if token not in source:
            raise RuntimeError(f"missing frozen protocol token: {token}")
    for token in ("boost/", "boost::", "quantlib", "rdtsc", "rdtscp",
                  "clock_monotonic", "asian_meta_affine_plan_create"):
        if token in source:
            raise RuntimeError(f"forbidden portable token: {token}")

    dependencies = output("ldd", str(binary)).lower()
    allowed = ("linux-vdso", "libstdc++", "libm.so", "libgcc_s",
               "libc.so", "ld-linux", "statically linked")
    for line in dependencies.splitlines():
        if line.strip() and not any(token in line for token in allowed):
            raise RuntimeError(f"unexpected dependency: {line}")

    donor_math = output("git", "show", f"{DONOR}:private/"
                        "autocall_single_asset_three_date_d1_native_math_avx512.S")
    donor_leaf = output("git", "show", f"{DONOR}:private/"
                        "autocall_single_asset_three_date_d1_native_avx512.S")
    print(
        "two_asset_common_factor_audit PASS additive_only=YES "
        "parent_blobs_unchanged=YES portable_only=YES native_leaf=DEFERRED "
        "boost=NO quantlib=NO external_runtime=NO timing=NO "
        "qualification_manifest=VERIFIED_PARENT_LOW32 "
        "holdout_manifest=FROZEN_HIGH32 "
        f"portable_sha256={sha256(binary)} "
        f"donor_math_blob_sha256={hashlib.sha256(donor_math.encode()).hexdigest()} "
        f"donor_leaf_blob_sha256={hashlib.sha256(donor_leaf.encode()).hexdigest()}"
    )


if __name__ == "__main__":
    main()
