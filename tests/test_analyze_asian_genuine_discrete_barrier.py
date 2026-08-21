#!/usr/bin/env python3
import importlib.util
import json
import sys
import tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
ANALYZER=ROOT/"tests/analyze_asian_genuine_discrete_barrier.py"

def stream(name,a,b,ratio,phase="selection"):
    q=[]
    bv=round(1000*ratio)
    for i in range(201):
        if i&1:q.append({"pattern":"BAAB","tsc":[bv,1000,1000,bv],"wall_ns":[bv,1000,1000,bv]})
        else:q.append({"pattern":"ABBA","tsc":[1000,bv,bv,1000],"wall_ns":[1000,bv,bv,1000]})
    return {"phase":phase,"comparison":name,"a":a,"b":b,"quartets":q}

def main():
    cells=[]
    for n in (16,32,64,128,256):
        for option in ("call","put"):
            for barrier,bits in ((80,"0x42a00000"),(90,"0x42b40000"),(95,"0x42be0000"),(100,"0x42c80000")):
                for mode in ("candidate_warm","historical_32KiB_rmw"):
                    cells.append({"N":n,"option":option,"barrier":barrier,
                      "barrier_bits":bits,"strike":100,"mode":mode,"streams":[
                        stream("self_schedule","resident_self_grouped","resident_self_interleaved",.99),
                        stream("explicit_schedule","resident_explicit_grouped","resident_explicit_interleaved",.99),
                        stream("self_i_vs_explicit_i","resident_self_interleaved","resident_explicit_interleaved",1.01),
                        stream("vanilla_vs_self_i","vanilla_interleaved","resident_self_interleaved",1.02,"qualification"),
                        stream("self_i_vs_table_i","resident_self_interleaved","mask_table_interleaved",1.10,"diagnostic"),
                        stream("ours_self_i_vs_onemkl","complete_self_interleaved","complete_onemkl_point_major",2.0,"complete")
                      ]})
    controls=[]
    for n in (16,256):
        for option in ("call","put"):
            for mode in ("candidate_warm","historical_32KiB_rmw"):
                controls.append({"N":n,"option":option,"mode":mode,"streams":[
                    stream("byte_identical_AA","vanilla_grouped","vanilla_grouped",1.0,"control"),
                    stream("no_barrier_vanilla_vs_resident","vanilla_grouped","resident_self_grouped",1.02,"control")
                ]})
    raw={"status":"RAW_NATIVE_TIMINGS","binary_sha256":"00"*32,
         "cells":cells,"controls":controls,"knockout_profiles":[]}
    with tempfile.TemporaryDirectory() as td:
        d=Path(td);inp=d/"raw.json";out=d/"qualification.json";md=d/"qualification.md"
        inp.write_text(json.dumps(raw))
        spec=importlib.util.spec_from_file_location("barrier_analyzer",ANALYZER)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        module.BOOTSTRAPS=10
        saved=sys.argv;sys.argv=[str(ANALYZER),"--input",str(inp),"--json",str(out),"--markdown",str(md)]
        try:module.main()
        finally:sys.argv=saved
        report=json.loads(out.read_text())
        assert report["status"]=="DISCRETE_BARRIER_NATIVE_PERFORMANCE_QUALIFIED"
        assert report["candidate_selection"]["finalist"]=="resident_self_interleaved"
        assert not report["candidate_selection"]["explicit_promoted"]
        assert report["negative_controls"]["method_valid"]
        assert report["mask_table"]["resident_materially_better"]
        assert report["onemkl"]["ours_faster_in_both_modes"]
    print("asian_genuine_discrete_barrier_analysis=PASS cells=80")

if __name__=="__main__":main()
