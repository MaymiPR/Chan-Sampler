"""Generic null-test and measurement helpers for the house null-test harnesses.

Everything here is numpy only. Rendering through a plug-in is done elsewhere (see refrender_harness_example.py for a
JUCE RefRender host, pedalboard_refhost_example.py for pedalboard). Typical use:

    import nulltest_lib as nl
    x = nl.pink(6, -18)                                  # 6 s of pink noise at -18 dBFS rms, float32, 48 kHz
    ref, ours = render_ref(x), render_ours(x)            # (2, n) arrays, your own renderers
    ours = nl.align(ref, ours)                           # remove any latency mismatch (reports the lag)
    print(nl.residual_db(ref, ours, skip=2 * nl.FS))     # dB re the reference's energy, first 2 s skipped
    print(nl.worst_second(ref, ours))                    # (dB, second index): where it is worst

Conventions that every project ended up using (do not change them without a reason):
- residual = 10 log10(sum((ref - ours)^2) / sum(ref^2)), over everything after a skip (start-up, pre-roll).
- "null" is claimed only below about -60 dB on the material in question; -40 is "close", -20 is "a model".
- always report median AND worst over a grid; the worst case is what a listener hears.
- keep renders cached by a hash of (input, settings); never cache your own build's output.
"""
import hashlib
import numpy as np

FS = 48000


# ---------------------------------------------------------------- signals ------------------------------------------
def tone(f, secs, db, fs=FS, phase=0.0):
    t = np.arange(int(secs * fs)) / fs
    return (10 ** (db / 20) * np.sin(2 * np.pi * f * t + phase)).astype(np.float32)


def tones(freqs, secs, db_each, fs=FS, phases=None):
    """several equal inharmonic tones with fixed absolute phases: a locator for phase restarts and ratio grids
    (a stretch engine's splice analysis)."""
    t = np.arange(int(secs * fs)) / fs
    phases = phases or [0.0] * len(freqs)
    x = sum(np.sin(2 * np.pi * f * t + p) for f, p in zip(freqs, phases))
    return (10 ** (db_each / 20) * x).astype(np.float32)


def noise(secs, db_rms, fs=FS, seed=1, pink=False):
    rng = np.random.default_rng(seed)
    x = rng.standard_normal(int(secs * fs))
    if pink:
        X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / fs); X[1:] /= np.sqrt(f[1:]); X[0] = 0
        x = np.fft.irfft(X, len(x))
    x /= np.sqrt(np.mean(x ** 2))
    return (x * 10 ** (db_rms / 20)).astype(np.float32)


def pink(secs, db_rms, fs=FS, seed=1):
    return noise(secs, db_rms, fs, seed, pink=True)


def sweep(secs, db, f0=20, f1=20000, fs=FS):
    """log sine sweep: frequency response, filter order, aliasing images after oversampling."""
    t = np.arange(int(secs * fs)) / fs
    k = np.log(f1 / f0) / secs
    return (10 ** (db / 20) * np.sin(2 * np.pi * f0 * (np.exp(k * t) - 1) / k)).astype(np.float32)


def impulse(secs, db=0.0, at=0.1, fs=FS):
    """one sample: the impulse response of anything linear (crossovers, delays, interpolators, trims)."""
    x = np.zeros(int(secs * fs), np.float32); x[int(at * fs)] = 10 ** (db / 20); return x


def clicks(secs, period_s, db=0.0, fs=FS, start=0.1):
    """a chain of single-sample clicks: superposition lets you measure a linear black box with one render
    (trim filter fits)."""
    x = np.zeros(int(secs * fs), np.float32)
    for i in np.arange(int(start * fs), len(x), int(period_s * fs)): x[int(i)] = 10 ** (db / 20)
    return x


def steps(levels_db, step_s, f=1000.0, fs=FS):
    """a tone whose level steps every step_s seconds: attack / release time constants, gain laws, ride and hold."""
    parts = [tone(f, step_s, db, fs) for db in levels_db]
    return np.concatenate(parts).astype(np.float32)


def nyquist_alt(secs, db, fs=FS):
    """+a, -a, +a ...: every sample is a peak, so a sample-peak detector's gain y/x is observable sample by sample
    (compressor measurement)."""
    x = np.tile(np.array([1.0, -1.0], np.float32), int(secs * fs) // 2)
    return (x * 10 ** (db / 20)).astype(np.float32)


def pilot(secs, db=-54.0, f=997.0, fs=FS):
    """a quiet pilot tone to add under probes: exposes the gain of a pure-gain plug-in during silences. Pick a
    frequency that is not a harmonic of the probe."""
    return tone(f, secs, db, fs)


def ramp(secs, db0, db1, f=1000.0, fs=FS):
    """level ramp up (or down) in dB: thresholds, knees, meter laws, LED thresholds."""
    t = np.arange(int(secs * fs)) / fs
    g = 10 ** ((db0 + (db1 - db0) * t / secs) / 20)
    return (g * np.sin(2 * np.pi * f * t)).astype(np.float32)


def am(secs, f=1000.0, fm=4.0, depth=0.5, db=-12.0, fs=FS):
    """amplitude-modulated tone: detector type (peak vs rms), release shape, ripple."""
    t = np.arange(int(secs * fs)) / fs
    return (10 ** (db / 20) * (1 - depth * (1 - np.cos(2 * np.pi * fm * t)) / 2) * np.sin(2 * np.pi * f * t)).astype(np.float32)


def drums(secs, db=-6.0, seed=3, fs=FS):
    """synthetic drum loop (kick, snare, hats at 120 bpm): transients and program dependence."""
    rng = np.random.default_rng(seed)
    n = int(secs * fs); x = np.zeros(n); beat = int(0.5 * fs)
    t = np.arange(int(0.4 * fs)) / fs
    kick = np.sin(2 * np.pi * (55 * t + 60 * (1 - np.exp(-t * 30)) / 30)) * np.exp(-t * 8)
    snare = (rng.standard_normal(len(t)) * 0.6 + np.sin(2 * np.pi * 190 * t)) * np.exp(-t * 18)
    hat = rng.standard_normal(int(0.05 * fs)) * np.exp(-np.arange(int(0.05 * fs)) / fs * 80) * 0.3
    for i in range(0, n - len(t), beat):
        k = i // beat
        x[i:i + len(t)] += kick if k % 2 == 0 else snare
        h = i + beat // 2
        if h + len(hat) < n: x[h:h + len(hat)] += hat
    x *= 10 ** (db / 20) / np.max(np.abs(x))
    return x.astype(np.float32)


def music(secs, db=-10.0, seed=5, fs=FS):
    """dense synthetic stereo mix (bass, chords, pink bed, drums): the "does it hold on real material" check."""
    rng = np.random.default_rng(seed)
    n = int(secs * fs); t = np.arange(n) / fs
    notes = np.array([55, 65.4, 73.4, 49])
    f = notes[(t / 0.75).astype(int) % 4]
    bass = np.sin(2 * np.pi * np.cumsum(f) / fs) * 0.5
    chord = sum(np.sin(2 * np.pi * f0 * k * t) / k for f0 in (220, 277.2, 329.6) for k in range(1, 8)) * 0.08
    bed = pink(secs, -30, fs, seed)
    d = drums(secs, -3, seed, fs)
    l = bass + chord + bed + d * 0.8; r = bass + chord * 0.8 + np.roll(bed, 37) + d * 0.8
    m = max(np.max(np.abs(l)), np.max(np.abs(r)))
    return (np.stack([l, r]) * 10 ** (db / 20) / m).astype(np.float32)


# ---------------------------------------------------------------- residuals ----------------------------------------
def stereo(x):
    x = np.asarray(x, np.float32)
    return np.stack([x, x]) if x.ndim == 1 else x


def residual_db(ref, ours, skip=0):
    """10 log10(energy of the difference / energy of the reference), after the first `skip` samples."""
    a, b = stereo(ref)[:, skip:], stereo(ours)[:, skip:]
    n = min(a.shape[1], b.shape[1]); a, b = a[:, :n], b[:, :n]
    # an exact match reads -300 dB (floored), never -inf: it keeps tables and medians finite
    return float(10 * np.log10(max(np.sum((a - b) ** 2), 1e-30) / max(np.sum(a ** 2), 1e-30)))


def residual_db_weighted(ref, ours, skip=0, win=0.1, fs=FS):
    """energy-weighted mean of per-window residuals in dB. Used as a FIT OBJECTIVE (CMA-ES release-law fits): an rms-dB
    objective over the whole file let loud windows hide quiet ones and wrecked voice material."""
    a, b = stereo(ref)[:, skip:], stereo(ours)[:, skip:]
    n = min(a.shape[1], b.shape[1]); w = int(win * fs); m = n // w
    a, b = a[:, :m * w].reshape(2, m, w), b[:, :m * w].reshape(2, m, w)
    e = np.sum(a ** 2, axis=(0, 2)); d = np.sum((a - b) ** 2, axis=(0, 2))
    r = 10 * np.log10(np.maximum(d, 1e-30) / np.maximum(e, 1e-30))
    return float(np.sum(r * e) / max(np.sum(e), 1e-30))


def worst_second(ref, ours, skip=0, fs=FS):
    """(dB, second index) of the worst one-second window: say WHERE a null fails, not only how much."""
    a, b = stereo(ref)[:, skip:], stereo(ours)[:, skip:]
    n = min(a.shape[1], b.shape[1]) // fs
    worst = (-999.0, -1)
    for s in range(n):
        r = residual_db(a[:, s * fs:(s + 1) * fs], b[:, s * fs:(s + 1) * fs])
        if r > worst[0]: worst = (r, s)
    return worst


def find_lag(ref, ours, max_lag=20000):
    """integer sample lag of ours relative to ref by cross-correlation (positive: ours is late)."""
    a = stereo(ref)[0].astype(np.float64); b = stereo(ours)[0].astype(np.float64)
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    c = np.fft.irfft(np.fft.rfft(b, 2 * n) * np.conj(np.fft.rfft(a, 2 * n)))
    lags = np.concatenate([np.arange(0, max_lag + 1), np.arange(-max_lag, 0)])
    vals = np.concatenate([c[:max_lag + 1], c[-max_lag:]])
    return int(lags[np.argmax(vals)])


def align(ref, ours, max_lag=20000, verbose=True):
    """shift ours so it lines up with ref (whole samples). Sub-sample offsets show as a high-frequency residual:
    then the model's own delay is wrong (nulls move 10 dB per 0.002 samples of offset in a character path)."""
    lag = find_lag(ref, ours, max_lag)
    if verbose and lag: print(f'align: ours is {lag:+d} samples relative to ref')
    b = stereo(ours)
    if lag > 0: b = np.concatenate([b[:, lag:], np.zeros((2, lag), np.float32)], axis=1)
    elif lag < 0: b = np.concatenate([np.zeros((2, -lag), np.float32), b[:, :lag]], axis=1)
    return b


def gain_trace(ref, x, lat=0, eps=1e-9):
    """sample-wise gain of a pure-gain plug-in: ref[n] / x[n - lat]. Noisy near zero crossings: use with the Nyquist
    alternating signal, or take the Hilbert-envelope ratio (envelope_ratio) on tones."""
    a = stereo(ref)[0]; b = np.roll(stereo(x)[0], lat)
    return a / np.where(np.abs(b) < eps, np.nan, b)


def envelope(x):
    """analytic-signal envelope (Hilbert), the most informative probe for gain trajectories on tones."""
    x = np.asarray(stereo(x)[0], np.float64)
    X = np.fft.rfft(x); h = np.zeros(len(X)); h[0] = 1; h[1:] = 2
    if len(x) % 2 == 0: h[-1] = 1
    z = np.fft.ifft(np.concatenate([X * h, np.zeros(len(x) - len(X), complex)]))
    return np.abs(z[:len(x)])


def envelope_ratio(ref, x, lat=0):
    return envelope(ref) / np.maximum(np.roll(envelope(x), lat), 1e-9)


# ---------------------------------------------------------------- analysis -----------------------------------------
def transfer(y, x, fs=FS, n=4096):
    """averaged transfer function H = Sxy / Sxx of a render against its input (filter fits).
    Returns (freqs, H complex). Feed noise or a sweep; read |H| in dB and the phase."""
    x = stereo(x)[0]; y = stereo(y)[0]
    m = min(len(x), len(y)) - n
    w = np.hanning(n); Sxy = 0; Sxx = 0
    for i in range(0, m, n // 2):
        X = np.fft.rfft(w * x[i:i + n]); Y = np.fft.rfft(w * y[i:i + n])
        Sxy = Sxy + Y * np.conj(X); Sxx = Sxx + np.abs(X) ** 2
    return np.fft.rfftfreq(n, 1 / fs), Sxy / np.maximum(Sxx, 1e-30)


def harmonics(y, f, count=8, fs=FS, skip=0):
    """levels (dB) of the first `count` harmonics of a tone at f in render y (THD vs level, odd vs even shaper).
    Note the argument order: the third positional is count, not fs (passing fs there fakes silent batches)."""
    s = np.asarray(stereo(y)[0][skip:], np.float64)
    n = len(s); w = np.hanning(n); S = np.abs(np.fft.rfft(s * w)) * 2 / np.sum(w)
    fr = np.fft.rfftfreq(n, 1 / fs)
    out = []
    for k in range(1, count + 1):
        i = int(round(k * f * n / fs))
        if i >= len(S): break
        out.append(20 * np.log10(max(S[max(0, i - 2):i + 3].max(), 1e-12)))
    return out


def rms_db(x):
    x = np.asarray(x, np.float64)
    return float(10 * np.log10(max(float(np.mean(x * x)), 1e-30)))


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


def time_constant(trace, t0, fs=FS, target=0.632):
    """samples from t0 until trace reaches `target` of its final move (t63 of an attack or release step)."""
    tr = np.asarray(trace, np.float64)[t0:]
    a, b = tr[0], tr[-1]
    if abs(b - a) < 1e-12: return 0
    k = np.argmax(np.abs(tr - a) >= target * abs(b - a))
    return int(k)


def ripple_rate(y, fs=FS, band=(15000, 20000), skip=0):
    """dominant rate (Hz) of a periodic artefact's amplitude ripple inside a band. A periodic artefact's RATE can
    reveal a hidden internal parameter (a stretch-ratio grid read from the treble ripple)."""
    s = np.asarray(stereo(y)[0][skip:], np.float64)
    X = np.fft.fft(s); f = np.fft.fftfreq(len(s), 1 / fs)
    X[(f < band[0]) | (f > band[1])] = 0          # one-sided: the inverse is the analytic signal of the band
    env = np.abs(np.fft.ifft(X))
    E = np.abs(np.fft.rfft(env - env.mean())); fe = np.fft.rfftfreq(len(env), 1 / fs)
    E[fe < 0.5] = 0
    return float(fe[np.argmax(E)])


# ---------------------------------------------------------------- files and caching --------------------------------
def write_f32(path, x):
    """stereo interleaved float32, the format RefRender and each plug-in's renderer read and write."""
    stereo(x).T.astype(np.float32).tofile(path)


def read_f32(path):
    return np.fromfile(path, np.float32).reshape(-1, 2).T.copy()


def cache_key(x, **settings):
    """sha1 of the exact input plus the settings. Add a 'salt' entry for global state a job cannot carry (the
    reference's oversampling page, a hidden mode), or the cache will hand back a render made under another state."""
    h = hashlib.sha1(stereo(x).tobytes())
    for k in sorted(settings): h.update(repr((k, settings[k])).encode())
    return h.hexdigest()


def summarize(results):
    """results: {name: residual_dB}. Prints median and worst, the two numbers every NULLTESTS.md leads with."""
    v = np.array(list(results.values()), float)
    worst = max(results, key=results.get)
    print(f'{len(v)} cases: median {np.median(v):.1f} dB, worst {v.max():.1f} dB ({worst})')
    return float(np.median(v)), float(v.max()), worst
