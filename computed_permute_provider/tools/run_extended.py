#!/usr/bin/env python3
"""Run the D1..D1280 x blocks 0..255 scope in block shards and merge the JSON.

Each shard seeds the block state of its first block from the oracle and then
uses the asm recurrence, so every block boundary inside a shard is an asm
transition and the shard's first block is entered by an asm transition too.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shlex
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SUM_KEYS = (
    "dimension_block_pairs", "packets",
    "lanes_checked_affine_vs_oracle", "mismatch_affine_vs_oracle", "affine_prepare_failures",
    "lanes_checked_computed_vs_oracle", "mismatch_computed_vs_oracle",
    "oracle_block_inverse_failures",
    "first_packet_lanes", "first_packet_mismatch", "last_packet_lanes", "last_packet_mismatch",
    "donor_selection_failures", "duplicate_or_omitted_blocks",
    "block_state_checks", "block_state_failures", "asm_block_transitions",
    "repeat_runs", "repeat_mismatch",
)


def run_shard(binary: str, run: str, dims: int, b0: int, b1: int) -> dict:
    with tempfile.NamedTemporaryFile(suffix=".json") as tmp:
        cmd = shlex.split(run) + [binary, "--scopes", "extended", "--dims", str(dims),
                                  "--block-begin", str(b0), "--block-end", str(b1),
                                  "--json", tmp.name]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        report = json.loads(Path(tmp.name).read_text())
    report["exit_code"] = proc.returncode
    report["block_begin"] = b0
    report["block_end"] = b1
    return report


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--run", default="")
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--dims", type=int, default=1280)
    ap.add_argument("--blocks", type=int, default=256)
    ap.add_argument("--shard-blocks", type=int, default=16)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    shards = [(b, min(b + args.shard_blocks, args.blocks))
              for b in range(0, args.blocks, args.shard_blocks)]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        reports = list(pool.map(lambda s: run_shard(args.binary, args.run, args.dims, *s), shards))

    merged = {k: 0 for k in SUM_KEYS}
    shard_rows = []
    for rep in reports:
        sc = rep["scopes"][0]
        for k in SUM_KEYS:
            merged[k] += sc[k]
        shard_rows.append({"blocks": f"{rep['block_begin']}..{rep['block_end'] - 1}",
                           "pass": rep["pass"], "exit_code": rep["exit_code"],
                           "computed_outputs_sha256": sc["computed_outputs_sha256"],
                           "affine_contexts_sha256": sc["affine_contexts_sha256"],
                           "seconds": sc["seconds"]})
    chain = hashlib.sha256("".join(r["computed_outputs_sha256"] for r in shard_rows).encode())
    ok = all(r["pass"] and r["exit_code"] == 0 for r in shard_rows)
    expected_pairs = args.dims * args.blocks
    ok &= merged["dimension_block_pairs"] == expected_pairs
    ok &= merged["lanes_checked_computed_vs_oracle"] == expected_pairs * 4096
    ok &= merged["lanes_checked_affine_vs_oracle"] == expected_pairs * 4096
    for k in SUM_KEYS:
        if "mismatch" in k or "failures" in k:
            ok &= merged[k] == 0
    out = {
        "pass": ok,
        "scope": "extended",
        "dimensions": f"D1..D{args.dims}",
        "blocks": f"0..{args.blocks - 1} (points 0..{args.blocks * 4096 - 1})",
        "records_sha256": reports[0]["records"]["sha256"],
        "totals": merged,
        "shards": shard_rows,
        "shard_output_digest_chain_sha256": chain.hexdigest(),
    }
    args.out.write_text(json.dumps(out, indent=2) + "\n")
    print(f"extended pass={ok} pairs={merged['dimension_block_pairs']} "
          f"mismatch_affine={merged['mismatch_affine_vs_oracle']} "
          f"mismatch_computed={merged['mismatch_computed_vs_oracle']}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
