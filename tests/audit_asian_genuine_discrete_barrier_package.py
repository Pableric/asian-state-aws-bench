#!/usr/bin/env python3
import hashlib
import json
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
MAKE=(ROOT/"tests/Makefile.asian_genuine_discrete_barrier").read_text()
BENCH=(ROOT/"benchmarks/bench_asian_genuine_discrete_barrier.c").read_text()
target=re.search(r"^aws-benchmark-native:.*?(?=^\S|\Z)",MAKE,re.M|re.S)
body=target.group(0) if target else ""
forbidden=("python","numpy","mpfr","sde","generate_")
source_manifest=[]
for line in (ROOT/"results/asian_genuine_discrete_barrier/SOURCE_SHA256SUMS").read_text().splitlines():
    digest,rel=line.split(None,1);rel=rel.strip()
    p=ROOT/"asian_genuine_discrete_barrier_carrier"/rel
    got=hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None
    source_manifest.append({"path":rel,"expected":digest,"actual":got,"pass":got==digest})
checks={
 "aws_target_exists":bool(target),
 "aws_target_builds_native_benchmark":"bench_asian_genuine_discrete_barrier" in body,
 "aws_target_runs_only_builtin_preflight":"./bench_asian_genuine_discrete_barrier --check-only" in body,
 "aws_target_has_no_forbidden_dependency":not any(x in body.lower() for x in forbidden),
 "warmups_16":"WARMUPS=16" in BENCH,
 "quartets_201":"QUARTETS=201" in BENCH,
 "abba_and_baab":all(x in BENCH for x in ('"BAAB"','"ABBA"')),
 "fenced_tsc":all(x in BENCH for x in ("_mm_lfence","__rdtsc","__rdtscp")),
 "monotonic_raw":"CLOCK_MONOTONIC_RAW" in BENCH,
 "reset_before_condition":re.search(r"reset_candidate\(c\);condition\(c,mode\);",BENCH) is not None,
 "historical_32k_rmw":all(x in BENCH for x in ("a64(32768)","f.pressure[i]+=i+3")),
 "exclusive_json_creation":"O_WRONLY|O_CREAT|O_EXCL" in BENCH,
 "raw_four_invocations":all(x in BENCH for x in ('uint64_t t[4],w[4]','for(int j=0;j<4;++j)measure_one')),
 "source_corrected_x_then_exp":BENCH.find("ordered_d1_x_only_diag")<BENCH.find("asian_vector_exp_range_reduced_array_diag"),
 "all_import_hashes_match":all(x["pass"] for x in source_manifest),
}
report={"status":"PASS" if all(checks.values()) else "FAIL","checks":checks,
        "imported_files":source_manifest,"aws_target":body.strip(),
        "notes":["The AWS target performs compilation and the binary's native preflight only.",
                 "Local SDE correctness, object audit and deterministic analysis are separate targets and committed prerequisites."]}
out=ROOT/"results/asian_genuine_discrete_barrier/package_audit.json"
out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2,sort_keys=True)+"\n")
print(json.dumps({"status":report["status"],"checks":checks},indent=2))
raise SystemExit(report["status"]!="PASS")
