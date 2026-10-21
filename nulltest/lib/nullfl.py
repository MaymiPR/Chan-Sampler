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

def render_pair(name, sample, fl_patches=(), ours=None, notes=None, vel=100, beats=8, force=False, gap=None, bpm=None):
    # gap: beats between the halves. Use a multiple of 13 for tick-grid features
    # (a tick is 11025/52 samples at 130 BPM, so only then do both halves sit
    # on the same sub-sample tick positions). bpm: the project tempo (130 if
    # not given); 26 beats also lines up at 65, 100, 120 and 160 BPM.
    gap = gap or GAP_BEATS
    bpm = bpm or TEMPO
    notes = notes or [(0, beats, 60, vel)]
    wav = os.path.join(OUT, name + ".wav")
    if force or not os.path.exists(wav):
        proj = os.path.join(OUT, name + ".flp")
        def patch(ev):
            for p in fl_patches: p(ev)
            if bpm != TEMPO:
                next(e for e in ev if e[0] == 156)[1] = bytearray(struct.pack('<I', int(round(bpm * 1000))))
        all_notes = [(st, ln, k, v, 0) for st, ln, k, v in notes] + [(st + gap, ln, k, v, 1) for st, ln, k, v in notes]
        # A note at the very end keeps FL rendering through the whole second
        # half (FL stops a fixed tail after the last note, which cut off long
        # echo trails); it sounds after both compared windows.
        all_notes.append((2 * gap + 2, 0.25, 60, 1, 0))   # +2 beats: reverb rings slightly ahead of a note
        V.build_vst_project(proj, paths.signal(sample), ours or {}, all_notes, sampler_patch=patch)
        F.run_fl(proj, wav)
    y, sr = wavio.read(wav)
    split = int(round(gap * 60.0 / bpm * sr))
    a = y[:split]
    b = y[split:2 * split]
    n = min(len(a), len(b))
    return a[:n], b[:n]

def null_db(a, b):
    r = a - b
    if (a ** 2).sum() < 1e-20:   # silent reference: compare absolute levels instead
        return 10 * np.log10(max((r ** 2).sum(), 1e-30) / len(a))
    return 10 * np.log10(max((r ** 2).sum(), 1e-30) / max((a ** 2).sum(), 1e-30))

def run(cases, only=None, force=False):
    res = []
    for c in cases:
        if only and not any(o in c["name"] for o in only): continue
        a, b = render_pair(c["name"], c["sample"], c.get("fl", []), c.get("ours", {}), c.get("notes"), c.get("vel", 100), c.get("beats", 8), force, c.get("gap"), c.get("bpm"))
        d = null_db(a, b)
        res.append((c["name"], d))
        print(f"{F.verdict(d):5s} {d:7.1f} dB  {c['name']}", flush=True)
    return res
