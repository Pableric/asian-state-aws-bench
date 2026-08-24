#!/usr/bin/env python3
import argparse
import math
import subprocess
import sys


ANALYTIC_TOLERANCE = 1.0e-10
PUBLISHED_TOLERANCE = 0.02
MANDATORY_LEVELS = (32768, 131072, 524288, 1048576)
EXTENSION_LEVELS = (2097152, 4194304)


def run(command):
    completed = subprocess.run(command, text=True, capture_output=True)
    if completed.returncode != 0:
        if completed.stderr:
            sys.stderr.write(completed.stderr)
        if completed.stdout:
            sys.stderr.write(completed.stdout)
        raise RuntimeError("command failed: " + " ".join(command))
    return completed.stdout


def records(text, expected_tag):
    parsed = []
    for line in text.splitlines():
        fields = line.split()
        if not fields or fields[0] != expected_tag:
            raise RuntimeError(f"unexpected protocol line: {line}")
        record = {}
        for field in fields[1:]:
            if "=" not in field:
                raise RuntimeError(f"malformed protocol field: {field}")
            key, value = field.split("=", 1)
            if key in record:
                raise RuntimeError(f"duplicate protocol field: {key}")
            record[key] = value
        parsed.append(record)
    return parsed


def number(text):
    return float.fromhex(text) if text.lower().startswith(("0x", "-0x")) else float(text)


def contract_equal(ours, quantlib, keys):
    for key in keys:
        if ours.get(key) != quantlib.get(key):
            raise RuntimeError(
                f"contract mismatch key={key} ours={ours.get(key)} "
                f"quantlib={quantlib.get(key)}")


def analytic_gate(ours_text, quantlib_text):
    ours = {row["option"]: row for row in records(ours_text, "OUR_ANALYTIC")}
    quantlib = {row["option"]: row for row in records(quantlib_text, "QL_ANALYTIC")}
    if set(ours) != {"call", "put"} or set(quantlib) != {"call", "put"}:
        raise RuntimeError("analytic option set mismatch")
    print("analytic_geometric option ours quantlib abs_error decision")
    passed = True
    for option in ("call", "put"):
        left = ours[option]
        right = quantlib[option]
        contract_equal(left, right,
                       ("basis", "first_tick", "step_tick", "n", "s0",
                        "strike", "q", "r", "sigma"))
        if right.get("times_exact") != "YES":
            raise RuntimeError("QuantLib analytic fixing times are not exact")
        decisions = []
        for field in ("price", "mean", "variance"):
            got = number(left[field])
            reference = number(right[field])
            error = abs(got - reference)
            limit = ANALYTIC_TOLERANCE * (1.0 + abs(reference))
            decisions.append(error <= limit)
        price_error = abs(number(left["price"]) - number(right["price"]))
        decision = all(decisions)
        passed = passed and decision
        print(f"analytic_geometric {option} {number(left['price']):.17g} "
              f"{number(right['price']):.17g} {price_error:.17g} "
              f"{'PASS' if decision else 'FAIL'}")
    if not passed:
        raise RuntimeError("analytic geometric convention gate failed")


def published_stage(ours_text, quantlib_text):
    ours_rows = records(ours_text, "OUR_PUBLISHED")
    ql_rows = records(quantlib_text, "QL_PUBLISHED")
    ours = {row["id"]: row for row in ours_rows}
    quantlib = {row["id"]: row for row in ql_rows}
    if len(ours) != 24 or len(quantlib) != 24 or set(ours) != set(quantlib):
        raise RuntimeError("published case identifier set mismatch")
    print("first N schedule_status reference quantlib ours_plain ours_geocv "
          "quantlib_abs_error plain_abs_error geocv_abs_error decisions")
    compatible = 0
    skipped = 0
    quantlib_failed = False
    for left in ours_rows:
        right = quantlib[left["id"]]
        contract_equal(left, right,
                       ("id", "first", "n", "basis", "first_tick",
                        "step_tick", "s0", "strike", "q", "r", "sigma",
                        "published"))
        if right.get("times_exact") != "YES":
            raise RuntimeError(f"QuantLib fixing-time gate failed id={left['id']}")
        published = number(right["published"])
        ql_value = number(right["quantlib"])
        ql_error = abs(ql_value - published)
        ql_ok = ql_error <= PUBLISHED_TOLERANCE
        quantlib_failed = quantlib_failed or not ql_ok
        first = int(left["first"]) / 12.0
        n = int(left["n"])
        if left["status"] == "SKIPPED":
            skipped += 1
            print(f"{first:.12g} {n} SKIPPED_UNSUPPORTED_SCHEDULE "
                  f"reference={published:.17g} quantlib={ql_value:.17g} "
                  f"quantlib_abs_error={ql_error:.17g} "
                  f"quantlib_within_0.02={'YES' if ql_ok else 'NO'} "
                  f"reason={left['reason']}")
            continue
        if left.get("status") != "SUPPORTED" or left.get("q_identity") != "YES":
            raise RuntimeError(f"invalid ours status id={left['id']}")
        compatible += 1
        plain = number(left["plain_put"])
        geocv = number(left["geocv_put"])
        plain_error = abs(plain - published)
        geocv_error = abs(geocv - published)
        print(f"{first:.12g} {n} SUPPORTED {published:.17g} "
              f"{ql_value:.17g} {plain:.17g} {geocv:.17g} "
              f"{ql_error:.17g} {plain_error:.17g} {geocv_error:.17g} "
              f"quantlib_within_0.02={'YES' if ql_ok else 'NO'} "
              f"plain_within_0.02={'YES' if plain_error <= PUBLISHED_TOLERANCE else 'NO'} "
              f"geocv_within_0.02={'YES' if geocv_error <= PUBLISHED_TOLERANCE else 'NO'}")
    print(f"published_case_counts compatible={compatible} skipped={skipped}")
    if compatible != 9 or skipped != 15:
        raise RuntimeError("unexpected compatibility count")
    if quantlib_failed:
        raise RuntimeError("QuantLib published regression exceeded 0.02")


def n64_row(command, samples, level):
    def one_replication(replication):
        output = run(command + ["--stage", "n64", "--samples", str(samples),
                                "--level", str(level), "--replication",
                                str(replication)])
        rows = records(output, "QL_N64_REP")
        if len(rows) != 1:
            raise RuntimeError("N64 QuantLib replication protocol mismatch")
        return rows[0]

    replications = [one_replication(replication) for replication in range(16)]

    contract = {
        "basis": "64", "first_tick": "1", "step_tick": "1", "n": "64",
        "s0": "0x1.9p+6", "strike": "0x1.9p+6", "q": "0x0p+0",
        "r": "0x1.eb851eb851eb8p-6", "sigma": "0x1.999999999999ap-3",
    }
    seeds = set()
    for replication, row in enumerate(replications):
        if (row.get("times_exact") != "YES" or row.get("bridge") != "YES" or
                row.get("antithetic") != "YES" or row.get("control") != "YES" or
                int(row["samples"]) != samples or int(row["level"]) != level or
                int(row["replication"]) != replication):
            raise RuntimeError(
                f"N64 QuantLib configuration mismatch replication={replication}")
        for key, expected in contract.items():
            if row.get(key) != expected:
                raise RuntimeError(
                    f"N64 QuantLib contract mismatch replication={replication} "
                    f"key={key}")
        seeds.add(row["seed"])
    if len(seeds) != 16:
        raise RuntimeError("N64 QuantLib replication seeds are not unique")

    by_option = {}
    for option in ("call", "put"):
        values = [number(row[option]) for row in replications]
        mean = math.fsum(values) / 16.0
        variance = math.fsum((value - mean) ** 2 for value in values) / 15.0
        stderr = math.sqrt(variance / 16.0)
        half_width = 2.131449545559323 * stderr
        by_option[option] = {
            "option": option, "samples": str(samples), "replications": "16",
            "level": str(level), "mean": mean.hex(), "stderr": stderr.hex(),
            "ci_low": (mean - half_width).hex(),
            "ci_high": (mean + half_width).hex(), "times_exact": "YES",
            "bridge": "YES", "antithetic": "YES", "control": "YES",
        }
    return by_option


def reference_decision(previous, current, ours):
    strong = True
    for option in ("call", "put"):
        old = previous[option]
        new = current[option]
        old_mean = number(old["mean"])
        new_mean = number(new["mean"])
        old_half = (number(old["ci_high"]) - number(old["ci_low"])) * 0.5
        new_half = (number(new["ci_high"]) - number(new["ci_low"])) * 0.5
        combined = math.hypot(old_half, new_half)
        if abs(new_mean - old_mean) > combined:
            return False, False
        geocv_error = abs(ours[f"geocv_{option}"] - new_mean)
        if not new_half < 0.25 * geocv_error:
            return False, False
        strong = strong and new_half < 0.10 * geocv_error
    return True, strong


def n64_stage(ours_row, quantlib_command):
    required = ("basis", "first_tick", "step_tick", "n", "s0", "strike",
                "q", "r", "sigma")
    expected = {
        "basis": "64", "first_tick": "1", "step_tick": "1", "n": "64",
        "s0": "0x1.9p+6", "strike": "0x1.9p+6", "q": "0x0p+0",
        "r": "0x1.eb851eb851eb8p-6", "sigma": "0x1.999999999999ap-3",
    }
    for key in required:
        if ours_row.get(key) != expected[key]:
            raise RuntimeError(f"ours N64 contract mismatch key={key}")
    if ours_row.get("q_identity") != "YES":
        raise RuntimeError("ours N64 arithmetic bridge failed")
    ours = {key: number(ours_row[key]) for key in
            ("plain_call", "plain_put", "geocv_call", "geocv_put")}

    levels = []
    all_samples = MANDATORY_LEVELS + EXTENSION_LEVELS
    resolved = False
    strong = False
    for level, samples in enumerate(all_samples):
        current = n64_row(quantlib_command, samples, level)
        levels.append(current)
        if level + 1 < len(MANDATORY_LEVELS):
            continue
        resolved, strong = reference_decision(levels[-2], levels[-1], ours)
        if resolved:
            break

    print("N64_REFERENCE_LEVEL option samples_per_rep replications mean stderr "
          "ci_low ci_high")
    for level in levels:
        for option in ("call", "put"):
            row = level[option]
            print(f"N64_REFERENCE_LEVEL {option} {row['samples']} "
                  f"{row['replications']} {number(row['mean']):.17g} "
                  f"{number(row['stderr']):.17g} {number(row['ci_low']):.17g} "
                  f"{number(row['ci_high']):.17g}")
    if not resolved:
        print("N64_REFERENCE_STATUS REFERENCE_UNRESOLVED")
        raise RuntimeError("N64 reference uncertainty remains unresolved")

    final = levels[-1]
    for option in ("call", "put"):
        row = final[option]
        reference = number(row["mean"])
        plain = ours[f"plain_{option}"]
        geocv = ours[f"geocv_{option}"]
        plain_signed = plain - reference
        geocv_signed = geocv - reference
        plain_abs = abs(plain_signed)
        geocv_abs = abs(geocv_signed)
        reduction = math.inf if geocv_abs == 0.0 else plain_abs / geocv_abs
        print(f"N64_RESULT option={option} reference={reference:.17g} "
              f"reference_ci_low={number(row['ci_low']):.17g} "
              f"reference_ci_high={number(row['ci_high']):.17g}")
        print(f"           ours_plain={plain:.17g} "
              f"plain_signed_error={plain_signed:.17g} plain_abs_error={plain_abs:.17g}")
        print(f"           ours_geocv={geocv:.17g} "
              f"geocv_signed_error={geocv_signed:.17g} geocv_abs_error={geocv_abs:.17g}")
        print(f"           geocv_error_reduction={reduction:.17g}")
    print("N64_REFERENCE_STATUS " +
          ("RESOLVED_STRONG_10_PERCENT" if strong else "RESOLVED_25_PERCENT"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ours", required=True)
    parser.add_argument("--quantlib", required=True)
    parser.add_argument("--sde", default="/opt/intel-sde/sde64")
    args = parser.parse_args()

    ql_command = [args.quantlib]
    ours_command = [args.sde, "-spr", "--", args.ours]
    version_rows = records(run(ql_command + ["--stage", "version"]), "QL_VERSION")
    if len(version_rows) != 1 or version_rows[0].get("version") != "1.39-dev":
        raise RuntimeError("QuantLib version is not 1.39-dev")
    print("quantlib_version 1.39-dev linkage=libQuantLib.so")

    analytic_gate(run(ours_command + ["--stage", "analytic"]),
                  run(ql_command + ["--stage", "analytic"]))

    ours_arithmetic = run(ours_command + ["--stage", "arithmetic"])
    ours_lines = ours_arithmetic.splitlines()
    published_text = "\n".join(
        line for line in ours_lines if line.startswith("OUR_PUBLISHED "))
    n64_text = "\n".join(
        line for line in ours_lines if line.startswith("OUR_N64 "))
    if len(published_text.splitlines()) != 24 or len(n64_text.splitlines()) != 1:
        raise RuntimeError("ours arithmetic protocol is incomplete")
    published_stage(published_text,
                    run(ql_command + ["--stage", "published"]))
    n64_stage(records(n64_text, "OUR_N64")[0], ql_command)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"accuracy_check_failure: {error}", file=sys.stderr)
        sys.exit(1)
