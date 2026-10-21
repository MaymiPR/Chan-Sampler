"""Null tests: FL Studio's Sampler vs Chan Sampler.

Each case renders the same sample and note through FL (command-line render of a
generated project) and through Chan Sampler (offline renderer), scales ours by
FL's channel gain (volume 78.125% + centre pan = 0.389612, measured), flips
its polarity, sums, and reports the residual relative to FL's signal.
Residual < -60 dB = null (FL's 16-bit export floor is about -70 dB here)."""
import os, subprocess, numpy as np
from lib import flrender as F, wavio, paths

HERE = paths.ROOT
RENDER = paths.RENDER_TOOL
OURS = os.path.join(HERE, "ours"); os.makedirs(OURS, exist_ok=True)
CHANNEL_GAIN = 0.389612
TEMPO = 130.0

def ours(name, sample, params, key=60, velocity=100, beats=8, seconds=None, notes=None):
    pfile = os.path.join(OURS, name + ".txt")
    params = {"output": 0.0, **params}   # FL channel gain is applied below instead of the default trim
    with open(pfile, "w") as f:
        for k, v in params.items(): f.write(f"{k} {v}\n")
    out = os.path.join(OURS, name + ".wav")
    notes = notes or [(0, beats, key, velocity)]
    b2s = 60.0 / TEMPO
    seconds = seconds or max(st + ln for st, ln, _, _ in notes) * b2s + 1.5
    spec = ";".join(f"{k}:{st * b2s}:{ln * b2s}:{v}" for st, ln, k, v in notes)
    subprocess.run([RENDER, paths.signal(sample), out, str(seconds), pfile, spec], check=True)
    return wavio.read(out)[0] * CHANNEL_GAIN

def null(fl, us):
    n = max(len(fl), len(us))
    a = np.zeros((n, 2)); a[:len(fl)] = fl
    b = np.zeros((n, 2)); b[:len(us)] = us
    r = a - b
    e = max((a ** 2).sum(), 1e-30)
    return 10 * np.log10(max((r ** 2).sum(), 1e-30) / e)

def run(cases, only=None):
    results = []
    for c in cases:
        name = c["name"]
        if only and not any(o in name for o in only): continue
        fl = F.render(name, c["sample"], c.get("fl", []), velocity=c.get("vel", 100), beats=c.get("beats", 8), notes=c.get("notes"))
        us = ours(name, c["sample"], c.get("ours", {}), velocity=c.get("vel", 100), beats=c.get("beats", 8), notes=c.get("notes"))
        r = null(fl, us)
        results.append((name, r))
        print(f"{F.verdict(r):5s} {r:7.1f} dB  {name}", flush=True)
    return results
