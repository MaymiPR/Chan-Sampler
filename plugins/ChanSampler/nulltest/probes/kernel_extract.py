"""FL's interpolation kernel sampled from impulses every 50 samples played at several keys."""
import numpy as np; from lib import flrender as F
def points(keys=(59, 57, 55, 50), unit=13103.0):
    D = []; K = []
    for k in keys:
        y = F.render(f'imps_k{k}', 'signals/impulses50.wav', key=k, velocity=128)[:, 0] * 32768 / unit
        r = 2 ** ((k - 60) / 12)
        n = np.arange(len(y)); p = n * r
        j = np.round((p - 1000) / 50)
        valid = (j >= 0) & (j * 50 + 1000 < 130000) & (p < 129000)
        d = p - (1000 + 50 * j)
        D.append(d[valid]); K.append(y[valid])
    return np.concatenate(D), np.concatenate(K)
if __name__ == '__main__':
    d, k = points()
    print(len(d))
    o = np.argsort(d); d, k = d[o], k[o]
    for lo in np.arange(-17, 17, 0.5):
        m = (d >= lo) & (d < lo + 0.5)
        if m.any(): print(round(lo, 1), m.sum(), round(np.abs(k[m]).max(), 5))
