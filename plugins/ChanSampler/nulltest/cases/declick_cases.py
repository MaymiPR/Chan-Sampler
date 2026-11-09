"""In-FL measurement of the declick fades with a constant (DC) sample, so the
output is the fade curve itself: start offset (in-fade, end-of-sample fade)
and Cut itself (cut fade) for each declick mode (parameters event 215 @83)."""
from lib import flrender as F

CASES = []
for mode in range(6):
    CASES.append(dict(name=f"dkdc{mode}_so", sample="signals/dc.wav", fl=[F.params(83, '<B', mode), F.params(144, '<d', 0.25)],
                      ours={"declick": mode, "startOffset": 0.25}, beats=4))
    CASES.append(dict(name=f"dkdc{mode}_cut", sample="signals/dc.wav", fl=[F.params(83, '<B', mode), F.dword(132, 1 | (1 << 16))],
                      ours={"declick": mode, "cutItself": 1}, notes=[(0, 4, 60, 100), (1, 4, 60, 100)], beats=4, gap=26))

# The same with real audio (noise for the start offset, a sine for cuts) at velocity 100.
for mode in range(6):
    CASES.append(dict(name=f"v100_dk{mode}_so", sample="signals/noise.wav", fl=[F.params(83, '<B', mode), F.params(144, '<d', 0.25)],
                      ours={"declick": mode, "startOffset": 0.25}, beats=4))
    CASES.append(dict(name=f"v100_dk{mode}_cut", sample="signals/sine3.wav", fl=[F.params(83, '<B', mode), F.dword(132, 1 | (1 << 16))],
                      ours={"declick": mode, "cutItself": 1}, notes=[(0, 4, 60, 100), (1, 4, 60, 100)], beats=4, gap=26))
    # Noise separates the fading note from the new one (Transient's out-fade is a sweeping high-pass, not a gain).
    CASES.append(dict(name=f"v100_dk{mode}_cutn", sample="signals/noise.wav", fl=[F.params(83, '<B', mode), F.dword(132, 1 | (1 << 16))],
                      ours={"declick": mode, "cutItself": 1}, notes=[(0, 4, 60, 100), (1, 4, 60, 100)], beats=4, gap=26))
