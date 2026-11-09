"""In-FL e3 pitch cases: FL's Sampler in Auto (= e3 generic once PITCH moves)
against Chan Sampler at -1200 ... +1200 cents. FL's own samples are read from
the FL Studio install (not copied here: they are Image-Line's); elastique is
proprietary, so these measure closeness (envelope / spectrum distance), not
nulls."""
import os, sys; from lib import flrender as F, nullfl, paths

PACKS = os.path.join(paths.FL_PATCHES, 'Packs')
SAMPLES = {
    "k707":   (os.path.join(PACKS, r"Drums\Kicks\707 Kick.wav"), 2),
    "rhodes": (os.path.join(PACKS, r"Instruments\Keyboard\Rhodes\Piano Rhodes (7).wav"), 4),
    "vox":    (os.path.join(PACKS, r"Vocals\Laurie Webb Ahh A.wav"), 4),
    "groove": (os.path.join(PACKS, r"Loops\DL Groover.wav"), 4),
    "loop":   ("signals/e3_loop.wav", 4),
    "noise":  ("signals/e3_noise.wav", 2),
}

def case(name, sample, cents, beats=2):
    fl = [F.params(108, '<i', 5), F.params(100, '<i', cents)]
    ours = {"stretchMode": 7, "pitch": cents}
    return dict(name=name, sample=sample, fl=fl, ours=ours, vel=100, beats=beats)

CASES = []
for key, (path, beats) in SAMPLES.items():
    for c in ((-700, 700) if key == "noise" else (-1200, -700, -300, 300, 700, 1200)):
        CASES.append(case(f"e3p_{key}_{c}", path, c, beats))

def source(key):
    """The sample as FL plays it at its own pitch (Resample, 0 cents): FL's
    decode, through the same channel, for analysis."""
    path, beats = SAMPLES[key]
    a, _ = nullfl.render_pair(f"e3p_{key}_src", path, [F.params(108, '<i', 0), F.params(100, '<i', 0)],
                              {"stretchMode": 0, "pitch": 0}, beats=beats)
    return a

if __name__ == '__main__':
    nullfl.run(CASES, [a for a in sys.argv[1:] if a != '-f'] or None, force='-f' in sys.argv)
