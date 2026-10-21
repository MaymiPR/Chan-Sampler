"""Single-sample clicks on alternating even / odd samples (e3_par) through
e3 generic at exactly +1200 / -1200 cents: which clicks survive the read at
twice the rate, and at what level (energy against the click's own), FL's and
ours. At +1200 both drop the even ones (E3_NOTES.md, round 8).

    python -m stretch.make_signals
    python -m stretch.e3_parity"""
import numpy as np
from lib import flrender as F
from stretch import common as C

POSITIONS = [20000 + k * 4001 for k in range(16)]

for cents in (1200, -1200):
    fl = F.render(f'e3par_{cents}', 'e3_par.wav', [F.params(108, '<i', 1), F.params(100, '<i', cents)], beats=12)[:, 0] / C.GAIN
    ours = C.ours('e3_par.wav', f'par_{cents}', cents, 'e3gen', 1)
    r = 2 ** (cents / 1200)
    print('cents', cents)
    for p in POSITIONS:
        t = int(round(p / r))
        e = [(y[t - 300:t + 300] ** 2).sum() / 0.25 if t + 300 <= len(y) else float('nan') for y in (fl, ours)]
        print(f'  click at {p} ({"odd" if p % 2 else "even"}): energy FL {e[0]:.2f}, ours {e[1]:.2f}')
