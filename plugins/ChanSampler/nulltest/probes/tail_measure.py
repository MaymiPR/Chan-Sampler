"""Measure FL's trimmed-tail filter (SMP LENGTH < 100%): linear, and a click d
frames before the cut only affects the output from cut-d to cut-d/2, so clicks
whose distances more than double from one to the next are measured together.
Result: C[D, d] = output at distance D from the cut per unit click at distance d."""
import numpy as np, os
from lib import wavio, flrender as F
N = 88200; cut = 44100; K = 200
rng = np.random.default_rng(1)
os.makedirs('probe/tail', exist_ok=True)   # the click signals (generated, not in the repository)
floor = (rng.standard_normal((N, 2)) * 1e-4).astype(np.float32)
g = 0.399871826171875 / 0.5            # render gain (full-level click)
amp = 0.5

def render(name, ds):
    x = floor.copy()
    for d in ds: x[cut - d] += amp
    p = os.path.abspath(f'probe/tail/{name}.wav')
    if not os.path.exists(p): wavio.write_float(p, x, 44100)
    y = F.render(f'tail_{name}', p, [F.params(128, '<d', 0.0), F.params(136, '<d', 0.5)], velocity=128, beats=8)
    return y[:, 0] / g

# chains of distances, each less than half of the previous one
left = list(range(K, 0, -1)); chains = []
while left:
    chain = [left.pop(0)]
    for d in list(left):
        if d * 2 + 6 < chain[-1]:
            chain.append(d); left.remove(d)
    chains.append(chain)
print(len(chains), 'renders', flush=True)
base = render('floor_only', [])
C = np.zeros((K + 1, K + 1))
for ci, chain in enumerate(chains):
    y = render('ch2_%d' % ci, chain) - base
    for d in chain:
        lo = d // 2 - 2                 # support down to distance ~d/2 (two frames margin)
        for D in range(max(1, lo), d + 1):
            C[D, d] = y[cut - D] / amp
    print(ci, chain, flush=True)
np.save('data/tail_matrix.npy', C)
