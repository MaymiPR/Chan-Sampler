"""e3's band below the split after an attack (E3_NOTES.md, round 10).

FL's e3 restarts the low band's phases from its loudest point after an
attack: the loudest sample below the split 660 ... 1172 samples after the
attack's start (20 earlier when the attack is timed by its peak), played
where the stretch puts it. A steady partial then comes out turned by its
frequency x (stretch - 1) x where that point is, so the low band's phase
against ours says, per attack, how far FL's point is from ours.

    python -m stretch.lowband probe 5850          # steady tones switched on by a click, onsets
    python -m stretch.lowband probe 0 700         #   stepped through 512 samples (MUL raw, cents)
    python -m stretch.lowband case vox 700        # an in-FL e3 pitch case: the phase delay over time

probe: five steady tones (211 ... 1129 Hz) from the onset plus a treble
click (an attack); after it, the tones' phases FL against ours (the
stretcher alone) as the restart point's offset FL - ours, in source samples,
and how well one offset fits all five (degrees). case: the low band of
FL's half against ours, block by block, as a phase delay (output samples)."""
import os, subprocess, sys
import numpy as np
from numpy.fft import rfft, irfft
from lib import flrender as F, paths, wavio
from stretch import common as C

SR = 44100
TONES = [211.3, 389.7, 617.1, 877.9, 1129.3]


def signal(onset):
    name = f'e3_lbatt_{onset}'
    path = os.path.join(paths.SIGNALS, name + '.wav')
    if not os.path.exists(path):
        n = 3 * SR; t = np.arange(n); x = np.zeros(n)
        phase = np.random.default_rng(3).uniform(0, 2 * np.pi, len(TONES))
        for f, q in zip(TONES, phase):
            x += 0.08 * np.sin(2 * np.pi * f * t / SR + q)
        x[:onset] = 0
        b = np.random.default_rng(4).standard_normal(64); B = rfft(b, 256); B[np.fft.rfftfreq(256, 1 / SR) < 3000] = 0
        x[onset:onset + 256] += 0.5 * irfft(B, 256)
        wavio.write_float(path, x.astype(np.float32), SR)
    return name


def phasor(y, f, t0, W=8192):
    t = np.arange(t0, t0 + W)
    return (y[t0:t0 + W] * np.hanning(W)) @ np.exp(-2j * np.pi * f * t / SR)


def probe(raw, cents=0, step=16):
    ratio = 2 ** (cents / 1200); a1 = C.mul(raw) * ratio - 1
    for onset in range(4096, 4608, step):
        name = signal(onset)
        fl = F.render(f'lba_{name}_{raw}_{cents}', name + '.wav', [F.params(108, '<i', C.MODES['e3gen'][0]), F.params(100, '<i', cents),
                      F.params(104, '<i', raw)], beats=16)[:, 0].astype(float) / C.GAIN
        ours = C.ours(name + '.wav', f'lba_{name}_{raw}_{cents}', cents, 'e3gen', 1 / C.mul(raw), force=True).astype(float)
        t0 = int(onset * (1 + a1)) + 12000
        e = np.array([np.angle(phasor(fl, f * ratio, t0) / phasor(ours, f * ratio, t0)) for f in TONES])
        offs = np.arange(-3000, 3000, 0.5)
        err = np.mean([np.abs(np.angle(np.exp(1j * (ei + 2 * np.pi * f * a1 * offs / SR)))) for ei, f in zip(e, TONES)], 0)
        i = int(np.argmin(err)); i0 = int(np.argmin(np.abs(offs)))
        print(f'onset {onset} (grid {onset % 512:3d}): FL - ours {offs[i]:7.1f} samples (fits {np.degrees(err[i]):4.1f} deg; '
              f'as is {np.degrees(err[i0]):5.1f} deg)', flush=True)


def case(key, cents, B=16):
    from lib import nullfl
    from cases import e3_pitch_cases as E
    cs = next(x for x in E.CASES if x['name'] == f'e3p_{key}_{cents}')
    a, b = nullfl.render_pair(cs['name'], cs['sample'], cs['fl'], cs['ours'], beats=cs['beats'])
    N, H = 2048, 256; w = np.hanning(N)
    stft = lambda x: np.array([rfft(x[i:i + N] * w) for i in range(0, len(x) - N, H)])
    A, O = stft(a[:, 0].astype(float)), stft(b[:, 0].astype(float))
    f = np.fft.rfftfreq(N, 1 / SR); m = (f > 80) & (f < 1300)
    Ds = np.arange(-1500, 1500, 1.0); E_ = np.exp(2j * np.pi * np.outer(Ds, f[m]) / SR)
    for i in range(0, len(A) - B, B // 2):
        Sx = (A[i:i + B, m] * np.conj(O[i:i + B, m])).sum(0); lev = 10 * np.log10((np.abs(A[i:i + B, m]) ** 2).sum() + 1e-20)
        if lev < -40:
            continue
        sc = np.abs(E_ @ Sx); j = int(np.argmax(sc)); tot = np.abs(Sx).sum() + 1e-30
        print(f'{i * H:6d}: phase delay {Ds[j]:6.0f} (fits {sc[j] / tot:.2f}; none {sc[1500] / tot:.2f})  level {lev:5.1f} dB')


if __name__ == '__main__':
    if sys.argv[1] == 'probe':
        probe(int(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) > 3 else 0)
    else:
        case(sys.argv[2], int(sys.argv[3]))
