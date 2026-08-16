#!/usr/bin/env python3
import argparse
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


def normal_cdf(x: float) -> float:
    return 0.5 * math.erfc(-x / math.sqrt(2.0))


def bs_price(req: Request) -> float:
    s0 = float(req.s0)
    k = float(req.k)
    r = float(req.r)
    sigma = float(req.sigma)
    t = float(req.t)
    df = math.exp(-r * t)
    if sigma == 0.0:
        fwd = s0 * math.exp(r * t)
        payoff = max(fwd - k, 0.0) if req.type == 0 else max(k - fwd, 0.0)
        return df * payoff
    vol = sigma * math.sqrt(t)
    d1 = (math.log(s0 / k) + (r + 0.5 * sigma * sigma) * t) / vol
    d2 = d1 - vol
    if req.type == 0:
        return s0 * normal_cdf(d1) - k * df * normal_cdf(d2)
    return k * df * normal_cdf(-d2) - s0 * normal_cdf(-d1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--blocks", type=int, default=16)
    ap.add_argument("--type", choices=["call", "put"], default="call")
    ap.add_argument("--mode", choices=["buffer", "gaussian-exp", "gaussian-exp-reduced-fma", "gaussian-dynamic-ranges", "gaussian-center-shared", "gaussian-split-tail", "direct", "hybrid", "hybrid-direct-tail"], default="buffer")
    ap.add_argument("--s0", type=float, default=100.0)
    ap.add_argument("--k", type=float, default=100.0)
    ap.add_argument("--r", type=float, default=0.05)
    ap.add_argument("--sigma", type=float, default=0.2)
    ap.add_argument("--t", type=float, default=1.0)
    args = ap.parse_args()

    lib = ctypes.CDLL(str(LIB))
    lib.price_european.argtypes = [ctypes.POINTER(Request), ctypes.POINTER(Result)]
    lib.price_european.restype = ctypes.c_int

    req = Request(
        args.s0,
        args.k,
        args.r,
        args.sigma,
        args.t,
        args.blocks,
        0 if args.type == "call" else 1,
        {"buffer": 0, "gaussian-exp": 1, "direct": 2, "hybrid": 3, "hybrid-direct-tail": 4, "gaussian-split-tail": 5, "gaussian-center-shared": 6, "gaussian-dynamic-ranges": 7, "gaussian-exp-reduced-fma": 8}[args.mode],
    )
    got = Result()
    rc = lib.price_european(ctypes.byref(req), ctypes.byref(got))
    if rc != 0:
        raise SystemExit(f"price_european failed: rc={rc}")

    analytic = bs_price(req)
    print(f"blocks={args.blocks} type={args.type} mode={args.mode} samples={got.samples}")
    print(f"price={got.price:.12g} analytic={analytic:.12g} abs_err={abs(got.price - analytic):.6g}")
    print(f"kernel_seconds={got.kernel_seconds:.9f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
