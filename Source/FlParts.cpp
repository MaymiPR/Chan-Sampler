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
    startTimerHz (20);
}

float EnvelopeDisplay::get (const char* suffix) const
{
    return apvts.getRawParameterValue (prefix + suffix)->load();
}

void EnvelopeDisplay::timerCallback()
{
    float hash = 0.0f;
    int i = 1;
    for (auto s : { "EnvOn", "Del", "Att", "Hold", "Dec", "Sus", "Rel", "AttT", "DecT", "RelT" })
        hash += get (s) * (float) (i++ * 7919);
    if (hash != lastHash)
    {
        lastHash = hash;
        repaint();
    }
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
    const auto lineColour = fl::colour::envLine;
    g.setColour (lineColour);
    g.strokePath (p, juce::PathStrokeType (1.6f));

    const auto pts = points (gm);
    for (int i = 0; i < 5; ++i)
    {
        const bool hot = on && (i == hovered || i == dragging);
        const auto size = on ? 7.0f : 5.5f;
        const auto dot = juce::Rectangle<float> (size, size).withCentre (pts[(size_t) i]);
        g.setColour (hot ? lineColour : fl::colour::well);
        g.fillEllipse (dot);
        g.setColour (lineColour);
        g.drawEllipse (dot, 1.4f);
    }
}

//==============================================================================
LfoDisplay::LfoDisplay (juce::AudioProcessorValueTreeState& s, const char* p)
    : apvts (s), prefix (p)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (20);
}

float LfoDisplay::get (const char* suffix) const
{
    return apvts.getRawParameterValue (prefix + suffix)->load();
}

void LfoDisplay::timerCallback()
{
    float hash = 0.0f;
    int i = 1;
    for (auto s : { "LfoShape", "LfoSpeed", "LfoAmt", "LfoDel", "LfoAtt" })
        hash += get (s) * (float) (i++ * 7919);
    if (hash != lastHash)
    {
        lastHash = hash;
        repaint();
    }
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
    g.strokePath (p, juce::PathStrokeType (1.6f));
}

//==============================================================================
RootKeyboard::RootKeyboard (juce::AudioProcessorValueTreeState& s, juce::MidiKeyboardState& k)
    : apvts (s), state (k)
{
    startTimerHz (30);
}

juce::Rectangle<float> RootKeyboard::keyBounds (int note) const
{
    const auto kw = (float) getWidth() / (float) numWhite;
    const auto top = (float) labelH;
    const auto h = (float) getHeight() - top;

    // Index of the white key at or below this note
    int whites = 0;
    for (int n = lowNote; n < note; ++n)
        whites += isBlack (n) ? 0 : 1;

    if (! isBlack (note))
        return { (float) whites * kw, top, kw, h };

    const auto bw = kw * 0.62f;
    return { (float) whites * kw - bw * 0.5f, top, bw, h * 0.62f };
}

int RootKeyboard::noteAt (juce::Point<float> p) const
{
    const int high = lowNote + numWhite / 7 * 12;
    for (int n = lowNote; n < high; ++n)
        if (isBlack (n) && keyBounds (n).contains (p))
            return n;
    for (int n = lowNote; n < high; ++n)
        if (! isBlack (n) && keyBounds (n).withTop (0.0f).contains (p))
            return n;
    return -1;
}

void RootKeyboard::paint (juce::Graphics& g)
{
    const int root = (int) apvts.getRawParameterValue (pid::rootNote)->load();
    const int high = lowNote + numWhite / 7 * 12;
    const auto kw = (float) getWidth() / (float) numWhite;

    // Label strip with octave names
    g.setColour (juce::Colour (0xffc3cad3));
    g.fillRect (0, 0, getWidth(), labelH);
    g.setFont (fl::font (9.5f, true));
    for (int n = lowNote; n < high; n += 12)
    {
        const auto kb = keyBounds (n);
        g.setColour (fl::colour::well);
        g.drawText (flNoteName (n), juce::Rectangle<float> (kb.getX() + 2.0f, 0.0f, kw * 3.0f, (float) labelH),
                    juce::Justification::centredLeft, false);
    }
    if (root >= lowNote && root < high)
    {
        g.setColour (fl::colour::keyRoot);
        const auto kb = keyBounds (root);
        g.fillRect (kb.getX(), 0.0f, kb.getWidth(), (float) labelH);
        g.setColour (fl::colour::well);
        g.drawText (flNoteName (root), juce::Rectangle<float> (kb.getX() + 2.0f, 0.0f, kw * 3.0f, (float) labelH),
                    juce::Justification::centredLeft, false);
    }

    for (int pass = 0; pass < 2; ++pass)
    {
        for (int n = lowNote; n < high; ++n)
        {
            if (isBlack (n) != (pass == 1))
                continue;
            const auto kb = keyBounds (n);
            auto c = isBlack (n) ? fl::colour::keyBlack : fl::colour::keyWhite;
            if (n == root)            c = fl::colour::keyRoot;
            if (state.isNoteOn (1, n)) c = c.interpolatedWith (fl::colour::amber, 0.55f);
            g.setColour (c);
            g.fillRect (isBlack (n) ? kb : kb.reduced (0.5f, 0.0f));
            if (! isBlack (n))
            {
                g.setColour (juce::Colour (0xffaab2bc));
                g.drawVerticalLine ((int) kb.getRight(), kb.getY(), kb.getBottom());
            }
        }
    }
}

void RootKeyboard::mouseDown (const juce::MouseEvent& e)
{
    const int n = noteAt (e.position);
    if (n < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        // Right-click sets the root key (FL behaviour)
        auto* p = apvts.getParameter (pid::rootNote);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) n));
        p->endChangeGesture();
        return;
    }

    playing = n;
    state.noteOn (1, n, 100.0f / 127.0f);
}

void RootKeyboard::mouseDrag (const juce::MouseEvent& e)
{
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
    if (playing >= 0)
        state.noteOff (1, playing, 0.0f);
    playing = -1;
}

void RootKeyboard::timerCallback()
{
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
RootNotePanel::RootNotePanel (juce::AudioProcessorValueTreeState& s, juce::MidiKeyboardState& k)
    : fl::Panel ("Root note: C5"), apvts (s), keyboard (s, k),
      fine (*s.getParameter (pid::fineTune), juce::Slider::LinearHorizontal)
{
    resetButton.setTooltip ("Default root note and fine tune");
    resetButton.onClick = [this]
    {
        auto reset = [this] (const char* id, float value)
        {
            auto* p = apvts.getParameter (id);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        };
        reset (pid::rootNote, 60.0f);
        reset (pid::fineTune, 0.0f);
    };

    fineLabel.setFont (fl::font (11.5f));
    fineAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, pid::fineTune, fine);
    fine.setDoubleClickReturnValue (true, 0.0);
    fine.setTooltip ("Fine tuning");

    hint.setFont (fl::font (11.0f));
    hint.setJustificationType (juce::Justification::centredRight);
    hint.setColour (juce::Label::textColourId, fl::colour::dim);

    for (auto* c : std::initializer_list<juce::Component*> { &resetButton, &fineLabel, &fine, &hint, &keyboard })
        addAndMakeVisible (c);

    // Layout from FL's KeybPanel (544 x 137)
    resetButton.setBounds (3, 23, 46, 21);
    fineLabel.setBounds (58, 23, 56, 21);
    fine.setBounds (114, 24, 170, 22);
    hint.setBounds (290, 23, 248, 21);
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
ParamKnob::ParamKnob (juce::RangedAudioParameter& p, SliderStyle style)
    : juce::Slider (style, juce::Slider::NoTextBox), menu (*this, p)
{
    menu.beforeEdit = [this] { if (onDragStart != nullptr) onDragStart(); };
    menu.afterEdit = [this] { if (onUserChange != nullptr) onUserChange(); };
    // After the attachment has passed the value to the parameter.
    onValueChange = [this]
    {
        if (onUserChange != nullptr && (isMouseButtonDown() || wheelActive))
            onUserChange();
    };
}

void ParamKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    wheelActive = true;
    juce::Slider::mouseWheelMove (e, w);
    wheelActive = false;
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        menu.show();
    else
        juce::Slider::mouseDown (e);
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
