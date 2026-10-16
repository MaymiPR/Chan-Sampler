import nullfl, numpy as np, sys
def track(y, hop=256, win=2048):
    out = []
    for i in range(0, len(y) - win, hop):
        s = y[i:i + win]
        if np.abs(s).max() < 1e-3: out.append(0); continue
        f = np.abs(np.fft.rfft(s * np.hanning(win), 8 * win)); out.append(np.argmax(f) * 44100 / (8 * win))
    return np.array(out)
def segments(fr, hop=256):
    segs = []; start = 0
    for i in range(1, len(fr) + 1):
        if i == len(fr) or abs(fr[i] - fr[start]) > 8:
            segs.append((round(start * hop / 44100, 3), round(float(np.median(fr[start:i])))));  start = i
    return segs
for name in sys.argv[1:]:
    a, b = nullfl.render_pair(name, 'sine3.wav')
    for lab, y in (('FL', a[:, 0]), ('ours', b[:, 0])):
        print(name, lab, segments(track(y[:44100 * 2]))[:16])
