#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// The controls every Chan Sampler editor draws (knobs, switches, menus, dialogs, panels), in the tokens of
// Theme.h. All drawing is our own, in code.
namespace chansampler
{
// Never take keyboard focus: the host keeps its shortcuts (the space bar in FL Studio). Call it on every
// component and on the editor itself; FocusCheck (tools/) verifies the result on the built VST3.
inline void noFocus (juce::Component& c)
{
    c.setWantsKeyboardFocus (false);
    c.setMouseClickGrabsKeyboardFocus (false);
    for (auto* child : c.getChildren()) noFocus (*child);
}

// What the suite's snapshot tool (tools/Snapshot.cpp) may ask of an editor beyond its parameters: meters frozen
// at given levels. An editor with meters derives from it; the tool finds it by dynamic_cast.
struct SnapshotHooks
{
    virtual ~SnapshotHooks() = default;
    virtual void setTestLevels (float inDb, float outDb) = 0;
};
}

namespace chansampler::ui
{
    // The shared controls, drawn as FL draws them. Knobs and switches are
    // rendered per pixel (FL's bevels and glows are not path shapes).
    // pos: 0..1; zeroPos: where the value arc starts (0 for one-sided knobs,
    // 0.5 for two-sided ones). A knob larger than 50 px is FL's filter knob.
    // hover: how far the control's hover highlight has faded in (0..1).
    // showDot false: FL's envelope tension knobs, which have the arc but no value dot (measured)
    void drawKnob (juce::Graphics&, juce::Rectangle<float> area, float pos, float zeroPos, juce::Colour arc, bool enabled, float hover = 0.0f,
                   bool showDot = true);
    // lit: how far the light is on (0..1; FL fades its switches, see Switch).
    void drawLed (juce::Graphics&, juce::Rectangle<float> area, float lit, bool enabled, bool green = false);
    // The round slider thumb with its coloured line (FL's fine tune / Mid sliders).
    void drawSliderThumb (juce::Graphics&, juce::Rectangle<float> area, juce::Colour line, bool enabled, float hover = 0.0f);

    // FL eases a control's hover highlight in and out rather than switching
    // it (measured frame by frame in FL): in, an exponential that is half
    // way after 60 ms and 90 % after 170 ms; out, a straight fade over about
    // 300 ms. A control owns one of these, tells it when the mouse enters and
    // leaves, and the drawing reads the amount through the component's
    // "hover" property.
    // Pressing, dragging and releasing a knob or slider change nothing beyond
    // this highlight (measured: FL's controls look the same pressed as hovered,
    // and the highlight stays while the mouse is held down outside them).
    inline constexpr float hoverFadeIn = 0.2f;           // of the remaining distance per 60 Hz frame
    inline constexpr float hoverFadeOut = 1.0f / 18.0f;   // per frame
    // The filter knob's highlight fades out as an exponential instead, with a
    // 218 ms time constant (measured over 660 ms): of the remaining distance per frame.
    inline constexpr float hoverFadeOutSlow = 0.0736f;
    class HoverFade : private juce::Timer
    {
    public:
        explicit HoverFade (juce::Component& c) : owner (c) {}
        void setTarget (bool over)
        {
            target = over ? 1.0f : 0.0f;
            if (amount != target)
                startTimerHz (60);
        }
        void setSlowOut (bool slow) { slowOut = slow; }   // the filter knob
        float get() const noexcept { return amount; }

    private:
        void timerCallback() override
        {
            if (target > amount)
                amount += (target - amount) * hoverFadeIn;
            else if (slowOut)
                amount -= amount * hoverFadeOutSlow;
            else
                amount -= hoverFadeOut;
            if (std::abs (target - amount) < (slowOut && target < amount ? 0.01f : 0.02f) || amount < 0.0f)
            {
                amount = target;
                stopTimer();
            }
            owner.getProperties().set ("hover", amount);
            owner.repaint();
        }
        juce::Component& owner;
        float amount = 0.0f, target = 0.0f;
        bool slowOut = false;
    };

    // FL's switch (a round light with its label): it toggles the moment the
    // mouse goes down, its light fades in over about 180 ms and out over about
    // 235 ms, and nothing changes under the mouse (measured frame by frame in
    // FL on Use loop points and CLIP). The drawing reads the amount.
    inline constexpr float litFadeIn = 1.0f / 11.0f, litFadeOut = 1.0f / 14.0f;   // per 60 Hz frame
    class Switch : public juce::ToggleButton, private juce::Timer
    {
    public:
        explicit Switch (const juce::String& buttonText = {}) : juce::ToggleButton (buttonText) { setTriggeredOnMouseDown (true); }
        float getLit() const noexcept { return lit; }

    private:
        void buttonStateChanged() override
        {
            const float target = getToggleState() ? 1.0f : 0.0f;
            if (! shown)   // the first state (a restored project's) shows at once
            {
                shown = true;
                lit = target;
            }
            if (lit != target && ! isTimerRunning())
                startTimerHz (60);
        }
        void timerCallback() override
        {
            const float target = getToggleState() ? 1.0f : 0.0f;
            lit = target > lit ? juce::jmin (target, lit + litFadeIn) : juce::jmax (target, lit - litFadeOut);
            if (lit == target)
                stopTimer();
            repaint();
        }
        float lit = 0.0f;
        bool shown = false;
    };

    // FL's small arrows (drop-down fields, panel menus) turn from pointing
    // right to pointing down under the mouse: measured frame by frame in FL (the
    // Filter list, the envelope and echo menus), the triangle rotates
    // rigidly a quarter turn clockwise at a steady rate, over about 160 ms in
    // and 200 ms back, with no wait. While its list or menu is open it stays
    // turned (and lit), whatever the mouse does; once the menu closes it turns
    // back the same way as on a plain leave. The amount (0 right .. 1 down) is
    // read through the component's "turn" property.
    inline constexpr double arrowTurnInMs = 160.0, arrowTurnOutMs = 200.0;
    class ArrowTurn : private juce::Timer
    {
    public:
        // held: while it returns true the arrow stays turned (its menu is open)
        explicit ArrowTurn (juce::Component& c, std::function<bool()> held = {}) : owner (c), isHeld (std::move (held)) {}
        void setTarget (bool over) { mouseOver = over; refresh(); }
        // to call when the hold starts or ends
        void refresh()
        {
            const bool held = isHeld != nullptr && isHeld();
            if (wasHeld && ! held)
                mouseOver = owner.isMouseOver (true);
            wasHeld = held;
            if ((amount != target() || held) && ! isTimerRunning())
            {
                last = juce::Time::getMillisecondCounterHiRes();
                startTimerHz (60);
            }
        }
        float get() const noexcept { return amount; }

    private:
        float target() const { return mouseOver || (isHeld != nullptr && isHeld()) ? 1.0f : 0.0f; }
        void timerCallback() override
        {
            refresh();   // notices the end of a hold
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto dt = juce::jlimit (0.0, 100.0, now - last);
            last = now;
            const auto t = target();
            amount = t > amount ? juce::jmin (t, amount + (float) (dt / arrowTurnInMs))
                                : juce::jmax (t, amount - (float) (dt / arrowTurnOutMs));
            if (amount == t && ! wasHeld)
                stopTimer();
            owner.getProperties().set ("turn", amount);
            owner.repaint();
        }
        juce::Component& owner;
        std::function<bool()> isHeld;
        float amount = 0.0f;
        bool mouseOver = false, wasHeld = false;
        double last = 0.0;
    };
    float turnOf (const juce::Component&);
    // A slider's position (0..1) to draw: its eased one while a wheel glide
    // runs (the "shown" property), else its value's.
    float shownProportion (juce::Slider&);
    // Draws one of those arrows: at turn 0 a triangle pointing right (tipLen
    // wide, baseH tall, centred on rest), at turn 1 the same triangle rotated a
    // quarter turn clockwise (pointing down, centred on down); between the two
    // it is rotated rigidly while its centre moves.
    void drawTurnArrow (juce::Graphics&, juce::Point<float> rest, juce::Point<float> down, float turn, juce::Colour,
                        float baseH, float tipLen);
    // The hover amount of a component: its fade if it has one, else 1 under the mouse.
    float hoverOf (const juce::Component&);
    // FL's push button (Reset, Cut self, A / B); nothing changes under the mouse, pressed it sinks.
    void drawButton (juce::Graphics&, juce::Rectangle<int> area, bool lit, bool highlighted, bool down, bool enabled);

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos,
                               float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawLabel (juce::Graphics&, juce::Label&) override;
        juce::Font getPopupMenuFont() override;
        // The value popup shown while a knob turns: drawn like the knob menus.
        void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;
        juce::Font getSliderPopupFont (juce::Slider&) override;
        int getSliderPopupPlacement (juce::Slider&) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
        // FL has two menu looks: drop-down lists (dark, sectioned, in columns)
        // and the knob right-click menu (grey with underlined shortcut letters).
        void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int w, int h, const juce::PopupMenu::Options&) override;
        void drawPopupMenuItemWithOptions (juce::Graphics&, const juce::Rectangle<int>& area, bool isHighlighted,
                                           const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
        void drawPopupMenuSectionHeaderWithOptions (juce::Graphics&, const juce::Rectangle<int>& area,
                                                    const juce::String& sectionName, const juce::PopupMenu::Options&) override;
        void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int standardHeight,
                                                   int& w, int& h, const juce::PopupMenu::Options&) override;
        void getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int standardHeight,
                                                            int& w, int& h, const juce::PopupMenu::Options&) override;
        void drawPopupMenuColumnSeparatorWithOptions (juce::Graphics&, const juce::Rectangle<int>&, const juce::PopupMenu::Options&) override;
        int getPopupMenuColumnSeparatorWidthWithOptions (const juce::PopupMenu::Options&) override;
        int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return 1; }
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;
        void preparePopupMenuWindow (juce::Component&) override;   // armed menus: their underlined letters choose
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    };

    // Panel with FL's header: the title sits on a darker tab whose right end
    // slants down into a dark strip that runs to the panel's edge; the body is
    // outlined on its left, right and bottom.
    class Panel : public juce::Component
    {
    public:
        Panel (const juce::String& title = {}, int headerInset = 0);
        void setTitle (const juce::String& t) { title = t; repaint(); }
        void paint (juce::Graphics&) override;
        static constexpr int headerH = 20;

    private:
        juce::String title;
        int headerInset;
    };

    // Value box dragged up/down (FL "digi wheel"), e.g. RANGE, TRACK, Max poly.
    class DigiBox : public juce::Slider
    {
    public:
        explicit DigiBox (bool redStyle);
        void paint (juce::Graphics&) override;
        void valueChanged() override { repaint(); }
        void setFixedText (const juce::String& t) { fixedText = t; repaint(); }
        void mouseEnter (const juce::MouseEvent& e) override { hover.setTarget (true); juce::Slider::mouseEnter (e); }
        void mouseExit (const juce::MouseEvent& e) override { hover.setTarget (false); juce::Slider::mouseExit (e); }

    private:
        bool red;
        juce::String fixedText;
        HoverFade hover { *this };
    };

    // A drop-down field whose arrow turns down and brightens under the mouse (as FL's does).
    class ComboBox : public juce::ComboBox
    {
    public:
        void mouseEnter (const juce::MouseEvent& e) override { turn.setTarget (true); juce::ComboBox::mouseEnter (e); }
        void mouseExit (const juce::MouseEvent& e) override { turn.setTarget (false); juce::ComboBox::mouseExit (e); }
        void showPopup() override { juce::ComboBox::showPopup(); turn.refresh(); }

    private:
        ArrowTurn turn { *this, [this] { return isPopupActive(); } };
    };


    // FL's grey context menus (sample view, presets): shown with the plugin's
    // look, ticks for on/off entries and thin separators (measured from FL's
    // sample right-click menu).
    // onClose (optional) is called when the menu goes, chosen from or not.
    void showContextMenu (juce::PopupMenu&, juce::Component& target, bool atMouse, std::function<void()> onClose = {});
    // Makes the next menu shown answer to its underlined letters ("&Reload":
    // R), as FL's menus do. showContextMenu does it itself.
    void armMenuLetters (const juce::PopupMenu&);
    // The text FL shows beside an entry for a Ctrl shortcut ("Ctrl+C"; Cmd on macOS).
    juce::String commandShortcutText (char key);
    // The component property that marks a menu's target as such a context menu.
    inline constexpr const char* contextMenuProperty = "contextMenu";

    // FL's message dialog (measured from FL's dialogs): a title bar, the
    // message on a panel with a large grey sign, OK below. Shown over the
    // plugin window that holds owner; closes with OK, the cross, Return or Esc.
    void showDialog (juce::Component& owner, const juce::String& title, const juce::String& message, bool warning = false);
    // FL's question dialog with a column of choices; onChoice gets the chosen
    // index (not called when the dialog is closed).
    void showChoices (juce::Component& owner, const juce::String& title, const juce::String& message,
                      const juce::StringArray& choices, std::function<void (int)> onChoice);
    // A one-line text entry in the same style; onText is not called on cancel.
    void askText (juce::Component& owner, const juce::String& title, const juce::String& prompt, const juce::String& initial,
                  std::function<void (const juce::String&)> onText);

    // The same dialog as a properties sheet: label / value rows (an empty
    // label and value draws a rule between groups).
    using InfoRows = std::vector<std::pair<juce::String, juce::String>>;
    void showInfo (juce::Component& owner, const juce::String& title, const InfoRows& rows);

    enum class Icon { sampler, envelope, wrench, valve, sine, triangle, square, folder, locate, close,
                      arpOff, arpUp, arpDown, arpUpDown, arpSticky, arpRandom, link };
    // Draws an icon as FL draws it, at FL's size, centred in area.
    void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour);

    // The small arrow in a panel header that opens the panel's menu (FL's
    // envelope and echo presets). Under the mouse it turns to point down;
    // the "boxed" one (the envelope's) also gets FL's amber 20 x 20 px frame
    // and turns amber itself (measured).
    class MenuArrow : public juce::Button
    {
    public:
        explicit MenuArrow (bool boxedStyle = false) : juce::Button ({}), boxed (boxedStyle) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        void mouseEnter (const juce::MouseEvent& e) override { turn.setTarget (true); juce::Button::mouseEnter (e); }
        void mouseExit (const juce::MouseEvent& e) override { turn.setTarget (false); juce::Button::mouseExit (e); }
        // Shows the panel's menu under the arrow; the arrow stays turned (and
        // the envelope's frame lit) until the menu closes, as in FL.
        void showMenu (juce::PopupMenu&);

    private:
        bool boxed;
        bool menuOpen = false;
        ArrowTurn turn { *this, [this] { return menuOpen; } };
    };

    // FL's upright A / B switch (the effects view's reverb type): toggled
    // state B puts the knob at the bottom.
    class SlideSwitch : public juce::Button
    {
    public:
        SlideSwitch() : juce::Button ({}) { setClickingTogglesState (true); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    class IconButton : public juce::Button
    {
    public:
        explicit IconButton (Icon i) : juce::Button ({}), icon (i) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Icon icon;
    };

    // A fixed icon (the Playback panel's loop link).
    class IconView : public juce::Component
    {
    public:
        IconView (Icon i, juce::Colour c) : icon (i), colour (c) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { drawIcon (g, icon, getLocalBounds().toFloat(), colour); }

    private:
        Icon icon;
        juce::Colour colour;
    };

    // A row of selectable icons (sheet selector, effects view, LFO shape).
    class IconTabs : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        IconTabs (std::vector<Icon> icons, bool large);
        void setSelected (int index, bool notify = true);
        int getSelected() const noexcept { return selected; }
        std::function<void (int)> onChange;
        juce::Colour selectedColour = colour::tabIcon, unselectedColour = colour::icon;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        std::vector<Icon> icons;
        bool large;
        int selected = 0;
    };

    // A row of selectable words (envelope targets, tracking Vol/Key).
    class TextTabs : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        TextTabs (juce::StringArray items, float fontHeight, bool highlightStyle);
        void setSelected (int index, bool notify = true);
        int getSelected() const noexcept { return selected; }
        std::function<void (int)> onChange;
        std::vector<int> nudges;   // per item: moves it that many px (FL's spacing is not uniform)

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<int> tabBounds (int index) const;

        juce::StringArray items;
        float fontHeight;
        bool highlightStyle;
        int selected = 0;
    };
}
