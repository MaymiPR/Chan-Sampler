"""In-FL sweep of SMP START / LENGTH (FL 2026: doubles at params+128 / +136)
with and without Reverse (flag 2 in event 70)."""
import flrender as F

def sweep(sample="noise.wav"):
    C = []
    for st in (0.0, 0.1, 0.25, 0.5, 0.8):
        for ln in (0.0, 0.05, 0.3, 0.75, 1.0):
            for rev in (0, 1):
                if st == 0.0 and ln == 1.0 and not rev:
                    continue
                fl = [F.params(128, '<d', st), F.params(136, '<d', ln)] + ([F.word(70, 2)] if rev else [])
                ours = {"pcStart": st, "pcLength": ln, "pcReverse": rev}
                C.append(dict(name=f"tr_s{st}_l{ln}_r{rev}", sample=sample, fl=fl, ours=ours))
    return C

CASES = sweep()
