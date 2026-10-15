import flrender as F
# Velocity 128 maps exactly to MIDI 127 (FL 100 would be rounded to MIDI 99),
# so cases use it unless they test velocity. Its gain is 2.0527; the 0.25 sine
# and the noise stay below full scale.
CASES = [
    dict(name="v128_base", sample="noise.wav", vel=128),
]
for b in [16, 64, 128, 256]:
    CASES.append(dict(name=f"v128_boost{b}", sample="sine_small.wav", vel=128, fl=[F.word(74, b)], ours={"pcBoost": b / 256}))
CASES.append(dict(name="v128_boost64clip", sample="sine.wav", vel=128, fl=[F.word(74, 64), F.word(70, 4)], ours={"pcBoost": 0.25, "pcBoostClip": 1}))
for t in [0, 64, 192, 256]:
    CASES.append(dict(name=f"v128_eq{t}", sample="noise_q.wav", vel=128, fl=[F.word(69, t)], ours={"pcEq": (t - 128) / 128}))

import numpy as np
def ln(f): return min(max(0, int(4096 * (np.log2(f) + 15))), 61440)
V = dict(vel=128)
for raw in [64, 256, 1024]:
    CASES.append(dict(name=f"v128_fin{raw}", sample="noise.wav", fl=[F.word(76, raw)], ours={"pcFadeIn": raw / 1024}, **V))
    CASES.append(dict(name=f"v128_fout{raw}", sample="noise.wav", fl=[F.word(75, raw)], ours={"pcFadeOut": raw / 1024}, **V))
CASES += [
    dict(name="v128_start25", sample="noise.wav", fl=[F.params(133, '<H', ln(.25)), F.params(135, '<H', 63)], ours={"pcStart": 0.25}, **V),
    dict(name="v128_len50", sample="noise.wav", fl=[F.params(141, '<H', ln(.5)), F.params(143, '<H', 63)], ours={"pcLength": 0.5}, **V),
    dict(name="v128_norm", sample="noise_q.wav", fl=[F.params(80, '<B', 1)], ours={"pcNormalize": 1}, **V),
    dict(name="v128_rev", sample="noise.wav", fl=[F.word(70, 2)], ours={"pcReverse": 1}, **V),
    dict(name="v128_inv", sample="noise.wav", fl=[F.params(81, '<B', 1)], ours={"pcReversePolarity": 1}, **V),
    dict(name="v128_swap", sample="noise.wav", fl=[F.word(70, 256)], ours={"pcSwapStereo": 1}, **V),
    dict(name="v128_fadest", sample="noise.wav", fl=[F.word(70, 1)], ours={"pcFadeStereo": 1}, **V),
    dict(name="v128_remdc", sample="dcsine.wav", fl=[F.params(9, '<B', 1)], ours={"pcRemoveDC": 1}, **V),
]
for raw in [16, 64, 256]:
    CASES.append(dict(name=f"v128_trim{raw}", sample="decay.wav", fl=[F.params(88, '<I', raw)], ours={"pcTrim": raw / 256}, **V))
for mix, fq in [(256, 128), (128, 64), (256, 0)]:
    CASES.append(dict(name=f"v128_rm{mix}_{fq}", sample="noise_q.wav", fl=[F.dword(131, mix | (fq << 16))], ours={"pcRmMix": mix / 256, "pcRmFreq": fq / 256}, **V))
for cut, res in [(512, 0), (768, 0), (256, 0), (640, 320), (640, 640)]:
    CASES.append(dict(name=f"v128L_pccut{cut}_{res}", sample="noise.wav", fl=[F.word(71, cut), F.word(83, res)], ours={"pcCut": cut / 1024, "pcRes": res / 640}, **V))
for sd in [0, 1024, 3072, 4096]:
    CASES.append(dict(name=f"v128_sd{sd}", sample="noise.wav", fl=[F.word(85, sd)], ours={"pcStereoDelay": (sd - 2048) / 2048}, **V))
for pg in [0, 128, 384, 512]:
    CASES.append(dict(name=f"v128_pogo{pg}", sample="noise_q.wav", fl=[F.word(86, pg)], ours={"pcPogo": (pg - 256) / 256}, **V))

# Engine: start offset, declick modes, cut itself
for so in [0.1, 0.5]:
    CASES.append(dict(name=f"v128_so{so}", sample="noise.wav", fl=[F.params(144, '<d', so)], ours={"startOffset": so}, **V))
for mode in range(6):
    CASES.append(dict(name=f"v128_dk{mode}_so", sample="noise.wav", fl=[F.params(83, '<B', mode), F.params(144, '<d', 0.25)], ours={"declick": mode, "startOffset": 0.25}, **V))
    CASES.append(dict(name=f"v128_dk{mode}_cut", sample="sine3.wav", fl=[F.params(83, '<B', mode), F.dword(132, 1 | (1 << 16))],
                      ours={"declick": mode, "cutItself": 1}, notes=[(0, 4, 60, 128), (1, 4, 60, 128)], **V))

# Volume envelope (event 218, target "vol"); ours uses knob = raw/65536, sustain raw/128, tension raw/128
def envcase(name, beats=8, sample="dc20.wav", **kw):
    base = dict(env_on=1, delay=100, attack=20000, hold=20000, decay=30000, sustain=50, release=20000, att_tension=0, dec_tension=0, rel_tension=-101)
    base.update(kw)
    ours = {"volEnvOn": 1, "volDel": base["delay"] / 65536 if base["delay"] > 100 else 0, "volAtt": base["attack"] / 65536,
            "volHold": base["hold"] / 65536, "volDec": base["decay"] / 65536, "volSus": base["sustain"] / 128,
            "volRel": base["release"] / 65536, "volAttT": base["att_tension"] / 128, "volDecT": base["dec_tension"] / 128,
            "volRelT": base["rel_tension"] / 128}
    return dict(name=name, sample=sample, fl=[F.env('vol', k, v) for k, v in base.items()], ours=ours, beats=beats, vel=128)
CASES += [
    envcase("v128_env_default"),
    envcase("v128_env_att10000", attack=10000, hold=100, decay=100, sustain=128, release=100, rel_tension=0),
    envcase("v128_env_dec20000", attack=100, hold=100, decay=20000, sustain=64, release=100, rel_tension=0),
    envcase("v128_env_rel20000", beats=1, attack=100, hold=100, decay=100, sustain=128, release=20000, rel_tension=0),
    envcase("v128_env_tension", beats=4, attack=20000, att_tension=-96, decay=20000, dec_tension=64, sustain=40, release=15000, rel_tension=-128),
]

# Channel filter (Levels event): mod_x 0..256 -> cutoff, mod_y 0..256 -> resonance
for typ in range(8):
    for mx, my in [(128, 0), (96, 160)]:
        CASES.append(dict(name=f"v128_flt{typ}_{mx}_{my}", sample="noise.wav",
                          fl=[F.levels('mod_x', mx), F.levels('mod_y', my), F.levels('type', typ)],
                          ours={"cutoff": mx / 256, "resonance": my / 256, "filterType": typ}, **V))

# Crossfade without loop points (FL turns the sample into a crossfaded loop); start + length interplay
for raw in [64, 128, 256]:
    CASES.append(dict(name=f"v128_xf{raw}_noloop", sample="noise.wav", fl=[F.params(84, '<I', raw)], ours={"pcCrossfade": raw / 256}, **V))
CASES.append(dict(name="v128_st25_len50", sample="noise.wav", fl=[F.params(128, '<d', 0.25), F.params(136, '<d', 0.5)], ours={"pcStart": 0.25, "pcLength": 0.5}, **V))
