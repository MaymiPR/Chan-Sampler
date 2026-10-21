"""Writes the stretch-mode test signals into signals/ (they are generated, not
stored in the repository): bit-identical to the ones FL rendered, so cached
FL renders stay valid.

    python -m stretch.make_signals          # every signal that is missing
    python -m stretch.make_signals -f       # rewrite them all

The signals (44.1 kHz, float):
  e3_lfclick / e3_hfclick  band-limited clicks below 1.3 kHz / 2-10 kHz
  e3_bursts                noise bursts, one with a level step
  e3_saw<f0>               band-limited saws at 60 ... 900 Hz
  e3_comb<D>               noise plus one echo D samples later (the period estimator)
  e3_imp / e3_impn / e3_tb single-sample clicks (alone, on noise), 2 kHz tone bursts
  e3_onsA / e3_onsB        noise bursts: flat with varied start, decays and fade-ins
  e3_grid / e3_grid2       identical bursts stepped through a 512-sample grid
  e3_am / e3_amlo          noise swelling in and out (no attacks), and below 700 Hz
  e3_par                   single-sample clicks on alternating even / odd samples
(e3_clicks, e3_loop and e3_noise are stored in signals/.)"""
import json, os, sys
import numpy as np
from lib import paths, wavio

SR = 44100


def _blclick(lo, hi, L=4097):
    f = np.fft.rfftfreq(L, 1 / SR)
    h = np.fft.fftshift(np.fft.irfft(((f >= lo) & (f <= hi)).astype(float), L))
    return h / np.abs(h).max()


def _clicks(lo, hi):
    n = SR * 2; x = np.zeros(n); h = _blclick(lo, hi)
    for t in (0.25, 0.75, 1.25):
        i = int(t * SR); x[i - 2048:i + 2049] += 0.5 * h
    return x


def _bursts():
    rng = np.random.default_rng(11); x = np.zeros(SR * 3)
    for t0, t1, lv in ((0.3, 1.0, 0.2), (1.3, 1.6, 0.05), (1.6, 2.4, 0.2)):
        a, b = int(t0 * SR), int(t1 * SR); x[a:b] = rng.standard_normal(b - a) * lv
    return x


def _saw(f0):
    n = SR * 2; t = np.arange(n); x = np.zeros(n)
    for k in range(1, int(20000 / f0) + 1):
        x += np.sin(2 * np.pi * k * f0 * t / SR) / k
    return x * (0.5 / np.abs(x).max())


def _comb(D):
    n = SR * 2; w = np.random.default_rng(21).standard_normal(n + 2000) * 0.15
    return w[1000:1000 + n] + w[1000 - D:1000 - D + n]


def _imp_set():
    n = SR * 2; rng = np.random.default_rng(5)
    A = np.zeros(n)
    for t in (0.25, 0.75, 1.25, 1.75): A[int(t * SR)] = 0.5
    B = rng.standard_normal(n) * 0.03
    for t in (0.25, 0.75, 1.25, 1.75): B[int(t * SR)] += 0.5
    C = np.zeros(n); tt = np.arange(int(0.3 * SR))
    for t in (0.2, 1.0):
        i = int(t * SR); C[i:i + len(tt)] = 0.3 * np.sin(2 * np.pi * 2000 * tt / SR)
    return {'e3_imp': A, 'e3_impn': B, 'e3_tb': C}


def _ons():
    rng = np.random.default_rng(9); n = int(SR * 6.5)
    A = np.zeros(n); startsA = []
    for k in range(10):
        s = int(k * 0.6 * SR) + k * 53 + 2000; startsA.append(s); L = int(0.15 * SR); A[s:s + L] = rng.standard_normal(L) * 0.2
    B = np.zeros(n); startsB = []; shapes = []
    for k, shape in enumerate(['d20', 'd80', 'd300', 'd1000', 'flat', 'rise1000', 'd20', 'd300', 'rise300', 'flat']):
        s = int(k * 0.6 * SR) + 3000; startsB.append(s); L = int(0.15 * SR); t = np.arange(L); w = rng.standard_normal(L) * 0.2
        if shape.startswith('d'): env = np.exp(-t / float(shape[1:]))
        elif shape.startswith('rise'): env = np.minimum(1, t / float(shape[4:]))
        else: env = np.ones(L)
        B[s:s + L] = w * env; shapes.append(shape)
    return {'e3_onsA': A, 'e3_onsB': B}, {'A': startsA, 'B': startsB, 'shapes': shapes}


def _grid():
    burst = np.random.default_rng(13).standard_normal(4000) * 0.2
    x = np.zeros(int(SR * 14.6)); starts = []
    for k in range(40):
        s = 3000 + int(k * 0.35 * SR) + k * 29; starts.append(s); x[s:s + 4000] = burst
    return x, starts


def _grid2():
    burst = np.random.default_rng(13).standard_normal(4000) * 0.2
    starts = [3072 + k * (9728 + 8) for k in range(64)]   # 9728 = 19 x 512: the grid phase steps by 8
    x = np.zeros(starts[-1] + 12000)
    for s in starts: x[s:s + 4000] = burst
    return x, starts


def _am():
    n = 3 * SR; t = np.arange(n)
    env = 0.3 * np.sin(np.pi * t / 14700) ** 2           # swells, never an attack
    return (env * (np.random.default_rng(5).random(n) * 2 - 1)).astype(np.float32)


def _amlo(am):
    X = np.fft.rfft(am.astype(np.float64)); X[np.fft.rfftfreq(len(am), 1 / SR) > 700] = 0
    return np.fft.irfft(X, len(am)).astype(np.float32)


def _par():
    x = np.zeros(100000, np.float32)
    for k in range(16): x[20000 + k * 4001] = 0.5
    return x


def signals():
    out = {'e3_lfclick': _clicks(50, 1300), 'e3_hfclick': _clicks(2000, 10000), 'e3_bursts': _bursts()}
    for f0 in (60, 80, 100, 147, 200, 300, 500, 900): out[f'e3_saw{f0}'] = _saw(f0)
    for D in (20, 30, 40, 50, 60, 100, 150, 250, 400, 430, 460, 490, 512, 530, 550, 650, 800, 1000): out[f'e3_comb{D}'] = _comb(D)
    out.update(_imp_set())
    ons, meta = _ons(); out.update(ons)
    out['e3_grid'], grid = _grid()
    out['e3_grid2'], grid2 = _grid2()
    out['e3_am'] = _am(); out['e3_amlo'] = _amlo(out['e3_am'])
    out['e3_par'] = _par()
    return out, {'ons_meta.json': meta, 'grid_meta.json': grid, 'grid2_meta.json': grid2}


def main(force=False):
    out, metas = signals()
    for name, x in out.items():
        p = os.path.join(paths.SIGNALS, name + '.wav')
        if force or not os.path.exists(p):
            wavio.write_float(p, x, SR)
    # the burst positions, for the analyses (also next to FL's renders, where they were first kept)
    for name, m in metas.items():
        for d in (paths.DATA, os.path.join(paths.ROOT, 'renders_fl')):
            os.makedirs(d, exist_ok=True)
            json.dump(m, open(os.path.join(d, name), 'w'))
    print(len(out), 'signals in', paths.SIGNALS)


if __name__ == '__main__':
    main('-f' in sys.argv)
