"""Where the null-test files live (everything relative to nulltest/)."""
import os, sys

ROOT = os.path.dirname(os.path.abspath(__file__))                    # plugin/nulltest/
REPO = os.path.normpath(os.path.join(ROOT, '..', '..'))              # the repository root (build/ is there)
SIGNALS = os.path.join(ROOT, 'signals')   # test signals (inputs)
DATA = os.path.join(REPO, 'measurements')   # the measured data (published)
LOCAL = os.path.join(ROOT, 'local')       # files FL wrote (git-ignored, never committed)
RENDER_TOOL = os.path.join(REPO, 'build', 'CsRender_artefacts', 'Release',
                           'CsRender' + ('.exe' if sys.platform == 'win32' else ''))


def _find_fl():
    """FL Studio's folder: FL_DIR when set, else the newest FL Studio
    folder holding FL64.exe in Program Files / Image-Line."""
    if os.environ.get('FL_DIR'):
        return os.environ['FL_DIR']
    base = r'C:\Program Files\Image-Line'
    exes = [os.path.join(base, d, 'FL64.exe') for d in (os.listdir(base) if os.path.isdir(base) else [])
            if d.startswith('FL Studio')]
    exes = [e for e in exes if os.path.exists(e)]
    return os.path.dirname(max(exes, key=os.path.getmtime)) if exes else os.path.join(base, 'FL Studio')


# FL Studio: set FL_DIR when it is installed elsewhere.
FL_DIR = _find_fl()
FL_EXE = os.path.join(FL_DIR, 'FL64.exe')
FL_PATCHES = os.path.join(FL_DIR, 'Data', 'Patches')   # FL's own samples (read, never copied here)


def _find_vst3():
    """Our VST3 as FL loads it: NULLTEST_PLUGIN_VST3 when set, else the Release
    VST3 under build/ (the junction in Common Files points at it)."""
    import glob
    if os.environ.get('NULLTEST_PLUGIN_VST3'):
        return os.environ['NULLTEST_PLUGIN_VST3']
    hits = glob.glob(os.path.join(REPO, 'build', '*_artefacts', 'Release', 'VST3', '*.vst3',
                                  'Contents', 'x86_64-win', '*.vst3'))
    return os.path.normpath(hits[0]) if hits else None


PLUGIN_VST3 = _find_vst3()   # in-FL pair renders depend on it (queue_lib)


def signal(name):
    """A sample by name: as given if it exists (absolute, or relative to
    nulltest/), else in signals/."""
    for p in (name, os.path.join(ROOT, name), os.path.join(SIGNALS, name)):
        if os.path.isabs(p) and os.path.exists(p):
            return os.path.normpath(p)
    return os.path.normpath(os.path.join(SIGNALS, name))


def data(name):
    """A measured data file by name, in measurements/."""
    return os.path.join(DATA, name)
