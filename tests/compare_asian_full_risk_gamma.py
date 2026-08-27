#!/usr/bin/env python3
"""Deterministic SDE/QuantLib accuracy driver for fused finite-bump Gamma_h."""

import argparse
import concurrent.futures
import math
import subprocess
import sys


MANDATORY_LEVELS = (32768, 131072, 524288, 1048576)
EXTENSION_LEVELS = (2097152, 4194304)
REPLICATIONS = 16
T95_15 = 2.131449545559323
SENSITIVITY = (0.005, 0.02)


def run(command):
    completed = subprocess.run(command, text=True, capture_output=True)
    if completed.returncode != 0:
        if completed.stdout:
            sys.stderr.write(completed.stdout)
        if completed.stderr:
            sys.stderr.write(completed.stderr)
        raise RuntimeError("command failed: " + " ".join(command))
    return completed.stdout


def records(text, tag):
    output = []
    for line in text.splitlines():
        fields = line.split()
        if not fields or fields[0] != tag:
            continue
        record = {}
        for item in fields[1:]:
            if "=" not in item:
                raise RuntimeError(f"malformed protocol field {item}")
            key, value = item.split("=", 1)
            record[key] = value
        output.append(record)
    return output


def number(value):
    return float.fromhex(value) if value.lower().startswith(("0x", "-0x")) \
        else float(value)


def quantlib_level(command, samples, level, fraction):
    def one(replication):
        text = run(command + [
            "--stage", "gamma", "--samples", str(samples),
            "--level", str(level), "--replication", str(replication),
            "--fraction", repr(fraction)])
        rows = records(text, "QL_GAMMA_REP")
        if len(rows) != 1:
            raise RuntimeError("QuantLib Gamma protocol mismatch")
        row = rows[0]
        if (int(row["samples"]) != samples or int(row["level"]) != level or
                int(row["replication"]) != replication or
                number(row["fraction"]) != fraction or
                row.get("basis") != "64" or row.get("first_tick") != "1" or
                row.get("step_tick") != "1" or row.get("n") != "64" or
                row.get("s0") != "0x1.9p+6" or
                row.get("strike") != "0x1.9p+6" or
                row.get("q") != "0x0p+0" or
                row.get("r") != "0x1.eb851eb851eb8p-6" or
                row.get("sigma") != "0x1.999999999999ap-3" or
                row.get("maturity") != "0x1p+0" or
                row.get("times_exact") != "YES" or
                row.get("bridge") != "YES" or
                row.get("antithetic") != "YES" or
                row.get("control") != "YES"):
            raise RuntimeError("QuantLib Gamma configuration mismatch")
        return replication, row

    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        rows = list(executor.map(one, range(REPLICATIONS)))
    rows.sort(key=lambda pair: pair[0])
    if len({row["seed"] for _, row in rows}) != REPLICATIONS:
        raise RuntimeError("QuantLib Gamma seeds are not unique")

    result = {"samples": samples, "level": level, "fraction": fraction,
              "bump": number(rows[0][1]["bump"])}
    for side in ("call", "put"):
        values = [number(row[f"{side}_gamma"]) for _, row in rows]
        mean = math.fsum(values) / REPLICATIONS
        variance = (math.fsum((value - mean) ** 2 for value in values) /
                    (REPLICATIONS - 1))
        stderr = math.sqrt(variance / REPLICATIONS)
        half = T95_15 * stderr
        result[side] = {"mean": mean, "stderr": stderr,
                        "low": mean - half, "high": mean + half,
                        "half": half}
    return result


def resolved(previous, current, ours):
    strong = True
    for side in ("call", "put"):
        old = previous[side]
        new = current[side]
        if abs(new["mean"] - old["mean"]) > math.hypot(old["half"],
                                                        new["half"]):
            return False, False
        errors = [abs(ours[f"fused_{side}"] - new["mean"]),
                  abs(ours[f"triple_{side}"] - new["mean"])]
        nonzero = [value for value in errors if value > 0.0]
        if not nonzero or not new["half"] < 0.25 * min(nonzero):
            return False, False
        strong = strong and new["half"] < 0.10 * min(nonzero)
    return True, strong


def print_level(row, label="N64_GAMMA_REFERENCE_LEVEL"):
    for side in ("call", "put"):
        value = row[side]
        print(f"{label} side={side} fraction={row['fraction']:.17g} "
              f"effective_bump={row['bump']:.17g} "
              f"samples_per_rep={row['samples']} replications={REPLICATIONS} "
              f"mean={value['mean']:.17g} stderr={value['stderr']:.17g} "
              f"ci_low={value['low']:.17g} ci_high={value['high']:.17g}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--test", required=True)
    parser.add_argument("--quantlib", required=True)
    parser.add_argument("--sde", default="/opt/intel-sde/sde64")
    args = parser.parse_args()

    ql = [args.quantlib]
    version = records(run(ql + ["--stage", "version"]), "QL_VERSION")
    if len(version) != 1 or version[0].get("version") != "1.39-dev":
        raise RuntimeError("QuantLib version mismatch")
    print("quantlib_version 1.39-dev linkage=libQuantLib.so execution=NATIVE "
          "max_parallel_replications=2")

    sde = [args.sde, "-spr", "--", args.test]
    correctness = run(sde)
    pass_lines = [line for line in correctness.splitlines()
                  if line.startswith("asian_full_risk_gamma_correctness PASS")]
    if len(pass_lines) != 1:
        raise RuntimeError("SDE correctness protocol mismatch")
    print(pass_lines[0])

    ours_rows = records(run(sde + ["--accuracy"]), "OUR_GAMMA")
    if len(ours_rows) != 3:
        raise RuntimeError("ours Gamma accuracy protocol mismatch")
    ours_by_fraction = {number(row["fraction"]): row for row in ours_rows}
    expected_contract = {
        "basis": "64", "first_tick": "1", "step_tick": "1", "n": "64",
        "s0": "0x1.9p+6", "strike": "0x1.9p+6", "q": "0x0p+0",
        "r": "0x1.eb851eb851eb8p-6",
        "sigma": "0x1.999999999999ap-3", "maturity": "0x1p+0"}
    for row in ours_rows:
        if any(row.get(key) != value for key, value in expected_contract.items()):
            raise RuntimeError("ours N64 Gamma contract mismatch")
    principal = ours_by_fraction[0.01]
    if (principal.get("scalar_identity") != "YES" or
            principal.get("call_put_gamma_equal") != "YES"):
        raise RuntimeError("fused Gamma identity gate failed")
    ours = {key: number(principal[key]) for key in
            ("fused_call", "fused_put", "triple_call", "triple_put")}

    levels = []
    final_resolved = False
    strong = False
    for level, samples in enumerate(MANDATORY_LEVELS + EXTENSION_LEVELS):
        current = quantlib_level(ql, samples, level, 0.01)
        levels.append(current)
        if level + 1 < len(MANDATORY_LEVELS):
            continue
        final_resolved, strong = resolved(levels[-2], levels[-1], ours)
        if final_resolved:
            break
    print("N64_GAMMA_REFERENCE side fraction effective_bump samples_per_rep "
          "replications mean stderr ci_low ci_high")
    for row in levels:
        print_level(row)
    if not final_resolved:
        raise RuntimeError("N64 Gamma reference uncertainty unresolved")

    final = levels[-1]
    for side in ("call", "put"):
        ref = final[side]
        fused = ours[f"fused_{side}"]
        triple = ours[f"triple_{side}"]
        print(f"N64_GAMMA_RESULT side={side} Gamma_h={ref['mean']:.17g} "
              f"reference_ci_low={ref['low']:.17g} "
              f"reference_ci_high={ref['high']:.17g} "
              f"effective_bump={final['bump']:.17g} "
              f"fused_4096={fused:.17g} "
              f"fused_signed_error={fused-ref['mean']:.17g} "
              f"fused_abs_error={abs(fused-ref['mean']):.17g} "
              f"triple_4096={triple:.17g} "
              f"triple_signed_error={triple-ref['mean']:.17g} "
              f"triple_abs_error={abs(triple-ref['mean']):.17g}")

    for sensitivity_index, fraction in enumerate(SENSITIVITY, start=16):
        row = quantlib_level(ql, final["samples"], sensitivity_index, fraction)
        print_level(row, "N64_GAMMA_BUMP_SENSITIVITY_REFERENCE")
        observed = ours_by_fraction[fraction]
        for side in ("call", "put"):
            mean = row[side]["mean"]
            fused = number(observed[f"fused_{side}"])
            triple = number(observed[f"triple_{side}"])
            print(f"N64_GAMMA_BUMP_SENSITIVITY side={side} "
                  f"fraction={fraction:.17g} effective_bump={row['bump']:.17g} "
                  f"reference={mean:.17g} fused_4096={fused:.17g} "
                  f"triple_4096={triple:.17g} "
                  f"fused_abs_error={abs(fused-mean):.17g} "
                  f"triple_abs_error={abs(triple-mean):.17g}")
    print("N64_GAMMA_REFERENCE_STATUS " +
          ("RESOLVED_STRONG_10_PERCENT" if strong else
           "RESOLVED_25_PERCENT"))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"gamma_accuracy_failure: {error}", file=sys.stderr)
        raise SystemExit(1)
