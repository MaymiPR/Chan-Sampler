// Offline renderer for null tests against FL Studio renders, and the
// developer checks that need the whole plugin without a host.
//
//   ChanSamplerRender <sample.wav> <out.wav> <seconds> <params.txt> <notes>
//
// params.txt: one "paramID value" per line (plain, unnormalised values).
// notes: "key:startSec:lengthSec:velocity;..." with FL's note velocity, which
// FL's plugin wrapper passes on unchanged as the MIDI velocity (so 1-127).
//
// Other commands (each described where it is handled below):
//   loadcheck <sample>                 a file's loop points don't switch looping on
//   stretchtest <in> <out> <cents> [mode] [speed]   the stretcher alone on a file
//   tempotest <sample>                 Detect tempo's estimate and FL's ranges
//   reloadtest <a> <b> <copy>          a sample changed on disk reloads, settings kept
//   cliptest <sample> <folder>         copy to the clipboard and paste into another instance
//   statetest <sample>                 save / reload round trip plays identically
//   cuttest <sample>                   cut groups between two instances
//   fftbench                           the stretcher's FFT against JUCE's
//   bench <sample>                     CPU per stretch mode, 32 / 64 voices
//   decode <file> <out-prefix>         what each audio format reader decodes

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <iostream>

#include "../Source/PluginProcessor.h"
#include "../Source/SampleExchange.h"
#include "../Source/TempoDetect.h"
#include "../Source/TimeStretch.h"
#include "../Source/FastFFT.h"
#include "../Source/FlPreset.h"
#include "../Source/Params.h"
#include "../Source/FactoryPresets.h"

// Group test: two instances in one process. A (Cut 1) plays a note at 1 s
// while B (By 1) holds a long note; reports where B goes silent, with A
// processed before B and after B.
//   ChanSamplerRender cuttest <sample.wav>
static int cutTest (const juce::File& sampleFile)
{
    constexpr double sr = 44100.0;
    constexpr int bs = 512;
    struct Clock : juce::AudioPlayHead
    {
        int64_t t = 0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (130.0);
            info.setIsPlaying (true);
            info.setTimeInSamples (t);
            return info;
        }
    };
    for (int order = 0; order < 2; ++order)
    {
        ChanSamplerProcessor a, b;
        Clock clock;
        for (auto* p : { &a, &b })
        {
            p->loadSample (sampleFile);
            p->waitForPendingRebuilds();
            p->rebuildNow();
            p->setPlayHead (&clock);
            p->setPlayConfigDetails (0, 2, sr, bs);
            p->prepareToPlay (sr, bs);
        }
        auto setInt = [] (ChanSamplerProcessor& p, const char* id, float v)
        {
            auto* prm = p.apvts.getParameter (id);
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        };
        setInt (a, pid::cutGroup, 1.0f);
        setInt (b, pid::cutBy, 1.0f);

        const int cutAt = 44100 + 300;   // mid-block
        juce::AudioBuffer<float> ba (2, bs), bb (2, bs);
        int silentFrom = -1, lastLoud = -1;
        for (int pos = 0; pos < 3 * 44100; pos += bs)
        {
            clock.t = pos;
            juce::MidiBuffer ma, mb;
            if (pos == 0)
                mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (cutAt >= pos && cutAt < pos + bs)
                ma.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), cutAt - pos);
            if (order == 0) { a.processBlock (ba, ma); b.processBlock (bb, mb); }
            else            { b.processBlock (bb, mb); a.processBlock (ba, ma); }
            for (int i = 0; i < bs; ++i)
                if (std::abs (bb.getSample (0, i)) > 1.0e-6f)
                    lastLoud = pos + i;
        }
        silentFrom = lastLoud + 1;
        std::cout << (order == 0 ? "A before B" : "B before A") << ": cut at " << cutAt
                  << ", B silent from " << silentFrom << " (fade " << (silentFrom - cutAt) << " samples after the cut)\n";
    }
    return 0;
}

// Save / reload test: random settings, state round trip into a new instance,
// then the same notes through both.
//   ChanSamplerRender statetest <sample.wav>
static int stateTest (const juce::File& sampleFile)
{
    constexpr double sr = 44100.0;
    constexpr int bs = 512;
    juce::Random rng (1234);
    ChanSamplerProcessor a;
    a.loadSample (sampleFile);
    for (auto* p : a.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != pid::stretchMode && ! rp->getParameterID().startsWith ("pc"))
                rp->setValueNotifyingHost (rng.nextFloat());
    a.apvts.state.setProperty ("uiWidth", 777, nullptr);
    juce::MemoryBlock block;
    a.getStateInformation (block);

    ChanSamplerProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    int mismatches = 0;
    for (auto* p : a.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            auto* other = b.apvts.getParameter (rp->getParameterID());
            // compare what the parameter means (switches store on / off)
            if (other == nullptr || other->getCurrentValueAsText() != rp->getCurrentValueAsText()
                || std::abs (other->convertFrom0to1 (other->getValue()) - rp->convertFrom0to1 (rp->getValue())) > 1.0e-4f)
            {
                ++mismatches;
                std::cout << "  mismatch " << rp->getParameterID() << ": " << rp->getValue() << " vs "
                          << (other != nullptr ? other->getValue() : -1.0f) << "\n";
            }
        }
    const bool sampleOk = b.getSampleFile() == a.getSampleFile() && b.getSampleLength() == a.getSampleLength();
    const bool widthOk = (int) b.apvts.state.getProperty ("uiWidth", 0) == 777;

    // Same notes through both instances
    float maxDiff = 0.0f;
    for (auto* p : { &a, &b })
    {
        p->waitForPendingRebuilds();
        p->rebuildNow();
        p->setPlayConfigDetails (0, 2, sr, bs);
        p->prepareToPlay (sr, bs);
    }
    juce::AudioBuffer<float> ba (2, bs), bb (2, bs);
    for (int pos = 0; pos < 44100 * 2; pos += bs)
    {
        juce::MidiBuffer m;
        if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        if (pos == 44032) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        juce::MidiBuffer m2 (m);
        a.processBlock (ba, m);
        b.processBlock (bb, m2);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < bs; ++i)
                maxDiff = juce::jmax (maxDiff, std::abs (ba.getSample (ch, i) - bb.getSample (ch, i)));
    }
    std::cout << "parameters: " << a.getParameters().size() << ", mismatches: " << mismatches
              << "\nsample restored: " << (sampleOk ? "yes" : "NO")
              << "\nwindow width restored: " << (widthOk ? "yes" : "NO")
              << "\nmax audio difference after reload: " << maxDiff << "\n";
    return mismatches == 0 && sampleOk && widthOk && maxDiff < 1.0e-6f ? 0 : 1;
}

int main (int argc, char** argv)
{
    // "loadcheck <sample>": Use loop points off, load, print the loop state
    // (a file's loop points set the markers but must not switch looping on).
    if (argc >= 3 && juce::String (argv[1]) == "loadcheck")
    {
        juce::ScopedJuceInitialiser_GUI init;
        ChanSamplerProcessor p;
        auto* use = p.apvts.getParameter (pid::useLoop);
        use->setValueNotifyingHost (0.0f);
        if (! p.loadSample (juce::File (juce::String (argv[2]))))
            return 1;
        std::cout << "useLoop " << use->getCurrentValueAsText() << ", loopStart " << p.apvts.getParameter (pid::loopStart)->getCurrentValueAsText()
                  << ", loopEnd " << p.apvts.getParameter (pid::loopEnd)->getCurrentValueAsText() << "\n";
        return use->getValue() < 0.5f ? 0 : 2;
    }
    if (argc >= 5 && juce::String (argv[1]) == "stretchtest")
    {
        // stretchtest <in.wav> <out.wav> <cents> [mode index] [speed]: the
        // stretcher alone, at the file's rate (pitch from cents, length / speed).
        juce::ScopedJuceInitialiser_GUI init;
        ChanSamplerProcessor p;
        if (! p.loadSample (juce::File (juce::String (argv[2]))))
            return 1;
        const auto raw = p.getRaw();
        const auto mode = (StretchMode) (argc > 5 ? juce::String (argv[5]).getIntValue() : (int) StretchMode::e3Generic);
        const double speed = argc > 6 ? juce::String (argv[6]).getDoubleValue() : 1.0;
        const double ratio = std::pow (2.0, juce::String (argv[4]).getDoubleValue() / 1200.0);
        const int inFrames = raw->audio.getNumSamples(), outFrames = (int) (inFrames / speed);
        const auto* l = raw->audio.getReadPointer (0);
        const auto* r = raw->audio.getReadPointer (raw->audio.getNumChannels() > 1 ? 1 : 0);
        TimeStretcher st;
        st.configure (mode, raw->sampleRate);
        st.reset (0.0);
        juce::AudioBuffer<float> out (2, outFrames);
        auto read = [&] (int k, float& sl, float& sr) { if (k < 0 || k >= inFrames) { sl = sr = 0; return; } sl = l[k]; sr = r[k]; };
        for (int i = 0; i < outFrames; ++i)
        {
            float a, b;
            st.next (read, ratio, speed, 0.0f, a, b);
            out.setSample (0, i, a);
            out.setSample (1, i, b);
        }
        juce::File f { juce::String (argv[3]) };
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (raw->sampleRate).withNumChannels (2).withBitsPerSample (32));
        w->writeFromAudioSampleBuffer (out, 0, outFrames);
        return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "tempotest")
    {
        // Detect tempo on a file: the estimate and each of FL's ranges.
        juce::ScopedJuceInitialiser_GUI init;
        ChanSamplerProcessor p;
        if (! p.loadSample (juce::File (juce::String (argv[2]))))
            return 1;
        const auto raw = p.getRaw();
        const double ranges[][2] = { { 75, 150 }, { 50, 100 }, { 100, 200 }, { 150, 300 } };
        std::cout << "file " << tempo::format (raw->fileTempo) << " | ";
        for (auto& r : ranges)
            std::cout << r[0] << "-" << r[1] << ": " << tempo::format (tempo::detect (raw->audio, raw->sampleRate, r[0], r[1])) << "  ";
        std::cout << "\n";
        return 0;
    }
    if (argc >= 5 && juce::String (argv[1]) == "reloadtest")
    {
        // Loads a copy of sample A, overwrites the copy with sample B (as an
        // audio editor saving it would) and checks the plugin picks it up with
        // its settings kept.
        juce::ScopedJuceInitialiser_GUI init;
        const juce::File a { juce::String (argv[2]) }, b { juce::String (argv[3]) }, copy { juce::String (argv[4]) };
        a.copyFileTo (copy);
        ChanSamplerProcessor p;
        if (! p.loadSample (copy))
            return 1;
        auto* vol = p.apvts.getParameter (pid::startOffset);
        vol->setValueNotifyingHost (0.3f);
        const int before = p.getSampleLength();
        p.reloadIfFileChanged();
        juce::Thread::sleep (1100);   // a newer modification time
        b.copyFileTo (copy);
        copy.setLastModificationTime (juce::Time::getCurrentTime());
        p.reloadIfFileChanged();      // sees the change, waits for it to settle
        const int waiting = p.getSampleLength();
        p.reloadIfFileChanged();      // reloads
        std::cout << "frames " << before << " -> " << waiting << " -> " << p.getSampleLength() << ", start offset " << vol->getValue() << "\n";
        return before != p.getSampleLength() && waiting == before && std::abs (vol->getValue() - 0.3f) < 1.0e-6f ? 0 : 2;
    }
    if (argc >= 4 && juce::String (argv[1]) == "cliptest")
    {
        // Copies the sample to the system clipboard (file + WAV data), pastes it
        // back into a second instance and checks the audio came through.
        juce::ScopedJuceInitialiser_GUI init;
        ChanSamplerProcessor a, b;
        if (! a.loadSample (juce::File (juce::String (argv[2]))))
            return 1;
        juce::Component owner;
        owner.addToDesktop (0);
        const auto wav = a.getSampleAsWav();
        const bool copied = exchange::copyToClipboard (juce::SystemStats::getEnvironmentVariable ("CLIP_DATA_ONLY", {}).isNotEmpty() ? juce::File() : a.getSampleFile(), wav, owner.getPeer()->getNativeHandle());
        std::cout << "copied " << copied << " (" << wav.getSize() << " bytes of WAV), clipboard has audio " << exchange::clipboardHasAudio() << "\n";
        const auto pasted = exchange::pasteFromClipboard (juce::File (juce::String (argv[3])));
        std::cout << "pasted " << pasted.getFullPathName() << "\n";
        if (! b.loadSample (pasted))
            return 2;
        const auto ra = a.getRaw(), rb = b.getRaw();
        float diff = 0;
        for (int c = 0; c < ra->audio.getNumChannels(); ++c)
            for (int i = 0; i < ra->audio.getNumSamples(); ++i)
                diff = juce::jmax (diff, std::abs (ra->audio.getSample (c, i) - rb->audio.getSample (c, i)));
        std::cout << "frames " << ra->audio.getNumSamples() << " / " << rb->audio.getNumSamples() << ", max difference " << diff << "\n";
        return ra->audio.getNumSamples() == rb->audio.getNumSamples() && diff == 0 ? 0 : 3;
    }
    if (argc >= 3 && juce::String (argv[1]) == "statetest")
    {
        juce::ScopedJuceInitialiser_GUI init;
        return stateTest (juce::File (juce::String (argv[2])));
    }
    if (argc >= 3 && juce::String (argv[1]) == "cuttest")
    {
        juce::ScopedJuceInitialiser_GUI init;
        return cutTest (juce::File (juce::String (argv[2])));
    }

    // "fftbench": the stretcher's FFT (FastFFT) against JUCE's, per size:
    // the largest difference and the time per transform.
    if (argc >= 2 && juce::String (argv[1]) == "fftbench")
    {
        juce::Random rnd (1);
        for (int order = 8; order <= 13; ++order)
        {
            const int N = 1 << order;
            juce::dsp::FFT jf (order);
            FastFFT::Scratch scratch (order);
            FastFFT ff (order, scratch);
            std::vector<std::complex<float>> in ((size_t) N), a ((size_t) N), b ((size_t) N);
            for (auto& x : in) x = { rnd.nextFloat() - 0.5f, rnd.nextFloat() - 0.5f };
            double err = 0, mag = 0;
            for (const bool inv : { false, true })
            {
                jf.perform (in.data(), a.data(), inv);
                ff.perform (in.data(), b.data(), inv);
                for (int i = 0; i < N; ++i) { err = std::max (err, (double) std::abs (a[(size_t) i] - b[(size_t) i])); mag = std::max (mag, (double) std::abs (a[(size_t) i])); }
            }
            const int reps = 4000000 / N;
            auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int r = 0; r < reps; ++r) jf.perform (in.data(), a.data(), r & 1);
            auto t1 = juce::Time::getMillisecondCounterHiRes();
            for (int r = 0; r < reps; ++r) ff.perform (in.data(), b.data(), r & 1);
            auto t2 = juce::Time::getMillisecondCounterHiRes();
            std::cout << "N " << N << "  max error " << juce::String (20.0 * std::log10 (err / mag + 1e-30), 1) << " dB  JUCE "
                      << juce::String ((t1 - t0) * 1000.0 / reps, 2) << " us  fast " << juce::String ((t2 - t1) * 1000.0 / reps, 2) << " us\n";
        }
        return 0;
    }
    // "bench <sample>": CPU load (one core, % of real time) for 32 / 64 looping
    // voices per stretch method, and for heavy automation.
    if (argc >= 3 && juce::String (argv[1]) == "bench")
    {
        juce::ScopedJuceInitialiser_GUI init;
        constexpr double rate = 44100.0;
        constexpr int block = 256, seconds = 10;
        auto run = [&] (int stretchMode, int voices, bool automate) -> double
        {
            ChanSamplerProcessor proc;
            if (! proc.loadSample (juce::File (juce::String (argv[2]))))
                return -1.0;
            auto set = [&proc] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
            set (pid::stretchMode, (float) stretchMode);
            set (pid::useLoop, 1.0f); set (pid::loopStart, 0.1f); set (pid::loopEnd, 0.9f);
            if (stretchMode != 0) set (pid::stretchMul, 1.3f);
            proc.waitForPendingRebuilds();
            proc.rebuildNow();
            proc.setPlayConfigDetails (0, 2, rate, block);
            proc.prepareToPlay (rate, block);
            juce::AudioBuffer<float> buf (2, block);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int pos = 0, b = 0; pos < (int) (seconds * rate); pos += block, ++b)
            {
                juce::MidiBuffer midi;
                if (pos == 0)
                    for (int v = 0; v < voices; ++v)
                        midi.addEvent (juce::MidiMessage::noteOn (1 + v / 32, 36 + v % 48, (juce::uint8) 100), 0);
                if (automate)
                {
                    const auto ph = (float) b * 0.05f;
                    if (auto* only = std::getenv ("BENCH_PARAM"))   // one parameter only (profiling)
                    {
                        set (only, (float) (0.5 + 0.3 * std::sin (ph)) * (juce::String (only) == "fineTune" ? 50.0f : 1.0f));
                        goto automated;
                    }
                    set (pid::cutoff, 0.5f + 0.4f * std::sin (ph));
                    set (pid::pan, 0.8f * std::sin (ph * 1.3f));
                    set (pid::volume, 1.0f + 0.3f * std::sin (ph * 0.7f));
                    set (pid::fineTune, 50.0f * std::sin (ph * 0.9f));
                    set (pid::resonance, 0.3f + 0.2f * std::sin (ph * 1.1f));
                }
                automated:
                buf.clear();
                proc.processBlock (buf, midi);
            }
            return (juce::Time::getMillisecondCounterHiRes() - t0) / (seconds * 10.0);   // % of real time
        };
        const char* names[] = { "Resample", "Stretch", "Stretch Pro", "e3 generic", "e3 mono", "Slice stretch", "Slice map",
                                "Auto", "e2 generic", "e2 transient", "e2 mono", "e2 speech" };
        const auto* onlyMode = std::getenv ("BENCH_MODE");   // one stretch method by index (profiling)
        for (int m = 0; m < (std::getenv ("BENCH_AUTOMATION_ONLY") ? 0 : 12); ++m)
            if (onlyMode == nullptr || juce::String (onlyMode).getIntValue() == m)
            {
                if (auto* counts = std::getenv ("BENCH_VOICES"))   // other voice counts, e.g. "16,32,48,64"
                {
                    for (auto& v : juce::StringArray::fromTokens (counts, ",", {}))
                        std::cout << names[m] << ": " << v << " voices " << juce::String (run (m, v.getIntValue(), false), 1) << "%\n";
                    continue;
                }
                std::cout << names[m] << ": 32 voices " << juce::String (run (m, 32, false), 1) << "%, 64 voices " << juce::String (run (m, 64, false), 1) << "%\n";
            }
        std::cout << "Resample, 16 voices: " << juce::String (run (0, 16, false), 1) << "%, with heavy automation " << juce::String (run (0, 16, true), 1) << "%\n";
        std::cout << "Stretch Pro, 16 voices: " << juce::String (run (2, 16, false), 1) << "%, with heavy automation " << juce::String (run (2, 16, true), 1) << "%\n";
        return 0;
    }

    // "decode": writes what every registered format that reads the file
    // decodes (out-prefix + format name + .wav), to compare decoders.
    if (argc >= 4 && juce::String (argv[1]) == "decode")
    {
        juce::ScopedJuceInitialiser_GUI init;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        const juce::File in { juce::String (argv[2]) };
        for (int i = 0; i < fm.getNumKnownFormats(); ++i)
        {
            auto* fmt = fm.getKnownFormat (i);
            std::unique_ptr<juce::AudioFormatReader> r (fmt->createReaderFor (in.createInputStream().release(), true));
            if (r == nullptr)
                continue;
            juce::AudioBuffer<float> buf ((int) r->numChannels, (int) r->lengthInSamples);
            r->read (&buf, 0, (int) r->lengthInSamples, 0, true, true);
            const juce::File out { juce::String (argv[3]) + fmt->getFormatName().removeCharacters (" ") + ".wav" };
            out.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
            auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (r->sampleRate).withNumChannels (buf.getNumChannels()).withBitsPerSample (32));
            w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
            std::cout << fmt->getFormatName() << ": " << r->lengthInSamples << " frames at " << r->sampleRate << "\n";
        }
        return 0;
    }

    if (argc < 6)
    {
        std::cerr << "usage: ChanSamplerRender sample.wav out.wav seconds params.txt notes\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI init;
    const juce::File sampleFile { juce::String (argv[1]) };
    const juce::File outFile { juce::String (argv[2]) };
    const double seconds = juce::String (argv[3]).getDoubleValue();
    const juce::File paramFile { juce::String (argv[4]) };
    const juce::String noteSpec (argv[5]);
    const double sampleRate = std::getenv ("RENDER_SR") ? juce::String (std::getenv ("RENDER_SR")).getDoubleValue() : 44100.0;
    constexpr int blockSize = 512;

    ChanSamplerProcessor proc;

    // Load first so embedded loop points cannot override explicit settings.
    // An FL Sampler preset (.fst) brings its own sample and settings.
    if (flpreset::isPresetFile (sampleFile))
    {
        const auto r = flpreset::import (proc, sampleFile);
        if (! r.ok || r.message.isNotEmpty())
            std::cerr << r.message << "\n";
        if (! r.ok)
            return 1;
    }
    else if (! proc.loadSample (sampleFile))
    {
        std::cerr << proc.getLoadError() << "\n";
        return 1;
    }

    juce::StringArray lines;
    paramFile.readLines (lines);
    for (auto& line : lines)
    {
        const auto id = line.upToFirstOccurrenceOf (" ", false, false).trim();
        if (id.isEmpty())
            continue;
        auto* p = proc.apvts.getParameter (id);
        if (p == nullptr)
        {
            std::cerr << "unknown parameter " << id << "\n";
            return 1;
        }
        p->setValueNotifyingHost (p->convertTo0to1 (line.fromFirstOccurrenceOf (" ", false, false).getFloatValue()));
    }
    // RENDER_FACTORY="name": a factory preset (on top of the params file)
    if (auto* f = std::getenv ("RENDER_FACTORY"))
    {
        bool found = false;
        for (auto& fp : factoryPresets())
            if (juce::String (fp.name) == f) { applyFactoryPreset (proc, fp); found = true; }
        if (! found) { std::cerr << "no factory preset " << f << "\n"; return 1; }
    }
    proc.waitForPendingRebuilds();
    proc.rebuildNow();

    // Host tempo (FL's test projects run at 130 BPM); override with RENDER_BPM.
    struct FixedTempo : juce::AudioPlayHead
    {
        double bpm = 130.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            return info;
        }
    } tempo;
    if (auto* env = std::getenv ("RENDER_BPM"))
        tempo.bpm = juce::String (env).getDoubleValue();
    proc.setPlayHead (&tempo);
    proc.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    // Notes: FL's plugin wrapper sends a note's velocity unchanged as the MIDI
    // velocity (measured; SamplerEngine reads it so), so FL's 128 can't reach
    // a plugin: it is clamped to 127.
    juce::MidiBuffer all;
    for (auto& n : juce::StringArray::fromTokens (noteSpec, ";", {}))
    {
        auto f = juce::StringArray::fromTokens (n, ":", {});
        if (f.size() < 4)
            continue;
        const int key = f[0].getIntValue();
        const int on = (int) std::round (f[1].getDoubleValue() * sampleRate);
        const int off = on + (int) std::round (f[2].getDoubleValue() * sampleRate);
        const auto vel = (juce::uint8) juce::jlimit (1, 127, (int) std::round (f[3].getDoubleValue()));
        all.addEvent (juce::MidiMessage::noteOn (1, key, vel), on);
        all.addEvent (juce::MidiMessage::noteOff (1, key), off);
    }

    const int total = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> out (2, total);
    juce::AudioBuffer<float> block (2, blockSize);
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int n = juce::jmin (blockSize, total - pos);
        block.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        for (const auto m : all)
            if (m.samplePosition >= pos && m.samplePosition < pos + n)
                midi.addEvent (m.getMessage(), m.samplePosition - pos);
        proc.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, block, ch, 0, n);
    }

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = outFile.createOutputStream();
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (32));
    if (writer == nullptr)
    {
        std::cerr << "cannot write " << outFile.getFullPathName() << "\n";
        return 1;
    }
    writer->writeFromAudioSampleBuffer (out, 0, total);
    return 0;
}
