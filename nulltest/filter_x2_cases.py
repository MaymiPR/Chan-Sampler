"""In-FL null cases for the 4th-order filters (FL Levels type 5 = LPx2, 7 = SVF LPx2).
SVF LPx2: two Chamberlin stages, q1 = 1 - 0.8 res, q2 = 1 - 0.6 res (bit-exact).
LPx2: two identical LP biquads plus a level compensation (Dsp.cpp flLpx2Gain)
that FL glides to from unity over the note's first tick."""
import sys, flrender as F, nullfl

CASES = []
for t in (5, 7):
    for smp, tag in (('noise.wav', 'n'), ('impulse.wav', 'i')):
        for mx, my in ((128, 0), (96, 160), (192, 64), (64, 224), (224, 128), (96, 256), (176, 200), (40, 180)):
            CASES.append(dict(name=f'x2c_t{t}_{tag}_{mx}_{my}', sample=smp, fl=[F.levels('type', t), F.levels('mod_x', mx), F.levels('mod_y', my)],
                              ours={'filterType': t, 'cutoff': mx / 256, 'resonance': my / 256}, beats=2))

if __name__ == '__main__':
    nullfl.run(CASES, [a for a in sys.argv[1:] if a != '-f'] or None, force='-f' in sys.argv)
