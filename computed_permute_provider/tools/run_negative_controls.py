#!/usr/bin/env python3
"""Run each injected fault on the contract + canonical scopes; every one must FAIL."""

from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

FAULTS = (
    ("record_daddr", "D2 daddr[3] ^= 64 (packet address step, t = 3)"),
    ("record_dlane", "D3 dlane[0] ^= 1 (packet lane step, t = 0)"),
    ("record_half", "D5 half ^= 2 (second-half lane XOR)"),
    ("record_ctl", "D8 ctl[9] ^= 4 (base lane control)"),
    ("record_cstep", "D11 cstep[0] ^= 0xF (block step, odd blocks)"),
    ("donor_block", "harness hands donor block B ^ 1 instead of B"),
    ("skip_advance", "harness skips CPP_BLOCK_ADVANCE at block boundaries"),
)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--run", default="")
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    results = []
    all_detected = True
    for name, what in FAULTS:
        with tempfile.NamedTemporaryFile(suffix=".json") as tmp:
            cmd = shlex.split(args.run) + [args.binary, "--scopes", "contract,canonical",
                                           "--fault", name, "--json", tmp.name]
            proc = subprocess.run(cmd, capture_output=True, text=True)
            report = json.loads(Path(tmp.name).read_text())
        canon = report["scopes"][0]
        nonzero = {k: v for k, v in canon.items()
                   if isinstance(v, int) and v and ("mismatch" in k or "failures" in k)}
        detected = proc.returncode == 1 and report["pass"] is False
        all_detected &= detected
        results.append({"fault": name, "description": what, "exit_code": proc.returncode,
                        "detected": detected, "nonzero_failure_counters": nonzero})
        print(f"{name}: exit={proc.returncode} detected={detected} {sorted(nonzero)}")
    args.out.write_text(json.dumps({"all_faults_detected": all_detected, "faults": results},
                                   indent=2) + "\n")
    return 0 if all_detected else 1


if __name__ == "__main__":
    sys.exit(main())
