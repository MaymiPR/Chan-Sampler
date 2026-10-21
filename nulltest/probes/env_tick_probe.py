"""FL's volume envelope on the song's tick grid (96 per beat): FL works the
envelope out once per tick from the note start and interpolates linearly in
between. Renders FL alone at several tempos and reports, per case, the stage
lengths in ticks read from the per-tick values (knots)."""
import sys, struct, numpy as np; from lib import flrender as F, wavio
from probes.tempo_probe import tempo

BPMS = (65, 77, 91, 100, 113, 143, 160, 177)
BASE = dict(env_on=1, delay=100, attack=100, hold=100, decay=100, sustain=128, release=100, att_tension=0, dec_tension=0, rel_tension=0)
SHAPES = {
    "dec20000": dict(decay=20000, sustain=64),
    "dec30000": dict(decay=30000, sustain=64),
    "hold5000": dict(hold=5000, sustain=0),
    "hold12000": dict(hold=12000, sustain=0),
    "att10000": dict(attack=10000),
    "del7000": dict(delay=7000),
}

def knots(y, bpm, sr=44100):
    T = sr * 60 / (bpm * 96); t = np.arange(len(y)); x = t / T; i = np.floor(x).astype(int); f = x - i
    K = i.max() + 2; A = np.zeros((K, K)); r = np.zeros(K)
    np.add.at(A, (i, i), (1 - f) ** 2); np.add.at(A, (i + 1, i + 1), f ** 2); np.add.at(A, (i, i + 1), f * (1 - f)); np.add.at(A, (i + 1, i), f * (1 - f))
    np.add.at(r, i, (1 - f) * y); np.add.at(r, i + 1, f * y)
    return np.linalg.solve(A + np.eye(K) * 1e-12, r), T

def case(sname, bpm, flags=4):
    kw = dict(BASE); kw.update(SHAPES[sname])
    patches = [tempo(bpm)] + [F.env('vol', k, v) for k, v in kw.items()] + [F.env('vol', 'flags', flags)]
    y = F.render(f"etp_{sname}_{bpm}_{flags}", "signals/dc20.wav", patches, velocity=100, beats=2)
    return y[:, 0] / 0.097412109375

if __name__ == '__main__':
    for sname in SHAPES:
        for bpm in BPMS:
            y = case(sname, bpm)
            v, T = knots(y[: int(1.5 * 60 / bpm * 44100)], bpm)
            print(sname, bpm, ' '.join('%.4f' % x for x in v[:4]), '|', 'T=%.2f' % T, flush=True)
            np.save(f"renders_fl/etp_{sname}_{bpm}_4.knots.npy", v)
