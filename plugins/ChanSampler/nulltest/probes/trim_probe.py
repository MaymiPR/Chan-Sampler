"""SMP LENGTH / trim: where FL's and our renders sit in the source and how they end."""
import numpy as np; from lib import nullfl, wavio
x, _ = wavio.read('signals/noise.wav'); x = x[:, 0].astype(float); N = len(x)
def locate(seg, src):
    """Offset of seg inside src (normalised cross-correlation via FFT)."""
    n = len(seg); L = 1 << int(np.ceil(np.log2(len(src) + n)))
    c = np.fft.irfft(np.fft.rfft(src, L) * np.conj(np.fft.rfft(seg, L)), L)[:len(src) - n + 1]
    i = int(np.argmax(np.abs(c))); return i, c[i] / (seg @ seg + 1e-30)
def probe(name, at=(0,)):
    a, b = nullfl.render_pair(name, 'signals/noise.wav')
    out = {}
    for lab, y in (('FL', a[:, 0]), ('our', b[:, 0])):
        nz = np.nonzero(np.abs(y) > 1e-4)[0]
        info = [lab, 'len', (nz[-1] + 1) if len(nz) else 0]
        for t in at:
            seg = y[t:t + 1024]
            if np.abs(seg).max() < 1e-4: info.append((t, 'silent')); continue
            i, g = locate(seg, x); j, h = locate(seg, x[::-1])
            info.append((t, 'fwd', i, round(1 / g, 3) if g else 0) if abs(g) > abs(h) else (t, 'rev', N - 1 - j, round(1 / h, 3)))
        out[lab] = info
    return out
if __name__ == '__main__':
    import sys
    print('N', N)
    for nm in sys.argv[1:]:
        r = probe(nm, at=(0, 5000))
        print(nm); print('  ', r['FL']); print('  ', r['our'])
