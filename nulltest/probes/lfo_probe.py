"""Measure FL's LFO curves: DC sample (dc20.wav), one INS target's LFO set by
raw event-218 fields, returns the per-sample output ratio to the LFO-off render."""
import sys, numpy as np
from lib import flrender as F

def curve(name, target='vol', beats=16, **lfo):
    base = F.render('lp_base', 'signals/dc20.wav', [], velocity=100, beats=beats)
    fields = dict(lfo_delay=100, lfo_attack=100, lfo_amount=128, lfo_speed=32950, lfo_shape=0)
    fields.update(lfo)
    y = F.render('lp_' + name, 'signals/dc20.wav', [F.env(target, k, v) for k, v in fields.items()], velocity=100, beats=beats)
    n = min(len(y), len(base))
    return y[:n], base[:n]

if __name__ == '__main__':
    y, b = curve('vol_a128')
    r = y[:, 0] / np.where(np.abs(b[:, 0]) > 1e-6, b[:, 0], np.nan)
    for t in np.arange(0, 6, 0.125):
        i = int(t * 44100); print(f'{t:6.3f} {r[i]:.5f}')
