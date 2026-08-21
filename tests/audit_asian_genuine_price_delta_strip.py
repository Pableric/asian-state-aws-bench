#!/usr/bin/env python3
from __future__ import annotations
import json,re,subprocess,sys
from pathlib import Path
from check_dynamic_mix import function_counts

ROOT=Path(__file__).resolve().parents[1]
OBJ=ROOT/"asian_genuine_price_delta_strip_avx512.o"
PRICE_ONLY="--price-only" in sys.argv[1:]
OUT=ROOT/"results/asian_genuine_price_delta_strip"/("object_audit_price.json" if PRICE_ONLY else "object_audit.json")
SYMBOLS={
 "l_to_g":"asian_genuine_strip_l_to_g_diag",
 "ar_price_1":"asian_genuine_strip_arithmetic_price_1_diag",
 "ar_price_4":"asian_genuine_strip_arithmetic_price_4_diag",
 "ar_price_8":"asian_genuine_strip_arithmetic_price_8_diag",
 "cv_price_1":"asian_genuine_strip_cv_price_1_diag",
 "cv_price_4":"asian_genuine_strip_cv_price_4_diag",
 "cv_price_8":"asian_genuine_strip_cv_price_8_diag",
 "ar_delta_1":"asian_genuine_strip_arithmetic_price_delta_1_diag",
 "ar_delta_4":"asian_genuine_strip_arithmetic_price_delta_4_diag",
 "ar_delta_8":"asian_genuine_strip_arithmetic_price_delta_8_diag",
 "cv_delta_1":"asian_genuine_strip_cv_price_delta_1_diag",
 "cv_delta_4":"asian_genuine_strip_cv_price_delta_4_diag",
 "cv_delta_8":"asian_genuine_strip_cv_price_delta_8_diag",
}

def pref(counts,names):return sum(v for k,v in counts.items() if k.startswith(names))
def sizes():
 result={}
 for line in subprocess.check_output(["nm","-S",str(OBJ)],text=True).splitlines():
  x=line.split()
  if len(x)==4:result[x[3]]=int(x[1],16)
 return result
def disassembly(symbol):
 body=subprocess.check_output(["objdump","-d","-Mintel","--no-show-raw-insn",f"--disassemble={symbol}",str(OBJ)],text=True).lower()
 ins=[]
 for line in body.splitlines():
  m=re.match(r"^\s*[0-9a-f]+:\s+([a-z0-9]+)\s*(.*)$",line)
  if m:ins.append((m.group(1),m.group(2)))
 return body,ins

def main():
 symbol_sizes=sizes();reports={};fail=[]
 active={k:v for k,v in SYMBOLS.items() if not PRICE_ONLY or k=="l_to_g" or "price" in k}
 for key,symbol in active.items():
  body,ins=disassembly(symbol);counts=function_counts(Path(f"/tmp/asian-strip-{key}.mix"),symbol)
  zmm=sorted({int(v) for v in re.findall(r"\bzmm(\d+)\b",body)})
  masks=sorted({int(v) for v in re.findall(r"\bk(\d+)\b",body)})
  raw=set(re.findall(r"\b(?:r(?:ax|bx|cx|dx|si|di|sp|bp|8|9|10|11|12|13|14|15)|e(?:ax|bx|cx|dx|si|di))\b",body))
  gpr=sorted(raw);stack=sum("rsp" in op or "rbp" in op for _,op in ins)
  item={
   "symbol":symbol,"text_bytes":symbol_sizes[symbol],"static_instructions":len(ins),
   "dynamic_instructions":counts.get("*total",0),
   "loads_by_width":{str(w):counts.get(f"*mem-read-{w}",0) for w in(1,2,4,8,16,32,64)},
   "stores_by_width":{str(w):counts.get(f"*mem-write-{w}",0) for w in(1,2,4,8,16,32,64)},
   "comparisons":pref(counts,("VCMP",)),"clamps":pref(counts,("VMAX",)),
   "fmas":pref(counts,("VFMADD","VFNMADD")),"multiplies":pref(counts,("VMUL",)),
   "adds":pref(counts,("VADD",)),"strike_broadcasts":0 if key=="l_to_g" else counts.get("VBROADCASTSS",0),
   "reduction_instructions":pref(counts,("VEXTRACTF32X4","VMOVHLPS","VSHUFPS","VCVTSS2SD")),
   "branches":pref(counts,("J",)),"calls":pref(counts,("CALL",)),
   "gathers":pref(counts,("VGATHER","VPGATHER")),"stack_references":stack,
   "spills":stack,"zmm_registers":zmm,"zmm_count":len(zmm),
   "peak_named_zmm_pressure":len(zmm),"gpr_registers":gpr,"gpr_count":len(gpr),
   "mask_registers":masks,"working_set_bytes":{
     "Q":16384 if key!="l_to_g" else 0,"L":16384 if key=="l_to_g" else 0,
     "G":16384,"strike_records":0 if key=="l_to_g" else int(key.rsplit("_",1)[1])*64,
     "outputs":0 if key=="l_to_g" else int(key.rsplit("_",1)[1])*32},
  }
  if item["calls"] or item["gathers"] or stack:
   fail.append(symbol)
  reports[key]=item
 def delta(a,b,scale=1):
  x,y=reports[a],reports[b]
  fields=("static_instructions","dynamic_instructions","comparisons","clamps","fmas","multiplies","adds","strike_broadcasts","reduction_instructions","branches","calls","gathers")
  return {field:(x[field]-scale*y[field]) for field in fields}|{
   "loads_by_width":{w:x["loads_by_width"][w]-scale*y["loads_by_width"][w] for w in x["loads_by_width"]},
   "stores_by_width":{w:x["stores_by_width"][w]-scale*y["stores_by_width"][w] for w in x["stores_by_width"]}}
 incremental={}
 def per_strike(item,n):
  return {k:({w:x/n for w,x in v.items()} if isinstance(v,dict) else v/n)
          for k,v in item.items()}
 for estimator in("ar","cv"):
  incremental[f"{estimator}_price_each_strike_1_to_4"]=per_strike(delta(f"{estimator}_price_4",f"{estimator}_price_1"),3)
  incremental[f"{estimator}_price_each_strike_4_to_8"]=per_strike(delta(f"{estimator}_price_8",f"{estimator}_price_4"),4)
  incremental[f"{estimator}_tile8_minus_two_tile4_price"]=delta(f"{estimator}_price_8",f"{estimator}_price_4",2)
  if not PRICE_ONLY:
   for count in(1,4,8):incremental[f"{estimator}_delta_added_per_strike_{count}"]=per_strike(delta(f"{estimator}_delta_{count}",f"{estimator}_price_{count}"),count)
   incremental[f"{estimator}_tile8_minus_two_tile4_delta"]=delta(f"{estimator}_delta_8",f"{estimator}_delta_4",2)
 payload={"status":"FAIL" if fail else "PASS","scope":"price-only" if PRICE_ONLY else "price-plus-Delta","classification":{"price":"QUALIFIED","delta":"not_in_price_package" if PRICE_ONLY else "KINK_AMBIGUITY_REPORTED_DIAGNOSTIC"},"symbols":reports,"incremental_work":incremental,"zero_call_gather_spill_gate":not fail,"working_sets":{"Q":16384,"L":16384,"G":16384,"Q_L_G":49152,"context_header":128,"strike_record_each":64,"output_each":32},"failures":fail}
 OUT.parent.mkdir(parents=True,exist_ok=True);OUT.write_text(json.dumps(payload,indent=2,sort_keys=True)+"\n");print(json.dumps(payload,indent=2,sort_keys=True));raise SystemExit(bool(fail))
if __name__=="__main__":main()
