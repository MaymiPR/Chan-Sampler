"""FL LFO period per raw speed: volume LFO (sine, full depth) on a DC sample,
period from the upward zero crossings of gain - 1. Also checks the tempo
dependence by rendering at another tempo when asked."""
import sys, json, numpy as np
from lib import flrender as F

def period(raw, beats=44):
    y = F.render(f'ls20_{raw}', 'signals/dc20.wav', [F.env('vol', 'lfo_amount', 128), F.env('vol', 'lfo_speed', raw),
                                         F.env('vol', 'lfo_delay', 100), F.env('vol', 'lfo_attack', 100)], velocity=100, beats=beats)
    g = y[:int(19.5 * 44100), 0] / 0.097412 - 1.0
    up = np.nonzero((g[:-1] < 0) & (g[1:] >= 0))[0]
    # sub-sample crossing positions
    xs = up + (-g[up]) / (g[up + 1] - g[up])
    if len(xs) >= 2:
        return float(np.mean(np.diff(xs))) / 44100, float(xs[0]) / 44100
    return None, None

if __name__ == '__main__':
    raws = [int(r) for r in sys.argv[1:]] or [200, 2000, 4000, 6000, 8000, 10000, 12000, 14000, 16000, 18000, 20000, 22000, 24000, 26000, 28000, 30000, 32950, 36000, 40000]
    out = {}
    for r in raws:
        p, first = period(r)
        out[r] = p
        print(r, p, first, flush=True)
    json.dump(out, open('data/lfo_periods.json', 'w'), indent=1)
