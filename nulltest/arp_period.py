import nullfl, numpy as np, arp_time as T, sys
def period(y):
    # pitch per 256 samples; step period = mean distance between changes over many notes
    hop = 128; fr = []
    for i in range(0, 44100 * 3 - 2048, hop):
        s = y[i:i + 1024]
        f = np.abs(np.fft.rfft(s * np.hanning(len(s)), 16384)); fr.append(np.argmax(f) * 44100 / 16384)
    fr = np.array(fr)
    note = np.digitize(fr, [245, 300])          # 0: C, 1: E, 2: G
    ch = [i * hop for i in range(1, len(note)) if note[i] != note[i - 1]]
    if len(ch) < 3: return None
    return (ch[-1] - ch[0]) / (len(ch) - 1)
for raw in [int(v) for v in sys.argv[1:]]:
    y = T.fl_only(f'at_{raw}', raw)
    p = period(y)
    print(raw, 'period', p, 'steps', p / (44100 * 60 / 130 / 4) if p else 'hold/none')
