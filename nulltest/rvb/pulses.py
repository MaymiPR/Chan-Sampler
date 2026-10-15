"""Decompose FL's precomputed reverb impulse response into sparse pulses:
y = dry * x + kernel * w, kernel = [0.5, 1, 0.5] (pulse peak one sample late)."""
import sys, numpy as np
sys.path.insert(0, __import__('os').path.dirname(__file__) + '/..')
import flrender as F

def ir(name, ch=0):
    return (F.render(name, 'impulse.wav') * 32768)[:, ch] / 13103.0

def pulses(y, n, kernel=(0.5, 1.0, 0.5), thresh=0.004):
    r = y[:n].copy()
    k = np.array(kernel)
    w = {}
    for _ in range(4000):
        c = np.convolve(r, k[::-1], mode='full')[len(k) - 1:len(k) - 1 + n] / (k @ k)
        i = int(np.argmax(np.abs(c)))
        if abs(c[i]) < thresh: break
        w[i] = w.get(i, 0.0) + c[i]
        r[i:i + len(k)] -= c[i] * k[:max(0, min(len(k), n - i))]
    return dict(sorted(w.items())), r

if __name__ == '__main__':
    name = sys.argv[1]; n = int(sys.argv[2]); ch = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    y = ir(name, ch)
    y[0] -= 0.7 if name.startswith('rv_A') else 0.0
    w, r = pulses(y, n)
    print('residual rms', np.sqrt((r ** 2).mean()) * 13103, 'LSB')
    for i, a in w.items():
        if abs(a) > 0.004: print(i, round(a, 4))
