#include "Widgets.h"

namespace chan::ui
{
juce::Font font (float em, bool bold)
{
    // FL draws its UI with Segoe UI on Windows. JUCE sizes a font by ascent +
    // descent, which for Segoe UI is 1.333 em.
    const auto style = bold ? juce::Font::bold : juce::Font::plain;
    const auto height = em * 1.3333f;
   #if JUCE_WINDOWS
    return juce::Font (juce::FontOptions ("Segoe UI", height, style));
   #else
    // Segoe UI is Windows-only. Elsewhere use the system sans font, narrowed
    // to Segoe UI's width so every label fits as designed (measured on
    // Windows with ChanSamplerSnapshot fontwidth: the reference text is 33.87
    // / 36.17 font heights wide, plain / bold).
    auto widthScale = [] (bool b)
    {
        const juce::Font ref (juce::FontOptions (120.0f, b ? juce::Font::bold : juce::Font::plain));
        const auto w = juce::GlyphArrangement::getStringWidth (ref, fontReferenceText) / 100.0f;
        return w > 0.0f ? juce::jmin (1.0f, (b ? 36.17f : 33.87f) / w) : 1.0f;
    };
    static const float plainScale = widthScale (false), boldScale = widthScale (true);
    return juce::Font (juce::FontOptions (height, style)).withHorizontalScale (bold ? boldScale : plainScale);
   #endif
}

//==============================================================================
// The shaded controls, drawn from the measured sizes and the colour tokens in
// Theme.h with our own shading: discs, rings and arcs filled with gradients
// between measured end colours, highlights as arcs fading out at their ends.
namespace
{
    juce::Rectangle<float> circle (juce::Point<float> c, float r) { return { c.x - r, c.y - r, 2.0f * r, 2.0f * r }; }

    // The band between radii lo and hi.
    juce::Path ring (juce::Point<float> c, float lo, float hi)
    {
        juce::Path p;
        p.addEllipse (circle (c, hi));
        p.addEllipse (circle (c, lo));
        p.setUsingNonZeroWinding (false);
        return p;
    }

    // Top to bottom over [y0, y1], through mid half way when it is given.
    juce::ColourGradient vertical (juce::Colour top, juce::Colour bottom, float y0, float y1, juce::Colour mid = {})
    {
        juce::ColourGradient grad (top, 0.0f, y0, bottom, 0.0f, y1, false);
        if (! mid.isTransparent())
            grad.addColour (0.5, mid);
        return grad;
    }

    // A light along radius r centred on angle at (clockwise from the top),
    // halfWidth radians each way, fading to nothing at its ends.
    void glint (juce::Graphics& g, juce::Point<float> c, float r, float width, juce::Colour colour, float at, float halfWidth)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r, r, 0.0f, at - halfWidth, at + halfWidth, true);
        const auto from = c.getPointOnCircumference (r, at - halfWidth), to = c.getPointOnCircumference (r, at + halfWidth);
        juce::ColourGradient grad (colour.withAlpha (0.0f), from, colour.withAlpha (0.0f), to, false);
        grad.addColour (0.5, colour);
        g.setGradientFill (grad);
        g.strokePath (arc, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // A soft round light (or shade) at p, clear at radius r.
    void spot (juce::Graphics& g, juce::Point<float> p, float r, juce::Colour colour)
    {
        g.setGradientFill (juce::ColourGradient (colour, p, colour.withAlpha (0.0f), p.translated (r, 0.0f), true));
        g.fillEllipse (circle (p, r));
    }

    // Everything drawn inside fades with alpha (a greyed control).
    struct Fade
    {
        Fade (juce::Graphics& gr, float alpha) : g (gr), on (alpha < 1.0f) { if (on) g.beginTransparencyLayer (alpha); }
        ~Fade() { if (on) g.endTransparencyLayer(); }
        juce::Graphics& g;
        bool on;
    };

    // The regular knob (22 to 38 px): a 1 px dark outer ring, a 2 px bevel
    // lit from above, a 1 px dark gap, and a body 4 px in from the edge,
    // darkening downwards with a light along its top rim and a fainter one at
    // its foot. The value dot sits 3.6 px inside the body; the arc runs on the
    // bevel. A knob turns a full circle: value 0.5 is straight up.
    void renderKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, float zeroPos, juce::Colour arc, float hover, float alpha, bool showDot)
    {
        const Fade fade (g, alpha);
        const auto R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f, Rb = R - 4.0f;
        const auto c = area.getCentre();
        const auto ang = juce::MathConstants<float>::twoPi * (pos - 0.5f), zero = juce::MathConstants<float>::twoPi * (zeroPos - 0.5f);

        g.setColour (colour::knobOuter);
        g.fillEllipse (circle (c, R));
        g.setGradientFill (vertical (colour::knobBevelTop, colour::knobBevelBottom, c.y - R, c.y + R));
        g.fillPath (ring (c, R - 3.0f, R - 1.0f));
        g.setGradientFill (vertical (colour::knobGapTop, colour::knobGapBottom, c.y - R, c.y + R));
        g.fillPath (ring (c, Rb, R - 3.0f));
        g.setGradientFill (vertical (colour::knobBodyTop, colour::knobBodyBottom, c.y - Rb, c.y + Rb));
        g.fillEllipse (circle (c, Rb));
        glint (g, c, Rb - 1.0f, 2.0f, colour::knobBodyRim, 0.0f, 1.2f);
        glint (g, c, Rb - 0.8f, 1.6f, colour::knobBodyFoot, juce::MathConstants<float>::pi, 1.0f);
        if (hover > 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.018f * hover));   // a touch lighter under the mouse
            g.fillEllipse (circle (c, R));
        }

        if (std::abs (pos - zeroPos) > 0.002f)
        {
            juce::Path value;
            value.addCentredArc (c.x, c.y, R - 2.09f, R - 2.09f, 0.0f, juce::jmin (zero, ang), juce::jmax (zero, ang), true);
            g.setColour (arc);
            g.strokePath (value, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        if (showDot)
        {
            // the value dot: a 1.5 x 1.8 px mark, radial, at 73 % of the arc colour
            juce::Path dot;
            dot.addRectangle (-0.75f, -0.9f, 1.5f, 1.8f);
            const auto at = c.getPointOnCircumference (Rb - 3.56f, ang);
            g.setColour (arc.withMultipliedBrightness (0.73f));
            g.fillPath (dot, juce::AffineTransform::rotation (ang).translated (at));
        }
    }

    // The filter knob (66 px): a cap lit from above with a soft light up to its
    // right, a dark collar, a wide ring lit at its top and dark at its sides, a
    // shadow at its foot. The knob sweeps 320 degrees; the dot (green on
    // MOD X, gold on MOD Y) rides at 22.5 px; nine 1 px tick marks sit every
    // 40 degrees from 29.5 px out. Under the mouse the cap lights up.
    void renderBigKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, juce::Colour dotColour, float hover, float alpha)
    {
        const Fade fade (g, alpha);
        const juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area.toNearestIntEdges());
        const auto c = area.getCentre();
        const auto k = juce::jmin (area.getWidth(), area.getHeight()) / 66.0f;
        const auto ang = juce::degreesToRadians (-160.0f + 320.0f * pos);

        for (int i = -4; i <= 4; ++i)
        {
            const auto a = juce::degreesToRadians (40.0f * (float) i);
            g.setColour (colour::bigTick);
            g.drawLine (juce::Line<float> (c.getPointOnCircumference (29.5f * k, a), c.getPointOnCircumference (36.0f * k, a)), 1.0f);
        }
        spot (g, c.translated (0.0f, 1.0f * k), 26.0f * k, colour::bigShadow);
        g.setGradientFill (vertical (colour::bigRingLight, colour::bigRingFoot, c.y - 23.0f * k, c.y + 23.0f * k, colour::bigRingDark));
        g.fillEllipse (circle (c, 23.0f * k));
        // Under the mouse the ring takes a faint glow of the knob's colour and a
        // dark collar grows out from the cap, five notches in it that turn with
        // the value (measured in FL: five notches 72 degrees apart, each about
        // 2.4 px deep and 41 degrees wide, the first 32 degrees on from the dot).
        // Both follow the hover fade, eased, so the collar opens and closes with it.
        const auto open = hover * hover * (3.0f - 2.0f * hover);
        if (open > 0.0f)
        {
            g.setColour (dotColour.withAlpha (0.16f * open));
            g.fillPath (ring (c, 18.5f * k, 23.0f * k));
        }
        const auto spacing = juce::degreesToRadians (72.0f), halfWidth = juce::degreesToRadians (20.5f);
        juce::Path collar;
        for (int i = 0; i <= 180; ++i)
        {
            const auto t = juce::MathConstants<float>::twoPi * (float) i / 180.0f;
            const auto u = std::remainder (t - ang - juce::degreesToRadians (32.0f), spacing);
            const auto notch = std::abs (u) < halfWidth ? 2.4f * juce::square (std::cos (juce::MathConstants<float>::halfPi * u / halfWidth)) : 0.0f;
            const auto p = c.getPointOnCircumference ((18.5f + open * (3.0f - notch)) * k, t);
            if (i == 0) collar.startNewSubPath (p); else collar.lineTo (p);
        }
        collar.closeSubPath();
        g.setColour (colour::bigCollar);
        g.fillPath (collar);
        g.setGradientFill (vertical (colour::bigCapTop, colour::bigCapBottom, c.y - 17.5f * k, c.y + 17.5f * k, colour::bigCapMid));
        g.fillEllipse (circle (c, 17.5f * k));
        // darker towards its sides, a light up to the right
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), c, juce::Colours::black.withAlpha (0.25f),
                                                 c.translated (17.5f * k, 0.0f), true));
        g.fillEllipse (circle (c, 17.5f * k));
        spot (g, c.getPointOnCircumference (10.0f * k, juce::degreesToRadians (30.0f)), 8.0f * k, colour::bigCapLight);
        spot (g, c.getPointOnCircumference (12.0f * k, juce::degreesToRadians (210.0f)), 5.0f * k, colour::bigCapLight.withMultipliedAlpha (0.4f));
        if (open > 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.1f * open));
            g.fillEllipse (circle (c, 17.5f * k));
        }
        // the value: an arc just outside the ring, as on the small knobs, and the dot on the ring
        juce::Path value;
        value.addCentredArc (c.x, c.y, 25.0f * k, 25.0f * k, 0.0f, juce::degreesToRadians (-160.0f), ang, true);
        g.setColour (dotColour.withAlpha (0.55f + 0.45f * open));
        g.strokePath (value, juce::PathStrokeType (1.6f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (dotColour.withAlpha (0.6f + 0.4f * open));
        g.fillEllipse (circle (c.getPointOnCircumference (22.5f * k, ang), 1.6f * k));
    }

    // The round switch: a 15 px light ring, a dark gap, and an inner disc
    // darker at the top with a light along its upper rim; lit, the disc glows
    // orange (or green), brightest at its foot, with a highlight at the top.
    // The 11 px switch (CLIP) has a 5 px disc, a 1 px gap and a 2 px ring.
    void renderLed (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool green, float alpha)
    {
        const Fade fade (g, alpha);
        const auto S = juce::jmin (area.getWidth(), area.getHeight());
        const auto k = S / 15.0f;
        const auto c = area.getCentre();
        const bool small = S < 13.0f;
        const auto ri = small ? 2.5f : 4.61f * k, ringIn = small ? 3.5f : 6.06f * k, ringOut = small ? 5.5f : 7.56f * k;

        g.setColour (on ? colour::ledOnRing : colour::ledRing);
        g.fillEllipse (circle (c, ringOut));
        g.setColour (on ? colour::ledOnGap : colour::ledGap);
        g.fillEllipse (circle (c, ringIn));
        const auto disc = circle (c, ri);
        if (! on && small)
            g.setGradientFill (vertical (colour::ledSmallTop, colour::ledSmallBottom, disc.getY(), disc.getBottom(), colour::ledSmallMid));
        else if (! on)
            g.setGradientFill (vertical (colour::ledInnerTop, colour::ledInnerBottom, disc.getY(), disc.getBottom()));
        else
            g.setGradientFill (vertical (green ? colour::ledAltCentre : colour::ledOnCentre,
                                         green ? colour::ledAltEdge : (small ? colour::ledSmallOnBottom : colour::ledOnBottom),
                                         c.y - ri * 0.3f, disc.getBottom()));
        g.fillEllipse (disc);
        if (! on && ! small)
            glint (g, c, ri - 0.8f, 1.4f, colour::ledInnerRim, 0.0f, 1.0f);
        if (on)
        {
            if (! small)
            {
                // a lighter rim all round
                g.setGradientFill (juce::ColourGradient ((green ? colour::ledAltEdge : colour::ledOnEdge).withAlpha (0.0f), c,
                                                         (green ? colour::ledAltEdge : colour::ledOnEdge).withAlpha (0.45f), c.translated (ri, 0.0f), true));
                g.fillEllipse (disc);
            }
            spot (g, c.translated (0.0f, -ri * 0.62f), ri * 0.6f, colour::ledOnTop);
        }
    }

    // The slider thumb: an 18 x 17 px ball with a dark rim, darkening
    // downwards, a light band inside the rim at its top, a softer light at its
    // foot, a 1 px coloured line down its middle, and a shadow on the ticks
    // below.
    void renderThumb (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour line, float hover, float alpha)
    {
        const Fade fade (g, alpha);
        const juce::Point<float> c (area.getX() + 9.5f, area.getY() + 8.5f);
        const auto ball = juce::Rectangle<float> (c.x - 9.18f, c.y - 9.1f, 18.36f, 18.2f);
        {
            // the shadow, on the rows below the ball only
            const juce::Graphics::ScopedSaveState below (g);
            g.reduceClipRegion (juce::Rectangle<float> (area.getX(), c.y + 7.5f, area.getWidth(), 8.0f).toNearestIntEdges());
            spot (g, c, 13.2f, juce::Colours::black.withAlpha (0.42f));
        }
        g.setGradientFill (vertical (colour::thumbTop, colour::thumbBottom, ball.getY(), ball.getBottom(), colour::thumbMid));
        g.fillEllipse (ball);
        spot (g, { c.x, ball.getBottom() - 2.4f }, 3.4f, colour::thumbFoot);
        juce::Path band;
        band.addCentredArc (c.x, c.y, 9.18f - 2.0f, 9.1f - 2.0f, 0.0f, -1.3f, 1.3f, true);
        juce::ColourGradient lit (colour::thumbLight, c.x, ball.getY(), colour::thumbLight.withAlpha (0.0f), c.x, c.y, false);
        g.setGradientFill (lit);
        g.strokePath (band, juce::PathStrokeType (1.4f));
        g.setColour (colour::thumbRim.brighter (0.25f * hover));
        g.drawEllipse (ball.reduced (0.85f), 1.7f);
        g.setColour (line);
        g.fillRect (juce::Rectangle<float> (c.x - 0.5f, c.y - 4.0f, 1.0f, 9.0f));
    }
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, float zeroPos, juce::Colour arc, bool enabled, float hover, bool showDot)
{
    const auto alpha = enabled ? 1.0f : 0.39f;   // FL (measured): a greyed knob is the knob at 39 % over the panel
    if (juce::jmin (area.getWidth(), area.getHeight()) > 50.0f)
        renderBigKnob (g, area, pos, arc, enabled ? hover : 0.0f, alpha);
    else
        renderKnob (g, area, pos, zeroPos, arc, enabled ? hover : 0.0f, alpha, showDot);
}

float hoverOf (const juce::Component& c)
{
    if (const auto* v = c.getProperties().getVarPointer ("hover"))
        return (float) (double) *v;
    return c.isMouseOverOrDragging() ? 1.0f : 0.0f;
}

float turnOf (const juce::Component& c)
{
    if (const auto* v = c.getProperties().getVarPointer ("turn"))
        return (float) (double) *v;
    return 0.0f;
}

void drawTurnArrow (juce::Graphics& g, juce::Point<float> rest, juce::Point<float> down, float turn, juce::Colour colour,
                    float baseH, float tipLen)
{
    // The right-pointing triangle (base at its left) rotated clockwise about
    // its box's centre by a quarter turn times turn, the centre moving from
    // rest to down. At 0 and 1 the corners are exactly the crisp arrows'.
    const auto t = juce::jlimit (0.0f, 1.0f, turn);
    const auto centre = rest + (down - rest) * t;
    const auto angle = t * juce::MathConstants<float>::halfPi;
    const auto c = std::cos (angle), s = std::sin (angle);
    auto corner = [&] (float x, float y) { return juce::Point<float> (centre.x + x * c - y * s, centre.y + x * s + y * c); };
    juce::Path p;
    p.addTriangle (corner (-tipLen * 0.5f, -baseH * 0.5f), corner (-tipLen * 0.5f, baseH * 0.5f), corner (tipLen * 0.5f, 0.0f));
    g.setColour (colour);
    g.fillPath (p);
}

float shownProportion (juce::Slider& s)
{
    if (const auto* v = s.getProperties().getVarPointer ("shown"))
        return (float) (double) *v;
    return (float) s.valueToProportionOfLength (s.getValue());
}

void drawLed (juce::Graphics& g, juce::Rectangle<float> area, float lit, bool enabled, bool green)
{
    // a switch fading: its lit look over its unlit one
    const auto alpha = enabled ? 1.0f : 0.45f;
    if (lit < 1.0f)
        renderLed (g, area, false, green, alpha);
    if (lit > 0.0f)
        renderLed (g, area, true, green, alpha * juce::jmin (1.0f, lit));
}

void drawSliderThumb (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour line, bool enabled, float hover)
{
    renderThumb (g, area, line, enabled ? hover : 0.0f, enabled ? 1.0f : 0.4f);
}

void drawButton (juce::Graphics& g, juce::Rectangle<int> area, bool lit, bool highlighted, bool down, bool enabled)
{
    // FL's push button (measured from Reset and Cut self): a 1 px dark frame
    // with rounded corners, a light inner edge along the top, inner edges down
    // the sides fading from that light to the fill's foot, a fill darkening
    // downwards, and a slightly lighter inner row at the bottom. Nothing
    // changes under the mouse; while pressed it darkens a little and the
    // label drops a pixel, both instantly (measured frame by frame).
    juce::ignoreUnused (highlighted);
    const auto alpha = enabled ? 1.0f : 0.45f;
    const auto b = area.toFloat();
    juce::ignoreUnused (lit);   // FL (measured): a toggled Cut self looks exactly like an untoggled one
    auto shade = [down] (juce::Colour c) { return down ? c.darker (0.15f) : c; };   // pressed: the frame goes 14 -> 10, the fill 1c -> 1a (measured)
    auto col = [&] (juce::Colour c) { g.setColour (shade (c).withMultipliedAlpha (alpha)); };
    col (colour::buttonFrame);
    g.fillRect (b.reduced (1.0f, 0.0f));
    g.fillRect (b.reduced (0.0f, 1.0f));
    col (colour::buttonCornerOuter);
    for (auto corner : { b.getTopLeft(), b.getTopRight().translated (-1.0f, 0.0f), b.getBottomLeft().translated (0.0f, -1.0f), b.getBottomRight().translated (-1.0f, -1.0f) })
        g.fillRect (corner.x, corner.y, 1.0f, 1.0f);
    col (colour::buttonCornerInner);
    for (auto corner : { b.getTopLeft().translated (1.0f, 0.0f), b.getTopLeft().translated (0.0f, 1.0f),
                         b.getTopRight().translated (-2.0f, 0.0f), b.getTopRight().translated (-1.0f, 1.0f),
                         b.getBottomLeft().translated (1.0f, -1.0f), b.getBottomLeft().translated (0.0f, -2.0f),
                         b.getBottomRight().translated (-2.0f, -1.0f), b.getBottomRight().translated (-1.0f, -2.0f) })
        g.fillRect (corner.x, corner.y, 1.0f, 1.0f);

    const auto inner = b.reduced (1.0f);
    const auto top = colour::buttonTop, bottom = colour::buttonBottom;
    g.setGradientFill (juce::ColourGradient (shade (top).withMultipliedAlpha (alpha), 0.0f, inner.getY() + 1.0f,
                                             shade (bottom).withMultipliedAlpha (alpha), 0.0f, inner.getBottom() - 1.0f, false));
    g.fillRect (inner.reduced (1.0f));
    col (colour::buttonEdgeTop);
    g.fillRect (inner.withHeight (1.0f));
    col (colour::buttonEdgeBottom);
    g.fillRect (inner.withTrimmedTop (inner.getHeight() - 1.0f));
    g.setGradientFill (juce::ColourGradient (shade (colour::buttonSideTop).withMultipliedAlpha (alpha), 0.0f, inner.getY() + 1.0f,
                                             shade (colour::buttonSideBottom).withMultipliedAlpha (alpha), 0.0f, inner.getBottom() - 1.0f, false));
    g.fillRect (inner.getX(), inner.getY() + 1.0f, 1.0f, inner.getHeight() - 2.0f);
    g.fillRect (inner.getRight() - 1.0f, inner.getY() + 1.0f, 1.0f, inner.getHeight() - 2.0f);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, colour::menuBg);
    setColour (juce::PopupMenu::textColourId, colour::menuText);
    setColour (juce::PopupMenu::headerTextColourId, colour::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2e2e2e));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::textColourId, colour::comboText);
    setColour (juce::Label::textColourId, colour::text);
    setColour (juce::TextButton::textColourOffId, colour::buttonText);
    setColour (juce::TextButton::textColourOnId, colour::arc);
    setColour (juce::TooltipWindow::textColourId, juce::Colour (0xffebebeb));   // also the knob value popup (menu text)
    setColour (juce::Slider::rotarySliderFillColourId, colour::arc);
    setColour (juce::TextEditor::highlightColourId, colour::arc.withAlpha (0.4f));
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
   #if JUCE_WINDOWS
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
    {
        auto copy = f;
        copy.setTypefaceName ("Segoe UI");
        return juce::Typeface::createSystemTypefaceFor (copy);
    }
   #endif
    return juce::LookAndFeel_V4::getTypefaceForFont (f);   // elsewhere ui::font sizes the system font
}

namespace
{
    bool isDropDown (const juce::PopupMenu::Options& o)
    {
        return dynamic_cast<juce::ComboBox*> (o.getTargetComponent()) != nullptr;
    }

    // Menus opened with showContextMenu (FL's sample / preset menus)
    bool isContext (const juce::PopupMenu::Options& o)
    {
        auto* t = o.getTargetComponent();
        return t != nullptr && (bool) t->getProperties()[contextMenuProperty];
    }

    // FL menu metrics (measured from FL screenshots at 100%).
    constexpr int menuRowH = 19, menuHeaderH = 13, knobHeaderH = 16;
    constexpr float menuEm = 10.8f, menuHeaderEm = 8.55f;
    const juce::Colour ddBg { 0xff0e0e0e }, ddEdge { 0xff000000 }, ddHeader { 0xff222222 }, ddHeaderText { 0xff8c8c8c },
                       ddText { 0xffffffff }, ddHighlight { 0xff383838 };
    const juce::Colour rmBg { 0xff141414 }, rmText { 0xffebebeb }, rmHighlight { 0xff2e2e2e }, rmDisabled { 0xff6c6c6c },
                       rmHeader { 0xff222222 }, rmHeaderText { 0xff8c8c8c }, rmSeparator { 0xff262626 };

    // "&Copy value" -> text without '&' and the index of the underlined letter.
    juce::String stripAccelerator (const juce::String& t, int& index)
    {
        index = t.indexOfChar ('&');
        return index < 0 ? t : t.substring (0, index) + t.substring (index + 1);
    }

    // The underlined letters of the menu about to open (armMenuLetters), handed
    // to its window when JUCE prepares it; the window owns them from then on.
    struct MenuLetters final : juce::KeyListener, juce::ComponentListener
    {
        std::vector<std::pair<juce::juce_wchar, std::function<void()>>> entries;

        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            const auto mods = key.getModifiers();
            if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
                return false;
            const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
            for (auto& [letter, action] : entries)
                if (letter == c)
                {
                    auto chosen = action;
                    juce::PopupMenu::dismissAllActiveMenus();
                    juce::MessageManager::callAsync (std::move (chosen));
                    return true;
                }
            return false;
        }

        void componentBeingDeleted (juce::Component& c) override
        {
            c.removeKeyListener (this);
            c.removeComponentListener (this);
            delete this;
        }
    };
    std::unique_ptr<MenuLetters> armedLetters;
}

void armMenuLetters (const juce::PopupMenu& menu)
{
    auto letters = std::make_unique<MenuLetters>();
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();
        int acc = -1;
        const auto text = stripAccelerator (item.text, acc);
        if (acc >= 0 && acc < text.length() && item.isEnabled && item.action != nullptr && item.subMenu == nullptr)
            letters->entries.emplace_back (juce::CharacterFunctions::toLowerCase (text[acc]), item.action);
    }
    armedLetters = letters->entries.empty() ? nullptr : std::move (letters);
}

juce::String commandShortcutText (char key)
{
   #if JUCE_MAC
    const juce::String prefix ("Cmd+");
   #else
    const juce::String prefix ("Ctrl+");
   #endif
    return prefix + juce::String::charToString (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key));
}

void LookAndFeel::preparePopupMenuWindow (juce::Component& window)
{
    if (armedLetters == nullptr)
        return;
    auto* letters = armedLetters.release();   // deleted with the window
    window.addKeyListener (letters);
    window.addComponentListener (letters);
}

void LookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int w, int h, const juce::PopupMenu::Options& o)
{
    g.fillAll (isDropDown (o) ? ddBg : rmBg);
    g.setColour (isDropDown (o) ? ddEdge : juce::Colours::black);
    g.drawRect (0, 0, w, h);
}

void LookAndFeel::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                         const juce::String& name, const juce::PopupMenu::Options& o)
{
    g.setColour (isDropDown (o) ? ddHeader : rmHeader);
    g.fillRect (area);
    g.setColour (isDropDown (o) ? ddHeaderText : rmHeaderText);
    g.setFont (font (menuHeaderEm));
    g.drawText (name, area.withTrimmedLeft (4), juce::Justification::centredLeft, true);
}

void LookAndFeel::getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int, int& w, int& h,
                                                                 const juce::PopupMenu::Options& o)
{
    w = (int) juce::GlyphArrangement::getStringWidth (font (menuHeaderEm), text) + 10;
    h = isDropDown (o) ? menuHeaderH : knobHeaderH;
}

void LookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int, int& w, int& h,
                                                        const juce::PopupMenu::Options& o)
{
    if (isSeparator)
    {
        w = 20;
        h = isContext (o) ? 7 : 5;
        return;
    }
    int acc = 0;
    const auto t = stripAccelerator (text, acc);
    w = (int) juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), t) + (isDropDown (o) ? 12 : 34);
    h = menuRowH;
}

juce::PopupMenu::Options LookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label&)
{
    // FL opens its lists straight below the box, left-aligned, in columns.
    return juce::PopupMenu::Options().withTargetComponent (&box)
                                     .withMinimumWidth (box.getWidth())
                                     .withMaximumNumColumns (2)
                                     .withStandardItemHeight (19)
                                     .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::downwards);
}

// FL's knob menus divide their columns with a 2 px black bar.
int LookAndFeel::getPopupMenuColumnSeparatorWidthWithOptions (const juce::PopupMenu::Options& o)
{
    return isDropDown (o) ? 1 : 2;
}

void LookAndFeel::drawPopupMenuColumnSeparatorWithOptions (juce::Graphics& g, const juce::Rectangle<int>& r, const juce::PopupMenu::Options& o)
{
    g.setColour (isDropDown (o) ? ddEdge : juce::Colours::black);
    g.fillRect (r);
}

void LookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool isHighlighted,
                                                const juce::PopupMenu::Item& item, const juce::PopupMenu::Options& o)
{
    const bool dd = isDropDown (o);
    if (item.isSeparator)
    {
        // FL: a full-width dark rule (2 px in the knob menus, 1 px in the
        // sample menu)
        g.setColour (dd ? juce::Colours::black.withAlpha (0.4f) : rmHeader);
        if (dd)                  g.fillRect (area.withHeight (1).withY (area.getCentreY()).withTrimmedLeft (2).withTrimmedRight (2));
        else if (isContext (o))  g.fillRect (area.withHeight (1).withY (area.getY() + 3));
        else                     g.fillRect (area.withHeight (2).withY (area.getCentreY() - 1));
        return;
    }
    // FL's dropdowns keep the current choice highlighted.
    const bool hl = (isHighlighted || (dd && item.isTicked)) && item.isEnabled;
    if (hl)
    {
        g.setColour (dd ? ddHighlight : rmHighlight);
        g.fillRect (area.reduced (1, 0));
    }

    int acc = -1;
    const auto text = stripAccelerator (item.text, acc);
    auto col = dd ? ddText : rmText;
    if (! item.isEnabled) col = rmDisabled;
    if (hl && dd)         col = juce::Colours::white;
    g.setColour (col);
    if (item.isTicked && ! dd)
    {
        // FL's tick in the left margin (measured: 9 x 7 px, 1.4 px strokes)
        const auto x = (float) area.getX(), y = (float) area.getY();
        juce::Path tick;
        tick.startNewSubPath (x + 2.5f, y + 7.0f);
        tick.lineTo (x + 5.5f, y + 10.5f);
        tick.lineTo (x + 10.5f, y + 4.0f);
        g.strokePath (tick, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    }
    const auto f = getPopupMenuFont();
    g.setFont (f);
    const auto textArea = area.withTrimmedLeft (dd ? 4 : 16).withTrimmedRight (dd ? 2 : 12);
    g.drawText (text, textArea, juce::Justification::centredLeft, true);

    // FL's shortcut text: right-aligned, italic, a little dimmer (FL capture: #A0A5A4)
    if (item.shortcutKeyDescription.isNotEmpty() && ! dd)
    {
        g.setColour (item.isEnabled ? juce::Colour (0xffa4a4a4) : rmDisabled);
        g.setFont (f.italicised());
        g.drawText (item.shortcutKeyDescription, area.withTrimmedRight (14), juce::Justification::centredRight, false);
        g.setFont (f);
        g.setColour (col);
    }

    // FL underlines the shortcut letter of its knob menu entries with a dotted line.
    if (acc >= 0 && acc < text.length())
    {
        const auto x0 = juce::roundToInt ((float) textArea.getX() + juce::GlyphArrangement::getStringWidth (f, text.substring (0, acc)));
        const auto x1 = juce::roundToInt ((float) textArea.getX() + juce::GlyphArrangement::getStringWidth (f, text.substring (0, acc + 1)));
        const auto y = juce::roundToInt ((float) area.getCentreY() + f.getAscent() * 0.55f);
        for (int x = x0; x < x1; x += 2)
            g.fillRect (x, y, 1, 1);
    }

    if (item.subMenu != nullptr)
    {
        // FL: a thin chevron
        juce::Path chevron;
        const auto ax = (float) area.getRight() - 9.0f, ay = (float) area.getCentreY();
        chevron.startNewSubPath (ax - 2.0f, ay - 4.0f);
        chevron.lineTo (ax + 2.0f, ay);
        chevron.lineTo (ax - 2.0f, ay + 4.0f);
        g.strokePath (chevron, juce::PathStrokeType (1.2f));
    }
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& slider)
{
    // Value arc as in FL: from the minimum for one-sided knobs, from the
    // centre for two-sided ones (pitch, EQ, panning...).
    // (the default value is kept in the slider's double-click value, though a
    // double-click does nothing, as in FL)
    // Two-sided: a range symmetric about zero (the tensions, whose default
    // need not be zero: the volume release's is -79 %, its arc running from
    // the top as in FL), or a default in the middle (MUL).
    const bool bipolar = std::abs (slider.getMinimum() + slider.getMaximum()) <= 1.0e-6 * std::abs (slider.getMaximum())
                      || std::abs (slider.valueToProportionOfLength (slider.getDoubleClickReturnValue()) - 0.5) < 1.0e-3;
    const auto d = juce::jmin (w, h);
    const auto area = juce::Rectangle<float> ((float) d, (float) d).withCentre (juce::Rectangle<int> (x, y, w, h).toFloat().getCentre());
    juce::ignoreUnused (pos);
    drawKnob (g, area, shownProportion (slider), bipolar ? 0.5f : 0.0f, slider.findColour (juce::Slider::rotarySliderFillColourId), slider.isEnabled(), hoverOf (slider),
              ! (bool) slider.getProperties().getWithDefault ("noDot", false));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float, float,
                                    float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    // FL's horizontal slider (fine tune, tracking Mid), measured: a 4 px
    // rounded groove 4.5 px in from the control's ends with a 2 px floor 3 px
    // inside it, a fill from the default position to the thumb (orange for a
    // one-sided range, red from the middle for a two-sided one), the ball
    // thumb, and a strip of 1 px ticks 10 px below the groove, every 6 px
    // (one-sided) or 12 px (two-sided), on a slightly darker band that stops
    // a pixel short of each tick. The control is 22 px tall: the thumb's 17
    // rows at its top, the groove at rows 7 to 10, the ticks at 17 to 21.
    // Under the mouse (anywhere on the control, measured frame by frame) the
    // groove grows a pixel up and down, the band's top row goes and the ball's
    // rims lift; pressing and dragging add nothing to that.
    const auto alpha = slider.isEnabled() ? 1.0f : 0.4f;
    const auto hover = slider.isEnabled() ? hoverOf (slider) : 0.0f;
    const auto top = (float) y + std::floor ((float) (h - 22) * 0.5f);
    const auto trackTop = top + 7.0f;
    const auto outer = juce::Rectangle<float> ((float) x + 4.5f, trackTop - hover, (float) w - 9.0f, 4.0f + 2.0f * hover);
    const auto inner = outer.reduced (3.0f, 1.0f);
    const bool bipolar = std::abs (slider.getMinimum() + slider.getMaximum()) < 1.0e-6 * std::abs (slider.getMaximum());
    const auto line = bipolar ? colour::thumbFine : colour::arc;
    const auto pos = shownProportion (slider);
    // the thumb's line (1 px) and the ball centred on it
    const auto lineX = std::floor (outer.getX() + 9.5f + pos * (outer.getWidth() - 19.0f));

    g.setColour (colour::sliderTrack.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (outer, 2.0f);
    g.setColour (colour::sliderGroove.withMultipliedAlpha (alpha));
    g.fillRect (inner);
    const auto defX = bipolar ? std::floor (outer.getCentreX()) : inner.getX();
    if (std::abs (lineX + 0.5f - defX) > 1.0f)
    {
        g.setColour (line.withMultipliedAlpha (alpha));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (defX, lineX + 0.5f), inner.getY(), juce::jmax (defX, lineX + 0.5f), inner.getBottom()));
    }

    // the tick strip
    const int divisions = bipolar ? 8 : 10;
    const auto bandY = trackTop + 10.0f;
    const auto firstTick = std::floor (outer.getX() + 3.5f), step = (outer.getWidth() - 8.0f) / (float) divisions;
    g.setColour (colour::sliderBandTop.withMultipliedAlpha (alpha * (1.0f - hover)));
    g.fillRect (firstTick + 1.0f, bandY, step * (float) divisions - 1.0f, 1.0f);
    g.setColour (colour::sliderBand.withMultipliedAlpha (alpha));
    g.fillRect (firstTick + 1.0f, bandY + 1.0f, step * (float) divisions - 1.0f, 4.0f);
    for (int i = 0; i <= divisions; ++i)
    {
        const auto tx = std::floor (firstTick + step * (float) i);
        g.setColour (colour::panel.withMultipliedAlpha (alpha));
        g.fillRect (tx - 1.0f, bandY, 3.0f, 5.0f);
        g.setColour (colour::sliderTickTop.withMultipliedAlpha (alpha));
        g.fillRect (tx, bandY, 1.0f, 1.0f);
        g.setColour (colour::sliderTick.withMultipliedAlpha (alpha));
        g.fillRect (tx, bandY + 1.0f, 1.0f, 4.0f);
    }

    // the ball (19 x 17 px) with its shadow rows below it
    drawSliderThumb (g, { lineX - 9.0f, top, 19.0f, 23.0f }, line, slider.isEnabled(), hover);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    const auto bounds = b.getLocalBounds().toFloat();
    // the light fades when the button is one of FL's switches (Switch)
    const auto* sw = dynamic_cast<const Switch*> (&b);
    const float lit = sw != nullptr ? sw->getLit() : (b.getToggleState() ? 1.0f : 0.0f);
    // "captionStyle": FL's small switch with a knob-caption label (CLIP in the
    // effects view, measured): an 11 px light 5 px in and 2 px down, the 7 px
    // bold caption 21 px in with its baseline 9 px down.
    if ((bool) b.getProperties()["captionStyle"])
    {
        drawLed (g, { bounds.getX() + 5.0f, bounds.getY() + 2.0f, 11.0f, 11.0f }, lit, b.isEnabled(), (bool) b.getProperties()["greenLed"]);
        g.setColour (b.isEnabled() ? colour::caption : colour::dim);
        juce::GlyphArrangement ga;
        ga.addLineOfText (font (7.0f, true), b.getButtonText(), bounds.getX() + 21.0f, bounds.getY() + 9.0f);
        ga.draw (g);
        return;
    }
    // FL's switch: a 15 px light at 4 px from the left, the label 19 px in.
    const auto ledSize = juce::jmin (bounds.getHeight() - 2.0f, 15.0f);
    // "textFirst": FL's label-then-LED switches (envelope / LFO Tempo, Global)
    const bool textFirst = (bool) b.getProperties()["textFirst"];
    const auto f = font (11.1f);
    const auto textW = juce::GlyphArrangement::getStringWidth (f, b.getButtonText());
    // "ledRight": the light at the right edge whatever the label's width
    const auto ledX = ! textFirst ? bounds.getX() + 4.0f
                    : (bool) b.getProperties()["ledRight"] ? bounds.getRight() - ledSize - 2.0f
                    : juce::jmin (bounds.getRight() - ledSize - 2.0f, bounds.getX() + textW + 4.0f);
    const auto led = juce::Rectangle<float> (ledSize, ledSize).withPosition (ledX, std::floor (bounds.getCentreY() - ledSize * 0.5f) + 1.0f);
    drawLed (g, led, lit, b.isEnabled(), (bool) b.getProperties()["greenLed"]);

    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (b.isEnabled() ? colour::text : colour::dim);
        g.setFont (f);
        if (textFirst)
        {
            // FL ends the label 4 px before the light and never cuts it short
            // (Ping pong / Fat mode in the echo panel are wider than their space)
            const auto right = led.getX() - 4.0f;
            const auto textArea = bounds.withRight (right).withLeft (juce::jmin (bounds.getX(), right - textW - 2.0f));
            g.drawText (b.getButtonText(), textArea, juce::Justification::centredRight, false);
        }
        else
        {
            g.drawText (b.getButtonText(), bounds.withTrimmedLeft (ledSize + 7.0f), juce::Justification::centredLeft, false);
        }
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    // FL's drop-down field: a 1 px dark frame, a lighter inner edge along the
    // top and sides, a fill darkening downwards, and a small right arrow.
    const auto alpha = box.isEnabled() ? 1.0f : 0.45f;
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    g.setColour (colour::comboEdge.withMultipliedAlpha (alpha));
    g.fillRect (r);
    g.setColour (colour::comboInnerSide.withMultipliedAlpha (alpha));
    g.fillRect (r.reduced (1.0f));
    g.setColour (colour::comboInnerTop.withMultipliedAlpha (alpha));
    g.fillRect (r.reduced (1.0f).withHeight (1.0f));
    auto inner = r.reduced (2.0f, 1.0f).withTrimmedTop (1.0f);
    g.setGradientFill (juce::ColourGradient (colour::comboTop.withMultipliedAlpha (alpha), 0.0f, inner.getY(),
                                             colour::comboBottom.withMultipliedAlpha (alpha), 0.0f, inner.getBottom(), false));
    g.fillRect (inner.withTrimmedBottom (1.0f));
    g.setColour (colour::comboInnerBottom.withMultipliedAlpha (alpha));
    g.fillRect (inner.removeFromBottom (1.0f));

    // The arrow: a 4 x 7 px triangle whose base is 9 px from the field's right
    // edge, on the middle row; it turns to point down (7 x 4 px, a pixel lower)
    // and brightens under the mouse and while the list is open (measured on
    // all four lists).
    const auto turn = turnOf (box);   // held turned while the list is open
    const auto mid = std::floor ((float) h * 0.5f);
    drawTurnArrow (g, { (float) w - 7.0f, mid + 0.5f }, { (float) w - 7.5f, mid + 1.0f }, turn,
                   colour::comboArrow.interpolatedWith (colour::icon, turn).withMultipliedAlpha (alpha), 7.2f, 4.0f);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jmin (11.7f, (float) box.getHeight() * 0.55f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (2, 0, box.getWidth() - 16, box.getHeight() - 2);
    label.setBorderSize ({ 0, 1, 0, 0 });
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    drawButton (g, b.getLocalBounds(), b.getToggleState(), highlighted, down, b.isEnabled());
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                  .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedBottom (1).translated (0, down ? 1 : 0), juce::Justification::centred, true);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int buttonHeight)
{
    // FL's Reset is set in 11 px; its taller Cut self button in 12.4 px (measured)
    const auto em = (bool) b.getProperties()["bigText"] ? 12.0f : 11.0f;
    return font (juce::jmin (em, (float) buttonHeight * 0.55f));
}

void LookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // "captionBaseline": FL's knob captions sit on the pixel grid, their
    // baseline on the label's last row; drawn from a whole-pixel start so the
    // bold caps come out as crisp as FL's.
    if (! (bool) label.getProperties()["captionBaseline"])
    {
        LookAndFeel_V4::drawLabel (g, label);
        return;
    }
    const auto f = label.getFont();
    const auto area = label.getLocalBounds().toFloat();
    const auto text = label.getText();
    // "captionTracking": extra space between the letters (FL spaces its
    // TENSION captions out, measured: 37 px of ink against 32 set solid)
    const auto tracking = (float) (double) label.getProperties().getWithDefault ("captionTracking", 0.0);
    const auto textW = juce::GlyphArrangement::getStringWidth (f, text) + tracking * (float) juce::jmax (0, text.length() - 1);
    auto x = std::round (area.getCentreX() - textW * 0.5f);
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    juce::GlyphArrangement ga;
    if (tracking == 0.0f)
        ga.addLineOfText (f, text, x, area.getBottom());
    else
        for (int i = 0; i < text.length(); ++i)
        {
            const auto letter = text.substring (i, i + 1);
            ga.addLineOfText (f, letter, x, area.getBottom());
            x += juce::GlyphArrangement::getStringWidth (f, letter) + tracking;
        }
    ga.draw (g);
}

void LookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent& bubble, const juce::Point<float>&, const juce::Rectangle<float>& body)
{
    // Same look as the right-click menus: flat grey box, 1 px black edge.
    juce::ignoreUnused (bubble);
    g.setColour (rmBg);
    g.fillRect (body);
    g.setColour (juce::Colours::black);
    g.drawRect (body, 1.0f);
}

juce::Font LookAndFeel::getSliderPopupFont (juce::Slider&)
{
    return getPopupMenuFont();
}

int LookAndFeel::getSliderPopupPlacement (juce::Slider&)
{
    return juce::BubbleComponent::above;
}

juce::Font LookAndFeel::getPopupMenuFont()
{
    return font (menuEm);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (colour::well);
    g.setColour (colour::panelEdge);
    g.drawRect (0, 0, w, h);
    g.setColour (colour::text);
    g.setFont (font (11.7f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 3);
}

//==============================================================================
Panel::Panel (const juce::String& t, int inset) : title (t), headerInset (inset)
{
}

void MenuArrow::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::ignoreUnused (highlighted);
    const auto t = down ? 1.0f : turnOf (*this);
    const auto b = getLocalBounds().toFloat();
    if (! boxed)
    {
        // the echo panel's: a light 3.4 x 8 px triangle that turns down under
        // the mouse (its base at x 326 and rows 167 to 174 of the canvas, measured)
        const auto c = b.getCentre().translated (-0.3f, -0.5f);
        drawTurnArrow (g, c, c, t, colour::icon, 8.0f, 3.4f);
        return;
    }
    // the envelope's: at rest a light triangle at the right of its 20 x 20 px
    // cell; under the mouse an amber frame (2 px, the inner pixel dimmer,
    // radius 5) grows in while the triangle turns down, moves to the middle
    // and turns amber
    if (t > 0.0f)
    {
        const auto frame = b.withSizeKeepingCentre (20.0f, 20.0f);
        g.setColour (colour::arc.withAlpha (t));
        g.drawRoundedRectangle (frame.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (colour::arc.darker (0.35f).withAlpha (t));
        g.drawRoundedRectangle (frame.reduced (1.5f), 4.0f, 1.0f);
    }
    // at rest the 4.4 x 8 px triangle sits a pixel right of the cell's middle;
    // turned down (8 x 4.4 px) it is centred, a pixel lower (measured)
    const auto cell = b.withSizeKeepingCentre (20.0f, 20.0f);
    drawTurnArrow (g, { cell.getX() + 11.0f, cell.getY() + 10.0f }, { cell.getX() + 10.0f, cell.getY() + 11.2f }, t,
                   colour::icon.interpolatedWith (colour::arc, t), 8.0f, 4.4f);
}

void Panel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colour::panel);
    g.fillRect (r);
    // FL outlines the body on its left, right and bottom only.
    g.setColour (colour::panelEdge);
    g.fillRect (r.withWidth (1.0f));
    g.fillRect (r.withTrimmedLeft (r.getWidth() - 1.0f));
    g.fillRect (r.withTrimmedTop (r.getHeight() - 1.0f));
    g.fillRect (r.withHeight (1.0f));

    if (title.isEmpty() && headerInset == 0)
        return;

    // Header as in FL (pixels): the title sits on a tab a shade darker than
    // the body, 20 rows with a dark line under it; its right end reaches 11 px
    // past the text at the top, cut by a shallow bevel over the last 17 px of
    // the top row, and leans back 10 px over its height. Right of the tab the
    // panel's dark top edge shows.
    const auto h = (float) headerH;
    const auto f = font (11.7f);
    const auto titleX = (float) headerInset + (title.isEmpty() ? 0.0f : 4.0f);
    const auto tabEnd = std::floor (titleX + juce::GlyphArrangement::getStringWidth (f, title)) + 11.0f;

    g.setColour (colour::panelEdge);
    g.fillRect (0.0f, 0.0f, r.getWidth(), 1.0f);
    juce::Path tab;
    tab.startNewSubPath (0.0f, 0.0f);
    tab.lineTo (tabEnd - 17.0f, 0.0f);
    tab.lineTo (tabEnd, 1.0f);
    tab.lineTo (tabEnd - 10.0f, h);
    tab.lineTo (0.0f, h);
    tab.closeSubPath();
    g.setColour (colour::panelTab);
    g.fillPath (tab);
    g.setColour (colour::panelEdge);
    g.fillRect (0.0f, h, tabEnd - 10.0f, 1.0f);

    g.setColour (colour::title);
    g.setFont (f);
    g.drawText (title, juce::Rectangle<float> (titleX, 0.0f, tabEnd - titleX, h), juce::Justification::centredLeft, false);
}

//==============================================================================
DigiBox::DigiBox (bool redStyle) : red (redStyle)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setMouseDragSensitivity (120);
}

void DigiBox::paint (juce::Graphics& g)
{
    // The digi wheel: a grey box with rounded corners (radius 4.7), lit from
    // its left middle and darker towards the right; dark up / down triangles
    // 5 px from the right; the number in a large light font, or three short
    // dashes when the box is empty. Under the mouse the box lightens a little
    // and the triangles go.
    const auto alpha = isEnabled() ? 1.0f : 0.5f;
    const auto b = getLocalBounds().toFloat();
    const auto box = juce::Rectangle<float> (b.getX() + 2.81f, b.getY() + 0.67f, b.getWidth() - 3.58f, b.getHeight() - 1.34f);
    const auto hoverAmount = isEnabled() ? juce::jmax (hoverOf (*this), isMouseButtonDown() ? 1.0f : 0.0f) : 0.0f;
    const bool lit = hoverAmount > 0.5f;   // the triangles go once the fade is half way
    {
        const Fade fade (g, alpha);
        if (red)
            g.setGradientFill (vertical (colour::digiRed.brighter (0.15f), colour::digiRed.darker (0.25f), box.getY(), box.getBottom()));
        else
            g.setGradientFill (juce::ColourGradient (colour::digiTop, box.getX(), box.getCentreY(), colour::digiBottom, box.getRight(), box.getCentreY(), true));
        g.fillRoundedRectangle (box, 4.7f);
        if (hoverAmount > 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.047f * hoverAmount));
            g.fillRoundedRectangle (box, 4.7f);
        }
    }

    // the triangles: 1, 3 and 5 px rows, 6 rows apart, 5 px in from the right
    g.setColour (colour::digiArrow.withMultipliedAlpha (alpha));
    const auto ax = std::floor (box.getRight()) - 6.0f, ay = std::round (box.getY());
    for (int i = 0; i < (lit ? 0 : 3); ++i)
    {
        const auto wdt = 1.0f + 2.0f * (float) i;
        g.setColour (colour::digiArrow.withMultipliedAlpha (alpha * (i == 0 ? 0.9f : 1.0f)));
        g.fillRect (ax - (float) i, ay + 6.0f + (float) i, wdt, 1.0f);
        g.fillRect (ax - (float) i, ay + 14.0f - (float) i, wdt, 1.0f);
    }

    const auto text = fixedText.isNotEmpty() ? fixedText : getTextFromValue (getValue());
    if (text.containsOnly ("-") && text.isNotEmpty())
    {
        // three 1.5 px dashes 3 px apart on the middle row, starting 10.5 px in
        const auto x0 = std::round (box.getX()) + 9.0f, cy = ay + 10.0f;
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (colour::digiNone.withMultipliedAlpha (alpha * 0.55f));
            g.fillRect (x0 + 3.0f * (float) i, cy, 1.0f, 1.0f);
            g.setColour (colour::digiNone.withMultipliedAlpha (alpha));
            g.fillRect (x0 + 3.0f * (float) i + 1.0f, cy, 1.0f, 1.0f);
        }
        return;
    }
    g.setColour (colour::digiText.withMultipliedAlpha (alpha));
    g.setFont (font (18.0f));
    g.drawText (text, box.withTrimmedRight (8.0f).withTrimmedBottom (3.0f), juce::Justification::centred, false);   // centred between the box's left edge and the triangles
}

//==============================================================================
namespace
{
    // Each icon's box inside its cell, and its size (measured): the sheet
    // tabs' 44 x 45 px cells, the effects view's 26 x 18 px cells (which get
    // the small wrench), the LFO's 23 x 19 px cells, the arpeggiator's 19 px cells.
    struct IconPlace { float dx, dy, w, h; };

    IconPlace iconPlace (Icon icon, bool smallCell)
    {
        switch (icon)
        {
            case Icon::sampler:   return { 8, 12, 28, 21 };
            case Icon::envelope:  return { 10, 10, 25, 24 };
            case Icon::wrench:    return smallCell ? IconPlace { 5, 3, 14, 14 } : IconPlace { 12, 13, 19, 20 };
            case Icon::valve:     return { 7, 3, 7, 14 };   // its cap on the canvas's rows 141-143 (measured)
            case Icon::sine:      return { 6, 5, 13, 11 };
            case Icon::triangle:  return { 6, 5, 13, 11 };
            case Icon::square:    return { 7, 6, 21, 9 };
            case Icon::arpOff:    return { 6, 5, 9, 9 };
            case Icon::arpUp:     return { 6, 7, 9, 5 };
            case Icon::arpDown:   return { 6, 7, 9, 5 };
            case Icon::arpUpDown: return { 7, 5, 7, 9 };
            case Icon::arpSticky: return { 6, 3, 9, 13 };
            case Icon::arpRandom: return { 6, 4, 9, 11 };
            case Icon::link:      return { 0, 0, 9, 9 };
            default:              return { 0, 0, 0, 0 };   // file buttons: IconButton draws them
        }
    }

    juce::Path triangle (float x0, float y0, float x1, float y1, float x2, float y2)
    {
        juce::Path p;
        p.addTriangle (x0, y0, x1, y1, x2, y2);
        return p;
    }
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> cell, juce::Colour c)
{
    // Our own glyphs: lines, arcs, bars and triangles drawn inside each icon's
    // measured box, so they sit where the layout wants them and scale cleanly.
    const auto place = iconPlace (icon, cell.getHeight() < 24.0f);
    if (place.w <= 0.0f)
        return;
    const auto at = juce::AffineTransform::translation (std::floor (cell.getX()) + place.dx, std::floor (cell.getY()) + place.dy);
    g.setColour (c);
    auto stroke = [&] (const juce::Path& p, float width, const juce::AffineTransform& t, bool sharp = false)
    {
        g.strokePath (p, juce::PathStrokeType (width, sharp ? juce::PathStrokeType::mitered : juce::PathStrokeType::curved,
                                               sharp ? juce::PathStrokeType::butt : juce::PathStrokeType::rounded), t);
    };
    auto dot = [&] (float x, float y, float r) { g.fillEllipse (at.getTranslationX() + x - r, at.getTranslationY() + y - r, 2.0f * r, 2.0f * r); };
    juce::Path p;
    switch (icon)
    {
        case Icon::sampler:
        {
            // a sound wave: seven rounded bars, the tallest in the middle
            const float heights[] = { 5.0f, 10.0f, 15.0f, 21.0f, 15.0f, 10.0f, 5.0f };
            for (int i = 0; i < 7; ++i)
                p.addRoundedRectangle (0.5f + 4.0f * (float) i, (21.0f - heights[i]) * 0.5f, 2.5f, heights[i], 1.25f);
            g.fillPath (p, at);
            break;
        }
        case Icon::envelope:
        {
            // an envelope: rings on its start, peak and end, a straight rise and an
            // exponential fall that levels out, each line stopping short of the rings
            const juce::Point<float> start (2.9f, 20.9f), peak (8.5f, 3.0f), end (22.1f, 20.9f);
            constexpr float ring = 2.4f, clear = ring + 1.0f;
            auto outside = [&] (juce::Point<float> q)
            {
                return q.getDistanceFrom (start) > clear && q.getDistanceFrom (peak) > clear && q.getDistanceFrom (end) > clear;
            };
            const auto rise = (peak - start) / start.getDistanceFrom (peak);
            p.startNewSubPath (start + rise * clear);
            p.lineTo (peak - rise * clear);
            bool open = false;
            for (int i = 0; i <= 80; ++i)
            {
                const auto u = (float) i / 80.0f;
                const juce::Point<float> q (peak.x + (end.x - peak.x) * u,
                                            peak.y + (end.y - peak.y) * (1.0f - std::exp (-4.0f * u)) / (1.0f - std::exp (-4.0f)));
                if (! outside (q))
                    continue;
                if (open) p.lineTo (q); else p.startNewSubPath (q);
                open = true;
            }
            stroke (p, 1.4f, at);
            juce::Path rings;
            for (const auto centre : { start, peak, end })
                rings.addEllipse (centre.x - ring, centre.y - ring, 2.0f * ring, 2.0f * ring);
            stroke (rings, 1.2f, at);
            break;
        }
        case Icon::wrench:
        {
            // a spanner: an open jaw at the top right, the handle down to the bottom left (drawn at 19 x 20, scaled)
            const auto t = juce::AffineTransform::scale (place.w / 19.0f, place.h / 20.0f).followedBy (at);
            const auto jaw = juce::MathConstants<float>::pi * 0.25f;
            p.addCentredArc (13.3f, 5.7f, 3.8f, 3.8f, 0.0f, jaw + 0.95f, jaw + juce::MathConstants<float>::twoPi - 0.95f, true);
            stroke (p, 2.6f, t);
            juce::Path handle;
            handle.startNewSubPath (10.8f, 8.3f);
            handle.lineTo (3.2f, 16.6f);
            stroke (handle, 3.4f, t);
            break;
        }
        case Icon::valve:
        {
            // a valve: a glass with a rounded top, two filaments, a base and a pin
            p.addRoundedRectangle (0.6f, 0.6f, 5.8f, 9.6f, 2.9f);
            stroke (p, 1.0f, at);
            juce::Path filament;
            filament.startNewSubPath (2.4f, 4.5f);
            filament.lineTo (2.4f, 8.6f);
            filament.startNewSubPath (4.6f, 4.5f);
            filament.lineTo (4.6f, 8.6f);
            stroke (filament, 0.9f, at, true);
            juce::Path base;
            base.addRectangle (0.8f, 10.6f, 5.4f, 1.4f);
            base.addTriangle (2.3f, 12.0f, 4.7f, 12.0f, 3.5f, 13.6f);
            g.fillPath (base, at);
            break;
        }
        case Icon::sine:
            for (int i = 0; i <= 48; ++i)
            {
                const auto u = (float) i / 48.0f;
                const juce::Point<float> pt (0.8f + 11.4f * u, 5.5f - 4.2f * std::sin (juce::MathConstants<float>::twoPi * u));
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            stroke (p, 1.5f, at);
            break;
        case Icon::triangle:
            p.startNewSubPath (0.8f, 5.5f);
            p.lineTo (3.65f, 1.3f);
            p.lineTo (9.35f, 9.7f);
            p.lineTo (12.2f, 5.5f);
            stroke (p, 1.5f, at);
            break;
        case Icon::square:
            p.startNewSubPath (0.8f, 8.2f);
            p.lineTo (0.8f, 0.8f);
            p.lineTo (10.5f, 0.8f);
            p.lineTo (10.5f, 8.2f);
            p.lineTo (20.2f, 8.2f);
            p.lineTo (20.2f, 0.8f);
            stroke (p, 1.4f, at, true);
            break;
        case Icon::arpOff:
            p.startNewSubPath (1.0f, 1.0f);
            p.lineTo (8.0f, 8.0f);
            p.startNewSubPath (8.0f, 1.0f);
            p.lineTo (1.0f, 8.0f);
            stroke (p, 1.7f, at, true);
            break;
        case Icon::arpUp:     g.fillPath (triangle (0.0f, 5.0f, 4.5f, 0.0f, 9.0f, 5.0f), at); break;
        case Icon::arpDown:   g.fillPath (triangle (0.0f, 0.0f, 9.0f, 0.0f, 4.5f, 5.0f), at); break;
        case Icon::arpUpDown:
            p = triangle (0.0f, 4.0f, 3.5f, 0.0f, 7.0f, 4.0f);
            p.addTriangle (0.0f, 5.0f, 7.0f, 5.0f, 3.5f, 9.0f);
            g.fillPath (p, at);
            break;
        case Icon::arpSticky:
            p.addRectangle (0.0f, 0.0f, 9.0f, 1.2f);
            p.addTriangle (1.5f, 5.4f, 4.5f, 2.4f, 7.5f, 5.4f);
            p.addTriangle (1.5f, 7.6f, 7.5f, 7.6f, 4.5f, 10.6f);
            p.addRectangle (0.0f, 11.8f, 9.0f, 1.2f);
            g.fillPath (p, at);
            break;
        case Icon::arpRandom:
        {
            // a question mark: a hook, a short stem and a dot
            p.addCentredArc (4.5f, 3.4f, 3.0f, 2.7f, 0.0f, -juce::MathConstants<float>::halfPi, 2.5f, true);
            p.lineTo (4.5f, 6.6f);
            p.lineTo (4.5f, 7.6f);
            stroke (p, 1.7f, at);
            dot (4.5f, 9.8f, 1.1f);
            break;
        }
        case Icon::link:
        {
            // a ring that comes round to an arrowhead
            const auto end = juce::MathConstants<float>::twoPi - 0.45f;
            p.addCentredArc (4.5f, 4.5f, 3.3f, 3.3f, 0.0f, 0.5f, end, true);
            stroke (p, 1.5f, at);
            const juce::Point<float> tip (4.5f + 3.3f * std::sin (end), 4.5f - 3.3f * std::cos (end));
            const juce::Point<float> dir (std::cos (end), std::sin (end)), side (-std::sin (end), std::cos (end));
            g.fillPath (triangle (tip.x + 2.0f * dir.x, tip.y + 2.0f * dir.y, tip.x + 1.6f * side.x, tip.y + 1.6f * side.y,
                                  tip.x - 1.6f * side.x, tip.y - 1.6f * side.y), at);
            break;
        }
        default:
            break;
    }
}


//==============================================================================
void SlideSwitch::paintButton (juce::Graphics& g, bool, bool)
{
    // FL's A / B reverb switch (measured): a 14 x 27 px upright pill with a
    // 1 px grey outline and a dark inside, a 12 px light grey knob at its top
    // (A) or bottom (B), the letters beside it.
    const auto alpha = isEnabled() ? 1.0f : 0.45f;
    const auto pill = juce::Rectangle<float> (0.0f, 0.0f, 14.0f, 27.0f);
    g.setColour (colour::pillEdge.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (pill, 7.0f);
    g.setColour (colour::pillInside.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (pill.reduced (1.0f), 6.0f);
    const auto knob = juce::Rectangle<float> (1.0f, getToggleState() ? 14.0f : 1.0f, 12.0f, 12.0f);
    g.setColour (colour::pillKnobEdge.withMultipliedAlpha (alpha));
    g.fillEllipse (knob);
    g.setColour (colour::pillKnob.withMultipliedAlpha (alpha));
    g.fillEllipse (knob.reduced (1.0f));
    g.setColour (colour::text.withMultipliedAlpha (alpha));
    juce::GlyphArrangement ga;
    ga.addLineOfText (font (10.3f), "A", 17.0f, 12.0f);   // baselines 12 and 27 (measured: the caps span 7 rows)
    ga.addLineOfText (font (10.3f), "B", 17.0f, 27.0f);
    ga.draw (g);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // The file buttons (sizes and colours measured in FL): a 1 px dark
    // frame, a 1 px lighter inner edge, a fill darkening downwards, and thin
    // 1 px line icons at fixed positions.
    const auto b = getLocalBounds().toFloat();
    const auto w = b.getWidth();
    drawButton (g, getLocalBounds(), false, highlighted, down, isEnabled());   // FL: the same push button as Reset

    const auto c = isEnabled() ? juce::Colour (0xffebebeb) : colour::dim;
    g.setColour (c);
    if (down)
        g.addTransform (juce::AffineTransform::translation (0.0f, 1.0f));   // FL: a pressed icon drops a pixel
    // Icons sit on FL's 1 px grid (pixel centres at +0.5).
    auto folder = [&g] (float x0, float x1, float tabX, float yTab, float yTop, float yBottom)
    {
        juce::Path p;
        p.startNewSubPath (x0 + 0.5f, yBottom + 0.5f);
        p.lineTo (x0 + 0.5f, yTab + 0.5f);
        p.lineTo (tabX + 0.5f, yTab + 0.5f);
        p.lineTo (tabX + 2.0f, yTop + 0.5f);
        p.lineTo (x1 + 0.5f, yTop + 0.5f);
        p.lineTo (x1 + 0.5f, yBottom + 0.5f);
        p.closeSubPath();
        g.strokePath (p, juce::PathStrokeType (1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square));
    };
    const auto cx = std::floor (w * 0.5f);   // FL centres the icons on the box
    switch (icon)
    {
        case Icon::folder:   // 12 x 10 px
            folder (cx - 5.0f, cx + 6.0f, cx - 1.0f, 6.0f, 8.0f, 15.0f);
            break;
        case Icon::locate:   // a 9 x 8 px folder over a little tree
        {
            folder (cx - 3.0f, cx + 5.0f, cx, 4.0f, 6.0f, 11.0f);
            const auto tx = cx + 1.0f;
            g.fillRect (tx, 13.0f, 1.0f, 6.0f);
            g.fillRect (tx + 1.0f, 15.0f, 2.0f, 1.0f);
            g.fillRect (tx + 1.0f, 18.0f, 2.0f, 1.0f);
            break;
        }
        case Icon::close:    // 9 x 9 px cross
        {
            const auto x0 = cx - 3.0f, y0 = 7.0f;
            g.drawLine (x0 + 0.2f, y0 + 0.2f, x0 + 8.8f, y0 + 8.8f, 1.7f);
            g.drawLine (x0 + 8.8f, y0 + 0.2f, x0 + 0.2f, y0 + 8.8f, 1.7f);
            break;
        }
        default:
            drawIcon (g, icon, b, c);
            break;
    }
}

//==============================================================================
void showContextMenu (juce::PopupMenu& menu, juce::Component& target, bool atMouse, std::function<void()> onClose)
{
    target.getProperties().set (contextMenuProperty, true);
    menu.setLookAndFeel (&target.getLookAndFeel());   // otherwise JUCE uses its default look
    armMenuLetters (menu);
    auto options = juce::PopupMenu::Options().withTargetComponent (&target);
    menu.showMenuAsync (atMouse ? options.withMousePosition() : options, [onClose] (int)
    {
        if (onClose != nullptr)
            onClose();
    });
}

void MenuArrow::showMenu (juce::PopupMenu& menu)
{
    menuOpen = true;
    turn.refresh();
    showContextMenu (menu, *this, false, [safe = juce::Component::SafePointer<MenuArrow> (this)]
    {
        if (safe != nullptr)
        {
            safe->menuOpen = false;
            safe->turn.refresh();
        }
    });
}

namespace
{
    // FL's dialog colours (measured from FL's dialogs)
    const juce::Colour dlgBg { 0xff000000 }, dlgEdge { 0xff2b2b2b }, dlgPanel { 0xff121212 }, dlgPanelEdge { 0xff2b2b2b },
                       dlgTitle { 0xffffffff }, dlgText { 0xffebebeb }, dlgSign { 0xffa78bfa }, dlgButtonText { 0xffe4e4e4 };
    constexpr float dlgEm = 11.25f, dlgSmallEm = 10.8f;

    // FL's dialog button: dark frame, lighter top edge, darkening fill
    void paintDialogButton (juce::Graphics& g, juce::Rectangle<int> button, const juce::String& label, bool hover, bool down)
    {
        const auto b = button.toFloat();
        g.setColour (juce::Colour (0xff2b2b2b));
        g.fillRect (b);
        g.setColour (juce::Colour (0xff383838));
        g.fillRect (b.reduced (1.0f));
        const float lift = down ? -0.06f : hover ? 0.06f : 0.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff222222).brighter (lift), 0.0f, b.getY() + 2.0f,
                                                 juce::Colour (0xff141414).brighter (lift), 0.0f, b.getBottom() - 1.0f, false));
        g.fillRect (b.reduced (1.0f).withTrimmedTop (1.0f));
        g.setColour (dlgButtonText);
        g.setFont (font (dlgSmallEm));
        g.drawText (label, button, juce::Justification::centred, false);
    }

    // What FL's dialogs share: the frame with its title and close cross, the
    // panel under the title, and closing (the dialog deletes itself once the
    // event that closed it is done).
    class DialogBase : public juce::Component
    {
    public:
        explicit DialogBase (const juce::String& t) : title (t)
        {
            setWantsKeyboardFocus (true);
        }

    protected:
        static constexpr int titleH = 25, buttonH = 31;

        // A box boxW wide and up to boxH tall in the middle, the panel under
        // its title, the close cross in its corner.
        void layoutFrame (int boxW, int boxH, int panelH)
        {
            box = juce::Rectangle<int> (boxW, juce::jmin (boxH, getHeight() - 12)).withCentre (getLocalBounds().getCentre());
            panel = juce::Rectangle<int> (box.getX() + 4, box.getY() + titleH + 1, boxW - 8, panelH);
            closeBox = juce::Rectangle<int> (box.getRight() - 22, box.getY() + 4, 17, 17);
        }

        void paintFrame (juce::Graphics& g) const
        {
            g.fillAll (juce::Colours::black.withAlpha (0.25f));   // keeps the eye on the dialog
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (box.translated (2, 3));
            g.setColour (dlgEdge);
            g.fillRect (box);
            g.setColour (dlgBg);
            g.fillRect (box.reduced (1));
            g.setColour (dlgTitle);
            g.setFont (font (dlgEm));
            g.drawText (title, box.getX() + 6, box.getY(), box.getWidth() - 34, titleH, juce::Justification::centredLeft, true);
            const auto cc = closeBox.toFloat().reduced (4.5f);
            g.setColour (closeHover ? juce::Colours::white : dlgTitle);
            g.drawLine (cc.getX(), cc.getY(), cc.getRight(), cc.getBottom(), 1.3f);
            g.drawLine (cc.getRight(), cc.getY(), cc.getX(), cc.getBottom(), 1.3f);
            g.setColour (dlgPanelEdge);
            g.fillRect (panel);
            g.setColour (dlgPanel);
            g.fillRect (panel.reduced (1));
        }

        // Hides the dialog and deletes it after the current event; then runs after.
        void dismiss (std::function<void()> after = {})
        {
            setVisible (false);
            juce::Component::SafePointer<DialogBase> self (this);
            juce::MessageManager::callAsync ([self, after]
            {
                delete self.getComponent();
                if (after != nullptr)
                    after();
            });
        }

        juce::String title;
        juce::Rectangle<int> box, panel, closeBox;
        bool closeHover = false;
    };

    // A message (OK), or a properties sheet of label / value rows.
    class Dialog : public DialogBase
    {
    public:
        Dialog (const juce::String& t, const juce::String& m, bool w, InfoRows r = {})
            : DialogBase (t), message (m), warning (w), rows (std::move (r))
        {
        }

        void resized() override
        {
            if (! rows.empty())
            {
                layoutTable();
                return;
            }
            constexpr int signW = 46;
            const int boxW = juce::jmin (380, getWidth() - 24);
            juce::AttributedString as;
            as.append (message, font (dlgEm), dlgText);
            as.setWordWrap (juce::AttributedString::byWord);
            text.createLayout (as, (float) (boxW - 8 - signW - 16));
            const int panelH = juce::jmax (64, (int) std::ceil (text.getHeight()) + 32);
            layoutFrame (boxW, titleH + 1 + panelH + 12 + buttonH + 12, panelH);
            textArea = panel.toFloat().withTrimmedLeft ((float) signW).reduced (8.0f, 16.0f);
            button = juce::Rectangle<int> (116, buttonH).withCentre ({ box.getCentreX(), panel.getBottom() + 12 + buttonH / 2 });
        }

        void paint (juce::Graphics& g) override
        {
            paintFrame (g);
            if (rows.empty())
            {
                g.setColour (dlgSign);
                g.setFont (font (36.0f));
                g.drawText (warning ? "!" : "i", panel.withWidth (46).withTrimmedTop (8).withHeight (56), juce::Justification::centred, false);
                text.draw (g, textArea);
            }
            else
                paintTable (g);

            paintDialogButton (g, button, "OK", buttonHover, buttonDown);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const bool h = button.contains (e.getPosition()), c = closeBox.contains (e.getPosition());
            if (h != buttonHover || c != closeHover)
            {
                buttonHover = h;
                closeHover = c;
                repaint();
            }
        }
        void mouseExit (const juce::MouseEvent&) override { buttonHover = closeHover = false; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            buttonDown = button.contains (e.getPosition());
            repaint();
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            const bool fire = (buttonDown && button.contains (e.getPosition())) || closeBox.contains (e.getPosition());
            buttonDown = false;
            repaint();
            if (fire)
                dismiss();
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::returnKey || k == juce::KeyPress::escapeKey || k == juce::KeyPress::spaceKey)
            {
                dismiss();
                return true;
            }
            return false;
        }

    private:
        static constexpr int labelW = 104, ruleH = 13;

        void layoutTable()
        {
            const int boxW = juce::jmin (440, getWidth() - 24);
            const auto valueW = (float) (boxW - 8 - 16 - labelW - 12);
            layouts.clear();
            int panelH = 12;
            for (auto& [label, value] : rows)
            {
                juce::TextLayout tl;
                if (label.isNotEmpty() || value.isNotEmpty())
                {
                    juce::AttributedString as;
                    as.append (value, font (dlgSmallEm), dlgText);
                    as.setWordWrap (juce::AttributedString::byChar);
                    tl.createLayout (as, valueW);
                    panelH += juce::jmax (20, (int) std::ceil (tl.getHeight()) + 5);
                }
                else
                    panelH += ruleH;
                layouts.push_back (std::move (tl));
            }
            panelH += 10;
            layoutFrame (boxW, titleH + 1 + panelH + 12 + buttonH + 12, panelH);
            panel.setHeight (juce::jmin (panelH, box.getHeight() - titleH - buttonH - 26));
            button = juce::Rectangle<int> (116, buttonH).withCentre ({ box.getCentreX(), panel.getBottom() + 12 + buttonH / 2 });
        }

        void paintTable (juce::Graphics& g)
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (panel.reduced (1));
            int y = panel.getY() + 12;
            const int x0 = panel.getX() + 12, xv = x0 + labelW;
            for (size_t i = 0; i < rows.size(); ++i)
            {
                const auto& [label, value] = rows[i];
                if (label.isEmpty() && value.isEmpty())
                {
                    g.setColour (dlgPanelEdge);
                    g.fillRect (x0, y + ruleH / 2, panel.getRight() - 12 - x0, 1);
                    y += ruleH;
                    continue;
                }
                const int h = juce::jmax (20, (int) std::ceil (layouts[i].getHeight()) + 5);
                g.setColour (dlgSign);
                g.setFont (font (dlgSmallEm));
                g.drawText (label, x0, y, labelW - 8, 20, juce::Justification::centredLeft, true);
                layouts[i].draw (g, juce::Rectangle<float> ((float) xv, (float) y + 2.5f, (float) (panel.getRight() - 12 - xv), (float) h));
                y += h;
            }
        }

        juce::String message;
        bool warning;
        InfoRows rows;
        std::vector<juce::TextLayout> layouts;
        juce::TextLayout text;
        juce::Rectangle<int> button;
        juce::Rectangle<float> textArea;
        bool buttonHover = false, buttonDown = false;
    };

    // FL's question dialog with a column of choices (Tempo detection).
    class ChoiceDialog : public DialogBase
    {
    public:
        ChoiceDialog (const juce::String& t, const juce::String& m, const juce::StringArray& c, std::function<void (int)> f)
            : DialogBase (t), message (m), choices (c), onChoice (std::move (f))
        {
        }

        void resized() override
        {
            constexpr int signW = 70, choiceW = 104, choiceH = 23, gap = 4;   // FL: 31 / 6, but our window is smaller
            const int boxW = juce::jmin (546, getWidth() - 24);
            juce::AttributedString as;
            as.append (message, font (dlgEm), dlgText);
            as.setWordWrap (juce::AttributedString::byWord);
            text.createLayout (as, (float) (boxW - 8 - signW - 16));
            const int panelH = juce::jmax (64, (int) std::ceil (text.getHeight()) + 24);
            const int listH = choices.size() * (choiceH + gap) - gap;
            layoutFrame (boxW, titleH + 1 + panelH + 12 + listH + 10, panelH);
            textArea = panel.toFloat().withTrimmedLeft ((float) signW).reduced (8.0f, 12.0f);
            buttons.clear();
            for (int i = 0; i < choices.size(); ++i)
                buttons.push_back (juce::Rectangle<int> (choiceW, choiceH)
                                       .withCentre ({ box.getCentreX(), panel.getBottom() + 12 + i * (choiceH + gap) + choiceH / 2 }));
        }

        void paint (juce::Graphics& g) override
        {
            paintFrame (g);
            g.setColour (dlgSign);
            g.setFont (font (57.6f));
            g.drawText ("?", panel.withWidth (70).withTrimmedTop (4).withHeight (80), juce::Justification::centred, false);
            text.draw (g, textArea);
            for (int i = 0; i < (int) buttons.size(); ++i)
                paintDialogButton (g, buttons[(size_t) i], choices[i], i == hover, i == down);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto h = indexAt (e.getPosition());
            const bool c = closeBox.contains (e.getPosition());
            if (h != hover || c != closeHover)
            {
                hover = h;
                closeHover = c;
                repaint();
            }
        }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; closeHover = false; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override { down = indexAt (e.getPosition()); repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            const auto i = indexAt (e.getPosition());
            const bool fire = i >= 0 && i == down;
            down = -1;
            repaint();
            if (fire)
                finish (i);
            else if (closeBox.contains (e.getPosition()))
                finish (-1);
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::escapeKey)
            {
                finish (-1);
                return true;
            }
            return false;
        }

    private:
        int indexAt (juce::Point<int> p) const
        {
            for (int i = 0; i < (int) buttons.size(); ++i)
                if (buttons[(size_t) i].contains (p))
                    return i;
            return -1;
        }

        void finish (int choice)
        {
            auto f = std::move (onChoice);
            dismiss ([f, choice]
            {
                if (f != nullptr && choice >= 0)
                    f (choice);
            });
        }

        juce::String message;
        juce::StringArray choices;
        std::function<void (int)> onChoice;
        juce::TextLayout text;
        juce::Rectangle<float> textArea;
        std::vector<juce::Rectangle<int>> buttons;
        int hover = -1, down = -1;
    };

    // A one-line entry in FL's dialog style (the Type in choices).
    class TextDialog : public DialogBase
    {
    public:
        TextDialog (const juce::String& t, const juce::String& p, const juce::String& initial, std::function<void (const juce::String&)> f)
            : DialogBase (t), prompt (p), onText (std::move (f))
        {
            editor.setFont (juce::FontOptions (13.0f));
            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141414));
            editor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
            editor.setColour (juce::TextEditor::outlineColourId, colour::arc);
            editor.setColour (juce::TextEditor::focusedOutlineColourId, colour::arc);
            editor.setJustification (juce::Justification::centred);
            editor.setText (initial, false);
            editor.onReturnKey = [this] { finish (true); };
            editor.onEscapeKey = [this] { finish (false); };
            addAndMakeVisible (editor);
        }

        void resized() override
        {
            constexpr int panelH = 86;
            layoutFrame (juce::jmin (300, getWidth() - 24), titleH + 1 + panelH + 12 + buttonH + 12, panelH);
            editor.setBounds (juce::Rectangle<int> (140, 24).withCentre ({ panel.getCentreX(), panel.getY() + 58 }));
            button = juce::Rectangle<int> (116, buttonH).withCentre ({ box.getCentreX(), panel.getBottom() + 12 + buttonH / 2 });
        }

        void visibilityChanged() override
        {
            if (isVisible())
                juce::MessageManager::callAsync ([self = juce::Component::SafePointer<TextDialog> (this)]
                {
                    if (self != nullptr)
                    {
                        self->editor.grabKeyboardFocus();
                        self->editor.selectAll();
                    }
                });
        }

        void paint (juce::Graphics& g) override
        {
            paintFrame (g);
            g.setColour (dlgText);
            g.setFont (font (dlgEm));
            g.drawText (prompt, panel.withHeight (40), juce::Justification::centred, true);
            paintDialogButton (g, button, "OK", buttonHover, buttonDown);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const bool h = button.contains (e.getPosition()), c = closeBox.contains (e.getPosition());
            if (h != buttonHover || c != closeHover)
            {
                buttonHover = h;
                closeHover = c;
                repaint();
            }
        }
        void mouseDown (const juce::MouseEvent& e) override { buttonDown = button.contains (e.getPosition()); repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            const bool ok = buttonDown && button.contains (e.getPosition());
            buttonDown = false;
            repaint();
            if (ok)
                finish (true);
            else if (closeBox.contains (e.getPosition()))
                finish (false);
        }

    private:
        void finish (bool apply)
        {
            auto f = std::move (onText);
            const auto t = editor.getText();
            dismiss ([f, t, apply]
            {
                if (apply && f != nullptr)
                    f (t);
            });
        }

        juce::String prompt;
        std::function<void (const juce::String&)> onText;
        juce::TextEditor editor;
        juce::Rectangle<int> button;
        bool buttonHover = false, buttonDown = false;
    };
}

namespace
{
    void present (juce::Component& owner, juce::Component* d)
    {
        // Shown on the editor's scaled canvas (the editor's child that holds
        // owner), so it grows with the window.
        juce::Component* top = owner.getTopLevelComponent();
        if (auto* editor = owner.findParentComponentOfClass<juce::AudioProcessorEditor>())
        {
            top = &owner;
            while (top->getParentComponent() != editor)
                top = top->getParentComponent();
        }
        top->addAndMakeVisible (d);   // the window owns it; it deletes itself when closed
        d->setBounds (top->getLocalBounds());
        d->grabKeyboardFocus();
    }
}

void showInfo (juce::Component& owner, const juce::String& title, const InfoRows& rows)
{
    present (owner, new Dialog (title, {}, false, rows));
}

void showDialog (juce::Component& owner, const juce::String& title, const juce::String& message, bool warning)
{
    present (owner, new Dialog (title, message, warning));
}

void showChoices (juce::Component& owner, const juce::String& title, const juce::String& message,
                  const juce::StringArray& choices, std::function<void (int)> onChoice)
{
    present (owner, new ChoiceDialog (title, message, choices, std::move (onChoice)));
}

void askText (juce::Component& owner, const juce::String& title, const juce::String& prompt, const juce::String& initial,
              std::function<void (const juce::String&)> onText)
{
    present (owner, new TextDialog (title, prompt, initial, std::move (onText)));
}

//==============================================================================
IconTabs::IconTabs (std::vector<Icon> i, bool l) : icons (std::move (i)), large (l)
{
}

void IconTabs::setSelected (int index, bool notify)
{
    if (index == selected)
        return;
    selected = index;
    repaint();
    if (notify && onChange)
        onChange (index);
}

void IconTabs::paint (juce::Graphics& g)
{
    const auto cellW = (float) getWidth() / (float) icons.size();
    for (size_t i = 0; i < icons.size(); ++i)
    {
        auto cell = juce::Rectangle<float> (std::floor (cellW * (float) i), 0.0f, std::floor (cellW), (float) getHeight());
        const bool sel = (int) i == selected;
        if (sel)
        {
            // FL: the selected sheet tab's cell is a shade lighter; a small
            // selector gets a dark rounded box
            g.setColour (large ? colour::tabSelected : colour::well);
            if (large) g.fillRect (cell);
            else       g.fillRoundedRectangle (cell.withTrimmedRight (1.0f).reduced (1.0f), 2.0f);
        }
        drawIcon (g, icons[i], cell, sel ? selectedColour : unselectedColour.withAlpha (isEnabled() ? 1.0f : 0.5f));
    }
}

void IconTabs::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    setSelected (juce::jlimit (0, (int) icons.size() - 1, (int) (e.x * (int) icons.size() / juce::jmax (1, getWidth()))));
}

//==============================================================================
TextTabs::TextTabs (juce::StringArray i, float h, bool highlight)
    : items (std::move (i)), fontHeight (h), highlightStyle (highlight)
{
}

juce::Rectangle<int> TextTabs::tabBounds (int index) const
{
    const auto f = font (fontHeight, highlightStyle);
    int x = 0;
    for (int i = 0; i < items.size(); ++i)
    {
        // FL: the envelope targets sit 9 px apart, the small selectors 10
        const int w = (int) juce::GlyphArrangement::getStringWidth (f, items[i]) + (highlightStyle ? 10 : 8);
        if (i == index)
            return { x + (i < (int) nudges.size() ? nudges[(size_t) i] : 0), 0, w, getHeight() };
        x += w;
    }
    return {};
}

void TextTabs::setSelected (int index, bool notify)
{
    if (index == selected)
        return;
    selected = index;
    repaint();
    if (notify && onChange)
        onChange (index);
}

void TextTabs::paint (juce::Graphics& g)
{
    g.setFont (font (fontHeight, highlightStyle));
    for (int i = 0; i < items.size(); ++i)
    {
        const auto r = tabBounds (i).toFloat();
        const bool sel = i == selected;
        // FL: the selected item is coloured text (green for the INS targets,
        // orange for the small selectors); no box behind it.
        g.setColour (sel ? (highlightStyle ? colour::arc : colour::textLit)
                         : (isEnabled() ? colour::text : colour::dim));
        g.drawText (items[i], r, juce::Justification::centred, false);
    }
}

void TextTabs::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    for (int i = 0; i < items.size(); ++i)
        if (tabBounds (i).contains (e.getPosition()))
            setSelected (i);
}
}
