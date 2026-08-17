#!/usr/bin/env python3
"""Run isolated Asian S/Q and dim-permute tests/benches. Stdlib only."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shlex
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ALLOWLIST = (
    ".gitignore",
    "BUILD_METADATA.json",
    "BUILD_METADATA_dim.json",
    "LICENSE",
    "README.md",
    "bin/asian_state_bench",
    "bin/asian_state_test",
    "bin/dim_permute_bench",
    "bin/dim_permute_test",
    "real_block_maps.bin",
    "scripts/run_aws.py",
)

ASIAN_CANDIDATES = (
    "packet_1x",
    "packet_2x",
    "packet_4x",
    "timestep_1x",
    "timestep_2x",
    "timestep_4x",
)

DIM_CANDIDATES = (
    "generic",
    "affine",
    "res2xor",
)

DIM_REAL_DIMS = (
    1,
    5,
    6,
    7,
    8,
    9,
    11,
    13,
    15,
    17,
    19,
    21,
    23,
    26,
    27,
    28,
    29,
    31,
)


def fail(message: str, code: int = 1) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(code)


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 16), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_sha256sums(path: Path) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2:
            fail(f"malformed SHA256SUMS line: {raw}")
        digest, rel = parts[0], parts[-1]
        if rel.startswith("*") or rel.startswith("./"):
            rel = rel.lstrip("*").lstrip("./")
        mapping[rel] = digest
    return mapping


def verify_checksums(root: Path) -> dict[str, str]:
    sums_path = root / "SHA256SUMS"
    if not sums_path.is_file():
        fail("SHA256SUMS missing")
    listed = parse_sha256sums(sums_path)
    expected = set(ALLOWLIST)
    if set(listed.keys()) != expected:
        fail(f"SHA256SUMS set mismatch: {sorted(listed)} vs {sorted(expected)}")
    hashes: dict[str, str] = {}
    for rel, expected_digest in listed.items():
        path = (root / rel).resolve()
        try:
            path.relative_to(root.resolve())
        except ValueError:
            fail(f"checksum path escaped root: {rel}")
        if path.is_symlink():
            fail(f"symlink rejected: {rel}")
        if not path.is_file():
            fail(f"missing artifact: {rel}")
        actual = sha256_file(path)
        if actual != expected_digest:
            fail(f"checksum mismatch: {rel}")
        hashes[rel] = actual
    return hashes


def cpu_info() -> tuple[str, list[str]]:
    model = "unknown"
    flags: list[str] = []
    try:
        text = Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace")
    except OSError:
        return model, flags
    for line in text.splitlines():
        if line.startswith("model name") and ":" in line and model == "unknown":
            model = line.split(":", 1)[1].strip()
        if line.startswith("flags") and ":" in line and not flags:
            flags = line.split(":", 1)[1].split()
    return model, flags


def avx512_flags(flags: list[str]) -> list[str]:
    return [f for f in flags if f.startswith("avx512")]


def read_cache() -> list[dict[str, str]]:
    caches = []
    base = Path("/sys/devices/system/cpu/cpu0/cache")
    if not base.is_dir():
        return caches
    for index in sorted(base.glob("index*")):
        def one(name: str) -> str:
            p = index / name
            try:
                return p.read_text(encoding="utf-8").strip()
            except OSError:
                return "unsupported"

        caches.append(
            {
                "index": index.name,
                "level": one("level"),
                "type": one("type"),
                "size": one("size"),
            }
        )
    return caches


def ldd(path: Path) -> str:
    try:
        return subprocess.run(
            ["ldd", str(path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unavailable"


def pin_cpu() -> int:
    if not hasattr(os, "sched_getaffinity") or not hasattr(os, "sched_setaffinity"):
        print("cpu_affinity=unsupported")
        return -1
    allowed = os.sched_getaffinity(0)
    if not allowed:
        fail("empty CPU affinity set")
    selected = 0 if 0 in allowed else min(allowed)
    os.sched_setaffinity(0, {selected})
    print(f"affinity_allowed={sorted(allowed)}")
    print(f"selected_cpu={selected}")
    return selected


def run_bin(path: Path, root: Path) -> subprocess.CompletedProcess[str]:
    resolved = path.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError:
        fail(f"refusing to execute binary outside repo: {path}")
    if resolved.is_symlink():
        fail(f"refusing to execute symlink: {path}")
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        fail(f"binary not executable: {path}")
    return subprocess.run(
        [str(resolved)],
        cwd=str(root),
        capture_output=True,
        text=True,
    )


def parse_results(stdout: str) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for line in stdout.splitlines():
        if not line.startswith("RESULT "):
            continue
        blob = line[len("RESULT ") :].strip()
        try:
            obj = json.loads(blob)
        except json.JSONDecodeError as exc:
            fail(f"malformed RESULT line: {exc}: {blob[:200]}")
        if not isinstance(obj, dict) or "kind" not in obj:
            fail(f"RESULT missing kind: {blob[:200]}")
        rows.append(obj)
    return rows


def parse_kv_line(line: str) -> dict[str, object]:
    fields: dict[str, object] = {}
    for token in shlex.split(line):
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def parse_dim_results(stdout: str) -> list[dict[str, object]]:
    """Parse and validate Junior's complete dest-order real-map benchmark."""
    rows: list[dict[str, object]] = []
    aggregates: dict[tuple[str, str], dict[str, object]] = {}
    real_rows: set[tuple[int, str, str]] = set()
    native_banner = False

    for line in stdout.splitlines():
        if line.startswith("dim_provider_bench native_avx512=1 "):
            native_banner = True
        if line.startswith("map="):
            row = parse_kv_line(line)
            row["kind"] = "dim_native"
            rows.append(row)
            if row.get("map") == "real" and row.get("mode") in {
                "warm_L1D",
                "pressure_32KiB",
            }:
                try:
                    key = (int(str(row["dim"])), str(row["variant"]), str(row["mode"]))
                    median = float(str(row["median_cyc"]))
                    p10 = float(str(row["p10"]))
                    p90 = float(str(row["p90"]))
                    float(str(row["cyc_per_128_block"]))
                    float(str(row["cyc_per_packet"]))
                except (KeyError, TypeError, ValueError) as exc:
                    fail(f"malformed real dimension row: {line}: {exc}")
                if not p10 <= median <= p90:
                    fail(f"invalid percentile order: {line}")
                if key in real_rows:
                    fail(f"duplicate real dimension row: {key}")
                real_rows.add(key)
        elif line.startswith("aggregate "):
            row = parse_kv_line(line)
            row["kind"] = "dim_aggregate_native"
            rows.append(row)
            try:
                key = (str(row["variant"]), str(row["mode"]))
                float(str(row["real_median_cyc"]))
                float(str(row["real_median_cyc_per_packet"]))
                worst_dim = int(str(row["worst_dim"]))
                float(str(row["worst_median_cyc"]))
                float(str(row["worst_cyc_per_packet"]))
                n_dims = int(str(row["n_dims"]))
            except (KeyError, TypeError, ValueError) as exc:
                fail(f"malformed dimension aggregate: {line}: {exc}")
            if row.get("map") != "real":
                fail(f"non-real map included in aggregate: {line}")
            if worst_dim not in DIM_REAL_DIMS or n_dims != len(DIM_REAL_DIMS):
                fail(f"invalid aggregate dimension coverage: {line}")
            if key in aggregates:
                fail(f"duplicate dimension aggregate: {key}")
            aggregates[key] = row
        elif line.startswith(("host ", "cache ", "timing_envelope ")):
            row = parse_kv_line(line)
            row["kind"] = line.split()[0]
            rows.append(row)

    if not native_banner:
        fail("dimension benchmark missing native AVX-512 banner")
    expected_rows = {
        (dim, candidate, env)
        for dim in DIM_REAL_DIMS
        for candidate in DIM_CANDIDATES
        for env in ("warm_L1D", "pressure_32KiB")
    }
    if real_rows != expected_rows:
        missing = sorted(expected_rows - real_rows)
        extra = sorted(real_rows - expected_rows)
        fail(f"dimension real-map coverage mismatch: missing={missing} extra={extra}")
    expected_aggregates = {
        (candidate, env)
        for candidate in DIM_CANDIDATES
        for env in ("warm_L1D", "pressure_32KiB")
    }
    if set(aggregates) != expected_aggregates:
        fail(
            "dimension aggregate coverage mismatch: "
            f"got={sorted(aggregates)} expected={sorted(expected_aggregates)}"
        )
    return rows


def sanitize(name: str) -> str:
    out = []
    for ch in name.lower():
        if ch.isalnum():
            out.append(ch)
        else:
            out.append("_")
    text = "".join(out).strip("_")
    while "__" in text:
        text = text.replace("__", "_")
    return text or "cpu"


def load_metadata(path: Path) -> dict[str, object]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"{path.name} unreadable: {exc}")
    if not isinstance(payload, dict):
        fail(f"{path.name} must be an object")
    return payload


def pick_metric(
    rows: list[dict[str, object]],
    env: str,
    candidate: str,
    metric: str,
    kind: str | None = None,
) -> float | None:
    for row in rows:
        if kind is not None and row.get("kind") != kind:
            continue
        if (
            row.get("kind") in {"measured_native", "derived_native"}
            and row.get("env") == env
            and row.get("candidate") == candidate
            and row.get("metric") == metric
        ):
            try:
                return float(row["value"])
            except (KeyError, TypeError, ValueError):
                return None
    return None


def fmt(x: float | None) -> str:
    return "n/a" if x is None else f"{x:.3f}"


def asian_verdict(cycles: float | None) -> str:
    if cycles is None:
        return "UNSUPPORTED"
    if cycles <= 6:
        return "EXCELLENT"
    if cycles <= 8:
        return "REVIEW"
    return "INVESTIGATE"


def dim_verdict(cycles: float | None) -> str:
    if cycles is None:
        return "UNSUPPORTED"
    if cycles <= 3:
        return "EXCELLENT"
    if cycles <= 5:
        return "GOOD"
    if cycles <= 7:
        return "REVIEW"
    return "INVESTIGATE"


def print_asian_table(rows: list[dict[str, object]] | None) -> None:
    print("ASIAN W2 (cycles / 32-path-fixing; not a winner pick)")
    hdr = f"{'variant':<14} {'env':<16} {'cyc/32fix':>10} {'cyc/4096':>10} {'cyc/4096x32':>12} {'verdict':<12}"
    print(hdr)
    if rows is None:
        print(f"{'all':<14} {'n/a':<16} {'n/a':>10} {'n/a':>10} {'n/a':>12} {'UNSUPPORTED':<12}")
        return
    for env in ("warm_L1D", "pressure_32KiB"):
        for cand in ASIAN_CANDIDATES:
            c32 = pick_metric(rows, env, cand, "cycles_per_32path_fixing", kind="derived_native")
            if c32 is None:
                c32 = pick_metric(rows, env, cand, "cycles_per_32path_fixing")
            c4096 = pick_metric(rows, env, cand, "cycles_per_4096_state_fixing")
            call = pick_metric(rows, env, cand, "cycles_4096_by_32fix")
            print(
                f"{cand:<14} {env:<16} {fmt(c32):>10} {fmt(c4096):>10} {fmt(call):>12} {asian_verdict(c32):<12}"
            )


def print_dim_table(rows: list[dict[str, object]] | None) -> None:
    print("DIM dest-order real-map aggregate (18 dimensions; not a winner pick)")
    print(
        f"{'candidate':<10} {'env':<16} {'median block':>13} {'median pkt':>11} "
        f"{'worst dim':>9} {'worst block':>12} {'worst pkt':>10} {'verdict':<12}"
    )
    if rows is None:
        print(
            f"{'all':<10} {'n/a':<16} {'n/a':>13} {'n/a':>11} "
            f"{'n/a':>9} {'n/a':>12} {'n/a':>10} {'UNSUPPORTED':<12}"
        )
        return
    for env in ("warm_L1D", "pressure_32KiB"):
        for cand in DIM_CANDIDATES:
            matches = [
                row
                for row in rows
                if row.get("kind") == "dim_aggregate_native"
                and row.get("variant") == cand
                and row.get("mode") == env
            ]
            if len(matches) != 1:
                fail(f"missing dimension aggregate for {cand}/{env}")
            row = matches[0]
            block = float(str(row["real_median_cyc"]))
            pkt = float(str(row["real_median_cyc_per_packet"]))
            worst_dim = int(str(row["worst_dim"]))
            worst_block = float(str(row["worst_median_cyc"]))
            worst_pkt = float(str(row["worst_cyc_per_packet"]))
            print(
                f"{cand:<10} {env:<16} {block:>13.1f} {pkt:>11.3f} "
                f"{worst_dim:>9} {worst_block:>12.1f} {worst_pkt:>10.3f} {dim_verdict(pkt):<12}"
            )


def run_asian_suite(
    name: str,
    test_bin: Path,
    bench_bin: Path,
    pass_banner: str,
    root: Path,
) -> tuple[str, str, list[dict[str, object]]]:
    print(f"ldd {test_bin.name}:")
    print(ldd(test_bin))
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))
    test = run_bin(test_bin, root)
    sys.stderr.write(test.stderr)
    if test.returncode != 0 or pass_banner not in test.stdout.splitlines():
        sys.stdout.write(test.stdout)
        fail(f"{name} correctness binary failed")
    for line in test.stdout.splitlines():
        if line.startswith("DIM_PERMUTE_TEST") or line.startswith("ASIAN_STATE_TEST") or line.startswith("tests="):
            print(line)
    bench = run_bin(bench_bin, root)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail(f"{name} benchmark binary failed")
    rows = parse_results(bench.stdout)
    if not rows:
        fail(f"{name} benchmark produced no RESULT lines")
    print(f"{name} bench: {len(rows)} RESULT rows (tables below)")
    return test.stdout, bench.stdout, rows


def run_dim_suite(root: Path) -> tuple[str, str, list[dict[str, object]]]:
    test_bin = root / "bin" / "dim_permute_test"
    bench_bin = root / "bin" / "dim_permute_bench"
    print(f"ldd {test_bin.name}:")
    print(ldd(test_bin))
    print(f"ldd {bench_bin.name}:")
    print(ldd(bench_bin))

    test = run_bin(test_bin, root)
    sys.stderr.write(test.stderr)
    expected = "dim_provider_test PASS tests=6101 native_avx512=1"
    if test.returncode != 0 or expected not in test.stdout.splitlines():
        sys.stdout.write(test.stdout)
        fail("dim correctness binary failed or reported prepare-only coverage")
    print(expected)

    bench = run_bin(bench_bin, root)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        sys.stdout.write(bench.stdout)
        fail("dim benchmark binary failed")
    rows = parse_dim_results(bench.stdout)
    print(f"dim bench: validated {len(rows)} native records (table below)")
    return test.stdout, bench.stdout, rows


def main() -> int:
    os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
    parser = argparse.ArgumentParser(description="Run isolated AWS bench carriers")
    parser.add_argument(
        "--suite",
        choices=("all", "asian", "dim"),
        default="all",
        help="which component to run (default: all)",
    )
    args = parser.parse_args()

    root = repo_root()
    print(f"repo_root={root}")
    if platform.system() != "Linux" or platform.machine() not in {"x86_64", "AMD64"}:
        fail("requires Linux x86-64")
    hashes = verify_checksums(root)
    asian_meta = load_metadata(root / "BUILD_METADATA.json")
    dim_meta = load_metadata(root / "BUILD_METADATA_dim.json")
    model, flags = cpu_info()
    avx = avx512_flags(flags)
    print(f"cpu_model={model}")
    print(f"avx512_flags={' '.join(avx) if avx else '(none)'}")
    print(f"kernel={platform.release()}")
    print(f"python={sys.version.split()[0]}")
    print(f"arch={platform.machine()}")
    caches = read_cache()
    l1d = "unsupported"
    l1i = "unsupported"
    for cache in caches:
        print(f"cache {cache['index']} level={cache['level']} type={cache['type']} size={cache['size']}")
        if cache["level"] == "1" and cache["type"] == "Data":
            l1d = cache["size"]
        if cache["level"] == "1" and cache["type"] == "Instruction":
            l1i = cache["size"]
    print(f"l1d={l1d}")
    print(f"l1i={l1i}")
    selected = pin_cpu()
    want_asian = args.suite in {"all", "asian"}
    want_dim = args.suite in {"all", "dim"}

    asian_rows: list[dict[str, object]] | None = None
    dim_rows: list[dict[str, object]] | None = None
    asian_test = ""
    asian_bench = ""
    dim_test = ""
    dim_bench = ""

    if "avx512f" not in flags:
        print("UNSUPPORTED: CPU lacks avx512f; not launching binaries")
        if want_asian:
            print_asian_table(None)
        if want_dim:
            print_dim_table(None)
        print("NO NATIVE DATA COLLECTED — AVX-512F HOST REQUIRED")
        return 2

    if want_asian:
        asian_test, asian_bench, asian_rows = run_asian_suite(
            "asian",
            root / "bin" / "asian_state_test",
            root / "bin" / "asian_state_bench",
            "ASIAN_STATE_TEST PASS",
            root,
        )
    if want_dim:
        dim_test, dim_bench, dim_rows = run_dim_suite(root)

    results_dir = root / "results"
    results_dir.mkdir(exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    out_path = results_dir / f"aws_{sanitize(model)}_{stamp}.json"
    payload = {
        "cpu_model": model,
        "cpu_flags": flags,
        "avx512_flags": avx,
        "l1_caches": read_cache(),
        "kernel": platform.release(),
        "python": sys.version.split()[0],
        "arch": platform.machine(),
        "selected_cpu": selected,
        "suite": args.suite,
        "binary_hashes": hashes,
        "build_metadata": asian_meta,
        "build_metadata_dim": dim_meta,
        "build_time_static_audit": asian_meta.get("build_time_static_audit"),
        "build_time_static_audit_dim": dim_meta.get("object_audit"),
        "correctness_status": "PASS",
        "asian": None
        if asian_rows is None
        else {
            "benchmark_measurements": asian_rows,
            "raw_batches": {
                f"{row.get('env')}/{row.get('candidate')}/{row.get('metric')}": row["raw_batches"]
                for row in asian_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "test_stdout": asian_test,
            "bench_stdout": asian_bench,
        },
        "dim": None
        if dim_rows is None
        else {
            "benchmark_measurements": dim_rows,
            "raw_batches": {
                f"{row.get('env')}/{row.get('candidate')}/{row.get('metric')}": row["raw_batches"]
                for row in dim_rows
                if isinstance(row.get("raw_batches"), list)
            },
            "test_stdout": dim_test,
            "bench_stdout": dim_bench,
        },
        "utc_timestamp": stamp,
    }
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {out_path}")
    print()
    if want_asian:
        print_asian_table(asian_rows)
        print()
    if want_dim:
        print_dim_table(dim_rows)
        print()
    print("NATIVE DATA COLLECTED — PRODUCTION SELECTION REQUIRES REVIEW")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        raise SystemExit(1)
