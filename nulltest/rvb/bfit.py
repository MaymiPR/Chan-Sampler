import numpy as np, pulses as P, combs as C, sys
from model import ap
def atomB(n, d2):
    x = np.zeros(n); x[0] = 1
    for D in (67, d2, 101, 197, 599): x = ap(x, D, .7)
    out = np.zeros(n); acc = 0
    for i in range(n): acc = 0.7 * acc + x[i]; out[i] = acc
    return out
if __name__ == '__main__':
    n = int(sys.argv[1])
    for m in (32, 128, 256):
        for ch, d2 in ((0, 97), (1, 79)):
            y = P.ir(f'rv_B{m}', ch)[:n].copy(); y[0] -= 0.7
            s, r = C.pursue(y, atomB(n, d2), 0, n, 0.0012)
            print(m, ch, 'resid', round(np.sqrt((r ** 2).mean()) * 13103, 2), [(k, round(a, 4)) for k, a in s.items()])
