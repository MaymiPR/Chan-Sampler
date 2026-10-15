import numpy as np, pulses as P
from model import ap
def comb_section(name, ch, aps, lp_a=None, kernel=None, dry=0.7, n=24000):
    y = P.ir(name, ch)[:n].copy(); y[0] -= dry
    if lp_a is not None:
        y = np.concatenate([[y[0]], y[1:] - lp_a * y[:-1]])
    N = 4 * n
    h = np.zeros(N); h[0] = 1
    for D, g in aps: h = ap(h, D, g)
    Y = np.fft.rfft(y, N); H = np.fft.rfft(h, N)
    if kernel is not None: H = H * np.fft.rfft(kernel, N)
    s = np.fft.irfft(Y / H, N)[:n]
    return s
