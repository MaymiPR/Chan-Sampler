// Saves a PNG of the editor for visual comparison with FL's Sampler window.
//
//   ChanSamplerSnapshot <sample.wav> <out.png> <params.txt> <sheet 0-2> [playMs] [inner page]
//
// params.txt as for ChanSamplerRender. With playMs > 0 a note plays for that
// long first, so the sample view shows its playback bar.

#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/WaveformView.h"

int main (int argc, char** argv)
{
    if (argc < 5 && ! (argc >= 2 && juce::String (argv[1]) == "fontwidth"))
    {
        std::cerr << "usage: ChanSamplerSnapshot sample.wav out.png params.txt sheet [playMs] [page]\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;

    // "fontwidth" mode: width of fl::font's reference text per unit height
    // (used to size the macOS font like Segoe UI).
    if (juce::String (argv[1]) == "fontwidth")
    {
        for (bool bold : { false, true })
            std::cout << (bold ? "bold " : "plain ") << juce::GlyphArrangement::getStringWidth (fl::font (100.0f, bold), fl::fontReferenceText) / 100.0f << "\n";
        return 0;
    }

    // "menus" mode: draws both FL menu styles with the plugin's look-and-feel.
    if (juce::String (argv[1]) == "menus")
    {
        fl::LookAndFeel lnf;
        juce::ComboBox box;
        juce::Slider knobComp;
        const auto ddOpts = juce::PopupMenu::Options().withTargetComponent (&box);
        const auto rmOpts = juce::PopupMenu::Options().withTargetComponent (&knobComp);
        juce::Component ctxComp;   // the sample / preset menus (fl::showContextMenu)
        ctxComp.getProperties().set ("flContextMenu", true);
        const auto ctxOpts = juce::PopupMenu::Options().withTargetComponent (&ctxComp);
        juce::Image img (juce::Image::RGB, 460, 330, true);
        {
        juce::Graphics g (img);
        g.fillAll (juce::Colours::white);
        auto drawMenu = [&] (int x, int y, int w, const juce::PopupMenu::Options& o, std::vector<std::pair<juce::String, int>> rows)
        {
            const auto sepH = &o == &ctxOpts ? 7 : 5;
            int h = 2;
            for (auto& r : rows) h += r.second == 2 ? 13 : r.second == 4 ? sepH : 19;
            juce::Graphics::ScopedSaveState ss (g);
            g.setOrigin (x, y);
            g.reduceClipRegion (0, 0, w, h);
            lnf.drawPopupMenuBackgroundWithOptions (g, w, h, o);
            int yy = 1;
            for (auto& [text, kind] : rows)   // 0 item, 1 highlighted, 2 header, 3 disabled, 4 separator, 5 ticked, 6 ticked + disabled, 7 submenu
            {
                if (kind == 4)
                {
                    juce::PopupMenu::Item sep;
                    sep.isSeparator = true;
                    lnf.drawPopupMenuItemWithOptions (g, { 1, yy, w - 2, sepH }, false, sep, o);
                    yy += sepH;
                    continue;
                }
                if (kind == 2)
                {
                    lnf.drawPopupMenuSectionHeaderWithOptions (g, { 1, yy, w - 2, 13 }, text, o);
                    yy += 13;
                    continue;
                }
                juce::PopupMenu::Item item (text);
                item.isEnabled = kind != 3 && kind != 6;
                item.isTicked = kind == 5 || kind == 6;
                if (kind == 7)
                    item.subMenu = std::make_unique<juce::PopupMenu>();
                lnf.drawPopupMenuItemWithOptions (g, { 1, yy, w - 2, 19 }, kind == 1, item, o);
                yy += 19;
            }
        };
        drawMenu (10, 10, 90, ddOpts, { { "Realtime", 2 }, { "Resample", 0 }, { "Stretch", 1 }, { "Stretch Pro", 0 },
                                         { "Elastique", 2 }, { "e3 generic", 0 }, { "e3 mono", 0 }, { "Special", 2 },
                                         { "Slice stretch", 0 }, { "Slice map", 0 } });
        drawMenu (130, 10, 112, rmOpts, { { "&Reset", 0 }, { "&Copy value", 1 }, { "Paste &value", 3 }, { "&Type in value...", 0 } });
        drawMenu (260, 10, 190, ctxOpts, { { "Reload", 0 }, { "Save as...", 1 }, { "Locate file", 0 }, { "", 4 }, { "Get properties", 0 }, { "", 4 },
                                           { "Spectrum view", 5 }, { "Multichannel waveform view", 6 }, { "Colorful waveforms", 7 } });
        }   // Direct2D draws when the Graphics goes away
        juce::File out { juce::String (argv[2]) };
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }
    ChanSamplerProcessor proc;
    if (! proc.loadSample (juce::File (juce::String (argv[1]))))
        return 1;

    juce::StringArray lines;
    juce::File (juce::String (argv[3])).readLines (lines);
    for (auto& line : lines)
    {
        const auto id = line.upToFirstOccurrenceOf (" ", false, false).trim();
        if (auto* p = id.isNotEmpty() ? proc.apvts.getParameter (id) : nullptr)
            p->setValueNotifyingHost (p->convertTo0to1 (line.fromFirstOccurrenceOf (" ", false, false).getFloatValue()));
    }
    proc.waitForPendingRebuilds();
    proc.rebuildNow();

    const double rate = std::getenv ("SNAPSHOT_SR") ? juce::String (std::getenv ("SNAPSHOT_SR")).getDoubleValue() : 44100.0;   // host rate
    constexpr int block = 512;
    proc.setPlayConfigDetails (0, 2, rate, block);
    proc.prepareToPlay (rate, block);
    const int playMs = argc > 5 ? juce::String (argv[5]).getIntValue() : 0;
    if (playMs > 0)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        proc.requestPreview (60, true);
        for (int done = 0; done < (int) (playMs * rate / 1000.0); done += block)
        {
            proc.processBlock (buf, midi);
            midi.clear();
        }
    }

    // SNAPSHOT_PROPS="name=value ...": plugin state properties (e.g. viewSpectrum=1)
    if (auto* props = std::getenv ("SNAPSHOT_PROPS"))
        for (auto& kv : juce::StringArray::fromTokens (props, " ", {}))
            proc.apvts.state.setProperty (kv.upToFirstOccurrenceOf ("=", false, false), kv.fromFirstOccurrenceOf ("=", false, false).getIntValue(), nullptr);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<ChanSamplerEditor*> (ed.get());
    editor->showSheetForTesting (juce::String (argv[4]).getIntValue(), argc > 6 ? juce::String (argv[6]).getIntValue() : -1);
    // SNAPSHOT_VIEW="from to": zoom the sample view (fractions of the sample).
    if (auto* v = std::getenv ("SNAPSHOT_VIEW"))
    {
        const auto parts = juce::StringArray::fromTokens (v, " ", {});
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* w = dynamic_cast<WaveformView*> (&c))
                w->setView (parts[0].getFloatValue(), parts[1].getFloatValue());
            for (auto* ch : c.getChildren())
                find (*ch);
        };
        find (*ed);
    }
    // SNAPSHOT_DIALOG="message": an FL-style message dialog over the editor
    if (auto* d = std::getenv ("SNAPSHOT_DIALOG"))
        fl::showDialog (*ed->getChildComponent (0), "Chan Sampler", d, false);
    // SNAPSHOT_PROPERTIES=1: the sample view's Get properties window
    if (std::getenv ("SNAPSHOT_PROPERTIES"))
    {
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* w = dynamic_cast<WaveformView*> (&c))
                w->showPropertiesForTesting();
            for (auto* ch : c.getChildren())
                find (*ch);
        };
        find (*ed);
    }
    for (int i = 0; i < 20; ++i)   // let timers update the views
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, std::getenv ("SNAPSHOT_SCALE") ? juce::String (std::getenv ("SNAPSHOT_SCALE")).getFloatValue() : 1.0f);   // SNAPSHOT_SCALE: e.g. 2 for Retina
    juce::File out { juce::String (argv[2]) };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << ed->getWidth() << "x" << ed->getHeight() << "\n";
    return 0;
}
