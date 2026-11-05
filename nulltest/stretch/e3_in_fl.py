"""e3 generic in FL: FL's Sampler and the Chan Sampler VST3 in one project
(cases/e3_pitch_cases.py: 707 Kick, a Rhodes, a sung "ah", DL Groover and
e3_loop at -1200 ... +1200 cents), compared by envelope / band-spectrum
distance and the null, raw and with the level fitted. The reference for e3's
pitch behaviour (nulltest/E3_NOTES.md).

    python -m stretch.e3_in_fl        # cached renders
    python -m stretch.e3_in_fl -f     # re-render in FL (close FL first; rebuild the VST3)"""
import sys
import numpy as np
from lib import nullfl
from cases import e3_pitch_cases as E
from stretch.metrics import envdist, specdist, null

LENGTH = {'k707': 6000, 'rhodes': 24000, 'vox': 66000, 'groove': 66000, 'loop': 66000}   # samples compared
CENTS = (-1200, -700, -300, 300, 700, 1200)


def run(force=False):
    En, Sp, Nr, Nf = [], [], [], []
    for k in LENGTH:
        row = []
        for c in CENTS:
            cs = next(x for x in E.CASES if x['name'] == f'e3p_{k}_{c}')
            a, b = nullfl.render_pair(cs['name'], cs['sample'], cs['fl'], cs['ours'], beats=cs['beats'], force=force)
            n = LENGTH[k]; fl, ours = a[:n, 0], b[:n, 0]
            e, s, nr, nf = envdist(fl, ours, floor=False), specdist(fl, ours), null(fl, ours, fit=False), null(fl, ours)
            En.append(e); Sp.append(s); Nr.append(nr); Nf.append(nf)
            row.append('%4.1f/%4.1f/%5.1f' % (e, s, nr))
        print('  %-7s ' % k + ' '.join(row), flush=True)
    print('mean env %.2f spec %.2f null(raw) %.2f null(fit) %.2f' % (np.mean(En), np.mean(Sp), np.mean(Nr), np.mean(Nf)))


if __name__ == '__main__':
    run('-f' in sys.argv)
