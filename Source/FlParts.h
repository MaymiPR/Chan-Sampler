#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "FlStyle.h"
#include <map>

// Live drawing of one INS target's envelope (DAHDSR with tension), using the
// same curve formula as the engine.
class EnvelopeDisplay : public juce::Component, private juce::Timer
{
public:
    EnvelopeDisplay (juce::AudioProcessorValueTreeState&, const char* prefix);
    void paint (juce::Graphics&) override;

    // As in FL, with the envelope on its points are bigger and can be dragged:
    // delay / attack / hold / release sideways, the decay point also up and
    // down (sustain).
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Geometry { juce::Rectangle<float> r; float unit, xd, xa, xh, xs, xe, sus; };
    Geometry geometry() const;
    std::array<juce::Point<float>, 5> points (const Geometry&) const;
    int pointAt (juce::Point<float>) const;
    void set (const char* suffix, float value);
    void timerCallback() override;
    float get (const char* suffix) const;

    int hovered = -1, dragging = -1;
    float dragStartValue = 0.0f, dragStartSus = 0.0f;
    juce::Point<float> dragStart;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String prefix;
    float lastHash = -1.0f;
};

// Live drawing of one INS target's LFO.
class LfoDisplay : public juce::Component, private juce::Timer
{
public:
    LfoDisplay (juce::AudioProcessorValueTreeState&, const char* prefix);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    float get (const char* suffix) const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String prefix;
    float lastHash = -1.0f;
};

// FL's right-click menu for a parameter control: Reset, Copy value, Paste
// value (xx%), Type in value... and optional value presets. "Copy value" puts
// the 0..1 position on the clipboard as plain text, exactly like FL, so values
// copied from FL's Sampler paste here (and back) to the same setting.
class ParamMenu
{
public:
    ParamMenu (juce::Component& owner, juce::RangedAudioParameter&);
    ~ParamMenu();

    // Extra menu entries: text and the parameter value they set.
    std::vector<std::pair<juce::String, float>> presets;
    // Called before / after any edit from the menu (e.g. turns an envelope on).
    std::function<void()> beforeEdit, afterEdit;
    // No "Type in value" entry (FL's TIME knob has none).
    bool noTypeIn = false;
    // Presets whose value is computed when the menu opens (a negative value
    // greys the entry out), e.g. TIME's "Project tempo".
    std::map<juce::String, std::function<float()>> computedPresets;
    // Presets that run an action instead of setting a value (TIME's Autodetect...).
    std::map<juce::String, std::function<void()>> actions;

    void show();

private:
    void setPosition (float normalised);
    void typeIn();

    juce::Component& owner;
    juce::RangedAudioParameter& param;
    std::unique_ptr<juce::TextEditor> editor;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

// A knob or slider with FL's right-click menu.
class ParamKnob : public juce::Slider
{
public:
    ParamKnob (juce::RangedAudioParameter&, SliderStyle);

    ParamMenu menu;

    // Called after the user changes the value (drag, wheel or the menu), not
    // when the host or automation moves it.
    std::function<void()> onUserChange;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    // FL's drag detents (measured in FL 2026): a two-sided control holds at its
    // centre, and PITCH at every semitone, for 20 px of drag when a drag
    // arrives there; a drag that starts on one leaves at once.
    static constexpr float detentHold = 20.0f;
    std::vector<double> detents;   // positions 0..1
    double dragPixels = 256.0;     // drag for the whole range (FL's, per control)
    double dragPos = 0.0;
    double snapSteps = 0.0;        // > 0: a drag sets whole steps of the range (TIME: 16ths)
    juce::Point<float> lastMouse;
    int holding = -1;              // index into detents while held
    float heldPx = 0.0f;           // drag so far while held (signed)

    bool wheelActive = false;
};

// FL's digital value box (pitch range, echoes, polyphony...) with the menu.
class ParamDigiBox : public fl::DigiBox
{
public:
    ParamDigiBox (juce::RangedAudioParameter&, bool redStyle);

    ParamMenu menu;
    void mouseDown (const juce::MouseEvent&) override;
};

// FL's root note keyboard (KeybPanel, measured from FL 2026): all 128 notes on
// 90 px octaves behind a scroll bar; left-click plays, right-click sets the
// root note (blue).
class RootKeyboard : public juce::Component, private juce::Timer
{
public:
    RootKeyboard (juce::AudioProcessorValueTreeState&, juce::MidiKeyboardState&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    // FL's layout (pixels): a 15 px label strip, a 1 px rule, 59 px keys on
    // 91 px octaves (white keys 13 px, black 8 x 40), a 15 px scroll bar.
    static constexpr float octaveW = 91.0f, whiteW = 13.0f, labelH = 15.0f, keysTop = 16.0f,
                           keysBottom = 75.0f, blackW = 8.0f, blackH = 40.0f, arrowW = 13.0f, thumbW = 57.0f;

    void timerCallback() override;
    int noteAt (juce::Point<float>) const;
    float noteX (int note) const;           // left edge in keyboard pixels (before scrolling)
    float maxScroll() const;
    void setScroll (float px);      // glides there
    void setScrollNow (float px);   // jumps there (dragging the bar)
    juce::Rectangle<float> thumbBounds() const;
    static bool isBlack (int note) { const int n = note % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }

    juce::AudioProcessorValueTreeState& apvts;
    juce::MidiKeyboardState& state;
    float scroll = 0.0f, target = 0.0f;
    bool animating = false;
    bool placed = false;
    float dragFrom = -1.0f, scrollFrom = 0.0f;   // dragging the scroll thumb
    int playing = -1;
    int lastRoot = -1;
    std::array<bool, 128> lastOn {};
};

// "Root note" panel shared by the INS and FUNC sheets.
class RootNotePanel : public fl::Panel, private juce::Timer
{
public:
    RootNotePanel (juce::AudioProcessorValueTreeState&, juce::MidiKeyboardState&);

private:
    void timerCallback() override;

    juce::AudioProcessorValueTreeState& apvts;
    juce::TextButton resetButton { "Reset" };
    juce::Label fineLabel { {}, "Fine tune" }, hint { {}, "Right-click a key to set the root note" };
    ParamKnob fine;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fineAttachment;
    RootKeyboard keyboard;
    int shownRoot = -1;
};
