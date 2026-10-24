"""Where FL ends a hold that follows an attack (the stage-end rounding when two
timed stages add up): attack + hold at nine tempos, Tempo off and on, read from
the per-tick values (env_tick_probe.knots). Prints attack ticks A (first tick at
full level) and the hold's end E (the tick before the fall to sustain 0)."""
import numpy as np; from lib import flrender as F
from probes.tempo_probe import tempo
from probes.env_tick_probe import knots

BPMS = (65, 77, 91, 100, 113, 130, 143, 160, 177)
PAIRS = [(10000, 10000), (20000, 20000), (10000, 20000), (20000, 5000), (5000, 12000), (0, 20000)]

def case(att, hold, bpm, flags):
    base = dict(env_on=1, delay=100, attack=max(100, att), hold=hold, decay=100, sustain=0, release=100, att_tension=0, dec_tension=0, rel_tension=0)
    patches = [tempo(bpm)] + [F.env('vol', k, v) for k, v in base.items()] + [F.env('vol', 'flags', flags)]
    y = F.render(f"ehp_{att}_{hold}_{bpm}_{flags}", "signals/dc20.wav", patches, velocity=100, beats=4)[:, 0] / 0.097412109375
    v, T = knots(y[: int(3.5 * 60 / bpm * 44100)], bpm)
    top = int(np.argmax(v > 0.9995))
    end = top + int(np.argmax(v[top:] < 0.9995)) - 1
    return top, end

# Params.cpp flEnvSeconds (the six-tempo table, log (t - c) interpolation)
RAWS = [1000, 1500, 2500, 5000, 7500, 10000, 12500, 15000, 17500, 20000, 22500, 25000, 27500, 30000, 32500, 35000,
        37500, 40000, 42500, 45000, 47500, 50000, 52500, 55000, 57500, 60000, 62500, 65536]
MS = [7.812, 11.088, 19.085, 44.334, 76.798, 119.861, 175.285, 247.253, 341.0, 463.086, 622.265, 828.828, 1098.203,
      1448.203, 1904.46, 2498.024, 3270.312, 4276.015, 5584.774, 7288.515, 9504.701, 12391.015, 16148.047, 21036.328,
      27396.947, 35677.991, 46458.69, 63999.14]

def env_seconds(raw):
    if raw <= 100: return 0.0
    if raw < 1000: return max(0.0, 0.0646133 * np.exp(6.8996 * (raw - 100) / 65436) - 0.0636541)
    i = next(k for k in range(27) if raw <= RAWS[k + 1]); t = (raw - RAWS[i]) / (RAWS[i + 1] - RAWS[i]); c = -63.6541
    return 0.001 * (np.exp(np.log(MS[i] - c) + (np.log(MS[i + 1] - c) - np.log(MS[i] - c)) * t) + c)

def sweep():
    """Hold alone over dense raw values at two tempos (data/env_hold_sweep.txt):
    E against the hold in ticks from the time table."""
    for bpm in (130, 77):
        for raw in range(15000, 25001, 250):
            a, e = case(0, raw, bpm, 4)
            th = env_seconds(raw) / (60 / (bpm * 96))
            print(bpm, raw, 'E', e, 'th', round(th, 3), 'E-th', round(e - th, 3), flush=True)

if __name__ == '__main__':
    import sys
    if '--sweep' in sys.argv:
        sweep()
        sys.exit()
    for att, hold in PAIRS:
        for flags, bpms in ((4, BPMS), (5, (65, 130, 160))):
            for bpm in bpms:
                a, e = case(att, hold, bpm, flags)
                print(att, hold, bpm, flags, 'A', a, 'E', e, flush=True)
