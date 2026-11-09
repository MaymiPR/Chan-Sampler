"""Reverb type B as fitted: combs, five allpasses (the second differs per
channel), then a one-pole output filter, to compare with FL's impulse response."""
import numpy as np
from probes.rvb.model import ap, comb
def lp(x, a=0.7):
    out = np.zeros(len(x)); acc = 0.0
    for i in range(len(x)): acc = a * acc + x[i]; out[i] = acc
    return out
def wetB(x, mix, d2, combs, cg, t60scale=1.0, G=0.16201, fs=44100.0):
    t60 = mix / 256 * fs * t60scale
    acc = x.copy()                  # direct path (comb count handled by cg)
    for D in combs:
        acc = acc + cg * (comb(x, D, 10 ** (-3 * D / t60)) - x)
    for D in (67, d2, 101, 197, 599): acc = ap(acc, D, .7)
    return G * lp(acc)
