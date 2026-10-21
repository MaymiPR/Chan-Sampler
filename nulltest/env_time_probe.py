"""FL's envelope times without the tick rounding: FL counts every stage in whole
song ticks, N = ceil(x / tick + 0.5) (see env_tick_probe.py), so a time measured
at one tempo is only known to a tick. Measuring the attack (a straight rise over
N ticks) at several tempos and intersecting the intervals gives the knob's real
time x. Writes env_times.txt: raw, x_low, x_high (seconds)."""
import os, math, numpy as np, flrender as F, wavio
from tempo_probe import tempo

HERE = os.path.dirname(os.path.abspath(__file__))
BPMS = (77, 100, 143, 177, 250, 400)
RAWS = [200, 400, 700, 1000, 1500] + list(range(2500, 62501, 2500)) + [65536]
TABLE = [24.036, 48.072, 81.723, 124.988, 177.868, 250.009, 346.188, 466.365, 624.989, 831.738, 1100.945, 1451.91, 1908.638,
         2504.798, 3274.036, 4278.807, 5591.254, 7293.248, 9509.471, 12393.98, 16149.056, 21038.385, 27398.73, 35683.054, 46455.421]

def table_seconds(raw):   # Params.cpp flEnvSeconds (measured at 130 BPM)
    if raw <= 5000: return raw * 48.072 / 5000 * 0.001
    if raw >= 62500: return 0.001 * math.exp(math.log(46455.421) + (math.log(64004.629) - math.log(46455.421)) * (raw - 62500) / 3036)
    pos = raw / 2500 - 1; i = min(23, int(pos)); t = pos - i
    return 0.001 * math.exp(math.log(TABLE[i]) + (math.log(TABLE[i + 1]) - math.log(TABLE[i])) * t)

def dc_sample():
    path = os.path.join(HERE, "dc70.wav")
    if not os.path.exists(path):
        wavio.write_float(path, np.full((70 * 44100, 2), 0.25), 44100)
    return "dc70.wav"

def attack_ticks(raw, bpm):
    x = table_seconds(raw)
    beats = max(1, math.ceil(x * bpm / 60 * 1.1) + 1)
    base = dict(env_on=1, delay=100, attack=raw, hold=20000, decay=100, sustain=128, release=100, att_tension=0, dec_tension=0, rel_tension=0)
    patches = [tempo(bpm)] + [F.env('vol', k, v) for k, v in base.items()] + [F.env('vol', 'flags', 4)]
    y = F.render(f"etm_att{raw}_{bpm}", dc_sample(), patches, velocity=100, beats=beats)[:, 0] / 0.097412109375
    y = y[: max(2, int(np.argmax(y >= 0.9995)))]   # the rise only (the release follows)
    n = np.arange(len(y)); m = (y > 0.05) & (y < 0.95)
    if m.sum() < 2:   # shorter than a tick or two: count from the knots
        T = 44100 * 60 / (bpm * 96)
        return round(len(y) / T)
    slope = np.polyfit(n[m], y[m], 1)[0]
    return 1.0 / (slope * 44100 * 60 / (bpm * 96))

if __name__ == '__main__':
    import sys
    out = open(os.path.join(HERE, "env_times.txt"), "w")
    for raw in RAWS:
        if len(sys.argv) > 1 and not os.path.exists(os.path.join(F.OUT, f"etm_att{raw}_{BPMS[-1]}.wav")): break   # analyse what is rendered
        lo, hi, ns = 0.0, 1e9, []
        for bpm in BPMS:
            nf = attack_ticks(raw, bpm); n = round(nf); ns.append(nf)
            r = bpm * 96 / 60
            lo = max(lo, (n - 1.5) / r); hi = min(hi, (n - 0.5) / r)
        line = f"{raw} {lo:.6f} {hi:.6f}  table {table_seconds(raw):.6f}  N {' '.join('%.3f' % v for v in ns)}"
        print(line, flush=True); out.write(line + "\n"); out.flush()
