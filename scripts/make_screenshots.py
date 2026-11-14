"""Regenerates docs/screenshots with each plug-in's snapshot tool (configure with the tools on and build every
<Stem>Snapshot first): <pluginid>.png per plug-in at its design size at 100 % (the tool's standard call, or the
arguments a plug-in lists in MAIN below), the views it lists in VIEWS as <pluginid>-<view>.png, and hero.png, the
plug-ins side by side 24 px apart, each centred in height (at most 1800 px wide). Every image is our own editor,
rendered without a window, so a run gives the same files on every machine. Needs Pillow (requirements.txt).

    python scripts/make_screenshots.py [--build build]

A plug-in whose editor shows nothing without content (a sampler without a sample, a vocal editor without a voice)
gets it from the suite's own module scripts/screenshot_content.py, which this script calls when the suite has one:

    prepare(tmp)                          optional, once: make the content in the temporary folder tmp (a sample, a
                                          vocal), from a script and a fixed seed; no audio file is committed
    render(target, exe, out, tmp) -> bool write every image of that plug-in into out with its own calls of the
                                          snapshot tool exe (<pluginid>.png and its views), and return True; return
                                          False for a plug-in that takes the standard calls below

The module is the suite's: the sync never writes it, and this file stays the starter's.
"""
import argparse, importlib.util, os, shutil, subprocess, sys, tempfile
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'docs', 'screenshots')
GAP = 24          # hero.png: between the editors
HERO_MAX = 1800   # hero.png: the widest it may be

# Suite block: the views of each plug-in beyond its main image, kept when the starter's form of this file is synced.
# VIEWS: the plug-in id (packaging/plugins.txt) to a list of (view, arguments of its snapshot tool), written as
# <pluginid>-<view>.png. The arguments are the tool's own: paramID=value in real units, --levels=inDb,outDb,
# --size=W, and an --option the editor's snapshot hooks take. MAIN: the arguments of the main image (none: the
# standard call). A plug-in without an entry takes the standard call and no further view.
VIEWS = {
    'ChanSampler': [],     # its views need a sample: scripts/screenshot_content.py renders them
}
MAIN = {
    'ChanSampler': [],
}
# Suite block end


def plugins():
    """[(target, class stem)] of packaging/plugins.txt: the first and the sixth column of each line."""
    out = []
    with open(os.path.join(ROOT, 'packaging', 'plugins.txt'), encoding='utf-8') as h:
        for line in h:
            if not line.strip() or line.startswith('#'):
                continue
            columns = [c.strip() for c in line.split('|')]
            if len(columns) < 6 or not columns[5]:
                sys.exit(f'packaging/plugins.txt: {columns[0]} has no class stem column (target|product name|AU '
                         f'plugin code|AU manufacturer code|AU type|class stem)')
            out.append((columns[0], columns[5]))
    return out


def tool(build, stem):
    """<Stem>Snapshot of a plug-in, by its class stem."""
    exe = os.path.join(build, f'{stem}Snapshot_artefacts', 'Release', f'{stem}Snapshot' + ('.exe' if sys.platform == 'win32' else ''))
    if not os.path.isfile(exe):
        sys.exit(f'{exe} is not built (configure with the tools on and build {stem}Snapshot)')
    return exe


def shot(exe, name, args):
    subprocess.run([exe, os.path.join(OUT, name + '.png')] + list(args), check=True, stdout=subprocess.DEVNULL, cwd=ROOT)
    print('wrote', name + '.png')


def content():
    """The suite's own module scripts/screenshot_content.py, or None when the suite has none."""
    path = os.path.join(ROOT, 'scripts', 'screenshot_content.py')
    if not os.path.isfile(path):
        return None
    spec = importlib.util.spec_from_file_location('screenshot_content', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not callable(getattr(module, 'render', None)):
        sys.exit('scripts/screenshot_content.py has no render(target, exe, out, tmp) function')
    return module


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--build', default=os.path.join(ROOT, 'build'))
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    own = content()
    tmp = tempfile.mkdtemp(prefix='screenshots-')
    mains = []
    try:
        if own is not None and callable(getattr(own, 'prepare', None)):
            own.prepare(tmp)
        for target, stem in plugins():
            exe, pid = tool(a.build, stem), target.lower()
            if own is not None and own.render(target, exe, OUT, tmp):
                print('wrote the images of', target, '(scripts/screenshot_content.py)')
            else:
                shot(exe, pid, MAIN.get(target, []))
                for view, args in VIEWS.get(target, []):
                    shot(exe, f'{pid}-{view}', args)
            main_image = os.path.join(OUT, pid + '.png')
            if not os.path.isfile(main_image):
                sys.exit(f'{pid}.png was not written: the main image of {target} is missing')
            mains.append(Image.open(main_image).convert('RGBA'))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    # hero.png: the editors side by side, each centred in height, on a transparent page
    w = sum(m.width for m in mains) + GAP * (len(mains) - 1)
    h = max(m.height for m in mains)
    hero = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    x = 0
    for m in mains:
        hero.paste(m, (x, (h - m.height) // 2)); x += m.width + GAP
    if hero.width > HERO_MAX:
        hero = hero.resize((HERO_MAX, round(hero.height * HERO_MAX / hero.width)), Image.LANCZOS)
    hero.save(os.path.join(OUT, 'hero.png'), optimize=True)
    print('wrote hero.png', hero.size)


if __name__ == '__main__':
    main()
