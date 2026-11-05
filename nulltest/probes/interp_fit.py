"""FL's interpolator at fractional positions, from an impulse played at several keys."""
import numpy as np; from lib import flrender as F
def data(k):
    y = F.render(f'interp_k{k}', 'signals/impulse1000.wav', key=k, velocity=128)[:, 0] * 32768 / 13103
    r = 2 ** ((k - 60) / 12)
    c0 = int(1000 / r)
    n = np.arange(max(0, c0 - 80), c0 + 80)
    return r, n * r - 1000, y[n]
