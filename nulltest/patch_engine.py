"""One-off source patch: replaces SamplerEngine note handling with the
scheduler / arpeggiator / echo version. Kept for reference."""
import pathlib

path = pathlib.Path(__file__).resolve().parent.parent / "Source" / "SamplerEngine.cpp"
s = path.read_text(encoding="utf-8")
a = s.index("void SamplerEngine::prepare (double newSampleRate)")
b = s.index("void SamplerEngine::releaseVoice (Voice& v, const EngineParams& p)")
alloc_start = s.index("SamplerEngine::Voice& SamplerEngine::allocateVoice")
alloc_end = s.index("void SamplerEngine::noteOn (int note, float velocity, const SampleData& s, const EngineParams& p)")
allocate = s[alloc_start:alloc_end]

new = (pathlib.Path(__file__).resolve().parent / "engine_notes.cpp.txt").read_text(encoding="utf-8")
new = new.replace("//@@ALLOCATE@@\n", allocate)
path.write_text(s[:a] + new + s[b:], encoding="utf-8")
print("patched", path)
