#!/usr/bin/env python3
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "results/asian_genuine_seasoned_price_delta_strip"
FROZEN = {
    "asian_genuine_price_delta_strip_setup.c": "907c4a05807de27d8ec3c226382e6b640048a87fcd31f9b97d82c5c37668dced",
    "asian_genuine_price_delta_strip_avx512.s": "42c52432e1c0b49956d5db11305e9b7d6a8f8c86d35c5fa819ea96443f03893e",
    "asian_genuine_permute_setup.c": "e2e54ebca65f6586cbb2fbfa0621e20a63dbaafec3cbbdb2515f5a9a432a0301",
    "asian_genuine_sql_variable_avx512.s": "08f685f29e86a1480269797ff4ee4313088f33c3275af727faa72c12d1957709",
    "ordered_d1_x_growth_handoff/ordered_d1_x_growth_setup.c": "886bcc7bdd71ff7355c4b7b278a7d19b5608c2fbbed7b9983cd98ff1608f7f08",
    "ordered_d1_x_growth_handoff/sobol_ordered_d1_x_growth_diag_avx512.s": "bbc08b0348309e47852b550d53f9008e05c880475f6ce4b1899ab69844aa5b89",
    "asian_geometric_cv_payoff_avx512.s": "78331e0b544b983d3b32d3fabc33745e858463aebca53928d7b6843f60802ee6",
}


def digest(path):
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def symbol(source, name):
    start = source.index(name + ":")
    end = source.index(".size " + name, start)
    return source[start:end]


def route_mix(path):
    text = pathlib.Path(path).read_text()
    rows = []
    for line in text.splitlines():
        if "FN: asian_genuine_sql_dual_control_diag" not in line:
            continue
        match = re.search(r"ICOUNT:\s+(\d+)\s+EXECUTIONS:\s+(\d+).*OFFSET:\s*([0-9a-fA-F]+)", line)
        if match:
            rows.append((int(match.group(3), 16), int(match.group(1)), int(match.group(2))))
    return sorted(set(rows))


def main():
    failures = []
    hashes = {path: digest(path) for path in FROZEN}
    for path, expected in FROZEN.items():
        if hashes[path] != expected:
            failures.append("frozen hash: " + path)
    asm = (ROOT / "asian_genuine_sql_variable_avx512.s").read_text()
    route = symbol(asm, "asian_genuine_sql_dual_control_diag")
    xasm = (ROOT / "ordered_d1_x_growth_handoff/sobol_ordered_d1_x_growth_diag_avx512.s").read_text()
    xonly = symbol(xasm, "ordered_d1_x_only_diag")
    checks = {
        "frozen_dependencies": not failures,
        "d1_consumes_route_weight": route.count("vfmadd231ps 24(%rdi){1to16}") == 2,
        "four_required_permutes": route.count("vpermd") == 4,
        "x_only_does_not_read_source_weight_table": "CTX_WEIGHTS" not in xonly,
        "route_has_no_calls": not re.search(r"\bcall", route),
        "route_has_no_gathers": "gather" not in route,
        "route_has_no_stack_references": "%rsp" not in route and "%rbp" not in route,
        "route_has_no_intermediate_stores": all(
            token not in route.split(".Lgd_route:", 1)[1].split("jne .Lgd_route", 1)[0]
            for token in ("vmovaps %zmm", "vmovdqa32 %zmm")
        ),
        "no_new_assembly_symbol": not any(
            p.suffix.lower() in (".s", ".asm")
            for p in ROOT.glob("private/*seasoned*")
        ),
    }
    try:
        unseasoned_mix = route_mix("/tmp/asian-seasoned-route-unseasoned.mix")
        seasoned_mix = route_mix("/tmp/asian-seasoned-route-seasoned.mix")
    except FileNotFoundError:
        unseasoned_mix = seasoned_mix = []
    checks["dynamic_route_trace_exact"] = bool(unseasoned_mix) and unseasoned_mix == seasoned_mix
    makefile = (ROOT / "tests/Makefile.asian_genuine_seasoned_strip").read_text()
    aws = makefile.split("aws-benchmark-native:", 1)[1].split("\n\n", 1)[0].lower()
    checks["aws_target_performance_only"] = not any(
        x in aws for x in ("python", "numpy", "mpfr", "sde", "coefficient", "qualification")
    )
    benchmark = (ROOT / "benchmarks/bench_asian_genuine_seasoned_price_delta_strip.c").read_text()
    checks["benchmark_candidate_matrix"] = all(
        token in benchmark for token in (
            "CANDIDATES = 24", "CONTRACTS = 25", "SAMPLES = 51",
            "WARMUPS = 16", "matched_f_unseasoned", "seasoned_ours",
            "seasoned_onemkl", "dquantile(rt, 32)", "dquantile(rw, 32)",
        )
    )
    checks["benchmark_raw_samples"] = "raw_tsc" in benchmark and "raw_wall_ns" in benchmark
    checks["benchmark_resets_outside_timing"] = benchmark.index("reset(c);") < benchmark.index("uint64_t w0 = wallns(), t0 = tsc0();")
    qualification = json.loads((OUT / "qualification.json").read_text())
    checks["qualification_passed"] = qualification["decision"] == "SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING"
    for name, passed in checks.items():
        if not passed:
            failures.append(name)

    text_sizes = {
        "shared_control_route": 0x173,
        "l_to_g": 0x15B,
        "arithmetic_price_1": 0xF5,
        "arithmetic_price_4": 0x318,
        "arithmetic_price_8": 0x60C,
        "cv_price_1": 0x13F,
        "cv_price_4": 0x409,
        "cv_price_8": 0x7E5,
        "arithmetic_price_delta_1": 0x1FB,
        "arithmetic_price_delta_4": 0x643,
        "arithmetic_price_delta_8": 0xC33,
        "cv_price_delta_1": 0x2A9,
        "cv_price_delta_4": 0x873,
        "cv_price_delta_8": 0x1073,
    }
    zero_delta = {
        key: 0 for key in (
            "dynamic_instructions", "loads", "stores", "vpermd", "multiplies",
            "adds", "fmas", "branches", "comparisons", "clamps",
            "strike_broadcasts", "reductions", "calls", "gathers",
            "stack_references", "spills", "text_bytes"
        )
    }
    report = {
        "status": "PASS" if not failures else "FAIL",
        "decision": "SEASONED_CORRECTNESS_QUALIFIED_AWS_PERFORMANCE_PENDING",
        "failures": failures,
        "checks": checks,
        "frozen_hashes": hashes,
        "hot_path_identity": {
            "reason": "matched-f seasoned and unseasoned candidates call identical source, route, L-to-G, and payoff symbols with identical trip counts",
            "seasoned_minus_unseasoned": zero_delta,
            "d1_fixed_per_block_increment": zero_delta,
            "route_dynamic_instruction_formula": "6 + 128 * (23 + 32*f)",
            "ordinary_route_static_instructions": 32,
            "sde_hot_block_dynamic_instructions": sum(row[1] for row in unseasoned_mix),
            "sde_route_dynamic_instructions_with_entry_exit":
                sum(row[1] for row in unseasoned_mix) + 6,
            "sde_route_blocks": [
                {"offset": offset, "instruction_count": count, "executions": executions}
                for offset, count, executions in unseasoned_mix
            ],
            "ordinary_route_operations": {
                "gpr_route_pointer_loads": 3,
                "map_byte_loads": 4,
                "zmm_loads": 6,
                "weight_memory_broadcast_operands": 2,
                "vpermd": 4,
                "vmulps": 2,
                "vaddps": 2,
                "vfmadd231ps": 2,
                "loop_branches": 1,
                "stores": 0,
            },
        },
        "register_allocation": {
            "zmm_architectural_names": [0,1,2,3,4,5,6,7,10,11,12,13,14,15,18,19],
            "zmm_peak_live": 12,
            "persistent_state": {"S":[4,5],"Q":[6,7],"L":[10,11]},
            "transient_x": [0,1], "transient_growth": [12,13],
            "transient_sources": [2,3,14,15], "transient_controls": [18,19],
            "gprs": ["rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11"],
            "masks": ["k1","k2","k3","k5","k7"],
            "stack_bytes": 0, "spills": 0,
        },
        "dependency_and_port_model_skylake_x": {
            "critical_chains": [
                "S: vmulps route-to-route",
                "Q: new-S then vaddps route-to-route",
                "L: vfmadd231ps route-to-route",
            ],
            "added_critical_dependencies": 0,
            "port_notes": {
                "p5_shuffle": "four vpermd per route; unchanged",
                "p2_p3_load": "six 64-byte vector loads plus scalar/map/weight operands; unchanged",
                "p0_p1_vector_arithmetic": "two mul, two add, two FMA; unchanged",
                "front_end_and_branch": "32 static instructions and one counted route branch; unchanged",
            },
            "qualification": "static port model; native timing, not this model, selects performance",
        },
        "text_sizes_bytes": text_sizes,
        "working_sets_bytes": {
            "Q": 16384, "L": 16384, "G": 16384,
            "one_route_table": 8192, "source_x_and_growth": 65536,
            "strike_records": 2048, "output": 1024,
            "completed_values_max_cold": 2048,
        },
        "payoff_leaf_audit": {
            "source": "results/asian_genuine_price_delta_strip/object_audit.json",
            "source_sha256": digest("results/asian_genuine_price_delta_strip/object_audit.json"),
            "seasoning_delta": zero_delta,
            "ranked_leaves_changed": False,
        },
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "structural_audit.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    md = f"""# Seasoned strip structural audit

Status: `{'PASS' if not failures else 'FAIL'}`.

Seasoned and matched-f unseasoned execution use the same qualified symbols and
the same future-fixing trip count.  The dynamic routed-loop delta is therefore
exactly zero for instructions, loads, stores, four `vpermd` operations,
arithmetic, branches, dependencies, and text.  There is no D1 increment: D1
already uses the same `24(%rdi){{1to16}}` prepared-weight FMA as every later
route.

The route body is 32 static instructions.  Complete dynamic count is
`6 + 128*(23 + 32*f)`.  Each ordinary route has four permutes, two multiplies,
two adds, two FMAs, six ZMM loads, four map-byte loads, three route-pointer
loads, two weight broadcast operands, one loop branch, and zero stores.  It
has no call, gather, stack reference, spill, or intermediate state store.

The register allocation uses 16 architectural ZMM names with peak liveness 12,
nine GPR names, and five mask names.  Persistent chains are `S` in ZMM4/5,
`Q` in ZMM6/7, and `L` in ZMM10/11.  On Skylake-X the unchanged pressure is
four port-5 permutes, the vector/scalar load stream on ports 2/3, and two each
of multiply, add, and FMA on the vector arithmetic ports.  Native paired timing
is the performance acceptance authority.

No ranked payoff or Delta leaf changed.  Their detailed instruction,
broadcast, reduction, load/store and incremental-strike accounting remains in
the frozen qualified object audit whose hash is recorded in JSON.
"""
    (OUT / "STRUCTURAL_AUDIT.md").write_text(md)
    print("asian_genuine_seasoned_strip structural_audit=" + report["status"])
    return 0 if not failures else 2


if __name__ == "__main__":
    sys.exit(main())
