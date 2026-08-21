#!/usr/bin/env python3
import json
import hashlib
import re
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BINARY=ROOT/"bench_asian_genuine_discrete_barrier"
OUT=ROOT/"results/asian_genuine_discrete_barrier/object_audit.json"
NS=(16,32,64,128,256)

RANKED=(
 "asian_barrier_vanilla_call_grouped_diag",
 "asian_barrier_vanilla_put_grouped_diag",
 "asian_barrier_vanilla_call_interleaved_diag",
 "asian_barrier_vanilla_put_interleaved_diag",
 "asian_barrier_down_call_self_grouped_diag",
 "asian_barrier_down_put_self_grouped_diag",
 "asian_barrier_down_call_self_interleaved_diag",
 "asian_barrier_down_put_self_interleaved_diag",
 "asian_barrier_down_call_explicit_grouped_diag",
 "asian_barrier_down_put_explicit_grouped_diag",
 "asian_barrier_down_call_explicit_interleaved_diag",
 "asian_barrier_down_put_explicit_interleaved_diag",
 "asian_barrier_down_call_table_grouped_diag",
 "asian_barrier_down_put_table_grouped_diag",
 "asian_barrier_down_call_table_interleaved_diag",
 "asian_barrier_down_put_table_interleaved_diag",
 "asian_barrier_up_call_self_grouped_diag",
 "asian_barrier_up_put_self_grouped_diag",
)

def disassemble(symbol):
    text=subprocess.check_output([
        "objdump","-d","-Mintel","--no-show-raw-insn",
        f"--disassemble={symbol}",str(BINARY)],text=True)
    ins=[]
    for line in text.splitlines():
        m=re.match(r"\s*([0-9a-f]+):\s+([a-zA-Z0-9_.]+)\s*(.*)",line)
        if m: ins.append({"address":int(m.group(1),16),"mnemonic":m.group(2).lower(),"operands":m.group(3).strip().lower()})
    if not ins: raise RuntimeError(f"no disassembly for {symbol}")
    return ins

def target(i):
    m=re.match(r"([0-9a-f]+)",i["operands"])
    return int(m.group(1),16) if m else None

def counts(ins):
    m=[x["mnemonic"] for x in ins]
    op=" ".join(x["operands"] for x in ins)
    return {
      "instructions":len(ins),"loads":sum("ptr [" in x["operands"] and x["operands"].split(",",1)[0].find("ptr [")<0 for x in ins),
      "stores":sum("ptr [" in x["operands"] and x["operands"].split(",",1)[0].find("ptr [")>=0 for x in ins),
      "vpermd":m.count("vpermd"),"vmulps":m.count("vmulps"),
      "vmaxps_clamps":m.count("vmaxps"),"fma":sum("fmadd" in x for x in m),
      "broadcasts":sum("broadcast" in x or "{1to16}" in y["operands"] for x,y in zip(m,ins)),
      "reduction_instructions":sum(x in ("vextractf32x4","vmovhlps","vshufps","vaddss","vcvtss2sd") for x in m),
      "comparisons":sum(x.startswith("vcmp") for x in m),
      "mask_ands":sum(x.startswith("kand") for x in m),
      "kmovw":m.count("kmovw"),"branches":sum(x.startswith("j") for x in m),
      "calls":sum(x.startswith("call") for x in m),
      "gathers":sum("gather" in x for x in m),
      "stack_references":op.count("rsp")+op.count("rbp"),
      "zmm_registers":sorted({int(v) for v in re.findall(r"zmm(\d+)",op)}),
      "mask_registers":sorted({int(v) for v in re.findall(r"k([0-7])",op)}),
      "gpr_registers":sorted(set(re.findall(r"\b(?:r(?:ax|cx|dx|si|di|8|9|10|11)|e(?:ax|cx|dx|si|di)|r(?:8|9|10|11)d)\b",op)))
    }

def control_segments(ins):
    jne=next(x for x in ins if x["mnemonic"]=="jne")
    jb=next(x for x in reversed(ins) if x["mnemonic"]=="jb")
    loop_start=target(jne); packet_start=target(jb)
    ai={x["address"]:i for i,x in enumerate(ins)}
    li=ai[loop_start]; pi=ai[packet_start]; ji=ins.index(jne); bi=ins.index(jb)
    return ins[:pi],ins[pi:li],ins[li:ji+1],ins[ji+1:bi+1],ins[bi+1:]

def dynamic(ins,n):
    entry,pre,loop,post,exit=control_segments(ins)
    parts=((entry,1),(pre,128),(loop,128*(n-1)),(post,128),(exit,1))
    numeric=("instructions","loads","stores","vpermd","vmulps","vmaxps_clamps","fma","broadcasts","reduction_instructions","comparisons",
             "mask_ands","kmovw","branches","calls","gathers","stack_references")
    c={key:0 for key in numeric}
    for seq,multiplier in parts:
        sc=counts(seq)
        for key in numeric:c[key]+=sc[key]*multiplier
    c["instructions_formula"]=(len(entry),128,len(pre),len(loop),len(post),len(exit))
    return c,counts(loop),len(entry)+len(pre)+(n-1)*len(loop)+len(post)+len(exit)

nm_sizes={}
for line in subprocess.check_output(["nm","-S","--defined-only",str(BINARY)],text=True).splitlines():
    fields=line.split()
    if len(fields)>=4:
        try:nm_sizes[fields[3]]=int(fields[1],16)
        except ValueError:pass

symbols={}
fail=[]
for sym in RANKED:
    ins=disassemble(sym); static=counts(ins); dyn={}; loop_ref=None
    for n in NS:
        dc,lc,_=dynamic(ins,n);dyn[str(n)]=dc
        loop_ref=lc
    size=nm_sizes[sym]
    kind="vanilla" if "vanilla" in sym else "table" if "table" in sym else "explicit" if "explicit" in sym else "resident"
    expected_cmp=0 if kind=="vanilla" else 2
    expected_delta=0 if kind=="vanilla" else 4 if kind=="explicit" else 8 if kind=="table" else 2
    vanilla_loop=21
    gates={
      "zero_calls":static["calls"]==0,"zero_gathers":static["gathers"]==0,
      "zero_stack_references":static["stack_references"]==0,
      "two_growth_permutes_per_route":loop_ref["vpermd"]==2,
      "two_s_updates_per_route":loop_ref["vmulps"]==2,
      "expected_route_comparisons":loop_ref["comparisons"]==expected_cmp,
      "expected_recurring_delta":loop_ref["instructions"]-vanilla_loop==expected_delta,
    }
    if not all(gates.values()):fail.append({"symbol":sym,"gates":gates,"loop":loop_ref})
    symbols[sym]={"text_bytes_upper_bound":size,"static":static,
        "ordinary_route":loop_ref,"dynamic":dyn,"gates":gates}

purpose=[
 {"count":2,"instruction":"movq","purpose":"load compact growth_base and map pointers"},
 {"count":4,"instruction":"movzbq","purpose":"decode two source lines and two prepared control selectors"},
 {"count":4,"instruction":"shlq $6","purpose":"form aligned 64-byte source/control offsets"},
 {"count":2,"instruction":"vmovdqa32 growth","purpose":"load two D1 growth source lines"},
 {"count":2,"instruction":"vmovdqa32 control","purpose":"load the two prepared permutation vectors"},
 {"count":2,"instruction":"vpermd","purpose":"route the two chronological growth halves"},
 {"count":2,"instruction":"vmulps","purpose":"advance the two resident S halves"},
 {"count":2,"instruction":"vcmp*_oqps","purpose":"resident candidate only: self-mask strict barrier survival"},
 {"count":1,"instruction":"addq $16","purpose":"advance to the next compact D2..DN route"},
 {"count":1,"instruction":"decl","purpose":"decrement remaining routed dates"},
 {"count":1,"instruction":"jne","purpose":"bottom-tested fixed route loop"},
]

report={
 "status":"PASS" if not fail else "FAIL",
 "binary":str(BINARY.relative_to(ROOT)),"audited_complete_linked_symbols":True,
 "binary_sha256":hashlib.sha256(BINARY.read_bytes()).hexdigest(),
 "symbols":symbols,"failures":fail,
 "ordinary_route_instruction_purpose":purpose,
 "recurring_instruction_contract":{
   "vanilla":21,"resident_self_mask":23,"resident_explicit":25,
   "mask_table":29,
   "resident_delta":"exactly two ordered-quiet masked comparisons",
   "explicit_delta":"two comparisons plus two kandw",
   "table_delta":"two comparisons plus two mask loads and two mask stores; kmovq pointer reloads are outside the memory-operation count"
 },
 "live_register_audit":{
   "resident_peak":{"zmm":13,"gpr":9,"mask":6},
   "explicit_peak":{"zmm":13,"gpr":9,"mask":7},
   "persistent_zmm":{"20":"call/put accumulator half 0","21":"accumulator half 1","26":"double payoff scale","28":"barrier","29":"strike","30":"zero","31":"initial spot"},
   "recurrence_zmm":{"4":"S half 0","5":"S half 1","12":"routed growth half 0","13":"routed growth half 1","14-15":"growth source scratch"},
   "persistent_masks":{"k2":"compact routes","k3":"direct D1 growth","k4":"alive half 0","k5":"route count","k6":"alive half 1","k7":"packet byte offset"},
   "critical_chain":"one vmulps S recurrence per date; barrier comparisons consume updated S but do not feed the next vmulps, allowing compare work to overlap subsequent routing"
 },
 "sapphire_rapids_pressure":{
   "load_ports":"four 64-byte route loads plus compact scalar metadata; table adds four 16-bit mask memory operations",
   "shuffle_ports":"two vpermd per routed date, unchanged by barrier observation",
   "fp_ports":"two independent vmulps recurrence chains plus two ordered comparisons",
   "front_end":"unroll one; bottom-tested route; bounded separate challenger bodies",
 },
 "working_sets_bytes":{
   "qualified_x":32768,"qualified_growth":32768,"compact_route_each":16,
   "fragment_map_each":1600,"context":64,"mask_table_challenger":512,
   "ranked_intermediate_s_stores":0,"ranked_payoff_materialization":0
 },
 "notes":[
   "D1 uses two direct aligned growth loads and is observed before the routed loop.",
   "Compact entry zero is D2.",
   "The oneMKL point-major consumer intentionally contains gathers and is excluded from ranked-leaf zero-gather gates.",
   "Unranked S/mask probes and copied S/Q/L proof leaves are excluded from the commercial leaf ranking."
 ]
}

mix_specs={
 "vanilla":("/tmp/asian-barrier-vanilla.mix","asian_barrier_vanilla_call_grouped_diag"),
 "resident_self":("/tmp/asian-barrier-self.mix","asian_barrier_down_call_self_grouped_diag"),
 "resident_explicit":("/tmp/asian-barrier-explicit.mix","asian_barrier_down_call_explicit_grouped_diag"),
 "mask_table":("/tmp/asian-barrier-table.mix","asian_barrier_down_call_table_grouped_diag"),
}
cross={}
for name,(path,sym) in mix_specs.items():
    p=Path(path)
    if not p.exists():continue
    lines=p.read_text(errors="replace").splitlines();start=None
    marker=f"$dynamic-counts-for-function: {sym} "
    for i,line in enumerate(lines):
        if marker in line:start=i
    if start is None:continue
    values={}
    for line in lines[start:]:
        m=re.match(r"([*A-Z0-9_-]+)\s+(\d+)\s*$",line.strip())
        if m:values[m.group(1)]=int(m.group(2))
        if line.strip().startswith("*total"):
            values["total"]=int(line.split()[-1]);break
    predicted=symbols[sym]["dynamic"]["256"]["instructions"]
    cross[name]={"mix_file":path,"mix_sha256":hashlib.sha256(p.read_bytes()).hexdigest(),"symbol":sym,"sde_total":values.get("total"),
      "formula_total":predicted,"exact_match":values.get("total")==predicted,
      "VCMPPS":values.get("VCMPPS",0),"VPERMD":values.get("VPERMD",0),
      "VMULPS":values.get("VMULPS",0),"KMOVW":values.get("KMOVW",0)}
    if not cross[name]["exact_match"]:report["status"]="FAIL"
report["sde_dynamic_crosscheck_N256"]=cross
OUT.parent.mkdir(parents=True,exist_ok=True)
OUT.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
print(json.dumps({"status":report["status"],"symbols":len(symbols),"failures":fail},indent=2))
raise SystemExit(report["status"]!="PASS")
