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
        juce::Image img (juce::Image::RGB, 420, 230, true);
        {
        juce::Graphics g (img);
        g.fillAll (juce::Colours::white);
        auto drawMenu = [&] (int x, int y, int w, const juce::PopupMenu::Options& o, std::vector<std::pair<juce::String, int>> rows)
        {
            int h = 2;
            for (auto& r : rows) h += r.second == 2 ? 13 : 19;
            juce::Graphics::ScopedSaveState ss (g);
            g.setOrigin (x, y);
            g.reduceClipRegion (0, 0, w, h);
            lnf.drawPopupMenuBackgroundWithOptions (g, w, h, o);
            int yy = 1;
            for (auto& [text, kind] : rows)   // 0 item, 1 highlighted, 2 header, 3 disabled
            {
                if (kind == 2)
                {
                    lnf.drawPopupMenuSectionHeaderWithOptions (g, { 1, yy, w - 2, 13 }, text, o);
                    yy += 13;
                    continue;
                }
                juce::PopupMenu::Item item (text);
                item.isEnabled = kind != 3;
                lnf.drawPopupMenuItemWithOptions (g, { 1, yy, w - 2, 19 }, kind == 1, item, o);
                yy += 19;
            }
        };
        drawMenu (10, 10, 90, ddOpts, { { "Realtime", 2 }, { "Resample", 0 }, { "Stretch", 1 }, { "Stretch Pro", 0 },
                                         { "Elastique", 2 }, { "e3 generic", 0 }, { "e3 mono", 0 }, { "Special", 2 },
                                         { "Slice stretch", 0 }, { "Slice map", 0 } });
        drawMenu (130, 10, 112, rmOpts, { { "&Reset", 0 }, { "&Copy value", 1 }, { "Paste &value", 3 }, { "&Type in value...", 0 } });
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

    constexpr double rate = 44100.0;
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

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<ChanSamplerEditor*> (ed.get());
    editor->showSheetForTesting (juce::String (argv[4]).getIntValue(), argc > 6 ? juce::String (argv[6]).getIntValue() : -1);
    for (int i = 0; i < 20; ++i)   // let timers update the views
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File out { juce::String (argv[2]) };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << ed->getWidth() << "x" << ed->getHeight() << "\n";
    return 0;
}
