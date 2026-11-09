"""Type A's comb echoes: matching pursuit of FL's impulse response (dry part
removed) with the allpass chain's response as the atom; prints the residual.

    python -m probes.rvb.combs <case, e.g. rv_A128> <samples> [channel]
"""
import sys, numpy as np
from probes.rvb import pulses as P
from probes.rvb.model import ap

def atom(n, aps=((43, .7), (127, .7), (389, .7))):
    x = np.zeros(n); x[0] = 1
    for D, g in aps: x = ap(x, D, g)
    return np.convolve(x, [.5, 1, .5])[:n]

def pursue(e, A, start, n, thresh):
    """Greedy: e ~ sum c_k A shifted by k (k >= start)."""
    r = e[:n].copy(); s = {}
    F = np.fft.rfft(A, 2 * n)
    for _ in range(400):
        c = np.fft.irfft(np.fft.rfft(r, 2 * n) * np.conj(F), 2 * n)[:n]
        c[:start] = 0
        k = int(np.argmax(np.abs(c))); a = c[k] / (A @ A)
        if abs(a) < thresh: break
        s[k] = s.get(k, 0) + a
        r[k:] -= a * A[:n - k]
    return dict(sorted(s.items())), r

if __name__ == '__main__':
    name = sys.argv[1]; n = int(sys.argv[2]); ch = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    y = P.ir(name, ch); y = y[:n]
    A = atom(n)
    x = np.zeros(n); x[0] = 1
    e = y - 0.7 * x
    s, r = pursue(e, A, 0, n, 0.002)
    print('resid rms', np.sqrt((r ** 2).mean()) * 13103, 'LSB')
    for k, a in s.items(): print(k, round(a, 4))
