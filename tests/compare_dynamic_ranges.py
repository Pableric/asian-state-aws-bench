#!/usr/bin/env python3
"""Compare the additive dynamic-range mode with the production Gaussian path."""

from __future__ import annotations

import ctypes
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "libeuropean_pricer.so"


class Request(ctypes.Structure):
    _fields_ = [
        ("s0", ctypes.c_float),
        ("k", ctypes.c_float),
        ("r", ctypes.c_float),
        ("sigma", ctypes.c_float),
        ("t", ctypes.c_float),
        ("num_blocks", ctypes.c_uint64),
        ("type", ctypes.c_int),
        ("mode", ctypes.c_int),
    ]


class Result(ctypes.Structure):
    _fields_ = [
        ("price", ctypes.c_double),
        ("payoff_sum", ctypes.c_double),
        ("samples", ctypes.c_uint64),
        ("coeff_setup_seconds", ctypes.c_double),
        ("kernel_seconds", ctypes.c_double),
    ]


CASES = (
    (1, 0, 100.0, 100.0, 0.05, 0.2, 1.0),
    (1, 1, 100.0, 100.0, 0.05, 0.2, 1.0),
    (16, 0, 120.0, 95.0, -0.01, 0.35, 0.25),
    (16, 1, 80.0, 105.0, 0.01, 0.55, 2.0),
)


def price(lib: ctypes.CDLL, case: tuple[float, ...], mode: int) -> Result:
    blocks, option_type, s0, k, r, sigma, t = case
    request = Request(s0, k, r, sigma, t, blocks, option_type, mode)
    result = Result()
    rc = lib.price_european(ctypes.byref(request), ctypes.byref(result))
    if rc != 0:
        raise RuntimeError(f"price_european failed: rc={rc}, mode={mode}, case={case}")
    return result


def main() -> int:
    lib = ctypes.CDLL(str(LIB))
    lib.price_european.argtypes = [ctypes.POINTER(Request), ctypes.POINTER(Result)]
    lib.price_european.restype = ctypes.c_int
    worst = 0.0
    for case in CASES:
        baseline = price(lib, case, 1)
        dynamic = price(lib, case, 7)
        difference = abs(dynamic.price - baseline.price)
        worst = max(worst, difference)
        if baseline.samples != dynamic.samples:
            raise AssertionError((baseline.samples, dynamic.samples, case))
        if not math.isfinite(dynamic.price) or difference > 2.0e-5:
            raise AssertionError(
                f"dynamic mismatch: difference={difference:.12g}, case={case}"
            )
        print(
            f"blocks={case[0]} type={'put' if case[1] else 'call'} "
            f"baseline={baseline.price:.12g} dynamic={dynamic.price:.12g} "
            f"difference={difference:.12g}"
        )
    print(f"worst_price_difference={worst:.12g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
