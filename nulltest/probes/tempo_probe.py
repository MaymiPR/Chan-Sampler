"""FL's envelope / LFO 'Tempo' switches (event 218 flags: 1 = envelope tempo,
2 = LFO tempo, 32 = LFO global phase) and the project tempo (event 156 = BPM x 1000)."""
import struct, numpy as np; from lib import flrender as F

def tempo(bpm):
    def p(ev):
        for e in ev:
            if e[0] == 156: e[1] = bytearray(struct.pack('<I', int(bpm * 1000))); return
        raise KeyError(156)
    return p

def lfo_period(name, bpm, flags, raw=20000):
    y = F.render(name, 'signals/dc20.wav', [tempo(bpm), F.env('vol', 'flags', flags), F.env('vol', 'lfo_amount', 128),
                                    F.env('vol', 'lfo_speed', raw), F.env('vol', 'lfo_delay', 100), F.env('vol', 'lfo_attack', 100)],
                 velocity=100, beats=40)
    g = y[:int(15 * 44100), 0]; g = g / np.median(np.abs(g[g != 0])) - 1 if False else g / 0.097412 - 1
    up = np.nonzero((g[:-1] < 0) & (g[1:] >= 0))[0]
    return float(np.mean(np.diff(up))) / 44100 if len(up) > 1 else None

def env_attack(name, bpm, flags, raw=20000):
    y = F.render(name, 'signals/dc6.wav', [tempo(bpm), F.env('vol', 'flags', flags), F.env('vol', 'env_on', 1), F.env('vol', 'attack', raw),
                                   F.env('vol', 'hold', 20000), F.env('vol', 'sustain', 128), F.env('vol', 'delay', 100)],
                 velocity=100, beats=8)
    g = np.abs(y[:, 0]); top = g[: 3 * 44100].max()
    return float(np.nonzero(g >= 0.999 * top)[0][0]) / 44100

if __name__ == '__main__':
    for bpm in (130, 65):
        for fl in (4, 6):
            print(f'LFO raw 20000  bpm {bpm:3d} flags {fl}: period {lfo_period(f"tp_lfo_{bpm}_{fl}", bpm, fl):.4f} s', flush=True)
    for bpm in (130, 65, 120):
        for fl in (4, 5):
            print(f'ENV attack 20000 bpm {bpm:3d} flags {fl}: attack {env_attack(f"tp_env_{bpm}_{fl}", bpm, fl):.4f} s', flush=True)
