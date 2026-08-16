#!/usr/bin/env python3
"""
Explore local polynomial fits for the planned fused European pricing kernels.

This is analysis-only for now. It uses the same deterministic memory layout as
the Sobol Gaussian generator and reports max absolute fit error for:
  1. exp(mu + vol*z) as a function of z
  2. discounted payoff as a direct function of Sobol u
"""

import argparse
import math

import numpy as np
from numpy.polynomial import Chebyshev, Polynomial
from scipy.special import ndtri
from scipy.stats import qmc


SOBOL_BLOCK_SIZE = 8192
WORK_CHUNK_SIZE = 4096
ZMM_LANES = 16
TWO_ZMM_LANES = 32
RANGE_SCALE = 4096.0


def logical_for_mem(mem_idx: int) -> int:
    in_block = mem_idx % SOBOL_BLOCK_SIZE
    internal_half = in_block // WORK_CHUNK_SIZE
    pos = in_block % WORK_CHUNK_SIZE
    step = pos // TWO_ZMM_LANES
    lane32 = pos % TWO_ZMM_LANES
    lane = lane32 if lane32 < ZMM_LANES else lane32 - ZMM_LANES

    if internal_half == 0:
        if lane32 < ZMM_LANES:
            return step + lane * 256
        return WORK_CHUNK_SIZE + step + lane * 256

    if lane32 < ZMM_LANES:
        return (WORK_CHUNK_SIZE - 1 - step) - lane * 256
    return (SOBOL_BLOCK_SIZE - 1 - step) - lane * 256


def fit_poly(x: np.ndarray, y: np.ndarray, degree: int) -> np.ndarray:
    if x.size == 0:
        return np.zeros(degree + 1, dtype=np.float64)
    lo = float(np.min(x))
    hi = float(np.max(x))
    if lo == hi:
        out = np.zeros(degree + 1, dtype=np.float64)
        out[0] = float(y[0])
        return out
    cheb = Chebyshev.fit(x, y, degree, domain=[lo, hi])
    poly = cheb.convert(kind=Polynomial)
    coeffs = np.asarray(poly.coef, dtype=np.float64)
    if coeffs.size < degree + 1:
        coeffs = np.pad(coeffs, (0, degree + 1 - coeffs.size))
    return coeffs[:degree + 1]


def eval_poly(coeffs: np.ndarray, x: np.ndarray) -> np.ndarray:
    out = np.zeros_like(x, dtype=np.float64)
    for c in coeffs[::-1]:
        out = out * x + c
    return out


def payoff(st: np.ndarray, k: float, option_type: str) -> np.ndarray:
    if option_type == "call":
        return np.maximum(st - k, 0.0)
    return np.maximum(k - st, 0.0)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--s0", type=float, default=100.0)
    ap.add_argument("--k", type=float, default=100.0)
    ap.add_argument("--r", type=float, default=0.05)
    ap.add_argument("--sigma", type=float, default=0.2)
    ap.add_argument("--t", type=float, default=1.0)
    ap.add_argument("--type", choices=["call", "put"], default="call")
    ap.add_argument("--degree", type=int, default=2)
    ap.add_argument("--skip-values", type=int, default=8192)
    args = ap.parse_args()

    sampler = qmc.Sobol(d=1, scramble=False)
    u_all = sampler.random(args.skip_values + SOBOL_BLOCK_SIZE)[:, 0]
    logical = np.array([logical_for_mem(i) for i in range(SOBOL_BLOCK_SIZE)], dtype=np.int64)
    u = u_all[args.skip_values + logical]
    z = ndtri(u)
    finite = np.isfinite(z)

    mu = (args.r - 0.5 * args.sigma * args.sigma) * args.t
    vol = args.sigma * math.sqrt(args.t)
    df = math.exp(-args.r * args.t)
    expo = np.exp(mu + vol * z[finite])
    st = args.s0 * expo
    disc_payoff = df * payoff(st, args.k, args.type)

    ranges = np.minimum(np.floor(np.abs(u - 0.5) * RANGE_SCALE).astype(np.int64), 2047)
    unique_ranges = np.unique(ranges[finite])

    max_exp_err = 0.0
    max_payoff_err = 0.0
    crossing = 0
    zero_ranges = 0

    for r in unique_ranges:
        idx_all = np.where((ranges == r) & finite)[0]
        if idx_all.size < args.degree + 1:
            continue
        z_r = z[idx_all]
        e_true = np.exp(mu + vol * z_r)
        e_coeff = fit_poly(z_r, e_true, args.degree)
        max_exp_err = max(max_exp_err, float(np.max(np.abs(eval_poly(e_coeff, z_r) - e_true))))

        u_r = u[idx_all]
        st_r = args.s0 * e_true
        p_true = df * payoff(st_r, args.k, args.type)
        if np.all(p_true == 0.0):
            zero_ranges += 1
            continue
        if np.any(p_true == 0.0) and np.any(p_true > 0.0):
            crossing += 1
        p_coeff = fit_poly(u_r, p_true, args.degree)
        max_payoff_err = max(max_payoff_err, float(np.max(np.abs(eval_poly(p_coeff, u_r) - p_true))))

    print(f"degree={args.degree} type={args.type} ranges={unique_ranges.size}")
    print(f"max_exp_abs_err={max_exp_err:.9g}")
    print(f"max_direct_payoff_abs_err={max_payoff_err:.9g}")
    print(f"zero_payoff_ranges={zero_ranges}")
    print(f"strike_crossing_ranges={crossing}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
