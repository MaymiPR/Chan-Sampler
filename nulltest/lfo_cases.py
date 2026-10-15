"""In-FL null cases for the INS LFOs (event 218 fields lfo_*): FL raw values
next to Chan Sampler's knob positions (amount raw/128, speed (raw-200)/65336,
predelay / attack like the envelope times, shape index)."""
import sys, flrender as F, nullfl
from fl_cases import envpos

PREFIX = {'pan': 'pan', 'vol': 'vol', 'x': 'x', 'y': 'y', 'pitch': 'p'}

def lfo(name, target='vol', sample='dc6.wav', beats=8, amount=128, speed=32950, shape=0, delay=100, attack=100, extra_fl=(), extra_ours=None):
    p = PREFIX[target]
    fl = [F.env(target, 'lfo_amount', amount), F.env(target, 'lfo_speed', speed), F.env(target, 'lfo_shape', shape),
          F.env(target, 'lfo_delay', delay), F.env(target, 'lfo_attack', attack), *extra_fl]
    ours = {p + 'LfoAmt': amount / 128, p + 'LfoSpeed': (speed - 200) / 65336, p + 'LfoShape': shape,
            p + 'LfoDel': envpos(delay), p + 'LfoAtt': envpos(attack), **(extra_ours or {})}
    return dict(name=name, sample=sample, fl=fl, ours=ours, beats=beats)

CASES = [lfo('l_vol_a128'), lfo('l_vol_a64', amount=64), lfo('l_vol_am64', amount=-64),
         lfo('l_vol_tri', shape=1), lfo('l_vol_pulse', shape=2),
         lfo('l_vol_def_att', attack=20000), lfo('l_vol_del10000', delay=10000), lfo('l_vol_att40000', attack=40000)]
for sp in (6000, 12000, 20000, 28000, 40000, 50000, 65536):
    CASES.append(lfo(f'l_vol_sp{sp}', speed=sp))
CASES += [lfo('l_pan', 'pan', amount=96),
          lfo('l_pitch', 'pitch', sample='sine.wav', amount=64),
          lfo('l_modx', 'x', sample='noise.wav', amount=-128),
          lfo('l_mody', 'y', sample='noise.wav', amount=128)]

# Envelope amounts on the other targets (the volume envelope is in fl_cases).
def envamt(name, target, sample, amount):
    p = PREFIX[target]
    raw = dict(env_on=1, amount=amount, delay=100, attack=10000, hold=100, decay=20000, sustain=64, release=100)
    ours = {p + 'EnvOn': 1, p + 'EnvAmt': amount / 128, p + 'Del': envpos(100), p + 'Att': envpos(10000), p + 'Hold': envpos(100),
            p + 'Dec': envpos(20000), p + 'Sus': 64 / 128, p + 'Rel': envpos(100)}
    return dict(name=name, sample=sample, fl=[F.env(target, k, v) for k, v in raw.items()], ours=ours, beats=8)
CASES += [envamt('e_pitch64', 'pitch', 'sine.wav', 64), envamt('e_modx-128', 'x', 'noise.wav', -128)]   # FL has no panning envelope

if __name__ == '__main__':
    nullfl.run(CASES, [a for a in sys.argv[1:] if a != '-f'] or None, force='-f' in sys.argv)
