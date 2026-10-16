// Offline renderer for null tests against FL Studio renders.
//
//   ChanSamplerRender <sample.wav> <out.wav> <seconds> <params.txt> <notes>
//
// params.txt: one "paramID value" per line (plain, unnormalised values).
// notes: "key:startSec:lengthSec:velocity;..." with FL velocity 0-128.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <iostream>

#include "../Source/PluginProcessor.h"
#include "../Source/FlPreset.h"

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
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), r->sampleRate, (unsigned) buf.getNumChannels(), 32, {}, 0));
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
        std::cerr << "cannot load " << sampleFile.getFullPathName() << "\n";
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

    // Notes: FL velocity v (0-128) arrives as MIDI velocity v * 127 / 128.
    juce::MidiBuffer all;
    for (auto& n : juce::StringArray::fromTokens (noteSpec, ";", {}))
    {
        auto f = juce::StringArray::fromTokens (n, ":", {});
        if (f.size() < 4)
            continue;
        const int key = f[0].getIntValue();
        const int on = (int) std::round (f[1].getDoubleValue() * sampleRate);
        const int off = on + (int) std::round (f[2].getDoubleValue() * sampleRate);
        const auto vel = (juce::uint8) juce::jlimit (1, 127, (int) std::round (f[3].getDoubleValue() * 127.0 / 128.0));
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
    JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4996)
    JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (outFile.createOutputStream().release(),
                                                                          sampleRate, 2, 32, {}, 0));
    JUCE_END_IGNORE_DEPRECATION_WARNINGS
    JUCE_END_IGNORE_WARNINGS_MSVC
    if (writer == nullptr)
    {
        std::cerr << "cannot write " << outFile.getFullPathName() << "\n";
        return 1;
    }
    writer->writeFromAudioSampleBuffer (out, 0, total);
    return 0;
}
