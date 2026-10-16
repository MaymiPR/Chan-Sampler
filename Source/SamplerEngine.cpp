#include "SamplerEngine.h"
#include "Chords.h"

namespace
{
    constexpr int controlBlock = 32;

    bool pitchLfoPerSample (const EngineParams& p)
    {
        return p.pitchT.lfo.amount != 0.0f && p.stretchMode == StretchMode::resample;
    }

    // FL's sample interpolator, measured from impulses played at many pitches
    // (nulltest/kernel_extract.py, window_fit.py): a 30-tap windowed sinc,
    // k(d) = c sinc(c d) w(d) for |d| < 15 input samples, with c = min(1, 1/ratio)
    // (the cutoff follows the playback ratio; the window stays 15 samples wide)
    // and w a 7-term cosine series. It reproduces FL's renders to < 1 LSB.
    struct FlInterpolator
    {
        static constexpr int halfTaps = 15, steps = 2048;
        std::vector<float> window, kernel;   // indexed by |d| * steps

        FlInterpolator()
        {
            static constexpr double a[] = { 0.362684569, 0.509910674, 0.132162708, -0.006084749,
                                            0.001926633, -0.000879890, 0.000296751 };
            const int n = halfTaps * steps + 2;
            window.resize ((size_t) n);
            kernel.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double d = (double) i / steps;
                double w = 0.0;
                if (d < halfTaps)
                    for (int m = 0; m < 7; ++m)
                        w += a[m] * std::cos (juce::MathConstants<double>::pi * m * d / halfTaps);
                window[(size_t) i] = (float) w;
                const double sinc = d == 0.0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * d) / (juce::MathConstants<double>::pi * d);
                kernel[(size_t) i] = (float) (sinc * w);
            }
        }

        static float lookup (const std::vector<float>& t, double d)
        {
            const auto x = std::abs (d) * steps;
            const auto i = (int) x;
            if (i >= halfTaps * steps)
                return 0.0f;
            const auto f = (float) (x - i);
            return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * f;
        }

        // Reads both channels at fractional position pos; `at` maps an index
        // into the buffer (loop wrap / clamping), negative indices are silent.
        template <typename At>
        void read (const float* l, const float* r, double pos, double ratio, At&& at, float& outL, float& outR) const
        {
            const int base = (int) std::floor (pos);
            const double frac = pos - base;
            if (frac == 0.0 && ratio <= 1.0)
            {
                const int k = base < 0 ? -1 : at (base);
                outL = k < 0 ? 0.0f : l[k];
                outR = k < 0 ? 0.0f : r[k];
                return;
            }
            const double c = ratio > 1.0 ? 1.0 / ratio : 1.0;
            double sl = 0.0, sr = 0.0;
            for (int j = -halfTaps + 1; j <= halfTaps; ++j)
            {
                const int idx = base + j;
                if (idx < 0)
                    continue;
                const double d = pos - idx;
                double k;
                if (c == 1.0)
                    k = lookup (kernel, d);
                else
                {
                    const double x = juce::MathConstants<double>::pi * c * d;
                    k = (x == 0.0 ? c : std::sin (x) / (juce::MathConstants<double>::pi * d)) * lookup (window, d);
                }
                if (k == 0.0)
                    continue;
                const int m = at (idx);
                sl += k * l[m];
                sr += k * r[m];
            }
            outL = (float) sl;
            outR = (float) sr;
        }
    };

    const FlInterpolator& flInterpolator()
    {
        static const FlInterpolator interp;
        return interp;
    }

    // FL's note velocity curve, measured by null test: gain = (11^(v/100) - 1) / 10
    // (unity at 100). FL's plugin wrapper sends a note's velocity unchanged as
    // the MIDI velocity (measured), so v = MIDI velocity.
    float velocityGain (float midiVelocity01)
    {
        const auto v = std::round (midiVelocity01 * 127.0f);
        return (std::pow (11.0f, v / 100.0f) - 1.0f) / 10.0f;
    }

    // Echo Mod X / Y (measured in FL): echo k shifts the value by
    // 1 - (1 - |m|)^k towards the knob's side (Mod X 50%: +0.5, +0.75, ...).
    float echoShift (float value, float m, int echo)
    {
        if (echo > 0 && m != 0.0f)
            return juce::jlimit (0.0f, 1.0f, value + std::copysign (1.0f - std::pow (1.0f - std::abs (m), (float) echo), m));
        return value;   // the filter clamps (the biquads allow modulation past the knob's end)
    }

    struct LoopFrames { int start, end; bool valid; };

    bool isGranular (StretchMode m) { return m != StretchMode::resample && m != StretchMode::sliceMap; }
    bool resetsAtOnsets (StretchMode m) { return m == StretchMode::sliceStretch || m == StretchMode::e2Transient; }


    LoopFrames loopFrames (const SampleData& s, const EngineParams& p)
    {
        // The precomputed data says when the crossfade loop applies (no loop set).
        if (s.autoLoop)
            return { s.autoLoopStart, s.playEnd, s.playEnd - s.autoLoopStart >= 2 };
        // FL: END = 0 (the default) means no loop; END never sits before START.
        // The END frame is inclusive in FL: a forward loop plays it and then
        // jumps to START (measured: one frame per pass otherwise); ping pong
        // turns at END itself.
        const int ls = (int) std::lround (juce::jlimit (0.0f, 1.0f, p.loopStart) * s.playEnd);
        int le = (int) std::lround (juce::jlimit (0.0f, 1.0f, juce::jmax (p.loopStart, p.loopEnd)) * s.playEnd);
        if (! p.pingPong)
            le = juce::jmin (s.playEnd, le + 1);
        const int shift = p.pingPong ? 0 : s.loopStartShift;   // the crossfade's loop restart
        return { juce::jmin (le - 1, ls + shift), le, p.useLoop && le - ls >= 2 };
    }
}

SamplerEngine::DeclickSpec SamplerEngine::declickSpec (DeclickMode m)
{
    // Measured in FL (cosine S-curves): the in-fade applies only when a note
    // starts after the sample's first frame (start offset); the out-fade
    // applies at the sample end (holding the last value) and when a note is
    // cut. Transient (no bleeding)'s out-fade and both Crossfade fades have
    // their own measured shapes (DeclickCurves.h); their times here are unused.
    switch (m)
    {
        case DeclickMode::outOnly:        return { 0,   false, 10,  false };
        case DeclickMode::transient:      return { 4,   true,  10,  false };
        case DeclickMode::transientBleed: return { 4,   true,  20,  true  };
        case DeclickMode::generic:        return { 20,  false, 20,  true  };
        case DeclickMode::smooth:         return { 100, false, 100, true  };
        case DeclickMode::crossfade:      return { 200, false, 200, true  };
    }
    return { 0, false, 10, false };
}

void SamplerEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& v : voices)
        v = Voice();
    pending.clear();
    pending.reserve (maxPending);
    noteStarts.reserve (128);
    held.clear();
    held.reserve (128);
    heldVel.clear();
    heldVel.reserve (128);
    arpNextStep = -1;
    arpIndex = 0;
    now = 0;
}

void SamplerEngine::updatePlayhead (const SampleData* sample) noexcept
{
    const Voice* newest = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.holding && v.mods.preview && (newest == nullptr || v.age > newest->age))
            newest = &v;
    const int total = sample != nullptr ? sample->getTotalFrames() : 0;
    playhead.store (newest != nullptr && total > 0 ? (float) juce::jlimit (0.0, 1.0, newest->pos / total) : -1.0f);
}

int SamplerEngine::getNumActiveVoices() const noexcept
{
    int n = 0;
    for (auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

void SamplerEngine::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi,
                            const SampleData* sample, const EngineParams& p, const std::vector<int>* cuts)
{
    noteStarts.clear();
    size_t nextCut = 0;
    // A new or rebuilt sample invalidates every playing position. The manual
    // notes that editing precomputed settings during playback glitches.
    if (sample != currentSample)
    {
        for (auto& v : voices)
            v.active = false;
        currentSample = sample;
    }

    const bool playable = sample != nullptr && sample->playEnd > 1;
    const int n = out.getNumSamples();
    auto it = midi.begin();
    int pos = 0;

    for (;;)
    {
        blockPos = pos;
        const auto serialBefore = eventSerial;

        // Cut by another channel's group: before this position's own notes.
        while (cuts != nullptr && nextCut < cuts->size() && (*cuts)[nextCut] <= pos)
        {
            cutByGroup (p);
            ++nextCut;
        }

        // Host events at this position
        while (it != midi.end() && (*it).samplePosition <= pos)
        {
            const auto msg = (*it).getMessage();
            if (msg.isNoteOn())
                handleNoteOn (now + pos, msg.getNoteNumber(), msg.getFloatVelocity(), p);
            else if (msg.isNoteOff())
                handleNoteOff (now + pos, msg.getNoteNumber(), p);
            else if (msg.isPitchWheel())
                bend = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f;
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                // FL sends this just before a note-off (measured): arpeggio
                // notes end as at key release, other notes release.
                held.clear();
                heldVel.clear();
                arpNextStep = -1;
                pending.clear();
                sustainedNote = -1;
                for (auto& v : voices)
                    if (v.active && ! v.released)
                    {
                        releaseVoice (v, p);
                        if (v.mods.arp && ! p.volT.envOn)
                            startKill (v, p);
                    }
            }
            ++it;
        }

        // Arpeggio steps due by now, then every scheduled note that is due
        // (note-offs before note-ons at the same time).
        advanceArp (now + pos, p);
        for (;;)
        {
            int best = -1;
            for (int i = 0; i < (int) pending.size(); ++i)
            {
                const auto& c = pending[(size_t) i];
                if (c.time > now + pos)
                    continue;
                if (best < 0 || c.time < pending[(size_t) best].time
                    || (c.time == pending[(size_t) best].time && ! c.on && pending[(size_t) best].on))
                    best = i;
            }
            if (best < 0)
                break;

            const auto e = pending[(size_t) best];
            pending[(size_t) best] = pending.back();
            pending.pop_back();

            if (e.on)
            {
                if (playable)
                    noteOn (e.note, e.velocity, e.mods, *sample, p);
            }
            else if (e.mods.arp)
            {
                endArpNote (e.note, p, false);
            }
            else if (e.mods.echo == 0)
            {
                playNoteOff (now + pos, e.note, 0, p);
            }
            else
            {
                noteOff (e.note, e.mods.echo, p);
            }
        }

        // Anything that changed a voice here (note on / off, legato pitch,
        // cut) starts fresh control blocks, so the change applies on this
        // sample as in FL; otherwise blocks run from each note's start.
        if (eventSerial != serialBefore && playable)
            for (auto& v : voices)
                if (v.active && v.ctlLeft > 0)
                    refreshControls (v, *sample, p);

        if (pos >= n)
            break;

        // Render up to the next event
        int64_t next = n;
        if (it != midi.end())
            next = juce::jmin (next, (int64_t) juce::jmax (pos + 1, (*it).samplePosition));
        for (auto& e : pending)
            next = juce::jmin (next, juce::jmax ((int64_t) pos + 1, e.time - now));
        if (p.arpDir != 0 && arpNextStep >= 0 && ! held.empty())
            next = juce::jmin (next, juce::jmax ((int64_t) pos + 1, arpNextStep - now));
        if (cuts != nullptr && nextCut < cuts->size())
            next = juce::jmin (next, (int64_t) juce::jmax (pos + 1, (*cuts)[nextCut]));

        if (playable)
            renderRange (out, pos, (int) next - pos, *sample, p);
        pos = (int) next;
    }

    now += n;
}

void SamplerEngine::schedule (const Scheduled& e)
{
    if (pending.size() < maxPending)
        pending.push_back (e);
}

void SamplerEngine::previewNote (int note, bool on, const EngineParams& p)
{
    if (on)
    {
        NoteMods m;
        m.preview = true;
        playNoteOn (now, note, 100.0f / 127.0f, m, p);   // FL previews at its default velocity 100
    }
    else
    {
        for (auto& v : voices)
            if (v.active && ! v.released && v.mods.preview)
                releaseVoice (v, p);
    }
}

void SamplerEngine::handleNoteOn (int64_t time, int note, float velocity, const EngineParams& p)
{
    if (p.arpDir != 0)
    {
        // Held keys feed the arpeggiator instead of sounding directly.
        if (std::find (held.begin(), held.end(), note) == held.end() && held.size() < 128)
        {
            held.push_back (note);
            heldVel.push_back (velocity);
        }
        if (arpNextStep < 0)
        {
            arpNextStep = time;
            arpStart = time;
            arpSteps = 0;
            arpIndex = 0;
        }
        return;
    }
    playNoteOn (time, note, velocity, {}, p);
}

// Measured in FL: an arpeggio note ends when its step (gate) or the key ends
// (one note at a time), unlike a normal note-off, which lets the sample play
// on when the volume envelope is off.
void SamplerEngine::endArpNote (int note, const EngineParams& p, bool keyReleased)
{
    // With slide and a full gate the next step takes over this note (legato).
    if (! keyReleased && p.arpSlide && p.arpGate >= 0.999f && ! held.empty())
        return;
    for (auto& v : voices)
        if (v.active && ! v.released && v.note == note && v.mods.arp)
        {
            releaseVoice (v, p);
            if (! p.volT.envOn)
                startKill (v, p);
        }
}

void SamplerEngine::handleNoteOff (int64_t time, int note, const EngineParams& p)
{
    for (size_t i = 0; i < held.size(); ++i)
        if (held[i] == note)
        {
            held.erase (held.begin() + (std::ptrdiff_t) i);
            heldVel.erase (heldVel.begin() + (std::ptrdiff_t) i);
            break;
        }
    if (held.empty())
    {
        arpNextStep = -1;
        // Every sounding arpeggio note stops at key release (measured in FL).
        for (auto& v : voices)
            if (v.active && ! v.released && v.mods.arp)
                endArpNote (v.note, p, true);
        sustainedNote = -1;
    }

    if (p.arpDir == 0)
        playNoteOff (time, note, 0, p);
}

void SamplerEngine::advanceArp (int64_t until, const EngineParams& p)
{
    if (p.arpDir == 0 || held.empty() || arpNextStep < 0)
        return;

    // Measured in FL: step k starts at round (k * step) samples after the
    // first (not a rounded step added up). TIME "Hold" is approximated as one
    // note per bar.
    const auto step = samplesPerStep (p) * (p.arpTime > 0.0f ? (double) p.arpTime : 16.0);
    auto advance = [this, step]
    {
        ++arpSteps;
        arpNextStep = juce::jmax (arpNextStep + 1, arpStart + (int64_t) std::llround ((double) arpSteps * step));
    };
    while (arpNextStep <= until)
    {
        // Sequence: chord notes ascending, repeated per octave over the range.
        std::array<int, 128> chord {};
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

        // Auto (sustain), FL's default: a single held key with a one-octave
        // range is not arpeggiated; it plays once and sustains until released
        // (measured in FL).
        if (p.arpChord == 1 && nChord == 1 && p.arpRange == 1)
        {
            // Measured in FL: once the sample has run out, the held key starts
            // again on the next step.
            bool sounding = false;
            for (auto& v : voices)
                sounding = sounding || (v.active && v.note == chord[0]);
            if (arpIndex == 0 || ! sounding)
            {
                NoteMods held1;
                held1.arp = true;
                playNoteOn (arpNextStep, chord[0], heldVel.empty() ? 1.0f : heldVel.back(), held1, p);
                sustainedNote = chord[0];
            }
            ++arpIndex;
            advance();
            continue;
        }

        std::array<int, 512> up {};
        int nUp = 0;
        for (int o = 0; o < p.arpRange; ++o)
            for (int r = 0; r < juce::jmax (1, p.arpRepeat); ++r)
                for (int c = 0; c < nChord && nUp < 512; ++c)
                    up[(size_t) nUp++] = juce::jlimit (0, 127, chord[(size_t) c] + 12 * o);

        int note = up[0];
        if (nUp > 0)
        {
            switch (p.arpDir)
            {
                case 1: note = up[(size_t) (arpIndex % nUp)]; break;
                case 2: note = up[(size_t) (nUp - 1 - arpIndex % nUp)]; break;
                case 3:   // up / down, ends played once
                {
                    const int period = juce::jmax (1, 2 * nUp - 2);
                    const int i = arpIndex % period;
                    note = up[(size_t) (i < nUp ? i : period - i)];
                    break;
                }
                case 4:   // up / down sticky: lowest and highest played twice
                {
                    const int period = 2 * nUp;
                    const int i = arpIndex % period;
                    note = up[(size_t) (i < nUp ? i : period - 1 - i)];
                    break;
                }
                default: note = up[(size_t) arpRandom.nextInt (nUp)]; break;
            }
        }

        NoteMods mods;
        mods.slide = p.arpSlide;
        mods.arp = true;
        mods.legatoOnly = p.arpSlide && arpIndex > 0;
        playNoteOn (arpNextStep, note, heldVel.empty() ? 1.0f : heldVel.back(), mods, p);
        NoteMods offMods;
        offMods.arp = true;
        // The gate ends on the same absolute grid as the steps, so a full gate
        // ends exactly where the next step starts.
        const auto offTime = arpStart + (int64_t) std::llround (((double) arpSteps + juce::jlimit (0.05, 1.0, (double) p.arpGate)) * step);
        schedule ({ juce::jmax (arpNextStep + 1, offTime), false, note, 0.0f, offMods });

        ++arpIndex;
        advance();
    }
}

void SamplerEngine::playNoteOn (int64_t time, int note, float velocity, NoteMods mods, const EngineParams& p)
{
    if (mods.echo != 0 || p.echoFeed <= 0.0f)
    {
        schedule ({ time, true, note, velocity, mods });
        return;
    }

    // Echoes: each is a later voice with accumulated level, pan, Mod X/Y and
    // pitch offsets (FEED centre = same level as the original).
    // Fat mode (measured in FL): every echo gets the same level
    // 2 G / (3 count + 2) and the original 1.5 - count * that; pan only lowers
    // an echo's level on both sides, by cos (pi/4 min (1, k |pan|)), and ping
    // pong does nothing.
    const auto feedGain = 2.0f * p.echoFeed;
    const auto fatGain = 2.0f * feedGain / (3.0f * (float) p.echoCount + 2.0f);
    const auto echoBase = mods;
    if (p.echoFat)
        mods.gain *= 1.5f - (float) p.echoCount * fatGain;
    schedule ({ time, true, note, velocity, mods });

    const auto period = samplesPerStep (p) * (double) p.echoTime;
    for (int k = 1; k <= p.echoCount; ++k)
    {
        NoteMods e = echoBase;
        e.echo = k;
        auto pan = p.echoPan * (float) k;
        if (p.echoFat)
        {
            e.gain = fatGain * std::cos (juce::MathConstants<float>::pi * 0.25f * juce::jmin (1.0f, std::abs (pan)));
            pan = 0.0f;
        }
        else
        {
            e.gain = std::pow (feedGain, (float) k);
            if (e.gain < 1.0e-3f)
                break;
            if (p.echoPing)   // bounce between hard left and right
            {
                pan = std::fmod (pan + 1.0f, 4.0f);
                if (pan < 0.0f)
                    pan += 4.0f;
                pan = pan <= 2.0f ? pan - 1.0f : 3.0f - pan;
            }
        }

        e.pan = juce::jlimit (-1.0f, 1.0f, pan);
        e.cut = p.echoX;   // applied k times in refreshControls (echoMod)
        e.res = p.echoY;
        e.pitch = p.echoPitch * (float) k;
        schedule ({ time + (int64_t) std::round (period * k), true, note, velocity, e });
    }
}

void SamplerEngine::playNoteOff (int64_t time, int note, int echo, const EngineParams& p)
{
    noteOff (note, echo, p);
    if (echo != 0 || p.echoFeed <= 0.0f)
        return;

    const auto period = samplesPerStep (p) * (double) p.echoTime;
    for (int k = 1; k <= p.echoCount; ++k)
    {
        NoteMods e;
        e.echo = k;
        schedule ({ time + (int64_t) std::round (period * k), false, note, 0.0f, e });
    }
}

SamplerEngine::Voice& SamplerEngine::allocateVoice (const EngineParams& p)
{
    // FL's Max polyphony has no effect on what it renders (measured: 1 and 2
    // voices, same and different keys, with and without the volume envelope),
    // so the knob is kept for the look and ignored here. Only the engine's own
    // voice count limits: the oldest note fades out with the declick.
    const int limit = maxVoices;
    for (;;)
    {
        int sounding = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.killing)
            {
                ++sounding;
                if (oldest == nullptr || v.age < oldest->age)
                    oldest = &v;
            }

        if (sounding < limit || oldest == nullptr)
            break;
        startKill (*oldest, p);
    }

    for (auto& v : voices)
        if (! v.active)
            return v;

    // Every slot busy (including fading voices): hard-steal the oldest.
    auto* oldest = &voices[0];
    for (auto& v : voices)
        if (v.age < oldest->age)
            oldest = &v;
    return *oldest;
}

void SamplerEngine::cutByGroup (const EngineParams& p)
{
    ++eventSerial;
    // Like Cut itself: voices go to their release, or fade out with the
    // declick time when there is no volume envelope.
    for (auto& v : voices)
        if (v.active && ! v.killing)
        {
            releaseVoice (v, p);
            if (! p.volT.envOn)
                startKill (v, p);
        }
}

void SamplerEngine::noteOn (int note, float velocity, NoteMods mods, const SampleData& s, const EngineParams& p)
{
    ++eventSerial;
    if (! mods.preview && noteStarts.size() < 128)
        noteStarts.push_back (blockPos);

    // Cut itself: earlier notes jump to their release phase. Without a
    // volume envelope a sample would otherwise keep playing to its end, so it
    // is stopped with the declick fade instead.
    if (p.cutItself)
        for (auto& v : voices)
            if (v.active && ! v.killing)
            {
                releaseVoice (v, p);
                if (! p.volT.envOn)
                    startKill (v, p);
            }

    // Arpeggio slide (measured in FL): the sounding arpeggio note takes the new
    // pitch at the step without restarting the sample (legato, no glide).
    if (mods.arp && mods.slide)
        for (auto& v : voices)
            if (v.active && ! v.released && ! v.killing && v.mods.arp)
            {
                v.note = note;
                v.targetNote = (float) note;
                v.glideTicks = 0;
                lastNote = (float) note;
                return;
            }
    // Measured: once the sliding note has run out, later slide steps stay silent.
    if (mods.legatoOnly)
        return;

    if (p.mono && mods.echo == 0)
    {
        // Overlapping notes in mono mode slide to the new pitch without
        // retriggering; the earlier key goes on the stack to return to.
        for (auto& v : voices)
            if (v.active && ! v.released && ! v.killing && v.mods.echo == 0)
            {
                eraseMonoKey (note);
                eraseMonoKey (v.note);
                if (numMonoKeys < (int) monoKeys.size())
                    monoKeys[(size_t) numMonoKeys++] = v.note;
                v.note = note;
                startGlide (v, note, p);
                lastNote = (float) note;
                return;
            }
        numMonoKeys = 0;
    }

    auto& v = allocateVoice (p);
    const auto spec = declickSpec (p.declick);
    const auto loop = loopFrames (s, p);

    v = Voice();
    v.filter.startCompensation ((int) std::lround (sampleRate * 60.0 / (juce::jmax (1.0, p.bpm) * 96.0)));   // LPx2 (measured)
    v.active = true;
    v.note = note;
    v.velocity = velocity;
    v.mods = mods;
    v.age = ++ageCounter;
    v.pos = juce::jlimit (0.0, (double) s.playEnd - 1.0, (double) p.startOffset * s.playEnd);
    v.inLoop = loop.valid && v.pos < loop.end;
    if (isGranular (p.stretchMode))
    {
        auto& st = stretchers[(size_t) (&v - voices.data())];
        st.configure (p.stretchMode, s.sourceRate);
        st.sourceToOutputRateScale = sampleRate / s.sourceRate;
        st.reset (v.pos);
    }
    v.onsetIdx = 0;
    while (v.onsetIdx < (int) s.onsets.size() && s.onsets[(size_t) v.onsetIdx] <= (int) v.pos)
        ++v.onsetIdx;
    v.slicePos = v.pos;
    v.targetNote = (float) note;
    v.glideTicks = 0;
    if ((p.porta || mods.slide) && lastNote >= 0)
        beginGlide (v, lastNote * 100.0f, note, p);   // portamento: from the previous note

    for (int i = 0; i < 5; ++i)
    {
        v.env[i].noteOn();
        v.lfo[i].noteOn();
    }
    startModKnots (v, p);
    v.pitchMul = 1;
    // FL's pitch modulation runs half a tick ahead of the note (measured for
    // the LFO and the envelope); the envelope starts that much early (less
    // 13 samples, as it is applied per control block).
    if (p.pitchT.envOn)
        v.env[4].advance (p.pitchT.env, (float) juce::jmax (0.0, 30.0 / (juce::jmax (1.0, p.bpm) * 96.0) - 13.0 / sampleRate));

    // FL only fades in when playback starts after the first frame. The
    // Transient modes keep the attack and only remove the step: they subtract
    // the first value with a fading weight (measured, 175 samples at 44.1 kHz).
    v.fadeInLinear = spec.transientIn;
    v.fadeInCrossfade = p.declick == DeclickMode::crossfade;
    v.fadeInLen = v.pos <= 0.0 ? 0
                : spec.transientIn ? (int) std::round (175.0 * sampleRate / 44100.0)
                : v.fadeInCrossfade ? (int) std::round (declick::crossfadeInLength * sampleRate / 44100.0)
                                    : (int) std::round (spec.inMs * 0.001 * sampleRate);
    if (mods.echo == 0)
        lastNote = (float) note;
}

void SamplerEngine::eraseMonoKey (int key)
{
    for (int i = 0; i < numMonoKeys; ++i)
        if (monoKeys[(size_t) i] == key)
        {
            for (int j = i + 1; j < numMonoKeys; ++j)
                monoKeys[(size_t) j - 1] = monoKeys[(size_t) j];
            --numMonoKeys;
            return;
        }
}

void SamplerEngine::startGlide (Voice& v, int note, const EngineParams& p)
{
    beginGlide (v, v.glideTicks > 0 ? v.glideCents : v.targetNote * 100.0f, note, p);
}

void SamplerEngine::beginGlide (Voice& v, float fromCents, int note, const EngineParams& p) const
{
    v.targetNote = (float) note;
    v.glideCents = fromCents;
    v.glideTicks = juce::jmax (1, p.slideTicks);
    v.glideStep = (float) (((double) note * 100.0 - (double) fromCents) / v.glideTicks);
    v.glideDone = 0;

    // The event's tick: host events arrive at the sample before a tick that
    // falls between samples, so a tick within a sample counts as this one;
    // off-grid events step at once.
    const auto ticks = songTimeAt (p, blockPos) * juce::jmax (1.0, p.bpm) * 1.6;
    const auto tickSamples = sampleRate * 60.0 / (juce::jmax (1.0, p.bpm) * 96.0);
    const auto nearest = std::round (ticks);
    v.glideTick0 = std::abs (ticks - nearest) * tickSamples <= 1.0 ? nearest : std::floor (ticks);
}

double SamplerEngine::soundingNote (Voice& v, const EngineParams& p, double songSeconds) const
{
    if (v.glideTicks <= 0)
        return v.targetNote;
    if (v.glideDone < v.glideTicks)
    {
        // A step lands on the sample nearest its tick (measured: FL steps half
        // a sample early against the exact tick time).
        const auto tickSamples = sampleRate * 60.0 / (juce::jmax (1.0, p.bpm) * 96.0);
        const auto ticks = songSeconds * juce::jmax (1.0, p.bpm) * 1.6 + 0.5 / tickSamples;
        const auto due = juce::jmin (v.glideTicks, (int) std::floor (ticks - v.glideTick0) + 1);
        while (v.glideDone < due)
        {
            v.glideCents += v.glideStep;
            ++v.glideDone;
        }
    }
    return (double) v.glideCents / 100.0;
}

void SamplerEngine::noteOff (int note, int echo, const EngineParams& p)
{
    ++eventSerial;

    // Mono (measured in FL): releasing the sounding key glides back to the
    // previous key on the stack and keeps the note going; with none left the
    // note is released. Releasing another key takes it off the stack, but only
    // with the volume envelope on: without it FL keeps the key and still
    // returns to it later.
    if (p.mono && echo == 0)
        for (auto& v : voices)
            if (v.active && ! v.released && ! v.killing && v.mods.echo == 0)
            {
                if (v.note != note)
                {
                    if (p.volT.envOn)
                        eraseMonoKey (note);
                    return;
                }
                if (numMonoKeys > 0)
                {
                    const int back = monoKeys[(size_t) --numMonoKeys];
                    v.note = back;
                    startGlide (v, back, p);
                    lastNote = (float) back;
                    return;
                }
                releaseVoice (v, p);
                return;
            }

    // Overlapping notes on one key: a note-off ends the oldest (measured in FL).
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.active && ! v.released && v.note == note && v.mods.echo == echo && (oldest == nullptr || v.age < oldest->age))
            oldest = &v;
    if (oldest != nullptr)
        releaseVoice (*oldest, p);
}

void SamplerEngine::releaseVoice (Voice& v, const EngineParams& p)
{
    ++eventSerial;
    v.released = true;
    const EngineParams::Target* targets[] = { &p.panT, &p.volT, &p.xT, &p.yT, &p.pitchT };
    for (int i = 0; i < 5; ++i)
        v.env[i].noteOff (targets[i]->env, 1.0 / sampleRate);
    releaseModKnots (v, v.lfo[1].getTime() - v.ctlLeft / sampleRate, p);
    v.filterKnot = -1;

    if (! p.volT.envOn && v.inLoop)
        startKill (v, p);   // a looping sample without an envelope would never end
    // Otherwise, without a volume envelope, the sample plays to its end
    // after note release (manual: default sampler behaviour).
}

void SamplerEngine::startKill (Voice& v, const EngineParams& p)
{
    if (v.killing)
        return;
    v.killing = true;
    v.killPos = 0;
    v.killLowL = v.killLowR = 0;
    v.killShape = p.declick == DeclickMode::transient ? KillShape::transient
                : p.declick == DeclickMode::crossfade ? KillShape::crossfade
                                                      : KillShape::cosine;
    v.killLen = v.killShape == KillShape::transient ? (int) std::round (declick::transientOutLength * sampleRate / 44100.0)
              : v.killShape == KillShape::crossfade ? (int) std::round (declick::crossfadeOutLength * sampleRate / 44100.0)
                                                    : (int) std::round (declickSpec (p.declick).outMs * 0.001 * sampleRate);
    if (v.killLen <= 0)
        v.active = false;
}

void SamplerEngine::startModKnots (Voice& v, const EngineParams& p)
{
    const auto tick = 60.0 / (juce::jmax (1.0, p.bpm) * 96.0);
    for (int xy = 0; xy < 2; ++xy)
    {
        auto& k = v.knots[xy];
        const auto& ep = (xy == 0 ? p.xT : p.yT).env;
        k.env = v.env[2 + xy];
        k.envTime = 0;
        k.nextKnot = tick;
        auto ahead = k.env;
        k.next = k.prev = ahead.advance (ep, (float) tick);   // the first tick holds the second knot
    }
}

float SamplerEngine::modEnvelopeAt (Voice& v, int xy, double t, const EngineParams& p)
{
    const auto tick = 60.0 / (juce::jmax (1.0, p.bpm) * 96.0);
    const auto& ep = (xy == 0 ? p.xT : p.yT).env;
    auto& k = v.knots[xy];
    for (int guard = 0; t >= k.nextKnot && guard < 64; ++guard)
    {
        k.env.advance (ep, (float) (k.nextKnot - k.envTime));
        k.envTime = k.nextKnot;
        k.prev = k.next;
        k.nextKnot += tick;
        auto ahead = k.env;
        k.next = ahead.advance (ep, (float) (k.nextKnot - k.envTime));
    }
    const auto frac = juce::jlimit (0.0, 1.0, (t - (k.nextKnot - tick)) / tick);
    return k.prev + (k.next - k.prev) * (float) frac;
}

void SamplerEngine::releaseModKnots (Voice& v, double t, const EngineParams& p)
{
    for (int xy = 0; xy < 2; ++xy)
    {
        auto& k = v.knots[xy];
        const auto& ep = (xy == 0 ? p.xT : p.yT).env;
        if (t > k.envTime)
        {
            k.env.advance (ep, (float) (t - k.envTime));
            k.envTime = t;
        }
        k.env.noteOff (ep, 1.0 / sampleRate);
        auto ahead = k.env;
        k.next = ahead.advance (ep, (float) (k.nextKnot - k.envTime));
    }
}

void SamplerEngine::setupFilterAt (Voice& v, double t, double songT, const EngineParams& p)
{
    // FL (measured): Mod X / Y are worked out at each tick of the note (the
    // first tick holding the second knot) and the filter's coefficients move
    // linearly from one tick's to the next.
    const auto tick = 60.0 / (juce::jmax (1.0, p.bpm) * 96.0);
    if (p.filterType != FilterType::fastLP)
    {
        t += 0.5 / sampleRate;   // these switch on the sample nearest each tick (measured; the Fast LP does not)
        songT += 0.5 / sampleRate;
    }
    if (v.filterKnot < 0.0 || t >= v.filterKnot)
    {
        const auto knot = (std::floor (t / tick) + 1.0) * tick;
        float envPrev[2] = { 0, 0 }, envNext[2] = { 0, 0 };
        for (int xy = 0; xy < 2; ++xy)
            if ((xy == 0 ? p.xT : p.yT).envOn)
            {
                modEnvelopeAt (v, xy, t, p);
                envPrev[xy] = v.knots[xy].prev;
                envNext[xy] = v.knots[xy].next;
            }
        auto setAt = [&] (dsp::VoiceFilter& c, double at, const float* env)
        {
            at = juce::jmax (at, tick);   // the first tick holds the second knot
            const auto songAt = songT + (at - t);
            float x = v.cutBase + p.xT.envAmt * env[0], y = v.resBase + p.yT.envAmt * env[1];
            if (p.xT.lfo.amount != 0.0f)
                x += dsp::Lfo::at (p.xT.lfo, at, p.bpm, songAt);
            if (p.yT.lfo.amount != 0.0f)
                y += dsp::Lfo::at (p.yT.lfo, at, p.bpm, songAt);
            c.setup (p.filterType, echoShift (x, v.mods.cut, v.mods.echo), echoShift (y, v.mods.res, v.mods.echo), sampleRate);
        };
        setAt (v.coefPrev, knot - tick, envPrev);
        setAt (v.coefNext, knot, envNext);
        v.filterKnot = knot;
    }
    v.filter.setBetween (v.coefPrev, v.coefNext, juce::jlimit (0.0, 1.0, (t - (v.filterKnot - tick)) / tick));
}

void SamplerEngine::refreshControls (Voice& v, const SampleData& s, const EngineParams& p)
{
    // Recomputes pitch, gain and pan targets for the rest of the current
    // control block without moving the envelopes / LFOs on.
    updateControls (v, 0, s, p);
    if (! v.active)
        return;
    v.gainStep = (v.gainTarget - v.gain) / (float) juce::jmax (1, v.ctlLeft);
    const auto angle = (v.panValue + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    v.panL = juce::MathConstants<float>::sqrt2 * std::cos (angle);
    v.panR = juce::MathConstants<float>::sqrt2 * std::sin (angle);
}

void SamplerEngine::updateControls (Voice& v, int numSamples, const SampleData& s, const EngineParams& p)
{
    const auto dt = (float) (numSamples / sampleRate);

    // Envelopes and LFOs, index order pan, vol, x, y, pitch
    const EngineParams::Target* targets[5] = { &p.panT, &p.volT, &p.xT, &p.yT, &p.pitchT };
    float env[5], lfo[5];
    if (numSamples > 0)
    {
        v.volLfoStart = v.lfo[1].getTime();
        v.songLfoStart = songTimeAt (p, ctlBufferPos);
    }
    for (int i = 0; i < 5; ++i)
    {
        // The volume envelope runs per sample in renderRange (block steps
        // moved its transitions by up to a block against FL).
        env[i] = (targets[i]->envOn && i != 1) ? v.env[i].advance (targets[i]->env, dt) : 0.0f;
        lfo[i] = v.lfo[i].advance (targets[i]->lfo, dt, p.bpm, v.songLfoStart + dt);
    }

    const float volEnv = 1.0f;
    if (p.volT.envOn && v.env[1].isIdle() && v.envStarted)
        v.active = false;

    // Portamento / mono glide: the block's note (Resample steps per sample in renderRange)
    v.blockNote = soundingNote (v, p, v.songLfoStart);

    // Velocity and keyboard tracking offsets around their middle values
    // (measured in FL; a tracking knob at 50% is FL's raw 128): velocity goes
    // through the velocity-to-gain curve, the middle value through
    // (6.5^(m/100) - 1) / 5.5 with m = 0..128, scaled so a middle value of 128
    // is 1; a key knob at 50% moves 0.5 per octave.
    auto midCurve = [] (float m) { return (std::pow (6.5f, m / 100.0f) - 1.0f) / 5.5f; };
    const auto dv = 2.0f * (velocityGain (v.velocity) - midCurve (p.velMid * 128.0f)) / midCurve (128.0f);
    const auto dk = ((float) v.note - (float) p.keyMid) / 12.0f;

    // Pitch: semitones relative to the root key; MIDI pitch bend spans +/- range.
    const auto semis = (v.blockNote - (double) p.rootNote) + p.pitch + p.fineTune * 0.01f
                     + bend * (float) p.pitchRange + v.mods.pitch
                     // FL: the pitch envelope and LFO amounts are in cents, raw
                     // -128..128 (measured: full amount = +/-128 cents).
                     + (p.pitchT.envAmt * env[4]
                        + (pitchLfoPerSample (p) ? 0.0f : dsp::Lfo::at (p.pitchT.lfo, v.lfo[4].getTime() + 30.0 / (juce::jmax (1.0, p.bpm) * 96.0), p.bpm,
                                                                      v.songLfoStart + (double) numSamples / sampleRate + 30.0 / (juce::jmax (1.0, p.bpm) * 96.0)))) * 1.28f;
    v.increment = (s.sourceRate / sampleRate) * std::pow (2.0, semis / 12.0);

    // Time stretching: target duration from TIME (beats) or the sample's own
    // length, times MUL. Resample plays it like tape (pitch follows speed);
    // the other methods keep duration and pitch independent.
    const auto srcSeconds = (double) s.playEnd / s.sourceRate;
    const auto mul = std::abs (p.stretchMul - 1.0f) < 1.0e-4f ? 1.0 : (double) p.stretchMul;
    const auto speed = (p.stretchTime > 0.01f ? srcSeconds / ((double) p.stretchTime * 60.0 / juce::jmax (1.0, p.bpm)) : 1.0) / mul;
    if (p.stretchMode == StretchMode::resample)
    {
        v.increment *= speed;
        v.timeRate = v.increment;
    }
    else
    {
        v.timeRate = (s.sourceRate / sampleRate) * speed;
    }

    // Filter: Mod X = cutoff, Mod Y = resonance. With an envelope or LFO on
    // them the coefficients follow per sample (setupFilterAt).
    v.cutBase = p.cutoff + p.xOfs + p.velX * dv + p.keyX * dk;
    v.resBase = p.resonance + p.yOfs + p.velY * dv + p.keyY * dk;
    v.filterPerSample = p.xT.lfo.amount != 0.0f || p.yT.lfo.amount != 0.0f
                     || (p.xT.envOn && p.xT.envAmt != 0.0f) || (p.yT.envOn && p.yT.envAmt != 0.0f);
    const auto cut = echoShift (v.cutBase, v.mods.cut, v.mods.echo);
    const auto res = echoShift (v.resBase, v.mods.res, v.mods.echo);
    // Fast LP with Mod X fully open and no resonance is an identity; skip it,
    // unless Mod X / Y move (the filter state must carry on as in FL).
    const bool filterOn = v.filterPerSample || ! (p.filterType == FilterType::fastLP && cut >= 1.0f && res <= 0.0f);
    if (filterOn && ! v.filterOn)
        v.filter.reset();
    v.filterOn = filterOn;
    if (v.filterPerSample)
    {
        v.filterKnot = -1;   // parameters may have changed
        setupFilterAt (v, v.volLfoStart, v.songLfoStart, p);
    }
    else if (filterOn)
        v.filter.setup (p.filterType, cut, res, sampleRate);

    // Gain: volume multiplier, velocity, envelope and a tremolo of 1 + lfo
    // (measured in FL: a full-depth volume LFO swings the gain over 0..2).
    // The tremolo is applied per sample in renderRange (FL's LFO moves
    // linearly between song ticks, which the control blocks would blur).
    v.gainTarget = p.volume * velocityGain (v.velocity) * volEnv * v.mods.gain;
    if (v.firstBlock)
    {
        v.gain = v.gainTarget;
        v.firstBlock = false;
    }

    // Pan LFO: FL swings twice the amount (amount 75% reaches hard left /
    // right and clips there); added per sample in renderRange.
    v.panBase = p.pan + v.mods.pan + p.velPan * dv + p.keyPan * dk;   // FL has no panning envelope
    v.panValue = juce::jlimit (-1.0f, 1.0f, v.panBase + 2.0f * lfo[0]);
}

void SamplerEngine::renderRange (juce::AudioBuffer<float>& out, int start, int num,
                                 const SampleData& s, const EngineParams& p)
{
    const auto spec = declickSpec (p.declick);
    const auto loop = loopFrames (s, p);
    const int total = s.getTotalFrames();
    const auto* srcL = s.audio.getReadPointer (0);
    const auto* srcR = s.audio.getReadPointer (1);

    // When playback reaches the end of the sample FL holds the last value and
    // fades it out over the declick out-time (measured: 10 ms cosine for
    // "Out only"), instead of cutting off.
    const int endLimit = s.playEnd;
    juce::ignoreUnused (spec);
    const bool granular = isGranular (p.stretchMode);
    const bool sliceMap = p.stretchMode == StretchMode::sliceMap;
    const bool slicesReset = resetsAtOnsets (p.stretchMode);

    auto* outL = out.getWritePointer (0);
    auto* outR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        for (int done = 0; done < num && v.active;)
        {
            // Control blocks run from each note's own start, so envelope and
            // LFO timing does not depend on where the note falls in the host
            // buffer (it moved FL nulls by a sample or more).
            if (v.ctlLeft <= 0)
            {
                ctlBufferPos = start + done;
                updateControls (v, controlBlock, s, p);
                if (! v.active)
                    break;
                v.ctlLeft = controlBlock;
                v.gainStep = (v.gainTarget - v.gain) / (float) controlBlock;
                // FL's constant-power pan, unity at the centre (measured: 50% left
                // gives L x1.307, R x0.541 = sqrt2 * cos / sin of 22.5 degrees).
                const auto angle = (v.panValue + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                v.panL = juce::MathConstants<float>::sqrt2 * std::cos (angle);
                v.panR = juce::MathConstants<float>::sqrt2 * std::sin (angle);
            }
            const int chunk = juce::jmin (v.ctlLeft, num - done);
            const int base = controlBlock - v.ctlLeft;   // samples into the control block
            const auto panL = v.panL, panR = v.panR, gainStep = v.gainStep;

            for (int i = 0; i < chunk; ++i)
            {
                // Loop points can change mid-note; an invalid loop plays straight through.
                const bool looping = v.inLoop && loop.valid;

                // Neighbour index for the interpolator; wraps inside a forward loop.
                auto at = [&] (int k)
                {
                    if (looping && ! p.pingPong && k >= loop.end)
                        k -= loop.end - loop.start;
                    return juce::jlimit (0, total - 1, k);
                };
                // Pitch LFO in Resample, per sample: FL's pitch follows the
                // tick-interpolated LFO half a tick early, less 2.5 samples
                // (fitted to FL renders: -68 dB against -34 dB for a per-tick step).
                auto incNow = v.increment, rateNow = v.timeRate;
                if (pitchLfoPerSample (p))
                {
                    const auto tickSeconds = 60.0 / (juce::jmax (1.0, p.bpm) * 96.0);
                    const auto lead = 0.5 * tickSeconds - 2.5 / sampleRate;
                    v.pitchMul = std::exp2 ((double) dsp::Lfo::at (p.pitchT.lfo, v.volLfoStart + (double) (base + i) / sampleRate + lead, p.bpm,
                                                                          v.songLfoStart + (double) (base + i) / sampleRate + lead) * 1.28 / 12.0);
                    incNow *= v.pitchMul;
                    rateNow *= v.pitchMul;
                }
                if (v.glideTicks > 0)
                {
                    // Glide steps land on exact ticks (measured), not on control blocks.
                    if (p.stretchMode == StretchMode::resample)
                    {
                        const auto glideMul = std::exp2 ((soundingNote (v, p, v.songLfoStart + (double) (base + i) / sampleRate) - v.blockNote) / 12.0);
                        incNow *= glideMul;
                        rateNow *= glideMul;
                    }
                }

                float l, r;
                if (v.holding && p.stretchMode == StretchMode::resample)
                {
                    // Past the end FL keeps interpolating at the advancing
                    // position with the neighbours clamped (measured with TIME
                    // at fractional speeds), while the declick fades it out.
                    flInterpolator().read (srcL, srcR, v.pos, incNow, at, l, r);
                }
                else if (v.holding)
                {
                    l = v.holdL;
                    r = v.holdR;
                }
                else if (granular)
                {
                    // Time stretching (TimeStretch.h): pitch from the key, length
                    // from MUL / TIME. Past the sample end the source is silent.
                    auto read = [&] (int k, float& sl, float& sr)
                    {
                        if (k < 0 || (k >= s.playEnd && ! looping))
                        {
                            sl = sr = 0.0f;
                            return;
                        }
                        // The stretcher's read position keeps counting up, so
                        // fold it into the loop however many passes it has made.
                        if (looping && k >= loop.end)
                        {
                            const int len = juce::jmax (1, loop.end - loop.start);
                            const int passes = (k - loop.start) / len;
                            const int offset = (k - loop.start) % len;
                            k = (p.pingPong && (passes % 2 == 1)) ? loop.end - 1 - offset : loop.start + offset;
                        }
                        const int m = juce::jlimit (0, total - 1, k);
                        sl = srcL[m];
                        sr = srcR[m];
                    };
                    stretchers[(size_t) (&v - voices.data())].next (read, v.increment, v.timeRate, p.formant, l, r);
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
                            flInterpolator().read (srcL, srcR, v.slicePos, v.increment, at, l, r);
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
                    flInterpolator().read (srcL, srcR, v.pos, incNow, at, l, r);
                    v.lastL = l;
                    v.lastR = r;
                }

                v.gain += gainStep;
                auto g = v.gain;
                if (p.volT.envOn)
                {
                    g *= v.env[1].current (p.volT.env);
                    v.env[1].advance (p.volT.env, (float) (1.0 / sampleRate));
                    if (v.env[1].hasEnded() && ! v.killing)
                        startKill (v, p);   // measured: FL fades the voice out from the release's silent tick
                    v.envStarted = true;
                }
                if (p.volT.lfo.amount != 0.0f)
                    g *= 1.0f + dsp::Lfo::at (p.volT.lfo, v.volLfoStart + (double) (base + i) / sampleRate, p.bpm, v.songLfoStart + (double) (base + i) / sampleRate);

                if (v.fadeInPos < v.fadeInLen)
                {
                    if (v.fadeInLinear)   // transient: remove only the starting step
                    {
                        if (v.fadeInPos == 0)
                        {
                            v.stepL = l;
                            v.stepR = r;
                        }
                        const auto w = 1.0f - dsp::sCurve ((float) v.fadeInPos++ / (float) v.fadeInLen);
                        l -= v.stepL * w;
                        r -= v.stepR * w;
                    }
                    else if (v.fadeInCrossfade)
                    {
                        g *= declick::crossfadeIn ((double) v.fadeInPos++ / (double) v.fadeInLen);
                    }
                    else
                    {
                        g *= dsp::sCurve ((float) v.fadeInPos++ / (float) v.fadeInLen);
                    }
                }

                float killHp = 0;   // Transient (no bleeding): the sweeping high-pass's coefficient
                if (v.killing)
                {
                    switch (v.killShape)
                    {
                        case KillShape::transient:
                        {
                            const auto x = v.killPos * 44100.0 / sampleRate;
                            killHp = declick::transientCoefficient (x - 44100.0 / sampleRate, x);
                            g *= declick::transientGain (x);
                            break;
                        }
                        case KillShape::crossfade: g *= declick::crossfadeOut ((double) v.killPos / (double) v.killLen); break;
                        case KillShape::cosine:    g *= dsp::sCurve (1.0f - (float) v.killPos / (float) v.killLen); break;
                    }
                    if (++v.killPos >= v.killLen)
                        v.active = false;
                }

                if (v.filterOn)
                {
                    if (v.filterPerSample)
                        setupFilterAt (v, v.volLfoStart + (double) (base + i) / sampleRate,
                                       v.songLfoStart + (double) (base + i) / sampleRate, p);
                    l = v.filter.process (l, 0);
                    r = v.filter.process (r, 1);
                }

                if (killHp > 0)
                {
                    v.killLowL += killHp * (l - v.killLowL);
                    v.killLowR += killHp * (r - v.killLowR);
                    l -= v.killLowL;
                    r -= v.killLowR;
                }

                const int o = start + done + i;
                if (outR != nullptr)
                {
                    if (p.panT.lfo.amount != 0.0f)
                    {
                        const auto pan = juce::jlimit (-1.0f, 1.0f, v.panBase + 2.0f * dsp::Lfo::at (p.panT.lfo, v.volLfoStart + (double) (base + i) / sampleRate, p.bpm, v.songLfoStart + (double) (base + i) / sampleRate));
                        const auto a = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                        outL[o] += l * g * juce::MathConstants<float>::sqrt2 * std::cos (a);
                        outR[o] += r * g * juce::MathConstants<float>::sqrt2 * std::sin (a);
                    }
                    else
                    {
                        outL[o] += l * g * panL;
                        outR[o] += r * g * panR;
                    }
                }
                else
                {
                    outL[o] += 0.5f * (l + r) * g;
                }

                // Advance the play position with loop handling.
                v.pos += rateNow * v.dir;

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
                        stretchers[(size_t) (&v - voices.data())].reset (onset);   // slice stretch: restart at each transient
                    }
                }
                if (looping)
                {
                    if (! p.pingPong)
                    {
                        while (v.pos >= loop.end)
                        {
                            v.pos -= loop.end - loop.start;
                            v.onsetIdx = 0;
                            while (v.onsetIdx < (int) s.onsets.size() && s.onsets[(size_t) v.onsetIdx] <= (int) v.pos)
                                ++v.onsetIdx;
                        }
                    }
                    else if (v.dir > 0 && v.pos >= loop.end)
                    {
                        v.pos = juce::jmax ((double) loop.start, 2.0 * loop.end - v.pos);
                        v.dir = -1;
                    }
                    else if (v.dir < 0 && v.pos <= loop.start)
                    {
                        v.pos = juce::jmin ((double) loop.end, 2.0 * loop.start - v.pos);
                        v.dir = 1;
                    }
                }
                else if (! v.holding && (v.pos >= endLimit || v.pos < 0.0))
                {
                    v.holding = true;
                    v.holdL = v.lastL;
                    v.holdR = v.lastR;
                    startKill (v, p);
                }

                if (! v.active)
                    break;
            }
            done += chunk;
            v.ctlLeft -= chunk;
        }
    }
}
