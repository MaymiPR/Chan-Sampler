"""FL's sample-rate conversion kernel, from impulse trains in 22.05 / 48 / 96 kHz files."""
import numpy as np; from lib import flrender as F
def points(name, wav, rate, spacing, start=1000, unit=13103.0, half=60):
    y = F.render(name, wav, velocity=128)[:, 0] * 32768 / unit
    r = rate / 44100.0
    n = np.arange(len(y)); p = n * r
    j = np.round((p - start) / spacing)
    from lib import wavio
    x, _ = wavio.read(wav)
    last = np.nonzero(x[:, 0])[0][-1]
    valid = (j >= 0) & (start + j * spacing <= last) & (p < last - half)
    d = p - (start + spacing * j)
    return d[valid], y[valid], r
if __name__ == '__main__':
    for nm, wav, rate, sp in (('imps48_k60', 'signals/imps48.wav', 48000, 101), ('imps22_k60', 'signals/imps22.wav', 22050, 101), ('imps96_k60', 'signals/imps96.wav', 96000, 151)):
        d, k, r = points(nm, wav, rate, sp)
        nz = np.abs(k) * 13103 >= 1
        print(nm, 'ratio', r, 'support d', d[nz].min(), d[nz].max(), 'k at d~0', k[np.argmin(np.abs(d))])
