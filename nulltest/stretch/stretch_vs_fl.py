"""Compare each stretch mode with FL's render of the same settings by
log-spectrogram distance (Elastique cannot null; this guides the algorithms)."""
import subprocess, numpy as np, os, sys; from lib import flrender as F, wavio
from lib import paths
HERE = paths.ROOT
RENDER = paths.RENDER_TOOL
# (our index, FL raw, name)
MODES = [(1, -1, 'stretch'), (2, -2, 'pro'), (3, 1, 'e3gen'), (4, 2, 'e3mono'), (5, 3, 'slice'),
         (8, 6, 'e2gen'), (9, 7, 'e2trans'), (10, 8, 'e2mono'), (11, 9, 'e2speech')]
def spec(y):
    n = 2048; h = 512; w = np.hanning(n)
    fr = [np.abs(np.fft.rfft(y[i:i + n] * w)) for i in range(0, len(y) - n, h)]
    return 20 * np.log10(np.array(fr) + 1e-4)
def dist(a, b):
    m = min(len(a), len(b)); A = spec(a[:m]); B = spec(b[:m])
    mask = A > (A.max() - 70)
    return float(np.mean(np.abs(A - B)[mask]))
def run(out_dir, sample='signals/loud.wav', cents=600, mulraw=10000):
    for ours, flraw, nm in MODES:
        fl = F.render(f'sv_{nm}_{sample[:-4]}_{cents}_{mulraw}', sample,
                      [F.params(108, '<i', flraw), F.params(100, '<i', cents), F.params(104, '<i', mulraw)], velocity=128, beats=16)
        pf = os.path.join(out_dir, 'p.txt')
        open(pf, 'w').write(f'stretchMode {ours}\npitch {cents}\nstretchMul {2 ** (mulraw / 10000)}\n')
        out = os.path.join(out_dir, f'sv_{nm}.wav')
        subprocess.run([RENDER, paths.signal(sample), out, str(len(fl) / 44100), pf, f'60:0:{16 * 60 / 130}:128'], check=True)
        y, _ = wavio.read(out)
        a = fl[:, 0] / (np.sqrt((fl[:, 0] ** 2).mean()) + 1e-12); b = y[:, 0] / (np.sqrt((y[:, 0] ** 2).mean()) + 1e-12)
        print(f'{nm:9s} spectral distance {dist(a, b):5.2f} dB   level ratio ours/FL {np.sqrt((y[:,0]**2).mean()/(fl[:,0]**2).mean()):.2f}', flush=True)
if __name__ == '__main__':
    run(sys.argv[1], *(sys.argv[2:3] or []))
