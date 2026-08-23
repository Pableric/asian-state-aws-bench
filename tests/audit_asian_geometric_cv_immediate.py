#!/usr/bin/env python3
"""Final-linked structural, liveness, text, and hot-closure audit."""

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path

LEAVES = [
    ("price1", "asian_geometric_cv_immediate_price_1_diag",
     "asian_geometric_cv_immediate_hot_price_1", 0, 1, False),
    ("price_delta1", "asian_geometric_cv_immediate_price_delta_1_diag",
     "asian_geometric_cv_immediate_hot_price_delta_1", 1, 1, True),
    ("price2", "asian_geometric_cv_immediate_price_2_diag",
     "asian_geometric_cv_immediate_hot_price_2", 2, 2, False),
    ("price_delta2", "asian_geometric_cv_immediate_price_delta_2_diag",
     "asian_geometric_cv_immediate_hot_price_delta_2", 3, 2, True),
    ("price4", "asian_geometric_cv_immediate_price_4_diag",
     "asian_geometric_cv_immediate_hot_price_4", 4, 4, False),
    ("price_delta4", "asian_geometric_cv_immediate_price_delta_4_diag",
     "asian_geometric_cv_immediate_hot_price_delta_4", 5, 4, True),
]

SOURCE_SHA256 = {
    "asian_genuine_permute_setup.c":
        "e2e54ebca65f6586cbb2fbfa0621e20a63dbaafec3cbbdb2515f5a9a432a0301",
    "asian_genuine_fixed_block_source_setup.c":
        "9e010e70018c54770e17c40a78c6c2c81539e2c5fa8bae8daa62014e464b13d5",
    "asian_genuine_fixed_block_source_avx512.s":
        "6bb596c77a478a7af6290361dd05e8c4eb9788a041706d4fd0d9a113ee56d9dc",
    "asian_geometric_cv_payoff_avx512.s":
        "78331e0b544b983d3b32d3fabc33745e858463aebca53928d7b6843f60802ee6",
    "private/asian_exp_p8_18diag.inc":
        "3309305a0db639ca49a93dd37f375df389ee59a8234b174d2c094c1b7da74b85",
    "asian_genuine_sql_variable_avx512.s":
        "08f685f29e86a1480269797ff4ee4313088f33c3275af727faa72c12d1957709",
    "asian_genuine_price_delta_strip_setup.c":
        "907c4a05807de27d8ec3c226382e6b640048a87fcd31f9b97d82c5c37668dced",
    "asian_genuine_price_delta_strip_avx512.s":
        "42c52432e1c0b49956d5db11305e9b7d6a8f8c86d35c5fa819ea96443f03893e",
    "asian_arithmetic_pricer.c":
        "a4ee8c6b337ec248db11e371cab7ed48a8177d82223e8e38db2a92f18c57ac55",
    "asian_genuine_arithmetic_growth_only_q_avx512.s":
        "e8b09e63bc6b980315fd4e375657255f7e3cc76724554d1ad2ea2e2681b5a0f9",
    "asian_genuine_arithmetic_fused_source_exp_avx512.s":
        "d7013c01b16370a611051a3f6abd615ce07ea0b35b972599fe14d98aabcc16f4",
    "asian_geometric_cv_packet_local_avx512.s":
        "067a50bbc878252faf1c578792e05bd2da9b89712883216b6be9d9829fb1e74e",
    "private/asian_geometric_cv_immediate_diag.h":
        "f5ad30ecb6a16d9c4376a5921c3ea007b349f005224d7d8239525cbd48a8aa0f",
    "asian_geometric_cv_immediate_setup.c":
        "680c0f70a8633a1f513d85b8d259e62056803c8f56ec311c4fdbd352c051253d",
    "asian_geometric_cv_immediate_avx512.s":
        "1cd7854d811bc81d266a1201a006b4bf4b3d960f26db3aa6b784dd1cd52fe62c",
}

CANONICAL_PARENT_SHA256 = {
    "asian_genuine_fixed_block_signed_z_one_fma_source_diag":
        "dad5e1c70b10ce57514e58d0b51555060072b074a7ca19453ed4bf5b654fd0f3",
    "asian_vector_exp_range_reduced_array_diag":
        "d37e9402bcb0e7d02b6b61d8a85230286bf79bcaf83a2ca52750d6c125066178",
    "asian_genuine_sql_dual_control_diag":
        "5cc05481aec4d01b4a1e01b064336e0a6f7f108123bce14561efa9b021c56fc3",
    "asian_genuine_strip_l_to_g_diag":
        "644a19e95ba57c1b95dd8bf34f8153adbe2720d15effc098afd6c41bc8a77d00",
    "asian_genuine_strip_cv_price_1_diag":
        "22354da710a95b85a3d84bb915f7672c66c08d36474b8467a3bb14342b54e059",
    "asian_genuine_strip_cv_price_4_diag":
        "4fe44fe95ad1a3b5d5c69a502a7733d29563c28e31b6e1bba1ae7ed5c78096f7",
    "asian_genuine_strip_cv_price_delta_1_diag":
        "10c3fae467c6e8aa42850fb6e711a5fe59967403f4f6e3ab7b7761917e13177c",
    "asian_genuine_strip_cv_price_delta_4_diag":
        "97b2eb114133f1733aa697abf865f5cf699f1d689057a25ecea492226dafe718",
    "asian_geometric_cv_packet_local_qg_diag":
        "d31f2ec6c63ea8a6485eef16cb948afabb3edf20149e1e9e2b3a0a62b176eaaa",
}

REGISTER_RE = re.compile(
    r"\b(?:r(?:ax|bx|cx|dx|si|di|bp|sp|8|9|10|11|12|13|14|15)d?"
    r"|e(?:ax|bx|cx|dx|si|di|bp|sp)|[abcd][lh]"
    r"|[xyz]mm(?:[0-9]|[12][0-9]|3[01])|k[0-7]|flags)\b", re.I)


def run(*args):
    return subprocess.check_output(args, text=True)


def canonical_register(value):
    value = value.lower()
    if re.fullmatch(r"[xyz]mm\d+", value):
        return "zmm" + re.search(r"\d+", value).group(0)
    aliases = {
        "eax":"rax", "ax":"rax", "al":"rax", "ah":"rax",
        "ebx":"rbx", "bx":"rbx", "bl":"rbx", "bh":"rbx",
        "ecx":"rcx", "cx":"rcx", "cl":"rcx", "ch":"rcx",
        "edx":"rdx", "dx":"rdx", "dl":"rdx", "dh":"rdx",
        "esi":"rsi", "edi":"rdi", "ebp":"rbp", "esp":"rsp",
    }
    if value in aliases:
        return aliases[value]
    match = re.fullmatch(r"(r(?:8|9|10|11|12|13|14|15))d", value)
    return match.group(1) if match else value


def registers(text):
    return {canonical_register(x) for x in REGISTER_RE.findall(text)}


def split_operands(text):
    return [item.strip() for item in text.split(",") if item.strip()]


def symbol_table(binary):
    result = {}
    for line in run("nm", "-S", "--defined-only", str(binary)).splitlines():
        fields = line.split()
        if len(fields) >= 4:
            result[fields[-1]] = (int(fields[0],16),int(fields[1],16))
    return result


def symbol_extent(binary, symbol):
    table = symbol_table(binary)
    if symbol not in table:
        raise RuntimeError(f"missing final-linked symbol {symbol}")
    return table[symbol]


def instructions(binary, symbol):
    address,size=symbol_extent(binary,symbol)
    text=run("objdump","-d","-M","intel",f"--disassemble={symbol}",str(binary))
    code=[]
    for line in text.splitlines():
        match=re.match(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+)+\s*"
                       r"([a-z0-9]+)\s*(.*)$",line)
        if not match:continue
        pc=int(match.group(1),16)
        if re.fullmatch(r"[0-9a-f]{2}",match.group(2)):continue
        if address<=pc<address+size:
            code.append({"address":pc,"mnemonic":match.group(2).lower(),
                         "operands":match.group(3).strip()})
    if not code:raise RuntimeError(f"no instructions decoded for {symbol}")
    return code


def branch_target(instruction):
    match=re.match(r"([0-9a-f]+)\b",instruction["operands"])
    return int(match.group(1),16) if match else None


def uses_defs(instruction):
    mnemonic=instruction["mnemonic"]
    operands=split_operands(instruction["operands"])
    operand_regs=[registers(value) for value in operands]
    all_regs=set().union(*operand_regs) if operand_regs else set()
    address_regs=set().union(*(regs for operand,regs in zip(operands,operand_regs)
                               if "[" in operand)) if operands else set()
    uses,defs=set(),set()
    if mnemonic.startswith("j"):
        uses.add("flags")
    elif mnemonic in {"ret","nop","vzeroupper"}:
        pass
    elif mnemonic in {"cmp","test"}:
        uses|=all_regs;defs.add("flags")
    elif mnemonic in {"add","sub","dec","inc","shl","shr","xor"}:
        if operand_regs:
            defs|=operand_regs[0];uses|=operand_regs[0]
            if len(operand_regs)>1:uses|=set().union(*operand_regs[1:])
        uses|=address_regs;defs.add("flags")
        if mnemonic=="xor" and len(operand_regs)==2 and operand_regs[0]==operand_regs[1]:
            uses-=operand_regs[0]
    elif mnemonic in {"mov","movzx","lea","vbroadcastss"} or \
         mnemonic.startswith("vmov"):
        if operand_regs:
            defs|=operand_regs[0]
            if len(operand_regs)>1:uses|=set().union(*operand_regs[1:])
        uses|=address_regs
    elif mnemonic.startswith("v"):
        if operand_regs:
            defs|=operand_regs[0]
            if len(operand_regs)>1:uses|=set().union(*operand_regs[1:])
            uses|=address_regs
            if re.match(r"vf(?:n?m)(?:add|sub)",mnemonic):uses|=operand_regs[0]
            if mnemonic in {"vxorps","vxorpd","vpxord","vpxorq"} and \
               len(operand_regs)>=3 and operand_regs[0]==operand_regs[1]==operand_regs[2]:
                uses-=operand_regs[0]
    else:
        uses|=all_regs
    return uses,defs


def liveness(code):
    by_address={item["address"]:i for i,item in enumerate(code)}
    successors=[]
    for i,instruction in enumerate(code):
        edges=[]
        if instruction["mnemonic"]!="ret" and i+1<len(code):edges.append(i+1)
        if instruction["mnemonic"].startswith("j"):
            target=branch_target(instruction)
            if target in by_address:edges.append(by_address[target])
        successors.append(set(edges))
    live_in=[set() for _ in code];live_out=[set() for _ in code]
    changed=True
    while changed:
        changed=False
        for i in range(len(code)-1,-1,-1):
            uses,defs=uses_defs(code[i])
            outside=set().union(*(live_in[j] for j in successors[i])) \
                if successors[i] else set()
            inside=uses|(outside-defs)
            if inside!=live_in[i] or outside!=live_out[i]:
                live_in[i],live_out[i]=inside,outside;changed=True
    return max(sum(reg.startswith("zmm") for reg in before|after)
               for before,after in zip(live_in,live_out))


def backward_loops(code):
    index={item["address"]:i for i,item in enumerate(code)};result=[]
    for end,instruction in enumerate(code):
        target=branch_target(instruction)
        if instruction["mnemonic"].startswith("j") and target in index and \
           target<instruction["address"]:result.append((index[target],end))
    return result


def memory_stores(code):
    result=[]
    for instruction in code:
        operands=split_operands(instruction["operands"])
        if operands and "PTR [" in operands[0]:result.append(instruction)
    return result


def canonical_disassembly(code):
    address_index={item["address"]:i for i,item in enumerate(code)}
    constants={};lines=[]
    for instruction in code:
        mnemonic=instruction["mnemonic"];operands=instruction["operands"]
        if mnemonic.startswith("j") or mnemonic.startswith("call"):
            target=branch_target(instruction)
            if target in address_index:operands=f"local_instruction_{address_index[target]}"
            else:
                symbol=re.search(r"<([^>]+)>",operands)
                operands="external_symbol_"+(symbol.group(1).split("+")[0]
                  if symbol else "unknown")
        def replace_rip(match):
            key=match.group(1) or match.group(0).lower()
            if key not in constants:constants[key]=len(constants)
            return f"[rip_constant_{constants[key]}]"
        operands=re.sub(r"\[rip[^]]*\](?:\s*#\s*([0-9a-f]+)(?:\s*<[^>]+>)?)?",
                        replace_rip,operands,flags=re.I)
        operands=re.sub(r"\s+","",operands).lower()
        lines.append(f"{mnemonic}\t{operands}")
    return("\n".join(lines)+"\n").encode("ascii")


def called_symbols(code):
    result=[]
    for instruction in code:
        if instruction["mnemonic"].startswith("call") or \
           instruction["mnemonic"]=="jmp":
            match=re.search(r"<([^>]+)>",instruction["operands"])
            if match:result.append(match.group(1).split("+")[0])
    return result


def closure_size(binary,root):
    table=symbol_table(binary);pending=[root];seen=set();total=0
    while pending:
        symbol=pending.pop()
        if symbol in seen:continue
        if symbol not in table:raise RuntimeError(f"closure symbol missing {symbol}")
        seen.add(symbol);total+=table[symbol][1]
        for called in called_symbols(instructions(binary,symbol)):
            if called in table and called not in seen:pending.append(called)
    return total


def contains_exp_sequences(code):
    expected=(["vmulps","vrndscaleps","vmovaps","vfnmadd231ps",
               "vfnmadd231ps","vbroadcastss"]+["vfmadd213ps"]*8+
              ["vscalefps"])
    mnemonics=[item["mnemonic"] for item in code]
    hits=0
    for i in range(len(mnemonics)-len(expected)+1):
        if mnemonics[i:i+len(expected)]==expected:hits+=1
    return hits==2


def audit_leaf(binary,symbol,count,delta):
    code=instructions(binary,symbol);address,text_size=symbol_extent(binary,symbol)
    loops=backward_loops(code);failures=[]
    if len(loops)!=2:return 0,text_size,False,[f"{symbol}:backward_loop_count={len(loops)}"]
    route=code[loops[0][0]:loops[0][1]+1]
    route_m=[item["mnemonic"] for item in route]
    all_m=[item["mnemonic"] for item in code]
    stores=memory_stores(code);peak=liveness(code)
    stack_refs=[item for item in code if re.search(r"\b(?:rsp|rbp)\b",item["operands"])]
    vector_spill=any(re.search(r"\b[xyz]mm",item["operands"]) for item in stack_refs)
    output_stores=[item for item in stores if not re.search(
      r"\b(?:rsp|rbp)\b",item["operands"])]
    expected_stores=2*count*(2 if delta else 1)
    expected_route=["mov","mov","mov","movzx","shl","vmovdqa32",
      "vmovdqa32","movzx","shl","vmovdqa32","vmovdqa32","movzx",
      "shl","vmovdqa32","movzx","shl","vmovdqa32","vpermd",
      "vpermd","vpermd","vpermd","vmulps","vmulps","vaddps",
      "vaddps","vfmadd231ps","vfmadd231ps","add","cmp","jne"]
    constant_offsets=[]
    for item in code:
        match=re.search(
          r"<asian_genuine_arithmetic_fused_exp_constants(?:\+0x([0-9a-f]+))?>",
          item["operands"])
        if match:constant_offsets.append(int(match.group(1),16) if match.group(1) else 0)
    one_exp_offsets=[0,4,8,44,40,36,32,28,24,20,16,12]
    hard={
      "exact_four_recurring_vpermd":route_m.count("vpermd")==4 and all_m.count("vpermd")==4,
      "route_load_shape":route_m.count("vmovdqa32")==6 and route_m.count("movzx")==4,
      "exact_route_shape":route_m==expected_route,
      "rounded_s_q":route_m.count("vmulps")==2 and route_m.count("vaddps")==2,
      "weighted_l":route_m.count("vfmadd231ps")==2,
      "exact_terminal_exp":contains_exp_sequences(code),
      "exact_terminal_constants":constant_offsets[:24]==
        one_exp_offsets+one_exp_offsets,
      "terminal_log_base_adds":sum(item["mnemonic"]=="vaddps" and
        "[rsi+0x2c]" in item["operands"] for item in code)==2,
      "final_double_stores_only":len(output_stores)==expected_stores and
        all(item["mnemonic"]=="vmovsd" and "QWORD PTR [rcx" in item["operands"]
            for item in output_stores),
      "no_calls":not any(x.startswith("call") for x in all_m),
      "no_unexplained_stack":not stack_refs or vector_spill,
      "no_gather_scatter":not any("gather" in x or "scatter" in x for x in all_m),
      "no_kmov":not any(x.startswith("kmov") for x in all_m),
      "k0_absent":not any(re.search(r"\bk0\b",item["operands"]) for item in code),
      "masks_only_predicates":not any(re.search(r"\bk[3-7]\b",item["operands"])
                                     for item in code),
      "fixed_branches":sum(x.startswith("j") for x in all_m)==2,
      "text_alignment_64":address%64==0 and text_size>0,
    }
    failures.extend(f"{symbol}:{name}" for name,passed in hard.items() if not passed)
    rejected=peak>=32 or vector_spill
    return peak,text_size,rejected,failures


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--binary",required=True)
    args=parser.parse_args();binary=Path(args.binary);failures=[];results=[];mask=0
    if not binary.is_file():failures.append("linked_benchmark_missing")
    else:
        for label,symbol,root,bit,count,delta in LEAVES:
            try:
                peak,text_size,rejected,leaf_failures=audit_leaf(binary,symbol,count,delta)
                closure=closure_size(binary,root);failures.extend(leaf_failures)
                if not rejected:mask|=1<<bit
                results.append((label,peak,text_size,closure,rejected))
            except (OSError,subprocess.CalledProcessError,RuntimeError) as error:
                failures.append(str(error));results.append((label,0,0,0,True))
    root=Path(__file__).resolve().parent.parent
    for relative,expected in SOURCE_SHA256.items():
        path=root/relative
        observed=hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else "missing"
        if observed!=expected:failures.append(f"source_sha256={relative}:{observed}")
    if binary.is_file():
        for symbol,expected in CANONICAL_PARENT_SHA256.items():
            try:observed=hashlib.sha256(canonical_disassembly(
                  instructions(binary,symbol))).hexdigest()
            except RuntimeError as error:failures.append(str(error));continue
            if observed!=expected:
                failures.append(f"canonical_disassembly_sha256={symbol}:{observed}")
        names=run("nm","-g","--defined-only",str(binary)).lower()
        for forbidden in ("mkl","onemkl","vega","rho"):
            if forbidden in names:failures.append(f"forbidden_linked_symbol={forbidden}")
    if failures:
        for failure in failures:print(f"linked_audit_failure {failure}",file=sys.stderr)
        return 1
    fields=["linked_audit PASS",f"accepted_mask={mask}"]
    for label,peak,text_size,closure,rejected in results:
        fields.extend([f"{label}_peak={'REJECTED' if rejected else peak}",
                       f"{label}_text={text_size}",f"{label}_closure={closure}"])
    print(" ".join(fields));return 0


if __name__=="__main__":sys.exit(main())
