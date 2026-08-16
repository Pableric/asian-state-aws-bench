#!/usr/bin/env python3
"""Compare the reduced-FMA mode with the production Gaussian-exp path."""

from __future__ import annotations

import ctypes
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "libeuropean_pricer.so"


class Request(ctypes.Structure):
    _fields_ = [
        ("s0", ctypes.c_float), ("k", ctypes.c_float),
        ("r", ctypes.c_float), ("sigma", ctypes.c_float),
        ("t", ctypes.c_float), ("num_blocks", ctypes.c_uint64),
        ("type", ctypes.c_int), ("mode", ctypes.c_int),
    ]


class Result(ctypes.Structure):
    _fields_ = [
        ("price", ctypes.c_double), ("payoff_sum", ctypes.c_double),
        ("samples", ctypes.c_uint64),
        ("coeff_setup_seconds", ctypes.c_double),
        ("kernel_seconds", ctypes.c_double),
    ]


ELIGIBLE = (
    (1, 0, 100.0, 100.0, 0.05, 0.2, 1.0),
    (1, 1, 100.0, 100.0, 0.05, 0.2, 1.0),
    (16, 0, 80.0, 105.0, -0.01, 0.4, 0.25),
    (16, 1, 120.0, 95.0, 0.10, 0.1, 0.25),
    (16, 0, 100.0, 130.0, 0.03, 0.2, 1.0),
    (16, 1, 100.0, 70.0, 0.03, 0.2, 1.0),
)
FALLBACK = (
    (1, 0, 100.0, 100.0, 0.05, 0.21, 1.0),
    (1, 1, 100.0, 100.0, 0.05, 0.0, 1.0),
    (16, 0, 80.0, 105.0, 0.01, 0.55, 2.0),
)


def price(lib: ctypes.CDLL, case: tuple[float, ...], mode: int) -> Result:
    blocks, option_type, s0, k, rate, sigma, maturity = case
    request = Request(s0, k, rate, sigma, maturity, blocks, option_type, mode)
    result = Result()
    rc = lib.price_european(ctypes.byref(request), ctypes.byref(result))
    if rc != 0:
        raise RuntimeError(f"price_european failed: rc={rc}, case={case}, mode={mode}")
    return result


def prepared_prices(
    lib: ctypes.CDLL, case: tuple[float, ...], repeats: int = 2
) -> list[Result]:
    blocks, option_type, s0, k, rate, sigma, maturity = case
    request = Request(s0, k, rate, sigma, maturity, blocks, option_type, 8)
    prepared = ctypes.c_void_p()
    rc = lib.european_prepare(ctypes.byref(request), ctypes.byref(prepared))
    if rc != 0 or not prepared.value:
        raise RuntimeError(f"european_prepare failed: rc={rc}, case={case}")
    results = []
    try:
        for _ in range(repeats):
            result = Result()
            rc = lib.european_price_prepared(prepared, ctypes.byref(result))
            if rc != 0:
                raise RuntimeError(
                    f"european_price_prepared failed: rc={rc}, case={case}")
            results.append(result)
    finally:
        lib.european_prepared_destroy(prepared)
    return results


def main() -> int:
    lib = ctypes.CDLL(str(LIB))
    lib.price_european.argtypes = [ctypes.POINTER(Request), ctypes.POINTER(Result)]
    lib.price_european.restype = ctypes.c_int
    lib.european_prepare.argtypes = [
        ctypes.POINTER(Request), ctypes.POINTER(ctypes.c_void_p)]
    lib.european_prepare.restype = ctypes.c_int
    lib.european_price_prepared.argtypes = [ctypes.c_void_p, ctypes.POINTER(Result)]
    lib.european_price_prepared.restype = ctypes.c_int
    lib.european_prepared_destroy.argtypes = [ctypes.c_void_p]
    lib.european_prepared_destroy.restype = None
    worst = 0.0
    for case in ELIGIBLE:
        baseline = price(lib, case, 1)
        reduced = price(lib, case, 8)
        difference = abs(reduced.price - baseline.price)
        worst = max(worst, difference)
        if baseline.samples != reduced.samples or not math.isfinite(reduced.price):
            raise AssertionError((case, baseline.samples, reduced.samples, reduced.price))
        if difference > 2.0e-5:
            raise AssertionError(f"eligible mismatch={difference:.12g}, case={case}")
        prepared = prepared_prices(lib, case)
        if any(
            item.price != reduced.price or item.payoff_sum != reduced.payoff_sum
            or item.samples != reduced.samples or item.coeff_setup_seconds != 0.0
            for item in prepared
        ):
            raise AssertionError(f"prepared eligible mismatch: case={case}")
        print(f"eligible case={case} difference={difference:.12g}")
    for case in FALLBACK:
        baseline = price(lib, case, 1)
        reduced = price(lib, case, 8)
        if baseline.price != reduced.price or baseline.payoff_sum != reduced.payoff_sum:
            raise AssertionError(f"fallback is not exact: case={case}")
        prepared = prepared_prices(lib, case)
        if any(
            item.price != reduced.price or item.payoff_sum != reduced.payoff_sum
            or item.samples != reduced.samples or item.coeff_setup_seconds != 0.0
            for item in prepared
        ):
            raise AssertionError(f"prepared fallback mismatch: case={case}")
        print(f"fallback case={case} exact=1")
    print(f"worst_eligible_price_difference={worst:.12g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
