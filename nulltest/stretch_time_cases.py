"""In-FL null cases for TIME (event 215 @96, float ticks at 192 per beat) and
MUL / PITCH together in each stretch mode. Resample is tape-style and must
null; FL's Auto with anything stretched is exactly FL's "e3 generic"
(elastique), which cannot."""
import sys, flrender as F, nullfl

MODES = {0: 0, 5: 7, 3: 1, -1: 1, -2: 2}   # FL raw -> our index (Auto -> 7)
OUR = {0: 0, 5: 7, 1: 3, -1: 1, -2: 2}

def case(name, mode, ticks=0.0, mulraw=0, cents=0, sample="loud.wav", beats=8):
    fl = [F.params(108, '<i', mode), F.params(96, '<f', ticks), F.params(104, '<i', mulraw), F.params(100, '<i', cents)]
    ours = {"stretchMode": OUR[mode], "stretchTime": ticks / 192.0, "stretchMul": 2 ** (mulraw / 10000), "pitch": cents}
    return dict(name=name, sample=sample, fl=fl, ours=ours, vel=100, beats=beats)

CASES = []
for ticks in (768.0, 384.0, 1000.0, 1536.0):
    CASES.append(case(f"st_res_t{int(ticks)}", 0, ticks))
CASES.append(case("st_res_t768_mul5000", 0, 768.0, mulraw=5000))
CASES.append(case("st_res_t768_noise", 0, 768.0, sample="noise.wav"))
CASES.append(case("st_auto_none", 5))
CASES.append(case("st_auto_t768", 5, 768.0))
CASES.append(case("st_auto_p600", 5, cents=600))

if __name__ == '__main__':
    nullfl.run(CASES, sys.argv[1:] or None, force='-f' in sys.argv)
