"""Where the null-test files live (everything relative to nulltest/)."""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # nulltest/
SIGNALS = os.path.join(ROOT, 'signals')   # test signals (inputs)
DATA = os.path.join(ROOT, 'data')         # saved measurements, FL project data
RENDER_TOOL = os.path.join(ROOT, '..', 'build', 'ChanSamplerRender_artefacts', 'Release',
                           'ChanSamplerRender' + ('.exe' if sys.platform == 'win32' else ''))


def _find_fl():
    """FL Studio's folder: FL_STUDIO_DIR when set, else the newest FL Studio
    folder holding FL64.exe in Program Files / Image-Line."""
    if os.environ.get('FL_STUDIO_DIR'):
        return os.environ['FL_STUDIO_DIR']
    base = r'C:\Program Files\Image-Line'
    exes = [os.path.join(base, d, 'FL64.exe') for d in (os.listdir(base) if os.path.isdir(base) else [])
            if d.startswith('FL Studio')]
    exes = [e for e in exes if os.path.exists(e)]
    return os.path.dirname(max(exes, key=os.path.getmtime)) if exes else os.path.join(base, 'FL Studio')


# FL Studio: set FL_STUDIO_DIR when it is installed elsewhere.
FL_DIR = _find_fl()
FL_EXE = os.path.join(FL_DIR, 'FL64.exe')
FL_PATCHES = os.path.join(FL_DIR, 'Data', 'Patches')   # FL's own samples (read, never copied here)


def signal(name):
    """A sample by name: as given if it exists (absolute, or relative to
    nulltest/), else in signals/."""
    for p in (name, os.path.join(ROOT, name), os.path.join(SIGNALS, name)):
        if os.path.isabs(p) and os.path.exists(p):
            return os.path.normpath(p)
    return os.path.normpath(os.path.join(SIGNALS, name))
