#!/usr/bin/env python3
"""Final-linked structural audit for the fixed-block source experiment."""

import json
import re
import subprocess
import sys
import os
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_dynamic_mix import function_counts

BINARY = Path("bench_asian_genuine_fixed_block_source")
OUTPUT = Path("results/asian_genuine_fixed_block_source/linked_structural_audit.json")
SUMMARY = Path("results/asian_genuine_fixed_block_source/linked_structural_audit_summary.txt")
SYMBOLS = {
    "ordered_d1_x_only_diag": (Path("/tmp/asian-fixed-source-x3.mix"), 65, 10),
    "asian_genuine_fixed_block_signed_z_one_fma_source_diag":
        (Path("/tmp/asian-fixed-source-onefma.mix"), 64, 4),
    "asian_genuine_fixed_block_prepared_exact_x_lookup_diag":
        (Path("/tmp/asian-fixed-source-exact.mix"), 64, 2),
}

AUDIT_LEAVES = {
    "ordered_d1_x_only_diag": "x3",
    "asian_genuine_fixed_block_signed_z_one_fma_source_diag": "fixed",
    "asian_genuine_fixed_block_prepared_exact_x_lookup_diag": "exact",
}


def command(*args):
    return subprocess.check_output(args, text=True)


def generate_mix_files():
    environment = os.environ.copy()
    environment.update({"MKL_THREADING_LAYER": "SEQUENTIAL",
                        "MKL_NUM_THREADS": "1", "MKL_DYNAMIC": "FALSE"})
    for symbol, (mix_path, _, _) in SYMBOLS.items():
        mix_path.unlink(missing_ok=True)
        subprocess.run(
            ["/opt/intel-sde/sde64", "-skx", "-omix", str(mix_path), "--",
             str(BINARY.resolve()), "--audit-leaf", AUDIT_LEAVES[symbol]],
            check=True, env=environment, stdout=subprocess.DEVNULL)


def disassembly():
    text = command("objdump", "-d", "-M", "intel", str(BINARY))
    result = {symbol: [] for symbol in SYMBOLS}
    current = None
    for line in text.splitlines():
        header = re.match(r"^\s*[0-9a-f]+\s+<([^>]+)>:$", line)
        if header:
            current = header.group(1) if header.group(1) in result else None
            continue
        if current is None:
            continue
        match = re.match(
            r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+)+\s*([a-z0-9]+)\s*(.*)$",
            line,
        )
        if match:
            result[current].append({"address": match.group(1),
                                    "mnemonic": match.group(2),
                                    "operands": match.group(3).strip()})
            if match.group(2) == "ret":
                current = None
    return result


def sizes():
    result = {}
    for line in command("nm", "-S", "--defined-only", str(BINARY)).splitlines():
        match = re.match(r"^[0-9a-f]+\s+([0-9a-f]+)\s+\S\s+(\S+)$", line)
        if match and match.group(2) in SYMBOLS:
            result[match.group(2)] = int(match.group(1), 16)
    return result


def families(text, stem):
    values = set()
    for kind, number in re.findall(r"\b(zmm|ymm|xmm|k)(\d+)\b", text):
        if stem == "zmm" and kind in ("zmm", "ymm", "xmm"):
            values.add(int(number))
        elif stem == "k" and kind == "k":
            values.add(int(number))
    return sorted(values)


def gpr_families(text):
    aliases = {
        "eax": "rax", "ax": "rax", "al": "rax", "ebx": "rbx", "bx": "rbx",
        "bl": "rbx", "ecx": "rcx", "cx": "rcx", "cl": "rcx", "edx": "rdx",
        "dx": "rdx", "dl": "rdx", "esi": "rsi", "si": "rsi", "sil": "rsi",
        "edi": "rdi", "di": "rdi", "dil": "rdi", "ebp": "rbp", "bp": "rbp",
        "bpl": "rbp", "esp": "rsp", "sp": "rsp", "spl": "rsp",
    }
    found = set()
    pattern = r"\b(?:r(?:ax|bx|cx|dx|si|di|bp|sp|1[0-5]|[8-9])(?:d|w|b)?|e(?:ax|bx|cx|dx|si|di|bp|sp)|(?:[abcd][xl]|[sb]pl|[sd]il))\b"
    for name in re.findall(pattern, text):
        match = re.match(r"^(r(?:1[0-5]|[89]))[dwb]$", name)
        name = match.group(1) if match else aliases.get(name, name)
        found.add(name)
    return sorted(found)


def memory_width(operands):
    widths = {"ZMMWORD": 64, "YMMWORD": 32, "XMMWORD": 16,
              "QWORD": 8, "DWORD": 4, "WORD": 2, "BYTE": 1}
    match = re.search(r"\b(ZMMWORD|YMMWORD|XMMWORD|QWORD|DWORD|WORD|BYTE) PTR\b",
                      operands)
    return widths[match.group(1)] if match else 0


def static_classes(items):
    classes = Counter()
    for item in items:
        mnemonic, operands = item["mnemonic"], item["operands"]
        parts = [part.strip() for part in operands.split(",")]
        width = memory_width(operands)
        if parts and "[" in parts[0]:
            classes[f"stores_{width}_bytes"] += 1
        elif any("[" in part for part in parts[1:]):
            classes[f"loads_{width}_bytes"] += 1
        if mnemonic.startswith("vfmadd"):
            classes["fmas"] += 1
        if mnemonic.startswith("j"):
            classes["branches"] += 1
        if mnemonic.startswith("vperm"):
            classes["permutes"] += 1
        if mnemonic.startswith(("vpsub", "vpabs", "vpand", "vpsll", "vpord", "vpxor")):
            classes["integer_folding_or_selector"] += 1
        if mnemonic.startswith("vbroadcast") or mnemonic.startswith("vpbroadcast"):
            classes["broadcasts"] += 1
    return dict(sorted(classes.items()))


def backward_liveness(items):
    live = set()
    rows = []
    for item in reversed(items):
        operands = [part.strip() for part in item["operands"].split(",")]
        registers = set(families(item["operands"], "zmm"))
        definitions = set()
        uses = set(registers)
        if operands:
            destination = set(families(operands[0], "zmm"))
            if destination and "[" not in operands[0]:
                definitions |= destination
                if "fma" not in item["mnemonic"]:
                    uses -= destination
        live -= definitions
        live |= uses
        rows.append([item["address"],
                     f"0x{sum(1 << register for register in live):08x}",
                     len(live)])
    return list(reversed(rows))


def purpose(item):
    mnemonic = item["mnemonic"]
    if mnemonic.startswith("vfmadd"):
        return "affine FMA or X3 polynomial/hard correction"
    if mnemonic.startswith("vmov") and "[" in item["operands"]:
        return "payload/coefficient load or x store"
    if mnemonic.startswith("vp") or mnemonic.startswith("vsub"):
        return "X3 folding and selector construction"
    if mnemonic.startswith("j"):
        return "fixed-count or X3 hard-correction loop control"
    if mnemonic in ("ret", "vzeroupper"):
        return "leaf epilogue"
    if mnemonic.startswith("vbroadcast"):
        return "prepared scalar broadcast"
    return "pointer, counter, or X3 address control"


def audit_symbol(symbol, items, linked_size):
    mix_path, invocations, true_peak = SYMBOLS[symbol]
    dynamic = function_counts(mix_path, symbol)
    assembly = " ".join(f"{item['mnemonic']} {item['operands']}" for item in items)
    mnemonics = Counter(item["mnemonic"] for item in items)
    liveness = backward_liveness(items)
    calls = sum(value for name, value in mnemonics.items() if name.startswith("call"))
    stack = len(re.findall(r"\b(?:rsp|rbp|esp|ebp)\b", assembly))
    gathers = sum(value for name, value in mnemonics.items() if "gather" in name)
    scatters = sum(value for name, value in mnemonics.items() if "scatter" in name)
    legend = sorted({purpose(item) for item in items})
    report = {
        "symbol": symbol,
        "linked_text_bytes": linked_size,
        "static_instruction_count": len(items),
        "dynamic_audit_invocations": invocations,
        "dynamic_instruction_total": dynamic["*total"],
        "dynamic_instructions_per_invocation": dynamic["*total"] // invocations,
        "dynamic_counts_total": dynamic,
        "mnemonic_counts_static": dict(sorted(mnemonics.items())),
        "static_instruction_classes": static_classes(items),
        "zmm_families": families(assembly, "zmm"),
        "gpr_families": gpr_families(assembly),
        "k_masks": families(assembly, "k"),
        "backward_liveness_parser_peak": max((row[2] for row in liveness), default=0),
        "true_simultaneous_peak_zmm_families": true_peak,
        "backward_liveness_columns": ["address", "live_zmm_mask_before", "count"],
        "backward_liveness": liveness,
        "forbidden": {"calls": calls, "stack_references": stack,
                      "gathers": gathers, "scatters": scatters},
        "instruction_columns": ["address", "mnemonic", "operands", "purpose"],
        "instructions": [[item["address"], item["mnemonic"], item["operands"],
                          purpose(item)] for item in items],
        "purpose_legend": legend,
    }
    report["dynamic_classes_per_invocation"] = {
        "instructions": dynamic["*total"] // invocations,
        "loads_4_bytes": dynamic.get("*mem-read-4", 0) // invocations,
        "loads_8_bytes": dynamic.get("*mem-read-8", 0) // invocations,
        "loads_64_bytes": dynamic.get("*mem-read-64", 0) // invocations,
        "stores_4_bytes": dynamic.get("*mem-write-4", 0) // invocations,
        "stores_64_bytes": dynamic.get("*mem-write-64", 0) // invocations,
        "conditional_branches": dynamic.get("*category-COND_BR", 0) // invocations,
        "unconditional_branches": dynamic.get("*category-UNCOND_BR", 0) // invocations,
        "vector_fmas": sum(value for name, value in dynamic.items()
                           if name.startswith("VFMADD") and name.endswith("PS")) // invocations,
        "scalar_fmas": sum(value for name, value in dynamic.items()
                           if name.startswith("VFMADD") and name.endswith("SS")) // invocations,
        "permutes": sum(value for name, value in dynamic.items()
                        if name.startswith("VPERM")) // invocations,
    }
    return report


def main():
    generate_mix_files()
    found = disassembly()
    linked_sizes = sizes()
    if any(not found[symbol] for symbol in SYMBOLS):
        raise SystemExit("missing final-linked symbol")
    reports = [audit_symbol(symbol, found[symbol], linked_sizes[symbol])
               for symbol in SYMBOLS]
    by_name = {report["symbol"]: report for report in reports}
    x3 = by_name["ordered_d1_x_only_diag"]
    fixed = by_name["asian_genuine_fixed_block_signed_z_one_fma_source_diag"]
    exact = by_name["asian_genuine_fixed_block_prepared_exact_x_lookup_diag"]
    xd, fd, ed = (report["dynamic_counts_total"] for report in (x3, fixed, exact))
    x3_calls, fixed_calls, exact_calls = (SYMBOLS[name][1] for name in SYMBOLS)
    per = lambda counts, key, calls: counts.get(key, 0) // calls
    gates = {
        "one_fma_exact_dynamic_total": fixed["dynamic_instructions_per_invocation"] == 2566,
        "exact_ceiling_dynamic_total": exact["dynamic_instructions_per_invocation"] == 2052,
        "one_fma_two_vector_fmas_per_32": per(fd, "VFMADD132PS", fixed_calls) == 512,
        "one_fma_two_z_loads_per_32": per(fd, "*mem-read-64", fixed_calls) == 512,
        "one_fma_two_x_stores_per_32": per(fd, "*mem-write-64", fixed_calls) == 512,
        "one_fma_no_scalar_hard_fma": per(fd, "VFMADD213SS", fixed_calls) == 0,
        "one_fma_no_permute": per(fd, "VPERMPS", fixed_calls) == 0,
        "exact_no_fma": per(ed, "VFMADD132PS", exact_calls) == 0,
        "no_forbidden_new_leaf_instruction": not any(
            fixed["forbidden"].values()) and not any(exact["forbidden"].values()),
    }
    x3_total = x3["dynamic_instructions_per_invocation"]
    fixed_total = fixed["dynamic_instructions_per_invocation"]
    x3_fmas = (per(xd, "VFMADD213PS", x3_calls) +
               per(xd, "VFMADD213SS", x3_calls))
    fixed_fmas = per(fd, "VFMADD132PS", fixed_calls)
    output = {
        "status": "PASS" if all(gates.values()) else "FAIL",
        "scope": "final linked executable symbols",
        "binary": str(BINARY),
        "gates": gates,
        "symbols": reports,
        "dynamic_differences_per_invocation": {
            "removed_instructions_vs_x3": x3_total - fixed_total,
            "removed_vector_fmas_vs_x3": per(xd, "VFMADD213PS", x3_calls) - fixed_fmas,
            "removed_scalar_hard_tail_fmas_vs_x3": per(xd, "VFMADD213SS", x3_calls),
            "removed_total_fmas_vs_x3": x3_fmas - fixed_fmas,
            "x3_64_byte_reads": per(xd, "*mem-read-64", x3_calls),
            "prepared_fixed_block_64_byte_reads": per(fd, "*mem-read-64", fixed_calls),
            "removed_64_byte_reads": (per(xd, "*mem-read-64", x3_calls) -
                                      per(fd, "*mem-read-64", fixed_calls)),
            "x3_recurring_coefficient_read_bytes": 256 * 8 * 64,
            "added_signed_z_read_bytes": 256 * 2 * 64,
            "net_removed_recurring_wide_read_bytes": 256 * 6 * 64,
            "removed_folding_selection_instructions": {
                name: per(xd, name, x3_calls) - per(fd, name, fixed_calls)
                for name in ("VPSUBD", "VPABSD", "VPANDD", "VPSLLD", "VPORD",
                             "VSUBPS", "VPERMPS")
            },
            "dependency_depth": "three-term X3 Horner plus hard pass replaced by one affine FMA",
        },
        "sapphire_rapids_port_model": {
            "prepared_fixed_block_source_consumption": (
                "per 32 values: two aligned 64-byte loads, two independent vector "
                "FMAs, and two aligned 64-byte stores; expected load/store bandwidth "
                "and 64-KiB combined table/output footprint dominate"),
            "qualified_x3": (
                "selector/folding front end, eight 64-byte coefficient reads per packet, "
                "six vector Horner FMAs, and a scalar 64-position hard pass"),
            "exact_ceiling": (
                "per 32 values: two aligned loads and two aligned stores; unranked copy ceiling"),
            "residency": "32-KiB table plus 32-KiB x output exceeds nominal 48-KiB L1D; no L1 residency claim",
        },
        "working_sets": {
            "signed_z_table_bytes": 32768,
            "x_output_bytes": 32768,
            "source_combined_bytes": 65536,
            "x_plus_growth_payload_bytes": 65536,
        },
        "terminology": {
            "ranked": "prepared fixed-block source consumption",
            "baseline": "general generated-source baseline",
            "ceiling": "prepared_exact_x_lookup_ceiling (unranked)",
        },
    }
    OUTPUT.write_text(json.dumps(output, separators=(",", ":")) + "\n")
    lines = [f"status={output['status']} scope=final_linked_executable"]
    for report in reports:
        lines.append(
            f"{report['symbol']} static={report['static_instruction_count']} "
            f"dynamic_per_call={report['dynamic_instructions_per_invocation']} "
            f"text_bytes={report['linked_text_bytes']} "
            f"true_peak_zmm={report['true_simultaneous_peak_zmm_families']} "
            f"calls={report['forbidden']['calls']} stack={report['forbidden']['stack_references']} "
            f"gathers={report['forbidden']['gathers']} scatters={report['forbidden']['scatters']}")
    lines.append(f"removed_instructions_vs_x3={x3_total-fixed_total} "
                 f"removed_vector_fmas_vs_x3={per(xd, 'VFMADD213PS', x3_calls)-fixed_fmas} "
                 f"removed_scalar_hard_tail_fmas_vs_x3={per(xd, 'VFMADD213SS', x3_calls)}")
    SUMMARY.write_text("\n".join(lines) + "\n")
    if not all(gates.values()):
        raise SystemExit("structural gate failed")


if __name__ == "__main__":
    main()
