#!/usr/bin/env python3
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

BASE = "538840542de2380aa0423684aa89da5ff0d748d8"
BINARY = Path("bench_asian_genuine_multistrike_full_risk_hybrid_dispatch")
FROZEN = (
    "asian_genuine_multistrike_full_risk_avx512.s",
    "asian_genuine_multistrike_full_risk_setup.c",
    "private/asian_genuine_multistrike_full_risk_diag.h",
    "asian_genuine_aad_phase1_avx512.s",
)
LEAVES = {
    "asian_genuine_msfr_arithmetic_tile2_diag": 0x700,
    "asian_genuine_msfr_arithmetic_tile4_diag": 0xD18,
    "asian_genuine_msfr_cv_tile2_diag": 0x9F2,
    "asian_genuine_msfr_cv_tile4_diag": 0x1262,
}


def run(*args: str) -> bytes:
    return subprocess.check_output(args)


def disassemble(symbol: str) -> str:
    return run("objdump", "-dr", f"--disassemble={symbol}",
               str(BINARY)).decode()


def main() -> int:
    if not BINARY.is_file():
        print(f"missing linked benchmark: {BINARY}", file=sys.stderr)
        return 2
    subprocess.check_call(("git", "diff", "--quiet", BASE, "--", *FROZEN))
    frozen_hashes = {}
    for path in FROZEN:
        current = Path(path).read_bytes()
        qualified = run("git", "show", f"{BASE}:{path}")
        if current != qualified:
            raise RuntimeError(f"frozen file changed: {path}")
        frozen_hashes[path] = hashlib.sha256(current).hexdigest()

    sizes = {}
    for line in run("nm", "-S", "--size-sort", str(BINARY)).decode().splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[3] in LEAVES:
            sizes[fields[3]] = int(fields[1], 16)
    if sizes != LEAVES:
        raise RuntimeError(f"ranked symbol size mismatch: {sizes!r}")
    for symbol in LEAVES:
        body = disassemble(symbol)
        if re.search(r"\bcallq?\b", body):
            raise RuntimeError(f"call found in frozen ranked leaf: {symbol}")
        if "hybrid_consume_block" in body or "hybrid_plan" in body:
            raise RuntimeError(f"dispatcher reference found in ranked leaf: {symbol}")

    dispatcher = disassemble("asian_genuine_msfr_hybrid_consume_block_diag")
    for target in (*LEAVES,
                   "asian_genuine_aad_phase1_forward_arithmetic_call_diag",
                   "asian_genuine_aad_phase1_forward_arithmetic_put_diag",
                   "asian_genuine_aad_phase1_forward_cv_call_diag",
                   "asian_genuine_aad_phase1_forward_cv_put_diag"):
        if target not in dispatcher:
            raise RuntimeError(f"dispatcher target missing: {target}")

    print(json.dumps({
        "status": "PASS",
        "qualified_commit": BASE,
        "frozen_hashes": frozen_hashes,
        "ranked_symbol_sizes": sizes,
        "ranked_leaf_calls": 0,
        "dispatcher_outside_ranked_leaves": True,
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
