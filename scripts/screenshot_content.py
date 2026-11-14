"""The content Chan Sampler's screenshots need, and how its snapshot tool is called. scripts/make_screenshots.py
calls this module (prepare once, then render per plug-in); the starter's script itself stays as the starter writes it.

A sampler without a sample shows an empty editor, so the views show a drum loop. It is made here, by script, from a
fixed seed: no audio file is committed. Chan Sampler's snapshot tool (plugin/tools/Snapshot.cpp) takes the sample, the
image, a parameter file and the sheet, and reads view properties from the environment variable SNAPSHOT_PROPS.
Needs numpy and Pillow (requirements.txt).
"""
import os, struct, subprocess
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (parameter lines, arguments after the parameter file, environment). The main image is the looped drum loop on the
# Sample sheet; the sample view's three colour modes are cropped from the Sample sheet and stacked.
LOOPED = 'useLoop 1\nloopStart 0.5\nloopEnd 1\nstartOffset 0.02\n'
VIEWS = {
    'ChanSampler': {
        'main': (LOOPED, ['0'], {}),
        'views': [('envelope', '', ['1', '0', '1'], {}),
                  ('misc', '', ['2'], {}),
                  ('spectrum', '', ['0'], {'SNAPSHOT_PROPS': 'viewSpectrum=1'}),
                  ('effects', '', ['0', '0', '1'], {})],
        'stack': ('waveforms', (8, 301, 556, 431), ['colorfulWaveforms=2', 'colorfulWaveforms=1', 'viewMultichannel=1']),
    },
}


def drum_loop(path, sr=44100, bpm=120):
    """Two bars of kick, snare and panned hats (16-bit stereo), from a fixed seed: the sample a sampler's views show."""
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
    with open(path, 'wb') as h:
        h.write(b'RIFF' + struct.pack('<I', len(body)) + body)


def prepare(tmp):
    """The drum loop, written once into the temporary folder."""
    drum_loop(os.path.join(tmp, 'drumloop.wav'))


def run(cmd, env=None):
    subprocess.run(cmd, env=dict(os.environ, **(env or {})), check=True, stdout=subprocess.DEVNULL, cwd=ROOT)


def render(target, exe, out, tmp):
    """Every image of a plug-in listed in VIEWS; False for any other, which takes the standard calls."""
    spec = VIEWS.get(target)
    if spec is None:
        return False
    loop, pid = os.path.join(tmp, 'drumloop.wav'), target.lower()

    def shot(name, params, args, env):
        pfile = os.path.join(tmp, name + '.txt')
        with open(pfile, 'w') as h:
            h.write(params)
        run([exe, loop, os.path.join(out, name + '.png'), pfile] + args, env)
        print('wrote', name + '.png')

    shot(pid, *spec['main'])
    for view, params, args, env in spec['views']:
        shot(f'{pid}-{view}', params, args, env)
    view, box, props = spec['stack']
    plain = os.path.join(tmp, 'plain.txt')
    with open(plain, 'w') as h:
        h.write('')
    crops = []
    for i, p in enumerate(props):
        part = os.path.join(tmp, f'stack{i}.png')
        run([exe, loop, part, plain, '0'], {'SNAPSHOT_PROPS': p})
        with Image.open(part) as im:
            crops.append(im.convert('RGB').crop(box))
    w, h = crops[0].size
    sheet = Image.new('RGB', (w, len(crops) * h + (len(crops) - 1) * 10), (0, 0, 0))
    for i, c in enumerate(crops):
        sheet.paste(c, (0, i * (h + 10)))
    sheet.save(os.path.join(out, f'{pid}-{view}.png'), optimize=True)
    print('wrote', f'{pid}-{view}.png')
    return True
