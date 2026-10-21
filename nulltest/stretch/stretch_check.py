"""Offline sanity check of every stretch mode (no FL needed): renders a sine
and a drum loop with PITCH +600 cents and MUL x2 through ChanSamplerRender and
reports duration, pitch, level, the largest sample step and render time."""
import subprocess, numpy as np, time, os, sys; from lib import wavio
from lib import paths

HERE = paths.ROOT
RENDER = paths.RENDER_TOOL
MODES = {1: 'stretch', 2: 'pro', 3: 'e3gen', 4: 'e3mono', 5: 'slice', 8: 'e2gen', 9: 'e2trans', 10: 'e2mono', 11: 'e2speech'}

def run(out_dir, pitch=600, mul=2.0, extra=''):
    for smp in ('signals/sine3.wav', 'signals/loud.wav'):
        x, _ = wavio.read(os.path.join(HERE, smp)); xl = x[:, 0]
        for m, nm in MODES.items():
            pf = os.path.join(out_dir, 'p.txt')
            open(pf, 'w').write(f'stretchMode {m}\npitch {pitch}\nstretchMul {mul}\n{extra}')
            out = os.path.join(out_dir, f'm_{nm}_{smp}')
            t = time.time()
            subprocess.run([RENDER, os.path.join(HERE, smp), out, '8', pf, '60:0:7.9:128'], check=True)
            dt = time.time() - t
            y, _ = wavio.read(out); yl = y[:, 0]
            nz = np.nonzero(np.abs(yl) > 1e-3)[0]
            dur = (nz[-1] + 1) / 44100 if len(nz) else 0
            s = yl[8000:40000]
            f = np.abs(np.fft.rfft(s * np.hanning(len(s)), 262144)); fr = np.argmax(f) * 44100 / 262144
            jump = np.max(np.abs(np.diff(yl)))
            rmsr = np.sqrt((yl[:max(1, int(dur * 44100))] ** 2).mean()) / np.sqrt((xl ** 2).mean())
            print(f'{smp:10s} {nm:9s} dur {dur:5.2f}s  peakfreq {fr:7.1f}  rms ratio {rmsr:5.2f}  max step {jump:.3f}  {dt:.1f}s', flush=True)

if __name__ == '__main__':
    run(sys.argv[1])
