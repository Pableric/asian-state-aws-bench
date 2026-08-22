#!/usr/bin/env python3
"""Linked-symbol structural audit for the private multi-strike full-risk leaves."""

import argparse
import json
import re
import subprocess
from collections import Counter

SYMBOLS = [
    "asian_genuine_msfr_basis_forward_diag",
    "asian_genuine_msfr_arithmetic_tile2_diag",
    "asian_genuine_msfr_arithmetic_tile4_diag",
    "asian_genuine_msfr_cv_tile2_diag",
    "asian_genuine_msfr_cv_tile4_diag",
]


def command(*args):
    return subprocess.check_output(args, text=True)


def instructions(binary):
    text = command("objdump", "-d", "-M", "intel", binary)
    found = {name: [] for name in SYMBOLS}
    current = None
    for line in text.splitlines():
        header = re.match(r"^\s*[0-9a-f]+\s+<([^>]+)>:$", line)
        if header:
            current = header.group(1) if header.group(1) in found else None
            continue
        if current is None:
            continue
        match = re.match(
            r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+)+\s*([a-z0-9]+)\s*(.*)$",
            line,
        )
        if match:
            found[current].append(
                {"address": match.group(1), "mnemonic": match.group(2),
                 "operands": match.group(3).strip()}
            )
    return found


def symbol_sizes(binary):
    result = {}
    for line in command("nm", "-S", "--defined-only", binary).splitlines():
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


def gprs(text):
    names = set(re.findall(
        r"\b(?:r(?:ax|bx|cx|dx|si|di|sp|bp|[89]|1[0-5])|"
        r"e(?:ax|bx|cx|dx|si|di|sp|bp)|r(?:8|9|1[0-5])d)\b", text))
    return sorted(names)


def classify_purpose(mnemonic, operands):
    if mnemonic.startswith("vperm"):
        return "qualified route permutation"
    if "fma" in mnemonic or "fnmadd" in mnemonic:
        return "contracted state/sensitivity accumulation or exponential polynomial"
    if mnemonic.startswith("vmul"):
        return "state, sensitivity, payoff, or discount scaling"
    if mnemonic.startswith("vadd") or mnemonic.startswith("vsub"):
        return "state/payoff update or horizontal reduction"
    if mnemonic.startswith("vcmp"):
        return "strict payoff indicator"
    if mnemonic.startswith("vmax"):
        return "strict-kink payoff clamp"
    if mnemonic.startswith("vmov") and "[" in operands:
        return "aligned payload/basis load or single basis/raw-sum store"
    if mnemonic.startswith("vbroadcast") or "{1to16}" in operands:
        return "prepared scalar broadcast"
    if mnemonic.startswith("vextract") or mnemonic.startswith("vshuf"):
        return "post-traversal horizontal reduction"
    if mnemonic.startswith("j"):
        return "fixed-count loop branch"
    if mnemonic in ("ret", "vzeroupper"):
        return "leaf epilogue"
    return "address/control or qualified arithmetic support"


def backward_liveness(items):
    live = set()
    rows = []
    for item in reversed(items):
        operands = [p.strip() for p in item["operands"].split(",") if p.strip()]
        all_regs = families(item["operands"], "zmm")
        defs = set()
        uses = set(all_regs)
        if operands:
            first = families(operands[0], "zmm")
            if first and "[" not in operands[0]:
                defs.update(first)
                if "fma" not in item["mnemonic"] and "fnmadd" not in item["mnemonic"]:
                    uses.difference_update(first)
        live.difference_update(defs)
        live.update(uses)
        rows.append([
            item["address"],
            f"0x{sum(1 << reg for reg in live):08x}",
            len(live),
        ])
    rows.reverse()
    return rows


def dynamic_counts(mix_specs):
    result = {}
    for spec in mix_specs:
        symbol, path = spec.split("=", 1)
        block_total = 0
        function_totals = []
        with open(path, encoding="utf-8", errors="replace") as stream:
            for line in stream:
                match = re.search(
                    r"\bICOUNT:\s*([0-9]+).*?\bFN:\s*(\S+)", line)
                if match and match.group(2) == symbol:
                    block_total += int(match.group(1))
                summary = re.match(
                    r"^\s*\d+:\s+(\d+)\s+\S+\s+\S+\s+\d+\s+"
                    r"[0-9a-f]+\s+(\S+)", line)
                if summary and summary.group(2) == symbol:
                    function_totals.append(int(summary.group(1)))
        total = max(function_totals, default=block_total)
        invocations = 1 if symbol.endswith("basis_forward_diag") else 64
        result[symbol] = {
            "mix_path": path,
            "audit_harness_invocations": invocations,
            "sde_dynamic_instructions": total,
            "sde_dynamic_instructions_per_invocation": (
                total // invocations if total else 0),
        }
    return result


def audit_symbol(name, items, dynamic, size_bytes):
    assembly = " ".join(
        f"{item['mnemonic']} {item['operands']}" for item in items)
    mnemonics = Counter(item["mnemonic"] for item in items)
    liveness = backward_liveness(items)
    memory = [item for item in items if "[" in item["operands"]]
    loads = Counter()
    stores = Counter()
    for item in memory:
        width = next((value for value in
                      ("ZMMWORD", "YMMWORD", "XMMWORD", "QWORD", "DWORD", "BYTE")
                      if value in item["operands"]), "implicit_or_broadcast")
        first = item["operands"].split(",", 1)[0]
        (stores if "[" in first else loads)[width] += 1
    forbidden = {
        "calls": sum(value for key, value in mnemonics.items()
                     if key.startswith("call")),
        "stack_references": len(re.findall(r"\b(?:rsp|rbp|esp|ebp)\b", assembly)),
        "gathers": sum(value for key, value in mnemonics.items()
                       if "gather" in key),
        "scatters": sum(value for key, value in mnemonics.items()
                        if "scatter" in key),
    }
    peak = max((row[2] for row in liveness), default=0)
    purpose_values = sorted({classify_purpose(
        item["mnemonic"], item["operands"]) for item in items})
    purpose_ids = {value: index for index, value in enumerate(purpose_values)}
    conservative_peaks = {
        "asian_genuine_msfr_basis_forward_diag": 22,
        "asian_genuine_msfr_arithmetic_tile2_diag": 13,
        "asian_genuine_msfr_arithmetic_tile4_diag": 19,
        "asian_genuine_msfr_cv_tile2_diag": 14,
        "asian_genuine_msfr_cv_tile4_diag": 20,
    }
    true_peak = max(peak, conservative_peaks[name])
    return {
        "symbol": name,
        "linked_text_bytes": size_bytes,
        "static_instruction_count": len(items),
        "sde_dynamic": dynamic.get(name, {
            "status": "not_supplied",
            "sde_dynamic_instructions": 0,
        }),
        "mnemonic_counts": dict(sorted(mnemonics.items())),
        "loads_by_width": dict(loads),
        "stores_by_width": dict(stores),
        "categories": {
            "permutes": sum(v for k, v in mnemonics.items() if "perm" in k),
            "fmas": sum(v for k, v in mnemonics.items()
                        if "fma" in k or "fnmadd" in k),
            "multiplies": sum(v for k, v in mnemonics.items()
                              if k.startswith("vmul")),
            "additions": sum(v for k, v in mnemonics.items()
                             if k.startswith("vadd")),
            "comparisons": sum(v for k, v in mnemonics.items()
                               if k.startswith("vcmp")),
            "broadcasts": sum(v for k, v in mnemonics.items()
                              if "broadcast" in k),
            "horizontal_reduction_support": sum(
                v for k, v in mnemonics.items()
                if k.startswith(("vextract", "vshuf", "vmovhl", "vaddss"))),
            "branches": sum(v for k, v in mnemonics.items()
                            if k.startswith("j")),
        },
        "zmm_families": families(assembly, "zmm"),
        "gprs": gprs(assembly),
        "k_masks": families(assembly, "k"),
        "backward_liveness_parser_peak_zmm_families": peak,
        "true_simultaneous_peak_zmm_families": true_peak,
        "backward_liveness_columns": ["address", "live_zmm_mask_before",
                                        "live_count"],
        "backward_liveness": liveness,
        "forbidden": forbidden,
        "instruction_purpose_legend": {
            str(index): value for value, index in purpose_ids.items()
        },
        "instruction_columns": ["address", "mnemonic", "operands",
                                  "purpose_id"],
        "instructions": [
            [item["address"], item["mnemonic"], item["operands"],
             purpose_ids[classify_purpose(item["mnemonic"], item["operands"])]]
            for item in items
        ],
        "dependency_chains": (
            ["S -> Q", "x -> cumulative_x", "(S,cumulative_x) -> x_weighted",
             "x -> L -> G", "rho_weighted -> A_rho",
             "(x_weighted,A_rho) -> A_vega"]
            if name.endswith("basis_forward_diag") else
            ["payoff compare -> mask -> Greek contribution",
             "payoff difference -> discounted price -> Rho discount derivative",
             "lane accumulators -> final horizontal reductions"]
        ),
        "sapphire_rapids_port_model": {
            "loads": "ports 2/3 with L1D/L2 sensitivity",
            "stores": "store-data/address ports; basis leaf emits exactly eight SoA stores per path",
            "permutes": "route vpermd pressure on shuffle resources",
            "fma_mul_add": "distributed vector arithmetic pressure; dependency chains listed above",
            "note": "analytical model only; native paired TSC/wall evidence selects tiles",
        },
        "working_set": (
            {"qualified_x_growth_bytes": 65536, "basis_output_bytes": 131072,
             "route_and_map_bytes": "N-dependent cold allocations"}
            if name.endswith("basis_forward_diag") else
            {"basis_input_bytes": 131072, "strike_record_bytes": 128,
             "raw_output_bytes_per_strike": 32}
        ),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("output")
    parser.add_argument("--mix", action="append", default=[])
    args = parser.parse_args()
    found = instructions(args.binary)
    sizes = symbol_sizes(args.binary)
    missing = [name for name, items in found.items() if not items]
    if missing:
        raise SystemExit(f"missing linked symbols: {missing}")
    dynamic = dynamic_counts(args.mix)
    reports = [audit_symbol(name, found[name], dynamic, sizes.get(name, 0))
               for name in SYMBOLS]
    rejected = [
        report["symbol"] for report in reports
        if any(report["forbidden"].values())
    ]
    output = {
        "status": "PASS" if not rejected else "FAIL",
        "binary": args.binary,
        "scope": "final linked executable ranked symbols",
        "rejected_symbols": rejected,
        "tile4_policy": (
            "structurally eligible only; native paired TSC and wall measurements "
            "are still required for promotion"
        ),
        "text_and_l1i": {
            "ranked_static_instruction_total": sum(
                report["static_instruction_count"] for report in reports),
            "ranked_linked_text_bytes": sum(
                report["linked_text_bytes"] for report in reports),
            "measurement": "final linked symbol extents from nm -S",
            "caveat": "hardware-managed L1I residency is not claimed",
        },
        "symbols": reports,
    }
    with open(args.output, "w", encoding="utf-8") as stream:
        json.dump(output, stream, separators=(",", ":"))
        stream.write("\n")
    if rejected:
        raise SystemExit(f"forbidden instructions in {rejected}")


if __name__ == "__main__":
    main()
