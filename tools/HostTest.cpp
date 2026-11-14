// Hosts a built Chan Sampler VST3 the way a DAW does (JUCE's VST3 hosting) and checks what a project depends on:
// the parameter list, the host bypass, parameter automation while rendering, MIDI notes and controllers (an
// instrument), save and reload into a new instance (the host's view, the plug-in's own state and the sound), the
// editor, the host changing its sample rate mid-session, offline rendering, and that projects saved by earlier
// releases load unchanged. One source for every plug-in of the suite: each plugin.cmake builds it as <Stem>HostTest
// and registers it with ctest on its VST3 and its saved states.
//
//   <Stem>HostTest <plugin.vst3> [--property <name>=<value> ...] [state-x.y.z.bin ...]
//   <Stem>HostTest <plugin.vst3> --save-state <state-x.y.z.bin>
//
// --save-state writes a project state with every parameter away from its default, plus a .txt beside it with each
// parameter's name and the text this version shows for it. The plug-in's tests/data holds one per release that changed
// a parameter or its reading; the test loads each into the current build and compares the texts. An instrument (no
// audio input) is played a note where an effect is fed noise. --property sets a root property of the plug-in's own
// state in every instance the checks load (a sampler's sample file: --property samplePath=<sine.wav>), so an
// instrument that is silent until it has content plays; the test also checks that each value survives save and reload.
//
// Every check prints PASS or FAIL; a check that does not apply to this plug-in prints SKIP with the reason (the MIDI
// checks for an effect, the bypass check for an instrument). Exit code 0 only when no check failed.
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

// The plug-in's own state (a JUCE plug-in's XML) sits in the VST3 component state, followed by JUCE's private data:
// the host's state document, the component element in it and the plug-in's own XML.
struct OwnState
{
    std::unique_ptr<juce::XmlElement> head, ours;
    juce::XmlElement* comp = nullptr;
    juce::MemoryBlock compData;
};

// false when the state has another form
bool readOwnState (juce::AudioPluginInstance& p, OwnState& s)
{
    juce::MemoryBlock state;
    p.getStateInformation (state);
    s.head = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
    s.comp = s.head != nullptr ? s.head->getChildByName ("IComponent") : nullptr;
    if (s.comp == nullptr)
        return false;
    s.compData.fromBase64Encoding (s.comp->getAllSubText());
    s.ours = juce::AudioProcessor::getXmlFromBinary (s.compData.getData(), (int) s.compData.getSize());
    return s.ours != nullptr;
}

// Sets the --property values in the plug-in's own state.
void applyProperties (juce::AudioPluginInstance& p)
{
    if (properties.size() == 0)
        return;
    OwnState s;
    if (! readOwnState (p, s))
        return;
    const auto oursSize = 8 + (size_t) juce::ByteOrder::littleEndianInt (static_cast<const char*> (s.compData.getData()) + 4);
    for (auto& key : properties.getAllKeys())
        s.ours->setAttribute (key, properties[key]);
    juce::MemoryBlock newComp;
    juce::AudioProcessor::copyXmlToBinary (*s.ours, newComp);
    newComp.append (static_cast<const char*> (s.compData.getData()) + oursSize, s.compData.getSize() - oursSize);
    s.comp->deleteAllTextElements();
    s.comp->addTextElement (newComp.toBase64Encoding());
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (*s.head, out);
    p.setStateInformation (out.getData(), (int) out.getSize());
}

// A new instance, prepared; withProperties = false leaves the --property values out (an instance that takes a saved
// state and must show what the state holds).
std::unique_ptr<juce::AudioPluginInstance> load (const juce::String& path, double rate = 48000.0, bool withProperties = true)
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
        if (withProperties) applyProperties (*p);
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

// The parameters a host automates and a project saves: automatable, not the host bypass, not the ones above.
std::vector<juce::AudioProcessorParameter*> projectParams (juce::AudioPluginInstance& p)
{
    std::vector<juce::AudioProcessorParameter*> out;
    for (auto* prm : p.getParameters())
        if (prm->isAutomatable() && prm != p.getBypassParameter() && ! notInProject (*prm)) out.push_back (prm);
    return out;
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (128) == name) return prm;
    return nullptr;
}

using OnBlock = std::function<void (int block, juce::MidiBuffer&)>;

// The default events of a render: an instrument is played one held note from the first sample.
void heldNote (juce::AudioPluginInstance& p, int block, juce::MidiBuffer& m)
{
    if (block == 0 && isInstrument (p)) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
}

// One block of the fixed noise an effect is fed: the same sequence in every render, which starts it from Random (11).
void fillNoise (juce::AudioBuffer<float>& b, juce::Random& rng, int inputs)
{
    for (int c = 0; c < inputs; ++c)
        for (int k = 0; k < bs; ++k) b.setSample (c, k, 0.2f * (rng.nextFloat() * 2.0f - 1.0f));
}

// Renders blocks of fixed noise (an effect) or a held note (an instrument); returns the left channel. onBlock, when
// given, runs before each block and adds the events or changes the parameters itself (no note is added for it).
std::vector<float> render (juce::AudioPluginInstance& p, int blocks, const OnBlock& onBlock = {})
{
    std::vector<float> out;
    const int ch = juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> b (ch, bs);
    juce::Random rng (11);
    for (int i = 0; i < blocks; ++i)
    {
        juce::MidiBuffer m;
        if (onBlock) onBlock (i, m);
        else heldNote (p, i, m);
        b.clear();
        if (! isInstrument (p)) fillNoise (b, rng, p.getTotalNumInputChannels());
        p.processBlock (b, m);
        for (int k = 0; k < bs; ++k) out.push_back (b.getSample (0, k));
    }
    return out;
}

// The left channel of the noise render() feeds an effect with the given number of inputs.
std::vector<float> noiseFed (int blocks, int inputs)
{
    std::vector<float> out;
    juce::AudioBuffer<float> b (juce::jmax (2, inputs), bs);
    juce::Random rng (11);
    for (int i = 0; i < blocks; ++i)
    {
        fillNoise (b, rng, inputs);
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
    for (auto* prm : projectParams (*p))
        prm->setValueNotifyingHost ((float) std::fmod (0.137 + 0.618034 * i++, 1.0));
    render (*p, 4);                                         // the processor takes the changes
    juce::MemoryBlock state;
    p->getStateInformation (state);
    juce::StringArray lines;
    for (auto* prm : p->getParameters())
        if (prm->isAutomatable() && ! notInProject (*prm)) lines.add (prm->getName (128) + "\t" + prm->getText (prm->getValue(), 128));
    if (! out.getParentDirectory().createDirectory() || ! out.replaceWithData (state.getData(), state.getSize())
        || ! out.withFileExtension (".txt").replaceWithText (lines.joinIntoString ("\n") + "\n", false, false, "\n"))
    {
        std::cerr << "cannot write " << out.getFullPathName() << " and its .txt\n";
        return 1;
    }
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
    auto skip = [] (const juce::String& what, const juce::String& why) { std::cout << "SKIP  " << what << ": " << why << "\n"; };

    auto a = load (pluginPath);
    if (a == nullptr) { std::cerr << "cannot load " << pluginPath << "\n"; return 1; }
    const bool instrument = isInstrument (*a);
    int automatable = 0;
    for (auto* prm : a->getParameters())
        if (prm->isAutomatable()) ++automatable;
    std::cout << "plugin: " << a->getName() << (instrument ? " (instrument)" : " (effect)") << ", parameters "
              << a->getParameters().size() << " (" << automatable << " automatable)\n";
    check (a->getBypassParameter() != nullptr, "the host bypass is the plug-in's own parameter");

    // 1. Sound at the defaults: finite, and not silent
    const auto y0 = render (*a, 40);
    check (allFinite (y0) && rms (y0) > 1.0e-4f, "renders at the defaults (rms " + juce::String (rms (y0), 5) + ")");

    // 2. The host bypass: the input delayed by the latency the plug-in reports (compared after half a second, so a
    //    plug-in that fades into its bypass has arrived)
    if (instrument)
        skip ("the host bypass gives the input delayed by the latency", "an instrument has no input");
    else if (a->getBypassParameter() == nullptr)
        skip ("the host bypass gives the input delayed by the latency", "the plug-in offers the host no bypass parameter");
    else
    {
        auto e = load (pluginPath);
        e->getBypassParameter()->setValueNotifyingHost (1.0f);
        const int blocks = 100, latency = e->getLatencySamples();
        const auto y = render (*e, blocks);
        const auto x = noiseFed (blocks, e->getTotalNumInputChannels());
        float worst = 0.0f;
        if (latency >= 0 && latency < 40 * bs)
            for (size_t i = (size_t) (48 * bs + latency); i < y.size(); ++i)
                worst = juce::jmax (worst, std::abs (y[i] - x[i - (size_t) latency]));
        check (latency >= 0 && latency < 40 * bs && worst < 1.0e-6f,
               "the host bypass gives the input delayed by the latency (" + juce::String (latency) + " samples, max diff " + juce::String (worst) + ")");
    }

    // 3. Automation while rendering: the host moves every parameter on every block. The output stays finite, two
    //    instances given the same moves sound the same, and the moves reach the sound. Rendered offline, as a bounce:
    //    that is the render that has to repeat. In real time a plug-in may finish work in the background (a sampler
    //    that rebuilds its sample when a setting changes), which the timing of two runs does not share.
    if (projectParams (*a).empty())
        skip ("automation while rendering", "the plug-in has no automatable parameter");
    else
    {
        auto automated = [&pluginPath]
        {
            auto e = load (pluginPath);
            e->setNonRealtime (true);
            const auto prms = projectParams (*e);
            return render (*e, 60, [&] (int i, juce::MidiBuffer& m)
            {
                heldNote (*e, i, m);
                for (size_t j = 0; j < prms.size(); ++j)
                    prms[j]->setValueNotifyingHost ((float) std::fmod (0.31 + 0.618034 * (double) (i * 7 + (int) j), 1.0));
            });
        };
        const auto ya = automated(), yb = automated();
        auto plain = load (pluginPath);
        plain->setNonRealtime (true);
        const auto yp = render (*plain, 60);
        check (allFinite (ya) && maxDiff (ya, yb) < 1.0e-6f,
               "every parameter automated on every block: finite, and the same in two instances (max diff " + juce::String (maxDiff (ya, yb)) + ")");
        check (maxDiff (ya, yp) > 1.0e-6f, "the automation reaches the sound (max diff to the unautomated render " + juce::String (maxDiff (ya, yp)) + ")");
    }

    // 4. MIDI (an instrument): silent before the first note, a note sounds, pitch bend, controllers and pressure are
    //    taken, and a note-off with all-notes-off and all-sound-off ends the sound.
    if (! instrument)
        skip ("MIDI notes and controllers", "an effect takes no notes");
    else
    {
        auto e = load (pluginPath);
        const auto quiet = render (*e, 20, [] (int, juce::MidiBuffer&) {});
        check (allFinite (quiet) && rms (quiet) < 1.0e-5f, "silent before the first note (rms " + juce::String (rms (quiet), 6) + ")");
        const auto held = render (*e, 40, [] (int i, juce::MidiBuffer& m)
        {
            if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        });
        check (allFinite (held) && rms (held) > 1.0e-4f, "a note sounds (rms " + juce::String (rms (held), 5) + ")");
        const auto busy = render (*e, 40, [] (int i, juce::MidiBuffer& m)
        {
            if (i == 0)
            {
                m.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
                for (int cc : { 1, 7, 10, 11, 64 }) m.addEvent (juce::MidiMessage::controllerEvent (1, cc, 100), 1);
                m.addEvent (juce::MidiMessage::channelPressureChange (1, 90), 2);
                m.addEvent (juce::MidiMessage::aftertouchChange (1, 60, 90), 3);
            }
            if (i == 20)
            {
                m.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
                for (int cc : { 1, 10, 64 }) m.addEvent (juce::MidiMessage::controllerEvent (1, cc, 0), 1);
                m.addEvent (juce::MidiMessage::controllerEvent (1, 7, 100), 2);
                m.addEvent (juce::MidiMessage::controllerEvent (1, 11, 127), 2);
            }
        });
        check (allFinite (busy), "pitch bend, controllers 1, 7, 10, 11 and 64, and pressure are taken while a note sounds (finite)");
        const int limit = (int) (10.0 * 48000.0 / bs);
        int silentAfter = -1;
        for (int i = 0; i < limit && silentAfter < 0; ++i)
        {
            const auto y = render (*e, 1, [i] (int, juce::MidiBuffer& m)
            {
                if (i != 0) return;
                m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                m.addEvent (juce::MidiMessage::allNotesOff (1), 1);
                m.addEvent (juce::MidiMessage::allSoundOff (1), 1);
            });
            if (allFinite (y) && rms (y) < 1.0e-5f) silentAfter = i;
        }
        check (silentAfter >= 0, "a note-off with all-notes-off and all-sound-off ends the sound ("
                                 + (silentAfter >= 0 ? "silent after " + juce::String (silentAfter * bs / 48000.0, 2) + " s" : juce::String ("still sounding after 10 s")) + ")");
    }

    // 5. Save and reload into a new instance: the host's view of every parameter, the plug-in's own state, then the sound
    juce::Random rng (7);
    for (auto* prm : projectParams (*a)) prm->setValueNotifyingHost (rng.nextFloat());
    render (*a, 4);
    juce::MemoryBlock saved;
    a->getStateInformation (saved);
    auto b = load (pluginPath, 48000.0, false);
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
    {
        // The host's cached values miss a change the plug-in makes itself (a value it clamps on load), so the
        // plug-in's own saved values are compared too, and the --property values with them.
        OwnState sa, sb;
        if (! readOwnState (*a, sa) || ! readOwnState (*b, sb))
            skip ("the plug-in's own saved values after reload", "its state is not the XML form this test reads");
        else
        {
            int differ = 0, compared = 0;
            for (auto* pa : sa.ours->getChildWithTagNameIterator ("PARAM"))
            {
                ++compared;
                auto* pb = sb.ours->getChildByAttribute ("id", pa->getStringAttribute ("id"));
                if (pb == nullptr || std::abs (pa->getDoubleAttribute ("value") - pb->getDoubleAttribute ("value")) > 1.0e-6)
                {
                    ++differ;
                    std::cout << "      plug-in side differs: " << pa->getStringAttribute ("id") << " " << pa->getStringAttribute ("value")
                              << " / " << (pb != nullptr ? pb->getStringAttribute ("value") : juce::String ("(missing)")) << "\n";
                }
            }
            check (differ == 0, "the plug-in's own saved values are the same after reload (" + juce::String (compared) + " values, "
                                + juce::String (differ) + " different)");
            if (properties.size() == 0)
                skip ("the --property values survive save and reload", "no --property was given");
            else
            {
                // a value that names a file may come back in the platform's own form (other slashes)
                auto same = [] (const juce::String& given, const juce::String& saved)
                {
                    return given == saved || (juce::File::isAbsolutePath (given) && juce::File::isAbsolutePath (saved)
                                              && juce::File (given) == juce::File (saved));
                };
                juce::StringArray lost;
                for (auto& key : properties.getAllKeys())
                    if (! same (properties[key], sb.ours->getStringAttribute (key))) lost.add (key);
                check (lost.isEmpty(), "the --property values survive save and reload" + (lost.isEmpty() ? juce::String() : " (lost: " + lost.joinIntoString (", ") + ")"));
            }
        }
    }
    auto c = load (pluginPath), d = load (pluginPath);
    c->setStateInformation (saved.getData(), (int) saved.getSize());
    d->setStateInformation (saved.getData(), (int) saved.getSize());
    const auto yc = render (*c, 40), yd = render (*d, 40);
    check (maxDiff (yc, yd) < 1.0e-6f && allFinite (yc), "two instances loaded from one state sound the same (max diff "
                                                         + juce::String (maxDiff (yc, yd)) + ")");

    // 6. The editor opens and closes
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (d->createEditorAndMakeActive());
        check (ed != nullptr && ed->getWidth() > 0 && ed->getHeight() > 0,
               "the editor opens (" + juce::String (ed != nullptr ? ed->getWidth() : 0) + " x " + juce::String (ed != nullptr ? ed->getHeight() : 0) + ")");
    }

    // 7. The host changes its sample rate mid-session
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

    // 8. Offline rendering (export) gives the same output as playing, from a fresh instance and right after a
    //    project is loaded (nothing the load starts in the background is still pending in the first block)
    {
        auto live = load (pluginPath), offline = load (pluginPath);
        offline->setNonRealtime (true);
        const auto yl = render (*live, 40), yo = render (*offline, 40);
        check (maxDiff (yl, yo) < 1.0e-6f, "offline render equals the real-time render (max diff " + juce::String (maxDiff (yl, yo)) + ")");
        auto fresh = load (pluginPath);
        fresh->setNonRealtime (true);
        fresh->setStateInformation (saved.getData(), (int) saved.getSize());
        const auto yf = render (*fresh, 40);
        check (maxDiff (yc, yf) < 1.0e-6f, "offline render right after loading a project equals its real-time render (max diff "
                                           + juce::String (maxDiff (yc, yf)) + ")");
    }

    // 9. Projects saved by earlier releases
    for (auto& stateFile : states)
        checkSavedState (pluginPath, juce::File::getCurrentWorkingDirectory().getChildFile (stateFile), check);

    std::cout << (failures == 0 ? juce::String ("ALL PASSED") : juce::String (failures) + " FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}
