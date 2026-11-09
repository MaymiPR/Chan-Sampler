"""FL's arpeggiator TIME "Hold" (raw 1447), FL alone: which notes start and
when the sound ends. A 40 s sine (generated into probe/) shows the pitch of
every note. Measured: only the first step plays (the sequence's first note:
lowest for Up, highest for Down); keys pressed later, GATE and the sample
running out change nothing; the note ends when every key is up, and a new
press after that starts again."""
import os, numpy as np
from lib import flrender as F, wavio, paths

SPS = 44100 * 60 / 130 / 4   # samples per step at 130 BPM
SINE = os.path.join(paths.ROOT, 'probe', 'sine40.wav')
HOLD = [F.params(40, '<i', 1), F.params(52, '<i', 1447)]

def chord(length, start=0):
    return [(start, length, k, 100) for k in (60, 64, 67)]

def notes_played(y, seg=441):
    """(step, frequency) wherever the pitch changes or the sound starts / stops."""
    out, prev = [], None
    for i in range(0, len(y) - 4096, seg):
        s = y[i:i + 4096]
        f = 0.0 if np.abs(s).max() < 1e-3 else np.argmax(np.abs(np.fft.rfft(s * np.hanning(4096), 32768))) * 44100 / 32768
        if prev is None or abs(f - prev) > 10:
            out.append((round(i / SPS, 2), round(float(f), 1)))
        prev = f
    return out

def sound_span(y):
    idx = np.flatnonzero(np.abs(y) > 1e-4)
    return round(float(idx[0]) / SPS, 2), round(float(idx[-1]) / SPS, 2)

if __name__ == '__main__':
    if not os.path.exists(SINE):
        os.makedirs(os.path.dirname(SINE), exist_ok=True)
        t = np.arange(44100 * 40) / 44100
        wavio.write_float(SINE, np.stack([0.5 * np.sin(2 * np.pi * 440 * t)] * 2, 1), 44100)
    r = lambda name, patches, notes, beats, sample=SINE: F.render(name, sample, patches, beats=beats, notes=notes)[:, 0]
    print('chord held 32 beats:', notes_played(r('ah_up_hold32', HOLD, chord(32), 40)))
    print('down:', notes_played(r('ah_down', [F.params(40, '<i', 2), F.params(52, '<i', 1447)], chord(8), 10)))
    print('keys at 0 / 4 / 8 beats, then C+E at 20:', notes_played(r('ah_stagger', HOLD,
          [(0, 16, 60, 100), (4, 12, 64, 100), (8, 8, 67, 100), (20, 8, 60, 100), (20, 8, 64, 100)], 30)))
    print('first key up at 8 beats, other at 16 (sound span in steps):', sound_span(r('ah_end_own', HOLD, [(0, 8, 60, 100), (0, 16, 64, 100)], 18)))
    print('gate 50%:', sound_span(r('ah_gate50', HOLD + [F.params(56, '<i', 24)], [(0, 16, 60, 100)], 18)))
    print('3 s sample held 16 beats:', sound_span(r('ah_short1', HOLD, [(0, 16, 60, 100)], 18, 'signals/sine3.wav')))
