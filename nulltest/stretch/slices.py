"""The slice modes against FL, the whole plugin (ChanSamplerRender) on FL's own
samples (707 Kick, a Rhodes, a sung "ah", DL Groover; read from the FL
install) and e3_loop.

  map       Slice map at MUL x2 / x1.5 / x0.667: it plays the slices as they
            are, so it nulls once the slices start where FL's do (FL's channel
            volume on plugins, 0.7792, applied; no gain fitted)
  stretch   a stretch mode (default Slice stretch) at -700 / +700 cents and
            x1.5 / x0.667: envelope / band-spectrum distance and fitted null

    python -m stretch.slices map
    python -m stretch.slices stretch          # FL's mode 3 against ours 5
    python -m stretch.slices stretch 7 9      # e2 transient (also restarts at slices)"""
import os, subprocess, sys, tempfile
import numpy as np
from lib import flrender as F, paths, wavio
from cases import e3_pitch_cases as E
from stretch.metrics import envdist, specdist, null

SAMPLES = ('k707', 'rhodes', 'vox', 'groove', 'loop')
LENGTH = {'k707': 12000, 'rhodes': 24000, 'vox': 66000, 'groove': 66000, 'loop': 66000}   # samples compared at x1
PLUGIN_CHANNEL = 0.7792   # FL's default channel volume on a plugin channel


def ours(k, params, seconds, beats):
    path = E.SAMPLES[k][0]
    tmp = tempfile.mkdtemp()
    pf = os.path.join(tmp, 'p.txt')
    open(pf, 'w').write(''.join(f'{key} {value}\n' for key, value in params.items()))
    out = os.path.join(tmp, k + '.wav')
    subprocess.run([paths.RENDER_TOOL, path if os.path.isabs(path) else paths.signal(path), out, str(seconds), pf,
                    f'60:0:{beats * 60 / 130}:100'], check=True, capture_output=True)
    return wavio.read(out)[0][:, 0]


def slice_map():
    res = []
    for raw in (10000, 5850, -5850):
        row = []
        for k in SAMPLES:
            fl = F.render(f'slmap2_{k}' if raw == 10000 else f'slmap_{raw}_{k}', E.SAMPLES[k][0],
                          [F.params(108, '<i', 4), F.params(100, '<i', 0), F.params(104, '<i', raw)], beats=24)[:, 0]
            o = PLUGIN_CHANNEL * ours(k, {'stretchMode': 6, 'stretchMul': 2 ** (raw / 10000)}, len(fl) / 44100, 24)
            n = min(len(o), len(fl), int(66000 * 2 ** (raw / 10000) * 1.5))
            res.append(null(fl[:n], o[:n], fit=False))
            row.append('%s %6.1f' % (k, res[-1]))
        print(f'MUL x{2 ** (raw / 10000):.3f}: ' + '  '.join(row), flush=True)
    print('Slice map null, mean %.1f dB' % np.mean(res))


def stretch(mode_fl=3, mode_ours=5):
    E_, S_, N_ = [], [], []
    for k in SAMPLES:
        row = []
        for st, cents, raw in (('p-700', -700, 0), ('p700', 700, 0), ('m15', 0, 5850), ('m067', 0, -5850)):
            mul = 2 ** (raw / 10000)
            fl = F.render(f'sl{mode_fl}_{k}_{st}', E.SAMPLES[k][0],
                          [F.params(108, '<i', mode_fl), F.params(100, '<i', cents), F.params(104, '<i', raw)], beats=8)
            o = ours(k, {'stretchMode': mode_ours, 'pitch': cents, 'stretchMul': mul}, len(fl) / 44100, 8)
            n = min(int(LENGTH[k] * mul), len(fl))
            a = fl[:n, 0]; o = np.concatenate([o, np.zeros(max(0, n - len(o)))])[:n]
            E_.append(envdist(a, o)); S_.append(specdist(a, o)); N_.append(null(a, o))
            row.append('%4.1f/%4.1f/%5.1f' % (E_[-1], S_[-1], N_[-1]))
        print('  %-7s ' % k + ' '.join(row), flush=True)
    print('FL mode %d, ours %d: envelope %.2f spectrum %.2f null %.2f dB' % (mode_fl, mode_ours, np.mean(E_), np.mean(S_), np.mean(N_)))


if __name__ == '__main__':
    if sys.argv[1] == 'map':
        slice_map()
    else:
        stretch(*(int(a) for a in sys.argv[2:4]))
