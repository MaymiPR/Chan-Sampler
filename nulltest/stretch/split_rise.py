"""Where a stretch mode splits its vocoder from its grains as MUL grows
(E3_NOTES.md, round 13): noise stretched at each MUL, per third-octave band
from 1 kHz, the median correlation of 512-sample output blocks with the
source near their place. Pieces of the source (grains) give ~0.85 and up;
the vocoder follows a falling trend (0.85 at 1.6 kHz ... 0.5 at 10 kHz), so
the split is where a row jumps up from it.

    python -m stretch.make_signals                    # once (e3_noise.wav)
    python -m stretch.split_rise e3gen 1.5 2 2.5 3    # FL's renders (mode, MULs)
    python -m stretch.split_rise e3gen 2.5 3 -o       # ours, the stretcher alone"""
import math, sys
import numpy as np
from lib import flrender as F, paths, wavio
from stretch import common as C
from stretch.mode_probes import band, track

EDGES = [1000 * 2 ** (i / 3) for i in range(14)]   # 1 ... 20 kHz


def row(y, x, mul):
    t1 = min(len(y), int(len(x) * mul)) - 6000
    return [np.median(track(band(y, lo, hi), band(x, lo, hi), mul, t0=8192, t1=t1, step=512)[:, 2])
            for lo, hi in zip(EDGES[:-1], EDGES[1:])]


if __name__ == '__main__':
    ours = '-o' in sys.argv
    args = [a for a in sys.argv[1:] if a != '-o']
    mode, muls = args[0], [float(a) for a in args[1:]]
    x = wavio.read(paths.signal('e3_noise.wav'))[0][:, 0].astype(float)
    print('kHz        ' + ' '.join(f'{e / 1000:5.1f}' for e in EDGES[:-1]))
    for m in muls:
        raw = int(round(10000 * math.log2(m)))
        name = f'splitf_{mode}_{raw}'
        if ours:
            y = C.ours('e3_noise.wav', name, 0, mode, 1 / C.mul(raw), True)
        else:
            y = F.render(name, 'e3_noise.wav', [F.params(108, '<i', C.MODES[mode][0]), F.params(100, '<i', 0),
                                                F.params(104, '<i', raw)], beats=32)[:, 0]
        print(f'{"ours" if ours else "FL"} x{C.mul(raw):.3f} ' + ' '.join(f'{c:5.2f}' for c in row(y.astype(float), x, C.mul(raw))), flush=True)
