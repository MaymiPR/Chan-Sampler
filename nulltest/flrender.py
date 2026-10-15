"""Render FL Studio Sampler test cases through FL's command-line renderer.

render(name, sample, patches) builds a one-note project from the Basic
template (channel 0 = Sampler playing `sample`), applies `patches` to
channel 0's events, renders with `FL64.exe /R /Ewav` and returns the audio.
Results are cached under renders/<name>.wav."""
import os, struct, subprocess, numpy as np
import flp, wavio
from build_project import build

FL = r"C:\Program Files\Image-Line\FL Studio 2026\FL64.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
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

def render(name, sample, patches=(), key=60, velocity=100, beats=8, force=False, notes=None):
    wav = os.path.join(OUT, name + ".wav")
    if os.path.exists(wav) and not force:
        return wavio.read(wav)[0]
    proj = os.path.join(OUT, name + ".flp")
    def patch_all(ev):
        for p in patches: p(ev)
    build(proj, os.path.join(HERE, sample), patch_all, note_len_beats=beats, key=key, velocity=velocity, notes=notes)
    if os.path.exists(wav): os.remove(wav)
    subprocess.run([FL, "/R", "/Ewav", proj], timeout=300)
    return wavio.read(wav)[0]

def source(sample):
    return wavio.read(os.path.join(HERE, sample))[0]

def fit(y, x):
    """Least-squares gain of y against x (per channel) and residual in dB."""
    n = min(len(x), len(y))
    y, x = y[:n], x[:n]
    g = (y * x).sum(0) / np.maximum((x * x).sum(0), 1e-30)
    r = y - x * g
    return g, 10 * np.log10(np.maximum((r ** 2).sum(0), 1e-30) / np.maximum((y ** 2).sum(0), 1e-30))
