#!/usr/bin/env python3
"""Static object/disassembly audit of the provider boundaries and A/B loops.

Counts come from objdump of the assembled objects, not from source.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

SYM = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")

GPR32 = {"eax": "rax", "ebx": "rbx", "ecx": "rcx", "edx": "rdx", "esi": "rsi", "edi": "rdi",
         "ebp": "rbp", "esp": "rsp"}
for _n in range(8, 16):
    GPR32[f"r{_n}d"] = f"r{_n}"
GPR64 = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp"} | {f"r{n}" for n in range(8, 16)}
REG = re.compile(r"%([a-z0-9]+)")

WRITE_ONLY = {"mov", "movzbl", "movzwl", "tzcnt", "lea", "vmovdqa32", "vmovdqu32", "vmovaps",
              "vpbroadcastd", "vpmovzxbd", "vbroadcastss"}
RMW2 = {"add", "sub", "xor", "and", "or", "shl", "shr", "imul"}
AVX3 = {"vpxord", "vpermd", "vmulps", "vaddps"}
FMA = {"vfmadd213ps"}
NOWRITE = {"cmp", "test"}
STORE_MOV = {"mov", "vmovdqa32", "vmovdqu32", "vmovaps"}
FLAG_WRITERS = RMW2 | NOWRITE | {"tzcnt"}

FRAGMENTS = ("audit_a_dim_preload", "audit_a_packet", "audit_b_dim_preload",
             "audit_b_block_advance", "audit_b_block_enter", "audit_b_packet_first",
             "audit_b_packet_next")
CALLER = ("audit_a_context_select", "audit_b_record_select")
CONSUMERS = ("audit_consumer_sink", "audit_consumer_fused")
KERNELS = ("bench_a_sink", "bench_b_sink", "bench_a_fused", "bench_b_fused")

EXPECT = {
    "audit_a_packet": 11,
    "audit_a_dim_preload": 2,
    "audit_b_packet_next": 7,
    "audit_b_packet_first": 3,
    "audit_b_dim_preload": 2,
    "audit_b_block_advance": 2,
    "audit_b_block_enter": 6,
}


def split_operands(s: str) -> list[str]:
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "({":
            depth += 1
        elif ch in ")}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def norm(r: str) -> str | None:
    if r.startswith("zmm") or r.startswith("ymm") or r.startswith("xmm"):
        return r
    if r in GPR64:
        return r
    return GPR32.get(r)


def regs_in(op: str) -> list[str]:
    return [n for n in (norm(m) for m in REG.findall(op)) if n is not None]


def operand_size(op: str) -> int:
    for m in REG.findall(op):
        if m.startswith("zmm"):
            return 64
        if m in GPR64:
            return 8
        if m in GPR32:
            return 4
    return 0


def parse_insn(addr: int, nbytes: int, text: str) -> dict:
    text = text.split("#")[0].strip()
    parts = text.split(None, 1)
    mnem = parts[0]
    ops = split_operands(parts[1]) if len(parts) > 1 else []
    return {"addr": addr, "bytes": nbytes, "text": text, "mnemonic": mnem, "operands": ops}


def disassemble(objdump: str, obj: Path) -> tuple[str, list[dict], dict[str, int]]:
    text = subprocess.run([objdump, "-d", "-w", str(obj)], check=True, capture_output=True,
                          text=True).stdout
    insns: list[dict] = []
    syms: dict[str, int] = {}
    for line in text.splitlines():
        m = SYM.match(line)
        if m:
            syms[m.group(2)] = int(m.group(1), 16)
            continue
        cols = line.split("\t")
        if len(cols) >= 3 and cols[0].strip().endswith(":"):
            try:
                addr = int(cols[0].strip()[:-1], 16)
            except ValueError:
                continue
            nbytes = len(cols[1].split())
            insns.append(parse_insn(addr, nbytes, cols[2]))
    return text, insns, syms


def symbol_body(insns: list[dict], start: int, stop_at_ret: bool, end: int | None = None):
    body = []
    for ins in insns:
        if ins["addr"] < start:
            continue
        if end is not None and ins["addr"] >= end:
            break
        if stop_at_ret and ins["mnemonic"] == "ret":
            break
        body.append(ins)
    return body


def semantics(ins: dict):
    """Return (reads, writes, loads, stores) for an instruction."""
    m, ops = ins["mnemonic"], ins["operands"]
    reads: list[str] = []
    writes: list[str] = []
    loads: list[tuple[str, int]] = []
    stores: list[tuple[str, int]] = []
    if not ops:
        return reads, writes, loads, stores
    dest = ops[-1]
    srcs = ops[:-1]

    def mem_bytes(op: str, other: list[str]) -> int:
        if "{1to16}" in op:
            return 4
        if m == "movzbl":
            return 1
        if m == "movzwl":
            return 2
        if m == "vpmovzxbd":
            return 16
        if m in ("vpbroadcastd", "vbroadcastss"):
            return 4
        sizes = [operand_size(o) for o in other if "(" not in o]
        return max(sizes) if sizes else 0

    for op in ops:
        if "(" in op:
            reads += regs_in(op.split("(", 1)[1])
    for op in srcs:
        if "(" in op:
            loads.append((op, mem_bytes(op, ops)))
        elif op.startswith("%"):
            reads += regs_in(op)
    if m in NOWRITE or m.startswith("j"):
        if "(" not in dest and dest.startswith("%"):
            reads += regs_in(dest)
        elif "(" in dest:
            loads.append((dest, mem_bytes(dest, ops)))
        return reads, writes, loads, stores
    if "(" in dest:
        if m in STORE_MOV:
            stores.append((dest, mem_bytes(dest, ops)))
        else:
            loads.append((dest, mem_bytes(dest, ops)))
            stores.append((dest, mem_bytes(dest, ops)))
        return reads, writes, loads, stores
    d = regs_in(dest)
    if m in RMW2 and len(ops) == 2 or m in FMA:
        reads += d
    writes += d
    return reads, writes, loads, stores


def chain(body: list[dict]):
    """Longest register RAW chain (instructions) within one pass of body."""
    last_writer: dict[str, int] = {}
    depth = [0] * len(body)
    parent = [-1] * len(body)
    for i, ins in enumerate(body):
        reads, writes, _, _ = semantics(ins)
        best, who = 0, -1
        for r in reads:
            j = last_writer.get(r)
            if j is not None and depth[j] > best:
                best, who = depth[j], j
        depth[i] = best + 1
        parent[i] = who
        for w in writes:
            last_writer[w] = i
    if not body:
        return 0, []
    i = max(range(len(body)), key=lambda k: depth[k])
    path = []
    while i >= 0:
        path.append(body[i]["text"])
        i = parent[i]
    return max(depth), list(reversed(path))


def carried(body: list[dict]):
    """Registers live into the next iteration: read before written, then written.
    For each, the instructions on its value's path through one iteration."""
    first_read: dict[str, int] = {}
    written: set[str] = set()
    for i, ins in enumerate(body):
        reads, writes, _, _ = semantics(ins)
        for r in reads:
            if r not in written and r not in first_read:
                first_read[r] = i
        written.update(writes)
    out = {}
    for reg in sorted(r for r in first_read if r in written):
        depth: dict[str, int] = {reg: 0}
        final = 0
        for ins in body:
            reads, writes, _, _ = semantics(ins)
            ds = [depth[r] for r in reads if r in depth]
            for w in writes:
                if ds:
                    depth[w] = max(ds) + 1
                elif w in depth:
                    del depth[w]
            if reg in writes and reg in depth:
                final = depth[reg]
        out[reg] = final
    return out


def metrics(name: str, body: list[dict]) -> dict:
    vec = sum(1 for i in body if i["mnemonic"].startswith("v") or "%zmm" in i["text"])
    loads, stores = [], []
    zmm, gpr, zw, gw = set(), set(), set(), set()
    for ins in body:
        r, w, ld, st = semantics(ins)
        loads += [{"insn": ins["text"], "operand": o, "bytes": b} for o, b in ld]
        stores += [{"insn": ins["text"], "operand": o, "bytes": b} for o, b in st]
        for reg in set(r) | set(w):
            (zmm if reg.startswith("zmm") else gpr).add(reg)
        for reg in w:
            (zw if reg.startswith("zmm") else gw).add(reg)
    mn = Counter(i["mnemonic"] for i in body)
    depth, path = chain(body)
    return {
        "symbol": name,
        "instructions": len(body),
        "vector_instructions": vec,
        "scalar_instructions": len(body) - vec,
        "mnemonics": dict(sorted(mn.items())),
        "memory_operands": len(loads) + len(stores),
        "loads": len(loads),
        "load_bytes": sum(x["bytes"] for x in loads),
        "stores": len(stores),
        "store_bytes": sum(x["bytes"] for x in stores),
        "memory_detail": loads + stores,
        "zmm_registers": sorted(zmm, key=lambda s: int(s[3:])),
        "gpr_registers": sorted(gpr),
        "zmm_written": sorted(zw, key=lambda s: int(s[3:])),
        "gpr_written": sorted(gw),
        "code_bytes": sum(i["bytes"] for i in body),
        "calls": sum(1 for i in body if i["mnemonic"].startswith("call")),
        "branches": sum(1 for i in body if i["mnemonic"].startswith("j")),
        "gathers": sum(1 for i in body if "gather" in i["mnemonic"]),
        "stack_operations": sum(1 for i in body if i["mnemonic"] in ("push", "pop")
                                or "%rsp" in i["text"]),
        "spills": sum(1 for s in stores if "%rsp" in s["operand"] or "%rbp" in s["operand"]),
        "mask_register_uses": sum(1 for i in body if re.search(r"%k[0-7]\b", i["text"])),
        "flags_writers": sum(1 for i in body if i["mnemonic"] in FLAG_WRITERS),
        "vzeroupper": sum(1 for i in body if i["mnemonic"] == "vzeroupper"),
        "dependency_chain_instructions": depth,
        "dependency_chain": path,
        "listing": [f"{i['addr']:#06x}: {i['text']}" for i in body],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--objdump", default="objdump")
    ap.add_argument("--object", required=True, type=Path)
    ap.add_argument("--bench-object", required=True, type=Path)
    ap.add_argument("--listing", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    atext, ainsns, asyms = disassemble(args.objdump, args.object)
    btext, binsns, bsyms = disassemble(args.objdump, args.bench_object)
    boundaries = {}
    for name in FRAGMENTS + CALLER + CONSUMERS:
        boundaries[name] = metrics(name, symbol_body(ainsns, asyms[name], True))

    gates = []
    for name, n in EXPECT.items():
        gates.append({"check": f"{name} instructions == {n}",
                      "pass": boundaries[name]["instructions"] == n,
                      "observed": boundaries[name]["instructions"]})
    for name in FRAGMENTS:
        b = boundaries[name]
        bad = {k: b[k] for k in ("calls", "branches", "gathers", "stack_operations", "stores",
                                 "spills", "mask_register_uses", "vzeroupper") if b[k]}
        gates.append({"check": f"{name} has no calls/branches/gathers/stack/stores/spills/"
                               "mask registers/vzeroupper",
                      "pass": not bad, "observed": bad})

    for name, writes in (("audit_b_dim_preload", False), ("audit_b_block_advance", True),
                         ("audit_b_block_enter", True), ("audit_b_packet_first", False),
                         ("audit_b_packet_next", True)):
        got = boundaries[name]["flags_writers"] > 0
        gates.append({"check": f"{name} writes flags == {writes}", "pass": got == writes,
                      "observed": boundaries[name]["flags_writers"]})

    loops = {}
    for k in KERNELS:
        body = symbol_body(binsns, bsyms[f"{k}_packet_begin"], False, bsyms[f"{k}_packet_end"])
        m = metrics(f"{k} per-packet loop body", body)
        m["loop_carried_chain_instructions"] = carried(body)
        loops[k] = m
        if f"{k}_transition_begin" in bsyms:
            t = symbol_body(binsns, bsyms[f"{k}_transition_begin"], False,
                            bsyms[f"{k}_transition_end"])
            loops[f"{k}_transition"] = metrics(f"{k} block transition", t)
        nxt = sorted(a for a in bsyms.values() if a > bsyms[k])
        end = min([a for a in nxt if a not in (bsyms.get(f"{k}_packet_begin"),
                                               bsyms.get(f"{k}_packet_end"),
                                               bsyms.get(f"{k}_transition_begin"),
                                               bsyms.get(f"{k}_transition_end"))] or [None])
        whole = symbol_body(binsns, bsyms[k], False, end)
        while whole and whole[-1]["mnemonic"] != "ret":
            whole.pop()
        loops[k]["kernel_code_bytes"] = sum(i["bytes"] for i in whole)

    bd = boundaries
    unit = {
        "A_affine448": {
            "preload_per_context": bd["audit_a_dim_preload"]["instructions"],
            "preload_note": "each 448-byte context belongs to one (block, dimension), so the "
                            "preload runs once per (block, dimension)",
            "caller_context_select_per_block_x_dimension":
                bd["audit_a_context_select"]["instructions"],
            "per_block_transition": 0,
            "per_packet_provider": bd["audit_a_packet"]["instructions"],
            "provider_instructions_per_block_x_dimension":
                bd["audit_a_dim_preload"]["instructions"]
                + bd["audit_a_context_select"]["instructions"]
                + 128 * bd["audit_a_packet"]["instructions"],
        },
        "B_computed": {
            "per_dimension_preload": bd["audit_b_dim_preload"]["instructions"],
            "preload_note": "the record is block-independent: once per dimension when blocks "
                            "are the inner loop, once per (block, dimension) when dimensions "
                            "are the inner loop (the bench order)",
            "caller_record_select_per_dimension": bd["audit_b_record_select"]["instructions"],
            "per_block_transition": bd["audit_b_block_advance"]["instructions"]
                + bd["audit_b_block_enter"]["instructions"],
            "per_block_transition_detail": {
                "CPP_BLOCK_ADVANCE": bd["audit_b_block_advance"]["instructions"],
                "CPP_BLOCK_ENTER": bd["audit_b_block_enter"]["instructions"],
                "caller_state_load_store_when_dimensions_are_inner": 2,
            },
            "per_packet_provider_first": bd["audit_b_packet_first"]["instructions"],
            "per_packet_provider_next": bd["audit_b_packet_next"]["instructions"],
            "provider_instructions_per_block_x_dimension":
                bd["audit_b_dim_preload"]["instructions"]
                + bd["audit_b_record_select"]["instructions"]
                + bd["audit_b_block_advance"]["instructions"]
                + bd["audit_b_block_enter"]["instructions"]
                + bd["audit_b_packet_first"]["instructions"]
                + 127 * bd["audit_b_packet_next"]["instructions"],
        },
        "note": "Instruction counts per (block, dimension) unit of 128 packets, from the "
                "boundary symbols. Consumer and loop control are excluded here and are "
                "reported in loop_bodies.",
    }

    tools = {}
    for tool in ("gcc", "as", args.objdump):
        try:
            tools[tool] = subprocess.run([tool, "--version"], capture_output=True,
                                         text=True).stdout.splitlines()[0]
        except OSError:
            tools[tool] = "unavailable"

    ok = all(g["pass"] for g in gates)
    report = {
        "pass": ok,
        "source": "objdump -d -w of assembled objects",
        "toolchain": tools,
        "objects": {"boundaries": str(args.object), "bench": str(args.bench_object)},
        "gates": gates,
        "boundaries": boundaries,
        "loop_bodies": loops,
        "per_unit_accounting": unit,
    }
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    args.listing.write_text(atext + "\n\n" + btext)
    for name in FRAGMENTS + CALLER + CONSUMERS:
        b = boundaries[name]
        print(f"{name:28s} insns={b['instructions']:2d} vec={b['vector_instructions']:2d} "
              f"scalar={b['scalar_instructions']:2d} loads={b['loads']} ({b['load_bytes']} B) "
              f"stores={b['stores']} bytes={b['code_bytes']:3d} chain={b['dependency_chain_instructions']}")
    for k, m in loops.items():
        print(f"{k:28s} insns={m['instructions']:2d} vec={m['vector_instructions']:2d} "
              f"loads={m['loads']} ({m['load_bytes']} B) stores={m['stores']} "
              f"carried={m.get('loop_carried_chain_instructions', {})}")
    print(f"audit {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
