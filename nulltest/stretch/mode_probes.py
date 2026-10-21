"""What each stretch mode does, FL against ours, on test signals at MUL x1.5
and x0.667: clicks (timing of their onset, gain, energy before the onset,
how many copies), band levels on a saw and on noise, and whether the output
is pieces of the source (the best normalised correlation of short output
blocks with the source near their ideal place, low and high band: ~0.9+
for a time-domain splicer, ~0.2 for a spectral method) and how long the
pieces run.

    python -m stretch.make_signals                 # once
    python -m stretch.mode_probes e2gen e3mono      # modes (see common.MODES)

Used for the 1.2.0 stretch-mode work (docs/EVIDENCE.md, "Stretch modes")."""
import sys
import numpy as np
from numpy.fft import rfft, irfft
from lib import flrender as F, paths, wavio
from stretch import common as C

SR = 44100
SIGNALS = ['e3_clicks', 'e3_saw147', 'e3_noise', 'e3_bursts']
RAWS = [5850, -5850]


def source(smp):
    return wavio.read(paths.signal(smp + '.wav'))[0][:, 0].astype(float)


def fl(mode, smp, raw):
    if mode == 'src':
        return F.render(f'md_src_{smp}', smp + '.wav', [F.params(108, '<i', 0), F.params(100, '<i', 0)], beats=16)
    return F.render(f'md_{mode}_{smp}_{raw}', smp + '.wav',
                    [F.params(108, '<i', C.MODES[mode][0]), F.params(100, '<i', 0), F.params(104, '<i', raw)], beats=16)


def get(mode, smp, raw):
    """FL's output and ours, FL's channel gain removed (FL renders start at 0)."""
    a = fl(mode, smp, raw)[:, 0].astype(float) / C.GAIN
    o = C.ours(smp + '.wav', f'md_{mode}_{smp}_{raw}', 0, mode, 1 / C.mul(raw)).astype(float)
    return a, o


def band(x, lo, hi):
    X = rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR); X[(f < lo) | (f >= hi)] = 0
    return irfft(X, len(x))


def track(y, x, mul, W=512, step=256, span=3000, t0=4096, t1=None):
    """For output blocks [t, t + W): the best source offset d (source position
    t / mul + d) and its normalised correlation."""
    t1 = t1 or len(y) - W
    out = []
    for t in range(t0, t1, step):
        blk = y[t:t + W]; e = np.dot(blk, blk)
        c0 = int(round(t / mul)) - span
        if e < 1e-9 or c0 < 0 or c0 + 2 * span + W > len(x):
            continue
        seg = x[c0:c0 + 2 * span + W]
        n = 1
        while n < len(seg) + W:
            n *= 2
        cc = irfft(rfft(seg, n) * np.conj(rfft(blk, n)), n)[:2 * span + 1]
        es = np.convolve(seg ** 2, np.ones(W), 'valid')[:2 * span + 1]
        ncc = cc / np.sqrt(e * es + 1e-20)
        k = int(np.argmax(ncc))
        out.append((t, k - span, ncc[k]))
    return np.array(out)


def runs(y, x, mul, W=256, step=32, t0=4096, t1=None, minc=0.8):
    """Lengths of the runs where the output plays the source 1:1 (constant
    source-minus-output offset), in output samples."""
    tr = track(y, x, mul, W=W, step=step, span=1500, t0=t0, t1=t1)
    tr = tr[tr[:, 2] > minc]
    q = np.round(tr[:, 0] / mul + tr[:, 1] - tr[:, 0]).astype(int)
    out = []; s = 0
    for i in range(1, len(q)):
        if abs(q[i] - q[i - 1]) > 2 or tr[i, 0] - tr[i - 1, 0] > step:
            out.append(tr[i - 1, 0] - tr[s, 0] + W); s = i
    return np.array(out)


def clicks(y, x, mul):
    """Per source click: output onset vs its ideal place, peak gain, level
    before the onset (dB, relative to the peak), copies above 30% of the peak."""
    pk = np.abs(x); thr = 0.5 * pk.max(); pos = []; i = 0
    while i < len(x):
        if pk[i] > thr: pos.append(i); i += 4000
        else: i += 1
    rows = []
    for p in pos:
        t = int(round(p * mul))
        if t + 6000 > len(y) or t < 3000:
            continue
        w = y[t - 3000:t + 6000]; ref = x[p - 100:p + 400]
        peak = np.abs(w).max(); on = int(np.argmax(np.abs(w) > 0.1 * peak)) - 3000
        pre = np.sqrt(np.mean(w[:3000 + on - 32] ** 2)) / (peak + 1e-12)
        copies = 0; j = 0
        while j < len(w):
            if abs(w[j]) > 0.3 * peak: copies += 1; j += 300
            else: j += 1
        rows.append((on, peak / np.abs(ref).max(), 20 * np.log10(pre + 1e-9), copies))
    return np.array(rows)


def levels(y, x, mul):
    """Band levels (dB) of the output against the source, middle of the file."""
    r = []
    for lo, hi in ((50, 400), (400, 1400), (1400, 4000), (4000, 12000)):
        a = band(x, lo, hi); b = band(y, lo, hi); n = len(x)
        sa = a[n // 5:4 * n // 5]; sb = b[int(n // 5 * mul):int(4 * n // 5 * mul)]
        r.append(10 * np.log10(np.mean(sb ** 2) / np.mean(sa ** 2)))
    return r


def report(mode):
    print('=====', mode)
    for raw in RAWS:
        mul = C.mul(raw)
        x = source('e3_clicks'); a, o = get(mode, 'e3_clicks', raw)
        for nm, y in (('FL', a), ('ours', o)):
            r = clicks(y, x, mul)
            print(f' x{mul:.3f} clicks {nm:4s}: onset {r[:, 0].astype(int).tolist()}  gain {np.round(r[:, 1], 2).tolist()}'
                  f'  copies {r[:, 3].astype(int).tolist()}')
        for smp in ('e3_saw147', 'e3_noise'):
            x = source(smp); a, o = get(mode, smp, raw)
            for nm, y in (('FL', a), ('ours', o)):
                line = f' x{mul:.3f} {smp[3:]:7s} {nm:4s}: levels {np.round(levels(y, x, mul), 1).tolist()}'
                if smp == 'e3_noise':
                    t1 = min(len(y), int(len(x) * mul)) - 4096
                    for lo, hi, lab in ((0, 1300, 'low'), (1700, 20000, 'high')):
                        tr = track(band(y, lo, hi), band(x, lo, hi), mul, t1=t1)
                        line += f'  {lab} correlation {np.median(tr[:, 2]):.2f}'
                    line += f'  1:1 runs (median) {np.median(runs(y, x, mul, t1=t1)):.0f}'
                print(line)


if __name__ == '__main__':
    for m in sys.argv[1:] or list(C.MODES):
        report(m)
