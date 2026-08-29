#!/usr/bin/env python3
"""Additive/provenance audit for the portable consecutive-block screen."""

import argparse
import hashlib
import subprocess
from pathlib import Path

PARENT = "12495cc33c5d5cdd55124052c698fd5ce72767dc"
NEW_FILES = {
    "tests/Makefile.autocall_two_asset_three_date_worstof_blocks",
    "tests/audit_autocall_two_asset_three_date_worstof_blocks.py",
    "tests/reference_autocall_two_asset_three_date_worstof_blocks.cpp",
}
ARTIFACTS = {
    "reference_autocall_two_asset_three_date_worstof_blocks",
}
FORBIDDEN_SOURCE = (
    "boost/", "boost::", "quantlib", "rdtsc", "rdtscp",
    "clock_monotonic", "std::chrono", "benchmark-native",
    "autocall_worstof_prepared_leaf(", "autocall_worstof_inline_leaf(",
)
FORBIDDEN_SYMBOLS = (
    "autocall_worstof_prepared_leaf",
    "autocall_worstof_inline_leaf",
    "autocall_worstof_prepare_asset_b_growth",
    "asian_meta_affine_plan_create",
    "asian_meta_qsort_control_plan_create",
)


def output(*args):
    return subprocess.check_output(args, text=True)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    binary = (root / args.binary).resolve()
    if not binary.is_file():
        raise RuntimeError("portable reference executable missing")

    tracked = set(filter(None, output(
        "git", "diff", "--name-only", PARENT, "--").splitlines()))
    untracked = set(filter(None, output(
        "git", "ls-files", "--others", "--exclude-standard").splitlines()))
    untracked = {name for name in untracked
                 if not name.startswith(
                     ".autocall_two_asset_three_date_worstof_blocks_objects/")
                 and name not in ARTIFACTS}
    changed = tracked | untracked
    if changed != NEW_FILES:
        raise RuntimeError(f"additive manifest mismatch: {sorted(changed)}")

    status = output("git", "diff", "--name-status", PARENT, "--")
    for line in status.splitlines():
        if line and not line.startswith("A\t"):
            raise RuntimeError(f"parent blob changed: {line}")

    protected = [
        "direction_numbers/joe_kuo_6_21201.bin",
        "asian_arithmetic_joe_kuo_256.s",
        "private/asian_genuine_fixed_block_signed_z.bin",
        "private/asian_variable_sobol_signed_z.bin",
        "private/autocall_two_asset_three_date_worstof_raw_avx512.S",
        "autocall_two_asset_three_date_worstof_raw.c",
    ]
    subprocess.check_call(
        ["git", "diff", "--quiet", PARENT, "--", *protected])

    scanned_sources = NEW_FILES - {
        "tests/audit_autocall_two_asset_three_date_worstof_blocks.py"}
    source_text = "\n".join((root / name).read_text(
        encoding="utf-8", errors="replace").lower()
        for name in scanned_sources)
    for token in FORBIDDEN_SOURCE:
        if token in source_text:
            raise RuntimeError(f"forbidden source token: {token}")
    if "COUNT_BLOCKS[COUNTS] = {1u, 2u, 4u}".lower() not in source_text:
        raise RuntimeError("predetermined cumulative prefix table missing")
    if "FIRST_INDEX = 8192u".lower() not in source_text:
        raise RuntimeError("absolute-index base missing")
    if "HOLDOUT_SEED = UINT64_C(0x3257484f4c444f55)".lower() not in source_text:
        raise RuntimeError("frozen unseen holdout seed missing")

    symbols = output("nm", "-a", str(binary))
    for symbol in FORBIDDEN_SYMBOLS:
        if symbol in symbols:
            raise RuntimeError(f"native/production symbol linked: {symbol}")

    dependencies = output("ldd", str(binary)).lower()
    allowed = ("linux-vdso", "libstdc++", "libm.so", "libgcc_s",
               "libc.so", "ld-linux", "statically linked")
    for line in dependencies.splitlines():
        if line.strip() and not any(token in line for token in allowed):
            raise RuntimeError(f"unexpected runtime dependency: {line}")

    direction_hash = sha256(root / "direction_numbers/joe_kuo_6_21201.bin")
    fixed_hash = sha256(root / "private/asian_genuine_fixed_block_signed_z.bin")
    variable_hash = sha256(root / "private/asian_variable_sobol_signed_z.bin")
    print(
        "two_asset_worstof_blocks_audit PASS "
        "additive_only=YES parent_blobs_unchanged=YES "
        "direction_assets_unchanged=YES native_leaves_added=0 "
        "production_symbols_added=0 boost=NO quantlib=NO "
        "external_runtime=NO timing_code=NO result_dependent_blocks=NO "
        "prefixes=4096,8192,16384 "
        f"joe_kuo_sha256={direction_hash} "
        f"fixed_signed_z_sha256={fixed_hash} "
        f"variable_signed_z_sha256={variable_hash}")


if __name__ == "__main__":
    main()
