"""FL's arpeggiator TIME: renders FL alone at a given raw value and finds the note starts."""
import numpy as np; from lib import nullfl, flrender as F; from cases import arp_cases as A
def fl_only(name, raw_time, raw_gate=48):
    c = A.arp(name, 1)
    c['fl'] += [F.params(52, '<i', raw_time), F.params(56, '<i', raw_gate)]
    nullfl.render_pair(name, c['sample'], c['fl'], c['ours'], c['notes'], beats=8, force=True)
    a, _ = nullfl.render_pair(name, c['sample'])
    return a[:, 0]
def onsets(y, thr=1e-3):
    # note starts: where the signal returns after a gap or the pitch changes (sine3 notes)
    env = np.abs(y) > thr
    starts = [0]
    seg = 256
    fr = []
    for i in range(0, 44100 * 3, seg):
        s = y[i:i + 2048]
        if np.abs(s).max() < thr: fr.append(0); continue
        f = np.abs(np.fft.rfft(s * np.hanning(len(s)), 16384)); fr.append(np.argmax(f) * 44100 / 16384)
    fr = np.array(fr)
    ch = [i * seg for i in range(1, len(fr)) if abs(fr[i] - fr[i - 1]) > 20]
    return ch
if __name__ == '__main__':
    import sys
    for raw in [int(v) for v in sys.argv[1:]]:
        y = fl_only(f'at_{raw}', raw)
        o = onsets(y)
        d = np.diff(o)
        print(raw, 'first changes', o[:6], 'median spacing', np.median(d) if len(d) else None, 'steps', (np.median(d) / (44100 * 60 / 130 / 4)) if len(d) else None)
