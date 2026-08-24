#!/usr/bin/env python3

import argparse
import hashlib
import re
import subprocess
import sys


ROOTS = {
    "asian_n64_clean_plan_lookup",
    "asian_n64_clean_carrier_prepare",
    "asian_n64_clean_request_prepare",
    "asian_n64_clean_prepared_price",
    "asian_n64_clean_fresh_total",
    "asian_n64_clean_reuse_total",
}

FORBIDDEN = re.compile(
    r"sha256|fused_source_exp_prepare|arithmetic_growth_only_prepare|"
    r"route_plan_create|malloc|calloc|posix_memalign|free|build_map|"
    r"prepare_route|map_source",
    re.IGNORECASE,
)

KERNELS = {
    "asian_genuine_arithmetic_fused_source_exp_diag": (
        "fused",
        "5310c4e4033a5d3604e9aa0e4a4917f764e9d7f113554ea355a00011a3df43af",
        "0dd6ed71a7721ffca9fca97b957661687895564e37479ab63992603ac287ab83",
    ),
    "asian_genuine_arithmetic_growth_only_q_diag": (
        "growth",
        "3e30b285ff48b2179f86b7f345ccffd3f85ecc35f9d2d14a0f0801883ba66d8d",
        "ad613c8bad7575520901a8e6f50bfd379214d0edf200f15eb8e10d62c8915a4a",
    ),
    "asian_genuine_strip_arithmetic_price_1_diag": (
        "strip",
        "d8959ebb9a13b890cfc920573d4f8019128c9e30d49e6a37c963915162e1f0c6",
        "0822fecb09930e37296e461f411bbff38cab63b8a92162536e3dc2b740f7b2f5",
    ),
}


def output(*args):
    return subprocess.check_output(args, text=True)


def functions(disassembly):
    matches = list(re.finditer(r"^([0-9a-f]+) <([^>]+)>:$", disassembly,
                               re.MULTILINE))
    result = {}
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(disassembly)
        result[match.group(2)] = disassembly[match.end():end]
    return result


def direct_calls(body):
    return set(re.findall(r"\bcallq?\s+[0-9a-f]+\s+<([^>@+]+)", body))


def reachable(graph, roots):
    seen = set()
    stack = list(roots)
    while stack:
        name = stack.pop()
        if name in seen:
            continue
        seen.add(name)
        stack.extend(graph.get(name, ()))
    return seen


def canonical_body(object_path, symbol):
    text = output("objdump", "-dr", "-Mintel", "--disassemble=" + symbol,
                  object_path)
    lines = []
    for line in text.splitlines():
        if re.match(r"^\s*[0-9a-f]+:", line):
            fields = line.split("\t")
            if len(fields) >= 3:
                instruction = fields[-1]
                instruction = re.sub(r"\s+#\s+[0-9a-f]+.*$", "", instruction)
                lines.append(re.sub(r"\s+", " ", instruction.strip()))
        elif "R_X86_64_" in line:
            lines.append(re.sub(r"^\s*[0-9a-f]+:\s+", "", line.strip()))
    return "\n".join(lines).encode()


def text_bytes(object_path, symbol):
    symbols = output("nm", "-S", "--defined-only", object_path)
    match = re.search(rf"^([0-9a-f]+)\s+([0-9a-f]+)\s+\w\s+{re.escape(symbol)}$",
                      symbols, re.MULTILINE)
    if not match:
        raise RuntimeError(f"missing kernel symbol {symbol}")
    first, size = int(match.group(1), 16), int(match.group(2), 16)
    dump = output("objdump", "-s", "-j", ".text", object_path)
    data = bytearray()
    base = None
    for line in dump.splitlines():
        fields = line.split()
        if not fields or not re.fullmatch(r"[0-9a-f]+", fields[0]):
            continue
        address = int(fields[0], 16)
        if base is None:
            base = address
        for word in fields[1:5]:
            if not re.fullmatch(r"(?:[0-9a-f]{2}){1,4}", word):
                break
            data.extend(bytes.fromhex(word))
    if base is None or first < base or first + size > base + len(data):
        raise RuntimeError(f"cannot slice kernel {symbol}")
    return bytes(data[first - base:first - base + size])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--fused-object", required=True)
    parser.add_argument("--growth-object", required=True)
    parser.add_argument("--strip-object", required=True)
    args = parser.parse_args()

    disassembly = output("objdump", "-d", "-Mintel", args.binary)
    bodies = functions(disassembly)
    missing = ROOTS - bodies.keys()
    if missing:
        raise RuntimeError(f"missing clean roots: {sorted(missing)}")
    graph = {name: direct_calls(body) for name, body in bodies.items()}
    clean_graph = reachable(graph, ROOTS)
    bad = sorted(name for name in clean_graph if FORBIDDEN.search(name))
    if bad:
        raise RuntimeError(f"forbidden clean call graph: {bad}")

    object_by_key = {
        "fused": args.fused_object,
        "growth": args.growth_object,
        "strip": args.strip_object,
    }
    for symbol, (key, expected_text, expected_canonical) in KERNELS.items():
        raw_hash = hashlib.sha256(text_bytes(object_by_key[key], symbol)).hexdigest()
        canonical_hash = hashlib.sha256(
            canonical_body(object_by_key[key], symbol)).hexdigest()
        if raw_hash != expected_text or canonical_hash != expected_canonical:
            raise RuntimeError(
                f"kernel changed {symbol} text={raw_hash} canonical={canonical_hash}")
    print("lifecycle_audit PASS kernels_unchanged=YES")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"lifecycle_audit FAIL {error}", file=sys.stderr)
        raise SystemExit(1)
