#!/usr/bin/env python3
"""Generate exact AVX-512 direct-load and resident-template benchmark kernels.

The generated assembly is deliberately standalone.  It consumes prepared block
bases and the genuine D1--D256 16-lane templates; it never calls or edits either
pricing engine.  Natural-order hybrid schedules use an in-place ternary XOR
when a resident pair covers the next template delta and fall back to the exact
template load otherwise.
"""

from __future__ import annotations

import argparse
import itertools
import json
from collections import Counter, defaultdict
from functools import lru_cache
from pathlib import Path

from audit_zmm_resident_reuse import DIFFERENCE_COVER_14
from audit_zmm_sobol_blocks import DEFAULT_TABLE, ROOT, load_direction_table, sha256_file, template_audit


NATIVE = ROOT / "testing" / "pablito_sequence" / "native"
ASM_OUT = NATIVE / "zmm_resident_templates_generated.S"
HEADER_OUT = NATIVE / "zmm_resident_templates_generated.h"
SCHEDULE_OUT = NATIVE / "zmm_resident_templates_schedules.json"
DIMENSION_COUNTS = (16, 32, 64, 128, 256)
RESIDENT_BUDGETS = (4, 8, 10, 12, 14)


def mask_data() -> tuple[list[int], dict[int, int]]:
    table = load_direction_table(DEFAULT_TABLE)
    raw = template_audit(table, 16)
    masks = [int(value, 16) for value in raw["gf2"]["representation_masks_hex_by_dimension"]]
    representatives: dict[int, int] = {}
    for dimension, mask in enumerate(masks):
        representatives.setdefault(mask, dimension)
    return masks, representatives


def covered_weight(resident: tuple[int, ...], delta_weights: Counter[int]) -> int:
    pair_deltas = {left ^ right for left, right in itertools.combinations(resident, 2)}
    return delta_weights[0] + sum(delta_weights[delta] for delta in pair_deltas)


def optimize_residents(masks: list[int], budget: int) -> tuple[int, ...]:
    return _optimize_residents(tuple(masks), budget)


@lru_cache(maxsize=None)
def _optimize_residents(masks: tuple[int, ...], budget: int) -> tuple[int, ...]:
    if budget == 14:
        return tuple(DIFFERENCE_COVER_14)
    classes = tuple(sorted(set(masks)))
    deltas = [left ^ right for left, right in zip(masks, masks[1:])]
    # Transitions early in natural order occur in more requested prefixes.
    weights = [sum(index < count - 1 for count in DIMENSION_COUNTS) for index in range(len(deltas))]
    delta_weights: Counter[int] = Counter()
    for delta, weight in zip(deltas, weights):
        delta_weights[delta] += weight
    best: tuple[int, ...] = ()
    best_score = -1
    for initial in itertools.combinations(classes, 2):
        chosen = list(initial)
        while len(chosen) < budget:
            candidate = max(
                (value for value in classes if value not in chosen),
                key=lambda value: (covered_weight(tuple(chosen + [value]), delta_weights), -value),
            )
            chosen.append(candidate)
        candidate_tuple = tuple(sorted(chosen))
        score = covered_weight(candidate_tuple, delta_weights)
        if (score, tuple(-value for value in candidate_tuple)) > (
            best_score,
            tuple(-value for value in best),
        ):
            best, best_score = candidate_tuple, score
    # Deterministic single-swap hill climb.
    improved = True
    while improved:
        improved = False
        for old in best:
            for new in classes:
                if new in best:
                    continue
                candidate = tuple(sorted((set(best) - {old}) | {new}))
                score = covered_weight(candidate, delta_weights)
                if score > best_score or (score == best_score and candidate < best):
                    best, best_score, improved = candidate, score, True
                    break
            if improved:
                break
    return best


def recipes(resident: tuple[int, ...]) -> dict[int, tuple[int, int]]:
    result: dict[int, tuple[int, int]] = {}
    for left, right in itertools.combinations(resident, 2):
        result.setdefault(left ^ right, (left, right))
    return result


def emit_setup(lines: list[str], resident: tuple[int, ...], representatives: dict[int, int]) -> None:
    for register, mask in enumerate(resident):
        lines.append(f"        vmovdqu32 {representatives[mask] * 64}(%rdx), %zmm{register}")


def emit_current(
    lines: list[str], dimension: int, masks: list[int], resident: tuple[int, ...], pair: dict[int, tuple[int, int]],
) -> str:
    if dimension == 0:
        target = masks[0]
        if target in resident:
            lines.append(f"        vmovdqa32 %zmm{resident.index(target)}, %zmm14")
            return "resident_move"
        lines.append("        vmovdqu32 0(%rdx), %zmm14")
        return "direct_load"
    delta = masks[dimension - 1] ^ masks[dimension]
    if delta == 0:
        return "unchanged"
    if delta in pair:
        left, right = pair[delta]
        lines.append(
            f"        vpternlogd $0x96, %zmm{resident.index(left)}, %zmm{resident.index(right)}, %zmm14"
        )
        return "vpternlogd"
    lines.append(f"        vmovdqu32 {dimension * 64}(%rdx), %zmm14")
    return "direct_load"


def emit_dimension_body(
    lines: list[str], *, dimension: int, lanes: int, candidate: str, layout: str,
    masks: list[int], resident: tuple[int, ...], pair: dict[int, tuple[int, int]], label: str,
) -> str | None:
    transition: str | None = None
    if candidate.startswith("resident"):
        transition = emit_current(lines, dimension, masks, resident, pair)
    elif candidate == "direct16":
        lines.append(f"        vmovdqu32 {dimension * 64}(%rdx), %zmm14")
    elif candidate == "direct32_two_loads":
        lines.append(f"        vmovdqu32 {dimension * 128}(%rdx), %zmm14")
        lines.append(f"        vmovdqu32 {dimension * 128 + 64}(%rdx), %zmm17")
    elif candidate == "direct32_delta":
        lines.append(f"        vmovdqu32 {dimension * 64}(%rdx), %zmm14")
    else:
        raise ValueError(candidate)

    if lanes == 32 and candidate != "direct32_two_loads":
        lines.append(f"        vpbroadcastd {dimension * 4}(%rcx), %zmm17")

    if layout == "block_outer":
        lines.append(f"        vpbroadcastd {dimension * 4}(%rsi), %zmm15")
        lines.append("        vpxord %zmm14, %zmm15, %zmm16")
        lines.append("        vmovdqu32 %zmm16, (%rdi)")
        if lanes == 32:
            if candidate == "direct32_two_loads":
                lines.append("        vpxord %zmm17, %zmm15, %zmm16")
            else:
                lines.append("        vpxord %zmm17, %zmm16, %zmm16")
            lines.append("        vmovdqu32 %zmm16, 64(%rdi)")
        lines.append(f"        addq ${lanes * 4}, %rdi")
    else:
        lines.append("        xorl %eax, %eax")
        lines.append(f".{label}_block_d{dimension}:")
        lines.append("        vpbroadcastd (%r10,%rax,4), %zmm15")
        lines.append("        vpxord %zmm14, %zmm15, %zmm16")
        lines.append("        vmovdqu32 %zmm16, (%rdi)")
        if lanes == 32:
            if candidate == "direct32_two_loads":
                lines.append("        vpxord %zmm17, %zmm15, %zmm16")
            else:
                lines.append("        vpxord %zmm17, %zmm16, %zmm16")
            lines.append("        vmovdqu32 %zmm16, 64(%rdi)")
        lines.append(f"        addq ${lanes * 4}, %rdi")
        lines.append("        incq %rax")
        lines.append("        cmpq %r8, %rax")
        lines.append(f"        jne .{label}_block_d{dimension}")
        lines.append("        leaq (%r10,%r8,4), %r10")
    return transition


def emit_function(
    *, name: str, dims: int, lanes: int, candidate: str, layout: str,
    masks: list[int], resident: tuple[int, ...], representatives: dict[int, int],
) -> tuple[str, Counter[str]]:
    lines = [
        "        .p2align 6",
        f"        .globl {name}",
        f"        .type {name},@function",
        f"{name}:",
        "        testl %r8d, %r8d",
        f"        jz .{name}_done",
    ]
    counts: Counter[str] = Counter()
    pair = recipes(resident)
    if candidate.startswith("resident"):
        emit_setup(lines, resident, representatives)
        counts["resident_initial_loads_64B"] += len(resident)
    if layout == "dimension_outer":
        lines.append("        movq %rsi, %r10")
        for dimension in range(dims):
            transition = emit_dimension_body(
                lines, dimension=dimension, lanes=lanes, candidate=candidate, layout=layout,
                masks=masks, resident=resident, pair=pair, label=name,
            )
            if transition:
                counts[transition] += 1
    else:
        lines += ["        xorl %r9d, %r9d", f".{name}_outer:"]
        for dimension in range(dims):
            transition = emit_dimension_body(
                lines, dimension=dimension, lanes=lanes, candidate=candidate, layout=layout,
                masks=masks, resident=resident, pair=pair, label=name,
            )
            if transition:
                counts[transition] += 1
        lines += [
            f"        addq ${dims * 4}, %rsi",
            "        incq %r9",
            "        cmpq %r8, %r9",
            f"        jne .{name}_outer",
        ]
    lines += [f".{name}_done:", "        vzeroupper", "        ret", f"        .size {name},.-{name}", ""]
    return "\n".join(lines), counts


def generate() -> tuple[str, str, dict[str, object]]:
    masks, representatives = mask_data()
    resident_sets = {budget: optimize_residents(masks, budget) for budget in RESIDENT_BUDGETS}
    asm = [
        "/* Generated by generate_zmm_resident_native_benchmark.py. */",
        "        .text",
        "",
    ]
    header = [
        "/* Generated by generate_zmm_resident_native_benchmark.py. */",
        "#ifndef ZMM_RESIDENT_TEMPLATES_GENERATED_H",
        "#define ZMM_RESIDENT_TEMPLATES_GENERATED_H",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "typedef void (*zrt_kernel_fn)(uint32_t *, const uint32_t *, const uint32_t *, const uint32_t *, uint32_t);",
        "typedef struct { const char *symbol; const char *candidate; const char *layout; uint32_t lanes, dimensions, resident_budget, template_words; zrt_kernel_fn fn; } zrt_kernel_desc_t;",
    ]
    descriptors: list[str] = []
    schedule_rows: list[dict[str, object]] = []
    for layout in ("dimension_outer", "block_outer"):
        for dims in DIMENSION_COUNTS:
            variants: list[tuple[int, str, tuple[int, ...]]] = [
                (16, "direct16", ()),
                (32, "direct32_two_loads", ()),
                (32, "direct32_delta", ()),
            ]
            for lanes in (16, 32):
                for budget in RESIDENT_BUDGETS:
                    variants.append((lanes, f"resident{budget}", resident_sets[budget]))
            for lanes, candidate, resident in variants:
                if candidate == "direct16" and lanes != 16:
                    continue
                safe_layout = "do" if layout == "dimension_outer" else "bo"
                name = f"zrt_{safe_layout}_{candidate}_{lanes}_d{dims}"
                body, counts = emit_function(
                    name=name, dims=dims, lanes=lanes, candidate=candidate, layout=layout,
                    masks=masks, resident=resident, representatives=representatives,
                )
                asm.append(body)
                header.append(
                    f"void {name}(uint32_t *, const uint32_t *, const uint32_t *, const uint32_t *, uint32_t);"
                )
                budget = len(resident)
                template_words = 32 if candidate == "direct32_two_loads" else 16
                descriptors.append(
                    f'    {{"{name}", "{candidate}", "{layout}", {lanes}, {dims}, {budget}, {template_words}, {name}}}'
                )
                schedule_rows.append({
                    "symbol": name,
                    "candidate": candidate,
                    "layout": layout,
                    "lanes": lanes,
                    "dimensions": dims,
                    "resident_budget": budget,
                    "resident_masks_hex": [f"0x{value:02x}" for value in resident],
                    "resident_representative_dimensions": [representatives[value] + 1 for value in resident],
                    "static_counts_per_invocation_or_block": dict(counts),
                })
    header += [
        "static const zrt_kernel_desc_t ZRT_KERNELS[] = {",
        ",\n".join(descriptors),
        "};",
        "static const size_t ZRT_KERNEL_COUNT = sizeof(ZRT_KERNELS) / sizeof(ZRT_KERNELS[0]);",
        "#endif",
        "",
    ]
    schedule = {
        "schema_version": 1,
        "direction_table": {
            "logical_path": "direction_numbers/joe_kuo_6_21201.bin",
            "sha256": sha256_file(DEFAULT_TABLE),
        },
        "dimension_counts": list(DIMENSION_COUNTS),
        "resident_budgets": list(RESIDENT_BUDGETS),
        "resident_sets": {
            str(budget): {
                "masks_hex": [f"0x{value:02x}" for value in resident_sets[budget]],
                "representative_dimensions": [representatives[value] + 1 for value in resident_sets[budget]],
            }
            for budget in RESIDENT_BUDGETS
        },
        "kernels": schedule_rows,
    }
    return "\n".join(asm), "\n".join(header), schedule


def update(path: Path, content: str, check: bool) -> bool:
    old = path.read_text(encoding="utf-8") if path.exists() else None
    if old == content:
        return True
    if check:
        print(f"stale generated file: {path}")
        return False
    path.write_text(content, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    asm, header, schedule = generate()
    schedule_text = json.dumps(schedule, indent=2, sort_keys=True) + "\n"
    passed = all((
        update(ASM_OUT, asm, args.check),
        update(HEADER_OUT, header, args.check),
        update(SCHEDULE_OUT, schedule_text, args.check),
    ))
    return 0 if passed else 2


if __name__ == "__main__":
    raise SystemExit(main())
