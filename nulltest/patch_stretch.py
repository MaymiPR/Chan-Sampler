"""Source patch: time stretching, crossfade auto-loop and chord presets in SamplerEngine.cpp."""
import pathlib

p = pathlib.Path(__file__).resolve().parent.parent / "Source" / "SamplerEngine.cpp"
s = p.read_text(encoding="utf-8")

def rep(old, new):
    global s
    assert old in s, old[:80]
    s = s.replace(old, new, 1)

rep('#include "SamplerEngine.h"\n', '#include "SamplerEngine.h"\n#include "Chords.h"\n')

# Loop region: explicit loop points, or the crossfade auto-loop.
rep('''        const int ls = (int) (juce::jlimit (0.0f, 1.0f, p.loopStart) * s.playEnd);
        const int le = (int) (juce::jlimit (0.0f, 1.0f, p.loopEnd) * s.playEnd);
        return { ls, le, p.useLoop && le - ls >= 2 };''',
'''        if (! p.useLoop && s.autoLoop)
            return { s.autoLoopStart, s.playEnd, s.playEnd - s.autoLoopStart >= 2 };
        const int ls = (int) (juce::jlimit (0.0f, 1.0f, p.loopStart) * s.playEnd);
        const int le = (int) (juce::jlimit (0.0f, 1.0f, p.loopEnd) * s.playEnd);
        return { ls, le, p.useLoop && le - ls >= 2 };''')

rep('''    struct LoopFrames { int start, end; bool valid; };''',
'''    struct LoopFrames { int start, end; bool valid; };

    bool isGranular (StretchMode m) { return m != StretchMode::resample && m != StretchMode::sliceMap; }
    bool resetsAtOnsets (StretchMode m) { return m == StretchMode::sliceStretch || m == StretchMode::e2Transient; }

    // Grain length per method at 44.1 kHz: shorter grains for monophonic and
    // speech material, as the method names suggest.
    int grainLengthFor (StretchMode m, double sampleRate)
    {
        int n = 2048;
        switch (m)
        {
            case StretchMode::e3Mono: case StretchMode::e2Mono:              n = 1024; break;
            case StretchMode::e2Speech:                                      n = 768;  break;
            case StretchMode::sliceStretch: case StretchMode::e2Transient:   n = 1024; break;
            default: break;
        }
        return juce::jmax (64, (int) std::round (n * sampleRate / 44100.0)) & ~1;
    }

    float hann (int age, int len) { return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) age / (float) len); }''')

# Voice start: initialise grains.
rep('''    v.inLoop = loop.valid && v.pos < loop.end;
    v.targetNote = (float) note;''',
'''    v.inLoop = loop.valid && v.pos < loop.end;
    v.grainLen = grainLengthFor (p.stretchMode, sampleRate);
    v.grainPos[0] = v.grainPos[1] = v.pos;
    v.grainAge[0] = v.grainLen / 2;   // first grain starts at full weight so the attack is kept
    v.grainAge[1] = v.grainLen;       // empty slot: a grain spawns immediately
    v.spawnIn = 0;
    v.onsetIdx = 0;
    while (v.onsetIdx < (int) s.onsets.size() && s.onsets[(size_t) v.onsetIdx] <= (int) v.pos)
        ++v.onsetIdx;
    v.slicePos = v.pos;
    v.targetNote = (float) note;''')

# Rates: pitch increment and stretched time rate.
rep('''    v.increment = (s.sourceRate / sampleRate) * std::pow (2.0, semis / 12.0);''',
'''    v.increment = (s.sourceRate / sampleRate) * std::pow (2.0, semis / 12.0);

    // Time stretching: target duration from TIME (beats) or the sample's own
    // length, times MUL. Resample plays it like tape (pitch follows speed);
    // the other methods keep duration and pitch independent.
    const auto srcSeconds = (double) s.playEnd / s.sourceRate;
    const auto targetSeconds = (p.stretchTime > 0.01f ? (double) p.stretchTime * 60.0 / juce::jmax (1.0, p.bpm) : srcSeconds)
                             * (double) p.stretchMul;
    const auto speed = srcSeconds / juce::jmax (1.0e-6, targetSeconds);
    if (p.stretchMode == StretchMode::resample)
    {
        v.increment *= speed;
        v.timeRate = v.increment;
    }
    else
    {
        v.timeRate = (s.sourceRate / sampleRate) * speed;
    }''')

# Reading: granular / slice map branches.
rep('''                else
                {
                    const int i0 = at (idx - 1), i1 = at (idx), i2 = at (idx + 1), i3 = at (idx + 2);
                    l = hermite (srcL[i0], srcL[i1], srcL[i2], srcL[i3], t);
                    r = hermite (srcR[i0], srcR[i1], srcR[i2], srcR[i3], t);
                    v.lastL = l;
                    v.lastR = r;
                }''',
'''                else if (granular)
                {
                    // Two Hann grains, 50% overlap (weights sum to 1). Each
                    // reads at the pitch rate; new grains start at the play head.
                    if (v.spawnIn <= 0)
                    {
                        const int slot = v.grainAge[0] >= v.grainLen ? 0 : 1;
                        v.grainPos[slot] = v.pos;
                        v.grainAge[slot] = 0;
                        v.spawnIn = v.grainLen / 2;
                    }
                    --v.spawnIn;
                    l = r = 0.0f;
                    for (int gIdx = 0; gIdx < 2; ++gIdx)
                    {
                        if (v.grainAge[gIdx] >= v.grainLen)
                            continue;
                        const auto gp = v.grainPos[gIdx];
                        const int gi = (int) gp;
                        const auto gt = (float) (gp - gi);
                        const int a0 = at (gi - 1), a1 = at (gi), a2 = at (gi + 1), a3 = at (gi + 2);
                        const auto w = hann (v.grainAge[gIdx], v.grainLen);
                        l += w * hermite (srcL[a0], srcL[a1], srcL[a2], srcL[a3], gt);
                        r += w * hermite (srcR[a0], srcR[a1], srcR[a2], srcR[a3], gt);
                        v.grainPos[gIdx] += v.increment;
                        if (looping && ! p.pingPong && v.grainPos[gIdx] >= loop.end)
                            v.grainPos[gIdx] -= loop.end - loop.start;
                        ++v.grainAge[gIdx];
                    }
                    v.lastL = l;
                    v.lastR = r;
                }
                else if (sliceMap)
                {
                    // Slices keep their own speed and restart at their mapped time.
                    l = r = 0.0f;
                    if (v.slicePos >= 0.0)
                    {
                        const int nextOnset = v.onsetIdx < (int) s.onsets.size() ? s.onsets[(size_t) v.onsetIdx] : s.playEnd;
                        if (v.slicePos < nextOnset)
                        {
                            const int si = (int) v.slicePos;
                            const auto st = (float) (v.slicePos - si);
                            const int a0 = at (si - 1), a1 = at (si), a2 = at (si + 1), a3 = at (si + 2);
                            l = hermite (srcL[a0], srcL[a1], srcL[a2], srcL[a3], st);
                            r = hermite (srcR[a0], srcR[a1], srcR[a2], srcR[a3], st);
                            const int fadeLen = juce::jmax (1, (int) (0.002 * sampleRate));
                            if (v.sliceFade < fadeLen)
                            {
                                const auto fw = (float) ++v.sliceFade / (float) fadeLen;
                                l *= fw;
                                r *= fw;
                            }
                            v.slicePos += v.increment;
                        }
                    }
                    v.lastL = l;
                    v.lastR = r;
                }
                else
                {
                    const int i0 = at (idx - 1), i1 = at (idx), i2 = at (idx + 1), i3 = at (idx + 2);
                    l = hermite (srcL[i0], srcL[i1], srcL[i2], srcL[i3], t);
                    r = hermite (srcR[i0], srcR[i1], srcR[i2], srcR[i3], t);
                    v.lastL = l;
                    v.lastR = r;
                }''')

rep('''                // Advance the play position with loop handling.
                v.pos += v.increment * v.dir;''',
'''                // Advance the play position with loop handling.
                v.pos += v.timeRate * v.dir;

                // Slice modes restart at each transient the play head crosses.
                if ((sliceMap || slicesReset) && v.onsetIdx < (int) s.onsets.size()
                    && v.pos >= s.onsets[(size_t) v.onsetIdx])
                {
                    const auto onset = (double) s.onsets[(size_t) v.onsetIdx++];
                    if (sliceMap)
                    {
                        v.slicePos = onset;
                        v.sliceFade = 0;
                    }
                    else
                    {
                        v.grainPos[0] = onset;
                        v.grainAge[0] = v.grainLen / 2;
                        v.grainAge[1] = v.grainLen;
                        v.spawnIn = v.grainLen / 2;
                    }
                }''')

rep('''    const int endLimit = s.playEnd;
    juce::ignoreUnused (spec);''',
'''    const int endLimit = s.playEnd;
    juce::ignoreUnused (spec);
    const bool granular = isGranular (p.stretchMode);
    const bool sliceMap = p.stretchMode == StretchMode::sliceMap;
    const bool slicesReset = resetsAtOnsets (p.stretchMode);''')

# Loop wrap resets slice / onset tracking.
rep('''                        while (v.pos >= loop.end)
                            v.pos -= loop.end - loop.start;''',
'''                        while (v.pos >= loop.end)
                        {
                            v.pos -= loop.end - loop.start;
                            v.onsetIdx = 0;
                            while (v.onsetIdx < (int) s.onsets.size() && s.onsets[(size_t) v.onsetIdx] <= (int) v.pos)
                                ++v.onsetIdx;
                        }''')

# Arpeggiator chord presets and Auto (sustain).
rep('''        std::array<int, 128> chord {};
        int nChord = 0;
        if (p.arpChord == 2)
            chord[(size_t) nChord++] = held.front();          // (none): one note, octave steps
        else
            for (auto k : held)
                if (nChord < 128)
                    chord[(size_t) nChord++] = k;
        std::sort (chord.begin(), chord.begin() + nChord);''',
'''        std::array<int, 128> chord {};
        int nChord = 0;
        if (p.arpChord == 2)
        {
            chord[(size_t) nChord++] = held.front();          // (none): one note, octave steps
        }
        else if (p.arpChord >= 3 && p.arpChord - 3 < numChordPresets)
        {
            // Chord preset built on the lowest held key.
            int root = 127;
            for (auto k : held) root = juce::jmin (root, k);
            const auto& preset = chordPresets[p.arpChord - 3];
            chord[(size_t) nChord++] = root;
            int note = root;
            for (auto stepSize : preset.steps)
            {
                if (stepSize <= 0) break;
                note += stepSize;
                if (note <= 127) chord[(size_t) nChord++] = note;
            }
        }
        else
        {
            for (auto k : held)
                if (nChord < 128)
                    chord[(size_t) nChord++] = k;
        }
        std::sort (chord.begin(), chord.begin() + nChord);

        // Auto (sustain): a single held key with a one-octave range is not
        // arpeggiated; it plays once and sustains until released.
        if (p.arpChord == 1 && nChord == 1 && p.arpRange == 1)
        {
            if (arpIndex == 0)
            {
                playNoteOn (arpNextStep, chord[0], heldVel.empty() ? 1.0f : heldVel.back(), {}, p);
                sustainedNote = chord[0];
            }
            ++arpIndex;
            arpNextStep += juce::jmax ((int64_t) 1, (int64_t) std::round (step));
            continue;
        }''')

rep('''    if (held.empty())
        arpNextStep = -1;

    if (p.arpDir == 0)''',
'''    if (held.empty())
    {
        arpNextStep = -1;
        if (sustainedNote >= 0)
        {
            playNoteOff (time, sustainedNote, 0, p);
            sustainedNote = -1;
        }
    }

    if (p.arpDir == 0)''')

p.write_text(s, encoding="utf-8")
print("patched")
