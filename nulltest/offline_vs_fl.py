"""Compare an offline ChanSamplerRender render with FL's half of a nullfl case
(least-squares gain), for quick model tuning without FL re-renders."""
import os, sys, subprocess, numpy as np, wavio, nullfl
HERE = os.path.dirname(os.path.abspath(__file__))
R = os.path.join(HERE, '..', 'build', 'ChanSamplerRender_artefacts', 'Release', 'ChanSamplerRender.exe')

def compare(case, out_dir, env=None, seconds=None):
    a, _ = nullfl.render_pair(case['name'], None)
    pf = os.path.join(out_dir, 'ovf.txt')
    open(pf, 'w').write(''.join(f'{k} {v}\n' for k, v in case.get('ours', {}).items()))
    out = os.path.join(out_dir, 'ovf.wav'); beats = case.get('beats', 8)
    notes = case.get('notes') or [(0, beats, 60, case.get('vel', 100))]
    spec = ';'.join(f'{k}:{st * 60 / 130}:{ln * 60 / 130}:{v}' for st, ln, k, v in notes)
    subprocess.run([R, os.path.join(HERE, case['sample']), out, str(seconds or len(a) / 44100), pf, spec],
                   check=True, env=dict(os.environ, **(env or {})))
    y, _ = wavio.read(out); n = min(len(a), len(y)); A = a[:n, 0]; Y = y[:n, 0]
    g = (A @ Y) / (Y @ Y); r = A - g * Y
    return 10 * np.log10((r @ r) / (A @ A))
