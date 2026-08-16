#!/usr/bin/env python3
from __future__ import annotations

import ctypes
import math
from pathlib import Path

from scipy.special import ndtri


ROOT = Path(__file__).resolve().parents[1]


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


def sobol_word(index: int) -> int:
    gray = index ^ (index >> 1)
    value = 0
    column = 0
    while gray:
        if gray & 1:
            value ^= 1 << (31 - column)
        gray >>= 1
        column += 1
    return value


def exact_ordered_price(req: Request, points: int) -> float:
    s0 = float(req.s0)
    strike = float(req.k)
    rate = float(req.r)
    sigma = float(req.sigma)
    maturity = float(req.t)
    mu = (rate - 0.5 * sigma * sigma) * maturity
    alpha = sigma * math.sqrt(maturity)
    discount = math.exp(-rate * maturity)
    total = 0.0
    for offset in range(points):
        u = sobol_word(8192 + offset) / float(1 << 32)
        z = float(ndtri(u))
        terminal = s0 * math.exp(mu + alpha * z)
        payoff = max(terminal - strike, 0.0) if req.type == 0 else max(strike - terminal, 0.0)
        total += discount * payoff
    return total / points


def main() -> int:
    lib = ctypes.CDLL(str(ROOT / "libeuropean_pricer.so"))
    lib.price_european_points.argtypes = [
        ctypes.POINTER(Request), ctypes.c_uint64, ctypes.POINTER(Result)]
    lib.price_european_points.restype = ctypes.c_int
    lib.price_european.argtypes = [ctypes.POINTER(Request), ctypes.POINTER(Result)]
    lib.price_european.restype = ctypes.c_int
    lib.european_prepare.argtypes = [
        ctypes.POINTER(Request), ctypes.POINTER(ctypes.c_void_p)]
    lib.european_prepare.restype = ctypes.c_int
    lib.european_price_prepared.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(Result)]
    lib.european_price_prepared.restype = ctypes.c_int
    lib.european_price_prepared_points.argtypes = [
        ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(Result)]
    lib.european_price_prepared_points.restype = ctypes.c_int
    lib.european_prepared_destroy.argtypes = [ctypes.c_void_p]
    lib.european_prepared_destroy.restype = None

    req = Request(100.0, 100.0, 0.05, 0.2, 1.0, 0, 0, 9)
    results = {}
    for points in (32, 96, 8192, 8224, 16384):
        result = Result()
        rc = lib.price_european_points(ctypes.byref(req), points, ctypes.byref(result))
        if rc != 0 or result.samples != points or not math.isfinite(result.price):
            raise AssertionError((points, rc, result.samples, result.price))
        results[points] = result
        exact = exact_ordered_price(req, points)
        tolerance = 3.0e-4 if points % 8192 == 0 else 1.5e-3
        if abs(result.price - exact) > tolerance:
            raise AssertionError((points, result.price, exact, tolerance))
        print(f"points={points} price={result.price:.12g}")

    req.num_blocks = 1
    block_result = Result()
    if lib.price_european(ctypes.byref(req), ctypes.byref(block_result)) != 0:
        raise AssertionError("block wrapper failed")
    if (block_result.price != results[8192].price or
            block_result.payoff_sum != results[8192].payoff_sum):
        raise AssertionError("block and points APIs disagree")

    prepared = ctypes.c_void_p()
    if lib.european_prepare(ctypes.byref(req), ctypes.byref(prepared)) != 0:
        raise AssertionError("ordered prepare failed")
    try:
        prepared_block = Result()
        if lib.european_price_prepared(
                prepared, ctypes.byref(prepared_block)) != 0:
            raise AssertionError("ordered prepared block pricing failed")
        if (prepared_block.price != results[8192].price or
                prepared_block.payoff_sum != results[8192].payoff_sum or
                prepared_block.coeff_setup_seconds != 0.0):
            raise AssertionError("cold and prepared block APIs disagree")
        for points in (32, 96, 8192, 8224, 16384):
            prepared_result = Result()
            if lib.european_price_prepared_points(
                    prepared, points, ctypes.byref(prepared_result)) != 0:
                raise AssertionError(f"prepared points failed: {points}")
            cold = results[points]
            if (prepared_result.price != cold.price or
                    prepared_result.payoff_sum != cold.payoff_sum or
                    prepared_result.samples != cold.samples or
                    prepared_result.coeff_setup_seconds != 0.0):
                raise AssertionError(
                    f"cold and prepared points disagree: {points}")
    finally:
        lib.european_prepared_destroy(prepared)

    parameter_cases = (
        Request(100.0, 100.0, 0.05, 0.2, 1.0, 1, 1, 9),
        Request(100.0, 130.0, 0.01, 0.2, 1.0, 1, 0, 9),
        Request(130.0, 100.0, 0.03, 0.2, 1.0, 1, 0, 9),
        Request(100.0, 100.0, 0.02, 0.1, 1.0, 1, 0, 9),
    )
    for case in parameter_cases:
        result = Result()
        if lib.price_european_points(
                ctypes.byref(case), 8192, ctypes.byref(result)) != 0:
            raise AssertionError("ordered parameter case failed")
        exact = exact_ordered_price(case, 8192)
        if abs(result.price - exact) > 3.0e-4:
            raise AssertionError((result.price, exact, case.type, case.s0, case.k))

    req.num_blocks = 0
    for invalid in (0, 31, 33, (1 << 32) - 8192 + 32):
        result = Result()
        if lib.price_european_points(ctypes.byref(req), invalid, ctypes.byref(result)) == 0:
            raise AssertionError(f"invalid point count accepted: {invalid}")

    req.sigma = 0.0
    deterministic = Result()
    if lib.price_european_points(ctypes.byref(req), 32, ctypes.byref(deterministic)) != 0:
        raise AssertionError("sigma-zero case failed")
    expected = max(100.0 - 100.0 * math.exp(-0.05), 0.0)
    # Request fields are float32; compare against the double expression only
    # to the rounding scale introduced by those public inputs.
    if abs(deterministic.price - expected) > 1.0e-6:
        raise AssertionError((deterministic.price, expected))

    req.sigma = 0.21
    req.num_blocks = 1
    rejected = Result()
    if lib.price_european_points(ctypes.byref(req), 8192, ctypes.byref(rejected)) == 0:
        raise AssertionError("ordered alpha > 0.20 was accepted")
    if lib.price_european(ctypes.byref(req), ctypes.byref(rejected)) == 0:
        raise AssertionError("ordered block alpha > 0.20 was accepted")
    rejected_prepared = ctypes.c_void_p()
    if lib.european_prepare(ctypes.byref(req), ctypes.byref(rejected_prepared)) == 0:
        raise AssertionError("ordered prepare alpha > 0.20 was accepted")
    print("ordered_d1_api=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
