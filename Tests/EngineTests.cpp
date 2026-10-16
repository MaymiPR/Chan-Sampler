// Offline checks of the sampler engine against documented FL Channel Sampler
// behaviour (docs/EVIDENCE.md). Renders MIDI through SamplerEngine, no host.

#include <juce_audio_basics/juce_audio_basics.h>
#include <iostream>

#include "../Source/SampleData.h"
#include "../Source/SamplerEngine.h"
#include "../Source/Params.h"

namespace
{
    constexpr double rate = 48000.0;
    int failures = 0;

    void check (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << (detail.isEmpty() ? "" : "  (" + detail + ")") << "\n";
        failures += ok ? 0 : 1;
    }

    RawSample sine (double seconds, double hz = 440.0)
    {
        RawSample r;
        r.sampleRate = rate;
        r.audio.setSize (1, (int) (seconds * rate));
        for (int i = 0; i < r.audio.getNumSamples(); ++i)
            r.audio.setSample (0, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate));
        return r;
    }

    struct Event { int sample; juce::MidiMessage msg; };

    // Render numSamples in 512-sample blocks, returning the left channel.
    std::vector<float> render (const SampleData& s, const EngineParams& p, int numSamples, std::vector<Event> events)
    {
        SamplerEngine engine;
        engine.prepare (rate);
        std::vector<float> out;
        juce::AudioBuffer<float> block (2, 512);
        for (int pos = 0; pos < numSamples; pos += 512)
        {
            block.clear();
            juce::MidiBuffer midi;
            for (auto& e : events)
                if (e.sample >= pos && e.sample < pos + 512)
                    midi.addEvent (e.msg, e.sample - pos);
            engine.render (block, midi, &s, p);
            for (int i = 0; i < 512 && pos + i < numSamples; ++i)
                out.push_back (block.getSample (0, i));
        }
        return out;
    }

    float rms (const std::vector<float>& v, int from, int to)
    {
        double sum = 0;
        for (int i = from; i < to; ++i) sum += v[(size_t) i] * v[(size_t) i];
        return (float) std::sqrt (sum / juce::jmax (1, to - from));
    }

    // Zero crossings per second, rising edge only, which gives the frequency.
    double frequency (const std::vector<float>& v, int from, int to)
    {
        int crossings = 0;
        for (int i = from + 1; i < to; ++i)
            if (v[(size_t) i - 1] < 0.0f && v[(size_t) i] >= 0.0f) ++crossings;
        return crossings / ((to - from) / rate);
    }

    bool allFinite (const std::vector<float>& v)
    {
        for (auto x : v) if (! std::isfinite (x)) return false;
        return true;
    }

    EngineParams params() { return {}; }

    juce::MidiMessage on (int note)  { return juce::MidiMessage::noteOn (1, note, 1.0f); }
    juce::MidiMessage off (int note) { return juce::MidiMessage::noteOff (1, note); }
}

int main()
{
    const int sec = (int) rate;
    PrecomputeSettings plain;
    auto oneSec = buildProcessedSample (sine (1.0), plain);

    // Root key C5 (MIDI 60) plays at original pitch; an octave up doubles it.
    {
        auto p = params();
        auto a = render (*oneSec, p, sec / 2, { { 0, on (60) } });
        auto b = render (*oneSec, p, sec / 2, { { 0, on (72) } });
        const auto fa = frequency (a, 1000, sec / 2), fb = frequency (b, 1000, sec / 4);
        check (std::abs (fa - 440) < 5, "root key plays original pitch", juce::String (fa, 1) + " Hz");
        check (std::abs (fb - 880) < 10, "+12 semitones doubles frequency", juce::String (fb, 1) + " Hz");
    }

    // Manual: without the volume envelope the sample keeps playing after note release.
    {
        auto p = params();
        auto v = render (*oneSec, p, sec, { { 0, on (60) }, { 4800, off (60) } });
        check (rms (v, sec / 2, sec / 2 + 4800) > 0.3f, "env off: plays past note-off");
    }

    // With the volume envelope on, release ends the note.
    {
        auto p = params();
        p.volT.envOn = true;
        p.volT.env.release = 0.05f;
        auto v = render (*oneSec, p, sec, { { 0, on (60) }, { 4800, off (60) } });
        check (rms (v, sec / 2, sec / 2 + 4800) < 1.0e-4f, "env on: release silences note");
    }

    // Looping sustains past the sample's length.
    {
        auto shortSample = buildProcessedSample (sine (0.25), plain);
        auto p = params();
        p.useLoop = true;
        p.loopStart = 0.25f;
        p.loopEnd = 0.75f;
        auto v = render (*shortSample, p, sec, { { 0, on (60) } });
        check (rms (v, sec / 2, sec) > 0.3f, "loop sustains beyond sample end");

        p.pingPong = true;
        auto pp = render (*shortSample, p, sec, { { 0, on (60) } });
        check (rms (pp, sec / 2, sec) > 0.3f && allFinite (pp), "ping-pong loop sustains");
    }

    // Declick "Out only": a cut note fades over ~10 ms rather than stopping dead.
    {
        auto p = params();
        p.useLoop = true;     // looping + env off -> note-off fades out
        p.loopEnd = 1.0f;
        auto v = render (*oneSec, p, sec / 2, { { 0, on (60) }, { 9600, off (60) } });
        const int fade = (int) (0.010 * rate);
        check (rms (v, 9600, 9600 + fade / 4) > 0.05f, "declick: audible during fade");
        check (rms (v, 9600 + fade + 64, 9600 + fade + 2000) < 1.0e-4f, "declick: silent after 10 ms");
    }

    // Overlapping notes keep both voices (FL's Max polyphony has no effect, measured).
    {
        auto p = params();
        SamplerEngine engine;
        engine.prepare (rate);
        juce::AudioBuffer<float> block (2, 4096);
        juce::MidiBuffer midi;
        midi.addEvent (on (60), 0);
        midi.addEvent (on (64), 100);
        engine.render (block, midi, oneSec.get(), p);
        check (engine.getNumActiveVoices() == 2, "two overlapping notes keep two voices", juce::String (engine.getNumActiveVoices()));
    }

    // Mono slide: a second overlapping note glides instead of retriggering.
    {
        auto p = params();
        p.mono = true;
        p.slideTicks = 4;
        auto v = render (*oneSec, p, sec / 2, { { 0, on (60) }, { 4800, on (72) } });
        const auto f = frequency (v, sec / 4, sec / 2);
        check (std::abs (f - 880) < 10, "mono slides to new pitch", juce::String (f, 1) + " Hz");
    }

    // Precomputed: Normalize brings the peak to full scale; Length trims.
    {
        PrecomputeSettings s;
        s.normalize = true;
        s.length = 0.5f;   // fraction of what remains after Sample Start
        auto d = buildProcessedSample (sine (1.0), s);
        const auto peak = d->audio.getMagnitude (0, 0, d->playEnd);
        check (std::abs (peak - 1.0f) < 1.0e-3f, "normalize peak = 1", juce::String (peak, 4));
        check (std::abs (d->playEnd - sec / 2) < 20 && d->lengthTrimmed, "length 50% trims", juce::String (d->playEnd));
    }

    // Precomputed: Trim at zero keeps trailing digital silence (as FL does);
    // any threshold above zero removes it.
    {
        auto r = sine (0.5);
        r.audio.setSize (1, sec, true, true);   // second half is digital silence
        auto d = buildProcessedSample (r, plain);
        check (d->playEnd == sec, "trim 0 keeps trailing silence", juce::String (d->playEnd));
        auto t = plain;
        t.trim = 1.0f / 256.0f;
        d = buildProcessedSample (r, t);
        check (std::abs (d->playEnd - sec / 2) < 2, "trim removes trailing silence", juce::String (d->playEnd));
    }

    // Filter: a low cutoff attenuates a 440 Hz tone.
    {
        auto p = params();
        p.filterType = FilterType::lpx2;
        p.cutoff = 0.0f;   // Mod X minimum (~170 Hz)
        auto v = render (*oneSec, p, sec / 2, { { 0, on (60) } });
        check (rms (v, 4800, sec / 2) < 0.05f && allFinite (v), "LPx2 at low cutoff attenuates",
               juce::String (rms (v, 4800, sec / 2), 4));
    }

    // Time stretching: Stretch keeps the duration when pitched; Resample with
    // MUL x2 plays like tape (twice as long, an octave down); TIME sets the length.
    {
        auto lastSound = [] (const std::vector<float>& v)
        {
            for (int i = (int) v.size() - 1; i >= 0; --i)
                if (std::abs (v[(size_t) i]) > 1.0e-3f) return i;
            return 0;
        };

        auto p = params();
        p.stretchMode = StretchMode::stretch;
        auto up = render (*oneSec, p, 3 * sec, { { 0, on (72) }, { (int) (2.5 * sec), off (72) } });
        check (std::abs (lastSound (up) - sec) < sec / 20, "stretch: octave up keeps 1 s length", juce::String (lastSound (up)));
        check (std::abs (frequency (up, sec / 10, sec * 8 / 10) - 880) < 15, "stretch: octave up plays 880 Hz",
               juce::String (frequency (up, sec / 10, sec * 8 / 10), 1));

        auto q = params();
        q.stretchMul = 2.0f;
        auto tape = render (*oneSec, q, 3 * sec, { { 0, on (60) }, { (int) (2.5 * sec), off (60) } });
        check (std::abs (lastSound (tape) - 2 * sec) < sec / 20, "resample MUL x2 doubles length", juce::String (lastSound (tape)));
        check (std::abs (frequency (tape, sec / 10, sec) - 220) < 8, "resample MUL x2 halves pitch", juce::String (frequency (tape, sec / 10, sec), 1));

        auto t = params();
        t.stretchMode = StretchMode::stretch;
        t.stretchTime = 4.0f;    // 4 beats at 120 BPM = 2 s
        auto beats = render (*oneSec, t, 3 * sec, { { 0, on (60) }, { (int) (2.5 * sec), off (60) } });
        check (std::abs (lastSound (beats) - 2 * sec) < sec / 20, "TIME 4 beats at 120 BPM lasts 2 s", juce::String (lastSound (beats)));
        check (std::abs (frequency (beats, sec / 10, sec) - 440) < 8, "TIME stretch keeps pitch", juce::String (frequency (beats, sec / 10, sec), 1));
    }

    // Stress: every filter type, envelopes, LFOs and extreme pitch stay finite.
    {
        bool ok = true;
        for (int t = 0; t < 8; ++t)
        {
            auto p = params();
            p.filterType = (FilterType) t;
            p.cutoff = 0.5f;
            p.resonance = 1.0f;
            p.xT.envOn = p.yT.envOn = p.panT.envOn = p.pitchT.envOn = p.volT.envOn = true;
            p.xT.envAmt = p.yT.envAmt = p.panT.envAmt = 1.0f;
            p.pitchT.envAmt = -1.0f;
            p.xT.lfo.amount = p.yT.lfo.amount = p.pitchT.lfo.amount = p.volT.lfo.amount = p.panT.lfo.amount = 1.0f;
            p.pitch = 24.0f;
            ok = ok && allFinite (render (*oneSec, p, sec / 2, { { 0, on (100) }, { 100, on (20) }, { 9000, off (100) } }));
        }
        check (ok, "all filter types + full modulation stay finite");
    }

    // Knob positions and texts match FL's (positions are FL's "Copy value").
    {
        struct Host : juce::AudioProcessor
        {
            Host() : juce::AudioProcessor (BusesProperties()) {}
            const juce::String getName() const override { return "host"; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
            double getTailLengthSeconds() const override { return 0; }
            bool acceptsMidi() const override { return true; }
            bool producesMidi() const override { return false; }
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        } host;
        juce::AudioProcessorValueTreeState apvts (host, nullptr, "P", createParameterLayout());

        struct Case { const char* id; double pos; const char* text; };
        const Case cases[] = {
            { pid::pcCrossfade, 0.25, "25%" },
            { pid::pitch, 0.75, "+600 cents" },
            { pid::stretchMul, 0.75, "200.00%" },
            { pid::stretchTime, 4.0 / 192.0, "1:00:00" },
            { pid::stretchTime, 16.0 / 192.0, "4:00:00" },
            { pid::volume, 10000.0 / 25600.0, "78%  -5.2dB  0.55" },
            { pid::volume, 0.25, "50%  -12.7dB  0.23" },
            { pid::pan, 0.75, "50% right" },
            { pid::slide, 820.0 / 1660.0, "0:12" },
            { pid::slide, 1.0, "8:00" },
            { pid::arpTime, 0.575727181509137, "1:00" },
            { pid::arpTime, 1.0, "Hold" },
            { pid::echoTime, 0.375, "3:00" },
            { pid::echoFeed, 0.25, "50%" },
            { pid::pcCut, (512.0 - 16.0) / 1008.0, "50%" },
            { pid::pcStereoDelay, 0.25, "50% left" },
        };
        for (auto& c : cases)
        {
            auto* prm = apvts.getParameter (c.id);
            const auto text = prm->getText ((float) c.pos, 64);
            const auto back = prm->getValueForText (text);
            check (text == c.text && std::abs (back - c.pos) < 0.006,
                   juce::String ("knob text ") + c.id + " @" + juce::String (c.pos, 4),
                   text + " -> " + juce::String (back, 4));
        }
        check (std::abs (flLfoHz ((32950.0f - 200.0f) / 65336.0f) - 0.5f) < 0.01f, "LFO speed default = 0.5 Hz",
               juce::String (flLfoHz ((32950.0f - 200.0f) / 65336.0f), 3));
    }

    std::cout << "\n" << (failures == 0 ? "All tests passed" : juce::String (failures) + " test(s) failed") << "\n";
    return failures == 0 ? 0 : 1;
}
