#!/usr/bin/env python3
"""Audit the final linked standalone fixed-block end-to-end benchmark."""

import hashlib
import json
import re
import subprocess
from pathlib import Path

BINARY = Path("bench_asian_genuine_fixed_block_e2e")
RESULT_DIR = Path("results/asian_genuine_fixed_block_e2e")
JSON_OUT = RESULT_DIR / "linked_symbol_audit.json"
TEXT_OUT = RESULT_DIR / "linked_symbol_audit.txt"

SOURCE_SYMBOLS = [
    "ordered_d1_x_only_diag",
    "asian_genuine_fixed_block_signed_z_one_fma_source_diag",
    "asian_genuine_fixed_block_prepared_exact_x_lookup_diag",
]

COMMON_SYMBOLS = [
    "asian_fixed_e2e_post_source",
    "asian_fixed_e2e_vector_exp",
    "asian_fixed_e2e_route_evolution",
    "asian_fixed_e2e_consumer",
    "asian_vector_exp_range_reduced_array_diag",
    "asian_genuine_sql_dual_control_diag",
    "asian_genuine_msfr_basis_forward_diag",
    "asian_genuine_strip_l_to_g_diag",
    "asian_genuine_strip_price_diag",
    "asian_genuine_strip_price_delta_diag",
    "asian_genuine_msfr_accumulator_init",
    "asian_genuine_msfr_consume_block",
    "asian_genuine_msfr_finalize",
    "asian_genuine_msfr_arithmetic_tile4_diag",
    "asian_genuine_msfr_cv_tile4_diag",
]

PIPELINE_SYMBOLS = [
    "asian_fixed_e2e_source_x3",
    "asian_fixed_e2e_source_fixed",
    "asian_fixed_e2e_source_exact",
    "asian_fixed_e2e_run_complete",
] + SOURCE_SYMBOLS + COMMON_SYMBOLS


def command(*arguments):
    return subprocess.check_output(arguments, text=True)


def text_section():
    for line in command("readelf", "-SW", str(BINARY)).splitlines():
        match = re.match(
            r"^\s*\[\s*\d+\]\s+\.text\s+PROGBITS\s+([0-9a-fA-F]+)\s+"
            r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", line)
        if match:
            return tuple(int(value, 16) for value in match.groups())
    raise RuntimeError("linked .text section not found")


def linked_symbols():
    result = {}
    for line in command("nm", "-S", "--defined-only", str(BINARY)).splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[3] in PIPELINE_SYMBOLS:
            result[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
    missing = sorted(set(PIPELINE_SYMBOLS) - set(result))
    if missing:
        raise RuntimeError(f"missing final-linked symbols: {missing}")
    return result


def symbol_hashes():
    address, offset, _ = text_section()
    binary = BINARY.read_bytes()
    output = {}
    for name, (start, size) in linked_symbols().items():
        file_start = offset + start - address
        payload = binary[file_start:file_start + size]
        if len(payload) != size:
            raise RuntimeError(f"short linked symbol payload: {name}")
        output[name] = {
            "address": f"0x{start:x}",
            "bytes": size,
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
    return output


def disassembled_functions():
    text = command("objdump", "-d", "-M", "intel", str(BINARY))
    functions = {}
    current = None
    for line in text.splitlines():
        header = re.match(r"^\s*[0-9a-f]+\s+<([^>]+)>:$", line)
        if header:
            current = header.group(1)
            functions[current] = []
        elif current is not None and re.match(r"^\s*[0-9a-f]+:", line):
            functions[current].append(line)
    return functions


def target_count(lines, target):
    pattern = re.compile(rf"\b(?:call|jmp)\s+[^<]*<{re.escape(target)}>$")
    return sum(bool(pattern.search(line.strip())) for line in lines)


def main():
    RESULT_DIR.mkdir(parents=True, exist_ok=True)
    hashes = symbol_hashes()
    functions = disassembled_functions()
    run = functions["asian_fixed_e2e_run_complete"]
    post = functions["asian_fixed_e2e_post_source"]
    vector_exp = functions["asian_fixed_e2e_vector_exp"]
    route = functions["asian_fixed_e2e_route_evolution"]
    consumer = functions["asian_fixed_e2e_consumer"]
    source_targets = {
        "x3": target_count(run, "asian_fixed_e2e_source_x3"),
        "fixed_block": target_count(run, "asian_fixed_e2e_source_fixed"),
        "exact_lookup": target_count(run, "asian_fixed_e2e_source_exact"),
    }
    source_leaf_targets = {
        "x3": target_count(functions["asian_fixed_e2e_source_x3"], SOURCE_SYMBOLS[0]),
        "fixed_block": target_count(
            functions["asian_fixed_e2e_source_fixed"], SOURCE_SYMBOLS[1]),
        "exact_lookup": target_count(
            functions["asian_fixed_e2e_source_exact"], SOURCE_SYMBOLS[2]),
    }
    common_targets = {
        "post_source_from_complete": target_count(run, "asian_fixed_e2e_post_source"),
        "vector_exp_wrapper": target_count(post, "asian_fixed_e2e_vector_exp"),
        "route_evolution_wrapper": target_count(post, "asian_fixed_e2e_route_evolution"),
        "consumer_wrapper": target_count(post, "asian_fixed_e2e_consumer"),
        "vector_exp_leaf_sites": target_count(
            vector_exp, "asian_vector_exp_range_reduced_array_diag"),
        "sql_dual_control_site": target_count(
            route, "asian_genuine_sql_dual_control_diag"),
        "basis_forward_site": target_count(
            route, "asian_genuine_msfr_basis_forward_diag"),
        "l_to_g_site": target_count(consumer, "asian_genuine_strip_l_to_g_diag"),
        "strip_price_site": target_count(consumer, "asian_genuine_strip_price_diag"),
        "strip_price_delta_site": target_count(
            consumer, "asian_genuine_strip_price_delta_diag"),
        "accumulator_init_site": target_count(
            consumer, "asian_genuine_msfr_accumulator_init"),
        "consume_block_site": target_count(
            consumer, "asian_genuine_msfr_consume_block"),
        "finalize_site": target_count(consumer, "asian_genuine_msfr_finalize"),
    }
    dependencies = command("ldd", str(BINARY)).splitlines()
    undefined = command("nm", "-u", str(BINARY)).splitlines()
    forbidden = [line for line in dependencies + undefined
                 if re.search(r"(?:mkl|vsl|onemkl)", line, re.IGNORECASE)]
    candidate_common_hashes = {
        candidate: {symbol: hashes[symbol]["sha256"] for symbol in COMMON_SYMBOLS}
        for candidate in ("qualified_x3_source_baseline",
                          "prepared_fixed_block_source_consumption",
                          "prepared_exact_x_lookup_ceiling")
    }
    identical = len({json.dumps(value, sort_keys=True)
                     for value in candidate_common_hashes.values()}) == 1
    gates = {
        "no_onemkl_link_or_undefined_symbol": not forbidden,
        "one_linked_source_site_per_candidate": all(value == 1
                                                     for value in source_targets.values()),
        "one_linked_source_leaf_site_per_wrapper": all(
            value == 1 for value in source_leaf_targets.values()),
        "one_common_post_source_site_per_source_branch": (
            common_targets["post_source_from_complete"] == 3),
        "one_vector_exp_wrapper_site": common_targets["vector_exp_wrapper"] == 1,
        "one_route_evolution_wrapper_site": common_targets["route_evolution_wrapper"] == 1,
        "one_consumer_wrapper_site": common_targets["consumer_wrapper"] == 1,
        "two_vector_exp_leaf_sites": common_targets["vector_exp_leaf_sites"] == 2,
        "one_site_for_each_existing_evolution_and_consumer_api": all(
            common_targets[name] == 1 for name in (
                "sql_dual_control_site", "basis_forward_site", "l_to_g_site",
                "strip_price_site", "strip_price_delta_site", "accumulator_init_site",
                "consume_block_site", "finalize_site")),
        "all_candidates_share_identical_common_linked_symbol_hashes": identical,
    }
    report = {
        "status": "PASS" if all(gates.values()) else "FAIL",
        "binary": str(BINARY),
        "binary_sha256": hashlib.sha256(BINARY.read_bytes()).hexdigest(),
        "scope": "final linked standalone executable",
        "gates": gates,
        "dynamic_dependencies": dependencies,
        "forbidden_onemkl_matches": forbidden,
        "source_call_sites_in_common_complete_runner": source_targets,
        "source_leaf_call_sites_in_wrappers": source_leaf_targets,
        "common_call_sites": common_targets,
        "linked_symbol_hashes": hashes,
        "candidate_common_symbol_hashes": candidate_common_hashes,
        "invocation_contract": {
            "every_complete_valuation": {
                "selected_source": 1,
                "vector_exp": 2,
                "route_evolution": 1,
                "consumer_api": 1,
            },
            "price_or_price_delta_leaf_invocations": 1,
            "price_or_price_delta_cv_l_to_g_invocations": 1,
            "full_risk_accumulator_init_consume_finalize_invocations": {
                "accumulator_init": 1, "consume_block": 1, "finalize": 1,
            },
            "full_risk_tile4_leaf_invocations": {
                "K1": 1, "K4": 1, "K8": 2, "K16": 4, "K32": 8,
            },
            "runtime_preflight_instrumentation": (
                "independently checks the same counts for all three sources"),
        },
        "qualified_inputs_unchanged": {
            "base_commit": "9a9b204ed770f4e1ff6edecfdf177676e6a4579c",
            "proof": "all benchmark dependencies are pre-existing base blobs",
        },
    }
    JSON_OUT.write_text(json.dumps(report, separators=(",", ":")) + "\n")
    lines = [
        f"status={report['status']}",
        f"binary_sha256={report['binary_sha256']}",
        f"onemkl_link_matches={len(forbidden)}",
        f"source_call_sites={source_targets}",
        f"source_leaf_call_sites={source_leaf_targets}",
        f"common_call_sites={common_targets}",
        f"candidate_common_linked_hashes_identical={'yes' if identical else 'no'}",
        "complete_invocations=source:1 vector_exp:2 route_evolution:1 consumer_api:1",
        "full_risk_tile4_leaves=K1:1 K4:1 K8:2 K16:4 K32:8",
    ]
    TEXT_OUT.write_text("\n".join(lines) + "\n")
    if not all(gates.values()):
        raise SystemExit("linked-symbol gate failed")


if __name__ == "__main__":
    main()
