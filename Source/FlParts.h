#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "FlStyle.h"
#include "History.h"
#include <map>

// Live drawing of one INS target's envelope (DAHDSR with tension), using the
// same curve formula as the engine.
class EnvelopeDisplay : public juce::Component
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
    float get (const char* suffix) const;

    int hovered = -1, dragging = -1;
    float dragStartValue = 0.0f, dragStartSus = 0.0f;
    juce::Point<float> dragStart;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String prefix;
    // Each parameter's change repaints the display at once (a knob drag, a
    // point drag, undo); a timer would show the curve a frame or more late.
    std::vector<std::unique_ptr<juce::ParameterAttachment>> watchers;
};

// Live drawing of one INS target's LFO.
class LfoDisplay : public juce::Component
{
public:
    LfoDisplay (juce::AudioProcessorValueTreeState&, const char* prefix);
    void paint (juce::Graphics&) override;

private:
    float get (const char* suffix) const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String prefix;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> watchers;
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

    // FL's gestures (measured in FL): a drag moves the value by its
    // pixels (a quarter as fast with Ctrl; Shift changes nothing), a wheel
    // notch is worth 8 px of drag (half with Ctrl), Alt+click resets to the
    // default, a double-click does nothing. On a slider the wheel runs the
    // other way, and a press on the track (off the ball) walks the ball
    // towards the mouse: a pixel at once, then 15 px a second while held.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override {}
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent& e) override { hover.setTarget (true); juce::Slider::mouseEnter (e); }
    void mouseExit (const juce::MouseEvent& e) override { hover.setTarget (false); juce::Slider::mouseExit (e); }
    void resized() override;
    double snapValue (double attemptedValue, juce::Slider::DragMode) override;

    // Locked, the knob keeps its look but ignores the mouse (FL's START and
    // END without a sample: measured, they do not grey out).
    void setLocked (bool l) { locked = l; }

    // A wheel notch moves the value at once but the knob or ball eases to it:
    // measured frame by frame in FL (MOD X, ATT, the fine tune slider), an
    // exponential with a time constant of about 36 ms (30 to 42 ms in four
    // runs). A drag moves it at once (measured on ATT).
    static constexpr double wheelGlideMs = 36.0;
    void valueChanged() override;

    static constexpr float fineDrag = 0.25f;       // Ctrl: of the plain speed
    static constexpr double wheelPixels = 8.0;     // a notch, in drag pixels
    static constexpr float walkStep = 1.0f;        // a track press: the first step (thumb pixels)
    static constexpr float walkPerSecond = 15.0f;  // then this fast

private:
    // the ball walking towards a held mouse on the track
    class Walker : private juce::Timer
    {
    public:
        explicit Walker (ParamKnob& k) : knob (k) {}
        void start (float toward) { target = toward; startTimerHz (60); }
        void stop() { stopTimer(); }
        void setTarget (float toward) { target = toward; }

    private:
        void timerCallback() override { knob.walkTowards (target, walkPerSecond / 60.0f); }
        ParamKnob& knob;
        float target = 0.0f;
    };
    // the eased position drawn after a wheel notch (the "shown" property)
    class Glide : private juce::Timer
    {
    public:
        explicit Glide (ParamKnob& k) : knob (k) {}
        bool isActive() const noexcept { return active; }
        double shown (double otherwise) const noexcept { return active ? position : otherwise; }
        void startFrom (double drawnNow)
        {
            position = drawnNow;
            last = juce::Time::getMillisecondCounterHiRes();
            active = true;
            startTimerHz (60);
            publish();
        }
        void cancel()
        {
            if (! active)
                return;
            active = false;
            stopTimer();
            knob.getProperties().remove ("shown");
            knob.repaint();
        }

    private:
        void timerCallback() override
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            const auto dt = juce::jlimit (0.0, 100.0, now - last);
            last = now;
            const auto target = knob.valueToProportionOfLength (knob.getValue());
            position += (target - position) * (1.0 - std::exp (-dt / wheelGlideMs));
            if (std::abs (target - position) < 2.0e-4)
                cancel();
            else
                publish();
        }
        void publish()
        {
            knob.getProperties().set ("shown", position);
            knob.repaint();
        }
        ParamKnob& knob;
        double position = 0.0, last = 0.0;
        bool active = false;
    };
    Glide glide { *this };
    float thumbX (double proportion) const;   // the ball's centre for a value (slider pixels)
    void walkTowards (float x, float maxStep);
    Walker walker { *this };
    bool walking = false;

    fl::HoverFade hover { *this };
    bool locked = false;
    // FL's drag detents (measured in FL): a two-sided control holds at its
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

// FL's root note keyboard (KeybPanel, measured from FL): all 128 notes on
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
    RootNotePanel (juce::AudioProcessorValueTreeState&, juce::MidiKeyboardState&, History&);

private:
    void timerCallback() override;

    juce::AudioProcessorValueTreeState& apvts;
    History& history;
    juce::TextButton resetButton { "Reset" };
    juce::Label fineLabel { {}, "Fine tune" }, hint { {}, "Right-click a key to set the root note" };
    ParamKnob fine;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> fineAttachment;
    RootKeyboard keyboard;
    int shownRoot = -1;
};
