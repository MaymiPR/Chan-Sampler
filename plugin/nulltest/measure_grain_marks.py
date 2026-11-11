"""The grain modes' period and marks, FL against ours: pulse trains of known
period over a faint noise floor (the floor makes every 1:1 piece of the
source identifiable), stretched by MUL. For each splice, the jump (the
period the mode used) and where its crossfade sits in the source against
the pulses (whether the marks follow the waveform). Writes its own signals
to probe/mono/.

    python -m measure_grain_marks e3mono e2mono e2speech      # modes (see stretch_lib.MODES)
    python -m measure_grain_marks e2mono --periods 300 1000   # some periods only
    python -m measure_grain_marks e3mono --raw -5850          # MUL x0.667 (default 5850, x1.5)"""
import os, sys
import numpy as np
from numpy.fft import rfft, irfft
import ref_solo_lib as F; import wav_lib
import stretch_lib as C

SR = 44100
PERIODS = (64, 100, 150, 220, 300, 450, 600, 800, 1000, 1200, 1500, 2000)
FOLDER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'probe', 'mono')


def signal(P, seconds=3.0, width=24, floor=0.005):
    """A pulse train of period P (a Hann bump `width` wide, peak 0.5, the
    first at sample 1000) over noise at `floor` RMS, as a file."""
    name = f'mm_pulse{P}'
    path = os.path.join(FOLDER, name + '.wav')
    if not os.path.exists(path):
        os.makedirs(FOLDER, exist_ok=True)
        n = int(seconds * SR)
        x = np.random.default_rng(P).standard_normal(n) * floor
        bump = 0.5 * np.hanning(width + 2)[1:-1]
        for c in range(1000, n - width, P):
            x[c - width // 2:c - width // 2 + width] += bump
        wav_lib.write_float(path, np.stack([x, x], 1).astype(np.float32), SR)
    return path


def smap(y, x, mul, W=48, step=8, span=3000, t0=2048, t1=None):
    """For output windows [t, t + W): the source offset (source - output)
    that matches best, and its normalised correlation."""
    t1 = t1 or min(len(y), int(len(x) * mul)) - 4096
    n = 1
    while n < 2 * span + 2 * W:
        n *= 2
    out = []
    for t in range(t0, t1, step):
        blk = y[t:t + W]; e = np.dot(blk, blk)
        c0 = int(round(t / mul)) - span
        if e < 1e-9 or c0 < 0 or c0 + 2 * span + W > len(x):
            continue
        seg = x[c0:c0 + 2 * span + W]
        cc = irfft(rfft(seg, n) * np.conj(rfft(blk, n)), n)[:2 * span + 1]
        es = np.convolve(seg ** 2, np.ones(W), 'valid')[:2 * span + 1]
        ncc = cc / np.sqrt(e * es + 1e-20)
        k = int(np.argmax(ncc))
        out.append((t, c0 + k - t, ncc[k]))
    return np.array(out)


def segments(m, minc=0.97, minlen=3):
    """Runs of one offset: (output start, output end, offset)."""
    good = m[m[:, 2] > minc]
    segs, s = [], 0
    for i in range(1, len(good) + 1):
        if i == len(good) or abs(good[i, 1] - good[i - 1, 1]) > 1 or good[i, 0] - good[i - 1, 0] > 24:
            if i - s >= minlen:
                segs.append((int(good[s, 0]), int(good[i - 1, 0]) + 48, int(np.median(good[s:i, 1]))))
            s = i
    return segs


def crossfade_mid(y, x, A, B, W=9):
    """Where the crossfade from segment A to B is half way (output sample),
    fitted as y = a x[t + qA] + b x[t + qB]; None when it can't be seen."""
    t0, t1 = max(A[0], A[1] - 700), min(B[1], B[0] + 700)
    if t1 - t0 < 40:
        return None
    ts, bs = [], []
    for t in range(t0, t1):
        idx = np.arange(t - W // 2, t + W // 2 + 1)
        X = np.stack([x[idx + A[2]], x[idx + B[2]]], 1)
        ab, *_ = np.linalg.lstsq(X, y[idx], rcond=None)
        ts.append(t); bs.append(ab[1] / max(ab[0] + ab[1], 1e-9))
    b = np.convolve(bs, np.ones(15) / 15, 'same')
    i = np.nonzero(b > 0.5)[0]
    return ts[i[0]] if len(i) else None


def analyse(y, x, mul, P):
    """Jumps (source samples, median and share within 2 of a multiple of P)
    and the crossfade midpoints' phase against the pulses (source, -P/2..P/2)."""
    sg = segments(smap(y, x, mul))
    jumps = np.diff([g[2] for g in sg])
    phases = []
    for A, B in zip(sg[:-1], sg[1:]):
        t = crossfade_mid(y, x, A, B)
        if t is not None:
            s = t + A[2]   # source position A plays at the midpoint
            phases.append(((s - 1000 + P / 2) % P) - P / 2)
    jumps = jumps[jumps != 0]
    on = np.mean(np.abs(np.abs(jumps) - P * np.round(np.abs(jumps) / P)) <= 2) if len(jumps) else np.nan
    return np.abs(jumps), on, np.array(phases)


def main():
    args = sys.argv[1:]
    raw = 5850
    periods = PERIODS
    if '--raw' in args:
        i = args.index('--raw'); raw = int(args[i + 1]); del args[i:i + 2]
    if '--periods' in args:
        i = args.index('--periods'); periods = [int(a) for a in args[i + 1:]]; args = args[:i]
    mul = C.mul(raw)
    for mode in args or ('e3mono', 'e2mono', 'e2speech'):
        print(f'===== {mode} x{mul:.3f}')
        for P in periods:
            path = signal(P)
            x = wav_lib.read(path)[0][:, 0].astype(float)
            a = F.render(f'mm_{mode}_{P}_{raw}', path, [F.params(108, '<i', C.MODES[mode][0]), F.params(100, '<i', 0),
                                                       F.params(104, '<i', raw)], beats=16)[:, 0] / C.GAIN
            o = C.ours(path, f'mm_{mode}_{P}_{raw}', 0, mode, 1 / mul).astype(float)
            line = f'  P {P:5d}'
            for nm, y in (('FL', a), ('ours', o)):
                j, on, ph = analyse(y, x, mul, P)
                med = int(np.median(j)) if len(j) else -1
                pq = np.percentile(ph, [10, 50, 90]).astype(int).tolist() if len(ph) else []
                line += f' | {nm:4s} jump med {med:5d} on P {on:4.2f} phase {pq}'
            print(line, flush=True)


if __name__ == '__main__':
    main()
