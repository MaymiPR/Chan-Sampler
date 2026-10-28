#include "PluginEditor.h"
#include "FlPreset.h"
#include "PresetBar.h"
#include "Chords.h"
#include "TempoDetect.h"
#include "HostFocus.h"

namespace
{
    const juce::String repository = "https://github.com/MaymiPR/ChanSampler";

    // The product name in the top bar: a click opens the plugin's own menu
    // (version, About, the project's links) in FL's menu style.
    struct TitleButton : juce::Component, juce::SettableTooltipClient
    {
        TitleButton()
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("About Chan Sampler");
        }
        static constexpr float size = 11.25f;
        static int widthFor (const juce::String& s) { return (int) std::ceil (juce::GlyphArrangement::getStringWidth (fl::font (size, true), s)) + 4; }
        void paint (juce::Graphics& g) override
        {
            g.setFont (fl::font (size, true));
            g.setColour (isMouseOver() ? fl::colour::tabIcon.brighter (0.35f) : fl::colour::tabIcon);
            g.drawText (text, getLocalBounds().withTrimmedRight (1), juce::Justification::centredRight);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        void mouseDown (const juce::MouseEvent&) override  { if (onClick) onClick(); }
        juce::String text { "CHAN SAMPLER" };
        std::function<void()> onClick;
    };

    // Undo / redo (Chan Sampler's own; FL keeps them in its main toolbar): a
    // field like the preset bar's, split in two, each half a U-turn arrow
    // drawn in the preset arrows' colours: dim at rest, lifting towards white
    // under the mouse (the shared fade), white while pressed, darker when
    // there is nothing to undo or redo.
    struct HistoryButton : juce::Button
    {
        explicit HistoryButton (bool isRedo) : juce::Button (isRedo ? "Redo" : "Undo"), redo (isRedo) {}
        void paintButton (juce::Graphics& g, bool, bool down) override
        {
            auto c = isEnabled() ? fl::colour::dim.interpolatedWith (juce::Colours::white, 0.55f * fl::hoverOf (*this))
                                 : fl::colour::dim.withAlpha (0.4f);
            if (down && isEnabled())
                c = juce::Colours::white;
            // drawn for undo (head at the left) and mirrored for redo
            const float w = (float) getWidth(), cy = std::floor ((float) getHeight() * 0.5f);
            auto x = [this, w] (float v) { return redo ? w - v : v; };
            juce::Path shaft;
            shaft.startNewSubPath (x (9.0f), cy - 2.0f);
            shaft.lineTo (x (12.5f), cy - 2.0f);
            shaft.cubicTo (x (17.2f), cy - 2.0f, x (17.2f), cy + 5.0f, x (12.5f), cy + 5.0f);
            shaft.lineTo (x (8.5f), cy + 5.0f);
            g.setColour (c);
            g.strokePath (shaft, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            juce::Path head;
            head.addTriangle (x (5.0f), cy - 2.0f, x (9.5f), cy - 5.5f, x (9.5f), cy + 1.5f);
            g.fillPath (head);
        }
        void mouseEnter (const juce::MouseEvent& e) override { hover.setTarget (true); juce::Button::mouseEnter (e); }
        void mouseExit (const juce::MouseEvent& e) override { hover.setTarget (false); juce::Button::mouseExit (e); }
        bool redo;
        fl::HoverFade hover { *this };
    };

    // The field behind the two halves: the preset bar's well and edge, with a
    // rule between the halves.
    struct HistoryField : juce::Component
    {
        HistoryField()
        {
            setInterceptsMouseClicks (false, true);
            addAndMakeVisible (undo);
            addAndMakeVisible (redo);
        }
        void resized() override
        {
            const int half = getWidth() / 2;
            undo.setBounds (0, 0, half, getHeight());
            redo.setBounds (half + 1, 0, getWidth() - half - 1, getHeight());
        }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (fl::colour::well);
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (fl::colour::fieldEdge);
            g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
            g.fillRect ((float) (getWidth() / 2), 4.0f, 1.0f, r.getHeight() - 8.0f);
        }
        HistoryButton undo { false }, redo { true };
    };

    // Clicks anywhere in the editor give it the keyboard focus (not to the
    // control clicked, so the space bar is not swallowed): in a plugin window
    // whose host does not offer it the keys, that is what lets Ctrl+Z / Ctrl+Y
    // reach it, and JUCE passes every key the editor does not use on to the
    // host's window. Not for VST3 (the host offers each key to the view:
    // cmake/PatchJuceVst3Keys.cmake; holding the focus there upset FL's own
    // keys, the space bar starting playback only on its second press) nor
    // standalone (the window has the keys).
    // FL Studio (VST3): the plugin's FL window keeps the keyboard focus. A
    // click moves the Windows focus into the plugin's own window (Windows or
    // FL's panel does it, not JUCE); FL then sends the keys there and no longer
    // acts on those the plugin leaves, so after touching a knob the space bar
    // did not play or stop (logged in FL, see HostFocus.h). After a click
    // this hands the focus to FL's window for the plugin, as a click on FL's own
    // Sampler leaves it, unless a value box is being typed in: FL then offers
    // every key to the view (cmake/PatchJuceVst3Keys.cmake), the editor takes
    // Ctrl+Z / Ctrl+Y and FL acts on the rest.
    struct KeepHostFocus : juce::MouseListener, private juce::Timer
    {
        explicit KeepHostFocus (juce::Component& c) : editor (c) { startTimerHz (4); }
        // not for a right-click: its menu would close the moment the focus left
        void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) handBackSoon(); }
        void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) handBackSoon(); }

        // after JUCE has handled the click and the focus change it brought
        void handBackSoon()
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (&editor), this]
            {
                if (safe != nullptr)
                    handBack();
            });
        }
        // also when a typed value, a menu or a dialog is done with the focus
        void timerCallback() override { handBack(); }

        void handBack()
        {
            if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
                return;   // typing a value: the keys are the editor's
            if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
                return;   // a menu or dialog is open: moving the focus would close it
            auto* peer = editor.getPeer();
            if (peer == nullptr || ! hostfocus::isWithin (hostfocus::current(), peer->getNativeHandle()))
                return;
            hostfocus::giveTo (hostfocus::titledAncestor (peer->getNativeHandle()));
        }
        juce::Component& editor;
    };

    struct FocusOnClick : juce::MouseListener
    {
        explicit FocusOnClick (juce::Component& c) : editor (c) {}
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (editor.hasKeyboardFocus (true) || (e.eventComponent != nullptr && e.eventComponent->getWantsKeyboardFocus()))
                return;   // a value box being typed in keeps it
            editor.grabKeyboardFocus();
        }
        juce::Component& editor;
    };

    // Plain filled rectangle (bevels).
    struct Fill : juce::Component
    {
        Fill (juce::Colour c, float corner = 0.0f) : colour (c), radius (corner) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (colour);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), radius);
        }
        juce::Colour colour;
        float radius;
    };

    // FL's top panel: the dark strip with a darker rule above and below it.
    struct TopBar : juce::Component
    {
        TopBar() { setInterceptsMouseClicks (false, true); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (fl::colour::well);
            g.setColour (fl::colour::barLine);
            g.fillRect (0, 0, getWidth(), 1);
            g.fillRect (0, getHeight() - 1, getWidth(), 1);
        }
    };
}

//==============================================================================
ChanSamplerEditor::ChanSamplerEditor (ChanSamplerProcessor& p)
    : AudioProcessorEditor (p), samplerProcessor (p), waveform (p)
{
    setLookAndFeel (&lnf);

    canvas.setBounds (0, 0, canvasW, canvasH);
    addAndMakeVisible (canvas);

    // FL: top panel 561 x 45, sheets 561 x 399 below it.
    for (auto* sheet : { &smpSheet, &insSheet, &funcSheet })
    {
        sheet->setBounds (1, 46, 561, 399);
        canvas.addChildComponent (*sheet);
    }

    buildTopBar();
    buildSampleSheet();
    buildInsSheet();
    buildFuncSheet();
    showSheet (0);

    // Never take keyboard focus: focused buttons swallow the space bar, and
    // unhandled keys then reach the host (play / pause).
    std::function<void (juce::Component&)> noFocus = [&noFocus] (juce::Component& c)
    {
        c.setWantsKeyboardFocus (false);
        c.setMouseClickGrabsKeyboardFocus (false);
        for (auto* child : c.getChildren())
            noFocus (*child);
    };
    noFocus (*this);
    // ... except the editor itself (see FocusOnClick)
    setWantsKeyboardFocus (true);
    if (samplerProcessor.wrapperType != juce::AudioProcessor::wrapperType_VST3
        && samplerProcessor.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
    {
        focusOnClick = std::make_unique<FocusOnClick> (*this);
        addMouseListener (focusOnClick.get(), true);
    }
    else if (samplerProcessor.wrapperType == juce::AudioProcessor::wrapperType_VST3 && juce::PluginHostType().isFruityLoops())
    {
        focusOnClick = std::make_unique<KeepHostFocus> (*this);
        addMouseListener (focusOnClick.get(), true);
    }

    samplerProcessor.addChangeListener (this);
    samplerProcessor.history.addChangeListener (this);
    refreshSample();
    refreshHistoryButtons();

    // Opens at FL's own Sampler window size (563 x 446 content), or at the
    // size it was last resized to (stored with the plugin state).
    const int savedW = samplerProcessor.apvts.state.getProperty ("uiWidth", canvasW);
    const int w = juce::jlimit (canvasW / 2, canvasW * 4, savedW);
    setResizable (true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) canvasW / (double) canvasH);
    setResizeLimits (canvasW / 2, canvasH / 2, canvasW * 4, canvasH * 4);
    setSize (w, juce::roundToInt (w * (double) canvasH / canvasW));
    rememberSize = true;
}

ChanSamplerEditor::~ChanSamplerEditor()
{
    if (focusOnClick != nullptr)
        removeMouseListener (focusOnClick.get());
    samplerProcessor.history.removeChangeListener (this);
    samplerProcessor.removeChangeListener (this);
    if (keyWindow != nullptr)
        keyWindow->removeKeyListener (this);
    setLookAndFeel (nullptr);
}

void ChanSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (fl::colour::sheet);
}

// FL's Ctrl+C / Ctrl+V, heard on the window that holds the editor (the
// editor itself in a plugin, the app's window standalone); every other key
// goes on to the host.
void ChanSamplerEditor::parentHierarchyChanged()
{
    auto* top = getTopLevelComponent();
    if (top == keyWindow)
        return;
    if (keyWindow != nullptr)
        keyWindow->removeKeyListener (this);
    keyWindow = top;
    if (keyWindow != nullptr)
        keyWindow->addKeyListener (this);
}

bool ChanSamplerEditor::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;   // typing in a value box: the keys are the editor's
    const auto mods = key.getModifiers();
    auto code = key.getKeyCode();
    if (code >= 1 && code <= 26)   // Windows: Ctrl + a letter as its control character
        code += 'a' - 1;
    code = (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
    // FL Studio can hand one press over twice when the plugin window has the
    // focus (to the window, and through the VST3 key call), as the key and its
    // character: one action per press (a held key repeats every ~33 ms).
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto id = code | (mods.getRawFlags() & juce::ModifierKeys::allKeyboardModifiers) << 16;
    if (id == lastShortcut && now - lastShortcutTime < 30.0)
        return true;
    const bool ctrl = mods.isCommandDown() && ! mods.isAltDown();
    // Ctrl+Z undoes, Ctrl+Y (or Ctrl+Shift+Z) redoes. With nothing to undo or
    // redo here the key is not used, so it goes on to the host (its own undo).
    bool used = false;
    if (ctrl && ! mods.isShiftDown() && code == 'z')
        used = samplerProcessor.history.undo();
    else if (ctrl && ((! mods.isShiftDown() && code == 'y') || (mods.isShiftDown() && code == 'z')))
        used = samplerProcessor.history.redo();
    else
        used = waveform.keyShortcut (key);
    if (used)
    {
        lastShortcut = id;
        lastShortcutTime = now;
    }
    return used;
}

void ChanSamplerEditor::refreshHistoryButtons()
{
    auto& h = samplerProcessor.history;
    if (undoButton != nullptr)
    {
        undoButton->setEnabled (h.canUndo());
        undoButton->setTooltip (h.canUndo() ? "Undo " + h.getUndoName() + " (" + fl::commandShortcutText ('Z') + ")" : juce::String ("Nothing to undo"));
    }
    if (redoButton != nullptr)
    {
        redoButton->setEnabled (h.canRedo());
        redoButton->setTooltip (h.canRedo() ? "Redo " + h.getRedoName() + " (" + fl::commandShortcutText ('Y') + ")" : juce::String ("Nothing to redo"));
    }
}

void ChanSamplerEditor::resized()
{
    canvas.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) canvasW));
    if (rememberSize)
        samplerProcessor.apvts.state.setProperty ("uiWidth", getWidth(), nullptr);
}

//==============================================================================
juce::Slider& ChanSamplerEditor::knob (juce::Component& parent, const juce::String& paramId, int x, int y, int size, juce::Colour ring)
{
    auto* param = samplerProcessor.apvts.getParameter (paramId);
    // FL draws a knob 1 px in from the top left of its rectangle (the filter
    // knobs 1 px out).
    const int off = size > 50 ? -1 : 1;
    auto& s = own (std::make_unique<ParamKnob> (*param, juce::Slider::RotaryHorizontalVerticalDrag),
                   parent, { x + off, y + off, size, size });
    s.setColour (juce::Slider::rotarySliderFillColourId, ring);
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (samplerProcessor.apvts, paramId, s));

    s.setDoubleClickReturnValue (false, param->convertFrom0to1 (param->getDefaultValue()));   // the default (Alt+click, the arc); no double-click, as in FL
    s.setPopupDisplayEnabled (true, false, &canvas);
    s.setTooltip (param->getName (64));
    return s;
}

juce::Slider& ChanSamplerEditor::hslider (juce::Component& parent, const juce::String& paramId, int x, int y, int w, int h)
{
    auto* param = samplerProcessor.apvts.getParameter (paramId);
    auto& s = own (std::make_unique<ParamKnob> (*param, juce::Slider::LinearHorizontal), parent, { x, y, w, h });
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (samplerProcessor.apvts, paramId, s));
    s.setDoubleClickReturnValue (false, param->convertFrom0to1 (param->getDefaultValue()));
    s.setPopupDisplayEnabled (true, false, &canvas);
    s.setTooltip (param->getName (64));
    return s;
}

juce::Label& ChanSamplerEditor::caption (juce::Component& parent, const juce::String& t, int x, int y, int w, int h, float em)
{
    // FL's small all-caps knob captions (7 px bold); widen a little so text never clips.
    auto& l = text (parent, t, x - 8, y, w + 16, h, em, true, juce::Justification::centredTop);
    l.setColour (juce::Label::textColourId, fl::colour::caption);
    l.setBorderSize ({});
    l.getProperties().set ("captionBaseline", true);   // FL: the caps stand on the label's last row
    return l;
}

juce::Rectangle<int> ChanSamplerEditor::comboBounds (int x, int y, int w, int h)
{
    // FL draws its drop-down field 1 px in from the top left of the control's
    // rectangle, as wide as the control (its right edges measured on all four lists).
    return { x + 1, y + 1, w, h };
}

juce::ToggleButton& ChanSamplerEditor::led (juce::Component& parent, const juce::String& paramId, const juce::String& t,
                                            int x, int y, int w, int h)
{
    auto& b = own (std::make_unique<fl::Switch> (t), parent, { x, y, w, h });
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (samplerProcessor.apvts, paramId, b));
    b.setTooltip (samplerProcessor.apvts.getParameter (paramId)->getName (64));
    return b;
}

juce::ComboBox& ChanSamplerEditor::combo (juce::Component& parent, const juce::String& paramId, int x, int y, int w, int h)
{
    auto& c = own (std::make_unique<fl::ComboBox>(), parent, comboBounds (x, y, w, h));
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (samplerProcessor.apvts.getParameter (paramId)))
    {
        c.addItemList (choice->choices, 1);
        c.setTooltip (choice->getName (64));
    }
    comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (samplerProcessor.apvts, paramId, c));
    return c;
}

fl::DigiBox& ChanSamplerEditor::digi (juce::Component& parent, const juce::String& paramId, int x, int y, int w, int h, bool red)
{
    auto* param = samplerProcessor.apvts.getParameter (paramId);
    auto& d = own (std::make_unique<ParamDigiBox> (*param, red), parent, { x, y, w, h });
    sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (samplerProcessor.apvts, paramId, d));
    d.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    d.setTooltip (param->getName (64));
    return d;
}

juce::Label& ChanSamplerEditor::text (juce::Component& parent, const juce::String& s, int x, int y, int w, int h,
                                      float size, bool bold, juce::Justification just)
{
    auto& l = own (std::make_unique<juce::Label> (juce::String(), s), parent, { x, y, w, h });
    l.setFont (fl::font (size, bold));
    l.setJustificationType (just);
    l.setBorderSize ({ 0, 1, 0, 1 });
    l.setInterceptsMouseClicks (false, false);
    return l;
}

fl::Panel& ChanSamplerEditor::panel (juce::Component& parent, const juce::String& title, int x, int y, int w, int h, int headerInset)
{
    return own (std::make_unique<fl::Panel> (title, headerInset), parent, { x, y, w, h });
}

juce::Component& ChanSamplerEditor::group (juce::Component& parent, int x, int y, int w, int h)
{
    auto& c = own (std::make_unique<juce::Component>(), parent, { x, y, w, h });
    c.setInterceptsMouseClicks (false, true);
    return c;
}

//==============================================================================
void ChanSamplerEditor::buildTopBar()
{
    // FL: the strip runs from the canvas's top to the sheet, 47 rows with its rules.
    auto& bar = own (std::make_unique<TopBar>(), canvas, { 1, 0, 561, 47 });

    // FL's sheet tabs: three 44 px cells from 2 px in.
    auto tabs = std::make_unique<fl::IconTabs> (std::vector<fl::Icon> { fl::Icon::sampler, fl::Icon::envelope, fl::Icon::wrench }, true);
    sheetTabs = &own (std::move (tabs), bar, { 2, 1, 132, 45 });
    sheetTabs->onChange = [this] (int i) { showSheet (i); };
    sheetTabs->setTooltip ("Sampler / Envelopes / Misc");

    // Product name and loaded-sample summary (FL's channel VOL / PAN / PITCH /
    // RANGE / TRACK belong to the host channel and are left to the host).
    auto title = std::make_unique<TitleButton>();
    const int titleW = TitleButton::widthFor (title->text);
    auto& name = own (std::move (title), bar, { 500 - titleW, 6, titleW, 16 });
    name.onClick = [this, &name] { showAboutMenu (name); };
    sampleInfo = &text (bar, {}, 368, 23, 132, 14, 9.5f, false, juce::Justification::centredRight);
    sampleInfo->setMinimumHorizontalScale (0.85f);

    // The plugin's own presets (FL's wrapper has its preset menu here too)
    auto& presets = own (std::make_unique<PresetBar> (samplerProcessor), bar, { 146, 11, 168, 23 });
    presets.onMessage = [&presets] (const juce::String& message, bool ok)
    {
        fl::showDialog (presets, "Chan Sampler", message, ! ok);
    };

    knob (bar, pid::output, 517, 3, 30, fl::colour::green);
    caption (bar, "OUT", 518, 35, 28, 7);

    // Undo / redo, beside the presets and as tall as their field
    auto& history = own (std::make_unique<HistoryField>(), bar, { 318, 11, 45, 23 });
    history.undo.onClick = [this] { samplerProcessor.history.undo(); };
    history.redo.onClick = [this] { samplerProcessor.history.redo(); };
    undoButton = &history.undo;
    redoButton = &history.redo;
    sampleInfo->setColour (juce::Label::textColourId, fl::colour::dim);
}

void ChanSamplerEditor::showAboutMenu (juce::Component& target)
{
    const juce::String version = JucePlugin_VersionString;
    juce::PopupMenu menu;
    menu.addSectionHeader ("Chan Sampler v" + version);
    menu.addItem ("About Chan Sampler", [this, version]
    {
        const auto format = juce::AudioProcessor::getWrapperTypeDescription (samplerProcessor.wrapperType);
        fl::showInfo (canvas, "About Chan Sampler", {
            { "Version", version + (samplerProcessor.wrapperType == juce::AudioProcessor::wrapperType_Undefined ? juce::String() : " (" + juce::String (format) + ")") },
            { {}, {} },
            { "What it is", "FL Studio's Channel Sampler as a plugin for any DAW, measured against FL itself" },
            { "License", "Free software under the GNU Affero General Public License v3" },
            { "Built with", "JUCE, the VST3 SDK and CLAP; their licenses come with it" },
            { "Source", repository.fromFirstOccurrenceOf ("https://", false, false) },
            { {}, {} },
            { "Note", "An independent project: not made, endorsed or supported by Image-Line. FL Studio is a trademark of Image-Line Software." } });
    });
    menu.addItem ("Chan Sampler on GitHub", [] { juce::URL (repository).launchInDefaultBrowser(); });
    menu.addItem ("Report an Issue", [] { juce::URL (repository + "/issues/new/choose").launchInDefaultBrowser(); });
    menu.addItem ("License (AGPL-3.0)", [] { juce::URL (repository + "/blob/main/LICENSE").launchInDefaultBrowser(); });
    fl::showContextMenu (menu, target, false);
}

void ChanSamplerEditor::buildSampleSheet()
{
    auto& sheet = smpSheet;

    // File (FL's GrooveBox)
    auto& file = panel (sheet, {}, 7, 7, 270, 35);
    text (file, "File", 7, 10, 24, 15, 11.0f, true);
    fileName = &text (file, {}, 33, 10, 159, 15, 11.7f);
    fileName->setColour (juce::Label::textColourId, fl::colour::fileName);
    auto& open = own (std::make_unique<fl::IconButton> (fl::Icon::folder), file, { 193, 7, 32, 23 });
    open.setTooltip ("Load sample file");
    open.onClick = [this] { chooseFile(); };
    auto& locate = own (std::make_unique<fl::IconButton> (fl::Icon::locate), file, { 225, 7, 19, 23 });
   #if JUCE_WINDOWS
    locate.setTooltip ("Show sample file in Explorer");
   #elif JUCE_MAC
    locate.setTooltip ("Show sample file in Finder");
   #else
    locate.setTooltip ("Show sample file in its folder");
   #endif
    locate.onClick = [this] { if (samplerProcessor.getSampleFile().existsAsFile()) samplerProcessor.getSampleFile().revealToUser(); };
    auto& remove = own (std::make_unique<fl::IconButton> (fl::Icon::close), file, { 244, 7, 19, 23 });
    remove.setTooltip ("Remove sample");
    remove.onClick = [this] { samplerProcessor.removeSample(); };

    // Content (FL's WavePanel). FL's Keep on disk and Resample only change how
    // FL holds the file in memory, so they are left out. Regions / slice
    // markers feed the slice methods.
    auto& content = panel (sheet, "Content", 7, 50, 270, 110);
    led (content, pid::loadRegions, "Load regions", 3, 26, 128, 21);
    led (content, pid::loadSlices, "Load slice markers", 3, 46, 128, 21);
    text (content, "Declicking mode", 9, 76, 98, 23, 11.7f);
    combo (content, pid::declick, 111, 76, 148, 23);

    // Playback (FL's LoopPanel: knobs left, switches right)
    auto& playback = panel (sheet, "Playback", 7, 168, 270, 79);
    knob (playback, pid::startOffset, 17, 26, 34);
    caption (playback, "START OFFSET", 4, 63, 60, 9);
    loopKnobs[0] = &knob (playback, pid::loopStart, 70, 26, 34);
    static_cast<ParamKnob&> (*loopKnobs[0]).onUserChange = [this] { samplerProcessor.applyLoopLink (pid::loopStart); };
    caption (playback, "START", 57, 63, 60, 9);
    // FL: the link symbol sits on the caption row between START and END (START carries END along).
    own (std::make_unique<fl::IconView> (fl::Icon::link, fl::colour::caption), playback, { 104, 64, 9, 9 });
    loopKnobs[1] = &knob (playback, pid::loopEnd, 112, 26, 34);
    static_cast<ParamKnob&> (*loopKnobs[1]).onUserChange = [this] { samplerProcessor.applyLoopLink (pid::loopEnd); };
    caption (playback, "END", 99, 63, 60, 9);
    // FL draws these two lights 3 px further left and 1 px higher than its other switches (measured).
    led (playback, pid::useLoop, "Use loop points", 162, 23, 103, 21);
    led (playback, pid::pingPong, "Ping pong loop", 162, 43, 103, 21);

    // Time stretching (FL's StretchPanel layout)
    auto& stretch = panel (sheet, "Time stretching", 283, 7, 270, 76);
    {
        // FL switches Resample to Auto when PITCH is let go away from 0
        // (resampling cannot shift pitch on its own); the menu stays as it is
        // while dragging (measured in FL).
        auto& pk = static_cast<ParamKnob&> (knob (stretch, pid::pitch, 8, 26, 32));
        auto* modeParam = samplerProcessor.apvts.getParameter (pid::stretchMode);
        auto* pitchParam = samplerProcessor.apvts.getParameter (pid::pitch);
        auto toAuto = [modeParam, pitchParam]
        {
            if (juce::roundToInt (modeParam->convertFrom0to1 (modeParam->getValue())) == (int) StretchMode::resample
                && std::abs (pitchParam->convertFrom0to1 (pitchParam->getValue())) > 0.5f)
                setParameterAsGesture (*modeParam, (float) StretchMode::autoMode);
        };
        pk.onDragEnd = toAuto;
        pk.onUserChange = [&pk, toAuto] { if (! pk.isMouseButtonDown()) toAuto(); };   // the wheel and the menu
    }
    caption (stretch, "PITCH", 10, 61, 28, 7);
    knob (stretch, pid::stretchMul, 52, 26, 32);   caption (stretch, "MUL", 56, 61, 28, 7);
    {
        auto& t = static_cast<ParamKnob&> (knob (stretch, pid::stretchTime, 88, 26, 32));
        t.menu.noTypeIn = true;   // as FL's TIME menu
        // "Project tempo": the sample's own length at the host tempo (FL: 2 s
        // at 130 BPM gives 1:01:08).
        t.menu.computedPresets["Project tempo"] = [this]
        {
            auto s = samplerProcessor.getSampleForDisplay();
            if (s == nullptr || s->playEnd <= 0) return -1.0f;
            return (float) ((double) s->playEnd / s->sourceRate * displayBpm.load() / 60.0);
        };
        // "Autodetect...": the sample's tempo (FL's Tempo detection), then
        // TIME = its length in beats at that tempo (FL: a 2-bar loop at 128 BPM
        // gives 2:00:00), so it plays at the project's tempo.
        t.menu.actions["Autodetect..."] = [this]
        {
            waveform.detectTempo ([this] (double bpm)
            {
                const auto raw = samplerProcessor.getRaw();
                setParam (pid::stretchTime, bpm > 0 ? (float) tempo::beats (raw->audio.getNumSamples(), raw->sampleRate, bpm) : 0.0f);
            });
        };
        t.menu.presets = { { "(none)", 0.0f }, { "Autodetect...", 0.0f }, { "Project tempo", 0.0f }, { "-", 0.0f }, { "1 beat", 1.0f }, { "2 beats", 2.0f }, { "1 bar", 4.0f },
                      { "2 bars", 8.0f }, { "3 bars", 12.0f }, { "4 bars", 16.0f } };
    }
    caption (stretch, "TIME", 90, 61, 28, 7);
    text (stretch, "Mode", 126, 22, 40, 23, 11.7f);
    auto& mode = own (std::make_unique<fl::ComboBox>(), stretch, comboBounds (167, 22, 95, 23));
    {
        // FL's menu with its group headings; item ids follow the parameter's choices.
        struct Entry { const char* heading; int index; };
        const Entry entries[] = { { "Realtime", 0 }, { nullptr, 1 }, { nullptr, 2 }, { "Elastique", 3 }, { nullptr, 4 },
                                  { "Special", 5 }, { nullptr, 6 }, { "-", 7 }, { "Legacy elastique", 8 }, { nullptr, 9 },
                                  { nullptr, 10 }, { nullptr, 11 } };
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (samplerProcessor.apvts.getParameter (pid::stretchMode));
        for (auto& e : entries)
        {
            if (e.heading != nullptr)
            {
                if (juce::String (e.heading) == "-") mode.getRootMenu()->addColumnBreak();   // FL: second column
                else                                 mode.addSectionHeading (e.heading);
            }
            mode.addItem (choice->choices[e.index], e.index + 1);
        }
        mode.setTooltip ("Stretching method. Resample: pitch follows speed. Others keep length and pitch independent "
                         "(granular stretch; slice modes restart at transients).");
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (samplerProcessor.apvts, pid::stretchMode, mode));
    }
    {
        // Under the menu FL names the method Auto uses: always "e3 generic"
        // (it stays so with PITCH back at 0, measured in FL).
        auto& autoText = text (stretch, {}, 205, 49, 60, 14, 10.0f);
        autoText.setColour (juce::Label::textColourId, fl::colour::dim);
        auto& apvts = samplerProcessor.apvts;
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (pid::stretchMode));
        auto update = [&autoText, choice] (float value)   // the new value (the stored one can lag behind)
        {
            autoText.setText (juce::roundToInt (value) == (int) StretchMode::autoMode ? choice->choices[(int) StretchMode::e3Generic] : juce::String(),
                              juce::dontSendNotification);
        };
        paramAttachments.push_back (std::make_unique<juce::ParameterAttachment> (*choice, update));
        update (apvts.getRawParameterValue (pid::stretchMode)->load());
    }
    {
        // FORMANT slider: FL shows it under the menu in Stretch Pro mode only.
        // MIDI pitch bend range sits here otherwise; FORMANT takes its place in Stretch Pro.
        auto& rangeText = text (stretch, "Range", 126, 50, 40, 20, 11.5f);
        auto& range = digi (stretch, pid::pitchRange, 167, 50, 36, 20, false);
        range.setTooltip ("Pitch bend range (semitones)");
        auto& fcap = caption (stretch, "FORMANT", 124, 56, 40, 7);
        auto& fs = hslider (stretch, pid::formant, 163, 49, 98, 20);
        auto* modeParam = samplerProcessor.apvts.getParameter (pid::stretchMode);
        auto show = [&fcap, &fs, &rangeText, &range, modeParam] (float)
        {
            const bool pro = juce::roundToInt (modeParam->convertFrom0to1 (modeParam->getValue())) == (int) StretchMode::stretchPro;
            fcap.setVisible (pro);
            fs.setVisible (pro);
            rangeText.setVisible (! pro);
            range.setVisible (! pro);
        };
        auto att = std::make_unique<juce::ParameterAttachment> (*modeParam, show);
        att->sendInitialUpdate();
        paramAttachments.push_back (std::move (att));
    }

    // Precomputed effects: Tools (wrench) and Effects (valve) views
    auto& fx = panel (sheet, "Precomputed effects", 283, 91, 270, 156, 45);
    auto& tools = group (fx, 0, 0, 268, 156);
    auto& effects = group (fx, 1, 22, 265, 130);
    effects.setVisible (false);

    auto& fxSel = own (std::make_unique<fl::IconTabs> (std::vector<fl::Icon> { fl::Icon::wrench, fl::Icon::valve }, false), fx, { 0, 1, 52, 18 });
    fxTabs = &fxSel;
    fxSel.selectedColour = fl::colour::fxIconOn;   // FL: the chosen icon amber, the other light grey (measured)
    fxSel.unselectedColour = fl::colour::fxIconOff;
    fxSel.setTooltip ("Tools / Effects");
    fxSel.onChange = [&tools, &effects] (int i)
    {
        tools.setVisible (i == 0);
        effects.setVisible (i == 1);
    };

    led (tools, pid::pcRemoveDC,        "Remove DC offset", 3, 25, 128, 23);
    led (tools, pid::pcNormalize,       "Normalize",        3, 47, 128, 23);
    led (tools, pid::pcReverse,         "Reverse",          3, 69, 128, 23);
    led (tools, pid::pcReversePolarity, "Reverse polarity", 131, 25, 128, 23);
    led (tools, pid::pcFadeStereo,      "Fade stereo",      131, 47, 128, 23);
    led (tools, pid::pcSwapStereo,      "Swap stereo",      131, 69, 128, 23);
    knob (tools, pid::pcStart,     7,   104, 34);  caption (tools, "SMP START", -3, 141, 54, 7);
    knob (tools, pid::pcLength,    49,  104, 34);  caption (tools, "LENGTH", 51, 141, 31, 7);
    knob (tools, pid::pcFadeIn,    103, 104, 34);  caption (tools, "IN", 106, 141, 28, 7);
    knob (tools, pid::pcFadeOut,   142, 104, 34);  caption (tools, "OUT", 145, 141, 28, 7);
    knob (tools, pid::pcCrossfade, 189, 104, 34);  caption (tools, "CROSSFADE", 182, 141, 48, 7);
    knob (tools, pid::pcTrim,      228, 104, 34);  caption (tools, "TRIM", 231, 141, 28, 7);

    // FL's FXLegacyPanel layout
    own (std::make_unique<Fill> (fl::colour::sheet, 2.0f), effects, { 4, 2, 42, 69 });   // FL: a sheet-coloured well behind BOOST and CLIP (measured)
    knob (effects, pid::pcBoost, 7, 5, 34);          caption (effects, "BOOST", 10, 42, 28, 7);
    led (effects, pid::pcBoostClip, "CLIP", 3, 55, 38, 13).getProperties().set ("captionStyle", true);   // FL: an 11 px light with a knob caption
    knob (effects, pid::pcEq, 49, 5, 34);            caption (effects, "EQ", 54, 42, 28, 7);
    knob (effects, pid::pcRmMix, 97, 5, 34);         caption (effects, "RM MIX", 100, 42, 28, 7);
    knob (effects, pid::pcRmFreq, 137, 5, 34);       caption (effects, "RM FREQ", 137, 42, 34, 7);
    knob (effects, pid::pcCut, 188, 4, 34);          caption (effects, "CUT", 191, 42, 28, 7);
    knob (effects, pid::pcRes, 227, 4, 34);          caption (effects, "RES", 230, 42, 28, 7);
    knob (effects, pid::pcReverb, 49, 82, 34);       caption (effects, "REVERB", 27, 119, 36, 7);
    knob (effects, pid::pcStereoDelay, 97, 82, 34);  caption (effects, "STEREO DELAY", 84, 119, 60, 7);
    knob (effects, pid::pcPogo, 227, 82, 34);        caption (effects, "POGO", 230, 119, 28, 7);

    // Reverb type switch (A / B): FL's upright pill, its knob at the bottom for B
    auto& revType = own (std::make_unique<fl::SlideSwitch>(), effects, { 14, 86, 24, 27 });
    revType.setTooltip ("Reverb type: A = close source, tight space; B = distant source, larger space");
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (samplerProcessor.apvts, pid::pcReverbType, revType));

    // Sample view
    waveform.setBounds (7, 255, 546, 134);   // FL's view and bar (measured)
    sheet.addAndMakeVisible (waveform);
}

juce::Component& ChanSamplerEditor::buildTargetPage (const char* prefix, bool hasAmount, bool hasEnvelope)
{
    auto& page = group (insSheet, 0, 0, 452, 240);
    auto id = [prefix] (const char* s) { return pid::env (prefix, s); };

    // Envelope panel (FL's EnvPanel; none on the Panning page). The header
    // LED is "Use envelope".
    if (hasEnvelope)
    {
        auto& env = panel (page, "Envelope", 7, 42, 287, 195, 16);
        auto& envMenu = own (std::make_unique<fl::MenuArrow> (true), env, { 262, 4, 20, 20 });
        envMenu.onClick = [this, p = juce::String (prefix), &envMenu] { showEnvelopeMenu (p, envMenu); };
        auto* envOn = samplerProcessor.apvts.getParameter (id ("EnvOn"));
        // FL turns an envelope on as soon as one of its knobs is edited.
        auto envKnob = [this, envOn] (juce::Component& parent, const juce::String& paramId, int x, int y, int size) -> juce::Slider&
        {
            auto& s = knob (parent, paramId, x, y, size, fl::colour::green);   // FL: green arcs on INS
            s.onDragStart = [envOn]
            {
                if (envOn->getValue() < 0.5f)
                    envOn->setValueNotifyingHost (1.0f);
            };
            return s;
        };
        // The switch covers the "Envelope" title too: clicking the text toggles it, as in FL.
        led (env, id ("EnvOn"), {}, -2, 1, 82, 17).setTooltip ("Use envelope");
        own (std::make_unique<EnvelopeDisplay> (samplerProcessor.apvts, prefix), env, { 6, 26, 273, 83 });
        envKnob (env, id ("Del"),  6,   115, 30);  caption (env, "DELAY", 7,   148, 28, 7);
        envKnob (env, id ("Att"),  40,  115, 30);  caption (env, "ATT",   41,  148, 28, 7);
        envKnob (env, id ("Hold"), 74,  115, 30);  caption (env, "HOLD",  75,  148, 28, 7);
        envKnob (env, id ("Dec"),  108, 115, 30);  caption (env, "DEC",   109, 148, 28, 7);
        envKnob (env, id ("Sus"),  142, 115, 30);  caption (env, "SUS",   143, 148, 28, 7);
        envKnob (env, id ("Rel"),  176, 115, 30);  caption (env, "REL",   177, 148, 28, 7);
        // FL's tension knobs show their arc but no value dot, and their
        // captions are letter-spaced (measured)
        for (auto [key, kx] : { std::pair<const char*, int> { "AttT", 44 }, { "DecT", 112 }, { "RelT", 180 } })
            envKnob (env, id (key), kx, 158, 22).getProperties().set ("flNoDot", true);
        caption (env, "TENSION", 68, 166, 44, 7).getProperties().set ("captionTracking", 0.85);
        caption (env, "TENSION", 136, 166, 44, 7).getProperties().set ("captionTracking", 0.85);
        if (hasAmount)
        {
            envKnob (env, id ("EnvAmt"), 245, 115, 34);
            caption (env, "AMT", 248, 152, 28, 7);
        }
        // FL: "Tempo" (tempo-based times) at the bottom right
        auto& envTempo = led (env, id ("EnvTempo"), "Tempo", 229, 170, 60, 18);
        envTempo.setTooltip ("Tempo-based time");
        envTempo.getProperties().set ("textFirst", true);
    }

    auto lfoKnob = [this] (juce::Component& parent, const juce::String& paramId, int x, int y, int size) -> juce::Slider&
    {
        return knob (parent, paramId, x, y, size, fl::colour::green);
    };

    // LFO panel; the header holds the shape selector.
    auto& lfo = panel (page, "LFO", 300, 42, 146, 195, 69);
    auto& shapes = own (std::make_unique<fl::IconTabs> (std::vector<fl::Icon> { fl::Icon::sine, fl::Icon::triangle, fl::Icon::square }, false),
                        lfo, { 0, 0, 69, 19 });
    shapes.setTooltip ("LFO shape");
    auto* shapeParam = samplerProcessor.apvts.getParameter (id ("LfoShape"));
    auto shapeAttachment = std::make_unique<juce::ParameterAttachment> (*shapeParam,
        [&shapes] (float v) { shapes.setSelected (juce::roundToInt (v), false); });
    auto* shapeAtt = shapeAttachment.get();
    shapes.onChange = [shapeAtt] (int i) { shapeAtt->setValueAsCompleteGesture ((float) i); };
    shapeAttachment->sendInitialUpdate();
    paramAttachments.push_back (std::move (shapeAttachment));

    own (std::make_unique<LfoDisplay> (samplerProcessor.apvts, prefix), lfo, { 6, 26, 132, 83 });
    lfoKnob (lfo, id ("LfoDel"),   6,   115, 30);  caption (lfo, "DELAY", 7,   148, 28, 7);
    lfoKnob (lfo, id ("LfoAtt"),   40,  115, 30);  caption (lfo, "ATT",   41,  148, 28, 7);
    lfoKnob (lfo, id ("LfoAmt"),   74,  115, 30);  caption (lfo, "AMT",   75,  148, 28, 7);
    lfoKnob (lfo, id ("LfoSpeed"), 108, 115, 30);  caption (lfo, "SPEED", 109, 148, 28, 7);
    // FL: "Tempo" (tempo-based speed / times) and "Global" (song-wide phase)
    auto& lfoTempo = led (lfo, id ("LfoTempo"), "Tempo", 32, 170, 58, 18);
    lfoTempo.setTooltip ("Tempo-based time");
    lfoTempo.getProperties().set ("textFirst", true);
    auto& lfoGlobal = led (lfo, id ("LfoGlobal"), "Global", 90, 168, 60, 18);
    lfoGlobal.setTooltip ("Global / retriggered LFO phase");
    lfoGlobal.getProperties().set ("textFirst", true);

    return page;
}

void ChanSamplerEditor::buildInsSheet()
{
    // Target selector: Panning, Volume, Mod X, Mod Y, Pitch
    auto& targets = own (std::make_unique<fl::TextTabs> (juce::StringArray { "Panning", "Volume", "Mod X", "Mod Y", "Pitch" }, 11.7f, false),
                         insSheet, { 8, 6, 260, 29 });

    std::array<juce::Component*, 5> pages {
        &buildTargetPage (pid::panPrefix,   false, false),
        &buildTargetPage (pid::volPrefix,   false),
        &buildTargetPage (pid::modXPrefix,  true),
        &buildTargetPage (pid::modYPrefix,  true),
        &buildTargetPage (pid::pitchPrefix, true) };

    targets.onChange = [pages] (int i)
    {
        for (size_t k = 0; k < pages.size(); ++k)
            pages[k]->setVisible ((int) k == i);
    };
    targets.setSelected (1, false);
    targetTabs = &targets;
    for (size_t k = 0; k < pages.size(); ++k)
        pages[k]->setVisible (k == 1);

    // Filter panel (shared by all targets)
    auto& filter = panel (insSheet, "Filter", 452, 42, 101, 195);
    caption (filter, "MOD X", 45, 13, 37, 9, 9.5f);   // FL sets the filter captions larger (measured: 7 px caps)
    knob (filter, pid::cutoff, 5, 22, 66, fl::colour::bigDot);
    knob (filter, pid::resonance, 32, 87, 66, fl::colour::bigDotGold);
    caption (filter, "MOD Y", 4, 145, 37, 9, 9.5f);
    combo (filter, pid::filterType, 5, 167, 89, 23);   // FL's field: measured a pixel higher and 89 px wide

    own (std::make_unique<RootNotePanel> (samplerProcessor.apvts, samplerProcessor.keyboardState, samplerProcessor.history), insSheet, { 7, 245, 546, 137 });
}

void ChanSamplerEditor::buildFuncSheet()
{
    auto& sheet = funcSheet;

    auto& levels = panel (sheet, "Levels adjustment", 7, 7, 178, 100);
    knob (levels, pid::pan,    6,   26, 38, fl::colour::knobTeal);  caption (levels, "PAN",   6,   69, 38, 9);
    knob (levels, pid::volume, 48,  26, 38, fl::colour::green);  caption (levels, "VOL",   48,  69, 38, 9);
    knob (levels, pid::xOfs,   90,  26, 38, fl::colour::knobYellow);  caption (levels, "MOD X", 90,  69, 38, 9);
    knob (levels, pid::yOfs,   132, 26, 38);  caption (levels, "MOD Y", 132, 69, 38, 9);

    auto& poly = panel (sheet, "Polyphony", 191, 7, 178, 100);
    text (poly, "Max", 7, 30, 26, 23, 11.7f);
    digi (poly, pid::maxPoly, 33, 30, 32, 23, false);
    auto& slide = knob (poly, pid::slide, 82, 26, 30);
    auto& slideCap = caption (poly, "SLIDE", 83, 59, 28, 7);
    led (poly, pid::porta, "Porta", 3, 74, 56, 19);
    led (poly, pid::mono,  "Mono",  59, 74, 56, 19);
    {
        // As in FL, SLIDE is greyed out until Mono or Porta is on. The states
        // come from the attachments' own values: a parameter's listeners run
        // before its raw value is stored, so reading that lagged a click.
        auto& apvts = samplerProcessor.apvts;
        auto states = std::make_shared<std::array<bool, 2>> ();
        auto update = [&slide, &slideCap, states]
        {
            const bool on = (*states)[0] || (*states)[1];
            slide.setEnabled (on);
            slideCap.setEnabled (on);
        };
        const char* ids[] = { pid::mono, pid::porta };
        for (size_t i = 0; i < 2; ++i)
        {
            (*states)[i] = apvts.getRawParameterValue (ids[i])->load() > 0.5f;
            paramAttachments.push_back (std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (ids[i]),
                [update, states, i] (float v) { (*states)[i] = v > 0.5f; update(); }));
        }
        update();
    }

    // Tracking: Vol (velocity) / Key pages
    auto& tracking = panel (sheet, "Tracking", 376, 7, 177, 100, 52);
    auto& velPage = group (tracking, 0, 0, 177, 100);
    auto& keyPage = group (tracking, 0, 0, 177, 100);
    keyPage.setVisible (false);
    for (auto [page, ids] : { std::pair<juce::Component*, std::array<const char*, 4>> { &velPage, { pid::velPan, pid::velX, pid::velY, pid::velMid } },
                              std::pair<juce::Component*, std::array<const char*, 4>> { &keyPage, { pid::keyPan, pid::keyX, pid::keyY, pid::keyMid } } })
    {
        // FL's controls spread over the panel's width (Polyphony and Tracking share the row equally here)
        knob (*page, ids[0], 12,  26, 30, fl::colour::knobTeal);   caption (*page, "PAN",   12,  59, 30, 7);
        knob (*page, ids[1], 73,  26, 30, fl::colour::knobYellow); caption (*page, "MOD X", 73,  59, 30, 7);
        knob (*page, ids[2], 134, 26, 30, fl::colour::knobBlue);   caption (*page, "MOD Y", 134, 59, 30, 7);
        text (*page, "Mid", 13, 76, 28, 14, 10.0f);
        hslider (*page, ids[3], 33, 74, 141, 22);
    }
    auto& trackTabs = own (std::make_unique<fl::TextTabs> (juce::StringArray { "Vol", "Key" }, 11.5f, true), tracking, { 0, 0, 52, 19 });
    trackTabs.nudges = { 1, 0 };   // FL: Vol a pixel further right (measured)
    trackTabs.setTooltip ("Velocity / keyboard tracking");
    trackTabs.onChange = [&velPage, &keyPage] (int i)
    {
        velPage.setVisible (i == 0);
        keyPage.setVisible (i == 1);
    };
    trackTabs.setSelected (1, true);   // FL opens on Key

    // Group (FL layout): Cut / By group numbers, shared with other Chan
    // Sampler instances in the host (CutGroups.h), and Cut self.
    auto& groupPanel = panel (sheet, "Group", 7, 115, 69, 122);
    text (groupPanel, "Cut", 2, 29, 26, 22, 11.7f, false, juce::Justification::centredRight);
    digi (groupPanel, pid::cutGroup, 30, 29, 32, 23, false).setTooltip ("Cut group: notes on this instance cut instances whose By is this number");
    text (groupPanel, "By", 2, 58, 26, 22, 11.7f, false, juce::Justification::centredRight);
    digi (groupPanel, pid::cutBy, 30, 58, 32, 23, false).setTooltip ("Cut by group: this instance is cut by instances whose Cut is this number");
    auto& cutSelf = own (std::make_unique<juce::TextButton> ("Cut self"), groupPanel, { 4, 88, 61, 31 });
    cutSelf.setClickingTogglesState (true);
    cutSelf.getProperties().set ("bigText", true);
    cutSelf.setTooltip ("Cut itself when retriggered");
    buttonAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (samplerProcessor.apvts, pid::cutItself, cutSelf));

    // Arpeggiator: the direction selector sits in the header, as in FL.
    auto& arpPanel = panel (sheet, "Arpeggiator", 82, 115, 231, 122, 150);
    // FL draws the directions as symbols: off, up, down, up/down, sticky, random.
    auto& arpDir = own (std::make_unique<fl::IconTabs> (std::vector<fl::Icon> { fl::Icon::arpOff, fl::Icon::arpUp, fl::Icon::arpDown, fl::Icon::arpUpDown, fl::Icon::arpSticky, fl::Icon::arpRandom }, false),
                        arpPanel, { 0, 0, 114, 19 });
    arpDir.selectedColour = fl::colour::text;   // FL: the chosen symbol sits on a dark box, all of them light
    arpDir.setTooltip ("Arpeggio direction: off, up, down, up/down, up/down (ends twice), random");
    auto* dirParam = samplerProcessor.apvts.getParameter (pid::arpDir);
    auto dirAttachment = std::make_unique<juce::ParameterAttachment> (*dirParam,
        [&arpDir] (float v) { arpDir.setSelected (juce::roundToInt (v), false); });
    auto* dirAtt = dirAttachment.get();
    arpDir.onChange = [dirAtt] (int i) { dirAtt->setValueAsCompleteGesture ((float) i); };
    dirAttachment->sendInitialUpdate();
    paramAttachments.push_back (std::move (dirAttachment));

    knob (arpPanel, pid::arpTime, 6,  26, 30);  caption (arpPanel, "TIME", 6,  59, 30, 7);
    knob (arpPanel, pid::arpGate, 40, 26, 30);  caption (arpPanel, "GATE", 40, 59, 30, 7);
    text (arpPanel, "Range", 78, 30, 40, 23, 11.7f);
    digi (arpPanel, pid::arpRange, 115, 30, 26, 23, false);
    text (arpPanel, "Repeat", 158, 30, 44, 23, 11.7f);
    digi (arpPanel, pid::arpRepeat, 198, 30, 26, 23, false);
    led (arpPanel, pid::arpSlide, "Slide", 3, 84, 60, 21);
    text (arpPanel, "Chord", 80, 84, 40, 23, 11.7f);
    auto& chordBox = own (std::make_unique<fl::ComboBox>(), arpPanel, comboBounds (116, 84, 107, 23));   // FL's field is 107 px wide (measured)
    {
        chordBox.addSectionHeading ("Automatic");
        chordBox.addItem ("Auto", 1);
        chordBox.addItem ("Auto (sustain)", 2);
        chordBox.addItem ("(none)", 3);
        const char* groups[] = { "Triads", "Sixths", "Sevenths", "Ninths", "Elevenths", "Thirteenths" };
        int lastGroup = -1;
        for (int i = 0; i < numChordPresets; ++i)
        {
            if (chordPresets[i].group != lastGroup)
            {
                lastGroup = chordPresets[i].group;
                chordBox.addSectionHeading (groups[juce::jlimit (0, 5, lastGroup)]);
            }
            chordBox.addItem (chordPresets[i].shortName, i + 4);
        }
        chordBox.setTooltip ("Arpeggio chord: Auto uses the held keys; presets build the chord on the lowest key");
        comboAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (samplerProcessor.apvts, pid::arpChord, chordBox));
    }

    // Echo delay / fat mode
    auto& echoPanel = panel (sheet, "Echo delay / fat mode", 319, 115, 234, 122, 10);
    auto& echoMenu = own (std::make_unique<fl::MenuArrow>(), echoPanel, { 2, 2, 12, 16 });
    echoMenu.onClick = [this, &echoMenu] { showEchoPresets (echoMenu); };
    // FL colours the echo knobs by what they control (measured): green, teal, yellow, blue, pink, amber.
    const std::tuple<const char*, const char*, juce::Colour> echoKnobs[] = {
        { pid::echoFeed, "FEED", fl::colour::green }, { pid::echoPan, "PAN", fl::colour::knobTeal }, { pid::echoX, "MOD X", fl::colour::knobYellow },
        { pid::echoY, "MOD Y", fl::colour::knobBlue }, { pid::echoPitch, "PITCH", fl::colour::knobPink }, { pid::echoTime, "TIME", fl::colour::amber } };
    const int echoX[] = { 6, 38, 80, 112, 154, 196 };   // FL (measured): FEED / PAN and MOD X / MOD Y sit in pairs
    int k = 0;
    for (auto [pidName, label, ring] : echoKnobs)
    {
        knob (echoPanel, pidName, echoX[k], 26, 30, ring);
        caption (echoPanel, label, echoX[k], 59, 30, 7);
        ++k;
    }
    text (echoPanel, "Echoes", 8, 84, 48, 23, 11.7f);
    digi (echoPanel, pid::echoCount, 48, 85, 32, 23, false);
    // FL stacks the two switches on the right.
    for (auto* sw : { &led (echoPanel, pid::echoPing, "Ping pong", 153, 76, 76, 18),    // FL: the lights at the right (measured),
                      &led (echoPanel, pid::echoFat,  "Fat mode",  158, 96, 71, 18) })  // the labels ending 4 px before them
    {
        sw->getProperties().set ("textFirst", true);
        sw->getProperties().set ("ledRight", true);
    }

    own (std::make_unique<RootNotePanel> (samplerProcessor.apvts, samplerProcessor.keyboardState, samplerProcessor.history), sheet, { 7, 245, 546, 137 });
}

//==============================================================================
void ChanSamplerEditor::setParam (const juce::String& id, float value)
{
    if (auto* p = samplerProcessor.apvts.getParameter (id))
        setParameterAsGesture (*p, value);
}

void ChanSamplerEditor::showEchoPresets (juce::Component& anchor)
{
    // FL's echo presets, read with Copy value: pan = (position - 0.5) * 4,
    // pitch = (position - 0.5) * 2400 cents, time = position * 8 steps. Mod X / Y
    // stay centred in all of them.
    struct Echo { const char* name; float feed, pan, pitch, time; int count; bool ping, fat; };
    static const Echo presets[] = {
        { "Reset",                        0.0f,   0.0f,  0.0f, 3.0f,         4, false, false },
        { "Echo - classic",               0.25f,  0.0f,  0.0f, 3.0f,         4, false, false },
        { "Echo - ping pong",             0.25f, -2.0f,  0.0f, 3.0f,         4, true,  false },
        { "Fat - detuned",                0.875f, 0.5f,  3.0f, 0.0f,         2, false, true  },
        { "Fat - more detuned",           0.875f, 0.5f,  6.0f, 0.0f,         2, false, true  },
        { "Fat - detuned + time offset",  0.875f, 0.5f,  3.0f, 1.0f / 48.0f, 2, false, true  },
        { "Fat - 8 voices",               0.875f, 0.25f, 2.0f, 0.0f,         4, false, true  },
    };
    juce::PopupMenu menu;
    for (size_t i = 0; i < std::size (presets); ++i)
    {
        if (i == 1 || i == 3)
            menu.addSeparator();
        const auto& e = presets[i];
        menu.addItem (e.name, [this, e]
        {
            const History::Transaction edit (samplerProcessor.history, "Echo preset");
            setParam (pid::echoFeed, e.feed);
            setParam (pid::echoPan, e.pan);
            setParam (pid::echoX, 0.0f);
            setParam (pid::echoY, 0.0f);
            setParam (pid::echoPitch, e.pitch);
            setParam (pid::echoTime, e.time);
            setParam (pid::echoCount, (float) e.count);
            setParam (pid::echoPing, e.ping ? 1.0f : 0.0f);
            setParam (pid::echoFat, e.fat ? 1.0f : 0.0f);
        });
    }
    if (auto* arrow = dynamic_cast<fl::MenuArrow*> (&anchor))
        arrow->showMenu (menu);
    else
        fl::showContextMenu (menu, anchor, false);
}

namespace
{
    // What an envelope preset holds: the target page's envelope and LFO.
    const char* const envelopeKeys[] = { "EnvOn", "Del", "Att", "Hold", "Dec", "Sus", "Rel", "AttT", "DecT", "RelT", "EnvAmt", "EnvTempo",
                                         "LfoShape", "LfoDel", "LfoAtt", "LfoAmt", "LfoSpeed", "LfoTempo", "LfoGlobal" };

    juce::File envelopeFolder() { return ChanSamplerProcessor::userFolder ("Envelopes"); }
}

void ChanSamplerEditor::showEnvelopeMenu (const juce::String& prefix, juce::Component& anchor)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Presets");
    auto files = envelopeFolder().findChildFiles (juce::File::findFiles, false, "*.csenv");
    files.sort();
    for (const auto& f : files)
        menu.addItem (f.getFileNameWithoutExtension(), [this, prefix, f]
        {
            auto xml = juce::parseXML (f);
            if (xml == nullptr)
            {
                fl::showDialog (canvas, "Chan Sampler", "Could not read " + f.getFileName() + ".", true);
                return;
            }
            const History::Transaction edit (samplerProcessor.history, "Envelope preset");
            for (auto* key : envelopeKeys)
                if (xml->hasAttribute (key) && samplerProcessor.apvts.getParameter (pid::env (prefix.toRawUTF8(), key)) != nullptr)
                    setParam (pid::env (prefix.toRawUTF8(), key), (float) xml->getDoubleAttribute (key));
        });
    menu.addItem ("Save as...", [this, prefix]
    {
        envelopeFolder().createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Save envelope preset", envelopeFolder().getChildFile ("Envelope.csenv"), "*.csenv");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, prefix] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file == juce::File())
                                      return;
                                  juce::XmlElement xml ("CHANSAMPLER_ENVELOPE");
                                  for (auto* key : envelopeKeys)
                                      if (auto* p = samplerProcessor.apvts.getParameter (pid::env (prefix.toRawUTF8(), key)))
                                          xml.setAttribute (key, p->convertFrom0to1 (p->getValue()));
                                  if (! xml.writeTo (file.withFileExtension (".csenv")))
                                      fl::showDialog (canvas, "Chan Sampler", "Could not save " + file.withFileExtension (".csenv").getFullPathName() + ".", true);
                              });
    });
    if (auto* arrow = dynamic_cast<fl::MenuArrow*> (&anchor))
        arrow->showMenu (menu);
    else
        fl::showContextMenu (menu, anchor, false);
}

//==============================================================================
void ChanSamplerEditor::showSheetForTesting (int sheet, int insPage)
{
    showSheet (sheet);
    if (insPage >= 0 && sheet == 0 && fxTabs != nullptr)
        fxTabs->setSelected (insPage, true);
    else if (insPage >= 0 && targetTabs != nullptr)
        targetTabs->setSelected (insPage, true);
}

void ChanSamplerEditor::showSheet (int index)
{
    smpSheet.setVisible (index == 0);
    insSheet.setVisible (index == 1);
    funcSheet.setVisible (index == 2);
    if (sheetTabs != nullptr)
        sheetTabs->setSelected (index, false);
}

void ChanSamplerEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &samplerProcessor.history)
        refreshHistoryButtons();
    else
        refreshSample();
}

void ChanSamplerEditor::refreshSample()
{
    waveform.setSample (samplerProcessor.getSampleForDisplay());
    const auto raw = samplerProcessor.getRaw();
    // FL: START and END cannot be turned without a sample (they keep their look)
    for (auto* k : loopKnobs)
        if (k != nullptr)
            static_cast<ParamKnob*> (k)->setLocked (raw->file == juce::File() || raw->missing);
    if (fileName != nullptr)
    {
        fileName->setText (raw->file == juce::File() ? juce::String() : raw->file.getFileName() + (raw->missing ? " (missing)" : ""), juce::dontSendNotification);
        fileName->setColour (juce::Label::textColourId, raw->missing ? fl::colour::ledOn : fl::colour::fileName);
        fileName->setTooltip (raw->missing ? "Not found or unreadable: " + raw->file.getFullPathName() : samplerProcessor.getSampleInfo());
    }
    if (sampleInfo != nullptr)
        sampleInfo->setText (raw->missing ? juce::String ("Sample missing") : samplerProcessor.getSampleInfo(), juce::dontSendNotification);
}

void ChanSamplerEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load sample", samplerProcessor.getSampleFile(),
                                                   samplerProcessor.getFormatManager().getWildcardForAllFormats() + ";*.fst");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  loadFile (fc.getResult());
                          });
}

void ChanSamplerEditor::loadFile (const juce::File& file)
{
    // An FL Studio Sampler preset brings its sample and settings.
    if (flpreset::isPresetFile (file))
    {
        const History::Transaction edit (samplerProcessor.history, "Load preset");
        const auto r = flpreset::import (samplerProcessor, file);
        if (! r.ok || r.message.isNotEmpty())
            fl::showDialog (canvas, "Chan Sampler", r.message, ! r.ok);
        return;
    }
    if (! samplerProcessor.loadSample (file))
        fl::showDialog (canvas, "Chan Sampler", samplerProcessor.getLoadError(), true);
}

bool ChanSamplerEditor::canLoad (const juce::File& f)
{
    return flpreset::isPresetFile (f) || samplerProcessor.getFormatManager().findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

bool ChanSamplerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (waveform.isDraggingSampleOut())   // our own sample on its way out
        return false;
    return std::any_of (files.begin(), files.end(), [this] (const juce::String& f) { return canLoad (juce::File (f)); });
}

void ChanSamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (canLoad (juce::File (f)))
        {
            loadFile (juce::File (f));
            return;
        }
}
