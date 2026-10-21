"""In-FL arpeggiator null cases. Parameters event (215): direction int @40
(0 off, 1 up, 2 down, 3 up/down, 4 up/down sticky, 5 random), range @44,
chord @48 (-1 = Auto (sustain), FL default), time int @52 (1024 = 1 step), gate int @56 (48 = 100%),
slide byte @60, repeat @92."""
import flrender as F

CHORD = [(0, 8, 60, 100), (0, 8, 64, 100), (0, 8, 67, 100)]

def arp(name, direction, rng=1, repeat=1, notes=CHORD, sample="sine3.wav", beats=8):
    fl = [F.params(40, '<i', direction), F.params(44, '<i', rng), F.params(92, '<i', repeat)]
    ours = {"arpDir": direction, "arpRange": rng, "arpRepeat": repeat}
    return dict(name=name, sample=sample, fl=fl, ours=ours, notes=notes, beats=beats)

CASES = [
    arp("a_up", 1), arp("a_down", 2), arp("a_updown", 3), arp("a_sticky", 4),
    arp("a_up_r2", 1, rng=2), arp("a_up_rep2", 1, repeat=2),
    arp("a_up_single", 1, notes=[(0, 8, 60, 100)]),
]

# TIME: position = (raw - 450) / 997; GATE: raw / 48
for raw in (520, 640, 832, 1100, 1300, 1440):
    c = arp(f"a_time{raw}", 1)
    c["fl"].append(F.params(52, '<i', raw)); c["ours"]["arpTime"] = (raw - 450) / 997
    CASES.append(c)
for g in (12, 24, 36):
    c = arp(f"a_gate{g}", 1)
    c["fl"].append(F.params(56, '<i', g)); c["ours"]["arpGate"] = g / 48
    CASES.append(c)

# CHORD @48: -2 Auto, -1 Auto (sustain), 0 (none), k = preset k-1 (our index = raw + 2)
for raw in (-2, 0, 1, 5, 10):
    c = arp(f"a_chord{raw}", 1, notes=[(0, 8, 60, 100)])
    c["fl"].append(F.params(48, '<i', raw)); c["ours"]["arpChord"] = raw + 2
    CASES.append(c)
c = arp("a_slide", 1); c["fl"].append(F.params(60, '<B', 1)); c["ours"]["arpSlide"] = 1; CASES.append(c)
