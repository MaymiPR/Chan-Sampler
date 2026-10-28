#include "FlParts.h"
#include "Dsp.h"
#include "Params.h"

namespace
{
    void fillWell (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // FL: a plain dark display without grid lines.
        g.setColour (fl::colour::well);
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
    const auto attK = flTensionToK (get ("AttT")), decK = flTensionToK (get ("DecT")), relK = flTensionToK (get ("RelT"));
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
    g.setColour (fl::colour::envLine);
    g.strokePath (p, juce::PathStrokeType (1.5f));

    // FL's points: 8 px rings with a dark green fill
    const auto pts = points (gm);
    for (int i = 0; i < 5; ++i)
    {
        const bool hot = on && (i == hovered || i == dragging);
        const auto size = on ? 9.0f : 8.0f;
        const auto dot = juce::Rectangle<float> (size, size).withCentre (pts[(size_t) i]);
        g.setColour (hot ? fl::colour::envPoint : fl::colour::envPointFill);
        g.fillEllipse (dot);
        g.setColour (fl::colour::envPoint);
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
    g.setColour (fl::colour::envLine);
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
        const int root = (int) apvts.getRawParameterValue (pid::rootNote)->load();
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
    const int root = (int) apvts.getRawParameterValue (pid::rootNote)->load();
    const auto w = (float) getWidth();
    // FL's keyboard, pixel by pixel: a flat label strip, a rule, white
    // keys shading lighter downwards (no borders between them; C keys and the
    // root are flat), black keys with a dark outline, a lit left edge, a
    // front face and a lip, and a shadow row under them.
    using namespace fl::key;
    const juce::Colour rootBlue = fl::key::root, labelText = fl::key::label, rootText = fl::key::rootLabel;

    g.setColour (strip);
    g.fillRect (0.0f, 0.0f, w, labelH);
    g.setColour (rule);
    g.fillRect (0.0f, labelH, w, 1.0f);
    g.setGradientFill (juce::ColourGradient (whiteTop, 0.0f, keysTop, whiteBottom, 0.0f, keysBottom, false));
    g.fillRect (0.0f, keysTop, w, keysBottom - keysTop);

    // FL (measured): a played key turns orange
    auto pressedKey = [this] (int n, juce::Colour c) { return state.isNoteOn (1, n) ? fl::key::pressed : c; };
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
            g.setFont (fl::font (10.5f));
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
        else if (e.position.x > getWidth() - arrowW)   setScroll (scroll + octaveW);
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
        setParameterAsGesture (*apvts.getParameter (pid::rootNote), (float) n);
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
    const int root = (int) apvts.getRawParameterValue (pid::rootNote)->load();
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
    : fl::Panel ("Root note: C5"), apvts (s), history (h),
      fine (*s.getParameter (pid::fineTune), juce::Slider::LinearHorizontal), keyboard (s, k)
{
    resetButton.setTooltip ("Default root note and fine tune");
    resetButton.onClick = [this]
    {
        const History::Transaction edit (history, "Reset root note");
        setParameterAsGesture (*apvts.getParameter (pid::rootNote), 60.0f);
        setParameterAsGesture (*apvts.getParameter (pid::fineTune), 0.0f);
    };

    fineLabel.setFont (fl::font (11.5f));
    fineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, pid::fineTune, fine);
    fine.setDoubleClickReturnValue (false, 0.0);   // the default, for the arc; a double-click does nothing (as in FL)
    fine.setTooltip ("Fine tuning");

    hint.setFont (fl::font (10.5f));
    hint.setJustificationType (juce::Justification::centredLeft);
    hint.setColour (juce::Label::textColourId, fl::colour::dim);

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
    const int root = (int) apvts.getRawParameterValue (pid::rootNote)->load();
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
    if (editor != nullptr && editor->getParentComponent() != nullptr)
        editor->getParentComponent()->removeChildComponent (editor.get());
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
                setPosition (param.convertTo0to1 (flEnvKnobForSeconds (seconds)));
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
        m.addItem ("&Type in value...", [this, ok] { if (ok()) typeIn(); });

    m.setLookAndFeel (&owner.getLookAndFeel());   // otherwise JUCE uses its default look
    fl::armMenuLetters (m);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
}

void ParamMenu::typeIn()
{
    // FL opens a small text box at the control with the current text selected.
    auto* host = owner.getTopLevelComponent();
    if (host == nullptr)
        return;

    editor = std::make_unique<juce::TextEditor>();
    auto& ed = *editor;
    ed.setFont (juce::FontOptions (13.0f));
    ed.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1c2226));
    ed.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    ed.setColour (juce::TextEditor::outlineColourId, fl::colour::amber);
    ed.setColour (juce::TextEditor::focusedOutlineColourId, fl::colour::amber);
    ed.setJustification (juce::Justification::centred);
    ed.setText (param.getCurrentValueAsText(), false);

    const auto area = host->getLocalArea (&owner, owner.getLocalBounds());
    const auto w = juce::jmax (110, area.getWidth() * 3);
    host->addAndMakeVisible (ed);
    ed.setBounds (juce::Rectangle<int> (w, 22).withCentre ({ area.getCentreX(), area.getBottom() + 12 })
                      .constrainedWithin (host->getLocalBounds()));
    ed.setWantsKeyboardFocus (true);
    ed.grabKeyboardFocus();
    ed.selectAll();

    std::weak_ptr<bool> life = alive;
    auto close = [this, life] (bool apply)
    {
        auto a = life.lock();
        if (a == nullptr || ! *a || editor == nullptr)
            return;
        auto text = editor->getText();
        editor->onFocusLost = nullptr;
        editor->onReturnKey = nullptr;
        juce::MessageManager::callAsync ([this, life]
        {
            auto a2 = life.lock();
            if (a2 != nullptr && *a2 && editor != nullptr)
            {
                if (auto* parent = editor->getParentComponent())
                    parent->removeChildComponent (editor.get());
                editor.reset();
            }
        });
        if (apply && text.trim().isNotEmpty())
            setPosition (param.getValueForText (text));
    };
    ed.onReturnKey = [close] { close (true); };
    ed.onEscapeKey = [close] { close (false); };
    ed.onFocusLost = [close] { close (true); };
}

//==============================================================================
namespace
{
    // How far FL's controls drag for their whole range, in pixels (measured
    // in FL with Copy value before and after a 30 px drag, and pixel by
    // pixel on PITCH and the echo's PITCH). Most of FL's knobs take 256 px
    // (the envelopes' AMT and the velocity Mid slider measured so too).
    double flDragPixels (const juce::String& id)
    {
        static const std::map<juce::String, double> table {
            { pid::pitch, 192.0 }, { pid::stretchMul, 400.0 }, { pid::stretchTime, 768.0 },
            { pid::startOffset, 2048.0 }, { pid::loopStart, 1600.0 }, { pid::loopEnd, 1600.0 },
            { pid::pcStart, 1024.0 }, { pid::pcLength, 1024.0 },
            { pid::cutoff, 512.0 }, { pid::resonance, 512.0 }, { pid::fineTune, 400.0 },
            { pid::pan, 512.0 }, { pid::volume, 512.0 }, { pid::xOfs, 512.0 }, { pid::yOfs, 512.0 },
            { pid::slide, 1245.0 }, { pid::arpTime, 249.3 }, { pid::arpGate, 180.0 },
            { pid::echoX, 512.0 }, { pid::echoY, 512.0 }, { pid::echoPitch, 480.0 }, { pid::echoTime, 240.0 },
            { pid::velPan, 512.0 }, { pid::velX, 512.0 }, { pid::velY, 512.0 },
            { pid::keyPan, 512.0 }, { pid::keyX, 512.0 }, { pid::keyY, 512.0 }, { pid::keyMid, 900.0 } };
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
    dragPixels = flDragPixels (p.getParameterID());
    const auto& range = p.getNormalisableRange();
    if (std::abs (range.start + range.end) <= 1.0e-6f * std::abs (range.end))
        detents.push_back (p.convertTo0to1 (0.0f));       // two-sided: the centre
    else if (std::abs (p.getDefaultValue() - 0.5f) < 1.0e-4f)
        detents.push_back (0.5);                          // e.g. MUL (x1 in the middle)
    if (p.getParameterID() == pid::output)
        detents.push_back (p.getDefaultValue());          // OUT: its default level
    if (p.getParameterID() == pid::pitch)                 // the echo's PITCH has none (measured)
        for (int semitone = -12; semitone <= 12; ++semitone)
            if (semitone != 0)
                detents.push_back (p.convertTo0to1 ((float) semitone * 100.0f));
    if (p.getParameterID() == pid::stretchTime)
    {
        // TIME (measured in FL, its hint read pixel by pixel): one 16th
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
    const juce::MouseWheelDetails scaled { 0.0f, (float) ((pos - now) / 0.15), false, false };
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
    : fl::DigiBox (redStyle), menu (*this, p) {}

void ParamDigiBox::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        menu.show();
    else
        fl::DigiBox::mouseDown (e);
}
