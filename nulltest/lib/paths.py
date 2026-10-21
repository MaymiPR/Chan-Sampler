"""Where the null-test files live (everything relative to nulltest/)."""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # nulltest/
SIGNALS = os.path.join(ROOT, 'signals')   # test signals (inputs)
DATA = os.path.join(ROOT, 'data')         # saved measurements, FL project data
RENDER_TOOL = os.path.join(ROOT, '..', 'build', 'ChanSamplerRender_artefacts', 'Release',
                           'ChanSamplerRender' + ('.exe' if sys.platform == 'win32' else ''))


def signal(name):
    """A sample by name: as given if it exists (absolute, or relative to
    nulltest/), else in signals/."""
    for p in (name, os.path.join(ROOT, name), os.path.join(SIGNALS, name)):
        if os.path.isabs(p) and os.path.exists(p):
            return os.path.normpath(p)
    return os.path.normpath(os.path.join(SIGNALS, name))
