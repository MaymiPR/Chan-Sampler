"""FL's tail filter on a short trimmed segment: LENGTH 0 = 40 frames from
SMP START 0. One click per render (the support may differ from long segments)."""
import numpy as np, os, sys
from lib import wavio, flrender as F
N = 88200; L = int(sys.argv[1]) if len(sys.argv) > 1 else 40
lenFrac = 0.0 if L == 40 else (L - 0.5) / N
rng = np.random.default_rng(1)
floor = (rng.standard_normal((N, 2)) * 1e-4).astype(np.float32)
g = 0.399871826171875 / 0.5
def render(name, d):
    x = floor.copy()
    if d: x[L - d] += 0.5
    p = os.path.abspath(f'probe/tail/{name}.wav')
    if not os.path.exists(p): wavio.write_float(p, x, 44100)
    y = F.render(f'tail_{name}', p, [F.params(128, '<d', 0.0), F.params(136, '<d', lenFrac)], velocity=128, beats=8)
    return y[:, 0] / g
base = render(f's{L}_floor', 0)
nz = np.nonzero(np.abs(base) > 1e-7)[0]; print('segment ends at', nz[-1] + 1, flush=True)
M = np.zeros((L + 1, L + 1))
for d in range(1, L + 1):
    y = render(f's{L}_d{d}', d) - base
    for D in range(1, L + 1):
        M[D, d] = y[L - D] / 0.5
    print(d, round(M[d, d], 4), flush=True)
np.save(f'tail_matrix_{L}.npy', M)
