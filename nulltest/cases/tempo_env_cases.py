"""In-FL volume envelope cases at tempos other than 130 BPM (the harness default),
with the envelope's Tempo switch off (flags 4, FL's default) and on (flags 5),
and positive decay tension."""
from lib import flrender as F
from cases.fl_cases import envcase

CASES = []
SHAPES = dict(
    att=dict(attack=10000, hold=100, decay=100, sustain=128, release=100, rel_tension=0),
    dec=dict(attack=100, hold=100, decay=20000, sustain=64, release=100, rel_tension=0),
    hold=dict(attack=100, hold=10000, decay=100, sustain=0, release=100, rel_tension=0),
    rel=dict(attack=100, hold=100, decay=100, sustain=128, release=20000, rel_tension=0),
    default=dict(),
)
for bpm in (65, 100, 160):
    for tempo in (0, 1):
        for sname, kw in SHAPES.items():
            c = envcase(f"te_{sname}_{bpm}_t{tempo}", beats=2 if sname == "rel" else 4, **kw)
            c["fl"].append(F.env('vol', 'flags', 4 | tempo))
            c["ours"]["volEnvTempo"] = tempo
            c["bpm"] = bpm
            c["gap"] = 26
            CASES.append(c)
for t in (32, 64, 128):
    c = envcase(f"te_dect{t}", beats=4, attack=100, hold=100, decay=20000, dec_tension=t, sustain=32, release=100, rel_tension=0)
    c["gap"] = 26
    CASES.append(c)
