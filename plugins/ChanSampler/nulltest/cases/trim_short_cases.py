"""In-FL trims shorter than FL's shortest segment (SMP LENGTH close to 0), to
check how FL lengthens them: the segment's length in frames against FL's, and
starts within a few frames of the end of the sample.

    python -m cases.trim_short_cases [-f]"""
import sys
import numpy as np
from lib import flrender as F, nullfl

N = 88200   # signals/noise.wav


def frac(x):
    """A fraction FL's double and the plugin's float agree on (6 digits)."""
    return float(f'{x:.6g}')


CASES = []
for start, spans in ((0.3, (0, 1, 5, 20, 30, 39, 40, 41, 42, 45, 60)),
                     ((N - 1 - 30) / (N - 1), (0, 10, 20)),
                     ((N - 1 - 60) / (N - 1), (10, 30))):
    st = frac(start)
    for span in spans:
        ln = frac((span + 0.25) / ((N - 1) * (1 - st))) if span else 0.0   # rounds to span
        CASES.append(dict(name=f'trs_{st:.6f}_{span}', sample='signals/noise.wav',
                          fl=[F.params(128, '<d', st), F.params(136, '<d', ln)],
                          ours={'pcStart': st, 'pcLength': ln}, beats=2))


def sounding(x):
    """Frames from the first to the last sample above the 16-bit floor."""
    idx = np.nonzero(np.abs(x).max(axis=1) > 2.0 / 32768)[0] if x.ndim > 1 else np.nonzero(np.abs(x) > 2.0 / 32768)[0]
    return 0 if len(idx) == 0 else int(idx[-1] - idx[0] + 1)


if __name__ == '__main__':
    force = '-f' in sys.argv
    for c in CASES:
        a, b = nullfl.render_pair(c['name'], c['sample'], c['fl'], c['ours'], None, 100, c['beats'], force, None, None)
        d = nullfl.null_db(a, b)
        print(f"{F.verdict(d):5s} {d:7.1f} dB  {c['name']:28s} FL {sounding(a):4d} frames, ours {sounding(b):4d}", flush=True)
