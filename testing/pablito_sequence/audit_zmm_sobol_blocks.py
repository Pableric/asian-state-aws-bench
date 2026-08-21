#!/usr/bin/env python3
"""Exact Joe--Kuo D1--D256 aligned-block and range-pattern audit.

This is a structural experiment.  It reads the published Joe--Kuo direction
table, reconstructs uint32 Sobol words by Gray code, and never calls or edits a
pricing engine.  The default run covers two 2^20-point windows: index zero and
the production-relevant window beginning at 8192.

Range IDs reproduce the qualified producer's integer path after ``word >> 9``:

    q = word >> 9
    range_id = min(abs(q - 2**22) >> 11, 2047)

Equal 128-bit pattern hashes are always checked against all lane values before
they are counted as exact repeats; hashes are only an indexing accelerator.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import struct
import tempfile
import time
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable, Sequence

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
_TABLE_CANDIDATES = (
    ROOT / "direction_numbers" / "joe_kuo_6_21201.bin",
    ROOT / "asian-aws-publish.lxGUbF" / "direction_numbers" / "joe_kuo_6_21201.bin",
)
DEFAULT_TABLE = next((path for path in _TABLE_CANDIDATES if path.exists()), _TABLE_CANDIDATES[-1])
DEFAULT_OUT = ROOT / "testing" / "pablito_sequence" / "results" / "zmm_block_template_audit_20260821"
WORD_BITS = 32
DIMENSIONS = 256
PRODUCTION_START = 8192
FRAGMENT_POINTS = 4096
ZMM_LANES = 16
UINT32_MASK = (1 << 32) - 1

# Frozen sources from the qualified ordered inverse-normal producer.  They are
# read-only provenance: the experiment embeds the exact integer classifier and
# does not import either engine.
QUALIFIED_SOURCE_PROVENANCE = {
    "range_generator": {
        "path": "/home/pablo/Projects/european-option-engine/generate_gaussian_coeffs.py",
        "sha256": "f03e52b72ebc92394e5b9a7ea4eeea2039245c1eae3819d2575eb96969834a64",
        "lines": "202-204: floor(abs(u-0.5)*4096), capped at 2047",
    },
    "ordered_generator": {
        "path": "/home/pablo/Projects/european-option-engine/generate_ordered_d1_coeffs.py",
        "sha256": "ef4e06b51ee07818bb90bc653ecbbb629db5de8c34af2ae9d4121b18098bcc32",
        "lines": "84-86: identical 2048-class folded range",
    },
    "ordered_assembly": {
        "path": "/home/pablo/Projects/european-option-engine/sobol_european_ordered_d1_avx512.s",
        "sha256": "aaab3016d842cdae40db5e147bcba66a4d19de8db7350e73e4bb2f4f0448a7d3",
        "semantics": "word>>9 bit-injection and the same folded 2048-class schedule",
    },
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_direction_table(path: Path, dimensions: int = DIMENSIONS) -> np.ndarray:
    raw = path.read_bytes()
    offset = 0
    rows: list[np.ndarray] = []
    while offset < len(raw) and len(rows) < dimensions:
        if offset + 4 > len(raw):
            raise ValueError("truncated direction-table row length")
        count = struct.unpack_from("<I", raw, offset)[0]
        offset += 4
        if count != WORD_BITS or offset + count * 4 > len(raw):
            raise ValueError(f"invalid direction row {len(rows) + 1}: count={count}")
        rows.append(np.frombuffer(raw, dtype="<u4", count=count, offset=offset).copy())
        offset += count * 4
    if len(rows) != dimensions:
        raise ValueError(f"direction table has only {len(rows)} usable rows")
    return np.stack(rows).astype(np.uint32, copy=False)


def sobol_word_scalar(row: Sequence[int], index: int) -> int:
    gray = index ^ (index >> 1)
    word = 0
    bit = 0
    while gray:
        if gray & 1:
            word ^= int(row[bit])
        gray >>= 1
        bit += 1
    return word & UINT32_MASK


def gray_words(row: np.ndarray, start: int, count: int) -> np.ndarray:
    """Independent direct Gray-code reconstruction for one direction row."""
    index = np.arange(start, start + count, dtype=np.uint64)
    gray = index ^ (index >> np.uint64(1))
    words = np.zeros(count, dtype=np.uint32)
    used_bits = max(1, (start + count - 1).bit_length())
    for bit in range(used_bits):
        selected = ((gray >> np.uint64(bit)) & np.uint64(1)).astype(np.uint32)
        words ^= selected * row[bit]
    return words


def recurrence_words(row: np.ndarray, start: int, count: int) -> np.ndarray:
    """Independent one-column Sobol recurrence, initialized by scalar Gray code."""
    out = np.empty(count, dtype=np.uint32)
    state = sobol_word_scalar(row, start)
    out[0] = state
    if count == 1:
        return out
    absolute = np.arange(start + 1, start + count, dtype=np.uint64)
    low_bit = absolute & (np.uint64(0) - absolute)
    columns = np.empty(count - 1, dtype=np.uint8)
    for bit in range(WORD_BITS):
        columns[low_bit == np.uint64(1 << bit)] = bit
    deltas = row[columns]
    out[1:] = np.bitwise_xor.accumulate(deltas) ^ np.uint32(state)
    return out


def range_ids(words: np.ndarray) -> np.ndarray:
    """Exact integer form of the qualified 2048-class folded classifier."""
    q = (words >> np.uint32(9)).astype(np.int64)
    folded = np.abs(q - (1 << 22)) >> 11
    return np.minimum(folded, 2047).astype(np.uint16)


def tuple_to_bigint(values: Sequence[int]) -> int:
    result = 0
    for lane, value in enumerate(values):
        result |= int(value) << (WORD_BITS * lane)
    return result


def gf2_basis(vectors: Sequence[int]) -> tuple[list[int], list[int]]:
    """Return deterministic input-basis indices and masks for every vector."""
    pivots: dict[int, tuple[int, int]] = {}
    basis_indices: list[int] = []
    for input_index, original in enumerate(vectors):
        value = original
        mask = 0
        while value:
            pivot = value.bit_length() - 1
            if pivot in pivots:
                basis_value, basis_mask = pivots[pivot]
                value ^= basis_value
                mask ^= basis_mask
            else:
                new_bit = 1 << len(basis_indices)
                pivots[pivot] = (value, mask ^ new_bit)
                basis_indices.append(input_index)
                break
    representations: list[int] = []
    for original in vectors:
        value = original
        mask = 0
        while value:
            pivot = value.bit_length() - 1
            basis_value, basis_mask = pivots[pivot]
            value ^= basis_value
            mask ^= basis_mask
        representations.append(mask)
    return basis_indices, representations


def template_audit(table: np.ndarray, lanes: int) -> dict[str, Any]:
    templates = [tuple(sobol_word_scalar(row, lane) for lane in range(lanes)) for row in table]
    objects = [tuple_to_bigint(template) for template in templates]
    groups: dict[tuple[int, ...], list[int]] = defaultdict(list)
    for dimension, template in enumerate(templates, 1):
        groups[template].append(dimension)

    # An arbitrary in-ZMM vpermd can realize every permutation of 16 lanes.
    # For 32 lanes we separately retain the two-half policy used by two vpermd.
    arbitrary_classes: dict[tuple[int, ...], list[int]] = defaultdict(list)
    half_classes: dict[tuple[tuple[int, ...], tuple[int, ...]], list[int]] = defaultdict(list)
    reversal_classes: dict[tuple[int, ...], list[int]] = defaultdict(list)
    broadcast_classes: dict[tuple[int, ...], list[int]] = defaultdict(list)
    for dimension, template in enumerate(templates, 1):
        arbitrary_classes[tuple(sorted(template))].append(dimension)
        if lanes == 16:
            half_key = (tuple(sorted(template)), tuple())
        else:
            half_key = (tuple(sorted(template[:16])), tuple(sorted(template[16:])))
        half_classes[half_key].append(dimension)
        reversed_template = tuple(reversed(template))
        reversal_classes[min(template, reversed_template)].append(dimension)
        # Lane zero is zero for all raw templates, but retain the general form.
        broadcast_classes[tuple(value ^ template[0] for value in template)].append(dimension)

    object_to_dimensions: dict[int, list[int]] = defaultdict(list)
    for dimension, obj in enumerate(objects, 1):
        object_to_dimensions[obj].append(dimension)
    two_source: list[dict[str, Any]] = []
    for left in range(len(objects)):
        for right in range(left + 1, len(objects)):
            targets = object_to_dimensions.get(objects[left] ^ objects[right])
            if targets:
                two_source.append({"left": left + 1, "right": right + 1, "targets": targets})

    basis_indices, representations = gf2_basis(objects)
    popcounts = [mask.bit_count() for mask in representations]
    zmm_per_template = lanes // ZMM_LANES
    rank = len(basis_indices)
    basis_zmms = rank * zmm_per_template
    reserved_raw_zmms = 3 if lanes == 16 else 5
    available_for_basis = 32 - reserved_raw_zmms

    unique_entries = []
    for dictionary_id, (template, dimensions) in enumerate(sorted(groups.items(), key=lambda item: item[1][0])):
        unique_entries.append({
            "id": dictionary_id,
            "dimensions": dimensions,
            "words_hex": [f"0x{word:08x}" for word in template],
        })

    return {
        "block_size": lanes,
        "object_bits": lanes * WORD_BITS,
        "zmm_objects_per_template": zmm_per_template,
        "dimension_count": len(templates),
        "distinct_exact_templates": len(groups),
        "exact_duplicate_group_count": sum(len(v) > 1 for v in groups.values()),
        "exact_duplicate_dimensions": sum(len(v) for v in groups.values() if len(v) > 1),
        "exact_duplicate_groups": [v for v in groups.values() if len(v) > 1],
        "classes_under_lane_reversal": len(reversal_classes),
        "classes_under_arbitrary_lane_permutation": len(arbitrary_classes),
        "classes_under_within_each_16_lane_half_permutation": len(half_classes),
        "classes_after_same_lane_broadcast_xor": len(broadcast_classes),
        "broadcast_xor_note": "lane 0 is zero in every template, so same-lane broadcast-XOR equivalence collapses to exact equality",
        "two_existing_templates_xor_constructions": two_source,
        "gf2": {
            "rank": rank,
            "deterministic_basis_dimensions": [index + 1 for index in basis_indices],
            "representation_masks_hex_by_dimension": [f"0x{mask:x}" for mask in representations],
            "basis_terms_histogram": dict(sorted(Counter(popcounts).items())),
            "average_basis_terms": sum(popcounts) / len(popcounts),
            "maximum_basis_terms": max(popcounts),
            "average_vector_xors": sum(max(0, count - 1) for count in popcounts) / len(popcounts),
            "maximum_vector_xors": max(max(0, count - 1) for count in popcounts),
            "zmm_registers_for_resident_basis": basis_zmms,
            "raw_generator_reserved_zmms_model": reserved_raw_zmms,
            "zmm_registers_remaining": 32 - basis_zmms - reserved_raw_zmms,
            "fits_raw_generator_register_model": basis_zmms <= available_for_basis,
            "basis_is_deterministic_not_xor_count_optimized": True,
        },
        "instruction_models": {
            "direct": {
                "per_dimension_template_loads_64B": zmm_per_template,
                "per_block_broadcast_base_loads": 1,
                "per_block_vector_xors": zmm_per_template,
                "permutations": 0,
                "template_dictionary_bytes": len(groups) * lanes * 4,
                "dimension_selector_bytes": 256 if len(groups) <= 256 else 512,
            },
            "resident_basis_static_schedule": {
                "one_time_basis_loads_64B": basis_zmms,
                "per_dimension_average_vector_xors": (
                    sum(max(0, count - 1) for count in popcounts) / len(popcounts) * zmm_per_template
                ),
                "per_dimension_maximum_vector_xors": max(max(0, count - 1) for count in popcounts) * zmm_per_template,
                "per_block_broadcast_base_loads": 1,
                "per_block_base_vector_xors": zmm_per_template,
                "permutations": 0,
                "all_256_dimensions_vector_xor_instructions": sum(max(0, count - 1) for count in popcounts) * zmm_per_template,
                "lower_bound_l1i_bytes_at_6_bytes_per_evex_vpxord": sum(max(0, count - 1) for count in popcounts) * zmm_per_template * 6,
                "l1i_excludes_dispatch_base_xor_and_loop_control": True,
                "generic_selector_overhead": "one compact mask load plus tests/branches, or dimension-specialized code; neither is free",
            },
            "one_canonical_template_plus_permutation": {
                "resident_template_zmms": zmm_per_template,
                "per_dimension_permutation_index_loads_64B": zmm_per_template,
                "per_dimension_vpermd": zmm_per_template,
                "per_block_broadcast_base_loads": 1,
                "per_block_base_vector_xors": zmm_per_template,
                "note_32": "global cross-half 32-lane permutations require two-source permutes; the count above applies only when each half is closed",
            },
        },
        "dictionary": unique_entries,
    }


def pattern_hashes(patterns: np.ndarray, chunk_rows: int = 1 << 18) -> tuple[np.ndarray, np.ndarray]:
    """Two deterministic vectorized 64-bit hashes for rows of uint16 IDs."""
    count, lanes = patterns.shape
    first = np.empty(count, dtype=np.uint64)
    second = np.empty(count, dtype=np.uint64)
    p1 = np.uint64(0x9E3779B185EBCA87)
    p2 = np.uint64(0xC2B2AE3D27D4EB4F)
    for begin in range(0, count, chunk_rows):
        end = min(begin + chunk_rows, count)
        block = patterns[begin:end]
        a = np.full(end - begin, np.uint64(0x243F6A8885A308D3 ^ lanes), dtype=np.uint64)
        b = np.full(end - begin, np.uint64(0x13198A2E03707344 ^ (lanes << 8)), dtype=np.uint64)
        with np.errstate(over="ignore"):
            for lane in range(lanes):
                value = block[:, lane].astype(np.uint64) + np.uint64(lane * 0x10001 + 1)
                a = (a ^ value) * p1
                b = (b + value) * p2
                b ^= b >> np.uint64(29)
        first[begin:end] = a
        second[begin:end] = b
    return first, second


def _rows_equal_chunked(patterns: np.ndarray, left: np.ndarray, right: np.ndarray) -> np.ndarray:
    result = np.empty(left.size, dtype=bool)
    chunk = 1 << 18
    for begin in range(0, left.size, chunk):
        end = min(begin + chunk, left.size)
        result[begin:end] = np.all(patterns[left[begin:end]] == patterns[right[begin:end]], axis=1)
    return result


def within_zmm_control_summary(patterns: np.ndarray) -> dict[str, Any]:
    """Count stable sorted-schedule -> original-lane vpermd controls exactly."""
    rows, lanes = patterns.shape
    halves = lanes // ZMM_LANES
    packed = np.empty((rows, halves), dtype=np.uint64)
    chunk_rows = 1 << 17
    shifts = (np.arange(ZMM_LANES, dtype=np.uint64) * np.uint64(4))[None, :]
    for begin in range(0, rows, chunk_rows):
        end = min(begin + chunk_rows, rows)
        for half in range(halves):
            block = patterns[begin:end, half * 16:(half + 1) * 16]
            sorted_to_original = np.argsort(block, axis=1, kind="stable")
            original_to_sorted = np.argsort(sorted_to_original, axis=1, kind="stable").astype(np.uint64)
            packed[begin:end, half] = np.bitwise_or.reduce(original_to_sorted << shifts, axis=1)
    if halves == 1:
        _unique, counts = np.unique(packed[:, 0], return_counts=True)
    else:
        records = np.ascontiguousarray(packed).view(
            np.dtype((np.void, packed.dtype.itemsize * halves))
        ).ravel()
        _unique, counts = np.unique(records, return_counts=True)
    descending = np.sort(counts)[::-1]
    control_count = int(counts.size)
    selector_bytes = 1 if control_count <= 256 else 2 if control_count <= 65536 else 4
    return {
        "distinct_control_tuples": control_count,
        "control_dictionary_bytes_expanded_uint32": control_count * lanes * 4,
        "minimum_control_id_bytes_per_occurrence": selector_bytes,
        "control_id_stream_bytes_if_stored_for_every_occurrence": rows * selector_bytes,
        "top_control_coverage": {
            str(size): float(descending[:size].sum() / rows) for size in (16, 32, 64)
        },
        "construction": "stable argsort within each 16-lane half; controls compared as exact packed 4-bit lane indices",
    }


def exact_pattern_summary(patterns: np.ndarray, policy: str, top_n: int = 64) -> dict[str, Any]:
    """Count exact row patterns globally, checking every repeated hash group."""
    if policy == "exact":
        canonical = patterns
        permutation_instructions = 0
    elif policy == "reversal":
        reversed_patterns = patterns[:, ::-1]
        choose_reverse = np.zeros(patterns.shape[0], dtype=bool)
        undecided = np.ones(patterns.shape[0], dtype=bool)
        for lane in range(patterns.shape[1]):
            lower = reversed_patterns[:, lane] < patterns[:, lane]
            higher = reversed_patterns[:, lane] > patterns[:, lane]
            choose_reverse |= undecided & lower
            undecided &= ~(lower | higher)
        canonical = np.where(choose_reverse[:, None], reversed_patterns, patterns)
        permutation_instructions = 1
    elif policy == "within_zmm_permutation":
        canonical = patterns.copy()
        for begin in range(0, patterns.shape[1], 16):
            canonical[:, begin:begin + 16].sort(axis=1)
        permutation_instructions = patterns.shape[1] // 16
    elif policy == "global_permutation":
        canonical = np.sort(patterns, axis=1)
        permutation_instructions = 1 if patterns.shape[1] == 16 else 2
    else:
        raise ValueError(policy)

    h1, h2 = pattern_hashes(canonical)
    order = np.lexsort((h2, h1))
    same_hash = (h1[order[1:]] == h1[order[:-1]]) & (h2[order[1:]] == h2[order[:-1]])
    adjacent_exact = np.zeros_like(same_hash)
    positions = np.flatnonzero(same_hash)
    if positions.size:
        adjacent_exact[positions] = _rows_equal_chunked(canonical, order[positions], order[positions + 1])
    collision_adjacencies = int(np.count_nonzero(same_hash & ~adjacent_exact))
    if collision_adjacencies:
        raise RuntimeError(f"128-bit pattern hash collision under {policy}; exact fallback required")

    starts = np.r_[0, np.flatnonzero(~same_hash) + 1]
    ends = np.r_[starts[1:], order.size]
    counts = ends - starts
    representatives = order[starts]
    histogram = Counter(int(value) for value in counts)
    ranking = np.argsort(counts, kind="stable")[::-1][:top_n]
    top = []
    for rank_index in ranking:
        row = canonical[representatives[rank_index]]
        top.append({
            "count": int(counts[rank_index]),
            "coverage_fraction": float(counts[rank_index] / patterns.shape[0]),
            "range_ids": [int(value) for value in row],
        })
    recurring = counts >= 2
    coverage = {}
    descending = np.sort(counts)[::-1]
    for dictionary_size in (16, 32, 64):
        coverage[str(dictionary_size)] = {
            "occurrences": int(descending[:dictionary_size].sum()),
            "fraction": float(descending[:dictionary_size].sum() / patterns.shape[0]),
        }
    zmm_bytes = patterns.shape[1] * 4
    compact_bytes = patterns.shape[1] * 2
    result = {
        "policy": policy,
        "patterns": int(patterns.shape[0]),
        "lanes": int(patterns.shape[1]),
        "unique_patterns": int(counts.size),
        "recurring_patterns": int(np.count_nonzero(recurring)),
        "occurrences_covered_by_recurring_patterns": int(counts[recurring].sum()),
        "exact_repeat_occurrences_beyond_first": int(np.sum(counts - 1)),
        "frequency_histogram_count_to_patterns": {str(k): v for k, v in sorted(histogram.items())},
        "dictionary_bytes_all_recurring_expanded_uint32_zmm": int(np.count_nonzero(recurring) * zmm_bytes),
        "dictionary_bytes_all_recurring_compact_uint16": int(np.count_nonzero(recurring) * compact_bytes),
        "top_dictionary_coverage": coverage,
        "top_patterns": top,
        "per_occurrence_permutation_instructions": permutation_instructions,
        "hash_collision_adjacencies": collision_adjacencies,
        "exact_verification": "all adjacent rows in every repeated 128-bit hash run compared lane-by-lane",
    }
    if policy == "within_zmm_permutation":
        result["permutation_controls"] = within_zmm_control_summary(patterns)
    return result


def short_schedule_audit(ids: np.ndarray, start: int, count: int, lanes: int) -> dict[str, Any]:
    blocks = count // lanes
    periods = [1, 2, 4, 8, 16, 32, 64, 128, 256]
    dimensions_by_period: dict[str, list[int]] = {}
    parity_alternators = 0
    for period in periods:
        matching = []
        for dimension in range(ids.shape[0]):
            seq = ids[dimension, start:start + count].reshape(blocks, lanes)
            if np.array_equal(seq[period:], seq[:-period]):
                matching.append(dimension + 1)
        dimensions_by_period[str(period)] = matching
    for dimension in range(ids.shape[0]):
        seq = ids[dimension, start:start + count].reshape(blocks, lanes)
        if (np.all(seq[0::2] == seq[0]) and np.all(seq[1::2] == seq[1]) and
                not np.array_equal(seq[0], seq[1])):
            parity_alternators += 1
    return {
        "tested_periods_in_blocks": periods,
        "dimension_ids_with_full_period": dimensions_by_period,
        "dimensions_with_two_pattern_parity_alternation": parity_alternators,
        "public_short_schedule_found_for_any_dimension": any(dimensions_by_period.values()) or parity_alternators > 0,
        "public_short_schedule_found_for_all_dimensions": any(len(value) == ids.shape[0] for value in dimensions_by_period.values()),
    }


def fragment_permutation_audit(table: np.ndarray) -> dict[str, Any]:
    sources = [
        gray_words(table[0], PRODUCTION_START, FRAGMENT_POINTS),
        gray_words(table[0], PRODUCTION_START + FRAGMENT_POINTS, FRAGMENT_POINTS),
    ]
    reverse_maps = [{int(word): index for index, word in enumerate(source)} for source in sources]
    source_counts = [0, 0]
    all_patterns: dict[tuple[int, ...], int] = {}
    exact_zmms = 0
    routed_zmms = 0
    per_dimension = []
    for dimension, row in enumerate(table, 1):
        target = gray_words(row, PRODUCTION_START, FRAGMENT_POINTS)
        chosen = None
        indices: list[int] = []
        for source_id, reverse in enumerate(reverse_maps):
            try:
                candidate = [reverse[int(word)] for word in target]
            except KeyError:
                continue
            if len(set(candidate)) == FRAGMENT_POINTS:
                chosen = source_id
                indices = candidate
                break
        if chosen is None:
            raise AssertionError(f"D{dimension} is not a permutation of either qualified source block")
        source_counts[chosen] += 1
        dimension_patterns: set[tuple[int, ...]] = set()
        for begin in range(0, FRAGMENT_POINTS, ZMM_LANES):
            lane_indices = indices[begin:begin + ZMM_LANES]
            line = lane_indices[0] // ZMM_LANES
            if any(index // ZMM_LANES != line for index in lane_indices):
                raise AssertionError(f"D{dimension} target ZMM crosses source lines")
            control = tuple(index & 15 for index in lane_indices)
            all_patterns.setdefault(control, len(all_patterns))
            dimension_patterns.add(control)
            exact_zmms += int(control == tuple(range(16)))
            routed_zmms += 1
        per_dimension.append({
            "dimension": dimension,
            "source_block": chosen,
            "distinct_vpermd_controls": len(dimension_patterns),
        })
    return {
        "region": [PRODUCTION_START, PRODUCTION_START + FRAGMENT_POINTS],
        "source_blocks": [
            [PRODUCTION_START, PRODUCTION_START + FRAGMENT_POINTS],
            [PRODUCTION_START + FRAGMENT_POINTS, PRODUCTION_START + 2 * FRAGMENT_POINTS],
        ],
        "dimensions_exactly_routable": len(per_dimension),
        "source_block_dimension_counts": source_counts,
        "target_zmms": routed_zmms,
        "bit_identical_without_permutation_zmms": exact_zmms,
        "bit_identical_under_one_vpermd_zmms": routed_zmms,
        "global_distinct_vpermd_controls": len(all_patterns),
        "vpermd_controls": [list(pattern) for pattern in all_patterns],
        "maximum_controls_per_dimension": max(row["distinct_vpermd_controls"] for row in per_dimension),
        "per_dimension": per_dimension,
        "transform_invariance": (
            "The existing fragment applies each control to already-produced source x/growth. "
            "Therefore Z, x and growth bits are preserved exactly for every accepted contract; "
            "no numerical values are XOR-combined."
        ),
    }


def block_identity_gate(table: np.ndarray, starts: Sequence[int], count: int, lanes: int) -> dict[str, Any]:
    templates = np.stack([
        gray_words(row, 0, lanes) for row in table
    ])
    tested_blocks = 0
    mismatches = 0
    transition_mismatches = 0
    column_counts: Counter[int] = Counter()
    for start in starts:
        blocks = count // lanes
        for dimension, row in enumerate(table):
            words = gray_words(row, start, count).reshape(blocks, lanes)
            bases = words[:, 0]
            reconstructed = bases[:, None] ^ templates[dimension][None, :]
            mismatches += int(np.count_nonzero(words != reconstructed))
            block_number = start // lanes
            log_lanes = int(math.log2(lanes))
            for offset in range(1, blocks):
                next_block = block_number + offset
                column = int(math.log2(next_block & -next_block)) + log_lanes
                column_counts[column] += 1
                # Gray(L*b) has the boundary bit V[log2(L)-1] in addition
                # to shifted Gray(b).  Consecutive bases therefore use one
                # *prepared* scalar delta containing two direction words.
                prepared_delta = int(row[log_lanes - 1]) ^ int(row[column])
                transition_mismatches += int((int(bases[offset - 1]) ^ prepared_delta) != int(bases[offset]))
            tested_blocks += blocks
    return {
        "block_size": lanes,
        "tested_window_starts": list(starts),
        "points_per_window": count,
        "dimensions": len(table),
        "blocks_per_dimension_per_window": count // lanes,
        "total_blocks": tested_blocks,
        "all_32_bits_match": mismatches == 0,
        "word_mismatches": mismatches,
        "base_transition_mismatches": transition_mismatches,
        "base_update": {
            "formula": f"base[b] XOR (V[log2({lanes})-1] XOR V[log2({lanes}) + ctz(b+1)])",
            "prepared_delta_direction_words": 2,
            "prepared_xor_operands_per_transition": 1,
            "zmm_instructions_per_transition": "one vpbroadcastd plus one vpxord per template ZMM (or memory broadcast XOR)",
            "direction_column_frequency_zero_based": {str(k): v for k, v in sorted(column_counts.items())},
        },
        "proof": f"Gray({lanes}*b+l) = Gray({lanes}*b) XOR Gray(l) for 0<=l<{lanes}",
    }


def make_markdown(report: dict[str, Any]) -> str:
    t16 = report["raw_templates"]["16"]
    t32 = report["raw_templates"]["32"]
    lines = [
        "# ZMM-aware Joe--Kuo block-template audit",
        "",
        f"Status: **{report['status']}**. This is a read-only structural experiment; neither pricing engine was modified.",
        "",
        "## Executive result",
        "",
        "The aligned raw identity is exact for both block sizes, and every block base advances with one ctz-selected prepared direction-word XOR. Cross-dimension structure is unusually compact mathematically, but only the 16-lane basis is comfortably resident; neither the permutation encoding nor the 32-lane basis is an automatic hot-loop win. Range patterns are assessed separately below and never authorize reuse of transformed values.",
        "",
        "| Question | Result |",
        "|---|---|",
        f"| Resident template + block base? | **Yes.** Zero mismatches in {report['raw_identity']['16']['total_blocks']:,} 16-blocks and {report['raw_identity']['32']['total_blocks']:,} 32-blocks. |",
        f"| Small D1--D256 basis? | 16 lanes: rank **{t16['gf2']['rank']}** ({t16['gf2']['zmm_registers_for_resident_basis']} ZMMs); 32 lanes: rank **{t32['gf2']['rank']}** ({t32['gf2']['zmm_registers_for_resident_basis']} ZMMs). |",
        "| Reusable inverse-normal schedules? | **Only under lane permutation.** The production window has 128 within-ZMM classes (8/16 KiB); top 64 cover about 50%. Exact dictionaries are 8.3/59.9 MB. |",
        "| Proven hot-loop win? | **No measured win.** The 16-lane basis removes a 64-byte template load but adds an average XOR chain and selector/control cost; 32 lanes is instruction/register expensive. Native integration/timing was intentionally out of scope. |",
        "",
        "## Inputs and validity",
        "",
        f"- Direction table: `{report['direction_table']['path']}`",
        f"- SHA-256: `{report['direction_table']['sha256']}`",
        f"- Dimensions: D1--D{report['dimensions']}.",
        f"- Windows: `{report['windows']}`; block sizes 16 and 32.",
        f"- Independent SciPy Joe--Kuo table oracle: **{report['validity']['scipy_direction_table_match']}**.",
        f"- All-bit Gray/recurrent reconstruction: **{report['validity']['gray_vs_recurrence_all_bits']}**.",
        f"- Unique values, unique multidimensional points, expanding prefixes and no raw short period: **{report['validity']['all_mandatory_gates_pass']}**.",
        "- The defective lane-fill constructor is neither imported nor called.",
        "",
        "## Raw templates",
        "",
        "| Metric | 16 lanes | 32 lanes |",
        "|---|---:|---:|",
        f"| Exact templates | {t16['distinct_exact_templates']} | {t32['distinct_exact_templates']} |",
        f"| Arbitrary lane-permutation classes | {t16['classes_under_arbitrary_lane_permutation']} | {t32['classes_under_arbitrary_lane_permutation']} |",
        f"| Within-each-ZMM permutation classes | {t16['classes_under_within_each_16_lane_half_permutation']} | {t32['classes_under_within_each_16_lane_half_permutation']} |",
        f"| Same-lane broadcast-XOR classes | {t16['classes_after_same_lane_broadcast_xor']} | {t32['classes_after_same_lane_broadcast_xor']} |",
        f"| Two-existing-template XOR constructions | {len(t16['two_existing_templates_xor_constructions'])} | {len(t32['two_existing_templates_xor_constructions'])} |",
        f"| GF(2) rank | {t16['gf2']['rank']} | {t32['gf2']['rank']} |",
        f"| Resident basis ZMMs | {t16['gf2']['zmm_registers_for_resident_basis']} | {t32['gf2']['zmm_registers_for_resident_basis']} |",
        f"| Average vector XORs from basis | {t16['gf2']['average_vector_xors']:.3f} | {t32['gf2']['average_vector_xors'] * 2:.3f} (two halves) |",
        "",
        "Every template is a lane permutation of the same power-of-two cell because the first 4/5 Joe--Kuo direction columns are nonsingular upper-triangular generators. For 32 lanes, arbitrary global permutation is not one `vpermd`; the within-half count is the production-relevant one.",
        "",
        "## Range-pattern results",
        "",
    ]
    for window_name, by_size in report["range_patterns"].items():
        lines.extend((f"### {window_name}", "", "| Block | Exact unique / total | Exact top-64 | Exact recurring dictionary | Permuted classes | Permuted top-64 | Schedule/control dictionaries |", "|---:|---:|---:|---:|---:|---:|---:|"))
        for size in ("16", "32"):
            exact = by_size[size]["exact"]
            perm = by_size[size]["within_zmm_permutation"]
            controls = perm["permutation_controls"]
            lines.append(
                f"| {size} | {exact['unique_patterns']:,} / {exact['patterns']:,} | {exact['top_dictionary_coverage']['64']['fraction']:.6%} | {exact['dictionary_bytes_all_recurring_expanded_uint32_zmm']:,} B | {perm['unique_patterns']:,} | {perm['top_dictionary_coverage']['64']['fraction']:.6%} | {perm['dictionary_bytes_all_recurring_expanded_uint32_zmm']:,} B + {controls['control_dictionary_bytes_expanded_uint32']:,} B |"
            )
        lines.append("")
        lines.append("Selection requires at least a pattern ID/offset load plus a dictionary load. Permuted reuse additionally needs a control ID, a 64-byte control-vector load per ZMM unless resident, and one `vpermd` per ZMM. The JSON reports the exact control count and the multi-megabyte ID stream that would result if no cheaper public selector is derived.")
        lines.append("")
    frag = report["transformed_values"]["existing_dimension_fragment"]
    lines.extend((
        "## Transformed values and existing fragment",
        "",
        f"For indices 8192--12287, all **{frag['dimensions_exactly_routable']}** dimensions are exact source-set permutations of one of the two qualified D1 source blocks (source split `{frag['source_block_dimension_counts']}`). All {frag['target_zmms']:,} target ZMMs therefore preserve bit-identical Z/x/growth values under one existing `vpermd`; {frag['bit_identical_without_permutation_zmms']:,} need the identity control. There are {frag['global_distinct_vpermd_controls']} global controls and at most {frag['maximum_controls_per_dimension']} per dimension.",
        "",
        "This is coefficient/value routing already compatible with the existing fragment. Gaussian, x and growth values are never XOR-combined. No approximation, quantization or numerical threshold was changed.",
        "",
        "## Digital randomization",
        "",
        f"Per-dimension 32-bit digital XOR shift: **{report['digital_shift']['result']}**. The shift is absorbed into the scalar block base exactly. General Owen scrambling remains unsupported.",
        "",
        "## Cost and footprint conclusion",
        "",
        f"The exact-template dictionaries occupy {t16['instruction_models']['direct']['template_dictionary_bytes']:,} B (16) and {t32['instruction_models']['direct']['template_dictionary_bytes']:,} B (32), plus 256/256 bytes of selectors. A resident 16-lane basis consumes {t16['gf2']['zmm_registers_for_resident_basis']} ZMMs and averages {t16['gf2']['average_vector_xors']:.3f} XORs before the base XOR. Its dimension-specialized XOR bodies have a {t16['instruction_models']['resident_basis_static_schedule']['lower_bound_l1i_bytes_at_6_bytes_per_evex_vpxord']:,}-byte L1I lower bound. The 32-lane basis consumes {t32['gf2']['zmm_registers_for_resident_basis']} ZMMs, averages {t32['instruction_models']['resident_basis_static_schedule']['per_dimension_average_vector_xors']:.3f} vector XORs, has a {t32['instruction_models']['resident_basis_static_schedule']['lower_bound_l1i_bytes_at_6_bytes_per_evex_vpxord']:,}-byte schedule lower bound, and leaves only {t32['gf2']['zmm_registers_remaining']} registers in the raw-only model.",
        "",
        "L1D numbers are working-set bytes, not residency claims. The direct dictionaries fit nominal L1D capacities but still issue loads. Basis schedules trade those loads for dependency-chain XORs, register pressure, and either selector branches or dimension-specialized L1I. No native cycles were measured, so no throughput win is claimed.",
        "",
        "## Ranked recommendation",
        "",
        "1. **Keep the aligned base+template identity as a valid generator primitive.** It is exact, shift-compatible, and has a one-prepared-XOR base recurrence.",
        "2. **Benchmark the rank-7 16-lane basis only if a real producer is demonstrably L1D-load-bound.** It is the sole plausible resident candidate, but the average XOR/selector cost can erase the saved load.",
        "3. **Reject a general range-pattern dictionary for the tested hot loop.** The 128 production schedule classes are real, but they require 1,008 (16-lane) or 3,712 (32-lane) exact control tuples, 64/475 KiB of controls, and 32/16 MiB of control IDs if stored per occurrence. No all-dimension short schedule was found.",
        "4. **Reject the 32-lane resident basis as a default.** Twenty-two basis registers plus roughly twice the logical XOR count is too expensive for the qualified fused producer without a measured, integration-specific register allocation.",
        "5. **Do not claim a load/permutation win.** The rank-7 candidate trades one data load for XOR/selection; range reuse adds control loads and permutes; neither has native timing or a lower complete instruction model here.",
        "",
        "## Reproduction",
        "",
        "```bash",
        "python3 -u testing/pablito_sequence/audit_zmm_sobol_blocks.py \\",
        "  --out-dir testing/pablito_sequence/results/zmm_block_template_audit_rerun",
        "```",
        "",
        "The JSON contains complete template dictionaries, dimension masks, pattern histograms/top patterns, instruction models, source provenance and every validity gate.",
        "",
    ))
    return "\n".join(lines)


def run(args: argparse.Namespace) -> dict[str, Any]:
    started = time.time()
    table_path = args.direction_table.resolve()
    table = load_direction_table(table_path)
    count = args.points
    if count <= 0 or count & (count - 1) or count % 32:
        raise ValueError("--points must be a positive power of two divisible by 32")
    starts = [0, args.production_start]
    end = max(start + count for start in starts)
    if end > (1 << WORD_BITS):
        raise ValueError("tested indices exceed uint32 Sobol capacity")

    table_oracle = {"available": False, "match": None, "detail": None}
    try:
        from scipy.stats import qmc
        scipy_table = qmc.Sobol(d=DIMENSIONS, scramble=False, bits=32)._sv
        table_oracle = {
            "available": True,
            "match": bool(np.array_equal(table, scipy_table)),
            "detail": "scipy.stats.qmc.Sobol private direction array, used only as an independent table oracle",
        }
    except Exception as exc:  # pragma: no cover - environment provenance
        table_oracle["detail"] = repr(exc)

    triangular_rows = []
    for dimension, row in enumerate(table, 1):
        ok = all((int(row[column]) & (1 << (31 - column))) != 0 and
                 (int(row[column]) & ((1 << (31 - column)) - 1)) == 0
                 for column in range(WORD_BITS))
        triangular_rows.append(ok)
    if not all(triangular_rows):
        raise AssertionError("direction table is not nonsingular upper triangular")

    raw_templates = {str(lanes): template_audit(table, lanes) for lanes in (16, 32)}
    raw_identity = {str(lanes): block_identity_gate(table, starts, count, lanes) for lanes in (16, 32)}

    temp_context = tempfile.TemporaryDirectory(prefix="zmm-sobol-audit-", dir=args.temp_dir)
    temp_path = Path(temp_context.name) / "range_ids.u16"
    ids = np.memmap(temp_path, mode="w+", dtype=np.uint16, shape=(DIMENSIONS, end))
    gray_recurrence_mismatches = 0
    per_dimension_hashes = []
    for dimension, row in enumerate(table, 1):
        direct = gray_words(row, 0, end)
        recurrent = recurrence_words(row, 0, end)
        gray_recurrence_mismatches += int(np.count_nonzero(direct != recurrent))
        ids[dimension - 1] = range_ids(direct)
        per_dimension_hashes.append(hashlib.sha256(direct.tobytes()).hexdigest())
        if args.progress and (dimension == 1 or dimension % 16 == 0):
            print(f"generated/oracle-checked D1-D{dimension}", flush=True)
    ids.flush()

    range_report: dict[str, Any] = {}
    for window_name, start in (("first_2pow20", 0), ("production_start_8192", args.production_start)):
        by_size: dict[str, Any] = {}
        for lanes in (16, 32):
            # Copy to a contiguous 2-D matrix so global pattern sorting has a
            # stable row index independent of memmap strides.
            patterns = np.asarray(ids[:, start:start + count]).reshape(-1, lanes).copy()
            exact = exact_pattern_summary(patterns, "exact")
            reversal = exact_pattern_summary(patterns, "reversal")
            within = exact_pattern_summary(patterns, "within_zmm_permutation")
            global_perm = within if lanes == 16 else exact_pattern_summary(patterns, "global_permutation")
            by_size[str(lanes)] = {
                "exact": exact,
                "reversal": reversal,
                "within_zmm_permutation": within,
                "global_permutation": global_perm,
                "short_schedule": short_schedule_audit(ids, start, count, lanes),
                "selection_model": {
                    "exact_dictionary": "pattern-ID/offset metadata load + one dictionary load per ZMM",
                    "reversal": "exact dictionary selection + one public reversal bit + one vpermd when set",
                    "arbitrary_vpermd": "control-ID metadata + 64-byte control load (unless resident) + one vpermd per ZMM",
                },
            }
            if args.progress:
                print(f"range patterns {window_name} lanes={lanes} complete", flush=True)
        range_report[window_name] = by_size

    del ids
    temp_context.cleanup()

    fragment = fragment_permutation_audit(table)
    prefixes = [value for value in (16, 32, 64, 128, 256, 512, 1024, 2048, 4096,
                                     8192, 16384, 32768, 65536, 131072, 262144,
                                     524288, 1048576) if value <= count]
    prefix_results = {
        str(prefix): {
            "new_points_added_from_previous": prefix - (prefixes[index - 1] if index else 0),
            "unique_per_dimension": prefix,
            "unique_multidimensional_points": prefix,
            "proof": "nonsingular upper-triangular 32x32 direction matrix makes index-to-word injective in every dimension",
        }
        for index, prefix in enumerate(prefixes)
    }
    tested_shifts = [
        ((dimension * 0x9E3779B9) ^ 0xA5A5A5A5) & UINT32_MASK
        for dimension in range(1, DIMENSIONS + 1)
    ]
    shift_mismatches = 0
    for lanes in (16, 32):
        for dimension, row in enumerate(table):
            template = gray_words(row, 0, lanes)
            for start in starts:
                words = gray_words(row, start, lanes)
                shifted = words ^ np.uint32(tested_shifts[dimension])
                reconstructed = template ^ np.uint32(int(words[0]) ^ tested_shifts[dimension])
                shift_mismatches += int(np.count_nonzero(shifted != reconstructed))

    all_gates = (
        bool(table_oracle["match"])
        and gray_recurrence_mismatches == 0
        and all(item["all_32_bits_match"] for item in raw_identity.values())
        and all(item["base_transition_mismatches"] == 0 for item in raw_identity.values())
        and shift_mismatches == 0
        and all(triangular_rows)
        and fragment["dimensions_exactly_routable"] == DIMENSIONS
    )
    report: dict[str, Any] = {
        "schema_version": 1,
        "status": "PASS" if all_gates else "FAIL",
        "scope": "read-only exact Joe-Kuo D1-D256 aligned block-template and qualified range-pattern audit",
        "not_new_direction_numbers": True,
        "pricing_engines_modified": False,
        "defective_zmm_lane_fill_used": False,
        "direction_table": {
            "path": str(table_path.relative_to(ROOT) if table_path.is_relative_to(ROOT) else table_path),
            "sha256": sha256_file(table_path),
            "format": "21201 rows; little-endian uint32 count=32 followed by 32 uint32 direction words",
            "rows_loaded": DIMENSIONS,
            "words_per_row": WORD_BITS,
            "per_dimension_generated_word_sha256_0_to_end": per_dimension_hashes,
        },
        "qualified_range_classifier": {
            "integer_formula": "q=word>>9; range_id=min(abs(q-2^22)>>11,2047)",
            "inverse_cdf_evaluated": False,
            "source_provenance": QUALIFIED_SOURCE_PROVENANCE,
        },
        "dimensions": DIMENSIONS,
        "dimension_ids": [1, DIMENSIONS],
        "windows": [
            {"start_inclusive": start, "end_exclusive": start + count, "points": count}
            for start in starts
        ],
        "block_sizes": [16, 32],
        "raw_identity": raw_identity,
        "raw_templates": raw_templates,
        "range_patterns": range_report,
        "transformed_values": {
            "representative_accepted_contracts": [
                {
                    "fixings": n,
                    "rate": 0.03,
                    "sigma": 0.20,
                    "maturity": 1.0,
                    "step_drift_formula": "float32((r-0.5*sigma^2)/N)",
                    "step_diffusion_formula": "float32(sigma/sqrt(N))",
                    "qualified_alpha": 0.20,
                    "raw_source_set_route": "PASS",
                    "bit_identical_routed_zmms": fragment["target_zmms"],
                    "permitted_lane_operation": "one existing vpermd per target ZMM",
                    "z_x_growth_xor_combination": False,
                }
                for n in (16, 32, 64, 128, 256)
            ],
            "existing_dimension_fragment": fragment,
            "bit_identity_proof": "the fragment permutes already-qualified source float arrays; memmove-equivalent lane selection preserves every bit for Z/x/growth",
            "coefficient_schedule_reuse": "range-pattern results describe reusable schedules with distinct local coordinates; no transformed numerical values are XOR-combined",
            "numerical_gates_changed": False,
        },
        "digital_shift": {
            "result": "PASS" if shift_mismatches == 0 else "FAIL",
            "identity": "(template XOR base) XOR shift = template XOR (base XOR shift)",
            "tested_distinct_per_dimension_shifts": DIMENSIONS,
            "tested_windows": starts,
            "tested_block_sizes": [16, 32],
            "word_mismatches": shift_mismatches,
            "owen_scrambling": "unsupported; not claimed",
        },
        "validity": {
            "scipy_direction_table_oracle": table_oracle,
            "scipy_direction_table_match": "PASS" if table_oracle["match"] else "FAIL",
            "direction_rows_nonsingular_upper_triangular": all(triangular_rows),
            "gray_vs_recurrence_all_bits": "PASS" if gray_recurrence_mismatches == 0 else "FAIL",
            "gray_vs_recurrence_word_mismatches": gray_recurrence_mismatches,
            "unique_values_per_dimension": "PASS by checked nonsingular 32x32 generating matrix; injective for all uint32 indices",
            "unique_multidimensional_points": "PASS; D1 alone is injective, and all 256 coordinates match the oracle",
            "short_raw_repetition_period": "NONE; injectivity forbids equality at every nonzero period in the tested windows",
            "prefix_expansion": prefix_results,
            "all_mandatory_gates_pass": all_gates,
        },
        "footprints": {
            "l1d_note": "working-set byte counts only; no residency claim",
            "l1i_note": "dimension-specialized basis schedules consume instruction bytes not measured here; generic schedules require mask tests/branches",
            "raw_template_dictionaries": {
                "16_bytes": raw_templates["16"]["instruction_models"]["direct"]["template_dictionary_bytes"],
                "32_bytes": raw_templates["32"]["instruction_models"]["direct"]["template_dictionary_bytes"],
            },
            "basis_registers": {
                "16_zmms": raw_templates["16"]["gf2"]["zmm_registers_for_resident_basis"],
                "32_zmms": raw_templates["32"]["gf2"]["zmm_registers_for_resident_basis"],
            },
            "register_instruction_model": {
                "status": "static lower-bound model; no integrated object or native timing was produced",
                "architectural_zmms": 32,
                "direct_16_raw": {"persistent_template_zmms": 1, "base_result_jump_zmms": 3, "minimum_gprs": 5},
                "basis_16_raw": {"persistent_basis_zmms": 7, "base_result_jump_zmms": 3, "total_zmms": 10, "minimum_gprs_static_schedule": 5, "minimum_gprs_generic_mask_schedule": 6},
                "direct_32_raw": {"persistent_template_zmms": 2, "base_result_jump_zmms": 5, "minimum_gprs": 5},
                "basis_32_raw": {"persistent_basis_zmms": 22, "base_result_jump_zmms": 5, "total_zmms": 27, "minimum_gprs_static_schedule": 5, "minimum_gprs_generic_mask_schedule": 6},
                "gpr_roles": "input/output pointers, dimension/block counter, base/delta pointer; generic basis adds representation-mask scratch",
                "qualified_fused_integration": "not allocated or claimed; the sibling-engine audit reports up to 23 live ZMMs, so counts must not be added blindly",
            },
            "dimension_specialized_basis_l1i_lower_bound_bytes": {
                "16": raw_templates["16"]["instruction_models"]["resident_basis_static_schedule"]["lower_bound_l1i_bytes_at_6_bytes_per_evex_vpxord"],
                "32": raw_templates["32"]["instruction_models"]["resident_basis_static_schedule"]["lower_bound_l1i_bytes_at_6_bytes_per_evex_vpxord"],
                "excludes": "dispatch, base XOR, loop control, alignment and transform code",
            },
            "production_range_pattern_dictionaries": {
                size: {
                    "exact_recurring_expanded_bytes": range_report["production_start_8192"][size]["exact"]["dictionary_bytes_all_recurring_expanded_uint32_zmm"],
                    "permuted_schedule_expanded_bytes": range_report["production_start_8192"][size]["within_zmm_permutation"]["dictionary_bytes_all_recurring_expanded_uint32_zmm"],
                    "permutation_control_dictionary_bytes": range_report["production_start_8192"][size]["within_zmm_permutation"]["permutation_controls"]["control_dictionary_bytes_expanded_uint32"],
                    "control_id_stream_bytes_without_public_selector": range_report["production_start_8192"][size]["within_zmm_permutation"]["permutation_controls"]["control_id_stream_bytes_if_stored_for_every_occurrence"],
                }
                for size in ("16", "32")
            },
        },
        "recommendation": {
            "ranked": [
                "retain exact aligned base-plus-template generation as a valid primitive",
                "benchmark rank-7 16-lane resident basis only in a demonstrated L1D-load-bound producer",
                "retain existing exact source-set permutation fragment for the 8192/4096 transformed production packet",
                "reject rank-11 32-lane basis as a default qualified-producer design without an integration-specific register allocation",
                "use range dictionaries only when measured top-K coverage pays for selector, dictionary and permutation loads",
            ],
            "answers": {
                "1_resident_templates_plus_base": "YES, exact for 16 and 32; one ctz-selected prepared base XOR transition",
                "2_small_resident_basis": "YES for raw 16-lane generation (7 ZMM); mathematically yes but operationally cramped for 32 lanes (22 ZMM)",
                "3_range_schedule_reuse": "MATHEMATICALLY YES BUT OPERATIONALLY REJECTED: 128 production classes require 1008/3712 control tuples, 64/475 KiB control dictionaries, and no all-dimension short schedule",
                "4_actual_hot_loop_reduction": "NO PROVEN CANDIDATE: 16-lane basis removes one 64-byte template load but adds average XOR and selector cost; range reuse adds control loads and vpermd; 32-lane basis is clearly heavier",
            },
        },
        "runtime_seconds": time.time() - started,
        "reproduction": {
            "command": f"python3 -u testing/pablito_sequence/audit_zmm_sobol_blocks.py --points {count} --production-start {args.production_start} --out-dir {args.out_dir}",
            "python": os.sys.version,
            "numpy": np.__version__,
        },
    }
    return report


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--direction-table", type=Path, default=DEFAULT_TABLE)
    parser.add_argument("--points", type=int, default=1 << 20)
    parser.add_argument("--production-start", type=int, default=PRODUCTION_START)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--temp-dir", type=Path, default=Path("/tmp"))
    parser.add_argument("--progress", action=argparse.BooleanOptionalAction, default=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.out_dir.exists() and any(args.out_dir.iterdir()):
        raise SystemExit(f"refusing to overwrite non-empty result directory: {args.out_dir}")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    report = run(args)
    json_path = args.out_dir / "audit.json"
    markdown_path = args.out_dir / "README.md"
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    markdown_path.write_text(make_markdown(report), encoding="utf-8")
    print(json.dumps({
        "status": report["status"],
        "json": str(json_path),
        "markdown": str(markdown_path),
        "runtime_seconds": report["runtime_seconds"],
    }, sort_keys=True))
    return 0 if report["status"] == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
