"""How close a stretched render is to FL's when a null is impossible (FL's
stretch engine is proprietary): envelope distance, band-spectrum distance
and the null, all in dB."""
import numpy as np

SR = 44100


def _pad(a, b):
    return np.concatenate([b[:len(a)], np.zeros(max(0, len(a) - len(b)))])


def envdist(a, b, B=256, floor=True):
    """Mean |level difference| of 256-sample blocks, over FL's blocks within
    40 dB of its loudest, after removing the median offset; both floored 60 dB
    below FL's loudest block (digital silence against a quiet tail would
    otherwise count as a 100+ dB error). floor=False: the unfloored version
    the e3 rounds in E3_NOTES.md used."""
    b = _pad(a, b)
    ea = np.array([np.sqrt(np.mean(a[i:i + B] ** 2)) for i in range(0, len(a) - B, B)])
    eb = np.array([np.sqrt(np.mean(b[i:i + B] ** 2)) for i in range(0, len(a) - B, B)])
    floor = ea.max() * 10 ** (-60 / 20) if floor else 1e-9
    m = ea > ea.max() * 10 ** (-40 / 20)
    d = 20 * np.log10(np.maximum(ea[m], floor)) - 20 * np.log10(np.maximum(eb[m], floor))
    d -= np.median(d)
    return float(np.mean(np.abs(d)))


def specdist(a, b, N=2048, H=512):
    """Mean |difference| of third-octave band levels (from 60 Hz) per frame,
    over bands within 60 dB of FL's loudest, after removing the median offset."""
    b = _pad(a, b)
    w = np.hanning(N)
    S = lambda x: np.array([np.abs(np.fft.rfft(x[i:i + N] * w)) for i in range(0, len(x) - N, H)])
    A, Bm = S(a), S(b)
    f = np.fft.rfftfreq(N, 1 / SR)
    edges = 60 * 2 ** (np.arange(0, 28) / 3)
    bands = [(f >= edges[i]) & (f < edges[i + 1]) for i in range(len(edges) - 1) if edges[i + 1] < 20000]
    A2 = np.stack([np.sqrt((A[:, m] ** 2).sum(1)) for m in bands], 1)
    B2 = np.stack([np.sqrt((Bm[:, m] ** 2).sum(1)) for m in bands], 1)
    mask = A2 > A2.max() * 1e-3
    d = 20 * np.log10(A2 + 1e-9) - 20 * np.log10(B2 + 1e-9)
    d -= np.median(d[mask])
    return float(np.mean(np.abs(d[mask])))


def null(a, b, fit=True):
    """Residual energy of a - g b against a (dB); g fitted by least squares
    unless fit is False."""
    b = _pad(a, b)
    g = np.dot(a, b) / max(np.dot(b, b), 1e-20) if fit else 1.0
    r = a - g * b
    return float(10 * np.log10(np.dot(r, r) / np.dot(a, a)))
