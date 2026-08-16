#!/usr/bin/env python3
"""Authenticated localhost service for sanitized block-builder evaluation."""

from __future__ import annotations

import argparse
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading
from typing import Any

from evaluator import EvaluationError, evaluate_fragment

MAX_REQUEST_BYTES = 160 * 1024


def format_report(result: dict[str, Any]) -> str:
    digest = result.get("candidate_sha256", "unknown")
    status = result.get("status", "ERROR")
    lines = ["SOBOL BLOCK BUILDER PRIVATE EVALUATION", f"candidate_sha256={digest}",
             f"status={status}"]
    block = result.get("block")
    if isinstance(block, dict):
        for key in ("block_values", "block_bytes", "packets", "mismatches",
                    "guard_failures", "cycles_per_block", "cycles_per_value"):
            if key in block:
                lines.append(f"{key}={block[key]}")
    baseline = result.get("private_engine_baseline")
    if isinstance(baseline, dict):
        lines.append("private_engine_baseline=PASS")
        for key in ("points", "price", "abs_err", "ns_per_sample"):
            if key in baseline:
                lines.append(f"baseline_{key}={baseline[key]}")
    lines.append("l1d_budget=16KiB selected block; D1+selected=32KiB of assumed 48KiB")
    lines.append("integration=standalone block builder; final engine fusion remains manual")
    return "\n".join(lines)


def parse_result_line(output: str) -> dict[str, str]:
    lines = [line for line in output.splitlines() if line.startswith("RESULT ")]
    if len(lines) != 1:
        raise EvaluationError("private baseline returned no trusted RESULT line")
    result: dict[str, str] = {}
    for field in lines[0].split()[1:]:
        if "=" in field:
            key, value = field.split("=", 1)
            result[key] = value
    required = {"points", "price", "abs_err", "ns_per_sample"}
    if not required.issubset(result):
        raise EvaluationError("private baseline result is incomplete")
    return {key: result[key] for key in sorted(required)}


def run_private_baseline(executable: Path, timeout: float) -> dict[str, str]:
    process = subprocess.run(
        [str(executable), "8192", "call", "100", "100", "0.05", "0.2", "1", "16"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
        env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"},
    )
    if process.returncode != 0:
        raise EvaluationError("private baseline execution failed")
    return parse_result_line(process.stdout)


class EvaluationServer(ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], handler: type[BaseHTTPRequestHandler],
                 token: str, harness_dir: Path, baseline: Path | None,
                 timeout: float, unsafe_local: bool):
        super().__init__(address, handler)
        self.token = token
        self.harness_dir = harness_dir
        self.baseline = baseline
        self.timeout = timeout
        self.unsafe_local = unsafe_local
        self.evaluation_lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    server: EvaluationServer

    def log_message(self, format: str, *args: object) -> None:
        # Do not put candidate-controlled content in logs.
        super().log_message("%s", format % args)

    def send_json(self, status: int, value: dict[str, Any]) -> None:
        body = json.dumps(value, sort_keys=True).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def authenticated(self) -> bool:
        expected = "Bearer " + self.server.token
        return hmac.compare_digest(self.headers.get("Authorization", ""), expected)

    def do_GET(self) -> None:
        if self.path != "/healthz":
            self.send_json(404, {"status": "NOT_FOUND"})
            return
        self.send_json(200, {"status": "OK"})

    def do_POST(self) -> None:
        if self.path != "/v1/evaluate":
            self.send_json(404, {"status": "NOT_FOUND"})
            return
        if not self.authenticated():
            self.send_json(401, {"status": "UNAUTHORIZED"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = 0
        if length <= 0 or length > MAX_REQUEST_BYTES:
            self.send_json(413, {"status": "REQUEST_REJECTED"})
            return
        try:
            request = json.loads(self.rfile.read(length))
            candidate = request["candidate"]
            claimed_sha256 = request["sha256"]
            if not isinstance(candidate, str) or not isinstance(claimed_sha256, str):
                raise ValueError
            with self.server.evaluation_lock:
                result = evaluate_fragment(
                    candidate,
                    self.server.harness_dir,
                    claimed_sha256,
                    self.server.timeout,
                    self.server.unsafe_local,
                )
                if self.server.baseline is not None:
                    result["private_engine_baseline"] = run_private_baseline(
                        self.server.baseline, self.server.timeout)
            result["report"] = format_report(result)
            self.send_json(200, result)
        except (KeyError, ValueError, json.JSONDecodeError, EvaluationError,
                OSError, UnicodeError, subprocess.TimeoutExpired):
            # Never return compiler output, filesystem paths, or exception text.
            self.send_json(400, {"status": "REQUEST_REJECTED"})


def load_token(args: argparse.Namespace) -> str:
    if args.token_file:
        token = args.token_file.read_text(encoding="utf-8").strip()
    else:
        token = os.environ.get("SOBOL_FRAGMENT_TOKEN", "").strip()
    if len(token) < 32:
        raise SystemExit("token must contain at least 32 characters")
    return token


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--token-file", type=Path)
    parser.add_argument("--harness-dir", type=Path,
                        default=Path(__file__).with_name("public_harness"))
    parser.add_argument("--baseline", type=Path,
                        default=Path(__file__).resolve().parents[2] / "bench_european_points")
    parser.add_argument("--skip-baseline", action="store_true")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--unsafe-local-evaluator", action="store_true",
                        help="development only; never use for delegated submissions")
    args = parser.parse_args()
    token = load_token(args)
    baseline = None if args.skip_baseline else args.baseline.resolve()
    if baseline is not None and not baseline.is_file():
        raise SystemExit("build bench_european_points before starting the service")
    server = EvaluationServer(
        (args.listen, args.port), Handler, token, args.harness_dir.resolve(),
        baseline, args.timeout, args.unsafe_local_evaluator,
    )
    print(f"block-builder evaluator listening on {args.listen}:{args.port}", flush=True)
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
