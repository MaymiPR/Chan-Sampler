"""FL's stretch-ratio grid and e3's grain splices (E3_NOTES.md, round 9).

FL stretches by the ratio rounded down to a grid (e3 generic and e2 transient
1/512, e2 generic 1/256) and resamples the rest; its read interpolator dulls
the treble by an amount that follows the fractional read position, so the
15-20 kHz level ripples at exactly the drift rate |MUL / grid ratio - 1| * 44100.
This measures that ripple on noise for a list of MUL settings, and the size
of e3's splice jumps (the grain period it chose) for FL and ours.

    python -m stretch.make_signals                  # once (e3_noise.wav)
    python -m stretch.ratio_grid grid e3gen e2gen   # ripple rate against the grid's prediction
    python -m stretch.ratio_grid splices            # e3's jump sizes on noise, FL against ours"""
import math, sys
import numpy as np
from numpy.fft import rfft, irfft
from lib import flrender as F, paths, wavio
from stretch import common as C

SR = 44100
SIGNAL = 'e3_noise.wav'
GRID = {'e3gen': 512, 'e2trans': 512, 'e2gen': 256}


def band(x, lo, hi):
    X = rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR); X[(f < lo) | (f >= hi)] = 0
    return irfft(X, len(x))


def render(mode, raw):
    y = F.render(f'rg_{mode}_{raw}', SIGNAL, [F.params(108, '<i', C.MODES[mode][0]), F.params(100, '<i', 0), F.params(104, '<i', raw)], beats=16)
    return y[:, 0].astype(float) / C.GAIN


def ripple(y):
    """The strongest modulation of the 15-21 kHz level (Hz, dB over its surroundings)."""
    from numpy.lib.stride_tricks import sliding_window_view as sw
    h = band(y, 15000, 21000); e = np.convolve(h * h, np.ones(8) / 8, 'same')[::4]; fs = SR / 4
    N = 1 << 15; spec = []
    for i in range(4000, len(e) - N, N // 2):
        s = e[i:i + N] - e[i:i + N].mean(); spec.append(np.abs(np.fft.rfft(s * np.hanning(N))) ** 2)
    S = np.mean(spec, 0); f = np.fft.rfftfreq(N, 1 / fs); m = (f > 2) & (f < 3000)
    Sm = 10 * np.log10(S[m]); p = np.pad(Sm, 40, mode='edge'); ex = Sm - np.median(sw(p, 81), 1)
    i = int(np.argmax(ex)); return f[m][i], ex[i]


def grid(modes):
    for mode in modes:
        steps = GRID.get(mode)
        for raw in (-9000, -5850, -3000, 1000, 3000, 4000, 7000):
            m = C.mul(raw); f, ex = ripple(render(mode, raw))
            pred = abs(m / (math.floor(m * steps) / steps) - 1) * SR if steps else float('nan')
            print(f'{mode:8s} MUL {m:.4f}: ripple {f:7.2f} Hz ({ex:4.1f} dB), grid predicts {pred:7.2f} Hz', flush=True)


def offsets(y, x, mul, W=192, step=8, span=1200, t0=2000):
    """High band: per output block, the source - output offset of its best 1:1 match."""
    yh, xh = band(y, 2500, 20000), band(x, 2500, 20000)
    t1 = min(len(yh) - W, int((len(xh) - span - W) * mul)); out = []
    for t in range(t0, t1, step):
        blk = yh[t:t + W]; e = blk @ blk; c0 = int(round(t / mul)) - span
        if e < 1e-12 or c0 < 0: continue
        seg = xh[c0:c0 + 2 * span + W]; n = 1 << int(np.ceil(np.log2(len(seg) + W)))
        cc = irfft(rfft(seg, n) * np.conj(rfft(blk, n)), n)[:2 * span + 1]
        es = np.convolve(seg ** 2, np.ones(W), 'valid')[:2 * span + 1]
        ncc = cc / np.sqrt(e * es + 1e-20); k = int(np.argmax(ncc))
        if ncc[k] > 0.6:
            out.append((t, int(round(t / mul + k - span - t))))
    return out


def jumps(y, x, mul):
    tr = offsets(y, x, mul); out = []
    for (t0, q0), (t1, q1) in zip(tr, tr[1:]):
        if 30 < abs(q1 - q0) < 700: out.append(abs(q1 - q0))
    return np.array(out)


def splices():
    x = wavio.read(paths.signal(SIGNAL))[0][:, 0].astype(float)
    for raw in (5850, -5850):
        m = C.mul(raw)
        for who, y in (('FL', render('e3gen', raw)), ('ours', C.ours(SIGNAL, f'rg_e3gen_{raw}', 0, 'e3gen', 1 / m, force=True))):
            L = jumps(y, x, m)
            print(f'MUL {m:.3f} {who:4s}: {len(L)} jumps, size 10/25/50/75/90th pct {np.percentile(L, [10, 25, 50, 75, 90]).astype(int)}', flush=True)


if __name__ == '__main__':
    if sys.argv[1:2] == ['splices']:
        splices()
    else:
        grid(sys.argv[2:] or list(GRID))
