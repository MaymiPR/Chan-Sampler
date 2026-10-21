// Hosts the built Chan Sampler.vst3 the way a DAW does (JUCE's VST3 hosting)
// and checks host features: parameter list, automation, MIDI (pitch bend,
// CCs), save / reload across instances, the editor size, the host changing
// its sample rate mid-session and offline (non-realtime) rendering, and that
// projects saved by earlier versions load unchanged.
//
//   ChanSamplerHostTest <Chan Sampler.vst3> <sample.wav> [saved-state.bin...]
//   ChanSamplerHostTest <Chan Sampler.vst3> --save-state <state.bin>
//
// --save-state writes a project state with every parameter away from its
// default (no sample path), plus state.txt listing each parameter's name and
// its text as this version shows it. Tests/data holds one for each release
// that changed the parameters; the test loads each into the current build and
// compares the texts.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <thread>

namespace
{
    constexpr double sr = 44100.0;
    constexpr int bs = 512;

    std::unique_ptr<juce::AudioPluginInstance> load (const juce::String& path)
    {
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, path);
        if (found.isEmpty())
            return nullptr;
        juce::String error;
        auto p = format.createInstanceFromDescription (*found[0], sr, bs, error);
        if (p != nullptr)
        {
            p->setPlayConfigDetails (0, 2, sr, bs);
            p->prepareToPlay (sr, bs);
        }
        return p;
    }

    // The plugin's own state XML inside the host's VST3 state.
    std::unique_ptr<juce::XmlElement> pluginXml (juce::AudioPluginInstance& p)
    {
        juce::MemoryBlock state;
        p.getStateInformation (state);
        auto head = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        juce::MemoryBlock compData;
        compData.fromBase64Encoding (head->getChildByName ("IComponent")->getAllSubText());
        return juce::AudioProcessor::getXmlFromBinary (compData.getData(), (int) compData.getSize());
    }

    // The VST3 component state holds the plugin's own XML; point it at a sample.
    void setStateAttribute (juce::AudioPluginInstance& p, const juce::String& attr, const juce::var& value)
    {
        juce::MemoryBlock state;
        p.getStateInformation (state);
        auto head = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        auto* comp = head->getChildByName ("IComponent");
        juce::MemoryBlock compData;
        compData.fromBase64Encoding (comp->getAllSubText());
        auto ours = juce::AudioProcessor::getXmlFromBinary (compData.getData(), (int) compData.getSize());
        const auto oursSize = 8 + (size_t) juce::ByteOrder::littleEndianInt (static_cast<const char*> (compData.getData()) + 4);
        ours->setAttribute (attr, value.toString());
        juce::MemoryBlock newComp;
        juce::AudioProcessor::copyXmlToBinary (*ours, newComp);
        newComp.append (static_cast<const char*> (compData.getData()) + oursSize, compData.getSize() - oursSize);   // JUCE's private data
        comp->deleteAllTextElements();
        comp->addTextElement (newComp.toBase64Encoding());
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*head, out);
        p.setStateInformation (out.getData(), (int) out.getSize());
    }

    juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
    {
        for (auto* prm : p.getParameters())
            if (prm->getName (100) == name)
                return prm;
        return nullptr;
    }

    // Renders blocks; onBlock (blockIndex, midi) may add events or change parameters.
    std::vector<float> render (juce::AudioPluginInstance& p, int blocks, std::function<void (int, juce::MidiBuffer&)> onBlock)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> b (2, bs);
        for (int i = 0; i < blocks; ++i)
        {
            juce::MidiBuffer m;
            onBlock (i, m);
            b.clear();
            p.processBlock (b, m);
            for (int k = 0; k < bs; ++k)
                out.push_back (b.getSample (0, k));
        }
        return out;
    }

    float rms (const std::vector<float>& x, int from, int to)
    {
        double s = 0;
        for (int i = from; i < to; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
        return (float) std::sqrt (s / juce::jmax (1, to - from));
    }

    // Zero-crossing frequency estimate
    float freq (const std::vector<float>& x, int from, int to, double rate = sr)
    {
        int crossings = 0;
        for (int i = from + 1; i < to; ++i)
            if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
                ++crossings;
        return (float) (crossings * rate / (to - from));
    }

    float maxDiff (const std::vector<float>& a, const std::vector<float>& b)
    {
        float d = 0;
        for (size_t i = 0; i < juce::jmin (a.size(), b.size()); ++i)
            d = juce::jmax (d, std::abs (a[i] - b[i]));
        return d;
    }

    // A project state with every parameter away from its default, and the text
    // each one shows (the reference for later versions).
    int saveState (const juce::String& pluginPath, const juce::File& out)
    {
        auto p = load (pluginPath);
        if (p == nullptr) { std::cerr << "cannot load plugin\n"; return 1; }
        int i = 0;
        for (auto* prm : p->getParameters())
            if (prm->isAutomatable())
                prm->setValueNotifyingHost ((float) std::fmod (0.137 + 0.618034 * i++, 1.0));
        render (*p, 4, [] (int, juce::MidiBuffer&) {});   // the processor takes the changes
        setStateAttribute (*p, "samplePath", juce::String());
        juce::MemoryBlock state;
        p->getStateInformation (state);
        juce::StringArray lines;
        for (auto* prm : p->getParameters())
            if (prm->isAutomatable())
                lines.add (prm->getName (128) + "\t" + prm->getText (prm->getValue(), 128));
        if (! out.replaceWithData (state.getData(), state.getSize())
            || ! out.withFileExtension (".txt").replaceWithText (lines.joinIntoString ("\n") + "\n", false, false, "\n"))
            return 1;
        std::cout << "wrote " << out.getFullPathName() << " (" << lines.size() << " parameters)\n";
        return 0;
    }

    // A state saved by an earlier version: every parameter it listed reads back
    // with the same text (none removed or changed; new ones keep defaults).
    void checkSavedState (const juce::String& pluginPath, const juce::File& saved, std::function<void (bool, const juce::String&)> check)
    {
        juce::MemoryBlock state;
        juce::StringArray lines;
        saved.withFileExtension (".txt").readLines (lines);
        auto p = load (pluginPath);
        if (! saved.loadFileAsData (state) || lines.isEmpty() || p == nullptr)
        {
            check (false, "saved state " + saved.getFileName() + " and its .txt readable");
            return;
        }
        p->setStateInformation (state.getData(), (int) state.getSize());
        render (*p, 4, [] (int, juce::MidiBuffer&) {});
        int differ = 0, compared = 0;
        for (auto& line : lines)
        {
            const auto name = line.upToFirstOccurrenceOf ("\t", false, false);
            const auto text = line.fromFirstOccurrenceOf ("\t", false, false);
            if (name.isEmpty())
                continue;
            ++compared;
            auto* prm = findParam (*p, name);
            const auto now = prm != nullptr ? prm->getText (prm->getValue(), 128) : juce::String ("(removed)");
            if (now != text)
            {
                ++differ;
                std::cout << "      " << name << ": saved \"" << text << "\", now \"" << now << "\"\n";
            }
        }
        check (differ == 0, "a project saved by " + saved.getFileNameWithoutExtension().fromFirstOccurrenceOf ("state-", false, false)
                            + " loads unchanged (" + juce::String (compared) + " parameters, " + juce::String (differ) + " different)");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc == 4 && juce::String (argv[2]) == "--save-state")
        return saveState (argv[1], juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]));
    if (argc < 3)
    {
        std::cerr << "usage: ChanSamplerHostTest <plugin.vst3> <sample.wav> [saved-state.bin...]\n"
                     "       ChanSamplerHostTest <plugin.vst3> --save-state <state.bin>\n";
        return 2;
    }
    const juce::String pluginPath (argv[1]), samplePath (argv[2]);
    int failures = 0;
    auto check = [&failures] (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        if (! ok) ++failures;
    };

    auto a = load (pluginPath);
    if (a == nullptr) { std::cerr << "cannot load plugin\n"; return 1; }
    int automatable = 0;
    for (auto* prm : a->getParameters())
        if (prm->isAutomatable()) ++automatable;
    std::cout << "plugin: " << a->getName() << ", parameters " << a->getParameters().size()
              << " (" << automatable << " automatable)\n";
    setStateAttribute (*a, "samplePath", samplePath);

    // 1. Automation: the host turns the volume down half-way through a note.
    auto* vol = findParam (*a, "Volume multiplier");
    check (vol != nullptr && vol->isAutomatable(), "volume is exposed as an automatable parameter");
    const int blocks = (int) (2.0 * sr / bs);
    auto y = render (*a, blocks, [&] (int i, juce::MidiBuffer& m)
    {
        if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        if (i == blocks / 2 && vol != nullptr) vol->setValueNotifyingHost (vol->getValue() * 0.5f);
    });
    const auto before = rms (y, 4000, blocks / 2 * bs - 100), after = rms (y, blocks / 2 * bs + 4000, (int) y.size() - 4000);
    check (after < before * 0.8f, "volume automation changes the level mid-note (" + juce::String (before, 4) + " -> " + juce::String (after, 4) + ")");
    vol->setValueNotifyingHost (vol->getDefaultValue());
    render (*a, 20, [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });

    // 2. MIDI: pitch bend (default range 2 semitones) and controllers.
    y = render (*a, blocks, [&] (int i, juce::MidiBuffer& m)
    {
        if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        if (i == blocks / 4) m.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
    });
    // the 2 s sine at the root key: compare 0.05-0.45 s with 0.55-0.95 s (bent)
    const auto f0 = freq (y, 2200, blocks / 4 * bs - 2200), f1 = freq (y, blocks / 4 * bs + 2200, blocks / 2 * bs - 2200);
    std::cout << "      levels " << rms (y, 2000, blocks / 2 * bs - 100) << " / " << rms (y, blocks / 2 * bs + 2000, (int) y.size() - 2000) << "\n";
    std::cout << "      pitch bend: " << f0 << " Hz -> " << f1 << " Hz (ratio " << f1 / f0 << ", 2 semitones = 1.122)\n";
    check (std::abs (f1 / f0 - 1.1225f) < 0.02f, "pitch bend reaches the plugin");
    render (*a, 20, [] (int i, juce::MidiBuffer& m) { if (i == 0) { m.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0); m.addEvent (juce::MidiMessage::allNotesOff (1), 0); } });
    render (*a, 300, [] (int, juce::MidiBuffer&) {});   // let the bent note die away
    // Other CCs are ignored (hosts map them to parameters). Without the volume
    // envelope a note plays to the sample's end, as in FL, so each note here
    // overlaps the next two: two throw-away notes first, so every comparison
    // starts with the same notes still sounding.
    for (int k = 0; k < 2; ++k)
    {
        render (*a, 60, [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1); });
        render (*a, 20, [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
    }
    juce::StringArray changedBy;
    for (int cc : { 1, 7, 10, 11, 64 })
    {
        auto y0 = render (*a, 60, [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1); });
        render (*a, 20, [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
        auto y1 = render (*a, 60, [cc] (int i, juce::MidiBuffer& m)
        {
            if (i == 0) { m.addEvent (juce::MidiMessage::controllerEvent (1, cc, 20), 0); m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1); }
        });
        render (*a, 20, [cc] (int i, juce::MidiBuffer& m) { if (i == 0) { m.addEvent (juce::MidiMessage::controllerEvent (1, cc, cc == 7 ? 100 : 0), 0); m.addEvent (juce::MidiMessage::allNotesOff (1), 0); } });
        float diff = 0;
        for (size_t i = 0; i < y0.size(); ++i) diff = juce::jmax (diff, std::abs (y0[i] - y1[i]));
        if (diff > 1.0e-4f)
            changedBy.add ("CC" + juce::String (cc));
    }
    check (changedBy.isEmpty(), "CC1, 7, 10, 11 and 64 (pedal up) leave the sound alone"
                                + (changedBy.isEmpty() ? juce::String() : " (changed by " + changedBy.joinIntoString (", ") + ")"));

    // Sustain pedal: a note released while the pedal is down keeps sounding
    // (volume envelope on, so a note-off would otherwise release it).
    {
        auto* envOn = findParam (*a, "Volume envelope");
        if (envOn != nullptr) envOn->setValueNotifyingHost (1.0f);
        auto yp = render (*a, 120, [] (int i, juce::MidiBuffer& m)
        {
            if (i == 0) { m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0); m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1); }
            if (i == 20) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            if (i == 80) m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
        });
        const auto held = rms (yp, 40 * bs, 78 * bs), afterUp = rms (yp, 100 * bs, 120 * bs);
        std::cout << "      sustain: level while held " << held << ", after pedal up " << afterUp << "\n";
        check (held > 1.0e-3f && afterUp < held * 0.5f, "sustain pedal holds a released note until the pedal is lifted");
        if (envOn != nullptr) envOn->setValueNotifyingHost (0.0f);
        render (*a, 20, [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
    }

    // 3. Save / reload into a new instance: every parameter, then the sound
    //    with audible settings, then the editor size the user left it at.
    juce::Random rng (7);
    for (auto* prm : a->getParameters())
        if (prm->isAutomatable())
            prm->setValueNotifyingHost (rng.nextFloat());
    juce::MemoryBlock saved;
    a->getStateInformation (saved);
    auto b = load (pluginPath);
    b->setStateInformation (saved.getData(), (int) saved.getSize());
    int mismatches = 0;
    for (int i = 0; i < a->getParameters().size(); ++i)
        if (! a->getParameters()[i]->getName (100).startsWith ("MIDI CC")   // JUCE's CC pass-through, not state
            && a->getParameters()[i]->getCurrentValueAsText() != b->getParameters()[i]->getCurrentValueAsText())
        {
            ++mismatches;
            std::cout << "      differs: " << a->getParameters()[i]->getName (100) << " = " << a->getParameters()[i]->getCurrentValueAsText()
                      << " / " << b->getParameters()[i]->getCurrentValueAsText() << "\n";
        }
    // The host's cached values miss changes the plugin makes itself (the loop
    // END clamp), so also compare the plugin's own saved values.
    {
        auto xa = pluginXml (*a), xb = pluginXml (*b);
        int pluginMismatches = 0;
        for (auto* pa : xa->getChildWithTagNameIterator ("PARAM"))
            if (auto* pb = xb->getChildByAttribute ("id", pa->getStringAttribute ("id")))
                if (std::abs (pa->getDoubleAttribute ("value") - pb->getDoubleAttribute ("value")) > 1.0e-6)
                {
                    ++pluginMismatches;
                    std::cout << "      plugin-side differs: " << pa->getStringAttribute ("id") << " " << pa->getStringAttribute ("value")
                              << " / " << pb->getStringAttribute ("value") << "\n";
                }
        check (pluginMismatches == 0, "plugin-side state identical after reload (" + juce::String (pluginMismatches) + " different)");
    }
    check (mismatches == 0, "all parameters restored as the host sees them (" + juce::String (mismatches) + " different)");

    auto c = load (pluginPath);
    setStateAttribute (*c, "samplePath", samplePath);
    for (auto [name, value] : { std::pair<const char*, float> { "Pitch shift", 0.6f }, { "Volume multiplier", 0.4f }, { "Panning offset", 0.3f }, { "Filter frequency (modulation X)", 0.7f } })
        if (auto* prm = findParam (*c, name)) prm->setValueNotifyingHost (value);
    setStateAttribute (*c, "uiWidth", 700);
    c->getStateInformation (saved);
    auto d = load (pluginPath);
    d->setStateInformation (saved.getData(), (int) saved.getSize());
    auto notes = [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); };
    auto yc = render (*c, 100, notes), yd = render (*d, 100, notes);
    float diff = 0;
    for (size_t i = 0; i < yc.size(); ++i) diff = juce::jmax (diff, std::abs (yc[i] - yd[i]));
    check (diff < 1.0e-5f && rms (yd, 0, (int) yd.size()) > 1.0e-3f,
           "reloaded instance plays the sample identically (level " + juce::String (rms (yd, 0, (int) yd.size()), 4) + ", max diff " + juce::String (diff) + ")");
    std::unique_ptr<juce::AudioProcessorEditor> ed (d->createEditorAndMakeActive());
    check (ed != nullptr && ed->getWidth() == 700, "editor opens at the saved size (" + juce::String (ed != nullptr ? ed->getWidth() : 0) + " wide)");
    if (ed != nullptr) { d->editorBeingDeleted (ed.get()); ed.reset(); }

    // 4. The host changes its sample rate mid-session: the sample keeps its
    //    pitch and plays (the file is converted to each new rate).
    {
        auto e = load (pluginPath);
        setStateAttribute (*e, "samplePath", samplePath);
        auto note = [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); };
        auto off = [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); };
        const auto y44 = render (*e, 40, note);
        render (*e, 20, off);
        const auto f44 = freq (y44, 1000, 18000, 44100.0);
        for (double rate : { 96000.0, 48000.0 })
        {
            e->releaseResources();
            e->setPlayConfigDetails (0, 2, rate, bs);
            e->prepareToPlay (rate, bs);
            const int blocksAtRate = (int) (40 * rate / 44100.0);
            const auto yr = render (*e, blocksAtRate, note);
            render (*e, 20, off);
            const auto f = freq (yr, (int) (1000 * rate / 44100.0), (int) (18000 * rate / 44100.0), rate);
            check (f44 > 20.0f && std::abs (f / f44 - 1.0f) < 0.01f,
                   "host rate 44.1 -> " + juce::String (rate / 1000.0, 1) + " kHz keeps the pitch (" + juce::String (f44, 1) + " -> " + juce::String (f, 1) + " Hz)");
        }
    }

    // 5. Offline rendering (export / bounce) right after a change: the first
    //    block already uses the rebuilt sample (here Reverse, a precomputed
    //    effect rebuilt in the background).
    {
        auto note = [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); };
        auto ref = load (pluginPath);
        setStateAttribute (*ref, "samplePath", samplePath);
        const auto yPlain = render (*ref, 60, note);
        render (*ref, 20, [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
        if (auto* rev = findParam (*ref, "Reverse")) rev->setValueNotifyingHost (1.0f);
        render (*ref, 4, [] (int, juce::MidiBuffer&) {});   // the processor takes the change
        juce::MemoryBlock project;
        ref->getStateInformation (project);
        // The reference: loading a project builds its sample while it loads,
        // so there is no background rebuild to wait for.
        auto loaded = load (pluginPath);
        loaded->setStateInformation (project.getData(), (int) project.getSize());
        const auto yRef = render (*loaded, 60, note);
        check (maxDiff (yRef, yPlain) > 0.01f, "the reference plays the sample reversed");

        auto off = load (pluginPath);
        setStateAttribute (*off, "samplePath", samplePath);
        auto* rev = findParam (*off, "Reverse");
        off->setNonRealtime (true);
        if (rev != nullptr) rev->setValueNotifyingHost (1.0f);
        const auto yOff = render (*off, 60, note);   // straight away
        check (rev != nullptr && maxDiff (yRef, yOff) < 1.0e-6f && rms (yOff, 0, (int) yOff.size()) > 1.0e-3f,
               "offline render right after a precomputed change uses the new sample (max diff " + juce::String (maxDiff (yRef, yOff)) + ")");

        // ... and right after a project is loaded
        auto fresh = load (pluginPath);
        fresh->setNonRealtime (true);
        fresh->setStateInformation (project.getData(), (int) project.getSize());
        const auto yFresh = render (*fresh, 60, note);
        check (maxDiff (yRef, yFresh) < 1.0e-6f, "offline render right after loading a project matches (max diff " + juce::String (maxDiff (yRef, yFresh)) + ")");
    }

    // 6. A stretch mode chosen while the host plays on its own audio thread:
    //    the plugin makes its stretchers off the audio thread (when the change
    //    arrives on the message thread, else on its timer), and then an octave
    //    up keeps the sample's length instead of halving it as Resample does.
    {
        auto e = load (pluginPath);
        setStateAttribute (*e, "samplePath", samplePath);
        auto onAudioThread = [&e] (int blocks, std::function<void (int, juce::MidiBuffer&)> onBlock)
        {
            std::vector<float> y;
            std::thread audio ([&] { y = render (*e, blocks, onBlock); });
            audio.join();
            return y;
        };
        auto lastSound = [] (const std::vector<float>& y)
        {
            for (int i = (int) y.size() - 1; i >= 0; --i)
                if (std::abs (y[(size_t) i]) > 1.0e-3f)
                    return (float) i / (float) sr;
            return 0.0f;
        };
        auto octaveUp = [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0); };
        const int threeSeconds = (int) (3.0 * sr / bs);
        const auto resampled = lastSound (onAudioThread (threeSeconds, octaveUp));
        auto* mode = findParam (*e, "Stretch method");
        if (mode != nullptr)
            mode->setValueNotifyingHost (1.0f / 11.0f);   // "Stretch", the second of 12 methods
        onAudioThread (4, [] (int, juce::MidiBuffer&) {});   // the change reaches the plugin on the audio thread
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // its timer runs
        const auto stretched = lastSound (onAudioThread (threeSeconds, octaveUp));
        check (mode != nullptr && mode->getCurrentValueAsText() == "Stretch" && std::abs (resampled - 1.0f) < 0.1f && std::abs (stretched - 2.0f) < 0.1f,
               "a stretch mode chosen while playing plays stretched (an octave up lasts " + juce::String (stretched, 2)
               + " s, resampled " + juce::String (resampled, 2) + " s)");
    }

    // 7. Projects saved by earlier versions (Tests/data/state-*.bin), in new
    // instances with no sample: their loop position texts must not follow the
    // sample loaded above (each instance shows its own).
    for (int i = 3; i < argc; ++i)
        checkSavedState (pluginPath, juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]), check);

    std::cout << (failures == 0 ? "ALL PASSED\n" : juce::String (failures) + " FAILED\n");
    return failures;
}
