"""Regenerates the README's screenshots with ChanSamplerSnapshot (build it
first): a synthetic drum loop on the Sample page (loop on), the precomputed
effects' second page, the Envelope page, the Misc page, the spectrum view and
the sample view's colour modes, drawn at 1.6x. Needs numpy and Pillow.

    python docs/screenshots/make.py"""
import os, struct, subprocess, sys, tempfile
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
TOOL = os.path.join(ROOT, 'build', 'ChanSamplerSnapshot_artefacts', 'Release',
                    'ChanSamplerSnapshot' + ('.exe' if sys.platform == 'win32' else ''))


VIEW = (12, 482, 889, 690)   # the sample view and its scroll bar at 1.6x
GAP = 10


def drum_loop(path, sr=44100, bpm=120):
    """Two bars of kick, snare and panned hats (16-bit stereo)."""
    beat = 60 / bpm; n = int(8 * beat * sr); x = np.zeros((n, 2)); rng = np.random.default_rng(5)

    def add(t0, sig, pan=0.0):
        i = int(t0 * sr); m = min(len(sig), n - i)
        x[i:i + m, 0] += sig[:m] * (1 - pan); x[i:i + m, 1] += sig[:m] * (1 + pan)

    def kick():
        t = np.arange(int(0.4 * sr)) / sr; f = 45 + 120 * np.exp(-t * 30)
        return 0.9 * np.sin(2 * np.pi * np.cumsum(f) / sr) * np.exp(-t * 7)

    def snare():
        t = np.arange(int(0.25 * sr)) / sr; nz = rng.standard_normal(len(t))
        return 0.45 * nz * np.exp(-t * 18) + 0.4 * np.sin(2 * np.pi * 190 * t) * np.exp(-t * 25)

    def hat():
        t = np.arange(int(0.06 * sr)) / sr; nz = np.diff(np.concatenate([[0], rng.standard_normal(len(t))]))
        return 0.18 * nz * np.exp(-t * 70)

    for b in range(8):
        add(b * beat, kick() if b % 2 == 0 else snare())
        for h in range(2):
            add(b * beat + h * beat / 2, hat(), pan=0.3 if h else -0.3)
    add(6.5 * beat, kick())
    x /= np.abs(x).max() * 1.05
    raw = (x * 32767).astype('<i2').tobytes()
    fmt = struct.pack('<HHIIHH', 1, 2, sr, sr * 4, 4, 16)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', 16) + fmt + b'data' + struct.pack('<I', len(raw)) + raw
    open(path, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)


def main():
    tmp = tempfile.mkdtemp()
    loop = os.path.join(tmp, 'drumloop.wav'); drum_loop(loop)
    looped = os.path.join(tmp, 'looped.txt'); open(looped, 'w').write('useLoop 1\nloopStart 0.5\nloopEnd 1\nstartOffset 0.02\n')
    plain = os.path.join(tmp, 'plain.txt'); open(plain, 'w').write('')
    env = dict(os.environ, SNAPSHOT_SCALE='1.6')
    shots = [('sampler.png', looped, ['0'], {}),
             ('envelope.png', plain, ['1', '0', '1'], {}),
             ('misc.png', plain, ['2'], {}),
             ('spectrum.png', plain, ['0'], {'SNAPSHOT_PROPS': 'viewSpectrum=1'}),
             ('effects.png', plain, ['0', '0', '1'], {})]
    for name, params, args, extra in shots:
        subprocess.run([TOOL, loop, os.path.join(HERE, name), params] + args, env=dict(env, **extra), check=True,
                       stdout=subprocess.DEVNULL)
        print('wrote', name)

    # the sample view alone, in its three colour modes, one above the other
    views = []
    for i, props in enumerate(('colorfulWaveforms=2', 'colorfulWaveforms=1', 'viewMultichannel=1')):
        shot = os.path.join(tmp, 'view%d.png' % i)
        subprocess.run([TOOL, loop, shot, plain, '0'], env=dict(env, SNAPSHOT_PROPS=props), check=True,
                       stdout=subprocess.DEVNULL)
        views.append(Image.open(shot).convert('RGB').crop(VIEW))
    w, h = views[0].size
    sheet = Image.new('RGB', (w, 3 * h + 2 * GAP), (24, 27, 29))
    for i, v in enumerate(views):
        sheet.paste(v, (0, i * (h + GAP)))
    sheet.save(os.path.join(HERE, 'waveforms.png'), optimize=True)
    print('wrote waveforms.png')


if __name__ == '__main__':
    main()
