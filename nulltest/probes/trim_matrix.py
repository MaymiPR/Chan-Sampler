"""FL's trimmed segment as a matrix, SMP START 0, LENGTH set by span as
cases/trim_short_cases.py does: one click per input frame q = 0 .. span + 4
over a faint noise floor, the floor's own render subtracted. R[q, n] = FL's
output frame n per unit input at frame q (channel volume removed), saved to
data/trim_matrix_span<span>.npy. The end filter's sweep on segments under 200
frames (Source/TrimTail.h) is fitted to these.

    python -m probes.trim_matrix 41 50 60 80 100 150 190"""
import os, sys
import numpy as np
from lib import wavio, flrender as F

N = 88200
g = 0.399871826171875 / 0.5   # FL's channel gain at velocity 128, per 0.5
os.makedirs('probe/tail', exist_ok=True)
# A faint noise floor: FL renders nothing from a file that is silent but for a click.
floor = (np.random.default_rng(1).standard_normal((N, 2)) * 1e-3).astype(np.float32)


def frac(x):
    return float(f'{x:.6g}')


for span in (int(a) for a in sys.argv[1:]):
    ln = frac((span + 0.25) / (N - 1))

    def render(q):
        x = floor.copy()
        if q is not None:
            x[q] += 0.5
        name = f'tsf{span}_' + ('none' if q is None else f'q{q}')
        p = os.path.abspath(f'probe/tail/{name}.wav')
        if not os.path.exists(p):
            wavio.write_float(p, x, 44100)
        return F.render(name, p, [F.params(128, '<d', 0.0), F.params(136, '<d', ln)], velocity=128, beats=8)[:, 0] / g

    base = render(None)
    R = np.zeros((span + 5, span + 10))
    for q in range(span + 5):
        R[q] = (render(q) - base)[:span + 10] / 0.5
    np.save(f'data/trim_matrix_span{span}.npy', R)
    alive = [q for q in range(span + 5) if np.abs(R[q]).max() > 1e-3]
    print(f'span {span}: inputs that reach the output {alive[0]}..{alive[-1]}, '
          f'outputs {np.flatnonzero(np.abs(R).max(0) > 1e-3)[[0, -1]]}', flush=True)
