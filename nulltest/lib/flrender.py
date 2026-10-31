"""Render FL Studio Sampler test cases through FL's command-line renderer.

render(name, sample, patches) builds a one-note project from the Basic
template (channel 0 = Sampler playing `sample`), applies `patches` to
channel 0's events, renders with `FL64.exe /R /Ewav` and returns the audio.
Results are cached under renders/<name>.wav."""
import os, struct, subprocess, sys, numpy as np
from lib import flp, wavio, paths
from lib.build_project import build

FL = paths.FL_EXE
HERE = paths.ROOT
OUT = os.path.join(HERE, "renders")
os.makedirs(OUT, exist_ok=True)

ENV_TARGETS = ["pan", "vol", "x", "y", "pitch"]   # order of the five 218 events
ENV_FIELDS = ["flags", "env_on", "delay", "attack", "hold", "decay", "sustain", "release", "amount",
              "lfo_delay", "lfo_attack", "lfo_amount", "lfo_speed", "lfo_shape", "att_tension", "dec_tension", "rel_tension"]

def _channel0(ev):
    """Yield (index, event) for channel 0's events."""
    ch = -1
    for i, e in enumerate(ev):
        if e[0] == 64: ch = flp.val(e[1])
        elif ch == 0: yield i, e
        if ch > 0: return

def fl_running():
    """True while FL Studio's window is open (Windows)."""
    if sys.platform != 'win32':
        return False
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq FL64.exe'], capture_output=True, text=True).stdout
    return 'FL64.exe' in out


def run_fl(project, wav):
    """FL's command-line render of project into wav. A running FL swallows it,
    so refuse then, before the cached wav is touched."""
    if fl_running():
        raise RuntimeError('FL Studio is open: close it first (a running FL swallows the command-line render)')
    if os.path.exists(wav): os.remove(wav)
    subprocess.run([FL, "/R", "/Ewav", project], timeout=300)
    if not os.path.exists(wav):
        raise RuntimeError(f'FL did not render {project}')


def verdict(db):
    """NULL below -60 dB (a match; FL's 16-bit export floor is about -65 to -95), close to -30, else DIFF."""
    return 'NULL' if db < -60 else ('close' if db < -30 else 'DIFF')


def word(eid, value):
    def p(ev):
        for _, e in _channel0(ev):
            if e[0] == eid: e[1] = bytearray(struct.pack('<H', value)); return
        raise KeyError(eid)
    return p

def dword(eid, value, fmt='<I'):
    def p(ev):
        for _, e in _channel0(ev):
            if e[0] == eid: e[1] = bytearray(struct.pack(fmt, value)); return
        raise KeyError(eid)
    return p

def byte(eid, value):
    def p(ev):
        for _, e in _channel0(ev):
            if e[0] == eid: e[1] = bytearray([value]); return
        raise KeyError(eid)
    return p

def struct_field(eid, offset, fmt, value, occurrence=0):
    def p(ev):
        n = 0
        for _, e in _channel0(ev):
            if e[0] == eid:
                if n == occurrence:
                    struct.pack_into(fmt, e[1], offset, value); return
                n += 1
        raise KeyError(eid)
    return p

def env(target, field, value):
    return struct_field(218, 4 * ENV_FIELDS.index(field), '<i', value, ENV_TARGETS.index(target))

def params(offset, fmt, value):           # ChannelID.Parameters (215)
    return struct_field(215, offset, fmt, value)

def levels(field, value):                 # ChannelID.Levels (219): pan, volume, pitch, modx, mody, type
    return struct_field(219, 4 * ["pan", "volume", "pitch", "mod_x", "mod_y", "type"].index(field), '<i', value)

def project(case):
    """A function that writes the case's one-channel project (FL's Sampler alone)
    to a path. case: name, sample, fl (patches), key, vel, beats, notes."""
    patches = case.get("fl", ())
    def patch_all(ev):
        for p in patches: p(ev)
    def write(path, deps=None):
        sample = deps[0] if deps else paths.signal(case["sample"])
        build(path, sample, patch_all, note_len_beats=case.get("beats", 8),
              key=case.get("key", 60), velocity=case.get("vel", 100), notes=case.get("notes"))
    # FL renders at least to the end of the last note, at the Basic
    # template's 130 BPM (lib/flqueue rejects a shorter render).
    notes = case.get("notes") or [(0, case.get("beats", 8))]
    write.min_seconds = max(st + ln for st, ln, *_ in notes) * 60.0 / 130.0
    return write


def render(name, sample, patches=(), key=60, velocity=100, beats=8, force=False, notes=None, attempts=3):
    """FL's render of one case, through the queue: reused when the project, the
    sample and FL are unchanged (lib/flqueue), retried on a sporadic failure.
    force=True renders again regardless."""
    from lib import flqueue
    case = dict(name=name, sample=sample, fl=patches, key=key, vel=velocity, beats=beats, notes=notes)
    if force:
        for ext in (".wav", flqueue.SIDECAR):
            p = os.path.join(OUT, name + ext)
            if os.path.exists(p):
                if ext == ".wav" and fl_running():
                    raise RuntimeError('FL Studio is open: close it first (a running FL swallows the command-line render)')
                os.remove(p)
    q = flqueue.Queue(OUT, log=lambda s: None)
    q.add(name, project(case), [paths.signal(sample)])
    wavs = q.run(attempts=attempts)
    if name not in wavs:
        raise RuntimeError(f'FL render failed after {attempts} attempts: {q.failed[name]}')
    return wavio.read(wavs[name])[0]


def render_many(cases, only=None, force=False, attempts=3, adopt=False):
    """Every case through one queue (the plan is printed first): name -> audio."""
    from lib import flqueue
    q = flqueue.queue_for(cases, 'fl', only)
    if force:
        for j in q.jobs:
            for ext in (".wav", flqueue.SIDECAR):
                if os.path.exists(os.path.join(OUT, j.name + ext)):
                    if ext == ".wav" and fl_running():
                        raise RuntimeError('FL Studio is open: close it first')
                    os.remove(os.path.join(OUT, j.name + ext))
    wavs = q.run(attempts=attempts, adopt=adopt)
    return {n: wavio.read(p)[0] for n, p in wavs.items()}

def source(sample):
    return wavio.read(paths.signal(sample))[0]

def fit(y, x):
    """Least-squares gain of y against x (per channel) and residual in dB."""
    n = min(len(x), len(y))
    y, x = y[:n], x[:n]
    g = (y * x).sum(0) / np.maximum((x * x).sum(0), 1e-30)
    r = y - x * g
    return g, 10 * np.log10(np.maximum((r ** 2).sum(0), 1e-30) / np.maximum((y ** 2).sum(0), 1e-30))
