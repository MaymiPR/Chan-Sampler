"""FL Sampler preset import (Source/FlPreset.cpp) against FL itself.

For each in-FL null case, the FL Sampler channel of the case's project
(renders_fl/<case>.flp) is written out as a preset (.fst: the project's version
event plus the channel's events), Chan Sampler renders that preset offline with
nothing else set, and the result is compared with FL's half of the render.
The offline renderer outputs 1.25 x what the plugin does inside FL (FL's
channel), so ours is scaled by 0.8; no gain fitting, so levels are checked too.

  python fst_cases.py [name filters...]
"""
import os, sys, struct, subprocess, numpy as np
import flp, flrender as F, nullfl, wavio

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, '..', 'build', 'ChanSamplerRender_artefacts', 'Release', 'ChanSamplerRender.exe')
OUT = os.path.join(HERE, 'fst'); os.makedirs(OUT, exist_ok=True)

def write_fst(flp_path, fst_path):
    header, ev = flp.read(flp_path)
    out = [e for e in ev if e[0] == 199][:1]
    ch = -1
    for e in ev:
        if e[0] == 64:
            ch = flp.val(e[1])
            if ch > 0: break
        if ch == 0: out.append(e)
    flp.write(fst_path, header, out)

def run(cases, only=None):
    res = []
    for c in cases:
        if only and not any(o in c['name'] for o in only): continue
        proj = os.path.join(nullfl.OUT, c['name'] + '.flp')
        if not os.path.exists(proj): continue
        a, _ = nullfl.render_pair(c['name'], None, gap=c.get('gap'))
        fst = os.path.join(OUT, c['name'] + '.fst'); write_fst(proj, fst)
        notes = c.get('notes') or [(0, c.get('beats', 8), 60, c.get('vel', 100))]
        spec = ';'.join(f'{k}:{st * 60 / 130}:{ln * 60 / 130}:{v}' for st, ln, k, v in notes)
        empty = os.path.join(OUT, 'none.txt'); open(empty, 'w').close()
        wav = os.path.join(OUT, c['name'] + '.wav')
        subprocess.run([R, fst, wav, str(len(a) / 44100), empty, spec], check=True)
        y, _ = wavio.read(wav); n = min(len(a), len(y))
        d = nullfl.null_db(a[:n], 0.8 * y[:n])
        res.append((c['name'], d))
        print(f"{'NULL' if d < -60 else ('close' if d < -30 else 'DIFF'):5s} {d:7.1f} dB  {c['name']}", flush=True)
    return res

if __name__ == '__main__':
    import fl_cases, lfo_cases, echo_cases, untested_cases, stretch_time_cases, arp_cases
    allc = fl_cases.CASES + lfo_cases.CASES + echo_cases.CASES + untested_cases.CASES + stretch_time_cases.CASES + arp_cases.CASES
    run(allc, sys.argv[1:] or None)
