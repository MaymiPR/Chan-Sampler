"""Where each stretch mode puts a note that starts softly (no attack, so no
start lock): noise swelling in and out (e3_am, and e3_amlo below 700 Hz for
the band below e3's split), at MUL x1.5 / x0.667 and +700 / -700 cents. The
lag (output samples) of the output's loudness against the source's at the
stretched time, FL's and ours. Each of FL's modes has its own; Tests/
EngineTests.cpp locks ours.

    python -m stretch.make_signals
    python -m stretch.soft_start e3gen stretch pro      # modes (see common.MODES)
    python -m stretch.soft_start e3gen --low            # the low-passed swell"""
import sys
import numpy as np
from lib import flrender as F, paths, wavio
from stretch import common as C

CASES = [(2 ** -0.585, 0, 5850), (2 ** 0.585, 0, -5850), (1.0, 700, 0), (1.0, -700, 0)]   # speed, cents, FL's MUL


def smooth(e, w=1024):
    return np.convolve(e, np.ones(w) / w, 'same')


def lag(y, speed, env2):
    """Output samples by which y's loudness trails the source's at the
    stretched time (positive: late)."""
    t = np.arange(len(env2))
    ideal = np.interp(np.arange(len(y)) * speed, t, env2)
    a, b = smooth(y ** 2), smooth(ideal)
    s0, s1 = 8000, len(y) - 8000
    return max(range(-600, 601, 4), key=lambda d: np.dot(a[s0 + d:s1 + d], b[s0:s1]) / np.sqrt(np.dot(a[s0 + d:s1 + d], a[s0 + d:s1 + d])))


def run(mode, low=False):
    name = 'e3_amlo' if low else 'e3_am'
    x = wavio.read(paths.signal(name + '.wav'))[0][:, 0]
    env2 = (0.3 * np.sin(np.pi * np.arange(len(x)) / 14700) ** 2) ** 2   # the swell's power envelope
    r = []
    for speed, cents, raw in CASES:
        fl = F.render(f'{name[3:]}_{mode}_{raw}_{cents}', name + '.wav',
                      [F.params(108, '<i', C.MODES[mode][0]), F.params(100, '<i', cents), F.params(104, '<i', raw)], beats=16)[:, 0] / C.GAIN
        ours = C.ours(name + '.wav', f'{name}_{mode}_{raw}_{cents}', cents, mode, speed)
        n = int(len(x) / speed)
        r.append('x%.3f %+d: FL %+d ours %+d' % (1 / speed, cents, lag(fl[:n], speed, env2), lag(ours, speed, env2)))
    print('%-8s' % mode, ' | '.join(r), flush=True)


if __name__ == '__main__':
    low = '--low' in sys.argv
    for m in [a for a in sys.argv[1:] if not a.startswith('--')] or list(C.MODES):
        run(m, low)
