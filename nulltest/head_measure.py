"""FL's trimmed-start fade (SMP START > 0): one click per render at offset e
after the start; H[n, e] = output n frames after the start per unit click."""
import numpy as np, os
import wavio, flrender as F
N = 88200; S = 0.25; start = int(np.floor(S * (N - 1) + 0.5)); E = 32
rng = np.random.default_rng(1)
floor = (rng.standard_normal((N, 2)) * 1e-4).astype(np.float32)
g = 0.399871826171875 / 0.5
def render(name, e):
    x = floor.copy()
    if e is not None: x[start + e] += 0.5
    p = os.path.abspath(f'probe/tail/{name}.wav')
    if not os.path.exists(p): wavio.write_float(p, x, 44100)
    y = F.render(f'tail_{name}', p, [F.params(128, '<d', S), F.params(136, '<d', 1.0)], velocity=128, beats=8)
    return y[:, 0] / g
base = render('head_floor', None)
H = np.zeros((E + 40, E))
for e in range(E):
    y = render(f'head_e{e}', e) - base
    H[:, e] = y[:E + 40] / 0.5
    print(e, round(H[e, e], 4), 'nonzero rows', np.nonzero(np.abs(H[:, e]) > 2e-4)[0].tolist()[:12], flush=True)
np.save('head_matrix.npy', H)
