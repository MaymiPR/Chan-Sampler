// Hosts a built Chan Sampler VST3 the way a DAW does (JUCE's VST3 hosting) and checks what a project depends on:
// the parameter list, save and reload into a new instance (the host's view, the plug-in's own state and the sound),
// the editor, the host changing its sample rate mid-session, offline rendering, and that projects saved by earlier
// releases load unchanged. One source for every plug-in of the suite: each plugin.cmake builds it as <Stem>HostTest
// and registers it with ctest on its VST3 and its saved states.
//
//   <Stem>HostTest <plugin.vst3> [--property <name>=<value> ...] [state-x.y.z.bin ...]
//   <Stem>HostTest <plugin.vst3> --save-state <state-x.y.z.bin>
//
// --save-state writes a project state with every parameter away from its default, plus a .txt beside it with each
// parameter's name and the text this version shows for it. plugins/<Id>/tests/data holds one per release that changed
// a parameter or its reading; the test loads each into the current build and compares the texts. An instrument (no
// audio input) is played a note where an effect is fed noise. --property sets a root property of the plug-in's own
// state in every instance the checks load (a sampler's sample file: --property samplePath=<sine.wav>), so an
// instrument that is silent until it has content plays.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <iostream>

namespace
{
constexpr int bs = 512;

// --property name=value: root properties of the plug-in's own state that every instance starts with.
juce::StringPairArray properties;

// The plug-in's own state (a JUCE plug-in's XML) sits in the VST3 component state, followed by JUCE's private data;
// sets the --property values there.
void applyProperties (juce::AudioPluginInstance& p)
{
    if (properties.size() == 0)
        return;
    juce::MemoryBlock state;
    p.getStateInformation (state);
    auto head = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    auto* comp = head != nullptr ? head->getChildByName ("IComponent") : nullptr;
    if (comp == nullptr)
        return;
    juce::MemoryBlock compData;
    compData.fromBase64Encoding (comp->getAllSubText());
    auto ours = juce::AudioProcessor::getXmlFromBinary (compData.getData(), (int) compData.getSize());
    if (ours == nullptr)
        return;
    const auto oursSize = 8 + (size_t) juce::ByteOrder::littleEndianInt (static_cast<const char*> (compData.getData()) + 4);
    for (auto& key : properties.getAllKeys())
        ours->setAttribute (key, properties[key]);
    juce::MemoryBlock newComp;
    juce::AudioProcessor::copyXmlToBinary (*ours, newComp);
    newComp.append (static_cast<const char*> (compData.getData()) + oursSize, compData.getSize() - oursSize);
    comp->deleteAllTextElements();
    comp->addTextElement (newComp.toBase64Encoding());
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (*head, out);
    p.setStateInformation (out.getData(), (int) out.getSize());
}

std::unique_ptr<juce::AudioPluginInstance> load (const juce::String& path, double rate = 48000.0)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, path);
    if (found.isEmpty()) return nullptr;
    juce::String error;
    auto p = format.createInstanceFromDescription (*found[0], rate, bs, error);
    if (p != nullptr)
    {
        p->setPlayConfigDetails (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(), rate, bs);
        p->prepareToPlay (rate, bs);
        applyProperties (*p);
    }
    return p;
}

bool isInstrument (juce::AudioPluginInstance& p) { return p.getTotalNumInputChannels() == 0; }

// Parameters a project does not restore, which the save and reload checks leave out: the program list a VST3 plug-in
// with factory programs offers the host ("Program": choosing a program sets the other parameters, and a project
// restores those, not the program's index), and the MIDI controllers JUCE's VST3 wrapper offers an instrument as
// parameters ("MIDI CC 0|1" ...: they pass controller changes through and hold no state).
bool notInProject (juce::AudioProcessorParameter& prm)
{
    const auto name = prm.getName (128);
    return name == "Program" || name.startsWith ("MIDI CC ");
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (128) == name) return prm;
    return nullptr;
}

// Renders blocks of fixed noise (an effect) or a held note (an instrument); returns the left channel.
std::vector<float> render (juce::AudioPluginInstance& p, int blocks)
{
    std::vector<float> out;
    const int ch = juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> b (ch, bs);
    juce::Random rng (11);
    for (int i = 0; i < blocks; ++i)
    {
        juce::MidiBuffer m;
        if (i == 0 && isInstrument (p)) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        b.clear();
        if (! isInstrument (p))
            for (int c = 0; c < p.getTotalNumInputChannels(); ++c)
                for (int k = 0; k < bs; ++k) b.setSample (c, k, 0.2f * (rng.nextFloat() * 2.0f - 1.0f));
        p.processBlock (b, m);
        for (int k = 0; k < bs; ++k) out.push_back (b.getSample (0, k));
    }
    return out;
}

float rms (const std::vector<float>& x)
{
    double s = 0;
    for (auto v : x) s += (double) v * v;
    return (float) std::sqrt (s / (double) juce::jmax ((size_t) 1, x.size()));
}

float maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    float d = a.size() == b.size() ? 0.0f : 1.0f;
    for (size_t i = 0; i < juce::jmin (a.size(), b.size()); ++i) d = juce::jmax (d, std::abs (a[i] - b[i]));
    return d;
}

bool allFinite (const std::vector<float>& x)
{
    for (auto v : x)
        if (! std::isfinite (v)) return false;
    return true;
}

// A project state with every parameter away from its default, and the text each one shows (the reference for later
// versions).
int saveState (const juce::String& pluginPath, const juce::File& out)
{
    auto p = load (pluginPath);
    if (p == nullptr) { std::cerr << "cannot load " << pluginPath << "\n"; return 1; }
    int i = 0;
    for (auto* prm : p->getParameters())
        if (prm->isAutomatable() && prm != p->getBypassParameter() && ! notInProject (*prm))
            prm->setValueNotifyingHost ((float) std::fmod (0.137 + 0.618034 * i++, 1.0));
    render (*p, 4);                                         // the processor takes the changes
    juce::MemoryBlock state;
    p->getStateInformation (state);
    juce::StringArray lines;
    for (auto* prm : p->getParameters())
        if (prm->isAutomatable() && ! notInProject (*prm)) lines.add (prm->getName (128) + "\t" + prm->getText (prm->getValue(), 128));
    if (! out.getParentDirectory().createDirectory() || ! out.replaceWithData (state.getData(), state.getSize())
        || ! out.withFileExtension (".txt").replaceWithText (lines.joinIntoString ("\n") + "\n", false, false, "\n"))
        return 1;
    std::cout << "wrote " << out.getFullPathName() << " (" << lines.size() << " parameters)\n";
    return 0;
}

// A state saved by an earlier version: every parameter it listed reads back with the same text (none removed or
// changed; a parameter added since keeps its default).
void checkSavedState (const juce::String& pluginPath, const juce::File& saved, const std::function<void (bool, const juce::String&)>& check)
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
    render (*p, 4);
    int differ = 0, compared = 0;
    for (auto& line : lines)
    {
        const auto name = line.upToFirstOccurrenceOf ("\t", false, false);
        const auto text = line.fromFirstOccurrenceOf ("\t", false, false);
        if (name.isEmpty()) continue;
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
    if (argc < 2)
    {
        std::cerr << "usage: HostTest <plugin.vst3> [--property <name>=<value> ...] [state-x.y.z.bin ...]\n"
                     "       HostTest <plugin.vst3> --save-state <state-x.y.z.bin>\n";
        return 2;
    }
    const juce::String pluginPath (argv[1]);
    juce::StringArray states;
    for (int i = 2; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--property" && i + 1 < argc)
        {
            const juce::String pair (argv[++i]);
            properties.set (pair.upToFirstOccurrenceOf ("=", false, false), pair.fromFirstOccurrenceOf ("=", false, false));
        }
        else
        {
            states.add (argv[i]);
        }
    }
    int failures = 0;
    auto check = [&failures] (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        if (! ok) ++failures;
    };

    auto a = load (pluginPath);
    if (a == nullptr) { std::cerr << "cannot load " << pluginPath << "\n"; return 1; }
    int automatable = 0;
    for (auto* prm : a->getParameters())
        if (prm->isAutomatable()) ++automatable;
    std::cout << "plugin: " << a->getName() << (isInstrument (*a) ? " (instrument)" : " (effect)") << ", parameters "
              << a->getParameters().size() << " (" << automatable << " automatable)\n";
    check (a->getBypassParameter() != nullptr, "the host bypass is the plug-in's own parameter");

    // 1. Sound at the defaults: finite, and not silent
    const auto y0 = render (*a, 40);
    check (allFinite (y0) && rms (y0) > 1.0e-4f, "renders at the defaults (rms " + juce::String (rms (y0), 5) + ")");

    // 2. Save and reload into a new instance: the host's view of every parameter, then the sound
    juce::Random rng (7);
    for (auto* prm : a->getParameters())
        if (prm->isAutomatable() && prm != a->getBypassParameter() && ! notInProject (*prm)) prm->setValueNotifyingHost (rng.nextFloat());
    render (*a, 4);
    juce::MemoryBlock saved;
    a->getStateInformation (saved);
    auto b = load (pluginPath);
    b->setStateInformation (saved.getData(), (int) saved.getSize());
    int mismatches = 0;
    for (int i = 0; i < a->getParameters().size(); ++i)
        if (! notInProject (*a->getParameters()[i])
            && a->getParameters()[i]->getCurrentValueAsText() != b->getParameters()[i]->getCurrentValueAsText())
        {
            ++mismatches;
            std::cout << "      differs: " << a->getParameters()[i]->getName (128) << " = " << a->getParameters()[i]->getCurrentValueAsText()
                      << " / " << b->getParameters()[i]->getCurrentValueAsText() << "\n";
        }
    check (mismatches == 0, "every parameter restored as the host sees it (" + juce::String (mismatches) + " different)");
    auto c = load (pluginPath), d = load (pluginPath);
    c->setStateInformation (saved.getData(), (int) saved.getSize());
    d->setStateInformation (saved.getData(), (int) saved.getSize());
    const auto yc = render (*c, 40), yd = render (*d, 40);
    check (maxDiff (yc, yd) < 1.0e-6f && allFinite (yc), "two instances loaded from one state sound the same (max diff "
                                                         + juce::String (maxDiff (yc, yd)) + ")");

    // 3. The editor opens and closes
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (d->createEditorAndMakeActive());
        check (ed != nullptr && ed->getWidth() > 0 && ed->getHeight() > 0,
               "the editor opens (" + juce::String (ed != nullptr ? ed->getWidth() : 0) + " x " + juce::String (ed != nullptr ? ed->getHeight() : 0) + ")");
    }

    // 4. The host changes its sample rate mid-session
    {
        auto e = load (pluginPath);
        bool fine = true;
        for (double rate : { 96000.0, 44100.0, 48000.0 })
        {
            e->releaseResources();
            e->setPlayConfigDetails (e->getTotalNumInputChannels(), e->getTotalNumOutputChannels(), rate, bs);
            e->prepareToPlay (rate, bs);
            const auto y = render (*e, 200);                   // over 1 s: a wet-only delay is silent at first
            fine = fine && allFinite (y) && rms (y) > 1.0e-4f;
        }
        check (fine, "host rate 96, 44.1 and 48 kHz in one session: every render finite and not silent");
    }

    // 5. Offline rendering (export) gives the same output as playing
    {
        auto live = load (pluginPath), offline = load (pluginPath);
        offline->setNonRealtime (true);
        const auto yl = render (*live, 40), yo = render (*offline, 40);
        check (maxDiff (yl, yo) < 1.0e-6f, "offline render equals the real-time render (max diff " + juce::String (maxDiff (yl, yo)) + ")");
    }

    // 6. Projects saved by earlier releases
    for (auto& stateFile : states)
        checkSavedState (pluginPath, juce::File::getCurrentWorkingDirectory().getChildFile (stateFile), check);

    std::cout << (failures == 0 ? juce::String ("ALL PASSED") : juce::String (failures) + " FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
