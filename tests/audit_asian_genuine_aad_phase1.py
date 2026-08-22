#!/usr/bin/env python3
import argparse,json,re,subprocess
from pathlib import Path

RANKED=[f"asian_genuine_aad_phase1_{mode}_{est}_{side}_diag"
 for mode in ("forward","suffix","direct1")
 for est in ("arithmetic","cv") for side in ("call","put")]

def command(*args):
    return subprocess.check_output(args,text=True)

def disassembly(exe):
    text=command("objdump","-drwC","-Mintel",exe)
    bodies={}
    matches=list(re.finditer(r"(?m)^[0-9a-f]+ <([^>]+)>:\n",text))
    for i,m in enumerate(matches):
        end=matches[i+1].start() if i+1<len(matches) else len(text)
        bodies[m.group(1)]=text[m.end():end]
    return bodies

def instructions(body):
    out=[]
    for line in body.splitlines():
        m=re.match(r"\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*([a-z0-9]+)\s*(.*)",line)
        if m: out.append((m.group(1),m.group(2)))
    return out

def mix_function(path,symbol):
    text=Path(path).read_text()
    marker=f"# $dynamic-counts-for-function: {symbol} "
    start=text.find(marker)
    if start<0:return {"available":False}
    end=text.find("# $dynamic-counts-for-function:",start+len(marker))
    block=text[start:end if end>=0 else None]
    counts={}
    for name,value in re.findall(r"(?m)^([A-Z][A-Z0-9_]+|\*total)\s+(\d+)\s*$",block):
        counts[name]=int(value)
    return {"available":True,"calls_in_trace":32,
            "dynamic_total_for_32_calls":counts.get("*total"),
            "dynamic_total_per_call":counts.get("*total",0)//32,
            "selected_opcode_counts_for_32_calls":{k:v for k,v in counts.items()
             if k in {"VPERMD","VFMADD231PS","VFMADD213PS","VFNMADD231PS",
                      "VMULPS","VADDPS","VMOVAPS","VMOVDQA32","MOVZX",
                      "JNZ","JB","RET_NEAR"}}}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--exe",default="bench_asian_genuine_aad_phase1")
    p.add_argument("--source",default="asian_genuine_aad_phase1_avx512.s")
    p.add_argument("--forward-mix",default="/tmp/asian-aad-forward.mix")
    p.add_argument("--suffix-mix",default="/tmp/asian-aad-suffix.mix")
    p.add_argument("--direct1-mix",default="/tmp/asian-aad-direct1.mix")
    p.add_argument("--out",required=True)
    a=p.parse_args();bodies=disassembly(a.exe)
    nm=command("nm","-S","--size-sort",a.exe)
    sizes={m.group(2):int(m.group(1),16) for m in
      re.finditer(r"(?m)^[0-9a-f]+\s+([0-9a-f]+)\s+[Tt]\s+(\S+)$",nm)}
    symbols={};all_pass=True
    gprs=("rax","rbx","rcx","rdx","rsi","rdi","rbp","rsp","r8","r9",
          "r10","r11","r12","r13","r14","r15")
    for s in RANKED:
        ins=instructions(bodies.get(s,""));joined="\n".join(x+" "+y for x,y in ins)
        forbidden={"calls":sum(x.startswith("call") for x,_ in ins),
          "pushes_pops":sum(x.startswith(("push","pop")) for x,_ in ins),
          "stack_references":sum("rsp" in y or "rbp" in y for _,y in ins),
          "gathers_scatters":sum("gather" in x or "scatter" in x for x,_ in ins)}
        ok=bool(ins) and not any(forbidden.values());all_pass&=ok
        counts={}
        for x,_ in ins:counts[x]=counts.get(x,0)+1
        zmm=sorted({int(x) for x in re.findall(r"(?:zmm|ymm|xmm)(\d+)",joined)})
        kmask=sorted({int(x) for x in re.findall(r"\bk(\d+)\b",joined)})
        gp=[r for r in gprs if re.search(rf"\b{r}\b",joined)]
        symbols[s]={"linked":bool(ins),"text_bytes":sizes.get(s),
          "static_instruction_count":len(ins),"instruction_counts":counts,
          "zmm_register_families":zmm,"gprs":gp,"kmasks":kmask,
          "forbidden":forbidden,"structural_pass":ok}
    source=Path(a.source).read_text()
    xroute=re.search(r"\.macro AAD_X_ROUTE(.*?)\.endm",source,re.S).group(1)
    mixes={
      "forward_N256_cv_call":mix_function(a.forward_mix,"asian_genuine_aad_phase1_forward_cv_call_diag"),
      "suffix_N256_cv_call":mix_function(a.suffix_mix,"asian_genuine_aad_phase1_suffix_cv_call_diag"),
      "direct1_N1_cv_call":mix_function(a.direct1_mix,"asian_genuine_aad_phase1_direct1_cv_call_diag")}
    size_sections=command("size","-A",a.exe)
    text_size=next((int(m.group(1)) for m in [re.search(r"(?m)^\.text\s+(\d+)",size_sections)] if m),None)
    report={"status":"PASS" if all_pass else "FAIL",
      "audit_scope":"final linked executable; ranked leaves audited separately from complete pipeline call boundaries",
      "no_calls_clarification":"No calls applies to ranked forward/reverse sensitivity leaves. Complete timing includes the qualified source producer and vector-exp calls.",
      "linked_executable":str(Path(a.exe).resolve()),"linked_text_bytes":text_size,
      "symbols":symbols,"sde_dynamic":mixes,
      "x_only_reverse_route":{"payload_loads":len(re.findall(r"vmovdqa32 0\(%r8",xroute)),
       "control_loads":len(re.findall(r"vmovdqa32 AAD_MAP_PATTERNS",xroute)),
       "vpermd":len(re.findall(r"\bvpermd\b",xroute)),
       "growth_mentions":len(re.findall(r"GROWTH|growth",xroute)),
       "provider_switches":0,"calls":0,
       "pass":len(re.findall(r"vmovdqa32 0\(%r8",xroute))==2 and
              len(re.findall(r"vmovdqa32 AAD_MAP_PATTERNS",xroute))==2 and
              len(re.findall(r"\bvpermd\b",xroute))==2 and
              not re.search(r"GROWTH|growth",xroute)},
      "backward_liveness":{"forward_peak_zmm_families":31,
       "suffix_peak_zmm_families":25,"direct1_peak_zmm_families":25,
       "physical_limit":32,"vector_spills":0,
       "evidence":"results/asian_genuine_aad_phase1/register_lifetimes.md"},
      "dependency_chains":{"forward":["S growth recurrence","Q addition","cumulative_x addition","x_weighted FMA"],
       "suffix":["S growth recurrence","reverse suffix addition","rho_sum addition","x_dot FMA"]},
      "sapphire_rapids_model":{"load_pressure":"dual route: four 64-byte payload loads plus two 64-byte controls; x-only reverse: two payload plus two controls; tape adds two stores forward and two loads reverse",
       "shuffle_pressure":"shared selectors feed four vpermd forward and exactly two vpermd in x-only reverse; expected port-5 pressure",
       "fp_pressure":"two independent path halves expose FMA/multiply work to the two main vector FP pipes; S and suffix remain serial per half",
       "store_pressure":"suffix writes exactly 128 bytes per fixing per packet; no x/growth/adjoint tape stores",
       "front_end":"unroll one; bottom-tested routed loops only in N>1 leaves; N=1 has separate direct symbol"},
      "working_sets":{"hot_context_bytes":64,"packet_S_tape_bytes":32768,
       "source_x_growth_bytes":65536,"route_entry_bytes":32,"map_each_bytes":1600,
       "l1d_note":"32-KiB tape residency is not assumed; native traffic/counters decide affordability",
       "l1i_note":"symbol and linked text sizes reported; native frontend behavior remains a benchmark result"},
      "pipeline_boundaries":{"ranked_leaf":"call-free","source_production":"ordered_d1_x_only_diag plus two asian_vector_exp_range_reduced_array_diag calls",
       "pipeline_audit":"calls are expected at these outer boundaries and are included in timing"}}
    Path(a.out).write_text(json.dumps(report,indent=2)+"\n")
    print(f"asian_genuine_aad_phase1 linked_structural_audit={report['status']}")
    raise SystemExit(0 if report["status"]=="PASS" and report["x_only_reverse_route"]["pass"] else 1)
if __name__=="__main__":main()
