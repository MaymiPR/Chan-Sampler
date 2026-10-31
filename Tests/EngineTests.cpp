// Offline checks of the sampler engine against documented FL Channel Sampler
// behaviour (docs/EVIDENCE.md). Renders MIDI through SamplerEngine, no host.

#include <juce_audio_basics/juce_audio_basics.h>
#include <iostream>

#include "../Source/SampleData.h"
#include "../Source/SamplerEngine.h"
#include "../Source/Params.h"
#include "../Source/TimeStretch.h"
#include "../Source/FastFFT.h"

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
        engine.ensureStretchers();
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

    // The stretcher alone, as a voice drives it (pitch from cents, length /
    // speed), at 44.1 kHz where FL's e3 was measured.
    constexpr double sr44 = 44100.0;
    std::vector<float> stretch (const std::vector<float>& in, double cents, StretchMode mode = StretchMode::e3Generic, double speed = 1.0)
    {
        TimeStretcher st;
        st.configure (mode, sr44);
        st.reset (0.0);
        const double ratio = std::pow (2.0, cents / 1200.0);
        const int n = (int) (in.size() / speed);
        std::vector<float> out ((size_t) n);
        auto read = [&] (int k, float& l, float& r) { l = r = (k >= 0 && k < (int) in.size()) ? in[(size_t) k] : 0.0f; };
        for (int i = 0; i < n; ++i)
        {
            float l, r;
            st.next (read, ratio, speed, 0.0f, l, r);
            out[(size_t) i] = l;
        }
        return out;
    }

    int firstAbove (const std::vector<float>& v, int from, int to, float level)
    {
        for (int i = from; i < to; ++i)
            if (std::abs (v[(size_t) i]) > level) return i;
        return -1;
    }

    float peakOf (const std::vector<float>& v, int from, int to)
    {
        float p = 0.0f;
        for (int i = from; i < to; ++i) p = juce::jmax (p, std::abs (v[(size_t) i]));
        return p;
    }

    // Best lag (|lag| <= range) of b against a over [from, from + len).
    int bestLag (const std::vector<float>& a, const std::vector<float>& b, int from, int len, int range)
    {
        double best = -1.0e30; int lag = 0;
        for (int d = -range; d <= range; ++d)
        {
            double c = 0, ea = 0, eb = 0;
            for (int i = from; i < from + len; ++i)
            {
                c += a[(size_t) i] * b[(size_t) (i + d)];
                ea += a[(size_t) i] * a[(size_t) i];
                eb += b[(size_t) (i + d)] * b[(size_t) (i + d)];
            }
            const double v = c / std::sqrt (ea * eb + 1.0e-20);
            if (v > best) { best = v; lag = d; }
        }
        return lag;
    }

    std::vector<float> lowpass (const std::vector<float>& v, double hz)
    {
        juce::dsp::IIR::Filter<float> a, b;   // 4th order, so the high band is out of the way
        a.coefficients = b.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr44, (float) hz);
        std::vector<float> out (v.size());
        for (size_t i = 0; i < v.size(); ++i) out[i] = b.processSample (a.processSample (v[i]));
        return out;
    }

    std::vector<float> bandpass (const std::vector<float>& v, double hz, double q)
    {
        juce::dsp::IIR::Filter<float> f;
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr44, (float) hz, (float) q);
        std::vector<float> out (v.size());
        for (size_t i = 0; i < v.size(); ++i) out[i] = f.processSample (v[i]);
        return out;
    }

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

    // Arpeggio TIME "Hold": the first note only, until every key is up (FL).
    {
        auto longSample = buildProcessedSample (sine (4.0), plain);
        auto p = params();
        p.arpDir = 1;
        p.arpTime = 0.0f;
        p.arpGate = 0.5f;
        auto v = render (*longSample, p, 3 * sec, { { 0, on (60) }, { 0, on (64) }, { sec / 2, on (67) },
                                                    { sec, off (60) }, { 2 * sec, off (64) }, { 2 * sec, off (67) } });
        const auto f = frequency (v, sec + sec / 4, 2 * sec - 2400);
        check (std::abs (f - 440) < 5, "arp Hold keeps the first note", juce::String (f, 1) + " Hz");
        check (rms (v, 2 * sec + 4800, 3 * sec) < 1.0e-4f, "arp Hold ends when every key is up");
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

    // Precomputed: a Length shorter than FL's shortest segment is lengthened
    // by it, as in FL (30 frames play 72 and 41 play 42, FL's last frame
    // included), and within it of the sample's end the segment grows
    // backwards instead of running off the end.
    {
        const auto frames = [&] (float start, int span)
        {
            PrecomputeSettings s;
            s.start = start;
            s.length = (float) ((span + 0.25) / ((sec - 1) * (1.0 - (double) start)));
            return buildProcessedSample (sine (1.0), s)->playEnd;
        };
        check (frames (0.3f, 30) == 72, "length of 30 frames plays 72", juce::String (frames (0.3f, 30)));
        check (frames (0.3f, 41) == 42, "length of 41 frames plays 42", juce::String (frames (0.3f, 41)));
        const auto nearEnd = (float) ((sec - 1 - 20) / (double) (sec - 1));
        check (frames (nearEnd, 10) == 52, "short length at the end grows backwards", juce::String (frames (nearEnd, 10)));
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

        // Stretched far past x20 (FL's TIME reaches 48 bars): it still lasts the whole TIME.
        auto far = t;
        far.stretchMode = StretchMode::e3Generic;
        far.stretchTime = 48.0f;   // 24 s at 120 BPM: x24
        auto longNote = render (*oneSec, far, 25 * sec, { { 0, on (60) } });
        check (std::abs (lastSound (longNote) - 24 * sec) < sec / 4 && rms (longNote, 22 * sec, 23 * sec) > 0.05f,
               "TIME stretching x24 lasts the whole TIME", juce::String ((double) lastSound (longNote) / sec, 2) + " s");

        // e3 / e2 loops repeat the stretched loop exactly, a loop x MUL long (as FL's do).
        auto e = params();
        e.stretchMode = StretchMode::e3Generic;
        e.stretchMul = 2.0f;
        e.useLoop = true;
        e.loopStart = 0.3f;
        e.loopEnd = 0.31f;
        auto looped = render (*oneSec, e, 3 * sec, { { 0, on (60) } });
        const int loopLen = (int) std::lround (0.31 * sec) + 1 - (int) std::lround (0.3 * sec);
        float worst = 0.0f;
        for (int i = 2 * sec; i < 2 * sec + 4000; ++i)
            worst = juce::jmax (worst, std::abs (looped[(size_t) i] - looped[(size_t) (i + 2 * loopLen)]));
        check (worst < 1.0e-6f && rms (looped, 2 * sec, 3 * sec) > 0.05f, "e3 loop repeats every loop x MUL", juce::String (worst));
        check (std::abs (frequency (beats, sec / 10, sec) - 440) < 8, "TIME stretch keeps pitch", juce::String (frequency (beats, sec / 10, sec), 1));
    }

    // Slice map (as FL's): a slice starts on a zero crossing, at floor (start
    // x MUL), without a fade; it ends with a fade that is silent just before
    // its end, and the gap up to the next slice is silent.
    {
        RawSample hits;
        hits.sampleRate = rate;
        hits.audio.setSize (1, sec);
        const int second = sec / 2;
        for (int i = 0; i < sec; ++i)
        {
            const int k = i < second ? i : i - second;
            const auto amp = i < second ? 0.1 : 0.9;
            hits.audio.setSample (0, i, (float) (amp * std::exp (-k / (0.5 * rate)) * std::sin (juce::MathConstants<double>::twoPi * 150.0 * k / rate)));
        }
        auto d = buildProcessedSample (hits, plain);
        auto p = params();
        p.stretchMode = StretchMode::sliceMap;
        p.stretchMul = 2.0f;
        auto v = render (*d, p, 2 * sec, { { 0, on (60) } });
        const auto* src = d->audio.getReadPointer (0);
        const auto gain = v[1000] / src[1000];
        float worst = 0.0f;
        for (int k = 1; k < 64; ++k)
            worst = juce::jmax (worst, std::abs (v[(size_t) (2 * second + k)] - gain * src[second + k]));
        check (d->onsets.size() == 2 && d->onsets[1] == second, "slice map: the slice starts on the zero crossing",
               juce::String (d->onsets.size() > 1 ? d->onsets[1] : -1));
        check (worst < 1.0e-4f, "slice map: a slice starts at start x MUL without a fade", juce::String (worst));
        check (rms (v, second - 64, second) == 0.0f && rms (v, second - 2000, second - 1100) > 0.01f,
               "slice map: a slice fades out by its end", juce::String (rms (v, second - 2000, second - 1100), 4));
        check (rms (v, second, 2 * second) == 0.0f, "slice map: silence up to the next slice at MUL x2");

        // Below x1 the next slice cuts the first: the first fades out under
        // it (sounding for a while after the cut, silent 880 / 2 - 501 = 379
        // samples after it at 44.1 kHz, scaled to this rate).
        p.stretchMul = 0.6667f;
        auto w = render (*d, p, sec, { { 0, on (60) } });
        const int cut = (int) std::floor (second * 0.6667);
        auto newOnly = [&] (int t, int a, int b)   // the output minus the new slice alone, from a to b after t
        {
            double e = 0;
            for (int k = a; k < b; ++k)
                e += juce::square (w[(size_t) (t + k)] - gain * src[second + k]);
            return std::sqrt (e / (b - a));
        };
        int t = cut;
        for (int c = cut - 3; c <= cut + 3; ++c)
            if (newOnly (c, 600, 900) < newOnly (t, 600, 900))
                t = c;
        check (newOnly (t, 600, 900) < 1.0e-6 && newOnly (t, 0, 200) > 1.0e-3,
               "slice map: below x1 a cut slice fades out under the next one",
               juce::String (newOnly (t, 0, 200), 5) + " / " + juce::String (newOnly (t, 600, 900), 8));
    }

    // In stereo the slice starts after the nearest sign change in either
    // channel, even when the other channel is louder (FL: L + R would cross
    // further back), and a digitally silent channel never stops it.
    for (const bool silentRight : { false, true })
    {
        RawSample hits;
        hits.sampleRate = rate;
        hits.audio.setSize (2, sec);
        const int second = sec / 2;
        for (int i = 0; i < sec; ++i)
        {
            // A quiet background, negative from 2000 samples before the hit to
            // 30 (left) / 120 (right) before it, then the hit on top.
            const float left = (i >= second - 2000 && i < second - 30) ? -0.004f : 0.004f;
            const float right = silentRight ? 0.0f : ((i >= second - 2000 && i < second - 120) ? -0.008f : 0.008f);
            const auto hit = i >= second ? (float) (0.8 * std::exp (-(i - second) / 2500.0)) : 0.0f;
            hits.audio.setSample (0, i, left + hit);
            hits.audio.setSample (1, i, right + (silentRight ? 0.0f : hit));
        }
        auto d = buildProcessedSample (hits, plain);
        check (d->onsets.size() == 2 && d->onsets[1] == second - 30,
               silentRight ? "slice map: a silent channel does not stop the zero-crossing search"
                           : "slice map: the zero crossing nearest the hit, in either channel",
               juce::String (d->onsets.size() > 1 ? d->onsets[1] : -1));
    }

    // The stretcher's FFT gives JUCE's results (forward and inverse, every
    // size the stretcher uses).
    {
        juce::Random rnd (11);
        double worst = -300.0;
        for (int order = 8; order <= 13; ++order)
        {
            const int N = 1 << order;
            juce::dsp::FFT ref (order);
            FastFFT::Scratch scratch (order);
            FastFFT fast (order, scratch);
            std::vector<std::complex<float>> in ((size_t) N), a ((size_t) N), b ((size_t) N);
            for (auto& x : in) x = { rnd.nextFloat() - 0.5f, rnd.nextFloat() - 0.5f };
            for (const bool inverse : { false, true })
            {
                ref.perform (in.data(), a.data(), inverse);
                fast.perform (in.data(), b.data(), inverse);
                double err = 0, mag = 0;
                for (int i = 0; i < N; ++i) { err = juce::jmax (err, (double) std::abs (a[(size_t) i] - b[(size_t) i])); mag = juce::jmax (mag, (double) std::abs (a[(size_t) i])); }
                worst = juce::jmax (worst, juce::Decibels::gainToDecibels (err / mag, -300.0));
            }
        }
        check (worst < -100.0, "FastFFT matches JUCE's FFT (256..8192, both ways)", juce::String (worst, 1) + " dB");
    }

    // e3 generic (FL's structure: vocoder below 1.485 kHz, grains above;
    // nulltest/E3_NOTES.md). Regression checks of what was measured against FL.
    {
        const int n44 = (int) sr44;
        // A 1 kHz click after silence lands where FL's e3 puts it: 5 samples
        // late at +1200 cents, 6-13 early at -1200 (10% onset; FL: -6).
        std::vector<float> click ((size_t) n44, 0.0f);
        for (int i = 0; i < 400; ++i)
            click[(size_t) (22050 + i)] = 0.8f * (float) (std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / sr44) * std::exp (-i / 100.0));
        for (const double cents : { 1200.0, -1200.0 })
        {
            auto y = stretch (click, cents);
            const auto pk = peakOf (y, 20000, 24000);
            const int on = firstAbove (y, 20000, 24000, 0.1f * pk) - 22050;
            const bool ok = cents > 0 ? std::abs (on - 5) <= 2 : (on <= -5 && on >= -15);
            check (ok && pk > 0.3f, "e3: click timing at " + juce::String (cents, 0) + " cents", juce::String (on) + " samples, peak " + juce::String (pk, 2));
        }

        // FL times an attack by its first sample near the peak (95% of the
        // loudest within 400 samples): a noise burst whose loudest sample is
        // 200 in starts 200 (1 - 1 / ratio) late at +700 cents.
        {
            juce::Random rnd (5);
            std::vector<float> burst ((size_t) n44, 0.0f);
            for (int i = 0; i < 3000; ++i) burst[(size_t) (22050 + i)] = 0.3f * (rnd.nextFloat() * 2.0f - 1.0f);
            burst[22050 + 200] = 0.95f;
            auto y = stretch (burst, 700.0);
            const int on = firstAbove (y, 20000, 24000, 0.02f) - 22050;
            const auto expect = 200.0 * (1.0 - 1.0 / std::pow (2.0, 700.0 / 1200.0));
            check (std::abs (on - expect) <= 6, "e3: attack timed by its peak", juce::String (on) + " samples late (want " + juce::String (expect, 0) + ")");
        }

        // Steady tones in both bands keep their level and move by the pitch.
        for (const double hz : { 300.0, 4000.0 })
            for (const double cents : { -700.0, 700.0 })
            {
                std::vector<float> tone ((size_t) n44);
                for (int i = 0; i < n44; ++i) tone[(size_t) i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sr44);
                auto y = stretch (tone, cents);
                const auto f = frequency (y, n44 / 4, n44 * 3 / 4) * sr44 / rate;   // frequency() counts at the engine's rate
                const auto expect = hz * std::pow (2.0, cents / 1200.0);
                const auto gain = rms (y, n44 / 4, n44 * 3 / 4) / rms (tone, n44 / 4, n44 * 3 / 4);
                check (std::abs (f / expect - 1.0) < 0.01 && std::abs (juce::Decibels::gainToDecibels (gain)) < 1.0f,
                       "e3: " + juce::String (hz, 0) + " Hz tone at " + juce::String (cents, 0) + " cents",
                       juce::String (f, 1) + " Hz (want " + juce::String (expect, 1) + "), " + juce::String (juce::Decibels::gainToDecibels (gain), 2) + " dB");
            }

        // Noise comes out level across the split (stretched x1.25).
        {
            juce::Random rnd (7);
            std::vector<float> noise ((size_t) (2 * n44));
            for (auto& x : noise) x = 0.3f * (rnd.nextFloat() * 2.0f - 1.0f);
            auto y = stretch (noise, 0.0, StretchMode::e3Generic, 1.0 / 1.25);
            auto ratioAt = [&] (double hz)
            {
                auto a = bandpass (noise, hz, 4.0), b = bandpass (y, hz, 4.0);
                return juce::Decibels::gainToDecibels (rms (b, n44 / 4, n44 * 3 / 2) / rms (a, n44 / 4, n44 * 3 / 2));
            };
            const auto split = ratioAt (1485.0), low = ratioAt (600.0), high = ratioAt (4000.0);
            check (std::abs (split - high) < 1.5f && std::abs (low - high) < 1.5f, "e3: noise level across the 1.485 kHz split",
                   juce::String (low, 2) + " / " + juce::String (split, 2) + " / " + juce::String (high, 2) + " dB");
        }

        // A kick an octave down starts at full level and keeps its length.
        {
            std::vector<float> kick ((size_t) n44, 0.0f);
            for (int i = 0; i < 6000; ++i)
                kick[(size_t) i] = 0.8f * (float) (std::sin (juce::MathConstants<double>::twoPi * 60.0 * i / sr44) * std::exp (-i / 1200.0));
            auto y = stretch (kick, -1200.0);
            int lastIn = 0, lastOut = 0;
            for (int i = 0; i < n44; ++i)
            {
                if (std::abs (kick[(size_t) i]) > 0.01f) lastIn = i;
                if (std::abs (y[(size_t) i]) > 0.01f) lastOut = i;
            }
            const auto startGain = peakOf (y, 0, 512) / peakOf (kick, 0, 256);
            check (startGain > 0.6f && std::abs (lastOut - lastIn) < lastIn / 6, "e3: kick an octave down: start level and length",
                   "start x" + juce::String (startGain, 2) + ", ends " + juce::String (lastOut) + " vs " + juce::String (lastIn));
        }

        // Steady sound stays on time after a note's start: noise stretched a
        // hair (x1.0001) comes out as itself in both bands, not shifted.
        {
            juce::Random rnd (11);
            std::vector<float> noise ((size_t) n44);
            for (auto& x : noise) x = 0.3f * (rnd.nextFloat() * 2.0f - 1.0f);
            auto y = stretch (noise, 0.0, StretchMode::e3Generic, 0.9999);
            auto yl = lowpass (y, 1000.0), xl = lowpass (noise, 1000.0);
            const int lagLow = bestLag (xl, yl, 20000, 4096, 400);
            const int lagAll = bestLag (noise, y, 20000, 4096, 400);
            check (std::abs (lagLow - 2) <= 3 && std::abs (lagAll - 2) <= 3, "e3: steady sound on time", "low band " + juce::String (lagLow) + ", all " + juce::String (lagAll) + " samples");
        }

        // After an attack the low band restarts its phases from its loudest
        // point 660 ... 1172 samples after the attack's start, played where
        // the stretch puts it (FL's e3, measured). Steady low tones switched
        // on by a treble click, stretched x1.5; a small bump in that range
        // moves only that point, so the tones come out turned by frequency x
        // (stretch - 1) x how far it moved.
        {
            constexpr int onset = 4320;
            const double hz[] = { 211.3, 389.7, 617.1, 877.9, 1129.3 }, phase[] = { 0.3, 2.1, 4.4, 1.2, 5.6 };
            std::vector<float> tones ((size_t) (2 * n44), 0.0f);
            for (int i = onset; i < 2 * n44; ++i)
                for (int k = 0; k < 5; ++k)
                    tones[(size_t) i] += 0.08f * (float) std::sin (juce::MathConstants<double>::twoPi * hz[k] * i / sr44 + phase[k]);
            for (int i = 0; i < 32; ++i)
                tones[(size_t) (onset + i)] += (i % 2 == 0 ? 0.5f : -0.5f);
            int loudest = onset + 660;
            for (int i = onset + 660; i < onset + 1172; ++i)
                if (std::abs (tones[(size_t) i]) > std::abs (tones[(size_t) loudest])) loudest = i;
            auto bumped = tones;
            const int bump = loudest > onset + 1000 ? onset + 760 : onset + 1060;
            for (int i = -24; i <= 24; ++i)
                bumped[(size_t) (bump + i)] += 0.3f * (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * i / 24.0)) * (tones[(size_t) bump] < 0.0f ? -1.0f : 1.0f);
            auto a = stretch (tones, 0.0, StretchMode::e3Generic, 1.0 / 1.5), b = stretch (bumped, 0.0, StretchMode::e3Generic, 1.0 / 1.5);
            double worst = 0.0;
            for (int k = 0; k < 5; ++k)
            {
                std::complex<double> za, zb;
                for (int i = 30000; i < 38192; ++i)
                {
                    const auto w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * (i - 30000) / 8192.0);
                    const auto e = std::polar (w, -juce::MathConstants<double>::twoPi * hz[k] * i / sr44);
                    za += (double) a[(size_t) i] * e;
                    zb += (double) b[(size_t) i] * e;
                }
                const auto want = -juce::MathConstants<double>::twoPi * hz[k] * 0.5 * (bump - loudest) / sr44;
                worst = juce::jmax (worst, std::abs (std::arg (zb / za * std::polar (1.0, -want))));
            }
            check (worst < juce::degreesToRadians (6.0), "e3: low band restarts from its loudest point after an attack",
                   "worst tone " + juce::String (juce::radiansToDegrees (worst), 1) + " deg off");
        }

        // The mono modes (PSOLA, measured in FL): clicks on time at full level
        // when stretching and squeezing, and PITCH moves the partials but not
        // the spectral envelope (a voice keeps its formants).
        for (const double speed : { 1.0 / 1.5, 1.5 })
        {
            auto y = stretch (click, 0.0, StretchMode::e3Mono, speed);
            bool ok = true;
            juce::String detail;
            const int want = (int) std::lround (22050 / speed);   // the click, at its stretched time
            const auto pk = peakOf (y, want - 2000, want + 2000);
            const int on = firstAbove (y, want - 2000, want + 2000, 0.1f * pk) - want;
            ok = ok && std::abs (on) <= 8 && pk > 0.7f;
            detail << on << " samples, peak " << juce::String (pk, 2);
            check (ok, "e3 mono: click on time at x" + juce::String (1.0 / speed, 3), detail);
        }
        {
            // A 300 Hz pulse train through a 2 kHz resonance (a vowel-like source).
            std::vector<float> vowel ((size_t) n44, 0.0f);
            juce::dsp::IIR::Filter<float> f;
            f.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (sr44, 2000.0, 4.0);
            for (int i = 0; i < n44; ++i)
                vowel[(size_t) i] = f.processSample (i % 147 == 0 ? 1.0f : 0.0f);
            auto level = [&] (const std::vector<float>& v, double hz)   // Goertzel over the middle half
            {
                double s1 = 0, s2 = 0; const double c = 2.0 * std::cos (juce::MathConstants<double>::twoPi * hz / sr44);
                for (int i = n44 / 4; i < n44 * 3 / 4; ++i) { const double s0 = v[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
                return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2);
            };
            auto centroid = [&] (const std::vector<float>& v, double f0)   // of the harmonics from 1 to 5 kHz
            {
                double num = 0, den = 0;
                for (double hz = f0; hz < 5000.0; hz += f0)
                    if (hz > 1000.0) { const auto e = level (v, hz); num += hz * e * e; den += e * e; }
                return num / den;
            };
            const double f0 = sr44 / 147.0, up = f0 * std::pow (2.0, 700.0 / 1200.0);
            auto mono = stretch (vowel, 700.0, StretchMode::e3Mono), generic = stretch (vowel, 700.0, StretchMode::e3Generic);
            const auto c0 = centroid (vowel, f0), cm = centroid (mono, up), cg = centroid (generic, up);
            check (level (mono, up) > 4.0 * level (mono, f0) && std::abs (cm / c0 - 1.0) < 0.15 && cg / c0 > 1.3,
                   "e3 mono: pitch keeps the spectral envelope",
                   "centroid " + juce::String (c0, 0) + " Hz -> mono " + juce::String (cm, 0) + ", generic " + juce::String (cg, 0));
        }

        // Every mode keeps a softly starting sound where FL plays it (no
        // start lock, no attacks: noise swelling in and out, so only the
        // stretcher's own alignment decides). The lag of the output's
        // loudness against the source's at the stretched time, in output
        // samples, measured in FL with the same kind of signal (FL: e3
        // generic -56 / -4 / -8 / -16 at x1.5 / x0.667 / +700 / -700 cents;
        // Stretch -164 / -148 / -100 / -248; Stretch Pro -104 / -36 / -60 /
        // -128; e2 generic -200 / -76 / -116 / -120; e2 transient 56 / 96 /
        // -16 / 132; the mono and speech modes within +-32). The checks lock
        // ours, which are within ~50 samples of FL's (e2 speech x0.667: 84),
        // to 40 samples: a broken alignment moves them by hundreds.
        {
            const int n = 3 * n44;
            juce::Random rnd (5);
            std::vector<float> swell ((size_t) n), env2 ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const double e = 0.3 * std::pow (std::sin (juce::MathConstants<double>::pi * i / 14700.0), 2.0);
                env2[(size_t) i] = (float) (e * e);
                swell[(size_t) i] = (float) e * (rnd.nextFloat() * 2.0f - 1.0f);
            }
            auto smoothed = [] (const std::vector<float>& v)   // 1024-sample mean, every 4th sample
            {
                std::vector<double> out (v.size() / 4);
                std::vector<double> c (v.size() + 1, 0.0);
                for (size_t i = 0; i < v.size(); ++i) c[i + 1] = c[i] + v[i];
                for (size_t k = 0; k < out.size(); ++k)
                {
                    const auto i = (long) k * 4, a = juce::jmax (0L, i - 512), b = juce::jmin ((long) v.size(), i + 512);
                    out[k] = (c[(size_t) b] - c[(size_t) a]) / 1024.0;
                }
                return out;
            };
            auto lagOf = [&] (const std::vector<float>& in, StretchMode mode, double speed, double cents)
            {
                const auto y = stretch (in, cents, mode, speed);
                std::vector<float> y2 (y.size()), ideal (y.size());
                for (size_t t = 0; t < y.size(); ++t)
                {
                    y2[t] = y[t] * y[t];
                    const double p = (double) t * speed;
                    const int i = (int) p;
                    ideal[t] = i + 1 < n ? (float) (env2[(size_t) i] + (p - i) * (env2[(size_t) i + 1] - env2[(size_t) i])) : 0.0f;
                }
                const auto a = smoothed (y2), b = smoothed (ideal);
                const int s0 = 2000, s1 = (int) a.size() - 2000;
                double best = -1; int lag = 0;
                for (int d = -150; d <= 150; ++d)
                {
                    double dot = 0, ea = 0;
                    for (int k = s0; k < s1; ++k) { dot += a[(size_t) (k + d)] * b[(size_t) k]; ea += a[(size_t) (k + d)] * a[(size_t) (k + d)]; }
                    const double v = dot / std::sqrt (ea + 1e-30);
                    if (v > best) { best = v; lag = d * 4; }
                }
                return lag;
            };
            struct Row { StretchMode mode; const char* name; int lags[4]; };
            const Row rows[] = {
                { StretchMode::e3Generic,   "e3 generic",    { -20, 4, 12, 4 } },
                { StretchMode::stretch,     "Stretch",       { -144, -148, -104, -216 } },
                { StretchMode::stretchPro,  "Stretch Pro",   { -76, -56, -44, -84 } },
                { StretchMode::e2Generic,   "e2 generic",    { -168, -36, -84, -56 } },
                { StretchMode::e2Transient, "e2 transient",  { 20, 104, 12, 164 } },
                { StretchMode::e3Mono,      "e3 mono",       { 4, -20, 12, 12 } },
                { StretchMode::e2Mono,      "e2 mono",       { 0, -12, 0, 0 } },
                { StretchMode::e2Speech,    "e2 speech",     { 12, -52, 0, 24 } } };
            const double speeds[] = { std::pow (2.0, -0.585), std::pow (2.0, 0.585), 1.0, 1.0 }, cents[] = { 0.0, 0.0, 700.0, -700.0 };
            for (const auto& r : rows)
            {
                bool ok = true;
                juce::String got;
                for (int c = 0; c < 4; ++c)
                {
                    const int l = lagOf (swell, r.mode, speeds[c], cents[c]);
                    ok = ok && std::abs (l - r.lags[c]) <= 40;
                    got << l << (c < 3 ? " / " : "");
                }
                check (ok, juce::String (r.name) + ": a soft start lines up like FL's", got + " samples (x1.5 / x0.667 / +700 / -700)");
            }
            // The band below e3's split alone (the grains above would hide
            // the vocoder's alignment). FL's bands have their own offsets
            // (measured with the swell brick-wall filtered at 700 Hz: e3
            // generic -196 / -96 / 0 / -160, e2 generic 80 / 128 / 104 / 148;
            // ours within 60 of them there, apart from e3 generic squeezing,
            // ~200 later, kept for real material; this filter adds its own
            // delay).
            const auto low = lowpass (swell, 700.0);
            const Row lowRows[] = { { StretchMode::e3Generic, "e3 generic", { 40, 152, 32, 260 } },
                                    { StretchMode::e2Generic, "e2 generic", { 272, 164, 192, 288 } } };
            for (const auto& r : lowRows)
            {
                bool ok = true;
                juce::String got;
                for (int c = 0; c < 4; ++c)
                {
                    const int l = lagOf (low, r.mode, speeds[c], cents[c]);
                    ok = ok && std::abs (l - r.lags[c]) <= 40;
                    got << l << (c < 3 ? " / " : "");
                }
                check (ok, juce::String (r.name) + ": a soft start lines up (band below the split)", got + " samples (x1.5 / x0.667 / +700 / -700)");
            }
        }

        // Every stretch mode survives extreme and changing ratios.
        {
            juce::Random rnd (3);
            std::vector<float> in ((size_t) n44);
            for (int i = 0; i < n44; ++i) in[(size_t) i] = (i % 9000 < 300 ? 0.9f : 0.2f) * (rnd.nextFloat() * 2.0f - 1.0f);
            bool ok = true;
            for (int m = 0; m <= (int) StretchMode::e2Speech; ++m)
            {
                TimeStretcher st;
                st.configure ((StretchMode) m, sr44);
                st.reset (0.0);
                auto read = [&] (int k, float& l, float& r) { l = r = (k >= 0 && k < n44) ? in[(size_t) k] : 0.0f; };
                for (int i = 0; i < 60000; ++i)
                {
                    const double inc = std::pow (2.0, ((i / 2000) % 9 - 4) * 1.1), tr = std::pow (2.0, ((i / 3100) % 7 - 3) * 1.2);
                    float l, r;
                    st.next (read, inc, tr, 0.0f, l, r);
                    ok = ok && std::isfinite (l) && std::isfinite (r) && std::abs (l) < 50.0f;
                    if (i == 30000) st.reset (1234.0);
                }
            }
            check (ok, "stretcher: all modes finite at extreme, changing ratios");
        }
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
            using juce::AudioProcessor::processBlock;
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
        const std::atomic<int> sampleLength { 0 };
        juce::AudioProcessorValueTreeState apvts (host, nullptr, "P", createParameterLayout (sampleLength));

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
        // The default speed is 416 ticks at 130 BPM: 0.5 Hz (FL's 2 s).
        const auto period = flLfoPeriodT130 ((32950.0f - 200.0f) / 65336.0f);
        check (std::round (period) == 416.0, "LFO speed default = 0.5 Hz",
               juce::String (period, 2) + " ticks = " + juce::String (130.0 / 60.0 * 96.0 / std::round (period), 3) + " Hz");
    }

    std::cout << "\n" << (failures == 0 ? "All tests passed" : juce::String (failures) + " test(s) failed") << "\n";
    return failures == 0 ? 0 : 1;
}
