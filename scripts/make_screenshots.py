"""Regenerates docs/screenshots with each plug-in's snapshot tool (configure with the tools on and build every
<Stem>Snapshot first): <pluginid>.png per plug-in at its design size at 100 % (the tool's standard call), the views a
plug-in lists in VIEWS below as <pluginid>-<view>.png, and hero.png, the plug-ins side by side 24 px apart (at most
1800 px wide). Every image is our own editor, rendered without a window, so a run gives the same files on every
machine. Needs numpy and Pillow (requirements.txt).

    python scripts/make_screenshots.py [--build build]
"""
import argparse, os, struct, subprocess, sys, tempfile
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'docs', 'screenshots')
GAP = 24          # hero.png: between the editors
HERO_MAX = 1800   # hero.png: the widest it may be


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
    open(path, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)


# Chan Sampler's snapshot tool also takes a sample, a parameter file and a sheet (plugin/tools/Snapshot.cpp):
# (file, parameter lines, arguments after the parameter file, environment). Its main image is the looped drum loop on
# the Sample sheet; the sample view's three colour modes are cropped from the Sample sheet and stacked.
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


def plugins():
    with open(os.path.join(ROOT, 'packaging', 'plugins.txt'), encoding='utf-8') as h:
        return [line.split('|')[0].strip() for line in h if line.strip() and not line.startswith('#')]


def tool(build, target):
    """<Stem>Snapshot: the class stem is the target without a leading The."""
    stem = target[3:] if target.startswith('The') else target
    exe = os.path.join(build, f'{stem}Snapshot_artefacts', 'Release', f'{stem}Snapshot' + ('.exe' if sys.platform == 'win32' else ''))
    if not os.path.isfile(exe):
        sys.exit(f'{exe} is not built (configure with the tools on and build {stem}Snapshot)')
    return exe


def run(cmd, env=None):
    subprocess.run(cmd, env=dict(os.environ, **(env or {})), check=True, stdout=subprocess.DEVNULL, cwd=ROOT)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--build', default=os.path.join(ROOT, 'build'))
    a = ap.parse_args()
    tmp = tempfile.mkdtemp()
    loop = os.path.join(tmp, 'drumloop.wav'); drum_loop(loop)
    mains = []
    for target in plugins():
        exe, pid = tool(a.build, target), target.lower()
        spec = VIEWS.get(target)

        def shot(name, params, args, env):
            pfile = os.path.join(tmp, name + '.txt')
            open(pfile, 'w').write(params)
            run([exe, loop, os.path.join(OUT, name + '.png'), pfile] + args, env)
            print('wrote', name + '.png')

        if spec is None:
            run([exe, os.path.join(OUT, pid + '.png')])     # the standard call
            print('wrote', pid + '.png')
        else:
            shot(pid, *spec['main'])
            for view, params, args, env in spec['views']:
                shot(f'{pid}-{view}', params, args, env)
            view, box, props = spec['stack']
            crops = []
            for i, p in enumerate(props):
                pfile = os.path.join(tmp, 'plain.txt'); open(pfile, 'w').write('')
                part = os.path.join(tmp, f'stack{i}.png')
                run([exe, loop, part, pfile, '0'], {'SNAPSHOT_PROPS': p})
                crops.append(Image.open(part).convert('RGB').crop(box))
            w, h = crops[0].size
            sheet = Image.new('RGB', (w, len(crops) * h + (len(crops) - 1) * 10), (0, 0, 0))
            for i, c in enumerate(crops):
                sheet.paste(c, (0, i * (h + 10)))
            sheet.save(os.path.join(OUT, f'{pid}-{view}.png'), optimize=True)
            print('wrote', f'{pid}-{view}.png')
        mains.append(Image.open(os.path.join(OUT, pid + '.png')).convert('RGBA'))

    # hero.png: the editors side by side, on a transparent page
    w = sum(m.width for m in mains) + GAP * (len(mains) - 1)
    h = max(m.height for m in mains)
    hero = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    x = 0
    for m in mains:
        hero.paste(m, (x, 0)); x += m.width + GAP
    if hero.width > HERO_MAX:
        hero = hero.resize((HERO_MAX, round(hero.height * HERO_MAX / hero.width)), Image.LANCZOS)
    hero.save(os.path.join(OUT, 'hero.png'), optimize=True)
    print('wrote hero.png', hero.size)


if __name__ == '__main__':
    main()
