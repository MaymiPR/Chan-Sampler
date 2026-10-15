// Hosts the built Chan Sampler.vst3 the way a DAW does (JUCE's VST3 hosting)
// and checks host features: parameter list, automation, MIDI (pitch bend,
// CCs), save / reload across instances and the editor size.
//
//   ChanSamplerHostTest <Chan Sampler.vst3> <sample.wav>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

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
    float freq (const std::vector<float>& x, int from, int to)
    {
        int crossings = 0;
        for (int i = from + 1; i < to; ++i)
            if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
                ++crossings;
        return (float) (crossings * sr / (to - from));
    }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: ChanSamplerHostTest <plugin.vst3> <sample.wav>\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
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
    // one throw-away note so the first comparison starts from the same state as the others
    render (*a, 60, [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 1); });
    render (*a, 20, [] (int i, juce::MidiBuffer& m) { if (i == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
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
        std::cout << "      CC" << cc << " = 20: output " << (diff > 1.0e-4f ? "changes" : "unchanged") << " (max difference " << diff << ")\n";
    }

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
        if (prm->isAutomatable() && prm->getName (100) != "Time stretch mode")
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

    std::cout << (failures == 0 ? "ALL PASSED\n" : juce::String (failures) + " FAILED\n");
    return failures;
}
