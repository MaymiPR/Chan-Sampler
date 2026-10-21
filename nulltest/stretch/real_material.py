"""Each stretch mode against FL on real material: FL's own samples (707 Kick,
a Rhodes, a sung "ah", DL Groover; read from the FL install, never copied
here) and e3_loop, at -700 / +700 cents and MUL x1.5 / x0.667. FL's Sampler
renders alone; ours is the stretcher alone, fed FL's own decode of each
sample (its Resample render). Prints envelope / band-spectrum distance and
the fitted null (dB) per case and the means.

    python -m stretch.real_material e3gen e3mono       # modes (see common.MODES)
    python -m stretch.real_material e3gen -f           # re-render ours

Used for the 1.2.0 stretch-mode work (docs/EVIDENCE.md, "Stretch modes")."""
import os, sys
import numpy as np
from lib import flrender as F, paths, wavio
from cases import e3_pitch_cases as E
from stretch import common as C
from stretch.metrics import envdist, specdist, null

SAMPLES = ('k707', 'rhodes', 'vox', 'groove', 'loop')
SETTINGS = [('p-700', -700, 0), ('p700', 700, 0), ('m15', 0, 5850), ('m067', 0, -5850)]
LENGTH = {'k707': 12000, 'rhodes': 24000, 'vox': 66000, 'groove': 66000, 'loop': 66000}   # samples compared at x1


def fl(mode, k, setting):
    """FL's render (cached in renders/); mode 'src' = FL's Resample, unpitched."""
    path = E.SAMPLES[k][0]
    if mode == 'src':
        return F.render(f'mr_src_{k}', path, [F.params(108, '<i', 0), F.params(100, '<i', 0)], beats=8)
    _, cents, raw = next(s for s in SETTINGS if s[0] == setting)
    return F.render(f'mr_{mode}_{k}_{setting}', path,
                    [F.params(108, '<i', C.MODES[mode][0]), F.params(100, '<i', cents), F.params(104, '<i', raw)], beats=8)


def source(k):
    """FL's decode of a sample (its Resample render, gain removed) as a file."""
    out = os.path.join(C.OURS, 'src_' + k + '.wav')
    if not os.path.exists(out):
        os.makedirs(C.OURS, exist_ok=True)
        y = fl('src', k, None) / C.GAIN
        end = np.nonzero(np.abs(y).max(1) > 1e-5)[0][-1] + 1
        wavio.write_float(out, y[:end], 44100)
    return out


def score(mode, force=False, verbose=True):
    E_, S_, N_ = [], [], []
    for k in SAMPLES:
        row = []
        for setting, cents, raw in SETTINGS:
            n = int(LENGTH[k] * C.mul(raw))
            a = fl(mode, k, setting)[:n, 0] / C.GAIN
            o = C.ours(source(k), f'{mode}_{k}_{setting}', cents, mode, 1 / C.mul(raw), force)[:n]
            e, s, nf = envdist(a, o), specdist(a, o), null(a, o)
            E_.append(e); S_.append(s); N_.append(nf)
            row.append('%4.1f/%4.1f/%5.1f' % (e, s, nf))
        if verbose:
            print('  %-7s ' % k + ' '.join(row), flush=True)
    print('%-9s env %.2f spec %.2f null %.2f' % (mode, np.mean(E_), np.mean(S_), np.mean(N_)), flush=True)
    return np.mean(E_), np.mean(S_), np.mean(N_)


if __name__ == '__main__':
    for m in [a for a in sys.argv[1:] if a != '-f'] or list(C.MODES):
        score(m, '-f' in sys.argv)
