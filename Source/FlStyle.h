#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Visual language modelled on FL Studio's channel settings window.
// Colours were sampled from the manual's screenshots; all drawing is our own.
namespace fl
{
    namespace colour
    {
        // Sampled from FL Studio 2026's channel settings window (default theme).
        const juce::Colour sheet      { 0xff1e2122 };   // sheet background (gaps between panels)
        const juce::Colour panel      { 0xff232627 };   // panel body
        const juce::Colour panelEdge  { 0xff2d3133 };   // panel outline
        const juce::Colour well       { 0xff1b1d1e };   // top bar, displays
        const juce::Colour knobTop    { 0xff2e3134 };
        const juce::Colour knobBottom { 0xff17191b };
        const juce::Colour knobRim    { 0xff101213 };
        const juce::Colour amber      { 0xffffa830 };   // knob value arc
        const juce::Colour green      { 0xff41db8b };   // volume arc, selected tab text
        const juce::Colour envLine    { 0xff41e18e };
        const juce::Colour tabIcon    { 0xff4df1b1 };
        const juce::Colour ledOn      { 0xffe6653e };
        const juce::Colour ledGlow    { 0xffffb08a };
        const juce::Colour ledOff     { 0xff2e3030 };
        const juce::Colour ledRing    { 0xff545656 };
        const juce::Colour text       { 0xffcfd1d2 };
        const juce::Colour title      { 0xffe2e3e4 };
        const juce::Colour dim        { 0xff8a8d8f };
        const juce::Colour wavePink   { 0xffd9c6c6 };   // waveform edges
        const juce::Colour waveCream  { 0xfff6e4e4 };   // waveform line
        const juce::Colour waveBg     { 0xff201617 };   // sample view
        const juce::Colour waveBgPre  { 0xff0c0809 };   // before the Start flag
        const juce::Colour waveGrid   { 0xff2b1e1f };
        const juce::Colour watermark  { 0xff473221 };
        const juce::Colour loopFill   { 0xff3a1a1c };
        const juce::Colour loopRed    { 0xffb52a2a };
        const juce::Colour startGold  { 0xffc9a286 };
        const juce::Colour playOrange { 0xffff6a1f };   // playback bar
        const juce::Colour keyWhite   { 0xffdee3ec };
        const juce::Colour keyBlack   { 0xff2b2c2d };
        const juce::Colour keyRoot    { 0xff4a9cff };
        const juce::Colour digiRed    { 0xff8c424a };
        const juce::Colour digiGrey   { 0xff5b6063 };
        const juce::Colour tickPink   { 0xffe0606a };
        const juce::Colour menuBg     { 0xff343432 };
        const juce::Colour menuHeader { 0xff51514d };
        const juce::Colour menuText   { 0xffe8a06a };   // FL's warm menu text
        const juce::Colour comboBg    { 0xff1c1e1f };
        const juce::Colour buttonBg   { 0xff1a1b1d };
    }

    juce::Font font (float height, bool bold = false);
    // Text used to match the macOS font's width to Segoe UI's.
    inline constexpr const char* fontReferenceText = "Remove DC offset Normalize Reverse polarity Fade stereo 0123456789 START OFFSET";

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
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
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
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    };

    // Rounded panel with FL's header tab: the title sits on a panel-coloured
    // tab, the rest of the header row shows the sheet colour behind a slant.
    class Panel : public juce::Component
    {
    public:
        Panel (const juce::String& title = {}, int headerInset = 0);
        void setTitle (const juce::String& t) { title = t; repaint(); }
        void paint (juce::Graphics&) override;

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

    private:
        bool red;
        juce::String fixedText;
    };

    // FL's grey context menus (sample view, presets): shown with the plugin's
    // look, ticks for on/off entries and thin separators (measured from FL's
    // sample right-click menu).
    void showContextMenu (juce::PopupMenu&, juce::Component& target, bool atMouse);

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

    enum class Icon { sampler, envelope, wrench, valve, sine, triangle, square, folder, locate, close };
    void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour);

    // The small arrow in a panel header that opens the panel's menu (FL's
    // envelope and echo presets).
    class MenuArrow : public juce::Button
    {
    public:
        MenuArrow() : juce::Button ({}) {}
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

    // A row of selectable icons (sheet selector, effects view, LFO shape).
    class IconTabs : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        IconTabs (std::vector<Icon> icons, bool large);
        void setSelected (int index, bool notify = true);
        int getSelected() const noexcept { return selected; }
        std::function<void (int)> onChange;
        juce::Colour selectedColour = colour::tabIcon;

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
