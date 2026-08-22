#!/usr/bin/env python3
import argparse,json,statistics
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument("--aws",required=True)
p.add_argument("--audit",default="results/asian_genuine_aad_phase1/linked_structural_audit.json")
p.add_argument("--out",required=True)
a=p.parse_args();aws=json.loads(Path(a.aws).read_text());audit=json.loads(Path(a.audit).read_text())
rows=[r for r in aws["results"] if r.get("candidate")!="derived_ratios"]
groups={}
for r in rows:groups.setdefault((r["N"],r["side"],r["estimator"],r["conditioning"]),{})[r["candidate"]]=r
def ratios(left,right):
    return [v[left]["wall_ns_median"]/v[right]["wall_ns_median"] for v in groups.values() if left in v and right in v]
sf=statistics.median(ratios("contracted_suffix_price_delta_vega_rho","targeted_forward_price_delta_vega_rho"))
sg=statistics.median(ratios("contracted_suffix_price_delta_vega_rho","generic_reverse_unranked_diagnostic"))
sc=statistics.median(ratios("contracted_suffix_price_delta_vega_rho","crn_ours_seven_valuation"))
fc=statistics.median(ratios("targeted_forward_price_delta_vega_rho","crn_ours_seven_valuation"))
fits=audit["status"]=="PASS" and audit["backward_liveness"]["vector_spills"]==0
xonly=audit["x_only_reverse_route"]["pass"]
affordable=sf<=1.25
material=min(sc,fc)<=.8
phase2=affordable and xonly and material
answers=[
 f"1. Contracted suffix reverse fits without spills: {'yes' if fits else 'no'}.",
 f"2. The 32-KiB packet tape is affordable: {'yes' if affordable else 'no'} (median suffix/forward wall ratio {sf:.4f}).",
 f"3. x-only reverse routing works as expected: {'yes' if xonly else 'no'}.",
 f"4. Suffix beats generic reverse: {'yes' if sg<1 else 'no'} (median wall ratio {sg:.4f}).",
 f"5. Suffix beats targeted forward for scalar Vega/Rho: {'yes' if sf<1 else 'no'} (median wall ratio {sf:.4f}).",
 f"6. A scalar-risk candidate materially beats CRN: {'yes' if material else 'no'} (best median candidate/CRN ratio {min(sc,fc):.4f}).",
 f"7. Begin Phase 2: {'yes' if phase2 else 'no'}."]
Path(a.out).write_text("\n".join(answers)+"\n")
print("\n".join(answers))
