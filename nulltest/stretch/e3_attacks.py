"""e3 generic's attack timing against FL, with identical noise bursts stepped
8 samples through FL's 512-sample timing grid (e3_grid2) and differently
shaped bursts (e3_onsA / e3_onsB):

  pitch   bursts at +700 / -700 cents: each burst's reference point R (where
          the attack lands on time; 0 = its start, ~469 = its peak), FL's
          against ours, by grid phase
  mul     the same at MUL x1.5 / x0.667 / x2
  onsets  the shaped bursts at x1.5 / x0.667: placed within 3 samples of FL's
  gain    the band below the split while an attack is held, by grid phase
          (mean error of ours against FL's per 128-sample block)

    python -m stretch.make_signals
    python -m stretch.e3_attacks pitch mul onsets gain

FL's MUL renders of these signals (renders_fl/e3m_*.npy) come from the e3
rounds (E3_NOTES.md); the pitch ones are rendered here (renders/e3p_grid2_*)."""
import json, os, sys
import numpy as np
from lib import flrender as F, paths
from stretch import common as C

SR = 44100


def hp(x, lo=2000):
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR); X[f < lo] = 0
    return np.fft.irfft(X, len(x))


def lp(x, hi=1300):
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR); X[f >= hi] = 0
    return np.fft.irfft(X, len(x))


def fl_npy(name):
    x = np.load(os.path.join(paths.ROOT, 'renders_fl', name + '.npy'))
    return x if x.ndim == 1 else x[:, 0]


def best_offset(Y, tmpl, centre, M):
    """Where the template sits in Y around centre (normalised correlation)."""
    W = len(tmpl); seg = Y[max(0, centre - M):centre + M + W]
    cc = np.correlate(seg, tmpl, 'valid'); en = np.convolve(seg ** 2, np.ones(W), 'valid')
    nc = cc / np.sqrt(en * np.dot(tmpl, tmpl) + 1e-15)
    return int(np.argmax(nc)) - min(M, centre), float(nc.max())


def grid_source():
    starts = json.load(open(os.path.join(paths.DATA, 'grid2_meta.json')))
    src = fl_npy('e3m_grid2_0')
    return starts, src, hp(src)


def pitch():
    starts, src, SH = grid_source()
    for cents in (700, -700):
        b = 2 ** (cents / 1200)
        fl = F.render(f'e3p_grid2_{cents}', 'e3_grid2.wav', [F.params(108, '<i', 1), F.params(100, '<i', cents)], beats=34)[:, 0]
        ours = C.ours('e3_grid2.wav', f'grid2_{cents}', cents, 'e3gen', 1)
        bad = []
        for s in starts:   # the output of source s sits at s + R (1 - 1 / b)
            tmpl = np.interp(s + b * np.arange(200), np.arange(len(SH)), SH)
            R = [best_offset(hp(y[:len(src)]), tmpl, s, 700)[0] / (1 - 1 / b) for y in (fl, ours)]
            if abs(R[0] - R[1]) > 5:
                bad.append((s % 512, round(R[0]), round(R[1])))
        print(f'pitch {cents:+d}: {len(starts) - len(bad)} of {len(starts)} bursts timed like FL', bad[:6])


def mul():
    starts, src, SH = grid_source()
    for raw in (5850, -5850, 10000):
        a = C.mul(raw); n = int(len(src) * a)
        fl = hp(fl_npy(f'e3m_grid2_{raw}')[:n]); ours = hp(C.ours('e3_grid2.wav', f'mgrid_{raw}', 0, 'e3gen', 1 / a)[:n])
        bad = []
        for s in starts:   # the output of source s sits at s a + R (a - 1)
            R = [best_offset(Y, SH[s:s + 200], int(round(s * a)), 900)[0] / (a - 1) for Y in (fl, ours)]
            if abs(R[0] - R[1]) > 8:
                bad.append((s % 512, round(R[0]), round(R[1])))
        print(f'mul x{a:.3f}: {len(starts) - len(bad)} of {len(starts)} bursts timed like FL', bad[:6])


def onsets():
    meta = json.load(open(os.path.join(paths.DATA, 'ons_meta.json')))
    good = total = 0
    for nm, key in (('onsA', 'A'), ('onsB', 'B')):
        SH = hp(fl_npy(f'e3m_{nm}_0'))
        for raw in (5850, -5850):
            a = C.mul(raw); n = int(len(SH) * a)
            Y = [hp(fl_npy(f'e3m_{nm}_{raw}')[:n]), hp(C.ours(f'e3_{nm}.wav', f'{nm}_{raw}', 0, 'e3gen', 1 / a)[:n])]
            for s in meta[key]:
                d = [best_offset(y, SH[s:s + 160], int(round(s * a)), 900)[0] for y in Y]
                good += abs(d[0] - d[1]) <= 3; total += 1
    print(f'onsets: {good} of {total} bursts placed within 3 samples of FL')


def gain():
    starts, src, SH = grid_source()
    SL = lp(src)
    for raw in (5850, -5850, 10000):
        a = C.mul(raw); n = int(len(src) * a)
        ys = [fl_npy(f'e3m_grid2_{raw}')[:n], C.ours('e3_grid2.wav', f'mgrid_{raw}', 0, 'e3gen', 1 / a)[:n] * C.GAIN]
        bands = [(hp(y), lp(y)) for y in ys]
        err = []
        for s in starts:
            gains = []
            for YH, YL in bands:
                c = int(round(s * a)) + best_offset(YH, SH[s:s + 200], int(round(s * a)), 900)[0]
                gains.append([np.dot(YL[c + t:c + t + 128], SL[s + t:s + t + 128]) / np.dot(SL[s + t:s + t + 128], SL[s + t:s + t + 128])
                              for t in range(0, 512, 128)])
            err.append(np.mean(np.abs(np.array(gains[0]) - np.array(gains[1]))))
        print(f'gain x{a:.3f}: mean error of the held low band {np.mean(err):.3f}')


if __name__ == '__main__':
    for what in sys.argv[1:] or ['pitch', 'mul', 'onsets', 'gain']:
        globals()[what]()
