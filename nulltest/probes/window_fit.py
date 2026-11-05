"""Fit FL's interpolation kernel as a sinc times a cosine-series window; writes data/fl_window.npy."""
import numpy as np; from probes import kernel_extract as K
W = 15.0
def data(keys=(59, 55, 50, 61, 64, 67, 79)):
    Ds, Ks, Cs = [], [], []
    for key in keys:
        d, k = K.points((key,)); r = 2 ** ((key - 60) / 12)
        sel = np.random.default_rng(key).random(len(d)) < 0.25
        Ds.append(d[sel]); Ks.append(k[sel]); Cs.append(np.full(sel.sum(), min(1.0, 1.0 / r)))
    return np.concatenate(Ds), np.concatenate(Ks), np.concatenate(Cs)
def basis(d, M):
    x = np.clip(np.abs(d) / W, 0, 1)
    return np.stack([np.cos(np.pi * m * x) for m in range(M + 1)], 1) * (np.abs(d) < W)[:, None]
def fit(M, d, k, c):
    A = basis(d, M) * (c * np.sinc(c * d))[:, None]
    a, *_ = np.linalg.lstsq(A, k, rcond=None)
    r = (A @ a - k) * 13103
    return a, np.sqrt((r ** 2).mean()), np.abs(r).max()
if __name__ == '__main__':
    d, k, c = data()
    for M in (6, 10, 14, 18, 22, 26, 30):
        a, rms, mx = fit(M, d, k, c)
        print(M, round(rms, 3), round(mx, 2))
    a, rms, mx = fit(26, d, k, c)
    np.save('data/fl_window.npy', a); print(list(np.round(a, 8)))
