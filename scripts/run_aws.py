#!/usr/bin/env python3
"""Run isolated Asian S/Q state tests and native benches. Stdlib only."""

from __future__ import annotations

import hashlib
import json
import os
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ALLOWLIST = (
    ".gitignore",
    "BUILD_METADATA.json",
    "LICENSE",
    "README.md",
    "bin/asian_state_bench",
    "bin/asian_state_test",
    "scripts/run_aws.py",
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


def verdict(cycles: float | None, packet_major: bool) -> str:
    if cycles is None:
        return "UNSUPPORTED"
    if not packet_major:
        if cycles <= 6:
            return "EXCELLENT"
        if cycles <= 8:
            return "REVIEW"
        return "INVESTIGATE"
    if cycles <= 6:
        return "EXCELLENT"
    if cycles <= 8:
        return "REVIEW"
    return "INVESTIGATE"


def pick_metric(rows: list[dict[str, object]], env: str, candidate: str, metric: str) -> float | None:
    for row in rows:
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


def main() -> int:
    os.environ["PYTHONDONTWRITEBYTECODE"] = "1"
    root = repo_root()
    print(f"repo_root={root}")
    if platform.system() != "Linux" or platform.machine() not in {"x86_64", "AMD64"}:
        fail("requires Linux x86-64")
    hashes = verify_checksums(root)
    meta_path = root / "BUILD_METADATA.json"
    try:
        build_metadata = json.loads(meta_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"BUILD_METADATA.json unreadable: {exc}")
    if not isinstance(build_metadata, dict):
        fail("BUILD_METADATA.json must be an object")
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
    test_bin = root / "bin" / "asian_state_test"
    bench_bin = root / "bin" / "asian_state_bench"
    print("ldd asian_state_test:")
    print(ldd(test_bin))
    print("ldd asian_state_bench:")
    print(ldd(bench_bin))
    selected = pin_cpu()

    if "avx512f" not in flags:
        print("UNSUPPORTED: CPU lacks avx512f; not launching binaries")
        print("variant | environment | cycles/32-path-fixing | cycles/4096-fixing | cycles/4096x32 | verdict")
        print("all | n/a | n/a | n/a | n/a | UNSUPPORTED")
        print("NATIVE DATA COLLECTED — PRODUCTION SELECTION REQUIRES REVIEW")
        return 2

    test = run_bin(test_bin, root)
    sys.stdout.write(test.stdout)
    sys.stderr.write(test.stderr)
    if test.returncode != 0 or "ASIAN_STATE_TEST PASS" not in test.stdout.splitlines():
        fail("correctness binary failed")

    bench = run_bin(bench_bin, root)
    sys.stdout.write(bench.stdout)
    sys.stderr.write(bench.stderr)
    if bench.returncode != 0:
        fail("benchmark binary failed")
    rows = parse_results(bench.stdout)
    if not rows:
        fail("benchmark produced no RESULT lines")

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
        "binary_hashes": hashes,
        "build_metadata": build_metadata,
        "build_time_static_audit": build_metadata.get("build_time_static_audit"),
        "correctness_status": "PASS",
        "benchmark_measurements": rows,
        "raw_batches": {
            f"{row.get('env')}/{row.get('candidate')}/{row.get('metric')}": row["raw_batches"]
            for row in rows
            if isinstance(row.get("raw_batches"), list)
        },
        "raw_result_lines": [ln for ln in bench.stdout.splitlines() if ln.startswith("RESULT ")],
        "utc_timestamp": stamp,
        "test_stdout": test.stdout,
        "bench_stdout": bench.stdout,
    }
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {out_path}")

    print()
    print("variant | environment | cycles/32-path-fixing | cycles/4096-fixing | cycles/4096x32 | verdict")
    for env in ("warm_L1D", "pressure_32KiB"):
        for cand in (
            "packet_1x",
            "packet_2x",
            "packet_4x",
            "timestep_1x",
            "timestep_2x",
            "timestep_4x",
        ):
            c32 = pick_metric(rows, env, cand, "cycles_per_32path_fixing")
            c4096 = pick_metric(rows, env, cand, "cycles_per_4096_state_fixing")
            call = pick_metric(rows, env, cand, "cycles_4096_by_32fix")
            packet = cand.startswith("packet_")
            v = verdict(c32, packet)
            def fmt(x: float | None) -> str:
                return "n/a" if x is None else f"{x:.3f}"

            print(f"{cand} | {env} | {fmt(c32)} | {fmt(c4096)} | {fmt(call)} | {v}")
    print("NATIVE DATA COLLECTED — PRODUCTION SELECTION REQUIRES REVIEW")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        raise SystemExit(1)
