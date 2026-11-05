"""Where FL ends a LENGTH-trimmed sample: Source/TrimTail.h's end filter is
applied at every candidate end of the source segment and matched to FL's half
of each in-FL trim case; prints the formula's length and FL's (best) end.
FL: round(fraction * (N - 1 - start * (N - 1))) half to even, +1 reversed."""
import os, re, math, numpy as np
from lib import nullfl, wavio, paths
from cases import trim_sweep as T, trim_fit_cases as TF

TXT = open(os.path.join(paths.ROOT, '..', 'Source', 'TrimTail.h')).read()
def table(name, typ):
    m = re.search(r'constexpr %s %s\[\] = \{(.*?)\};' % (typ, name), TXT, re.S)
    return np.array([float(v.rstrip('f')) for v in re.findall(r'[-+0-9.e]+f?', m.group(1))])
START, COUNT, COEF = table('start', 'int').astype(int), table('count', 'int').astype(int), table('coef', 'float')
GAIN = 0.389614546619848   # FL's default channel level in the test projects

def tail(x):
    d = x.copy(); L = len(d); n = min(L, 200)
    inp = np.zeros(202); inp[1:n + 1] = d[L - np.arange(1, n + 1)]
    for dist in range(1, n + 1):
        cnt = min(COUNT[dist - 1], n - dist + 1)
        d[L - dist] = float(np.dot(COEF[START[dist - 1]:START[dist - 1] + cnt], inp[dist:dist + cnt]))
    return d

def fl_end(name, st, ln, reverse, src):
    a, _ = nullfl.render_pair(name, None); a = a[:, 0]; N = len(src)
    sp = st * (N - 1); start = math.floor(sp + 0.5)
    formula = max(40, round(ln * (N - 1 - sp)) + (1 if reverse else 0))
    best = None
    for M in range(formula - 3, formula + 4):
        seg = tail(src[start:start + M] * GAIN)
        y = np.zeros(len(a)); y[:M] = seg[::-1] if reverse else seg
        w = slice(0, M + 40) if reverse else slice(max(0, M - 250), M + 20)
        err = float(((a[w] - y[w]) ** 2).sum())
        if best is None or err < best[0]: best = (err, M)
    return formula, best[1]

if __name__ == '__main__':
    src = wavio.read(paths.signal('noise.wav'))[0][:, 0]
    for c in T.CASES + TF.CASES:
        o = c['ours']; ln = o.get('pcLength', 1.0)
        if ln <= 0 or ln >= 1 or 'noise.wav' not in c['sample'] or 'noise_q' in c['sample']: continue
        f, e = fl_end(c['name'], o.get('pcStart', 0.0), ln, bool(o.get('pcReverse')), src)
        print(f"{c['name']:20s} formula {f:6d}  FL {e:6d}  {'ok' if f == e else 'DIFF'}", flush=True)
