"""Type B: least-squares levels of the comb echoes at given positions (from
bfit), fitted up to the tail cut."""
import numpy as np
from probes.rvb import pulses as P
from probes.rvb.bfit import atomB
def fit(m, ch, pos, n=None):
    y = P.ir(f'rv_B{m}', ch).copy(); y[0] -= 0.7
    last = np.nonzero(np.round(y * 13103))[0][-1]; cut = last - 420
    n = cut
    A = atomB(n, 97 if ch == 0 else 79)
    pos = [p for p in pos if p < n]
    cols = [A] + [np.concatenate([np.zeros(p), A[:n - p]]) for p in pos]
    M = np.stack(cols, 1)
    c, *_ = np.linalg.lstsq(M, y[:n], rcond=None)
    r = (y[:n] - M @ c) * 13103
    return pos, c / c[0], c[0], np.sqrt((r ** 2).mean()), np.abs(r).max(), cut
