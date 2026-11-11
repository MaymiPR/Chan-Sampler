#include "ui/Controls.h"
#include "dsp/Dsp.h"
#include "Params.h"

#include <iomanip>
#include <map>
#include <sstream>

namespace cs
{

namespace
{
    void fillWell (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // FL: a plain dark display without grid lines.
        g.setColour (chan::ui::colour::well);
        g.fillRect (r);
    }
}

//==============================================================================
EnvelopeDisplay::EnvelopeDisplay (juce::AudioProcessorValueTreeState& s, const char* p)
    : apvts (s), prefix (p)
{
    for (auto* suffix : { "EnvOn", "Del", "Att", "Hold", "Dec", "Sus", "Rel", "AttT", "DecT", "RelT" })
        if (auto* param = apvts.getParameter (prefix + suffix))
            watchers.push_back (std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); }));
}

float EnvelopeDisplay::get (const char* suffix) const
{
    return apvts.getRawParameterValue (prefix + suffix)->load();
}

EnvelopeDisplay::Geometry EnvelopeDisplay::geometry() const
{
    // FL's envelope view (measured from FL screenshots): each stage is as wide
    // as its knob position times a third of the view; decay is scaled by
    // (1 - sustain), there is no sustain plateau (release starts where decay
    // ends, from the sustain level, at full width), and delay is empty space.
    Geometry gm;
    gm.r = getLocalBounds().toFloat().reduced (5.0f, 6.0f);
    gm.unit = gm.r.getWidth() / 3.0f;
    gm.sus = get ("Sus");
    gm.xd = gm.r.getX() + get ("Del") * gm.unit;
    gm.xa = gm.xd + get ("Att") * gm.unit;
    gm.xh = gm.xa + get ("Hold") * gm.unit;
    gm.xs = gm.xh + get ("Dec") * (1.0f - gm.sus) * gm.unit;
    gm.xe = gm.xs + get ("Rel") * gm.unit;
    return gm;
}

std::array<juce::Point<float>, 5> EnvelopeDisplay::points (const Geometry& gm) const
{
    auto yOf = [&gm] (float v) { return gm.r.getBottom() - v * gm.r.getHeight(); };
    return { juce::Point<float> (gm.xd, yOf (0.0f)), juce::Point<float> (gm.xa, yOf (1.0f)), juce::Point<float> (gm.xh, yOf (1.0f)),
             juce::Point<float> (gm.xs, yOf (gm.sus)), juce::Point<float> (gm.xe, yOf (0.0f)) };
}

int EnvelopeDisplay::pointAt (juce::Point<float> pos) const
{
    if (get ("EnvOn") < 0.5f)
        return -1;
    const auto pts = points (geometry());
    // Later points win when they overlap (e.g. zero-length stages).
    for (int i = 4; i >= 0; --i)
        if (pts[(size_t) i].getDistanceFrom (pos) <= 7.0f)
            return i;
    return -1;
}

void EnvelopeDisplay::set (const char* suffix, float value)
{
    if (auto* p = apvts.getParameter (prefix + suffix))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 1.0f, value)));
}

void EnvelopeDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto h = pointAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EnvelopeDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0 && dragging < 0)
    {
        hovered = -1;
        repaint();
    }
}

void EnvelopeDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = pointAt (e.position);
    if (dragging < 0)
        return;
    static const char* ids[] = { "Del", "Att", "Hold", "Dec", "Rel" };
    dragStartValue = get (ids[dragging]);
    dragStartSus = get ("Sus");
    dragStart = e.position;
    for (auto* id : { ids[dragging], "Sus" })
        if (auto* p = apvts.getParameter (prefix + id))
            p->beginChangeGesture();
}

void EnvelopeDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    static const char* ids[] = { "Del", "Att", "Hold", "Dec", "Rel" };
    const auto gm = geometry();
    const auto dx = (e.position.x - dragStart.x) / gm.unit;
    if (dragging == 3)
    {
        // Decay point: sideways is decay (its width is scaled by 1 - sustain),
        // up and down is sustain.
        const auto sus = juce::jlimit (0.0f, 1.0f, dragStartSus - (e.position.y - dragStart.y) / gm.r.getHeight());
        set ("Sus", sus);
        set ("Dec", dragStartValue + dx / juce::jmax (0.05f, 1.0f - sus));
    }
    else
    {
        set (ids[dragging], dragStartValue + dx);
    }
}

void EnvelopeDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    static const char* ids[] = { "Del", "Att", "Hold", "Dec", "Rel" };
    for (auto* id : { ids[dragging], "Sus" })
        if (auto* p = apvts.getParameter (prefix + id))
            p->endChangeGesture();
    dragging = -1;
}

void EnvelopeDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    fillWell (g, bounds);
    const auto gm = geometry();
    const auto& r = gm.r;
    const auto sus = gm.sus;

    // Same model as the engine: rate-based decay / release, FL tension curves.
    const auto attK = dsp::flTensionToK (get ("AttT")), decK = dsp::flTensionToK (get ("DecT")), relK = dsp::flTensionToK (get ("RelT"));
    const bool on = get ("EnvOn") > 0.5f;
    auto yOf = [&r] (float v) { return r.getBottom() - v * r.getHeight(); };

    juce::Path p;
    p.startNewSubPath (gm.xd, yOf (0.0f));
    for (int i = 1; i <= 32; ++i)
    {
        const auto t = (float) i / 32.0f;
        p.lineTo (gm.xd + t * (gm.xa - gm.xd), yOf (dsp::tensionCurve (t, attK)));
    }
    p.lineTo (gm.xh, yOf (1.0f));
    for (int i = 1; i <= 32; ++i)
    {
        const auto t = (float) i / 32.0f;
        p.lineTo (gm.xh + t * (gm.xs - gm.xh), yOf (sus + (1.0f - sus) * dsp::tensionCurve (1.0f - t, decK)));
    }
    for (int i = 1; i <= 32; ++i)
    {
        const auto t = (float) i / 32.0f;
        p.lineTo (gm.xs + t * (gm.xe - gm.xs), yOf (sus * dsp::tensionCurve (1.0f - t, relK)));
    }

    // FL draws the curve at full brightness whether or not the envelope is on;
    // when it is on the points grow to show they can be dragged.
    g.setColour (chan::ui::colour::envLine);
    g.strokePath (p, juce::PathStrokeType (1.5f));

    // FL's points: 8 px rings with a dark green fill
    const auto pts = points (gm);
    for (int i = 0; i < 5; ++i)
    {
        const bool hot = on && (i == hovered || i == dragging);
        const auto size = on ? 9.0f : 8.0f;
        const auto dot = juce::Rectangle<float> (size, size).withCentre (pts[(size_t) i]);
        g.setColour (hot ? chan::ui::colour::envPoint : chan::ui::colour::envPointFill);
        g.fillEllipse (dot);
        g.setColour (chan::ui::colour::envPoint);
        g.drawEllipse (dot, 1.2f);
    }
}

//==============================================================================
LfoDisplay::LfoDisplay (juce::AudioProcessorValueTreeState& s, const char* p)
    : apvts (s), prefix (p)
{
    setInterceptsMouseClicks (false, false);
    for (auto* suffix : { "LfoShape", "LfoSpeed", "LfoAmt", "LfoDel", "LfoAtt" })
        if (auto* param = apvts.getParameter (prefix + suffix))
            watchers.push_back (std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); }));
}

float LfoDisplay::get (const char* suffix) const
{
    return apvts.getRawParameterValue (prefix + suffix)->load();
}

void LfoDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    fillWell (g, bounds);
    const auto r = bounds.reduced (4.0f, 6.0f);

    // FL draws the LFO in knob units, not time (measured from FL screenshots):
    // on a 130 px wide view one period is 8 + 64 x SPEED, the predelay 64 x
    // DELAY and the linear fade-in 64 x ATT (knob positions 0..1); the
    // height follows AMT (negative starts downwards).
    const auto shape = (int) get ("LfoShape");
    const auto amount = get ("LfoAmt");
    const auto periodU = 8.0f + 64.0f * get ("LfoSpeed");
    const auto delayU = 64.0f * get ("LfoDel");
    const auto attackU = 64.0f * get ("LfoAtt");
    constexpr float viewU = 130.0f;

    juce::Path p;
    const int steps = juce::jmax (2, (int) r.getWidth() * 2);
    for (int i = 0; i <= steps; ++i)
    {
        const auto u = viewU * (float) i / (float) steps;
        float v = 0.0f;
        if (u >= delayU)
        {
            const auto t = u - delayU;
            const auto phase = std::fmod (t / periodU, 1.0f);
            const auto fade = attackU > 0.0f ? juce::jmin (1.0f, t / attackU) : 1.0f;
            float wave = 0.0f;
            switch (shape)
            {
                case 0:  wave = std::sin (phase * juce::MathConstants<float>::twoPi); break;
                case 1:  wave = phase < 0.25f ? 4.0f * phase : (phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f); break;
                default: wave = phase < 0.5f ? 1.0f : -1.0f; break;
            }
            v = amount * fade * wave;
        }
        const juce::Point<float> pt (r.getX() + r.getWidth() * (float) i / (float) steps, r.getCentreY() - v * r.getHeight() * 0.5f);
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    g.setColour (chan::ui::colour::envLine);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

//==============================================================================
RootKeyboard::RootKeyboard (juce::AudioProcessorValueTreeState& s, juce::MidiKeyboardState& k)
    : apvts (s), state (k)
{
    startTimerHz (30);
}

float RootKeyboard::noteX (int note) const
{
    // White keys every 13 px; black keys at FL's own offsets in the octave.
    static const float whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    static const float blackX[12]     = { 0, 8, 0, 23, 0, 0, 47, 0, 61, 0, 75, 0 };
    const float octave = (float) (note / 12) * octaveW;
    const int n = note % 12;
    return octave + (isBlack (note) ? blackX[n] : whiteIndex[n] * whiteW);
}

float RootKeyboard::maxScroll() const
{
    return juce::jmax (0.0f, noteX (127) + whiteW - (float) getWidth());
}

void RootKeyboard::setScroll (float px)
{
    // The keyboard glides to its new place, as FL's smooth scrolling does
    // (measured: an exponential ease with a 42 ms time constant).
    target = juce::jlimit (0.0f, maxScroll(), px);
    if (std::abs (target - scroll) > 0.5f && ! animating)
    {
        animating = true;
        startTimerHz (60);
    }
}

void RootKeyboard::setScrollNow (float px)
{
    target = juce::jlimit (0.0f, maxScroll(), px);
    if (target != scroll)
    {
        scroll = target;
        repaint();
    }
}

void RootKeyboard::resized()
{
    // FL opens with the root note at the same place: C5 at 267 px (C2 just
    // off the left edge).
    if (! placed && getWidth() > 0)
    {
        placed = true;
        const int root = (int) apvts.getRawParameterValue (id::rootNote)->load();
        scroll = -1.0f;
        setScrollNow (noteX (root - root % 12) - 267.0f);
    }
}

juce::Rectangle<float> RootKeyboard::thumbBounds() const
{
    const auto track = (float) getWidth() - 2.0f * arrowW - thumbW;
    const auto f = maxScroll() > 0.0f ? scroll / maxScroll() : 0.0f;
    return { arrowW + f * track, keysBottom + 6.0f, thumbW, 3.0f };
}

int RootKeyboard::noteAt (juce::Point<float> p) const
{
    if (p.y >= keysBottom)
        return -1;
    const auto x = p.x + scroll;
    if (p.y >= labelH && p.y < labelH + blackH)
        for (int n = 0; n < 128; ++n)
            if (isBlack (n) && x >= noteX (n) && x < noteX (n) + blackW)
                return n;
    for (int n = 0; n < 128; ++n)
        if (! isBlack (n) && x >= noteX (n) && x < noteX (n) + whiteW)
            return n;
    return -1;
}

void RootKeyboard::paint (juce::Graphics& g)
{
    const int root = (int) apvts.getRawParameterValue (id::rootNote)->load();
    const auto w = (float) getWidth();
    // The keyboard, laid out as FL's (sizes and colours measured): a flat label strip, a rule, white
    // keys shading lighter downwards (no borders between them; C keys and the
    // root are flat), black keys with a dark outline, a lit left edge, a
    // front face and a lip, and a shadow row under them.
    using namespace chan::ui::key;
    const juce::Colour rootBlue = chan::ui::key::root, labelText = chan::ui::key::label, rootText = chan::ui::key::rootLabel;

    g.setColour (strip);
    g.fillRect (0.0f, 0.0f, w, labelH);
    g.setColour (rule);
    g.fillRect (0.0f, labelH, w, 1.0f);
    g.setGradientFill (juce::ColourGradient (whiteTop, 0.0f, keysTop, whiteBottom, 0.0f, keysBottom, false));
    g.fillRect (0.0f, keysTop, w, keysBottom - keysTop);

    // FL (measured): a played key turns orange
    auto pressedKey = [this] (int n, juce::Colour c) { return state.isNoteOn (1, n) ? chan::ui::key::pressed : c; };
    for (int n = 0; n < 128; ++n)
    {
        if (isBlack (n))
            continue;
        const auto x = noteX (n) - scroll;
        if (x + whiteW < 0.0f || x > w)
            continue;
        if (n == root)
        {
            g.setColour (pressedKey (n, rootBlue));
            g.fillRect (x, 0.0f, whiteW, keysBottom);
        }
        else if (n % 12 == 0 || state.isNoteOn (1, n))
        {
            g.setColour (pressedKey (n, whiteTop));
            g.fillRect (x, keysTop, whiteW, keysBottom - keysTop);
        }
        if (n % 12 == 0 || n == root)
        {
            g.setColour (n == root ? rootText : labelText);
            g.setFont (chan::ui::font (10.5f));
            g.drawText (flNoteName (n), juce::Rectangle<float> (x + 1.0f, 0.0f, octaveW, labelH), juce::Justification::centredLeft, false);
        }
    }

    // Black keys (8 x 40 from the rule down): outline; a 5 px body lightening
    // over 27 rows, two fading rows, a 6 row front face, the lip, 3 dark rows.
    for (int n = 0; n < 128; ++n)
    {
        if (! isBlack (n))
            continue;
        const auto x = noteX (n) - scroll;
        if (x + blackW < 0.0f || x > w)
            continue;
        const auto top = labelH;
        const auto body = n == root ? rootBlue : blackTop, bodyEnd = n == root ? rootBlue : blackBottom;
        g.setColour (blackEdge);
        g.fillRect (x, top, blackW, blackH);
        g.setGradientFill (juce::ColourGradient (pressedKey (n, body), 0.0f, top + 1.0f, pressedKey (n, bodyEnd), 0.0f, top + 28.0f, false));
        g.fillRect (x + 1.0f, top + 1.0f, 5.0f, 27.0f);
        g.setColour (pressedKey (n, n == root ? rootBlue : blackFade));
        g.fillRect (x + 1.0f, top + 28.0f, 5.0f, 1.0f);
        g.setColour (pressedKey (n, n == root ? rootBlue : blackTop));
        g.fillRect (x + 1.0f, top + 29.0f, 5.0f, 1.0f);
        g.setGradientFill (juce::ColourGradient (blackLitTop, 0.0f, top + 11.0f, blackLitBottom, 0.0f, top + 37.0f, false));
        g.fillRect (x + 1.0f, top + 11.0f, 1.0f, 26.0f);
        g.setColour (blackBottom);
        g.fillRect (x + 1.0f, top + 36.0f, 5.0f, 1.0f);
        g.setColour (blackShadow);
        g.fillRect (x, top + blackH, blackW, 1.0f);
    }

    // Scroll bar: a flat strip with 2 px chevrons at the ends and FL's thin grip.
    const auto barH = (float) getHeight() - keysBottom;
    g.setColour (bar);
    g.fillRect (0.0f, keysBottom, w, barH);
    g.setColour (barArrow);
    const auto cy = keysBottom + 7.5f;
    juce::Path arrows;
    arrows.startNewSubPath (10.5f, cy - 3.5f);  arrows.lineTo (7.0f, cy);  arrows.lineTo (10.5f, cy + 3.5f);
    arrows.startNewSubPath (w - 10.5f, cy - 3.5f);  arrows.lineTo (w - 7.0f, cy);  arrows.lineTo (w - 10.5f, cy + 3.5f);
    g.strokePath (arrows, juce::PathStrokeType (1.6f));
    const auto t = thumbBounds();
    g.setColour (grip);
    g.fillRoundedRectangle (t, 1.5f);
    g.setColour (gripLine);
    g.fillRect (t.withSizeKeepingCentre (t.getWidth() - 2.0f, 1.0f));
}

void RootKeyboard::mouseDown (const juce::MouseEvent& e)
{
    if (e.position.y >= keysBottom)
    {
        // Scroll bar: arrows step an octave, the thumb drags, the track pages.
        const auto t = thumbBounds().withY (keysBottom).withHeight ((float) getHeight() - keysBottom);
        if (e.position.x < arrowW)                     setScroll (scroll - octaveW);
        else if (e.position.x > (float) getWidth() - arrowW)   setScroll (scroll + octaveW);
        else if (t.contains (e.position))              { dragFrom = e.position.x; scrollFrom = target; }
        else                                           setScroll (scroll + (e.position.x < t.getX() ? -1.0f : 1.0f) * (float) getWidth() * 0.8f);
        return;
    }

    const int n = noteAt (e.position);
    if (n < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        // Right-click sets the root key (FL behaviour)
        setParameterAsGesture (*apvts.getParameter (id::rootNote), (float) n);
        return;
    }

    playing = n;
    state.noteOn (1, n, 100.0f / 127.0f);
}

void RootKeyboard::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom >= 0.0f)
    {
        const auto track = (float) getWidth() - 2.0f * arrowW - thumbW;
        if (track > 0.0f)
            setScrollNow (scrollFrom + (e.position.x - dragFrom) / track * maxScroll());
        return;
    }
    if (playing < 0)
        return;
    const int n = noteAt (e.position);
    if (n >= 0 && n != playing)
    {
        state.noteOff (1, playing, 0.0f);
        playing = n;
        state.noteOn (1, n, 100.0f / 127.0f);
    }
}

void RootKeyboard::mouseUp (const juce::MouseEvent&)
{
    dragFrom = -1.0f;
    if (playing >= 0)
        state.noteOff (1, playing, 0.0f);
    playing = -1;
}

void RootKeyboard::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto d = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    setScroll (target - d * 47.0f);   // FL (measured): 22 px per wheel notch (JUCE gives 0.47 a notch)
}

void RootKeyboard::timerCallback()
{
    if (animating)
    {
        scroll += (target - scroll) * 0.33f;   // FL (measured): the glide closes with a 42 ms time constant
        if (std::abs (target - scroll) < 0.5f)
        {
            scroll = target;
            animating = false;
            startTimerHz (30);
        }
        repaint();
    }
    const int root = (int) apvts.getRawParameterValue (id::rootNote)->load();
    bool changed = root != lastRoot;
    for (int n = 0; n < 128; ++n)
    {
        const bool on = state.isNoteOn (1, n);
        changed = changed || on != lastOn[(size_t) n];
        lastOn[(size_t) n] = on;
    }
    lastRoot = root;
    if (changed)
        repaint();
}

//==============================================================================
RootNotePanel::RootNotePanel (juce::AudioProcessorValueTreeState& s, juce::MidiKeyboardState& k, History& h)
    : chan::ui::Panel ("Root note: C5"), apvts (s), history (h),
      fine (*s.getParameter (id::fineTune), juce::Slider::LinearHorizontal), keyboard (s, k)
{
    resetButton.setTooltip ("Default root note and fine tune");
    resetButton.onClick = [this]
    {
        const History::Transaction edit (history, "Reset root note");
        setParameterAsGesture (*apvts.getParameter (id::rootNote), 60.0f);
        setParameterAsGesture (*apvts.getParameter (id::fineTune), 0.0f);
    };

    fineLabel.setFont (chan::ui::font (11.5f));
    fineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id::fineTune, fine);
    fine.setDoubleClickReturnValue (false, 0.0);   // the default, for the arc; a double-click does nothing (as in FL)
    fine.setTooltip ("Fine tuning");

    hint.setFont (chan::ui::font (10.5f));
    hint.setJustificationType (juce::Justification::centredLeft);
    hint.setColour (juce::Label::textColourId, chan::ui::colour::dim);

    for (auto* c : std::initializer_list<juce::Component*> { &resetButton, &fineLabel, &fine, &hint, &keyboard })
        addAndMakeVisible (c);

    // Layout from FL's KeybPanel (544 x 137). FL puts "Fine tune" and its
    // 104 px track at the right; here they follow the Reset button and the
    // hint comes after them (the layout asked for).
    resetButton.setBounds (4, 23, 46, 21);
    fineLabel.setBounds (58, 23, 52, 21);
    fine.setBounds (108, 24, 113, 22);
    hint.setBounds (230, 23, 300, 21);
    keyboard.setBounds (0, 48, 544, 89);

    startTimerHz (10);
}

void RootNotePanel::timerCallback()
{
    const int root = (int) apvts.getRawParameterValue (id::rootNote)->load();
    if (root != shownRoot)
    {
        shownRoot = root;
        setTitle ("Root note: " + flNoteName (root));
    }
}

//==============================================================================
namespace
{
    // FL's clipboard text: the position with 15 significant digits.
    juce::String positionText (float pos)
    {
        char buf[40];
        std::snprintf (buf, sizeof (buf), "%.15g", (double) pos);
        return buf;
    }

    bool clipboardPosition (double& pos)
    {
        auto t = juce::SystemClipboard::getTextFromClipboard().trim().replaceCharacter (',', '.');
        if (t.isEmpty() || ! t.containsOnly ("0123456789.-+eE"))
            return false;
        pos = t.getDoubleValue();
        return pos >= -1.0e-9 && pos <= 1.0 + 1.0e-9;
    }
}

ParamMenu::ParamMenu (juce::Component& o, juce::RangedAudioParameter& p) : owner (o), param (p) {}

ParamMenu::~ParamMenu()
{
    *alive = false;
    if (editor != nullptr)
        if (auto* parent = editor->getParentComponent())
            parent->removeChildComponent (editor.get());
}

void ParamMenu::setPosition (float normalised)
{
    if (beforeEdit != nullptr)
        beforeEdit();
    param.beginChangeGesture();
    param.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
    param.endChangeGesture();
    if (afterEdit != nullptr)
        afterEdit();
}

void ParamMenu::show()
{
    // FL's knob menu look and order, keeping only the entries that do
    // something in a plugin (FL's Automations / Remote control sections are
    // host features and are left out).
    juce::PopupMenu m;
    std::weak_ptr<bool> life = alive;
    auto ok = [life] { auto a = life.lock(); return a != nullptr && *a; };

    if (! presets.empty())
    {
        for (auto& [label, value] : presets)
        {
            if (label == "-")
            {
                m.addSeparator();
                continue;
            }
            if (auto a = actions.find (label); a != actions.end())
            {
                m.addItem (label, a->second != nullptr, false, a->second);
                continue;
            }
            auto v = value;
            if (auto c = computedPresets.find (label); c != computedPresets.end())
            {
                v = c->second ? c->second() : -1.0f;
                if (v < 0.0f)
                {
                    m.addItem (label, false, false, {});
                    continue;
                }
            }
            const auto n = param.convertTo0to1 (v);
            m.addItem (label, true, std::abs (param.getValue() - n) < 1.0e-6f, [this, ok, n] { if (ok()) setPosition (n); });
        }
        m.addColumnBreak();
    }

    m.addItem ("&Reset", [this, ok] { if (ok()) setPosition (param.getDefaultValue()); });

    // Time knobs get FL's "Set" submenu (lengths in steps at the host tempo).
    const auto id = param.getParameterID();
    if (id.endsWith ("Del") || id.endsWith ("Att") || id.endsWith ("Hold") || id.endsWith ("Dec") || id.endsWith ("Rel"))
    {
        juce::PopupMenu set;
        for (int steps : { 1, 2, 3, 4, 8, 16, 32, 64, 128 })
            set.addItem (juce::String (steps) + (steps == 1 ? " step" : " steps"), [this, ok, steps]
            {
                if (! ok()) return;
                const auto seconds = (float) (steps * 15.0 / juce::jmax (1.0, displayBpm.load()));
                setPosition (param.convertTo0to1 (dsp::flEnvKnobForSeconds (seconds)));
            });
        m.addSubMenu ("&Set", set);
    }
    m.addSeparator();

    m.addItem ("&Copy value", [this, ok] { if (ok()) juce::SystemClipboard::copyTextToClipboard (positionText (param.getValue())); });
    double pos = 0.0;
    if (clipboardPosition (pos))
        m.addItem ("Paste &value  (" + juce::String (juce::roundToInt (pos * 100.0)) + "%)", [this, ok, pos] { if (ok()) setPosition ((float) pos); });
    else
        m.addItem ("Paste &value", false, false, {});
    if (! noTypeIn)
        m.addItem ("&Type in value...", [this, ok] { if (! ok()) return; menuClick = juce::Desktop::getMousePosition(); typeIn(); });

    m.setLookAndFeel (&owner.getLookAndFeel());   // otherwise JUCE uses its default look
    chan::ui::armMenuLetters (m);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
}

namespace
{
    // FL's Type in value box (measured in FL 2026): a 244 x 51 px panel that
    // opens where its menu entry was clicked, the mouse on its A button. A
    // text field holds the value, selected; at the right the mode buttons, the
    // control's own unit (dB, %, C, P, s, or V for a digit wheel) and A (the
    // 0..1 position), the chosen one framed; some controls (MUL, SLIDE, Fine
    // tune) have no buttons and show the position. Below: a tick (apply), a
    // cross (cancel) and "Value for" the control's name. It always opens in the
    // control's own unit; Return or the tick applies, Escape or the cross
    // cancels, and a click elsewhere leaves it open.
    class TypeInBox : public juce::Component
    {
    public:
        struct Mode
        {
            juce::String label;
            std::function<juce::String (float)> text;
            std::function<std::optional<float> (const juce::String&)> parse;
        };
        static constexpr int width = 244, height = 51;

        TypeInBox (const juce::String& name, std::vector<Mode> m, float position)
            : title ("Value for " + name), modes (std::move (m)), current (position)
        {
            field.setFont (chan::ui::font (15.0f));
            field.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
            field.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            field.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
            field.setColour (juce::TextEditor::textColourId, chan::ui::colour::title);
            field.setColour (juce::TextEditor::highlightColourId, chan::ui::colour::accent.withAlpha (0.45f));
            field.setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
            field.setIndents (2, 3);
            field.setText (modes.front().text (position), false);
            field.onReturnKey = [this] { if (onDone) onDone (true); };
            field.onEscapeKey = [this] { if (onDone) onDone (false); };
            addAndMakeVisible (field);
            setSize (width, height);
        }

        // The position the field asks for in the chosen mode, if it means one.
        std::optional<float> typed() const { return modes[(size_t) mode].parse (field.getText()); }
        std::function<void (bool apply)> onDone;
        juce::TextEditor field;

        void resized() override
        {
            const int right = modes.front().label.isNotEmpty() ? button (0).getX() - 3 : width - 3;
            field.setBounds (3, 1, right - 3, 27);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (chan::ui::colour::panelTab);
            g.setColour (chan::ui::colour::panelEdge);
            g.drawRect (getLocalBounds(), 1);
            for (int i = 0; i < (int) modes.size(); ++i)
            {
                if (modes[(size_t) i].label.isEmpty())
                    continue;
                const auto b = button (i).toFloat();
                const bool chosen = i == mode;
                g.setColour (chosen ? chan::ui::colour::accent : chan::ui::colour::comboEdge);
                g.drawRect (b, 1.0f);
                if (chosen)
                {
                    g.setColour (chan::ui::colour::comboInnerTop);
                    g.drawRect (b.reduced (1.0f), 1.0f);
                }
                g.setColour (chosen ? chan::ui::colour::accent : chan::ui::colour::text);
                g.setFont (chan::ui::font (9.5f, true));
                g.drawText (modes[(size_t) i].label, b, juce::Justification::centred, false);
            }
            // the tick and the cross, then the name
            g.setColour (chan::ui::colour::dim);
            juce::Path tick;
            tick.startNewSubPath (6.0f, 38.0f);
            tick.lineTo (9.0f, 41.5f);
            tick.lineTo (15.0f, 34.0f);
            g.strokePath (tick, juce::PathStrokeType (1.4f));
            g.drawLine (26.0f, 34.0f, 33.0f, 41.0f, 1.4f);
            g.drawLine (33.0f, 34.0f, 26.0f, 41.0f, 1.4f);
            g.setFont (chan::ui::font (11.7f));
            g.drawFittedText (title, juce::Rectangle<int> (45, 29, width - 50, 18), juce::Justification::centredLeft, 1, 1.0f);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto p = e.getPosition();
            if (juce::Rectangle<int> (2, 30, 18, 18).contains (p)) { if (onDone) onDone (true); return; }
            if (juce::Rectangle<int> (21, 30, 18, 18).contains (p)) { if (onDone) onDone (false); return; }
            for (int i = 0; i < (int) modes.size(); ++i)
                if (modes[(size_t) i].label.isNotEmpty() && button (i).contains (p) && i != mode)
                {
                    // the value as typed so far, else the control's, in the other unit
                    const auto position = typed().value_or (current);
                    mode = i;
                    field.setText (modes[(size_t) i].text (position), false);
                    field.selectAll();
                    field.grabKeyboardFocus();
                    repaint();
                    return;
                }
            field.grabKeyboardFocus();
        }

    private:
        // The mode buttons from the right: A (or the only one) at 222..240.
        juce::Rectangle<int> button (int i) const
        {
            const int fromRight = (int) modes.size() - 1 - i;
            return { width - 22 - fromRight * 21, 3, 18, 16 };
        }

        juce::String title;
        std::vector<Mode> modes;
        float current;
        int mode = 0;
    };

    // The position as FL's A mode prints it: up to 15 significant digits.
    juce::String positionText (double position)
    {
        std::ostringstream s;
        s << std::setprecision (15) << position;
        return s.str();
    }

    std::optional<double> numberIn (const juce::String& text)
    {
        const auto digits = text.retainCharacters ("+-0123456789.");
        if (! digits.containsAnyOf ("0123456789"))
            return {};
        return digits.getDoubleValue();
    }
}

ParamMenu::Unit ParamMenu::unit() const
{
    // Measured in FL (Type in value on every control of the Sampler window)
    const auto id = param.getParameterID();
    if (dynamic_cast<const juce::AudioParameterInt*> (&param) != nullptr)
        return Unit::value;                                   // digit wheels: V only
    if (id == id::output || id == id::volume)
        return Unit::decibels;
    if (id == id::pitch || id == id::echoPitch || id == id::formant)
        return Unit::cents;
    if (id == id::arpTime || id == id::echoTime)
        return Unit::steps;
    if (id == id::stretchMul || id == id::slide || id == id::fineTune)
        return Unit::position;                                // no buttons: the position
    for (auto* c : { id::pan, id::echoPan, id::pcStereoDelay, id::xOfs, id::yOfs, id::velPan, id::velX, id::velY,
                     id::keyPan, id::keyX, id::keyY, id::echoX, id::echoY })
        if (id == c)
            return Unit::centred;
    return Unit::percent;
}

juce::String ParamMenu::typedText (float position, bool asPosition) const
{
    const auto u = unit();
    // FL: A on a % knob reads the percentage shown (31% is 0.31, -79% on a
    // tension 0.105); a whole percentage is a few decimals, so the float's last bits go
    if (asPosition && u == Unit::percent)
        if (const auto shown = typedPosition (typedText (position)))
            return positionText (std::round ((double) *shown * 1.0e6) / 1.0e6);
    if (asPosition || u == Unit::position)
        return positionText (position);
    if (u == Unit::decibels)
    {
        const auto v = param.convertFrom0to1 (position);
        const auto gain = dsp::flVolumeGain (v);
        return gain > 0.0f ? juce::String (juce::Decibels::gainToDecibels (gain), 1) + "dB" : juce::String ("-INFdB");
    }
    if (u == Unit::percent && ! param.getText (1.0f, 64).contains ("%"))
        return juce::String (juce::roundToInt (position * 100.0f)) + "%";   // e.g. the loop points: FL types them as %
    auto text = param.getText (position, 64);
    return u == Unit::percent ? text.removeCharacters (" ") : text;
}

std::optional<float> ParamMenu::typedPosition (const juce::String& typed, bool asPosition) const
{
    const auto text = typed.trim();
    const auto number = numberIn (text);
    const auto u = unit();
    if (asPosition || u == Unit::position)
    {
        // A: the position; "50%" is half
        if (! number)
            return {};
        return juce::jlimit (0.0f, 1.0f, (float) (text.contains ("%") ? *number / 100.0 : *number));
    }
    switch (u)
    {
        case Unit::decibels:
        {
            // dB through the control's gain law (FL: -6 is -6 dB); "%" reads the knob's own percent
            if (text.containsIgnoreCase ("inf"))
                return 0.0f;
            if (! number)
                return {};
            if (text.contains ("%"))
                return param.convertTo0to1 ((float) (*number / 100.0));
            const auto gain = juce::Decibels::decibelsToGain (*number, -1000.0);
            const auto v = std::log (10.0 * gain + 1.0) / std::log (11.0);
            return param.convertTo0to1 (juce::jlimit (param.getNormalisableRange().start, param.getNormalisableRange().end, (float) v));
        }
        case Unit::percent:
        {
            // FL: 0..1 is a fraction ("0.5" is 50 %), more is a percentage ("2" is 2 %)
            if (! number)
                return {};
            auto percent = *number;
            if (! text.contains ("%") && std::abs (percent) <= 1.0)
                percent *= 100.0;
            if (! param.getText (1.0f, 64).contains ("%"))
                return juce::jlimit (0.0f, 1.0f, (float) (percent / 100.0));
            return param.getValueForText (juce::String (percent) + "%");
        }
        case Unit::centred:
        {
            // FL: whole percents, minus or "left" for the left
            if (text.containsIgnoreCase ("cent"))
                return param.convertTo0to1 (0.0f);
            if (! number)
                return {};
            auto percent = (double) juce::roundToInt (*number);
            if (text.containsIgnoreCase ("left"))
                percent = -std::abs (percent);
            return param.getValueForText (juce::String ((int) percent) + "%");
        }
        case Unit::steps:
        {
            // FL: steps:ticks (or bars:steps:ticks); a bare number counts ticks ("2" is 0:02)
            if (text.containsIgnoreCase ("hold"))
                return param.getValueForText (text);
            if (! number)
                return {};
            return param.getValueForText (text.contains (":") ? text : "0:" + juce::String (*number));
        }
        case Unit::cents:
        case Unit::value:
        case Unit::position:
            break;
    }
    if (! number)
        return {};
    return param.getValueForText (text);   // cents, a digit wheel's value
}

void ParamMenu::typeIn()
{
    // On the editor's canvas (the editor's child that holds the control),
    // never on a host wrapper's component (an AU wrapper sizes the host window
    // from its children).
    auto* host = owner.getTopLevelComponent();
    if (auto* pluginEditor = owner.findParentComponentOfClass<juce::AudioProcessorEditor>())
    {
        host = &owner;
        while (host->getParentComponent() != nullptr && host->getParentComponent() != pluginEditor)
            host = host->getParentComponent();
    }
    if (host == nullptr)
        return;
    closeTypeIn (false);   // one box at a time

    std::vector<TypeInBox::Mode> modes;
    const auto own = [this] (bool asPosition)
    {
        return TypeInBox::Mode { {}, [this, asPosition] (float p) { return typedText (p, asPosition); },
                                 [this, asPosition] (const juce::String& t) { return typedPosition (t, asPosition); } };
    };
    const auto u = unit();
    static const std::map<Unit, juce::String> labels { { Unit::decibels, "dB" }, { Unit::percent, "%" }, { Unit::centred, "C" },
                                                       { Unit::cents, "P" }, { Unit::steps, "s" }, { Unit::value, "V" } };
    modes.push_back (own (false));
    if (u != Unit::position)
    {
        modes.back().label = labels.at (u);
        if (u != Unit::value)
        {
            modes.push_back (own (true));
            modes.back().label = "A";
        }
    }
    auto box = std::make_unique<TypeInBox> (param.getName (64), std::move (modes), param.getValue());

    // Where the menu was clicked, the mouse on the A button (FL); opened otherwise,
    // just below the control. Inside the canvas either way.
    const auto control = host->getLocalArea (&owner, owner.getLocalBounds());
    const auto mouse = menuClick ? host->getLocalPoint (nullptr, *menuClick) : control.getBottomLeft() + juce::Point<int> (235, 46);
    menuClick.reset();
    host->addAndMakeVisible (*box);
    box->setTopLeftPosition (juce::Rectangle<int> (TypeInBox::width, TypeInBox::height)
                                 .withPosition (mouse - juce::Point<int> (235, 42))
                                 .constrainedWithin (host->getLocalBounds()).getPosition());
    std::weak_ptr<bool> life = alive;
    box->onDone = [this, life] (bool apply)
    {
        if (auto a = life.lock(); a != nullptr && *a)
            closeTypeIn (apply);
    };
    box->field.setWantsKeyboardFocus (true);
    box->field.grabKeyboardFocus();
    box->field.selectAll();
    editor = std::move (box);
}

void ParamMenu::closeTypeIn (bool apply)
{
    if (editor == nullptr)
        return;
    auto* box = static_cast<TypeInBox*> (editor.get());
    const auto position = box->typed();
    // Detached at once: no callback of it runs again, and it leaves the screen
    // and gives the keyboard focus away before anything else happens. It is
    // deleted once the key or click that closed it is done with it.
    box->onDone = nullptr;
    box->field.onReturnKey = nullptr;
    box->field.onEscapeKey = nullptr;
    std::shared_ptr<juce::Component> done (editor.release());
    done->setVisible (false);
    if (auto* parent = done->getParentComponent())
        parent->removeChildComponent (done.get());
    juce::MessageManager::callAsync ([done] {});

    if (apply && position)
        setPosition (*position);
}

//==============================================================================
namespace
{
    // How far FL's controls drag for their whole range, in pixels (measured
    // in FL with Copy value before and after a 30 px drag, and pixel by
    // pixel on PITCH and the echo's PITCH). Most of FL's knobs take 256 px
    // (the envelopes' AMT and the velocity Mid slider measured so too).
    double dragPixelsFor (const juce::String& id)
    {
        static const std::map<juce::String, double> table {
            { id::pitch, 192.0 }, { id::stretchMul, 400.0 }, { id::stretchTime, 768.0 },
            { id::startOffset, 2048.0 }, { id::loopStart, 1600.0 }, { id::loopEnd, 1600.0 },
            { id::pcStart, 1024.0 }, { id::pcLength, 1024.0 },
            { id::cutoff, 512.0 }, { id::resonance, 512.0 }, { id::fineTune, 400.0 },
            { id::pan, 512.0 }, { id::volume, 512.0 }, { id::xOfs, 512.0 }, { id::yOfs, 512.0 },
            { id::slide, 1245.0 }, { id::arpTime, 249.3 }, { id::arpGate, 180.0 },
            { id::echoX, 512.0 }, { id::echoY, 512.0 }, { id::echoPitch, 480.0 }, { id::echoTime, 240.0 },
            { id::velPan, 512.0 }, { id::velX, 512.0 }, { id::velY, 512.0 },
            { id::keyPan, 512.0 }, { id::keyX, 512.0 }, { id::keyY, 512.0 }, { id::keyMid, 900.0 } };
        if (const auto it = table.find (id); it != table.end())
            return it->second;
        // Envelope / LFO pages (prefix + name): times run on FL's raw 100..65536.
        for (auto* suffix : { "Del", "Att", "Hold", "Dec", "Rel", "LfoDel", "LfoAtt" })
            if (id.endsWith (suffix))
                return 256.0 * 65436.0 / 65536.0;
        if (id.endsWith ("LfoSpeed"))
            return 256.0 * 65336.0 / 65536.0;
        if (id.endsWith ("AttT") || id.endsWith ("DecT") || id.endsWith ("RelT"))
            return 512.0;
        return 256.0;
    }
}

ParamKnob::ParamKnob (juce::RangedAudioParameter& p, SliderStyle style)
    : juce::Slider (style, juce::Slider::NoTextBox), menu (*this, p)
{
    dragPixels = dragPixelsFor (p.getParameterID());
    const auto& range = p.getNormalisableRange();
    if (std::abs (range.start + range.end) <= 1.0e-6f * std::abs (range.end))
        detents.push_back (p.convertTo0to1 (0.0f));       // two-sided: the centre
    else if (std::abs (p.getDefaultValue() - 0.5f) < 1.0e-4f)
        detents.push_back (0.5);                          // e.g. MUL (x1 in the middle)
    if (p.getParameterID() == id::output)
        detents.push_back (p.getDefaultValue());          // OUT: its default level
    if (p.getParameterID() == id::pitch)                 // the echo's PITCH has none (measured)
        for (int semitone = -12; semitone <= 12; ++semitone)
            if (semitone != 0)
                detents.push_back (p.convertTo0to1 ((float) semitone * 100.0f));
    if (p.getParameterID() == id::stretchTime)
    {
        // TIME (measured in FL by dragging and reading its hint): one 16th
        // step per pixel, whole steps only, and the 20 px hold at every bar
        // (1:00:00 from 16 to 36 px, 2:00:00 at 52 px, 3:00:00 at 88 px).
        snapSteps = range.end * 4.0;
        for (float bar = 4.0f; bar < range.end; bar += 4.0f)
            detents.push_back (p.convertTo0to1 (bar));
    }
    if (! isRotary())
        setSliderSnapsToMousePosition (false);            // sliders drag from where they are, like knobs
    menu.beforeEdit = [this] { if (onDragStart != nullptr) onDragStart(); };
    menu.afterEdit = [this] { if (onUserChange != nullptr) onUserChange(); };
    // After the attachment has passed the value to the parameter.
    onValueChange = [this]
    {
        if (onUserChange != nullptr && (isMouseButtonDown() || wheelActive))
            onUserChange();
    };
}

void ParamKnob::resized()
{
    juce::Slider::resized();
    hover.setSlowOut (isRotary() && getWidth() > 50);   // the filter knob's slower fade-out
}

double ParamKnob::snapValue (double attemptedValue, juce::Slider::DragMode)
{
    if (snapSteps <= 0.0)
        return attemptedValue;
    const auto p = std::round (valueToProportionOfLength (attemptedValue) * snapSteps) / snapSteps;
    return proportionOfLengthToValue (p);
}

void ParamKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (locked || ! isEnabled() || isMouseButtonDown())
        return;
    // JUCE reports 0.46875 per notch on Windows; 8 px of drag a notch (half
    // with Ctrl), and the other way on a slider (measured).
    auto d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
    if (w.isReversed)
        d = -d;
    const double notches = d / 0.46875;
    double step = notches * wheelPixels / dragPixels * (e.mods.isCtrlDown() ? 0.5 : 1.0);
    if (! isRotary())
        step = -step;
    if (step == 0.0)
        return;
    auto pos = juce::jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + step);
    if (snapSteps > 0.0)
        pos = std::round (pos * snapSteps) / snapSteps;
    // Through JUCE's own wheel path (0.15 of the range per unit), so the
    // attachment hears the drag start / end as the gesture; snapValue holds TIME's steps.
    const auto now = valueToProportionOfLength (getValue());
    const auto drawn = glide.shown (now);   // where the knob is drawn (mid-glide or not)
    const juce::MouseWheelDetails scaled { 0.0f, (float) ((pos - now) / 0.15), false, false, false };
    wheelActive = true;
    juce::Slider::mouseWheelMove (e, scaled);
    wheelActive = false;
    if (std::abs (valueToProportionOfLength (getValue()) - drawn) > 1.0e-6)
        glide.startFrom (drawn);
}

void ParamKnob::valueChanged()
{
    // anything but the wheel (a drag, the menu, the host, an undo) shows at once
    if (! wheelActive)
        glide.cancel();
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (locked)
        return;
    if (e.mods.isPopupMenu())
    {
        menu.show();
        return;
    }
    if (! isEnabled())
        return;
    if (e.mods.isAltDown())
    {
        // Alt+click: back to the default (measured on FL's knobs and sliders)
        juce::Slider::mouseDown (e);
        setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);
        return;
    }
    juce::Slider::mouseDown (e);   // starts the gesture
    dragPos = valueToProportionOfLength (getValue());
    lastMouse = e.position;
    holding = -1;
    walking = false;
    if (! isRotary() && std::abs (e.position.x - thumbX (dragPos)) > 9.5f)
    {
        // a press on the track: the ball walks towards the mouse
        walking = true;
        walkTowards (e.position.x, walkStep);
        walker.start (e.position.x);
        return;
    }
    // As FL: the cursor hides while dragging and the drag goes on past the
    // screen's edges; it comes back at the control.
    e.source.enableUnboundedMouseMovement (true);
}

void ParamKnob::mouseUp (const juce::MouseEvent& e)
{
    walker.stop();
    walking = false;
    e.source.enableUnboundedMouseMovement (false);
    juce::Slider::mouseUp (e);
}

float ParamKnob::thumbX (double proportion) const
{
    // as drawn by LookAndFeel::drawLinearSlider: the ball's line
    const auto outerX = 4.5f, outerW = (float) getWidth() - 9.0f;
    return std::floor (outerX + 9.5f + (float) proportion * (outerW - 19.0f)) + 0.5f;
}

void ParamKnob::walkTowards (float x, float maxStep)
{
    const auto pos = valueToProportionOfLength (getValue());
    const auto here = thumbX (pos);
    const auto perPixel = 1.0 / juce::jmax (1.0, (double) getWidth() - 9.0 - 19.0);   // proportion per thumb pixel
    const auto wanted = (double) (x - here) * perPixel;
    const auto step = juce::jlimit (-(double) maxStep * perPixel, (double) maxStep * perPixel, wanted);
    if (std::abs (step) < 1.0e-9)
    {
        walker.stop();
        return;
    }
    dragPos = juce::jlimit (0.0, 1.0, pos + step);
    setValue (proportionOfLengthToValue (dragPos), juce::sendNotificationSync);
}

void ParamKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (locked)
        return;
    if (e.mods.isPopupMenu() || ! isEnabled() || e.mods.isAltDown())
        return;
    if (walking)
    {
        walker.setTarget (e.position.x);
        return;
    }

    // Relative drag (up / right raises), with FL's detents; a quarter as fast with Ctrl.
    auto d = isRotary() ? (e.position.x - lastMouse.x) - (e.position.y - lastMouse.y)
                        : (e.position.x - lastMouse.x);
    if (e.mods.isCtrlDown())
        d *= fineDrag;
    lastMouse = e.position;
    const auto full = dragPixels;

    if (holding >= 0)
    {
        heldPx += d;
        if (std::abs (heldPx) <= detentHold)
            return;
        dragPos = detents[(size_t) holding] + (heldPx - std::copysign (detentHold, heldPx)) / full;
        holding = -1;
    }
    else
    {
        const auto next = dragPos + d / full;
        int arrived = -1;
        for (size_t i = 0; i < detents.size(); ++i)
        {
            const auto t = detents[i];
            if ((dragPos < t && next >= t) || (dragPos > t && next <= t))
                if (arrived < 0 || std::abs (t - dragPos) < std::abs (detents[(size_t) arrived] - dragPos))
                    arrived = (int) i;   // the first one on the way
        }
        if (arrived >= 0)
        {
            const auto t = detents[(size_t) arrived];
            holding = arrived;
            heldPx = (float) ((next - t) * full);
            dragPos = t;
            if (std::abs (heldPx) > detentHold)
            {
                dragPos = t + (heldPx - std::copysign (detentHold, heldPx)) / full;
                holding = -1;
            }
        }
        else
        {
            dragPos = next;
        }
    }
    dragPos = juce::jlimit (0.0, 1.0, dragPos);
    const auto shown = snapSteps > 0.0 ? std::round (dragPos * snapSteps) / snapSteps : dragPos;
    setValue (proportionOfLengthToValue (shown), juce::sendNotificationSync);
}

ParamDigiBox::ParamDigiBox (juce::RangedAudioParameter& p, bool redStyle)
    : chan::ui::DigiBox (redStyle), menu (*this, p) {}

void ParamDigiBox::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        menu.show();
    else
        chan::ui::DigiBox::mouseDown (e);
}
}
