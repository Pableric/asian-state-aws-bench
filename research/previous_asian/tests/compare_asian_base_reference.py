#!/usr/bin/env python3
import argparse, ctypes
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class Request(ctypes.Structure):
    _fields_=[("s0",ctypes.c_float),("k",ctypes.c_float),("r",ctypes.c_float),("sigma",ctypes.c_float),("t",ctypes.c_float),("num_blocks",ctypes.c_uint64),("type",ctypes.c_int),("mode",ctypes.c_int)]
class Result(ctypes.Structure):
    _fields_=[("price",ctypes.c_double),("payoff_sum",ctypes.c_double),("samples",ctypes.c_uint64),("coeff_setup_seconds",ctypes.c_double),("kernel_seconds",ctypes.c_double)]

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--blocks',type=int,default=1); ap.add_argument('--all',action='store_true'); ap.add_argument('--tolerance',type=float,default=1e-4); a=ap.parse_args()
    lib=ctypes.CDLL(str(ROOT/'libasian_pricer.so'))
    lib.price_asian.argtypes=[ctypes.POINTER(Request),ctypes.POINTER(Result)]
    lib.price_asian_scalar_mode.argtypes=[ctypes.POINTER(Request),ctypes.c_int,ctypes.POINTER(Result)]
    modes=(1,2,3) if a.all else (2,)
    names={1:'final-z',2:'rank1',3:'coefficient-pair'}
    failed=False
    for typ in (0,1):
      for mode in modes:
        req=Request(100,100,.05,.2,1,a.blocks,typ,mode); got=Result(); ref=Result()
        if lib.price_asian(ctypes.byref(req),ctypes.byref(got)) or lib.price_asian_scalar_mode(ctypes.byref(req),mode,ctypes.byref(ref)): raise SystemExit('pricing call failed')
        rel=abs(got.price-ref.price)/max(abs(ref.price),1e-12)
        print(f"{names[mode]:16s} {'call' if typ==0 else 'put ':4s} vector={got.price:.12g} scalar={ref.price:.12g} rel={rel:.3e}")
        failed |= rel>a.tolerance
    return int(failed)
if __name__=='__main__': raise SystemExit(main())
