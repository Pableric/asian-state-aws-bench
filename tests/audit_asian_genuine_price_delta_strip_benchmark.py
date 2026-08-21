#!/usr/bin/env python3
from __future__ import annotations
import json,re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/"benchmarks/bench_asian_genuine_price_delta_strip.c"
OUTPUT=ROOT/"results/asian_genuine_price_delta_strip/benchmark_audit.json"
text=SOURCE.read_text()
required_candidates={
 "existing_single_complete_arithmetic","existing_single_complete_geometric_cv",
 "repeat_complete_arithmetic_per_strike","repeat_complete_geometric_cv_per_strike",
 "ours_arithmetic_strip_tile4","ours_arithmetic_strip_tile8",
 "ours_geometric_cv_strip_tile4","ours_geometric_cv_strip_tile8",
 "ours_arithmetic_strip_delta_tile4","ours_arithmetic_strip_delta_tile8",
 "ours_geometric_cv_strip_delta_tile4","ours_geometric_cv_strip_delta_tile8",
 "onemkl_arithmetic_strip_tile4","onemkl_arithmetic_strip_tile8",
 "onemkl_geometric_cv_strip_tile4","onemkl_geometric_cv_strip_tile8",
 "onemkl_arithmetic_strip_delta_tile4","onemkl_arithmetic_strip_delta_tile8",
 "onemkl_geometric_cv_strip_delta_tile4","onemkl_geometric_cv_strip_delta_tile8"}
present=set(re.findall(r'"((?:existing|repeat|ours|onemkl)_[a-z0-9_]+)"',text))
produce=re.search(r"static void produce_ours\(void\)\{(.*?)\}",text,re.S).group(1)
checks={
 "candidate_matrix":present==required_candidates,
 "corrected_source_order":produce.find("ordered_d1_x_only_diag")<produce.find("asian_vector_exp_range_reduced_array_diag"),
 "no_direct_growth_producer":"ordered_d1_x_growth_local_diag" not in text and "ORDERED_D1_DIAG_PREPARE_GROWTH3" not in text,
 "qualified_route":"asian_genuine_sql_dual_control_diag" in text,
 "runtime_matrix":all(v in text for v in ("{16,32,64,128,256}","{1,4,8,16,32}")),
 "protocol":all(v in text for v in ("SAMPLES=51","WARMUPS=16","shuffle(","CLOCK_MONOTONIC_RAW","__rdtscp","historical_32KiB_rmw","warm_candidate_specific")),
 "raw_samples":all(v in text for v in ("raw_tsc","raw_wall_ns","tsc_p10","tsc_median","tsc_p90","wall_ns_p10","wall_ns_median","wall_ns_p90")),
 "commercial_metrics":all(v in text for v in ("prices_per_second","price_delta_pairs_per_second","marginal_ticks_per_additional_strike","amortized_ticks_per_strike","speedup_vs_repeating_complete_single_strike","onemkl_over_ours_complete_strip")),
 "setup_separate":"setup_ticks" in text and "setup_wall_ns" in text,
 "matched_onemkl":"vsRngGaussian" in text and "asian_intel_point_major_sql_diag" in text,
 "labels":"QUALIFIED_PRICE" in text and "KINK_AMBIGUITY_REPORTED_DIAGNOSTIC" in text,
 "kink_decomposition":all(v in text for v in ("canonical_kink_validation","arithmetic_ambiguous_paths","geometric_ambiguous_paths","first_path","unadjusted_delta_difference","kink_flip_contribution","non_kink_residual")),
 "complete_validation_summary":all(v in text for v in ("max_abs_complete_price_error","signed_mean_price_error","max_abs_kink_adjusted_residual","max_abs_same_Q_G_delta_error")),
 "complete_strip_once":all(v in text for v in ("produce_ours();asian_genuine_sql_dual_control_diag","asian_genuine_strip_l_to_g_diag","asian_genuine_strip_price_diag")),
}
payload={"status":"PASS" if all(checks.values()) else "FAIL","checks":checks,
 "candidate_count":len(present),"result_path":"results/asian_genuine_price_delta_strip/aws.json",
 "aws_command":["make -f tests/Makefile.asian_genuine_price_delta_strip -j2 aws-ready",
 "MKL_THREADING_LAYER=SEQUENTIAL MKL_NUM_THREADS=1 MKL_DYNAMIC=FALSE ./bench_asian_genuine_price_delta_strip --json results/asian_genuine_price_delta_strip/aws.json"]}
OUTPUT.parent.mkdir(parents=True,exist_ok=True);OUTPUT.write_text(json.dumps(payload,indent=2,sort_keys=True)+"\n");print(json.dumps(payload,indent=2,sort_keys=True));raise SystemExit(payload["status"]!="PASS")
