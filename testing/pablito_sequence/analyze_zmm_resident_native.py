#!/usr/bin/env python3
"""Reduce the native resident-template benchmark and audit its machine code."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
_TABLE_CANDIDATES = (
    ROOT / "direction_numbers" / "joe_kuo_6_21201.bin",
    ROOT / "asian-aws-publish.lxGUbF" / "direction_numbers" / "joe_kuo_6_21201.bin",
)
DEFAULT_TABLE = next((path for path in _TABLE_CANDIDATES if path.exists()), _TABLE_CANDIDATES[0])
DEFAULT_SCHEDULES = ROOT / "testing/pablito_sequence/native/zmm_resident_templates_schedules.json"
DEFAULT_OUT = ROOT / "testing/pablito_sequence/results/zmm_resident_native_benchmark_20260821"
WIN_THRESHOLD = 1.02


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def object_audit(binary: Path, symbols: list[str]) -> dict[str, Any]:
    disassembly = subprocess.run(
        ["objdump", "-d", "-Mintel", str(binary)], check=True, text=True, capture_output=True
    ).stdout
    sizes_text = subprocess.run(
        ["nm", "-S", "--defined-only", str(binary)], check=True, text=True, capture_output=True
    ).stdout
    sizes: dict[str, int] = {}
    for line in sizes_text.splitlines():
        fields = line.split()
        if len(fields) >= 4:
            try:
                sizes[fields[3]] = int(fields[1], 16)
            except ValueError:
                pass
    symbol_set = set(symbols)
    bodies: dict[str, list[str]] = defaultdict(list)
    current: str | None = None
    header = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
    instruction = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*([a-z0-9]+)\s*(.*)$")
    for line in disassembly.splitlines():
        match = header.match(line.strip())
        if match:
            current = match.group(1) if match.group(1) in symbol_set else None
            continue
        if current is not None:
            bodies[current].append(line)
    rows = []
    for symbol in symbols:
        mnemonics: Counter[str] = Counter()
        stack_vector_references = 0
        for line in bodies.get(symbol, []):
            match = instruction.match(line)
            if not match:
                continue
            mnemonic, operands = match.groups()
            mnemonics[mnemonic] += 1
            if ("zmm" in operands or "ymm" in operands) and ("rsp" in operands or "rbp" in operands):
                stack_vector_references += 1
        rows.append({
            "symbol": symbol,
            "text_bytes": sizes.get(symbol),
            "mnemonics": dict(sorted(mnemonics.items())),
            "stack_vector_references": stack_vector_references,
            "no_vector_spills_or_reloads": stack_vector_references == 0,
        })
    return {
        "binary": str(binary),
        "symbols": rows,
        "all_symbols_found": all(row["text_bytes"] is not None for row in rows),
        "all_symbols_no_vector_spills_or_reloads": all(row["no_vector_spills_or_reloads"] for row in rows),
    }


def key(row: dict[str, Any]) -> tuple[str, int, int, int]:
    return row["layout"], row["lanes"], row["dimensions"], row["blocks"]


def analyze(native: dict[str, Any], schedules: dict[str, Any], binary: Path) -> dict[str, Any]:
    results = native["results"]
    grouped: dict[tuple[str, int, int, int], list[dict[str, Any]]] = defaultdict(list)
    for row in results:
        grouped[key(row)].append(row)
    comparisons = []
    for group_key, rows in sorted(grouped.items()):
        direct = [row for row in rows if row["candidate"].startswith("direct")]
        best_direct = min(direct, key=lambda row: row["ticks_median_per_invocation"])
        for row in rows:
            if not row["candidate"].startswith("resident"):
                continue
            speedup = best_direct["ticks_median_per_invocation"] / row["ticks_median_per_invocation"]
            robust_p90 = row["ticks_p90_per_invocation"] <= best_direct["ticks_p90_per_invocation"]
            comparisons.append({
                "layout": group_key[0], "lanes": group_key[1], "dimensions": group_key[2],
                "blocks": group_key[3], "candidate": row["candidate"],
                "resident_budget": row["resident_budget"], "best_direct": best_direct["candidate"],
                "median_speedup_vs_best_direct": speedup,
                "resident_ticks_median": row["ticks_median_per_invocation"],
                "direct_ticks_median": best_direct["ticks_median_per_invocation"],
                "resident_p90_not_worse": robust_p90,
                "qualified_measured_win": speedup >= WIN_THRESHOLD and robust_p90,
            })
    by_candidate: dict[tuple[str, int, int, str], list[dict[str, Any]]] = defaultdict(list)
    for row in comparisons:
        by_candidate[(row["layout"], row["lanes"], row["dimensions"], row["candidate"])].append(row)
    break_even = []
    for group_key, rows in sorted(by_candidate.items()):
        rows.sort(key=lambda row: row["blocks"])
        winning = [row["blocks"] for row in rows if row["qualified_measured_win"]]
        break_even.append({
            "layout": group_key[0], "lanes": group_key[1], "dimensions": group_key[2],
            "candidate": group_key[3], "first_qualified_winning_block_count": min(winning) if winning else None,
            "wins": len(winning), "tested_block_counts": [row["blocks"] for row in rows],
            "speedups": [row["median_speedup_vs_best_direct"] for row in rows],
        })
    symbols = [row["symbol"] for row in schedules["kernels"]]
    machine = object_audit(binary, symbols)
    size_by_symbol = {row["symbol"]: row["text_bytes"] for row in machine["symbols"]}
    for row in comparisons:
        schedule_symbol = next(
            item["symbol"] for item in schedules["kernels"]
            if item["candidate"] == row["candidate"] and item["layout"] == row["layout"]
            and item["lanes"] == row["lanes"] and item["dimensions"] == row["dimensions"]
        )
        row["resident_symbol_text_bytes"] = size_by_symbol[schedule_symbol]
    qualified = [row for row in comparisons if row["qualified_measured_win"]]
    best = sorted(qualified, key=lambda row: row["median_speedup_vs_best_direct"], reverse=True)[:20]
    return {
        "schema_version": 1,
        "status": "PASS" if native.get("status") == "PASS" and machine["all_symbols_no_vector_spills_or_reloads"] else "FAIL",
        "scope": "standalone production-shaped resident-template benchmark; no pricing-engine integration",
        "direction_table": {"path": str(DEFAULT_TABLE.relative_to(ROOT)), "sha256": sha256_file(DEFAULT_TABLE)},
        "native_environment": {key: native.get(key) for key in ("cpu", "cpu_model", "production_start", "samples", "warmups")},
        "win_gate": {
            "minimum_median_speedup": WIN_THRESHOLD,
            "p90_requirement": "resident p90 ticks must not exceed the best direct candidate p90",
            "correctness": "native exact check must pass and object audit must find no vector spills/reloads",
        },
        "comparisons": comparisons,
        "break_even": break_even,
        "top_qualified_wins": best,
        "qualified_win_count": len(qualified),
        "comparison_count": len(comparisons),
        "object_audit": machine,
        "integration_or_engine_change_performed": False,
    }


def markdown(report: dict[str, Any]) -> str:
    environment = report["native_environment"]
    best = report["top_qualified_wins"][:10]
    rows = "\n".join(
        f"| {row['layout']} | {row['lanes']} | {row['dimensions']} | {row['blocks']} | "
        f"{row['candidate']} | {row['median_speedup_vs_best_direct']:.3f}x |"
        for row in best
    ) or "| — | — | — | — | No qualified win | — |"
    return f"""# Native ZMM resident-template benchmark

Status: **{report['status']}**. No pricing engine was modified.

CPU: `{environment['cpu_model']}`. Production start: {environment['production_start']}.
Timing used {environment['samples']} randomized-order samples after {environment['warmups']} warmups.

The pre-registered win gate requires at least {report['win_gate']['minimum_median_speedup']:.2f}x median
speedup over the fastest direct construction, a non-worse p90, exact output,
and no vector spills/reloads.

Qualified resident wins: **{report['qualified_win_count']} / {report['comparison_count']}** comparisons.

| Layout | Lanes | Dimensions | Blocks | Candidate | Median speedup |
|---|---:|---:|---:|---|---:|
{rows}

The full per-case timings, break-even table, symbol sizes and instruction audit
are in `audit.json`. This is a standalone experiment, not an integration claim.
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-json", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--schedules", type=Path, default=DEFAULT_SCHEDULES)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    if args.out_dir.exists() and any(args.out_dir.iterdir()):
        raise SystemExit(f"refusing to overwrite non-empty result directory: {args.out_dir}")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    report = analyze(
        json.loads(args.native_json.read_text(encoding="utf-8")),
        json.loads(args.schedules.read_text(encoding="utf-8")),
        args.binary,
    )
    (args.out_dir / "audit.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (args.out_dir / "README.md").write_text(markdown(report), encoding="utf-8")
    print(json.dumps({"status": report["status"], "qualified_wins": report["qualified_win_count"]}, sort_keys=True))
    return 0 if report["status"] == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
