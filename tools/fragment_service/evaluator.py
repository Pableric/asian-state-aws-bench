#!/usr/bin/env python3
"""Trusted evaluator for an untrusted Sobol assembly include.

The candidate is compiled and executed with only the public harness mounted in
the sandbox. Private engine code is never mounted into the candidate sandbox.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from typing import Any

MAX_CANDIDATE_BYTES = 128 * 1024
REPORT_PREFIX = "FRAGMENT_REPORT_JSON "
REQUIRED_MACROS = (
    "D1_FRAG_PERMD",
    "D1_FRAG_PERMI2D",
    "D1_FRAG_GEN",
    "D1_FRAG_GEN_LOAD",
)


class EvaluationError(RuntimeError):
    pass


def validate_candidate(candidate: str, claimed_sha256: str | None = None) -> str:
    raw = candidate.encode("utf-8")
    if not raw or len(raw) > MAX_CANDIDATE_BYTES or "\0" in candidate:
        raise EvaluationError("candidate size or encoding is invalid")
    digest = hashlib.sha256(raw).hexdigest()
    if claimed_sha256 and claimed_sha256 != digest:
        raise EvaluationError("candidate SHA-256 does not match request")
    missing = [name for name in REQUIRED_MACROS if f".macro {name}" not in candidate]
    if missing:
        raise EvaluationError("candidate does not implement the complete contract")
    # These directives can import host files at assembly time. Candidate code
    # still runs in a filesystem sandbox, but rejecting them also keeps the
    # include self-contained and auditable.
    if re.search(r"(?mi)^\s*\.(?:include|incbin)\b", candidate):
        raise EvaluationError("candidate may not include external files")
    return digest


def sandbox_prefix(stage: Path) -> list[str]:
    bwrap = shutil.which("bwrap")
    if not bwrap:
        raise EvaluationError("bubblewrap is required by the protected evaluator")
    return [
        bwrap,
        "--die-with-parent",
        "--unshare-all",
        "--new-session",
        "--ro-bind", "/usr", "/usr",
        "--ro-bind", "/etc", "/etc",
        "--symlink", "usr/bin", "/bin",
        "--symlink", "usr/lib", "/lib",
        "--symlink", "usr/lib", "/lib64",
        "--proc", "/proc",
        "--dev", "/dev",
        "--tmpfs", "/tmp",
        "--bind", str(stage), "/work",
        "--chdir", "/work",
    ]


def run_limited(
    command: list[str], timeout: float, cwd: Path | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
        cwd=cwd,
        env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
    )


def parse_fragment_report(output: str) -> dict[str, Any]:
    reports = [line[len(REPORT_PREFIX):] for line in output.splitlines()
               if line.startswith(REPORT_PREFIX)]
    if len(reports) != 1:
        raise EvaluationError("candidate harness did not produce one trusted report")
    try:
        report = json.loads(reports[0])
    except json.JSONDecodeError as error:
        raise EvaluationError("candidate harness report is malformed") from error
    if report.get("status") not in {"PASS", "FAIL", "UNSUPPORTED"}:
        raise EvaluationError("candidate harness returned an invalid status")
    return report


def evaluate_fragment(
    candidate: str,
    harness_dir: Path,
    claimed_sha256: str | None,
    timeout: float,
    unsafe_local: bool = False,
) -> dict[str, Any]:
    digest = validate_candidate(candidate, claimed_sha256)
    required = ("Makefile", "fragment_wrappers.s", "fragment_harness.c")
    if any(not (harness_dir / name).is_file() for name in required):
        raise EvaluationError("trusted public harness is incomplete")

    with tempfile.TemporaryDirectory(prefix="sobol-fragment-") as directory:
        stage = Path(directory)
        for name in required:
            shutil.copyfile(harness_dir / name, stage / name)
        (stage / "candidate_fragment.inc").write_text(candidate, encoding="utf-8")

        prefix = [] if unsafe_local else sandbox_prefix(stage)
        build = run_limited(prefix + ["/usr/bin/make", "all"], timeout,
                            None if prefix else stage)
        if build.returncode != 0:
            return {
                "status": "BUILD_FAIL",
                "candidate_sha256": digest,
                "diagnostic_sha256": hashlib.sha256(
                    (build.stdout + build.stderr).encode()).hexdigest(),
            }

        run = run_limited(prefix + ["/work/fragment_harness"] if prefix
                          else [str(stage / "fragment_harness")], timeout,
                          None if prefix else stage)
        try:
            fragment = parse_fragment_report(run.stdout)
        except EvaluationError:
            return {
                "status": "RUN_FAIL",
                "candidate_sha256": digest,
                "exit_code": run.returncode,
            }
        return {
            "status": fragment["status"],
            "candidate_sha256": digest,
            "fragment": fragment,
        }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--harness-dir", type=Path,
                        default=Path(__file__).with_name("public_harness"))
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--unsafe-local", action="store_true",
                        help="development only: disables the filesystem sandbox")
    args = parser.parse_args()
    try:
        candidate = args.candidate.read_text(encoding="utf-8")
        result = evaluate_fragment(candidate, args.harness_dir, None,
                                   args.timeout, args.unsafe_local)
    except (OSError, UnicodeError, EvaluationError, subprocess.TimeoutExpired) as error:
        result = {"status": "ERROR", "reason": str(error)}
    print(json.dumps(result, sort_keys=True))
    return 0 if result.get("status") == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
