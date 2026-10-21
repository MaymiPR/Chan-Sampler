"""In-FL null cases with FL knob positions.

Each case sets an FL Sampler knob by its raw FLP value and Chan Sampler's
parameter to the value its knob has at the same position (what pasting FL's
"Copy value" gives). Velocity 100 (the FL default); both play in one render."""
from lib import flrender as F

def envpos(raw): return max(0, raw - 100) / 65436

CASES = [dict(name="d_default", sample="signals/noise.wav")]

# Crossfade: position = raw / 1024 (FL CROSSFADE knob)
for raw in [64, 128, 256, 512, 1024]:
    CASES.append(dict(name=f"d_xf{raw}", sample="signals/noise.wav", fl=[F.params(84, '<I', raw)], ours={"pcCrossfade": raw / 1024}))

# Precomputed effects at a few positions
for raw in [64, 256]:
    CASES.append(dict(name=f"d_boost{raw}", sample="signals/sine_small.wav", fl=[F.word(74, raw)], ours={"pcBoost": raw / 256}))
for raw in [64, 192]:
    CASES.append(dict(name=f"d_eq{raw}", sample="signals/noise_q.wav", fl=[F.word(69, raw)], ours={"pcEq": (raw - 128) / 128}))
for raw in [256, 1024]:
    CASES.append(dict(name=f"d_fin{raw}", sample="signals/noise.wav", fl=[F.word(76, raw)], ours={"pcFadeIn": raw / 1024}))
CASES.append(dict(name="d_cut512", sample="signals/noise.wav", fl=[F.word(71, 512)], ours={"pcCut": 512 / 1024}))

# Levels: volume (multiplier) raw / 12800 (100% = 12800), pan (raw - 6400) / 6400
for raw in [3200, 6400, 19200]:
    # FUNC Levels VOL = event 229 offset 4 (raw / 12800, 100% = 12800); the
    # channel volume (219) is FL's own and applies to plugin channels as well.
    CASES.append(dict(name=f"d_vol{raw}", sample="signals/noise.wav", fl=[F.struct_field(229, 4, '<i', raw)], ours={"volume": raw / 12800}))
for raw in [0, 3200, 9600, 12800]:
    CASES.append(dict(name=f"d_pan{raw}", sample="signals/noise.wav", fl=[F.levels('pan', raw)], ours={"pan": (raw - 6400) / 6400}))

# Volume envelope with FL positions
def envcase(name, beats=8, sample="signals/dc20.wav", **kw):
    base = dict(env_on=1, delay=100, attack=20000, hold=20000, decay=30000, sustain=50, release=20000, att_tension=0, dec_tension=0, rel_tension=-101)
    base.update(kw)
    ours = {"volEnvOn": 1, "volDel": envpos(base["delay"]), "volAtt": envpos(base["attack"]), "volHold": envpos(base["hold"]),
            "volDec": envpos(base["decay"]), "volSus": base["sustain"] / 128, "volRel": envpos(base["release"]),
            "volAttT": base["att_tension"] / 128, "volDecT": base["dec_tension"] / 128, "volRelT": base["rel_tension"] / 128}
    return dict(name=name, sample=sample, fl=[F.env('vol', k, v) for k, v in base.items()], ours=ours, beats=beats)
CASES += [
    envcase("d_env_default"),
    envcase("d_env_att10000", attack=10000, hold=100, decay=100, sustain=128, release=100, rel_tension=0),
    envcase("d_env_rel20000", beats=1, attack=100, hold=100, decay=100, sustain=128, release=20000, rel_tension=0),
]
CASES += [
    envcase("d_env_rel_lin_sus64", beats=4, attack=100, hold=100, decay=100, sustain=64, release=20000, rel_tension=0),
    envcase("d_env_rel_t64_sus32", beats=4, attack=100, hold=100, decay=100, sustain=32, release=30000, rel_tension=64),
]
for raw in [5000, 10000, 40000]:
    CASES.append(envcase(f"d_env_hold{raw}", beats=12, attack=100, hold=raw, decay=100, sustain=0, release=100, rel_tension=0))
    CASES.append(envcase(f"d_env_del{raw}", beats=12, delay=raw, attack=100, hold=100, decay=100, sustain=128, release=100, rel_tension=0))

# Precomputed reverb: event 139 = type * 65536 + mix (0..256); our knob = mix / 256
for t, tn in ((0, "A"), (1, "B")):
    for mix in (32, 128, 256):
        for smp in ("signals/impulse.wav", "signals/decay.wav"):
            CASES.append(dict(name=f"d_rv{tn}{mix}_{smp.split('/')[-1][:-4]}", sample=smp, fl=[F.dword(139, t * 65536 + mix)],
                              ours={"pcReverbType": t, "pcReverb": mix / 256}))
    CASES.append(dict(name=f"d_rv{tn}200_noise", sample="signals/noise.wav", fl=[F.dword(139, t * 65536 + 200)],
                      ours={"pcReverbType": t, "pcReverb": 200 / 256}))
