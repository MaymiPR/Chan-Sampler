// Saves a PNG of the editor for visual comparison with FL's Sampler window.
//
//   ChanSamplerSnapshot <sample.wav> <out.png> <params.txt> <sheet 0-2> [playMs] [inner page]
//   ChanSamplerSnapshot menus <out.png>     both of FL's menu styles
//   ChanSamplerSnapshot fontwidth           the reference text's width (sizes the macOS font)
//
// params.txt as for ChanSamplerRender. With playMs > 0 a note plays for that
// long first, so the sample view shows its playback bar. Environment:
// SNAPSHOT_SR (host rate), SNAPSHOT_SCALE (e.g. 2 for Retina), SNAPSHOT_PROPS
// ("name=value ..." state properties, e.g. viewSpectrum=1), SNAPSHOT_VIEW
// ("from to" zoom), SNAPSHOT_DIALOG (a message), SNAPSHOT_TEMPO=1 (Detect
// tempo dialog), SNAPSHOT_PROPERTIES=1 (Get properties), SNAPSHOT_DRAGTEST
// (drags the knob whose tooltip contains the text, printing its values).

#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/WaveformView.h"

namespace
{
    // Calls f on every sample view in the editor.
    void forEachWaveform (juce::Component& c, const std::function<void (WaveformView&)>& f)
    {
        if (auto* w = dynamic_cast<WaveformView*> (&c))
            f (*w);
        for (auto* child : c.getChildren())
            forEachWaveform (*child, f);
    }
}

int main (int argc, char** argv)
{
    const juce::String mode (argc >= 2 ? argv[1] : "");
    if (! (mode == "fontwidth" || (mode == "menus" && argc >= 3) || argc >= 5))
    {
        std::cerr << "usage: ChanSamplerSnapshot sample.wav out.png params.txt sheet [playMs] [page]\n"
                     "       ChanSamplerSnapshot menus out.png\n"
                     "       ChanSamplerSnapshot fontwidth\n";
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
        ctxComp.getProperties().set (fl::contextMenuProperty, true);
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
                for (auto& [text, kind] : rows)   // 0 item, 1 highlighted, 2 header, 3 disabled, 4 separator, 5 ticked, 6 ticked + disabled, 7 submenu, 8 Ctrl+C, 9 Ctrl+V disabled
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
                    item.isEnabled = kind != 3 && kind != 6 && kind != 9;
                    if (kind == 8 || kind == 9)
                        item.shortcutKeyDescription = fl::commandShortcutText (kind == 8 ? 'c' : 'v');
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
            drawMenu (260, 10, 190, ctxOpts, { { "&Reload", 0 }, { "&Save as...", 1 }, { "Locate file", 0 }, { "&Copy", 8 }, { "&Paste", 9 }, { "", 4 },
                                               { "&Detect tempo", 0 }, { "&Get properties", 0 }, { "", 4 },
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
        forEachWaveform (*ed, [&] (WaveformView& w) { w.setView (parts[0].getFloatValue(), parts[1].getFloatValue()); });
    }
    // SNAPSHOT_DIALOG="message": an FL-style message dialog over the editor
    if (auto* d = std::getenv ("SNAPSHOT_DIALOG"))
        fl::showDialog (*ed->getChildComponent (0), "Chan Sampler", d, false);
    // SNAPSHOT_DRAGTEST="knob name": drags that knob a pixel at a time with mouse
    // events and prints its value at each step (checks the feel of a drag).
    if (auto* name = std::getenv ("SNAPSHOT_DRAGTEST"))
    {
        std::function<juce::Slider* (juce::Component&)> find = [&] (juce::Component& c) -> juce::Slider*
        {
            if (auto* sl = dynamic_cast<juce::Slider*> (&c))
                if (sl->getTooltip().containsIgnoreCase (name))
                    return sl;
            for (auto* ch : c.getChildren())
                if (auto* found = find (*ch))
                    return found;
            return nullptr;
        };
        if (auto* sl = find (*ed))
        {
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const auto start = sl->getLocalBounds().getCentre().toFloat();
            const auto now = juce::Time::getCurrentTime();
            auto event = [&] (juce::Point<float> pos, bool dragged)
            {
                return juce::MouseEvent (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         sl, sl, now, start, now, 1, dragged);
            };
            std::cout << sl->getTooltip() << " starts at " << sl->getValue() << "\n";
            for (int dir : { 1, -1 })
            {
                sl->mouseDown (event (start, false));
                for (int px = 1; px <= 80; ++px)
                {
                    const auto pos = start.translated (0.0f, -(float) (dir * px));
                    sl->mouseDrag (event (pos, true));
                    std::cout << dir * px << ":" << sl->getValue() << " ";
                }
                sl->mouseUp (event (start.translated (0.0f, -(float) (dir * 80)), true));
                std::cout << "\n";
            }
        }
        return 0;
    }
    // SNAPSHOT_TEMPO=1: the sample view's Detect tempo dialog
    if (std::getenv ("SNAPSHOT_TEMPO"))
        forEachWaveform (*ed, [] (WaveformView& w) { w.detectTempo ({}); });
    // SNAPSHOT_PROPERTIES=1: the sample view's Get properties window
    if (std::getenv ("SNAPSHOT_PROPERTIES"))
        forEachWaveform (*ed, [] (WaveformView& w) { w.showPropertiesForTesting(); });
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
