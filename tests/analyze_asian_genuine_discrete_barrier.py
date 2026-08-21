#!/usr/bin/env python3
import argparse
import hashlib
import json
import math
import random
import statistics
from pathlib import Path

BOOTSTRAPS=10_000
BOOT_SEED=0x42415252424F4F54

def quantile(values,p):
    s=sorted(values);return s[round((len(s)-1)*p)]

def vectors(stream,clock):
    out=[];av=[];bv=[]
    for q in stream["quartets"]:
        x=q[clock]
        if q["pattern"]=="ABBA":a=x[0]+x[3];b=x[1]+x[2]
        else:b=x[0]+x[3];a=x[1]+x[2]
        out.append(b/a);av.append(a/2);bv.append(b/2)
    return out,av,bv

def flatten(raw,comparison,section="cells"):
    rows=[]
    for cell in raw[section]:
        for stream in cell["streams"]:
            if stream["comparison"]==comparison:
                tr,ta,tb=vectors(stream,"tsc");wr,wa,wb=vectors(stream,"wall_ns")
                rows.append({"key":{k:cell[k] for k in cell if k!="streams"},
                    "tsc":tr,"tsc_a":ta,"tsc_b":tb,
                    "wall":wr,"wall_a":wa,"wall_b":wb})
    return rows

def cell_summary(row,clock):
    v=row[clock];s=sorted(v);n=len(s);tail=0.0;k=0
    for candidate in range(n//2):
        next_tail=tail+math.comb(n,candidate)*(.5**n)
        if next_tail>.025:break
        tail=next_tail;k=candidate+1
    a=statistics.median(row[clock+"_a"]);b=statistics.median(row[clock+"_b"])
    result={**row["key"],"p10":quantile(v,.1),"median":statistics.median(v),
            "p90":quantile(v,.9),"median_lower_95":s[k],
            "median_upper_95":s[n-k-1],"median_ci_method":"exact_binomial_order_statistics",
            "a_median":a,"b_median":b,"absolute_overhead":b-a,
            "percentage_overhead":100*(b/a-1)}
    if "N" in row["key"]:
        nfix=row["key"]["N"]
        result.update({"a_per_path":a/4096,"b_per_path":b/4096,
          "a_per_path_monitoring_date":a/(4096*nfix),
          "b_per_path_monitoring_date":b/(4096*nfix)})
        if clock=="wall":result.update({"a_paths_per_second":4096e9/a,
          "b_paths_per_second":4096e9/b,"a_complete_prices_per_second":1e9/a,
          "b_complete_prices_per_second":1e9/b})
    return result

def gm(values):return math.exp(sum(math.log(x) for x in values)/len(values))

def bootstrap(rows,clock,seed=BOOT_SEED,reps=None):
    if reps is None:reps=BOOTSTRAPS
    rng=random.Random(seed);samples=[]
    for _ in range(reps):
        med=[]
        for row in rows:
            values=row[clock];n=len(values)
            draw=[values[rng.randrange(n)] for __ in range(n)]
            med.append(statistics.median(draw))
        samples.append(gm(med))
    point=gm([statistics.median(r[clock]) for r in rows])
    return {"ratio":point,"lower_95":quantile(samples,.025),
            "upper_95":quantile(samples,.95),"upper_97_5":quantile(samples,.975),
            "bootstrap_replicates":reps,"seed":f"0x{seed:016x}"}

def summarize(rows,clock,seed=BOOT_SEED):
    result=bootstrap(rows,clock,seed)
    cells=[cell_summary(r,clock) for r in rows]
    result["maximum_cell_median"]=max(x["median"] for x in cells)
    result["minimum_cell_median"]=min(x["median"] for x in cells)
    result["cells"]=cells
    return result

def mode_summary(rows,clock,seed):
    modes=sorted({r["key"]["mode"] for r in rows})
    return {m:bootstrap([r for r in rows if r["key"]["mode"]==m],clock,seed^i)
            for i,m in enumerate(modes)}

def prereq(path):
    data=json.loads(path.read_text())
    if data.get("status")!="PASS":raise SystemExit(f"failed prerequisite: {path}")
    return {"path":str(path),"sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
            "status":data["status"]}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--input",required=True,type=Path)
    ap.add_argument("--json",required=True,type=Path)
    ap.add_argument("--markdown",required=True,type=Path)
    a=ap.parse_args();raw=json.loads(a.input.read_text())
    root=Path(__file__).resolve().parents[1]
    prerequisites=[prereq(root/"results/asian_genuine_discrete_barrier/correctness.json"),
                   prereq(root/"results/asian_genuine_discrete_barrier/kink_decomposition.json"),
                   prereq(root/"results/asian_genuine_discrete_barrier/price_errors.json"),
                   prereq(root/"results/asian_genuine_discrete_barrier/object_audit.json"),
                   prereq(root/"results/asian_genuine_discrete_barrier/package_audit.json")]

    self_sched={c:bootstrap(flatten(raw,"self_schedule"),c,BOOT_SEED^0x11)
                for c in ("tsc","wall")}
    explicit_sched={c:bootstrap(flatten(raw,"explicit_schedule"),c,BOOT_SEED^0x22)
                    for c in ("tsc","wall")}
    self_letter="i" if self_sched["tsc"]["ratio"]<1 else "g"
    explicit_letter="i" if explicit_sched["tsc"]["ratio"]<1 else "g"
    promotion_name=f"self_{self_letter}_vs_explicit_{explicit_letter}"
    promotion_rows=flatten(raw,promotion_name)
    promotion={c:{"global":bootstrap(promotion_rows,c,BOOT_SEED^0x33),
                  "by_mode":mode_summary(promotion_rows,c,BOOT_SEED^0x44),
                  "max_cell_median":max(statistics.median(r[c]) for r in promotion_rows)}
               for c in ("tsc","wall")}
    promote=all(promotion[c]["by_mode"][m]["upper_95"]<1.0
                for c in ("tsc","wall") for m in promotion[c]["by_mode"])
    promote=promote and all(promotion[c]["max_cell_median"]<=1.02 for c in ("tsc","wall"))
    family="explicit" if promote else "self"
    letter=explicit_letter if promote else self_letter
    finalist=f"resident_{family}_{'interleaved' if letter=='i' else 'grouped'}"

    qualification_name=f"vanilla_vs_{family}_{letter}"
    qrows=flatten(raw,qualification_name)
    qualification={c:summarize(qrows,c,BOOT_SEED^(0x100 if c=="tsc" else 0x200))
                   for c in ("tsc","wall")}

    aa=flatten(raw,"byte_identical_AA","controls")
    aa_result={c:bootstrap(aa,c,BOOT_SEED^0x300) for c in ("tsc","wall")}
    method_valid=all(aa_result[c]["lower_95"]<=1.0<=aa_result[c]["upper_97_5"]
                     for c in ("tsc","wall"))
    performance_pass=method_valid and all(
        qualification[c]["ratio"]<=1.05 and
        qualification[c]["upper_95"]<=1.05 and
        qualification[c]["maximum_cell_median"]<=1.10
        for c in ("tsc","wall"))

    table_name=f"{family}_{letter}_vs_table_{letter}"
    table_rows=flatten(raw,table_name)
    table={c:{"global":bootstrap(table_rows,c,BOOT_SEED^0x400),
              "by_mode":mode_summary(table_rows,c,BOOT_SEED^0x401)}
           for c in ("tsc","wall")}
    masks_materially_better=all(table[c]["global"]["lower_95"]>1.01
                                for c in ("tsc","wall"))

    complete_name=f"ours_{family}_{letter}_vs_onemkl"
    complete_rows=flatten(raw,complete_name)
    complete={c:{"global":bootstrap(complete_rows,c,BOOT_SEED^0x500),
                 "by_mode":mode_summary(complete_rows,c,BOOT_SEED^0x501)}
              for c in ("tsc","wall")}
    ours_faster=all(complete[c]["by_mode"][m]["lower_95"]>1.0
                    for c in ("tsc","wall") for m in complete[c]["by_mode"])
    brownian_justified=performance_pass and ours_faster

    no_barrier={c:summarize(flatten(raw,"no_barrier_vanilla_vs_resident","controls"),c,
                            BOOT_SEED^0x600) for c in ("tsc","wall")}
    status="DISCRETE_BARRIER_NATIVE_PERFORMANCE_QUALIFIED" if performance_pass else "DISCRETE_BARRIER_NATIVE_PERFORMANCE_FAILED"
    statuses=[status]
    if performance_pass:statuses += ["DISCRETE_DOWN_AND_OUT_QUALIFIED"]
    report={"status":status,"statuses":statuses,"raw_input":str(a.input),
      "raw_sha256":hashlib.sha256(a.input.read_bytes()).hexdigest(),
      "benchmark_binary_sha256":raw["binary_sha256"],"prerequisites":prerequisites,
      "candidate_selection":{"self_schedule":self_sched,"explicit_schedule":explicit_sched,
        "promotion_comparison":promotion_name,"promotion":promotion,
        "explicit_promoted":promote,"finalist":finalist,
        "rule":"explicit requires per-cache-mode paired 95% upper ratio < 1.0 for TSC and wall, with no cell median > 1.02"},
      "resident_overhead":{"comparison":qualification_name,"tsc":qualification["tsc"],
                           "wall":qualification["wall"]},
      "negative_controls":{"method_valid":method_valid,"byte_identical_AA":aa_result,
                           "no_barrier":no_barrier},
      "mask_table":{"comparison":table_name,"resident_materially_better":masks_materially_better,
                    "tsc":table["tsc"],"wall":table["wall"]},
      "onemkl":{"comparison":complete_name,"ours_faster_in_both_modes":ours_faster,
                "tsc":complete["tsc"],"wall":complete["wall"]},
      "gates":{"global_point_ratio_le_1_05":all(qualification[c]["ratio"]<=1.05 for c in ("tsc","wall")),
               "global_upper_95_le_1_05":all(qualification[c]["upper_95"]<=1.05 for c in ("tsc","wall")),
               "no_cell_median_gt_1_10":all(qualification[c]["maximum_cell_median"]<=1.10 for c in ("tsc","wall")),
               "AA_method_valid":method_valid},
      "brownian_bridge_followup_justified":brownian_justified,
      "notes":["Cell medians are combined with equal weights; raw timings are never pooled.",
               "The original per-cell mandatory-interval rule is not used because it creates a multiple-testing failure mode.",
               "Dead-lane statistics are descriptive; the kernel has no early exit, compaction, or lane recycling."]}
    a.json.parent.mkdir(parents=True,exist_ok=True);a.markdown.parent.mkdir(parents=True,exist_ok=True)
    with a.json.open("x") as out:json.dump(report,out,indent=2,sort_keys=True);out.write("\n")
    md=["# Genuine Discrete-Barrier Native Qualification","",f"Status: `{status}`","",
        f"Selected leaf: `{finalist}`.","",
        "## Decision gates","",
        f"- TSC global ratio `{qualification['tsc']['ratio']:.6f}`, 95% upper `{qualification['tsc']['upper_95']:.6f}`.",
        f"- Wall global ratio `{qualification['wall']['ratio']:.6f}`, 95% upper `{qualification['wall']['upper_95']:.6f}`.",
        f"- Worst cell medians: TSC `{qualification['tsc']['maximum_cell_median']:.6f}`, wall `{qualification['wall']['maximum_cell_median']:.6f}`.",
        f"- Byte-identical A/A method control: `{'PASS' if method_valid else 'FAIL'}`.",
        f"- Resident masks materially beat the table: `{'YES' if masks_materially_better else 'NO'}`.",
        f"- Ours beats matched oneMKL in both cache modes: `{'YES' if ours_faster else 'NO'}`.","",
        "The preserved local correctness and linked-object audits are prerequisites identified by SHA-256 in the JSON report.",
        "The raw float32-versus-float64 barrier-side differences remain reported in `correctness.json`; only the explicitly decomposed indicator-flip contribution is removed from the smooth residual gate.","",
        f"Continuous-barrier/Brownian-bridge follow-up justified: `{'YES' if brownian_justified else 'NO'}`.",""]
    with a.markdown.open("x") as out:out.write("\n".join(md))
    print(json.dumps({"status":status,"finalist":finalist,"gates":report["gates"]},indent=2))

if __name__=="__main__":main()
