"""FL's sample-rate conversion modelled as cascaded windowed-sinc resamplers (the 30 / 84-tap kernels)."""
import numpy as np; from lib import wavio
A30 = np.array([0.362684569, 0.509910674, 0.132162708, -0.006084749, 0.001926633, -0.000879890, 0.000296751])
A84 = np.array([0.498387586, 0.490065433, 0.009996963, 0.002969411, -0.002640382, 0.002159460, -0.001503978, 0.000750707, -0.000168888])
def kern(d, c, co, W):
    x = np.clip(np.abs(d) / W, 0, 1); w = sum(a * np.cos(np.pi * m * x) for m, a in enumerate(co)) * (np.abs(d) < W)
    return c * np.sinc(c * d) * w
def resample(x, positions, ratio, co, W):
    c = min(1.0, 1.0 / ratio); half = int(np.ceil(W))
    out = np.zeros(len(positions))
    for i, p in enumerate(positions):
        b = int(np.floor(p)); idx = np.arange(b - half + 1, b + half + 1); idx = idx[(idx >= 0) & (idx < len(x))]
        out[i] = (kern(p - idx, c, co, W) * x[idx]).sum()
    return out
