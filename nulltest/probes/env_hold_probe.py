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

if __name__ == '__main__':
    for att, hold in PAIRS:
        for flags, bpms in ((4, BPMS), (5, (65, 130, 160))):
            for bpm in bpms:
                a, e = case(att, hold, bpm, flags)
                print(att, hold, bpm, flags, 'A', a, 'E', e, flush=True)
