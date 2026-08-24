#!/usr/bin/env python3
"""Audit the fixed-8192 static-schedule/vector-tail source candidate."""

from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path

from check_dynamic_mix import function_counts


ROOT = Path(__file__).resolve().parents[1]
OBJECT = ROOT / "asian_ordered_d1_source_opt_avx512.o"
CURRENT_MIX = Path("/tmp/asian-ordered-source-current.mix")
OPTIMIZED_MIX = Path("/tmp/asian-ordered-source-optimized.mix")
BASELINE_MIX = Path("/tmp/asian-ordered-source-baseline.mix")
CANDIDATE_MIX = Path("/tmp/asian-ordered-source-candidate.mix")
OUTPUT = ROOT / "results/asian_ordered_d1_source_opt_object_audit.json"
BASELINE = "ordered_d1_x_growth_local_diag"
CANDIDATE = "asian_ordered_d1_x_growth_static_8192_diag"
X_CANDIDATE = "asian_ordered_d1_x_static_8192_diag"


def count_prefix(counts: dict[str, int], prefixes: tuple[str, ...]) -> int:
    return sum(value for name, value in counts.items() if name.startswith(prefixes))


def summarize(counts: dict[str, int]) -> dict[str, object]:
    return {
        "dynamic_instructions": counts.get("*total", 0),
        "loads_64": counts.get("*mem-read-64", 0),
        "loads_4": counts.get("*mem-read-4", 0),
        "stores_64": counts.get("*mem-write-64", 0),
        "stores_4": counts.get("*mem-write-4", 0),
        "fmas": count_prefix(counts, ("VFMADD", "VFNMADD")),
        "permutes": count_prefix(counts, ("VPERM", "VSHUF")),
        "scatters": count_prefix(counts, ("VSCATTER",)),
        "branches": count_prefix(counts, ("J",)),
        "tzcnt": count_prefix(counts, ("TZCNT",)),
        "calls": count_prefix(counts, ("CALL",)),
        "gathers": count_prefix(counts, ("VGATHER", "VPGATHER")),
    }


def add_summaries(*items: dict[str, object]) -> dict[str, object]:
    keys = items[0].keys()
    return {
        key: sum(int(item[key]) for item in items)
        for key in keys
        if all(isinstance(item[key], int) for item in items)
    }


def subtract(
    candidate: dict[str, object], baseline: dict[str, object]
) -> dict[str, int]:
    return {
        key: int(candidate[key]) - int(baseline[key])
        for key in candidate
        if isinstance(candidate[key], int) and isinstance(baseline.get(key), int)
    }


def main() -> int:
    current_x = summarize(function_counts(CURRENT_MIX, "ordered_d1_x_only_diag"))
    current_exp = summarize(
        function_counts(CURRENT_MIX, "asian_vector_exp_range_reduced_array_diag")
    )
    current = add_summaries(current_x, current_exp)
    optimized_x = summarize(function_counts(OPTIMIZED_MIX, X_CANDIDATE))
    optimized_exp = summarize(
        function_counts(OPTIMIZED_MIX, "asian_vector_exp_range_reduced_array_diag")
    )
    optimized = add_summaries(optimized_x, optimized_exp)
    baseline = summarize(function_counts(BASELINE_MIX, BASELINE))
    candidate = summarize(function_counts(CANDIDATE_MIX, CANDIDATE))
    current_prepare = summarize(
        function_counts(CURRENT_MIX, "ordered_d1_diag_prepare")
    )
    optimized_prepare = summarize(
        function_counts(OPTIMIZED_MIX, "ordered_d1_diag_prepare")
    )
    candidate_prepare = summarize(
        function_counts(CANDIDATE_MIX, "ordered_d1_diag_prepare")
    )
    current_first_block = add_summaries(current_prepare, current)
    optimized_first_block = add_summaries(optimized_prepare, optimized)
    candidate_first_block = add_summaries(candidate_prepare, candidate)
    body = subprocess.check_output(
        [
            "objdump",
            "-d",
            "-Mintel",
            "--no-show-raw-insn",
            f"--disassemble={CANDIDATE}",
            str(OBJECT),
        ],
        text=True,
    ).lower()
    instructions = [
        line.split(":", 1)[1].strip()
        for line in body.splitlines()
        if re.match(r"^\s*[0-9a-f]+:\s+", line)
    ]
    sizes = subprocess.check_output(["nm", "-S", str(OBJECT)], text=True)
    size_match = re.search(
        rf"^([0-9a-f]+)\s+([0-9a-f]+)\s+[tT]\s+{CANDIDATE}$", sizes, re.M
    )
    x_body = subprocess.check_output(
        [
            "objdump",
            "-d",
            "-Mintel",
            "--no-show-raw-insn",
            f"--disassemble={X_CANDIDATE}",
            str(OBJECT),
        ],
        text=True,
    ).lower()
    x_instructions = [
        line.split(":", 1)[1].strip()
        for line in x_body.splitlines()
        if re.match(r"^\s*[0-9a-f]+:\s+", line)
    ]
    x_size_match = re.search(
        rf"^([0-9a-f]+)\s+([0-9a-f]+)\s+[tT]\s+{X_CANDIDATE}$", sizes, re.M
    )
    candidate_delta = subtract(candidate, baseline)
    gates = {
        "fewer_dynamic_instructions":
            candidate["dynamic_instructions"] < baseline["dynamic_instructions"],
        "fewer_64byte_loads": candidate["loads_64"] < baseline["loads_64"],
        "no_dynamic_tzcnt": candidate["tzcnt"] == 0,
        "eight_vector_scatters": candidate["scatters"] == 8,
        "no_calls": candidate["calls"] == 0,
        "no_gathers": candidate["gathers"] == 0,
        "no_stack_references": "rsp" not in body and "rbp" not in body,
        "no_callee_saved_clobbers": not re.search(
            r"\b(?:rbx|r12|r13|r14|r15)\b", body
        ),
        "static_tzcnt_absent": "tzcnt" not in body,
        "engine_source_fewer_dynamic_instructions":
            optimized["dynamic_instructions"] < current["dynamic_instructions"],
        "engine_first_block_fewer_dynamic_instructions":
            optimized_first_block["dynamic_instructions"]
            < current_first_block["dynamic_instructions"],
        "engine_x_four_vector_scatters": optimized_x["scatters"] == 4,
        "engine_x_no_dynamic_tzcnt": optimized_x["tzcnt"] == 0,
        "engine_x_no_calls": optimized_x["calls"] == 0,
        "engine_x_no_gathers": optimized_x["gathers"] == 0,
        "engine_x_no_stack_references": "rsp" not in x_body and "rbp" not in x_body,
        "engine_x_no_callee_saved_clobbers": not re.search(
            r"\b(?:rbx|r12|r13|r14|r15)\b", x_body
        ),
        "engine_x_static_tzcnt_absent": "tzcnt" not in x_body,
    }
    payload = {
        "status": "PASS" if all(gates.values()) else "FAIL",
        "scope": "bit-exact X-only and dual X/growth sources for D1 indices "
                 "8192..16383",
        "decision": {
            "engine_x_only_candidate": "AWAIT_NATIVE_COMPLETE_PATH_TIMING",
            "dual_x_growth_candidate": "DO_NOT_ADOPT_FOR_ONE_BLOCK_GROWTH3_SETUP",
        },
        "engine_current_source": current,
        "engine_optimized_source": optimized,
        "engine_optimized_x_leaf": optimized_x,
        "engine_optimized_minus_current_source": subtract(optimized, current),
        "engine_optimized_x_static_instructions": len(x_instructions),
        "engine_optimized_x_symbol_bytes": (
            int(x_size_match.group(2), 16) if x_size_match else None
        ),
        "candidate_minus_engine_current_source": subtract(candidate, current),
        "baseline": baseline,
        "candidate": candidate,
        "candidate_minus_baseline": candidate_delta,
        "one_block_instruction_evidence": {
            "engine_current_x3_prepare": current_prepare,
            "engine_optimized_x3_prepare": optimized_prepare,
            "candidate_x3_growth3_prepare": candidate_prepare,
            "engine_current_prepare_plus_source": current_first_block,
            "engine_optimized_prepare_plus_source": optimized_first_block,
            "engine_optimized_minus_current": subtract(
                optimized_first_block, current_first_block
            ),
            "candidate_prepare_plus_source": candidate_first_block,
            "candidate_minus_engine_current": subtract(
                candidate_first_block, current_first_block
            ),
        },
        "candidate_static_instructions": len(instructions),
        "candidate_symbol_bytes": int(size_match.group(2), 16) if size_match else None,
        "gates": gates,
        "notes": [
            "SDE is instruction evidence only; native AVX-512 timing remains required",
            "candidate uses four packed hard vectors and scatters x plus growth",
            "engine current source is X3 production plus two 4096-value exp passes",
            "engine optimized source keeps the same X3 prepare and exp passes",
        ],
    }
    OUTPUT.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    print(json.dumps(payload, indent=2, sort_keys=True))
    if payload["status"] != "PASS":
        raise SystemExit("asian_ordered_d1_source_opt_audit=FAIL")
    print("asian_ordered_d1_source_opt_audit=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
