"""In-FL null tests: FL's Sampler and Chan Sampler in the same project.

Channel 1 is FL's Sampler (patched like flrender cases), channel 2 is Chan
Sampler (state XML built from the case's parameters). The Sampler plays at
beat 0, Chan Sampler at beat 16; FL renders once and the two halves are
subtracted directly, with no gain correction, exactly as heard in FL."""
import os, struct, numpy as np
from lib import flrender as F, vstchannel as V, wavio, paths

HERE = paths.ROOT
OUT = os.path.join(HERE, "renders_fl")
os.makedirs(OUT, exist_ok=True)
TEMPO = 130.0
GAP_BEATS = 16

def project(case):
    """A function that writes the case's two-channel project (FL's Sampler at
    beat 0, our VST3 at beat gap) to a path. case: name, sample, fl (patches),
    ours (our parameters), notes, vel, beats, gap, bpm."""
    # gap: beats between the halves. Use a multiple of 13 for tick-grid features
    # (a tick is 11025/52 samples at 130 BPM, so only then do both halves sit
    # on the same sub-sample tick positions). bpm: the project tempo (130 if
    # not given); 26 beats also lines up at 65, 100, 120 and 160 BPM.
    gap = case.get("gap") or GAP_BEATS
    bpm = case.get("bpm") or TEMPO
    notes = case.get("notes") or [(0, case.get("beats", 8), 60, case.get("vel", 100))]
    fl_patches = case.get("fl", ())
    def patch(ev):
        for p in fl_patches: p(ev)
        if bpm != TEMPO:
            next(e for e in ev if e[0] == 156)[1] = bytearray(struct.pack('<I', int(round(bpm * 1000))))
    all_notes = [(st, ln, k, v, 0) for st, ln, k, v in notes] + [(st + gap, ln, k, v, 1) for st, ln, k, v in notes]
    # A note at the very end keeps FL rendering through the whole second
    # half (FL stops a fixed tail after the last note, which cut off long
    # echo trails); it sounds after both compared windows.
    all_notes.append((2 * gap + 2, 0.25, 60, 1, 0))   # +2 beats: reverb rings slightly ahead of a note
    def write(path, deps=None):
        sample = deps[0] if deps else paths.signal(case["sample"])
        V.build_vst_project(path, sample, case.get("ours") or {}, all_notes, sampler_patch=patch)
    return write


def halves(wav, gap=None, bpm=None):
    """FL's half and ours from a rendered pair, same length."""
    gap = gap or GAP_BEATS
    bpm = bpm or TEMPO
    y, sr = wavio.read(wav)
    split = int(round(gap * 60.0 / bpm * sr))
    a = y[:split]
    b = y[split:2 * split]
    n = min(len(a), len(b))
    return a[:n], b[:n]


def render_pair(name, sample, fl_patches=(), ours=None, notes=None, vel=100, beats=8, force=False, gap=None, bpm=None, attempts=3):
    """One pair render through the queue (lib/flqueue: reused when the project,
    the sample, our VST3 and FL are unchanged; retried on a sporadic failure).
    sample=None reads the render of that name as it is (a probe reading a case
    rendered by its suite), without rebuilding its project."""
    from lib import flqueue
    if sample is None:
        wav = os.path.join(OUT, name + ".wav")
        if not os.path.exists(wav):
            raise RuntimeError(f'no render named {name}: render its case first')
        return halves(wav, gap, bpm)
    case = dict(name=name, sample=sample, fl=fl_patches, ours=ours, notes=notes, vel=vel, beats=beats, gap=gap, bpm=bpm)
    if force:
        for ext in (".wav", flqueue.SIDECAR):
            p = os.path.join(OUT, name + ext)
            if os.path.exists(p):
                if ext == ".wav" and F.fl_running():
                    raise RuntimeError('FL Studio is open: close it first (a running FL swallows the command-line render)')
                os.remove(p)
    q = flqueue.Queue(OUT, deps=[paths.PLUGIN_VST3], log=lambda s: None)
    q.add(name, project(case), [paths.signal(sample)])
    wavs = q.run(attempts=attempts)
    if name not in wavs:
        raise RuntimeError(f'FL render failed after {attempts} attempts: {q.failed[name]}')
    return halves(wavs[name], gap, bpm)

def null_db(a, b):
    r = a - b
    if (a ** 2).sum() < 1e-20:   # silent reference: compare absolute levels instead
        return 10 * np.log10(max((r ** 2).sum(), 1e-30) / len(a))
    return 10 * np.log10(max((r ** 2).sum(), 1e-30) / max((a ** 2).sum(), 1e-30))

def run(cases, only=None, force=False, attempts=3, adopt=False, plugin_dep=True):
    """Render every case through one queue (its plan is printed first; only
    changed or missing renders go to FL, each retried), then null each pair.
    force=True renders everything again; adopt=True trusts old WAVs without a
    record (a rebuild that changed nothing); plugin_dep=False leaves our VST3
    out of the key."""
    from lib import flqueue
    q = flqueue.queue_for(cases, 'pair', only, plugin_dep)
    if force:
        if F.fl_running():
            raise RuntimeError('FL Studio is open: close it first (a running FL swallows the command-line render)')
        for j in q.jobs:
            for ext in (".wav", flqueue.SIDECAR):
                p = os.path.join(OUT, j.name + ext)
                if os.path.exists(p): os.remove(p)
    wavs = q.run(attempts=attempts, adopt=adopt)
    res = []
    for c in cases:
        if c["name"] not in wavs:
            if c["name"] in q.failed:
                print(f"FAIL          {c['name']}  ({q.failed[c['name']]})", flush=True)
            continue
        a, b = halves(wavs[c["name"]], c.get("gap"), c.get("bpm"))
        d = null_db(a, b)
        res.append((c["name"], d))
        print(f"{F.verdict(d):5s} {d:7.1f} dB  {c['name']}", flush=True)
    return res
