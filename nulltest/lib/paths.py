"""Where the null-test files live (everything relative to nulltest/)."""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # nulltest/
SIGNALS = os.path.join(ROOT, 'signals')   # test signals (inputs)
DATA = os.path.join(ROOT, 'data')         # saved measurements, FL project data
RENDER_TOOL = os.path.join(ROOT, '..', 'build', 'ChanSamplerRender_artefacts', 'Release',
                           'ChanSamplerRender' + ('.exe' if sys.platform == 'win32' else ''))

# FL Studio 2026: set FL_STUDIO_DIR when it is installed elsewhere.
FL_DIR = os.environ.get('FL_STUDIO_DIR', r'C:\Program Files\Image-Line\FL Studio 2026')
FL_EXE = os.path.join(FL_DIR, 'FL64.exe')
FL_PATCHES = os.path.join(FL_DIR, 'Data', 'Patches')   # FL's own samples (read, never copied here)


def signal(name):
    """A sample by name: as given if it exists (absolute, or relative to
    nulltest/), else in signals/."""
    for p in (name, os.path.join(ROOT, name), os.path.join(SIGNALS, name)):
        if os.path.isabs(p) and os.path.exists(p):
            return os.path.normpath(p)
    return os.path.normpath(os.path.join(SIGNALS, name))
