#!/usr/bin/env python3
"""Linked-symbol structural and exact backward-liveness audit."""

import argparse
import hashlib
import re
import struct
import subprocess
import sys
from pathlib import Path

STAGE1 = "asian_genuine_arithmetic_growth_only_q_diag"
FUSED = "asian_genuine_arithmetic_fused_source_exp_diag"
FUSED_CONSTANTS = "asian_genuine_arithmetic_fused_exp_constants"
LEAVES = {
    "asian_genuine_arithmetic_growth_only_price_1_diag": (1, False),
    "asian_genuine_arithmetic_growth_only_price_delta_1_diag": (1, True),
    "asian_genuine_arithmetic_growth_only_price_2_diag": (2, False),
    "asian_genuine_arithmetic_growth_only_price_delta_2_diag": (2, True),
    "asian_genuine_arithmetic_growth_only_price_4_diag": (4, False),
    "asian_genuine_arithmetic_growth_only_price_delta_4_diag": (4, True),
}

DOWNSTREAM_DISASSEMBLY_SHA256 = {
    STAGE1: "f7e6b7e61ffd98481d98afbcb6381ccee7c9b747ab63b8d5892d2f00b6d012d8",
    "asian_genuine_arithmetic_growth_only_price_1_diag":
        "990c444d49a453ef37de605eb3de3e9898c8ef8b5185ce39dfb6941ee539acb9",
    "asian_genuine_arithmetic_growth_only_price_delta_1_diag":
        "ccf427972b98d62970ca9d82d761f6777eda46f2e81fada0b2c5150652cabd91",
    "asian_genuine_arithmetic_growth_only_price_2_diag":
        "22cb1879d669f8c9d561a97d5de7a5cb4b45fd1c339b511f891ae1596add9c74",
    "asian_genuine_arithmetic_growth_only_price_delta_2_diag":
        "032903d9ef6012f7354dce4712a5091589d0266e9597fd390242d9559a1aa552",
    "asian_genuine_arithmetic_growth_only_price_4_diag":
        "c7862357b7766c762d58da90d47145a2c28dccec79094160c177f5ac32227334",
    "asian_genuine_arithmetic_growth_only_price_delta_4_diag":
        "8e021ad3675660b0f5c76a430a00280323763b40c5e1d64591c6f9e8e3c67136",
}

SOURCE_SHA256 = {
    "asian_genuine_fixed_block_source_avx512.s":
        "6bb596c77a478a7af6290361dd05e8c4eb9788a041706d4fd0d9a113ee56d9dc",
    "asian_geometric_cv_payoff_avx512.s":
        "78331e0b544b983d3b32d3fabc33745e858463aebca53928d7b6843f60802ee6",
    "private/asian_exp_p8_18diag.inc":
        "3309305a0db639ca49a93dd37f375df389ee59a8234b174d2c094c1b7da74b85",
    "asian_genuine_arithmetic_growth_only_q_avx512.s":
        "e8b09e63bc6b980315fd4e375657255f7e3cc76724554d1ad2ea2e2681b5a0f9",
    "asian_genuine_arithmetic_fused_source_exp_avx512.s":
        "d7013c01b16370a611051a3f6abd615ce07ea0b35b972599fe14d98aabcc16f4",
}

FUSED_CONSTANT_WORDS = (
    0x3FB8AA3B, 0x3F318000, 0xB95E8083,
    0x3F800000, 0x3F7FFFF9, 0x3EFFFFFC, 0x3E2AABBF,
    0x3D2AAB67, 0x3C085D88, 0x3AB5DE3B, 0x3959CFDE, 0x37D8C471,
)

EXP_MNEMONICS = (["vmulps", "vrndscaleps", "vmovaps",
                  "vfnmadd231ps", "vfnmadd231ps", "vbroadcastss"] +
                 ["vfmadd213ps"] * 8 + ["vscalefps"])
FUSED_LOOP_MNEMONICS = (["vmovaps", "vmovaps", "vfmadd132ps",
                         "vfmadd132ps"] + EXP_MNEMONICS + EXP_MNEMONICS +
                        ["vmovaps", "vmovaps", "add", "add", "dec", "jne"])


def exp_operands(input_register, output, exponent, reduced):
    return ([f"zmm{exponent},zmm{input_register},CONST",
             f"zmm{exponent},zmm{exponent},0x0",
             f"zmm{reduced},zmm{input_register}",
             f"zmm{reduced},zmm{exponent},CONST",
             f"zmm{reduced},zmm{exponent},CONST",
             f"zmm{output},CONST"] +
            [f"zmm{output},zmm{reduced},CONST"] * 8 +
            [f"zmm{output},zmm{output},zmm{exponent}"])


FUSED_LOOP_OPERANDS = (
    ["zmm4,ZMMWORD PTR [rax]", "zmm5,ZMMWORD PTR [rax+0x40]",
     "zmm4,zmm2,zmm3", "zmm5,zmm2,zmm3"] +
    exp_operands(4, 6, 8, 10) + exp_operands(5, 7, 9, 11) +
    ["ZMMWORD PTR [rsi],zmm6", "ZMMWORD PTR [rsi+0x40],zmm7",
     "rax,0x80", "rsi,0x80", "ecx", "BRANCH"])

ROUTE_MNEMONICS = [
    "mov", "mov", "movzx", "movzx", "movzx", "movzx",
    "shl", "shl", "shl", "shl",
    "vmovdqa32", "vmovdqa32", "vmovdqa32", "vmovdqa32",
    "vpermd", "vpermd", "vmulps", "vmulps", "vaddps", "vaddps",
    "add", "dec", "jne",
]

REGISTER_RE = re.compile(
    r"\b(?:r(?:ax|bx|cx|dx|si|di|bp|sp|8|9|10|11|12|13|14|15)d?"
    r"|e(?:ax|bx|cx|dx|si|di|bp|sp)|[abcd][lh]"
    r"|[xyz]mm(?:[0-9]|[12][0-9]|3[01])|k[0-7]|flags)\b", re.I)


def run(*args):
    return subprocess.check_output(args, text=True)


def canonical(register):
    value = register.lower()
    if re.fullmatch(r"[xyz]mm\d+", value):
        return "zmm" + re.search(r"\d+", value).group(0)
    aliases = {
        "eax": "rax", "ax": "rax", "al": "rax", "ah": "rax",
        "ebx": "rbx", "bx": "rbx", "bl": "rbx", "bh": "rbx",
        "ecx": "rcx", "cx": "rcx", "cl": "rcx", "ch": "rcx",
        "edx": "rdx", "dx": "rdx", "dl": "rdx", "dh": "rdx",
        "esi": "rsi", "edi": "rdi", "ebp": "rbp", "esp": "rsp",
    }
    if value in aliases:
        return aliases[value]
    match = re.fullmatch(r"(r(?:8|9|10|11|12|13|14|15))d", value)
    return match.group(1) if match else value


def registers(text):
    return {canonical(value) for value in REGISTER_RE.findall(text)}


def split_operands(text):
    return [part.strip() for part in text.split(",") if part.strip()]


def symbol_extent(binary, symbol):
    for line in run("nm", "-S", "--defined-only", str(binary)).splitlines():
        fields = line.split()
        if len(fields) >= 4 and fields[-1] == symbol:
            return int(fields[0], 16), int(fields[1], 16)
    raise RuntimeError(f"missing linked symbol: {symbol}")


def instructions(binary, symbol):
    address, size = symbol_extent(binary, symbol)
    text = run("objdump", "-d", "-M", "intel", f"--disassemble={symbol}",
               str(binary))
    code = []
    for line in text.splitlines():
        match = re.match(
            r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+)+\s*"
            r"([a-z0-9]+)\s*(.*)$", line)
        if not match:
            continue
        pc = int(match.group(1), 16)
        if re.fullmatch(r"[0-9a-f]{2}", match.group(2)):
            continue
        if address <= pc < address + size:
            code.append({"address": pc, "mnemonic": match.group(2),
                         "operands": match.group(3).strip()})
    if not code:
        raise RuntimeError(f"no instructions decoded for {symbol}")
    return code


def branch_target(instruction):
    match = re.match(r"([0-9a-f]+)\b", instruction["operands"])
    return int(match.group(1), 16) if match else None


def uses_defs(instruction):
    mnemonic = instruction["mnemonic"]
    operands = split_operands(instruction["operands"])
    operand_regs = [registers(value) for value in operands]
    all_regs = set().union(*operand_regs) if operand_regs else set()
    address_regs = set().union(
        *(regs for operand, regs in zip(operands, operand_regs) if "[" in operand)
    ) if operands else set()
    uses = set()
    defs = set()

    if mnemonic.startswith("j"):
        uses.add("flags")
    elif mnemonic in {"ret", "nop", "vzeroupper"}:
        pass
    elif mnemonic in {"cmp", "test"} or mnemonic.startswith("vcmp"):
        if mnemonic.startswith("vcmp") and operand_regs:
            defs |= operand_regs[0]
            uses |= set().union(*operand_regs[1:]) | address_regs
        else:
            uses |= all_regs
            defs.add("flags")
    elif mnemonic in {"add", "sub", "dec", "inc", "shl", "shr"}:
        if operand_regs:
            uses |= operand_regs[0]
            defs |= operand_regs[0]
            uses |= set().union(*operand_regs[1:]) if len(operand_regs) > 1 else set()
        uses |= address_regs
        defs.add("flags")
    elif mnemonic in {"xor", "vxorps", "vpxor", "vpxord"} and len(operand_regs) >= 2 and all(
            operand_regs[0] == value for value in operand_regs[1:]):
        defs |= operand_regs[0]
        if mnemonic == "xor":
            defs.add("flags")
    elif mnemonic in {"mov", "movzx", "kmovq", "kmovd"} or mnemonic.startswith("vmov") or mnemonic == "vbroadcastss":
        if not operands:
            return uses, defs
        if "[" in operands[0]:
            uses |= all_regs
        else:
            defs |= operand_regs[0]
            uses |= (set().union(*operand_regs[1:]) if len(operand_regs) > 1 else set())
            uses |= address_regs
    elif mnemonic.startswith("v"):
        if operand_regs:
            defs |= operand_regs[0]
            uses |= (set().union(*operand_regs[1:]) if len(operand_regs) > 1 else set())
            uses |= address_regs
            if re.match(r"vf(?:n?m)(?:add|sub)", mnemonic):
                uses |= operand_regs[0]
            mask = re.search(r"\{(k[0-7])\}", operands[0], re.I)
            if mask:
                uses.add(mask.group(1).lower())
                if "{z}" not in operands[0].lower():
                    uses |= operand_regs[0]
    else:
        uses |= all_regs
    return uses, defs


def liveness(code):
    by_address = {instruction["address"]: index
                  for index, instruction in enumerate(code)}
    successors = []
    for index, instruction in enumerate(code):
        edges = []
        mnemonic = instruction["mnemonic"]
        if mnemonic != "ret" and index + 1 < len(code):
            edges.append(index + 1)
        if mnemonic.startswith("j"):
            target = branch_target(instruction)
            if target in by_address:
                edges.append(by_address[target])
        successors.append(set(edges))

    live_in = [set() for _ in code]
    live_out = [set() for _ in code]
    changed = True
    while changed:
        changed = False
        for index in range(len(code) - 1, -1, -1):
            uses, defs = uses_defs(code[index])
            out = set().union(*(live_in[item] for item in successors[index])) \
                if successors[index] else set()
            inside = uses | (out - defs)
            if inside != live_in[index] or out != live_out[index]:
                live_in[index] = inside
                live_out[index] = out
                changed = True

    peak = 0
    peak_pc = 0
    table = []
    for instruction, before, after in zip(code, live_in, live_out):
        simultaneous = before | after
        vectors = sorted((value for value in simultaneous
                          if value.startswith("zmm")),
                         key=lambda value: int(value[3:]))
        if len(vectors) > peak:
            peak = len(vectors)
            peak_pc = instruction["address"]
        table.append((instruction["address"], instruction["mnemonic"],
                      instruction["operands"], before, after))
    return table, peak, peak_pc


def route_slice(code):
    backward = [(index, branch_target(instruction))
                for index, instruction in enumerate(code)
                if instruction["mnemonic"].startswith("j") and
                branch_target(instruction) is not None and
                branch_target(instruction) < instruction["address"]]
    if len(backward) != 2:
        raise RuntimeError(f"expected exactly two backward loops, got {len(backward)}")
    index_by_address = {instruction["address"]: index
                        for index, instruction in enumerate(code)}
    inner_end, inner_target = backward[0]
    outer_end, outer_target = backward[1]
    return (code[index_by_address[inner_target]:inner_end + 1],
            code[:index_by_address[outer_target]],
            code[inner_end + 1:outer_end + 1])


def memory_stores(code):
    stores = []
    for instruction in code:
        operands = split_operands(instruction["operands"])
        if operands and "PTR [" in operands[0]:
            stores.append(instruction)
    return stores


def normalized_operands(instruction):
    value = instruction["operands"]
    if instruction["mnemonic"].startswith("j"):
        return "BRANCH"
    value = re.sub(r"(?:DWORD (?:BCST|PTR) )?\[rip[^]]*\](?:\s*#.*)?",
                   "CONST", value)
    return value


def canonical_disassembly(code):
    """Address-independent final-linked mnemonic/operand representation."""
    address_to_index = {instruction["address"]: index
                        for index, instruction in enumerate(code)}
    rip_constants = {}
    lines = []
    for instruction in code:
        mnemonic = instruction["mnemonic"].lower()
        operands = instruction["operands"]
        if mnemonic.startswith("j") or mnemonic.startswith("call"):
            target = branch_target(instruction)
            if target in address_to_index:
                operands = f"local_instruction_{address_to_index[target]}"
            else:
                symbolic = re.search(r"<([^>]+)>", operands)
                name = symbolic.group(1).split("+")[0] if symbolic else "external"
                operands = f"external_symbol_{name}"

        def replace_rip(match):
            target = int(match.group(1), 16) if match.group(1) else None
            key = target if target is not None else match.group(0).lower()
            if key not in rip_constants:
                rip_constants[key] = len(rip_constants)
            return f"[rip_constant_{rip_constants[key]}]"

        operands = re.sub(
            r"\[rip[^]]*\](?:\s*#\s*([0-9a-f]+)(?:\s*<[^>]+>)?)?",
            replace_rip, operands, flags=re.I)
        operands = re.sub(r"\s+", "", operands).lower()
        lines.append(f"{mnemonic}\t{operands}")
    return ("\n".join(lines) + "\n").encode("ascii")


def symbol_bytes(binary, symbol):
    address, size = symbol_extent(binary, symbol)
    for line in run("objdump", "-h", str(binary)).splitlines():
        match = re.match(
            r"^\s*\d+\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+"
            r"[0-9a-f]+\s+([0-9a-f]+)\s+", line, re.I)
        if not match:
            continue
        section_size = int(match.group(1), 16)
        section_address = int(match.group(2), 16)
        section_offset = int(match.group(3), 16)
        if section_address <= address and address + size <= section_address + section_size:
            with binary.open("rb") as stream:
                stream.seek(section_offset + address - section_address)
                payload = stream.read(size)
            if len(payload) != size:
                raise RuntimeError(f"short linked symbol read: {symbol}")
            return payload
    raise RuntimeError(f"linked section not found for: {symbol}")


def audit_fused(binary):
    code = instructions(binary, FUSED)
    backward = [(index, branch_target(instruction))
                for index, instruction in enumerate(code)
                if instruction["mnemonic"].startswith("j") and
                branch_target(instruction) is not None and
                branch_target(instruction) < instruction["address"]]
    if len(backward) != 1:
        raise RuntimeError(f"fused leaf expected one fixed loop, got {len(backward)}")
    end, target = backward[0]
    index_by_address = {instruction["address"]: index
                        for index, instruction in enumerate(code)}
    loop = code[index_by_address[target]:end + 1]
    prologue = code[:index_by_address[target]]
    mnemonics = [instruction["mnemonic"] for instruction in code]
    loop_mnemonics = [instruction["mnemonic"] for instruction in loop]
    loop_operands = [normalized_operands(instruction) for instruction in loop]
    stores = memory_stores(code)
    _, peak, peak_pc = liveness(code)
    context_reads = [instruction["operands"] for instruction in prologue
                     if "PTR [rdi" in instruction["operands"]]
    store_operands = [instruction["operands"] for instruction in stores]
    constants = symbol_bytes(binary, FUSED_CONSTANTS)
    expected_constants = struct.pack("<12I", *FUSED_CONSTANT_WORDS)
    gates = {
        "exact_fixed_loop": loop_mnemonics == FUSED_LOOP_MNEMONICS,
        "exact_register_and_operand_schedule":
            loop_operands == FUSED_LOOP_OPERANDS,
        "exact_context_reads": context_reads == [
            "rax,QWORD PTR [rdi]", "rsi,QWORD PTR [rdi+0x8]",
            "zmm2,DWORD PTR [rdi+0x10]", "zmm3,DWORD PTR [rdi+0x14]"],
        "exact_signed_z_loads": sum(
            instruction["mnemonic"] == "vmovaps" and
            bool(re.search(r"zmm[45],ZMMWORD PTR \[rax(?:\+0x40)?\]",
                           instruction["operands"]))
            for instruction in loop) == 2,
        "exact_affine_fmas": sum(value == "vfmadd132ps"
                                   for value in loop_mnemonics) == 2 and
                              loop_operands[2:4] ==
                                ["zmm4,zmm2,zmm3", "zmm5,zmm2,zmm3"],
        "unchanged_exp_sequence": loop_mnemonics[4:19] == EXP_MNEMONICS and
                                  loop_mnemonics[19:34] == EXP_MNEMONICS,
        "exact_exp_coefficients": constants == expected_constants,
        "final_growth_stores_only": len(stores) == 2 and store_operands == [
            "ZMMWORD PTR [rsi],zmm6", "ZMMWORD PTR [rsi+0x40],zmm7"],
        "no_calls": not any(value.startswith("call") for value in mnemonics),
        "no_stack_references": not any(re.search(r"\b(?:rsp|rbp)\b",
                                                   instruction["operands"])
                                        for instruction in code),
        "no_gather_scatter": not any("gather" in value or "scatter" in value
                                      for value in mnemonics),
        "no_scalar_libm_or_fallback": not any(value.startswith("call")
                                                for value in mnemonics),
        "no_data_dependent_branch": sum(value.startswith("j")
                                          for value in mnemonics) == 1,
        "peak_below_32": peak < 32,
    }
    return peak, peak_pc, [name for name, passed in gates.items() if not passed]


def audit_symbol(binary, symbol, count=None, delta=False):
    code = instructions(binary, symbol)
    route, prologue, terminal = route_slice(code)
    table, peak, peak_pc = liveness(code)
    mnemonics = [instruction["mnemonic"] for instruction in code]
    route_mnemonics = [instruction["mnemonic"] for instruction in route]
    operands = "\n".join(instruction["operands"] for instruction in code)
    stores = memory_stores(code)
    forbidden_fma = [value for value in mnemonics
                     if re.search(r"(?:v?f(?:n?m)?(?:add|sub))", value)]
    calls = [value for value in mnemonics if value.startswith("call")]
    gather_scatter = [value for value in mnemonics
                      if "gather" in value or "scatter" in value]
    stack_refs = [instruction for instruction in code
                  if re.search(r"\b(?:rsp|rbp)\b", instruction["operands"])]
    route_rdi_memory = [instruction["operands"] for instruction in route
                        if "PTR [rdi" in instruction["operands"]]
    gates = {
        "route_is_exact_23_instruction_growth_body": route_mnemonics == ROUTE_MNEMONICS,
        "exactly_two_recurring_vpermd": sum(value == "vpermd" for value in route_mnemonics) == 2,
        "no_vpermd_outside_recurring_route": sum(value == "vpermd" for value in mnemonics) == 2,
        "no_calls": not calls,
        "no_stack_references": not stack_refs,
        "no_gather_scatter": not gather_scatter,
        "no_fma": not forbidden_fma,
        "route_reads_only_growth_and_map_fields": route_rdi_memory == [
            "r8,QWORD PTR [rdi+0x8]", "r9,QWORD PTR [rdi+0x10]"],
        "no_route_weight_access": not any("[rdi+0x18]" in value for value in route_rdi_memory),
    }

    if symbol == STAGE1:
        gates["stage1_has_exact_two_q_zmm_stores"] = (
            len(stores) == 2 and all(value["mnemonic"] == "vmovaps" and
                                     "ZMMWORD PTR" in value["operands"]
                                     for value in stores))
    else:
        expected_stores = count * (4 if delta else 2)
        gates.update({
            "immediate_context_q_pointer_never_read": not any(
                "[rdi+0x10]" in instruction["operands"] for instruction in prologue),
            "no_vector_state_stores_or_reloads": (
                len(stores) == expected_stores and
                all(value["mnemonic"] == "vmovsd" and
                    "QWORD PTR [r8" in value["operands"] for value in stores)),
            "required_final_output_stores_only": len(stores) == expected_stores,
            "no_x_or_l_route_access": route_rdi_memory == [
                "r8,QWORD PTR [rdi+0x8]", "r9,QWORD PTR [rdi+0x10]"],
            "no_path_data_branch": sum(value.startswith("j") for value in mnemonics) == 2,
        })

    failed = [name for name, passed in gates.items() if not passed]
    return {
        "symbol": symbol,
        "instructions": len(code),
        "route_instructions": len(route),
        "peak": peak,
        "peak_pc": peak_pc,
        "gpr": sorted(value for value in registers(operands)
                      if value.startswith("r")),
        "zmm": sorted((value for value in registers(operands)
                       if value.startswith("zmm")),
                      key=lambda value: int(value[3:])),
        "k": sorted(value for value in registers(operands) if value.startswith("k")),
        "failed": failed,
        "liveness": table,
        "terminal": terminal,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="bench_asian_genuine_arithmetic_growth_only")
    args = parser.parse_args()
    binary = Path(args.binary)
    if not binary.is_file():
        raise SystemExit(f"linked benchmark missing: {binary}")

    reports = [audit_symbol(binary, STAGE1)]
    reports.extend(audit_symbol(binary, symbol, count, delta)
                   for symbol, (count, delta) in LEAVES.items())
    fused_peak, fused_peak_pc, fused_failed = audit_fused(binary)
    failures = []
    source_root = Path(__file__).resolve().parent.parent
    for relative, expected in SOURCE_SHA256.items():
        path = source_root / relative
        if not path.is_file():
            failures.append(f"source_missing={relative}")
            continue
        observed = hashlib.sha256(path.read_bytes()).hexdigest()
        if observed != expected:
            failures.append(
                f"source_sha256={relative}:{observed},expected={expected}")
    for report in reports:
        if report["failed"]:
            failures.append(f"{report['symbol']}:{','.join(report['failed'])}")
        expected = DOWNSTREAM_DISASSEMBLY_SHA256[report["symbol"]]
        observed = hashlib.sha256(
            canonical_disassembly(instructions(binary, report["symbol"]))).hexdigest()
        if observed != expected:
            failures.append(
                f"{report['symbol']}:canonical_disassembly_sha256={observed},"
                f"expected={expected}")
    if fused_failed:
        failures.append(f"{FUSED}:{','.join(fused_failed)}@0x{fused_peak_pc:x}")
    if failures:
        for failure in failures:
            print(f"linked_audit_failure {failure}", file=sys.stderr)
        print(f"linked_audit FAIL fused_peak_zmm={fused_peak} "
              "downstream_symbols_unchanged=NO")
        return 1
    print(f"linked_audit PASS fused_peak_zmm={fused_peak} "
          "downstream_symbols_unchanged=YES")
    return 0


if __name__ == "__main__":
    sys.exit(main())
