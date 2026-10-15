"""In-FL sweep of the precomputed effects over their whole range.

Raw FLP values per knob (position = raw / range, as FL's "Copy value"):
boost w74 0..256, clip = flag 4 in w70, EQ w69 0..256 (128 = centre),
pogo w86 0..512 (256 = centre), ring mod d131 = mix | freq << 16 (0..256 each),
cutoff w71 16..1024, resonance w83 0..640."""
import flrender as F

def sweep(sample="noise_q.wav"):
    C = []
    for raw in [8, 32, 96, 160, 224, 256]:
        C.append(dict(name=f"s_boost{raw}", sample=sample, fl=[F.word(74, raw)], ours={"pcBoost": raw / 256}))
        C.append(dict(name=f"s_boostclip{raw}", sample=sample, fl=[F.word(74, raw), F.word(70, 4)], ours={"pcBoost": raw / 256, "pcBoostClip": 1}))
    for raw in [0, 16, 48, 100, 156, 208, 240, 256]:
        C.append(dict(name=f"s_eq{raw}", sample=sample, fl=[F.word(69, raw)], ours={"pcEq": (raw - 128) / 128}))
    for raw in [0, 64, 192, 240, 272, 320, 448, 512]:
        C.append(dict(name=f"s_pogo{raw}", sample=sample, fl=[F.word(86, raw)], ours={"pcPogo": (raw - 256) / 256}))
    for mix, fq in [(32, 128), (128, 0), (128, 16), (128, 200), (256, 256), (200, 77)]:
        C.append(dict(name=f"s_rm{mix}_{fq}", sample=sample, fl=[F.dword(131, mix | (fq << 16))], ours={"pcRmMix": mix / 256, "pcRmFreq": fq / 256}))
    for cut, res in [(1024, 64), (1024, 320), (1024, 640), (700, 100), (300, 500), (128, 640), (900, 600)]:
        C.append(dict(name=f"s_res{cut}_{res}", sample=sample, fl=[F.word(71, cut), F.word(83, res)], ours={"pcCut": cut / 1024, "pcRes": res / 640}))
    return C

CASES = sweep()
