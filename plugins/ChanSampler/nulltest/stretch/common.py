"""Shared by the stretch measurements: the modes, FL's channel gain, and our
stretcher run on its own (ChanSamplerRender stretchtest)."""
import os, subprocess
from lib import paths, wavio

# name: (FL's stored mode, our StretchMode index)
MODES = {'e3gen': (1, 3), 'e3mono': (2, 4), 'stretch': (-1, 1), 'pro': (-2, 2), 'e2gen': (6, 8),
         'e2trans': (7, 9), 'e2mono': (8, 10), 'e2speech': (9, 11)}

# FL's Sampler at velocity 100: its render of a file divided by the file
GAIN = 0.38960788709688265

OURS = os.path.join(paths.ROOT, 'ours', 'stretch')


def mul(raw):
    """FL's MUL from its stored value (event 104)."""
    return 2 ** (raw / 10000)


def ours(sample, out_name, cents, mode, speed, force=False):
    """Our stretcher alone on a file: pitch in cents, playback speed (1 / MUL),
    a mode name or StretchMode index. Returns the left channel."""
    os.makedirs(OURS, exist_ok=True)
    out = os.path.join(OURS, out_name + '.wav')
    index = MODES[mode][1] if isinstance(mode, str) else mode
    if force or not os.path.exists(out):
        subprocess.run([paths.RENDER_TOOL, 'stretchtest', paths.signal(sample), out, str(cents), str(index), str(speed)], check=True)
    return wavio.read(out)[0][:, 0]
