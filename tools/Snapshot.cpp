// Renders a Chan Sampler plug-in's editor to a PNG for the docs and the layout comparison with the reference
// (docs/screenshots/README.md). One source for every plug-in of the suite: each plugin.cmake builds it with its own
// sources as <Stem>Snapshot.
//   <Stem>Snapshot out.png [--scale=2] [--size=W] [paramID=value ...] [--levels=inDb,outDb]
// --scale renders through the editor's UI scale, as a host at that zoom would; --size resizes the open editor to W px
// wide as a host or the corner grip would (the constrainer keeps the proportions). Parameter values are in real units
// (dB, 0..1); --levels freezes the meters of an editor that offers it (shared/ui/Widgets.h SnapshotHooks).
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <iostream>
#include "shared/ui/Widgets.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: Snapshot out.png [--scale=2] [--size=W] [paramID=value ...] [--levels=inDb,outDb]\n"; return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    float scale = 1.0f, inDb = -200.0f, outDb = -200.0f;
    int sizeW = 0;
    bool levels = false;
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a.startsWith ("--scale=")) { scale = a.fromFirstOccurrenceOf ("=", false, false).getFloatValue(); continue; }
        if (a.startsWith ("--size=")) { sizeW = a.fromFirstOccurrenceOf ("=", false, false).getIntValue(); continue; }
        if (a.startsWith ("--levels="))
        {
            inDb = a.fromFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf (",", false, false).getFloatValue();
            outDb = a.fromFirstOccurrenceOf (",", false, false).getFloatValue();
            levels = true;
            continue;
        }
        for (auto* p : proc->getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (r->getParameterID() == a.upToFirstOccurrenceOf ("=", false, false))
                    r->setValueNotifyingHost (r->convertTo0to1 (a.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditorAndMakeActive());
    if (ed == nullptr) { std::cerr << "the plug-in has no editor\n"; return 1; }
    if (levels)
    {
        if (auto* hooks = dynamic_cast<chansampler::SnapshotHooks*> (ed.get())) hooks->setTestLevels (inDb, outDb);
        else std::cerr << "--levels: this editor has no meters to freeze\n";
    }
    // the editor opens at the state's scale (1 in a fresh instance): a scale is a resize by that factor
    const int w0 = ed->getWidth(), h0 = ed->getHeight();
    if (sizeW > 0 || scale != 1.0f)
    {
        const int w = sizeW > 0 ? sizeW : (int) std::lround (w0 * scale);
        const juce::Rectangle<int> b (0, 0, w, (int) std::lround (w * (double) h0 / w0));
        if (auto* c = ed->getConstrainer()) c->setBoundsForComponent (ed.get(), b, false, false, true, true);   // as the corner drags
        else ed->setBounds (b);
        std::cout << "resized to " << ed->getWidth() << " x " << ed->getHeight() << std::endl;
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    ed.reset();
    return 0;
}
