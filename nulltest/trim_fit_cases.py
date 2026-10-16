"""Extra in-FL trims of noise (SMP START / LENGTH), used to fit FL's trim-end
filter from real audio (the click-chain table nulls clicks but leaves -46 dB
on noise in the last ~40 frames)."""
import random, flrender as F
random.seed(7)
CASES = []
for k in range(70):
    st = round(random.uniform(0.0, 0.85), 4); ln = round(random.uniform(0.02, 0.6), 4)
    CASES.append(dict(name=f'trf_{k}', sample='noise.wav', fl=[F.params(128, '<d', st), F.params(136, '<d', ln)],
                      ours={'pcStart': st, 'pcLength': ln}, beats=2))
