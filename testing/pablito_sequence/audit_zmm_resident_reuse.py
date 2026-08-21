#!/usr/bin/env python3
"""Read-only 18-ZMM reuse audit for genuine Joe--Kuo D1--D256.

This follow-up asks a narrower implementation question than the global block
dictionary audit: with a fixed budget of free ZMM registers, which dimension
templates are resident, directly reusable, or constructible with one ternary
XOR?  It also checks same-lane blends and step-specific source-set permutation
reuse.  It does not edit or call either pricing engine.
"""

from __future__ import annotations

import argparse
import itertools
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Sequence

import numpy as np

from audit_zmm_sobol_blocks import (
    DEFAULT_TABLE,
    DIMENSIONS,
    ROOT,
    gray_words,
    load_direction_table,
    sha256_file,
    template_audit,
)


DEFAULT_OUT = ROOT / "testing" / "pablito_sequence" / "results" / "zmm_resident_reuse_transition_audit_20260821"
POINTS = 1 << 20
PRODUCTION_START = 8192

# Deterministic exact difference cover found over the 63 observed 7-bit
# template masks.  Pairwise XORs cover every one of the 63 nonzero even-parity
# masks, which are exactly the possible deltas between odd-parity templates.
DIFFERENCE_COVER_14 = [
    0x01, 0x04, 0x15, 0x1C, 0x26, 0x29, 0x3D,
    0x3E, 0x45, 0x51, 0x57, 0x5B, 0x61, 0x6B,
]


def triple_closure(resident: Sequence[int]) -> set[int]:
    return set(resident) | {
        left ^ middle ^ right
        for left, middle, right in itertools.combinations(resident, 3)
    }


def first_recipe(target: int, resident: Sequence[int]) -> tuple[int, ...] | None:
    if target in resident:
        return (target,)
    for recipe in itertools.combinations(resident, 3):
        if recipe[0] ^ recipe[1] ^ recipe[2] == target:
            return recipe
    return None


def resident_strategy(masks: list[int], budget: int) -> dict[str, Any]:
    groups: dict[int, list[int]] = defaultdict(list)
    for dimension, mask in enumerate(masks, 1):
        groups[mask].append(dimension)
    ranked = sorted(groups, key=lambda mask: (-len(groups[mask]), mask))
    resident = ranked[:budget]
    closure = triple_closure(resident)
    recipes = {mask: first_recipe(mask, resident) for mask in groups}
    covered = {mask for mask, recipe in recipes.items() if recipe is not None}
    direct_dimensions = sum(len(groups[mask]) for mask in resident)
    composed_dimensions = sum(len(groups[mask]) for mask in covered - set(resident))
    return {
        "resident_budget": budget,
        "resident_masks_hex": [f"0x{mask:02x}" for mask in resident],
        "resident_representative_dimensions": [groups[mask][0] for mask in resident],
        "resident_groups": [
            {"mask_hex": f"0x{mask:02x}", "representative_dimension": groups[mask][0], "dimensions": groups[mask]}
            for mask in resident
        ],
        "direct_dimensions": direct_dimensions,
        "three_source_dimensions": composed_dimensions,
        "covered_dimensions": direct_dimensions + composed_dimensions,
        "covered_exact_template_classes": len(covered),
        "all_dimensions_covered": covered == set(groups),
        "missing_masks_hex": [f"0x{mask:02x}" for mask in sorted(set(groups) - covered)],
        "per_dimension": [
            {
                "dimension": dimension,
                "target_mask_hex": f"0x{mask:02x}",
                "kind": "resident" if len(recipes[mask] or ()) == 1 else "three_source_vpternlogd" if recipes[mask] else "uncovered",
                "recipe_masks_hex": [f"0x{value:02x}" for value in (recipes[mask] or ())],
                "recipe_representative_dimensions": [groups[value][0] for value in (recipes[mask] or ())],
            }
            for dimension, mask in enumerate(masks, 1)
        ],
        "block_outer_instruction_model": {
            "resident_target": "vpxord base,resident_template,out (1 vector instruction, no template load)",
            "three_source_target": "vpxord base,A,out; vpternlogd $0x96,B,C,out (2 vector instructions, no template load)",
            "weighted_vector_instructions_per_16_lane_output": (
                direct_dimensions + 2 * composed_dimensions
            ) / DIMENSIONS if direct_dimensions + composed_dimensions == DIMENSIONS else None,
        },
        "dimension_outer_instruction_model": {
            "resident_target": "use resident template for every block",
            "three_source_setup_once": "vmovdqa32 A,current; vpternlogd $0x96,B,C,current",
            "per_block_after_setup": "vpxord base,current,out",
            "amortization": "two setup instructions once per composed dimension, then one instruction per aligned block",
        },
    }


def algebraic_eight_strategy(masks: list[int]) -> dict[str, Any]:
    resident = [1, 2, 4, 8, 16, 32, 64, 127]
    groups = Counter(masks)
    missing_residents = [mask for mask in resident if mask not in groups]
    closure = triple_closure(resident)
    return {
        "resident_masks_hex": [f"0x{mask:02x}" for mask in resident],
        "registers": len(resident),
        "missing_resident_templates": [f"0x{mask:02x}" for mask in missing_residents],
        "all_dimensions_direct_or_three_source": not missing_residents and set(groups) <= closure,
        "direct_dimensions": sum(groups[mask] for mask in resident),
        "three_source_dimensions": sum(groups[mask] for mask in set(groups) - set(resident)),
        "proof": "the seven unit masks plus 0x7f generate every odd-parity 7-bit mask with either one resident or XOR of three residents",
    }


def difference_cover_strategy(masks: list[int]) -> dict[str, Any]:
    groups: dict[int, list[int]] = defaultdict(list)
    for dimension, mask in enumerate(masks, 1):
        groups[mask].append(dimension)
    resident = DIFFERENCE_COVER_14
    pair_recipe: dict[int, tuple[int, int]] = {}
    for left, right in itertools.combinations(resident, 2):
        pair_recipe.setdefault(left ^ right, (left, right))
    required_deltas = {
        left ^ right for left in groups for right in groups if left != right
    }
    missing = sorted(required_deltas - set(pair_recipe))
    transitions = []
    instruction_count = 0
    for dimension in range(1, DIMENSIONS):
        current = masks[dimension - 1]
        following = masks[dimension]
        delta = current ^ following
        if delta == 0:
            recipe: tuple[int, ...] = ()
            kind = "same_template_no_instruction"
        else:
            recipe = pair_recipe.get(delta, ())
            kind = "in_place_vpternlogd" if recipe else "uncovered"
            instruction_count += int(bool(recipe))
        transitions.append({
            "from_dimension": dimension,
            "to_dimension": dimension + 1,
            "delta_mask_hex": f"0x{delta:02x}",
            "kind": kind,
            "resident_pair_masks_hex": [f"0x{value:02x}" for value in recipe],
            "resident_pair_representative_dimensions": [groups[value][0] for value in recipe],
        })
    first_mask = masks[0]
    first_is_resident = first_mask in resident
    return {
        "resident_template_registers": len(resident),
        "mutable_current_registers": 1,
        "total_zmm_registers": len(resident) + 1,
        "free_from_budget_18": 18 - len(resident) - 1,
        "resident_masks_hex": [f"0x{mask:02x}" for mask in resident],
        "resident_representative_dimensions": [groups[mask][0] for mask in resident],
        "resident_direct_dimensions": sum(len(groups[mask]) for mask in resident),
        "required_nonzero_template_deltas": len(required_deltas),
        "covered_nonzero_template_deltas": len(required_deltas & set(pair_recipe)),
        "missing_delta_masks_hex": [f"0x{mask:02x}" for mask in missing],
        "all_dimension_to_dimension_transitions_covered": not missing,
        "initialization": {
            "D1_mask_hex": f"0x{first_mask:02x}",
            "D1_is_resident": first_is_resident,
            "instructions": 1 if first_is_resident else 2,
            "model": "vmovdqa32 resident_D1,current" if first_is_resident else "resident triple construction",
        },
        "natural_D1_to_D256": {
            "transitions": DIMENSIONS - 1,
            "zero_instruction_same_template_transitions": sum(row["kind"] == "same_template_no_instruction" for row in transitions),
            "one_instruction_vpternlogd_transitions": sum(row["kind"] == "in_place_vpternlogd" for row in transitions),
            "uncovered_transitions": sum(row["kind"] == "uncovered" for row in transitions),
            "template_transition_instructions": instruction_count,
            "including_initialization_instructions": instruction_count + (1 if first_is_resident else 2),
            "average_template_setup_or_transition_instructions_per_dimension": (
                instruction_count + (1 if first_is_resident else 2)
            ) / DIMENSIONS,
        },
        "instruction": "vpternlogd $0x96,resident_A,resident_B,current",
        "proof": "all templates have odd GF(2) parity; every nonzero even delta is a pairwise XOR of the 14 resident masks",
        "per_transition": transitions,
    }


def blend_audit(templates: list[tuple[int, ...]]) -> dict[str, Any]:
    unique: list[tuple[int, ...]] = []
    for template in templates:
        if template not in unique:
            unique.append(template)
    maximum = 0
    productive_pairs = 0
    examples = []
    for left_index, left in enumerate(unique):
        for right_index in range(left_index, len(unique)):
            right = unique[right_index]
            covered = [
                target_index
                for target_index, target in enumerate(unique)
                if all(target[lane] in (left[lane], right[lane]) for lane in range(16))
            ]
            maximum = max(maximum, len(covered))
            new_targets = [target for target in covered if target not in (left_index, right_index)]
            if new_targets:
                productive_pairs += 1
                if len(examples) < 8:
                    examples.append({"left": left_index, "right": right_index, "new_targets": new_targets})
    return {
        "operation": "same-lane vpblendmd of two resident raw templates, arbitrary public lane mask",
        "unique_templates": len(unique),
        "maximum_templates_covered_by_a_pair_including_the_inputs": maximum,
        "pairs_creating_any_third_template": productive_pairs,
        "examples": examples,
        "verdict": "REJECTED: no pair creates any third Joe-Kuo template",
        "permuted_blend_note": "after arbitrary vpermd one source already contains every required lane value, so a second source/blend adds no expressive power",
    }


def permutation_kind(control: tuple[int, ...]) -> str:
    if control == tuple(range(16)):
        return "identity"
    if control == tuple(range(15, -1, -1)):
        return "reversal"
    for rotation in range(16):
        if control == tuple((lane + rotation) & 15 for lane in range(16)):
            return "rotation"
        if control == tuple((rotation - lane) & 15 for lane in range(16)):
            return "rotated_reversal"
    return "arbitrary"


def template_permutation_audit(templates: list[tuple[int, ...]]) -> dict[str, Any]:
    source = templates[0]
    controls = [tuple(source.index(value) for value in target) for target in templates]
    return {
        "all_dimensions_one_vpermd_from_D1": all(sorted(target) == sorted(source) for target in templates),
        "distinct_controls": len(set(controls)),
        "control_kinds_by_dimension": dict(Counter(permutation_kind(control) for control in controls)),
        "control_kinds_by_unique_template": dict(Counter(permutation_kind(control) for control in set(controls))),
        "cost": "one vpermd plus a 64-byte control load unless that exact control is resident; only identity is a simple public control here",
    }


def block_bases(table: np.ndarray, indices: np.ndarray) -> np.ndarray:
    gray = indices ^ (indices >> np.uint64(1))
    bases = np.zeros((indices.size, table.shape[0]), dtype=np.uint32)
    used = max(1, int(indices[-1]).bit_length())
    for bit in range(used):
        selected = ((gray >> np.uint64(bit)) & np.uint64(1)).astype(np.uint32)
        bases ^= selected[:, None] * table[:, bit][None, :]
    return bases


def _quantiles(values: np.ndarray) -> dict[str, float]:
    return {
        "min": float(np.min(values)),
        "p50": float(np.quantile(values, 0.5)),
        "mean": float(np.mean(values)),
        "p90": float(np.quantile(values, 0.9)),
        "max": float(np.max(values)),
    }


def step_permutation_audit(table: np.ndarray, lanes: int, start: int, points: int) -> dict[str, Any]:
    indices = np.arange(start, start + points, lanes, dtype=np.uint64)
    bases = block_bases(table, indices)
    low_mask = np.uint32((1 << (32 - (lanes.bit_length() - 1))) - 1)
    classes = bases & low_mask
    distinct = np.empty(indices.size, dtype=np.uint16)
    dynamic_top18 = np.empty(indices.size, dtype=np.uint16)
    duplicate_coverage = np.empty(indices.size, dtype=np.uint16)
    largest_group = np.empty(indices.size, dtype=np.uint16)
    own_group_size = np.empty_like(classes, dtype=np.uint16)
    for block, row in enumerate(classes):
        values, inverse, counts = np.unique(row, return_inverse=True, return_counts=True)
        del values
        distinct[block] = counts.size
        sorted_counts = np.sort(counts)
        dynamic_top18[block] = np.sum(sorted_counts[-18:])
        duplicate_coverage[block] = np.sum(counts[counts > 1])
        largest_group[block] = sorted_counts[-1]
        own_group_size[block] = counts[inverse]

    # A reproducible fixed-source heuristic: take the 18 dimensions with the
    # largest mean own-class size.  This is diagnostic, not an optimal search.
    fixed_dimensions = np.argsort(np.mean(own_group_size, axis=0))[::-1][:18]
    fixed_covered = np.zeros_like(classes, dtype=bool)
    for dimension in fixed_dimensions:
        fixed_covered |= classes == classes[:, dimension, None]
    fixed_coverage = np.sum(fixed_covered, axis=1)

    best_order = np.lexsort((indices, -dynamic_top18.astype(np.int64)))[:16]
    all_with_18 = indices[distinct <= 18]
    return {
        "lanes": lanes,
        "window": [start, start + points],
        "blocks": indices.size,
        "equivalence": f"two raw blocks are one-permute source-set matches iff lane-0 bases agree below the top {lanes.bit_length() - 1} bits",
        "distinct_source_sets_per_step": _quantiles(distinct),
        "dimensions_in_duplicate_source_sets": _quantiles(duplicate_coverage),
        "largest_source_set_group": _quantiles(largest_group),
        "dynamic_best_18_source_register_coverage": _quantiles(dynamic_top18),
        "blocks_fully_covered_by_at_most_18_dynamic_sources": int(np.count_nonzero(distinct <= 18)),
        "fully_covered_block_starts": [int(value) for value in all_with_18],
        "best_dynamic_steps": [
            {
                "block_start": int(indices[index]),
                "distinct_source_sets": int(distinct[index]),
                "top18_coverage_dimensions": int(dynamic_top18[index]),
                "largest_group": int(largest_group[index]),
            }
            for index in best_order
        ],
        "fixed_18_source_heuristic": {
            "dimension_ids": [int(value) + 1 for value in fixed_dimensions],
            "coverage": _quantiles(fixed_coverage),
            "warning": "fixed dimensions ranked by individual mean group size; this is not claimed globally optimal",
        },
        "instruction_model": "each covered non-source dimension still needs a control load/resident control plus one vpermd; source register selection is additional metadata",
    }


def second_half_audit(table: np.ndarray) -> dict[str, Any]:
    mismatches = 0
    delta_mismatches = 0
    rows = []
    for dimension, row in enumerate(table, 1):
        template32 = gray_words(row, 0, 32)
        delta = int(template32[16])
        mismatches += int(np.count_nonzero(template32[16:] != (template32[:16] ^ np.uint32(delta))))
        expected_delta = int(row[3]) ^ int(row[4])
        delta_mismatches += int(delta != expected_delta)
        rows.append({"dimension": dimension, "delta_hex": f"0x{delta:08x}"})
    return {
        "identity": "template32[16+l] = template16[l] XOR delta32[d]",
        "delta": "V[d,3] XOR V[d,4] (zero-based direction columns)",
        "word_mismatches": mismatches,
        "delta_mismatches": delta_mismatches,
        "all_dimensions_pass": mismatches == 0 and delta_mismatches == 0,
        "consequence": "a 32-point block needs one 16-lane template plus one scalar broadcast delta, not two independent resident template ZMMs",
        "per_dimension": rows,
    }


def make_markdown(report: dict[str, Any]) -> str:
    transition = report["resident_strategies"]["difference_cover_14_plus_current"]
    s17 = report["resident_strategies"]["17_plus_one_scratch"]
    s18 = report["resident_strategies"]["18_if_scratch_exists_elsewhere"]
    prod16 = report["step_specific_permutation"]["production_16"]
    prod32 = report["step_specific_permutation"]["production_32"]
    return f"""# 18-ZMM Joe--Kuo reuse audit

Status: **{report['status']}**. Read-only structural analysis; neither pricing engine was modified.

## Answer

Yes. The cheapest found schedule reuses the same mutable template register between dimensions. It is not a blend and does not need the rank-11 32-lane basis.

- Keep **14 resident templates plus one mutable current-template ZMM**. Their pairwise XORs cover every possible template-to-template delta.
- Initialize current from resident D1 with one register move. For every dimension change use `vpternlogd $0x96,resident_A,resident_B,current` in place.
- In natural D1--D256 order, **{transition['natural_D1_to_D256']['one_instruction_vpternlogd_transitions']} transitions cost one instruction**, **{transition['natural_D1_to_D256']['zero_instruction_same_template_transitions']} cost zero**, and none are uncovered. Including initialization, that is **{transition['natural_D1_to_D256']['including_initialization_instructions']} template-management instructions for 256 dimensions** ({transition['natural_D1_to_D256']['average_template_setup_or_transition_instructions_per_dimension']:.4f}/dimension).
- This occupies **{transition['total_zmm_registers']} of 18 free ZMMs**, leaving **{transition['free_from_budget_18']}** free. Once current is set for a dimension, reuse it for every aligned block of that dimension; each block remains one base XOR.

For random-access dimensions or a block-outer loop, the alternative resident-family result is:

- Reserve **17 ZMMs** for the most frequent exact 16-lane templates and one ZMM for the current composed template. This serves **{s17['direct_dimensions']} dimensions directly** and the other **{s17['three_source_dimensions']}** as XORs of three residents. Every D1--D256 template is covered exactly.
- The three-source case is one `vpternlogd $0x96` after copying a resident, or—when producing immediately—`vpxord base,A,out` followed by `vpternlogd $0x96,B,C,out`. It performs no template load.
- If an output/scratch register already exists outside the 18 free registers, all 18 may be resident: **{s18['direct_dimensions']} direct dimensions**, **{s18['three_source_dimensions']} three-source dimensions**.
- If dimensions are outermost, construct a nonresident template once, retain it for every block of that dimension, and amortize the two setup instructions over all block iterations. Every subsequent 16-lane block is one base XOR.
- For 32 points, `high_template = low_template XOR broadcast(delta[d])` exactly in all 256 dimensions. The 32-lane rank-11/22-ZMM representation is unnecessary under this permitted broadcast composition.

## Operations that do not help

- Same-lane `vpblendmd`: **rejected**. Across all 63 exact templates, no pair constructs any third template.
- One arbitrary `vpermd` from D1 can construct every template, but 62/63 controls are arbitrary. Loading a 64-byte control replaces the 64-byte template load and adds a permute, so it is not cheaper unless that exact control is already resident.
- `vpermt2d`/permuted blend gains no expressive power here: either resident template already contains the complete 16-value source set.

## Step-specific raw-output reuse

At the beginning of the sequence there is a real special window: the first 32 aligned blocks are fully coverable by at most 18 dynamically chosen source registers—indices 0--511 for 16 lanes and 0--1023 for 32 lanes.

That does **not** transfer to the production window starting at 8192:

| Metric | 16 lanes | 32 lanes |
|---|---:|---:|
| Median distinct source sets per step | {prod16['distinct_source_sets_per_step']['p50']:.0f} | {prod32['distinct_source_sets_per_step']['p50']:.0f} |
| Mean dimensions covered by dynamically best 18 | {prod16['dynamic_best_18_source_register_coverage']['mean']:.2f} | {prod32['dynamic_best_18_source_register_coverage']['mean']:.2f} |
| Maximum dynamic coverage | {prod16['dynamic_best_18_source_register_coverage']['max']:.0f} | {prod32['dynamic_best_18_source_register_coverage']['max']:.0f} |
| Mean coverage from fixed 18-source heuristic | {prod16['fixed_18_source_heuristic']['coverage']['mean']:.2f} | {prod32['fixed_18_source_heuristic']['coverage']['mean']:.2f} |
| Production blocks fully covered by 18 | {prod16['blocks_fully_covered_by_at_most_18_dynamic_sources']} | {prod32['blocks_fully_covered_by_at_most_18_dynamic_sources']} |

Thus, retaining *raw output ZMMs from 18 other dimensions at the same production step* is not useful on average. Retaining the **dimension-independent template family** is useful because its three-resident construction works at every block.

## Recommended experiment boundary

1. Best sequential structural candidate: 14-template difference cover + one mutable current register; one ternary XOR per changed dimension and three ZMMs left free.
2. For random dimension access or block-outer traversal: 17 frequent residents + one composed-template scratch.
3. Also valid: the eight-register algebraic core; it covers all dimensions but needs reconstruction rather than one-instruction transitions.
4. Keep the 32-point high half as one broadcast-XOR derivative of the low half.
5. Do not pursue same-lane blend or production-step raw-source caching.
6. This is an instruction/load model, not a cycle win. A future benchmark must account for the real surrounding allocation and dependency chain.

Full masks, resident dimension representatives, per-dimension ternary recipes, step distributions and 32-half deltas are in `audit.json`.
"""


def run() -> dict[str, Any]:
    table = load_direction_table(DEFAULT_TABLE)
    raw = template_audit(table, 16)
    masks = [int(value, 16) for value in raw["gf2"]["representation_masks_hex_by_dimension"]]
    templates = [tuple(int(value) for value in gray_words(row, 0, 16)) for row in table]
    strategies = {str(budget): resident_strategy(masks, budget) for budget in (8, 13, 17, 18)}
    report = {
        "schema_version": 1,
        "status": "PASS",
        "scope": "read-only 18-ZMM exact reuse audit for genuine Joe-Kuo D1-D256",
        "direction_table": {"path": str(DEFAULT_TABLE.relative_to(ROOT)), "sha256": sha256_file(DEFAULT_TABLE)},
        "dimensions": DIMENSIONS,
        "free_zmm_budget": 18,
        "resident_strategies": {
            "difference_cover_14_plus_current": difference_cover_strategy(masks),
            "algebraic_core_8": algebraic_eight_strategy(masks),
            "top_frequency_13": strategies["13"],
            "17_plus_one_scratch": strategies["17"],
            "18_if_scratch_exists_elsewhere": strategies["18"],
        },
        "ternary_xor": {
            "instruction": "vpternlogd $0x96,A,B,dest computes dest XOR A XOR B",
            "exact": True,
            "note": "templates only; Gaussian, x and growth values are never XOR-combined",
        },
        "same_lane_blend": blend_audit(templates),
        "template_permutation": template_permutation_audit(templates),
        "block32_from_block16": second_half_audit(table),
        "step_specific_permutation": {
            "initial_16": step_permutation_audit(table, 16, 0, POINTS),
            "initial_32": step_permutation_audit(table, 32, 0, POINTS),
            "production_16": step_permutation_audit(table, 16, PRODUCTION_START, POINTS),
            "production_32": step_permutation_audit(table, 32, PRODUCTION_START, POINTS),
        },
        "recommendation": {
            "candidate": "14 resident difference-cover templates plus one mutable current-template register",
            "works_every_block": True,
            "best_loop_order": "natural dimension order outer, aligned blocks inner; update current with one in-place ternary XOR",
            "blend": "reject",
            "step_specific_raw_register_reuse_at_8192": "reject except sparse opportunistic steps; fixed 18 sources cover about 18-19 dimensions on average",
            "integration_or_engine_change_performed": False,
            "native_cycle_win_claimed": False,
        },
    }
    if not (
        report["resident_strategies"]["17_plus_one_scratch"]["all_dimensions_covered"]
        and report["resident_strategies"]["difference_cover_14_plus_current"]["all_dimension_to_dimension_transitions_covered"]
        and report["resident_strategies"]["18_if_scratch_exists_elsewhere"]["all_dimensions_covered"]
        and report["block32_from_block16"]["all_dimensions_pass"]
        and report["same_lane_blend"]["pairs_creating_any_third_template"] == 0
    ):
        report["status"] = "FAIL"
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    if args.out_dir.exists() and any(args.out_dir.iterdir()):
        raise SystemExit(f"refusing to overwrite non-empty result directory: {args.out_dir}")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    report = run()
    (args.out_dir / "audit.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (args.out_dir / "README.md").write_text(make_markdown(report), encoding="utf-8")
    print(json.dumps({"status": report["status"], "out_dir": str(args.out_dir)}, sort_keys=True))
    return 0 if report["status"] == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
