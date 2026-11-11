"""Writes the base test signals into signals/ (they are generated, not stored in
the repository). Python's standard library only, so CI can run it as is.

    python nulltest/make_base_signals.py              # every signal that is missing
    python nulltest/make_base_signals.py sine.wav     # just these
    python nulltest/make_base_signals.py -f           # rewrite them all

Existing files are kept unless -f is given, so cached FL renders made from them
stay valid. Until 2026-11-01 these signals were stored in the repository; the
ones written here follow the same layout (lengths, levels, frequencies, markers),
the noises from fixed seeds, so FL renders made before then need re-rendering
from these.

The signals (44.1 kHz, 32-bit float stereo unless noted):
  sine / sine_small / sine3     440 Hz at 0.25 / 0.05 for 2 s; 220 Hz at 0.2 for 3 s
  dc / dc6 / dc20               0.25 for 2 / 6 / 20 s
  dcsine                        0.1 + 0.2 sin 440 Hz, 2 s
  impulse / impulse1000         one 0.5 click at 0 / 1000, 3 s
  impulses50                    0.5 clicks every 50 samples from 1000, 3 s
  imps22 / imps48 / imps96      0.5 clicks every 101 / 101 / 151 samples from 1000 at 22.05 / 48 / 96 kHz
  noise / noise_q               white noise (rms 0.083) and the same at a quarter, 2 s, channels independent
  decay                         white noise decaying from 0.5 s, 2 s, channels independent
  loud                          60 Hz tone, a quiet one, loud noise, quiet noise, each 0.125 s, four times
  freqsteps                     0.25 s each of 100, 400, 1600, 6400, 12000 Hz at 0.5, then 400 Hz falling in level
  freqsweep                     exponential sweep from 27 Hz, 6 s at 0.5 (aliasing past Nyquist at the end)
  cue_tone                      440 Hz at 0.5, 1 s, two cue points (13230, 30869)
  strc_tone / strc_jump         the same tone (strc_jump: 0.1 then 0.8 from 0.5 s) with an FL slice chunk
  smpl_loop                     220 Hz at 0.5, 1 s, 16-bit mono, a smpl loop 11025 ... 33075
  bits16_1ch / bits24_2ch       a decaying 220 Hz tone, 16-bit mono / 24-bit stereo (format badges)
  e3_clicks                     short 1 kHz clicks every 0.5 s, 4 s
  e3_loop                       a synthetic drum loop (kick, hat, snare, hat), 3.75 s
  e3_noise                      white noise (rms 0.2), 1 s, mono"""
import math, os, random, struct, sys
from array import array

SR = 44100
SIGNALS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'signals')


def _chunk(cid, body):
    return cid + struct.pack('<I', len(body)) + body + (b'\0' if len(body) & 1 else b'')


def _write(path, chans, sr=SR, bits=32, extra=b''):
    n = len(chans[0])
    if bits == 32:
        inter = array('f', (chans[c][i] for i in range(n) for c in range(len(chans))))
        fmt = struct.pack('<HHIIHH', 3, len(chans), sr, sr * len(chans) * 4, len(chans) * 4, 32)
        data = inter.tobytes()
    else:
        scale, width = (32767, 2) if bits == 16 else (8388607, 3)
        out = bytearray()
        for i in range(n):
            for c in chans:
                v = max(-scale - 1, min(scale, int(round(c[i] * (scale + 1)))))
                out += (v & ((1 << (8 * width)) - 1)).to_bytes(width, 'little')
        fmt = struct.pack('<HHIIHH', 1, len(chans), sr, sr * len(chans) * width, len(chans) * width, bits)
        data = bytes(out)
    body = b'WAVE' + _chunk(b'fmt ', fmt) + _chunk(b'data', data) + extra
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', len(body)) + body)


def _tone(f, a, secs, sr=SR):
    w = 2 * math.pi * f / sr
    return [a * math.sin(w * i) for i in range(int(secs * sr))]


def _clicks(n, start, step, sr=SR):
    x = [0.0] * n
    for i in range(start, n, step):
        x[i] = 0.5
    return x


def _noise(seed, n, sigma):
    r = random.Random(seed)
    return [r.gauss(0.0, sigma) for _ in range(n)]


def _decay_hit(n, f, a, tau, sr=SR):
    return [a * math.exp(-i / (tau * sr)) * math.sin(2 * math.pi * f * i / sr) for i in range(n)]


def _cue(points):
    body = struct.pack('<I', len(points))
    for k, p in enumerate(points, 1):
        body += struct.pack('<II4sIII', k, p, b'data', 0, 0, p)
    return _chunk(b'cue ', body)


def _strc(points):
    # FL's slice chunk as FL reads it: a header, a type-2 entry, one at 0, then the markers.
    body = bytearray(124)
    struct.pack_into('<IIIIII', body, 0, 28, 4, 0, 0, 1, 10)
    struct.pack_into('<I', body, 28, 2)
    for k, p in enumerate(points):
        struct.pack_into("<I", body, 80 + 24 * k, p)
    return _chunk(b'strc', bytes(body))


def _smpl(start, end, note=60):
    body = struct.pack('<IIIIIIIII', 0, 0, int(1e9 / SR), note, 0, 0, 0, 1, 0)
    body += struct.pack('<IIIIII', 0, 0, start, end, 0, 0)
    return _chunk(b'smpl', body)


def signals():
    s = {}
    for name, f, a, secs in (('sine', 440, 0.25, 2), ('sine_small', 440, 0.05, 2), ('sine3', 220, 0.2, 3)):
        x = _tone(f, a, secs); s[name] = lambda p, x=x: _write(p, [x, x])
    for name, secs in (('dc', 2), ('dc6', 6), ('dc20', 20)):
        s[name] = lambda p, secs=secs: _write(p, [[0.25] * (SR * secs)] * 2)
    s['dcsine'] = lambda p: _write(p, [[0.1 + v for v in _tone(440, 0.2, 2)]] * 2)
    s['impulse'] = lambda p: _write(p, [_clicks(3 * SR, 0, 3 * SR)] * 2)
    s['impulse1000'] = lambda p: _write(p, [_clicks(3 * SR, 1000, 3 * SR)] * 2)
    s['impulses50'] = lambda p: _write(p, [_clicks(3 * SR, 1000, 50)[:130000] + [0.0] * (3 * SR - 130000)] * 2)
    s['imps22'] = lambda p: _write(p, [_clicks(80000, 1000, 101)[:78000] + [0.0] * 2000] * 2, sr=22050)
    s['imps48'] = lambda p: _write(p, [_clicks(144000, 1000, 101)[:140000] + [0.0] * 4000] * 2, sr=48000)
    s['imps96'] = lambda p: _write(p, [_clicks(288000, 1000, 151)[:280000] + [0.0] * 8000] * 2, sr=96000)

    def noise(p, gain=1.0):
        _write(p, [[gain * v for v in _noise(seed, 2 * SR, 0.0833)] for seed in (1, 2)])
    s['noise'] = noise
    s['noise_q'] = lambda p: noise(p, 0.25)

    def decay(p):
        env = [1.0 if i < SR // 2 else math.exp(-(i - SR // 2) / (0.12 * SR)) for i in range(2 * SR)]
        _write(p, [[e * v for e, v in zip(env, _noise(seed, 2 * SR, 0.1))] for seed in (3, 4)])
    s['decay'] = decay

    def loud(p):
        q = SR // 8; r = random.Random(5); x = []
        for _ in range(4):
            x += _tone(60, 0.8, q / SR) + _tone(36, 0.09, q / SR)
            x += [r.gauss(0.0, 0.2) for _ in range(q)] + [r.gauss(0.0, 0.025) for _ in range(q)]
        x += [0.0] * (2 * SR - len(x))
        _write(p, [x, [0.99 * v for v in x]])
    s['loud'] = loud

    def freqsteps(p):
        q = SR // 4; x = []
        for f in (100, 400, 1600, 6400, 12000):
            x += _tone(f, 0.5, q / SR)
        for a in (0.5, 0.25, 0.1):
            x += _tone(400, a, q / SR)
        _write(p, [x, x])
    s['freqsteps'] = freqsteps

    def freqsweep(p):
        n = 6 * SR; ph = 0.0; x = []
        for i in range(n):
            x.append(0.5 * math.sin(ph)); ph += 2 * math.pi * 27 * 4 ** (i / SR) / SR
        _write(p, [x, x])
    s['freqsweep'] = freqsweep

    tone = _tone(440, 0.5, 1)
    s['cue_tone'] = lambda p: _write(p, [tone, tone], extra=_cue([13230, 30869]))
    s['strc_tone'] = lambda p: _write(p, [tone, tone], extra=_strc([13230, 30869]))
    jump = [v * (0.2 if i < SR // 2 else 1.6) for i, v in enumerate(tone)]
    s['strc_jump'] = lambda p: _write(p, [jump, jump], extra=_strc([13230, 30869]))
    s['smpl_loop'] = lambda p: _write(p, [_tone(220, 0.5, 1)], bits=16, extra=_smpl(11025, 33075))
    fade = [0.6 * math.exp(-3 * i / SR) * math.sin(2 * math.pi * 220 * i / SR) for i in range(SR)]
    s['bits16_1ch'] = lambda p: _write(p, [fade], bits=16)
    s['bits24_2ch'] = lambda p: _write(p, [fade, fade], bits=24)

    def e3_clicks(p):
        x = [0.0] * (4 * SR); hit = _decay_hit(350, 992, 0.84, 0.002)
        for k in range(1, 8):
            i = k * SR // 2; x[i:i + 350] = hit
        _write(p, [x, x])
    s['e3_clicks'] = e3_clicks

    def e3_loop(p):
        r = random.Random(6); beat = 165375 // 16; x = [0.0] * 165375
        kick = _decay_hit(beat, 50, 0.35, 0.08)
        hat = [0.37 * math.exp(-i / (0.01 * SR)) * r.uniform(-1, 1) for i in range(beat)]
        snare = [0.6 * math.exp(-i / (0.04 * SR)) * (r.uniform(-1, 1) * 0.5 + math.sin(2 * math.pi * 180 * i / SR)) for i in range(beat)]
        for b in range(16):
            hit = (kick, hat, snare, hat)[b % 4]
            for i, v in enumerate(hit):
                x[b * beat + i] += v
        _write(p, [x, x])
    s['e3_loop'] = e3_loop

    def e3_noise(p):
        r = random.Random(7)
        _write(p, [[r.gauss(0.0, 0.2) for _ in range(SR)]])
    s['e3_noise'] = e3_noise
    return s


def main(args):
    force = '-f' in args
    only = {a[:-4] if a.endswith('.wav') else a for a in args if a != '-f'}
    os.makedirs(SIGNALS, exist_ok=True)
    made = 0
    for name, write in signals().items():
        p = os.path.join(SIGNALS, name + '.wav')
        if (not only or name in only) and (force or not os.path.exists(p)):
            write(p); made += 1
    print(made, 'signals written in', SIGNALS)


if __name__ == '__main__':
    main(sys.argv[1:])
