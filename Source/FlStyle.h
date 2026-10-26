#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Visual language modelled on FL Studio's channel settings window. Every
// colour and size below was read pixel by pixel from FL 2026's screen (default
// theme, 100%; see docs/EVIDENCE.md "Skin"); all drawing is our own.
namespace fl
{
    // The design tokens: FL's palette. Nothing else in the UI holds a colour.
    namespace colour
    {
        // Surfaces
        const juce::Colour sheet       { 0xff202324 };   // sheet background (gaps between panels)
        const juce::Colour panel       { 0xff222525 };   // panel body
        const juce::Colour panelTab    { 0xff1f2222 };   // the header tab holding a panel's title
        const juce::Colour panelEdge   { 0xff1a1d1d };   // panel outline (left, right, bottom) and the header strip right of the tab
        const juce::Colour well        { 0xff1b1d1e };   // top bar, displays
        const juce::Colour barLine     { 0xff151718 };   // the dark rules above and below the top bar
        const juce::Colour tabSelected { 0xff202324 };   // the selected sheet tab's cell in the top bar

        // Knobs (FlStyle.cpp renders them per pixel)
        const juce::Colour knobOuter       { 0xff191c1f };   // outermost 1 px ring
        const juce::Colour knobBevelTop    { 0xff25282b };   // 2 px bevel ring, lit from above
        const juce::Colour knobBevelBottom { 0xff1b1d1f };
        const juce::Colour knobGapTop      { 0xff1d2023 };   // 1 px dark gap between bevel and body
        const juce::Colour knobGapBottom   { 0xff131517 };
        const juce::Colour amber           { 0xffffa830 };   // value arcs, the Mid slider's fill, the lit "Key" tab
        const juce::Colour green           { 0xff47f49a };   // INS arcs, VOL, the echo's FEED
        // FL colours each knob's arc (and its dot, at 73% of the arc colour) by what it controls
        const juce::Colour knobTeal        { 0xff5ceeff };   // PAN
        const juce::Colour knobYellow      { 0xffdbf459 };   // MOD X
        const juce::Colour knobBlue        { 0xff3b6be2 };   // the tracking and echo MOD Y
        const juce::Colour knobPink        { 0xffa05265 };   // the echo's PITCH
        const juce::Colour bigRingLight    { 0xff5a5b5b };   // the filter knobs' wide bevel, lit from above
        const juce::Colour bigRingDark     { 0xff0a0b0b };
        const juce::Colour bigDot          { 0xff7dff9e };   // the filter knobs' dots: green on MOD X, gold on MOD Y
        const juce::Colour bigDotGold      { 0xffffd040 };

        // Round switches (LEDs)
        const juce::Colour ledRing        { 0xff5c5f5f };
        const juce::Colour ledGap         { 0xff0f1010 };
        const juce::Colour ledInnerTop    { 0xff2d3031 };
        const juce::Colour ledInnerBottom { 0xff454849 };
        const juce::Colour ledOnCentre    { 0xffec704a };
        const juce::Colour ledOnEdge      { 0xfff8aa88 };
        const juce::Colour ledOnBottom    { 0xfffcecc8 };
        const juce::Colour ledOnTop       { 0xffffffea };
        const juce::Colour ledOnGap       { 0xff85250c };
        const juce::Colour ledOnRing      { 0xff5c5e5e };
        const juce::Colour ledGreenCentre { 0xff8fe86a };   // the green variant (FL's channel ON light)
        const juce::Colour ledGreenEdge   { 0xffe4ffd8 };
        const juce::Colour ledSmallTop    { 0xff4c4e4f };   // the 11 px switch's disc (CLIP): lit top, dark foot
        const juce::Colour ledSmallMid    { 0xff313435 };
        const juce::Colour ledSmallBottom { 0xff191b1c };
        const juce::Colour ledSmallOnBottom { 0xfff5b795 };   // lit: the foot of its disc

        // Text
        const juce::Colour title     { 0xffd4d4d4 };   // panel titles
        const juce::Colour text      { 0xffd0d0d0 };   // labels, switch captions
        const juce::Colour caption   { 0xffc4c4c4 };   // the small bold knob captions
        const juce::Colour dim       { 0xff8a8d8f };   // secondary text (sample info, hints)
        const juce::Colour fileName  { 0xffbcbcbc };   // the loaded file's name
        const juce::Colour textGreen { 0xff40f59a };   // the selected envelope target's name
        const juce::Colour tabIcon   { 0xff4df1b1 };   // the selected sheet tab's icon, the product name
        const juce::Colour icon      { 0xffc0c6c9 };   // unselected icons, the menu arrow
        const juce::Colour fxIconOn  { 0xffffc742 };   // the effects view's chosen icon (wrench / valve)
        const juce::Colour fxIconOff { 0xffd4dbdf };   // its other icon
        const juce::Colour envLine   { 0xff3bc97e };   // envelope / LFO curves
        const juce::Colour envPoint  { 0xff40d989 };   // the envelope's draggable points (ring)
        const juce::Colour envPointFill { 0xff224534 };

        // Drop-down fields
        const juce::Colour comboTop       { 0xff202122 };   // fill, darkening downwards
        const juce::Colour comboBottom    { 0xff191a1b };
        const juce::Colour comboEdge      { 0xff1b1e1e };   // 1 px frame
        const juce::Colour comboInnerTop  { 0xff313233 };   // lighter inner edge along the top
        const juce::Colour comboInnerSide { 0xff2c2d2e };
        const juce::Colour comboInnerBottom { 0xff262728 };
        const juce::Colour comboArrow     { 0xff686c6e };
        const juce::Colour comboText      { 0xffd0d0d0 };

        // Push buttons (Reset, Cut self), measured
        const juce::Colour buttonFrame       { 0xff141717 };
        const juce::Colour buttonCornerOuter { 0xff1e2121 };   // the rounded corners' two shades
        const juce::Colour buttonCornerInner { 0xff161919 };
        const juce::Colour buttonEdgeTop     { 0xff262729 };   // light inner edge along the top
        const juce::Colour buttonEdgeBottom  { 0xff1b1c1e };   // inner row at the bottom
        const juce::Colour buttonSideTop     { 0xff252628 };   // inner edges down the sides, fading
        const juce::Colour buttonSideBottom  { 0xff1b1c1e };
        const juce::Colour buttonTop         { 0xff202123 };   // fill, darkening downwards
        const juce::Colour buttonBottom      { 0xff18191b };
        const juce::Colour buttonText        { 0xffc4c6c8 };
        // The A / B reverb switch: an upright pill
        const juce::Colour pillEdge     { 0xff3c464b };
        const juce::Colour pillInside   { 0xff1d1f1f };
        const juce::Colour pillKnob     { 0xffa6abaf };
        const juce::Colour pillKnobEdge { 0xffb9bec2 };

        // Digital value boxes
        const juce::Colour digiTop    { 0xff5e6366 };
        const juce::Colour digiBottom { 0xff50555a };
        const juce::Colour digiEdge   { 0xff353a3d };   // bottom edge and corners
        const juce::Colour digiArrow  { 0xff303538 };
        const juce::Colour digiText   { 0xffc5cace };
        const juce::Colour digiNone   { 0xff8f9598 };   // the dashes of an empty box
        const juce::Colour digiRed    { 0xff8c424a };   // FL's red variant (pitch range)

        // Sliders
        const juce::Colour sliderTrack   { 0xff171a1a };   // the groove's edges
        const juce::Colour sliderGroove  { 0xff1d2020 };   // its floor
        const juce::Colour sliderTick    { 0xff171a1a };   // the 1 px ticks below it
        const juce::Colour sliderTickTop { 0xff1c1f1f };   // their top pixel
        const juce::Colour sliderBand    { 0xff202323 };   // the strip the ticks stand on
        const juce::Colour sliderBandTop { 0xff202424 };
        const juce::Colour thumbRed      { 0xffff4e60 };   // the fine tune thumb's line (the ball's shading is in FlStyle.cpp)

        // Sample view
        const juce::Colour fieldEdge  { 0xff3a3d3f };   // the preset bar's outline
        const juce::Colour startGold  { 0xffc9a286 };   // Start flag
        const juce::Colour playOrange { 0xffff6a1f };   // playback bar (overview)

        // Menus and dialogs (measured earlier, see EVIDENCE.md)
        const juce::Colour menuBg     { 0xff343432 };
        const juce::Colour menuText   { 0xffe8a06a };
        const juce::Colour ledOn      { 0xffe6653e };   // the file name when the sample is missing
    }

    // The sample view (pixels sampled from FL 2026's screen).
    namespace view
    {
        const juce::Colour frame      { 0xff202324 };   // strip above the view, around the bar
        const juce::Colour bg         { 0xff1b1d1e };
        const juce::Colour grid       { 0xff131516 };   // 0.1 s (or 10x finer / coarser) and the +/-50% lines
        const juce::Colour gridMajor  { 0xff090b0c };   // the next power of ten (whole seconds)
        const juce::Colour gridMajorEdge { 0xff17191a };   // 1 px each side of it
        const juce::Colour viewEdge   { 0xff161819 };   // the column at the view's right
        const juce::Colour waveMono   { 0xfff6feff };
        const juce::Colour waveStereo { 0xfff2eded };
        const juce::Colour watermark  { 0xff41464b };
        const juce::Colour barDark    { 0xff101213 };
        const juce::Colour barLight   { 0xff313334 };
        const juce::Colour track      { 0xff282a2b };
        const juce::Colour overview   { 0xffa3a5a6 };
        const juce::Colour arrow      { 0xff6a6d6f };
    }

    // The root note keyboard (pixels sampled from FL 2026's screen).
    namespace key
    {
        const juce::Colour strip       { 0xffbabec5 };   // label strip; also the flat C keys
        const juce::Colour rule        { 0xffa3a7ae };
        const juce::Colour whiteTop    { 0xffbabec5 };   // white keys shade lighter downwards
        const juce::Colour whiteBottom { 0xffcfd3da };
        const juce::Colour root        { 0xff77a9e6 };
        const juce::Colour label       { 0xff272224 };
        const juce::Colour rootLabel   { 0xff1f4f9e };
        const juce::Colour pressed     { 0xffeca449 };   // a played key
        const juce::Colour blackEdge   { 0xff2f3031 };
        const juce::Colour blackTop    { 0xff3a3b3c };   // the body shades lighter downwards
        const juce::Colour blackBottom { 0xff575859 };   // also the lip
        const juce::Colour blackFade   { 0xff4e4f50 };
        const juce::Colour blackLitTop { 0xff4c4d4e };   // the lit left column
        const juce::Colour blackLitBottom { 0xff6a6b6c };
        const juce::Colour blackShadow { 0xff979aa0 };   // the row under a black key
        const juce::Colour bar         { 0xff3f4042 };
        const juce::Colour barArrow    { 0xff242527 };
        const juce::Colour grip        { 0xff252628 };
        const juce::Colour gripLine    { 0xff202123 };
    }

    // FL's text: Segoe UI at the given em size in pixels (FL's labels are 11.7 px,
    // its knob captions 7 px bold).
    juce::Font font (float em, bool bold = false);
    // Text used to match the macOS font's width to Segoe UI's.
    inline constexpr const char* fontReferenceText = "Remove DC offset Normalize Reverse polarity Fade stereo 0123456789 START OFFSET";

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
    // it (measured frame by frame in FL 2026): in, an exponential that is half
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
    // FL 2026 on Use loop points and CLIP). The drawing reads the amount.
    inline constexpr float litFadeIn = 1.0f / 11.0f, litFadeOut = 1.0f / 14.0f;   // per 60 Hz frame
    class Switch : public juce::ToggleButton, private juce::Timer
    {
    public:
        explicit Switch (const juce::String& text = {}) : juce::ToggleButton (text) { setTriggeredOnMouseDown (true); }
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
    // right to pointing down under the mouse: measured frame by frame in FL
    // 2026 (the Filter list, the envelope and echo menus), the triangle rotates
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
    inline constexpr const char* contextMenuProperty = "flContextMenu";

    // FL's message dialog (measured from FL 2026's dialogs): a title bar, the
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
