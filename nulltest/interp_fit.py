import flrender as F, numpy as np
def data(k):
    y = F.render(f'interp_k{k}', 'impulse1000.wav', key=k, velocity=128)[:, 0] * 32768 / 13103
    r = 2 ** ((k - 60) / 12)
    c0 = int(1000 / r)
    n = np.arange(max(0, c0 - 80), c0 + 80)
    return r, n * r - 1000, y[n]
