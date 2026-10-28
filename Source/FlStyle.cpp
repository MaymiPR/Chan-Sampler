#include "FlStyle.h"
#include "FlBigKnob.h"
#include "FlIcons.h"

namespace fl
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
// Per-pixel rendering of FL's shaded controls. FL's knobs and switches are
// bitmaps with bevels and glows; a path renderer cannot reproduce their pixel
// values, so they are rendered into an image at the screen's pixel scale and
// drawn as one. The formulas below were fitted to FL 2026's pixels (mean
// error about 6 of 765 per pixel on the 22, 30, 32 and 34 px knobs).
namespace
{
    struct Rgb
    {
        float r, g, b;
        Rgb operator* (float k) const { return { r * k, g * k, b * k }; }
        Rgb operator+ (Rgb o) const { return { r + o.r, g + o.g, b + o.b }; }
        Rgb operator+ (float k) const { return { r + k, g + k, b + k }; }
    };
    Rgb rgb (juce::Colour c) { return { (float) c.getRed(), (float) c.getGreen(), (float) c.getBlue() }; }
    Rgb lerp (Rgb a, Rgb b, float t) { t = juce::jlimit (0.0f, 1.0f, t); return a * (1.0f - t) + b * t; }
    juce::Colour toColour (Rgb c, float alpha = 1.0f)
    {
        return juce::Colour ((juce::uint8) juce::jlimit (0, 255, juce::roundToInt (c.r)), (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (c.g)),
                             (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (c.b)), alpha);
    }
    // Coverage of a 1 px pixel at distance d from a centre by the ring [lo, hi);
    // a disc (lo = 0) covers the centre pixel whole.
    float cov (float d, float lo, float hi) { return juce::jlimit (0.0f, 1.0f, juce::jmin (d + 0.5f, hi) - juce::jmax (d - 0.5f, lo > 0.0f ? lo : -1.0f)); }
    float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }

    // Renders area through fn (logical pixel coordinates relative to area,
    // returning the pixel's colour and coverage) at the screen's pixel scale
    // and on its pixel grid, so the result stays crisp when the window is
    // scaled.
    template <typename Fn>
    void drawPixels (juce::Graphics& g, juce::Rectangle<float> area, float alpha, Fn fn)
    {
        const auto scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        // snap the image to whole screen pixels: a fractional start would be resampled
        area.setPosition (std::round (area.getX() * scale) / scale, std::round (area.getY() * scale) / scale);
        const int w = juce::jmax (1, juce::roundToInt (area.getWidth() * scale)), h = juce::jmax (1, juce::roundToInt (area.getHeight() * scale));
        juce::Image img (juce::Image::ARGB, w, h, true);
        {
            juce::Image::BitmapData px (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    float coverage = 1.0f;
                    const auto c = fn (((float) x + 0.5f) / scale, ((float) y + 0.5f) / scale, coverage);
                    px.setPixelColour (x, y, toColour (c, alpha * clamp01 (coverage)));
                }
        }
        g.setOpacity (1.0f);   // an image takes the current fill's opacity
        g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / scale).translated (area.getX(), area.getY()));
    }

    // Unit-circle angle from the top, clockwise, and the vertical cosine.
    struct Polar { float r, th, cosv; };
    Polar polar (float x, float y, float cx, float cy)
    {
        const auto dx = x - cx, dy = y - cy;
        const auto th = std::atan2 (dx, -dy);
        return { std::hypot (dx, dy), th, std::cos (th) };
    }
    bool between (float th, float a, float b) { return th >= juce::jmin (a, b) - 1.0e-6f && th <= juce::jmax (a, b) + 1.0e-6f; }

    // FL's regular knob (22 to 38 px): a 1 px dark outer ring, a 2 px bevel
    // lit from above, a 1 px dark gap, and a body 4 px in from the edge with a
    // vertical gradient, a highlight along its top and a lighter bottom rim.
    // The value dot sits 3.6 px inside the body; the arc runs on the bevel.
    // A knob turns a full circle: value 0.5 is straight up.
    void renderKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, float zeroPos, juce::Colour arc, float hover, float alpha, bool showDot)
    {
        const float lift = 4.0f * hover;   // FL lightens a hovered knob a touch (measured +3 to +6)
        const auto S = juce::jmin (area.getWidth(), area.getHeight());
        const auto R = S * 0.5f, Rb = R - 4.0f;
        const auto cx = area.getWidth() * 0.5f, cy = area.getHeight() * 0.5f;
        const auto ang = juce::MathConstants<float>::twoPi * (pos - 0.5f), zero = juce::MathConstants<float>::twoPi * (zeroPos - 0.5f);
        const auto dotR = Rb - 3.56f;
        const auto dotX = cx + dotR * std::sin (ang), dotY = cy - dotR * std::cos (ang);
        const auto ux = std::sin (ang), uy = -std::cos (ang);
        const Rgb outer = rgb (colour::knobOuter), bevelTop = rgb (colour::knobBevelTop), bevelBottom = rgb (colour::knobBevelBottom),
                  gapTop = rgb (colour::knobGapTop), gapBottom = rgb (colour::knobGapBottom), arcC = rgb (arc), dot = arcC * 0.73f;
        const bool hasArc = std::abs (pos - zeroPos) > 0.002f;

        drawPixels (g, area, alpha, [&] (float x, float y, float& coverage) -> Rgb
        {
            const auto p = polar (x, y, cx, cy);
            coverage = cov (p.r, 0.0f, R);
            // body: luma falls 1.93 per pixel downwards (at 16 px radius), a
            // little darker towards the edge, lit along the top rim
            const auto dy = y - cy;
            float luma = 38.66f + lift - 1.93f * dy * (16.0f / R) - 6.9f * (p.r / Rb) * (p.r / Rb);
            luma += 36.0f * std::pow (clamp01 (p.cosv), 1.05f) * clamp01 (1.0f - (Rb - p.r) / 3.2f);
            luma += 12.0f * clamp01 (-p.cosv) * clamp01 (1.0f - (Rb - p.r) / 3.0f);
            Rgb body { luma - 2.0f, luma, luma + 2.0f };
            // the value dot: a 1.5 x 1.8 px mark, radial
            const auto along = (x - dotX) * ux + (y - dotY) * uy, across = (x - dotX) * -uy + (y - dotY) * ux;
            const auto dcov = showDot ? cov (along, -0.9f, 0.9f) * cov (across, -0.75f, 0.75f) : 0.0f;
            body = lerp (body, dot, dcov);

            const auto t = (1.0f - p.cosv) * 0.5f;
            // pixels on the outer edge blend from the rim, not the lit body
            Rgb c = p.r < Rb + 0.5f ? body : outer;
            c = lerp (c, lerp (gapTop, gapBottom, t) + lift, cov (p.r, Rb, R - 3.0f));
            c = lerp (c, lerp (bevelTop, bevelBottom, t) + lift, cov (p.r, R - 3.0f, R - 1.0f));
            c = lerp (c, outer, cov (p.r, R - 1.0f, R));
            if (hasArc && between (p.th, zero, ang))
                c = lerp (c, arcC, cov (p.r, R - 3.09f, R - 1.09f));
            return c;
        });
    }

    // FL's filter knob (66 px). Its shading (the brushed body with its lit
    // lobes, the bevel lit from above, the shadow at its foot) is measured from
    // FL's screen in polar coordinates (FlBigKnob.h) and interpolated, so it
    // comes out as FL draws it at any scale. The knob sweeps 320 degrees; the
    // dot (green on MOD X, gold on MOD Y) rides at 22.5 px; nine 1 px tick
    // marks sit every 40 degrees from 29.5 to 36 px out.
    void renderBigKnob (juce::Graphics& g, juce::Rectangle<float> area, float pos, juce::Colour dotColour, float hover, float alpha)
    {
        // under the mouse FL lights the body up: the hovered knob was measured too (bigKnobLumaHover) and the two are crossfaded
        const auto cx = area.getWidth() * 0.5f, cy = area.getHeight() * 0.5f;
        const auto R = juce::jmin (cx, cy);          // 33 at FL's size
        const auto k = R / 33.0f;
        const auto ang = juce::degreesToRadians (-160.0f + 320.0f * pos);
        const auto dotX = cx + 22.5f * k * std::sin (ang), dotY = cy - 22.5f * k * std::cos (ang);
        const Rgb panel = rgb (colour::panel), dot = rgb (dotColour);
        // Under the mouse FL's knob shows a dark collar with five lobes round
        // its cap, and the lobes turn with the value while the light on it
        // stays (measured frame by frame in FL 2026 while MOD X was dragged
        // through 97 degrees: the collar's outline matched the first frame's
        // turned by the dot's angle, to within 1.2 degrees modulo 72, the cap
        // and the shadow unchanged). Fitted on the hovered capture (its dot at
        // 156.8 degrees): the outline is a base radius set by the light
        // (24.53 - 1.07 cos t - 0.67 cos 2t + 0.38 cos 3t px) less a dip
        // 2.43 px deep and 40.8 degrees wide (cos^2 to the power 0.79) at every
        // 72 degrees from 45.06; 0.38 px rms. The unhovered knob's collar is
        // round and mirror symmetric, nothing to turn. So the hovered table is
        // read with its radius warped between 19 and 29.5 px, moving its
        // measured outline to where the lobes have turned to.
        const auto turnDeg = (-160.0f + 320.0f * pos) - 156.8f;
        auto lobes = [] (float deg)
        {
            const auto u = std::fmod (std::fmod (deg - 45.06f + 36.0f, 72.0f) + 72.0f, 72.0f) - 36.0f;   // from the nearest valley
            if (std::abs (u) >= 20.4f)
                return 0.0f;
            const auto c = std::cos (juce::MathConstants<float>::pi * u / 40.8f);
            return -2.43f * std::pow (c * c, 0.79f);
        };
        auto hoverRadius = [&lobes, turnDeg] (float r, float deg)
        {
            constexpr float rIn = 19.0f, rOut = 29.5f;
            if (r <= rIn || r >= rOut || std::abs (turnDeg) < 0.01f)
                return r;
            const auto t = juce::degreesToRadians (deg);
            const auto base = 24.53f - 1.07f * std::cos (t) - 0.67f * std::cos (2.0f * t) + 0.38f * std::cos (3.0f * t);
            const auto edgeRef = base + lobes (deg), edgeNow = base + lobes (deg - turnDeg);
            return r < edgeNow ? rIn + (r - rIn) * (edgeRef - rIn) / (edgeNow - rIn)
                               : edgeRef + (r - edgeNow) * (rOut - edgeRef) / (rOut - edgeNow);
        };
        auto shade = [hover, &hoverRadius] (float r, float deg)
        {
            // bilinear in the measured table: 0.5 px rings, 5 degree spokes
            const auto ai = (deg + 180.0f) / 5.0f;
            const int a0 = (((int) std::floor (ai)) % bigKnobAngles + bigKnobAngles) % bigKnobAngles, a1 = (a0 + 1) % bigKnobAngles;
            const auto fa = ai - std::floor (ai);
            auto sample = [&] (const unsigned char (*t)[bigKnobAngles], float rr)
            {
                const auto ri = juce::jlimit (0.0f, (float) (bigKnobRadii - 1), rr * 2.0f);
                const int r0 = (int) ri, r1 = juce::jmin (r0 + 1, bigKnobRadii - 1);
                const auto fr = ri - (float) r0;
                const auto row = [&] (int k) { return (float) t[k][a0] * (1.0f - fa) + (float) t[k][a1] * fa; };
                return row (r0) * (1.0f - fr) + row (r1) * fr;
            };
            const auto base = sample (bigKnobLuma, r);
            if (hover <= 0.0f)
                return base;
            return base * (1.0f - hover) + sample (bigKnobLumaHover, hoverRadius (r, deg)) * hover;
        };

        drawPixels (g, area, alpha, [&] (float x, float y, float& coverage) -> Rgb
        {
            const auto p = polar (x, y, cx, cy);
            const auto r = p.r / k;
            const auto deg = juce::radiansToDegrees (p.th);
            coverage = 1.0f;
            const auto luma = shade (juce::jmin (r, 30.0f), deg);
            Rgb c { luma + bigKnobTint[0], luma + bigKnobTint[1], luma + bigKnobTint[2] };
            c = lerp (c, panel, (r - 29.5f) / 1.0f);   // the table ends at the panel colour
            // tick marks: 1 px lines from 29.5 to 36 px out, every 40 degrees
            const auto nearest = std::round (deg / 40.0f) * 40.0f;
            if (std::abs (nearest) <= 160.0f && r >= 29.5f && r < 36.0f)
            {
                const auto across = r * std::sin (juce::degreesToRadians (deg - nearest));
                c = lerp (c, panel + -6.0f, cov (across, -0.5f, 0.5f));
            }
            // the dot on the bevel
            const auto dd = std::hypot (x - dotX, y - dotY);
            c = lerp (c, dot, 0.6f * cov (dd, 0.0f, 1.1f * k));
            if (r > 36.5f)
                coverage = 0.0f;
            return c;
        });
    }

    // FL's round switch: a 15 px light ring, a dark gap, and an inner disc
    // shaded darker at the top with a lit rim; lit, the disc glows orange
    // (or green), brighter towards the bottom with a specular at the top.
    void renderLed (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool green, float alpha)
    {
        const auto S = juce::jmin (area.getWidth(), area.getHeight());
        const auto k = S / 15.0f;
        const auto cx = area.getWidth() * 0.5f, cy = area.getHeight() * 0.5f;
        // FL's 11 px switch (CLIP, measured) has a 5 px disc, a 1 px gap and a 2 px ring
        const bool small = S < 13.0f;
        const auto ri = small ? 2.5f : 4.61f * k, ringIn = small ? 3.5f : 6.06f * k, ringOut = small ? 5.5f : 7.56f * k;
        const Rgb ring = rgb (on ? colour::ledOnRing : colour::ledRing), gap = rgb (on ? colour::ledOnGap : colour::ledGap);
        const Rgb innerTop = rgb (colour::ledInnerTop), innerBottom = rgb (colour::ledInnerBottom);
        const Rgb onCentre = rgb (green ? colour::ledGreenCentre : colour::ledOnCentre), onBottom = rgb (green ? colour::ledGreenEdge : colour::ledOnBottom),
                  onEdge = rgb (green ? colour::ledGreenEdge : colour::ledOnEdge), onTop = rgb (colour::ledOnTop);

        drawPixels (g, area, alpha, [&] (float x, float y, float& coverage) -> Rgb
        {
            const auto p = polar (x, y, cx, cy);
            coverage = cov (p.r, 0.0f, ringOut);
            const auto dy = (y - cy) / ri;
            Rgb inner;
            if (! on && small)
            {
                // lit along its top, darkest along its bottom (measured: 4c, 31, 19)
                const auto t = (dy + 1.0f) * 0.5f;
                inner = lerp (rgb (colour::ledSmallTop), rgb (colour::ledSmallMid), clamp01 (t * 2.0f));
                inner = lerp (inner, rgb (colour::ledSmallBottom), clamp01 ((t - 0.5f) * 2.0f));
            }
            else if (! on)
            {
                inner = lerp (innerTop, innerBottom, (dy + 1.0f) * 0.5f);
                inner = inner + 31.2f * std::pow (clamp01 (p.cosv), 1.55f) * clamp01 ((p.r - 3.15f * k) / (ri - 3.15f * k));
            }
            else if (small)
            {
                // lit (measured): #EC744D in the middle to a lighter foot, a cream specular on the top row
                inner = lerp (onCentre, rgb (colour::ledSmallOnBottom), std::pow (clamp01 ((dy + 1.0f) * 0.5f), 1.4f));
                inner = lerp (inner, onTop, std::pow (clamp01 (p.cosv), 2.0f) * clamp01 ((p.r / ri - 0.45f) / 0.35f));
            }
            else
            {
                inner = lerp (onCentre, onBottom, std::pow (clamp01 ((dy + 1.0f) * 0.5f), 1.43f));
                inner = lerp (inner, onEdge, 0.43f * std::pow (clamp01 (p.r / ri), 4.77f));
                inner = lerp (inner, onTop, std::pow (clamp01 (p.cosv), 2.76f) * std::pow (clamp01 (p.r / ri), 4.5f));
            }
            Rgb c = p.r < ri + 0.5f ? inner : ring;   // the outer edge blends from the ring, not the lit disc
            c = lerp (c, gap, cov (p.r, ri, ringIn));
            c = lerp (c, ring, cov (p.r, ringIn, ringOut));
            return c;
        });
    }

    // FL's slider thumb: an 18 x 17 px ball with a dark rim, a lit band just
    // inside the rim at the top with a glow spreading down from it, a bright
    // spot at the bottom, a 1 px coloured line down its middle (lit a touch
    // either side), and a soft shadow on the ticks below. The shading is a
    // least-squares fit to FL's pixels (docs/EVIDENCE.md "Skin").
    void renderThumb (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour line, float hover, float alpha)
    {
        const float lift = 12.0f * hover;   // FL brightens the rims under the mouse
        constexpr float rx = 9.181f, ry = 9.096f, rimW = 1.717f, rimTop = 23.914f, rimSide = 21.812f,
                        bodyTop = 55.175f, bodyMid = 21.582f, bodyBot = 8.453f, hiA = 31.676f, hiP = 0.652f, hiIn = 1.983f, hiW = 0.847f,
                        glowA = 55.725f, glowS = 4.873f, botA = 45.570f, botSx = 2.521f, botSy = 2.861f, botOff = 1.664f, haloA = 3.027f, sideK = 7.021f;
        // the ball is centred 9.5 px in and 8.5 px down: 19 x 17 px with the shadow rows below
        const auto cx = 9.5f, cy = 8.5f;
        const Rgb lineC = rgb (line);
        drawPixels (g, area, alpha, [&] (float x, float y, float& coverage) -> Rgb
        {
            const auto dx = x - cx, dy = y - cy;
            const auto d = std::hypot (dx, dy);
            const auto r = std::hypot (dx / rx, dy / ry);
            const auto cosv = -dy / juce::jmax (d, 1.0e-6f);
            const auto cosUp = clamp01 (cosv);
            coverage = clamp01 ((1.0f - r) * ry + 0.5f);
            if (coverage <= 0.0f)
            {
                // the shadow on the ticks below the ball
                if (dy > 7.5f && d < 13.2f)
                {
                    const auto dark = (14.0f - (d - 8.5f) * 1.5f) * clamp01 ((13.2f - d) / 0.8f) * clamp01 (dy - 7.5f);
                    coverage = clamp01 (dark / 34.0f);
                    return Rgb { 0.0f, 0.0f, 0.0f };
                }
                return Rgb { 0.0f, 0.0f, 0.0f };
            }
            const auto t = clamp01 (dy / ry * 0.5f + 0.5f);
            auto body = t < 0.5f ? bodyTop + (bodyMid - bodyTop) * t / 0.5f : bodyMid + (bodyBot - bodyMid) * (t - 0.5f) / 0.5f;
            body += sideK * (1.0f - std::abs (dx) / rx);
            const auto edgeIn = (1.0f - r) * ry;
            const auto rim = clamp01 (rimW - edgeIn + 0.5f);
            const auto rimL = rimTop * std::abs (cosv) + rimSide * (1.0f - std::abs (cosv)) + lift;
            auto luma = body * (1.0f - rim) + rimL * rim;
            const auto lit = (hiA + lift) * std::pow (cosUp, hiP) * std::exp (-juce::square ((edgeIn - hiIn) / hiW));
            const auto glow = glowA * std::pow (cosUp, 8.0f) * std::exp (-juce::jmax (edgeIn - hiIn, 0.0f) / glowS);
            const auto bot = botA * std::exp (-(dx * dx / (botSx * botSx) + juce::square (dy - ry + botOff) / (botSy * botSy)));
            luma += (lit + glow) * (1.0f - rim * cosUp) + bot;
            if (std::abs (std::abs (dx) - 1.0f) < 0.01f && dy >= -4.5f && dy <= 5.5f)
                luma += haloA;
            Rgb c { luma - 2.0f, luma + 1.0f, luma + 2.0f };
            // the line: 1 px wide, from 4 px above the centre to 5 below, its end pixels half covered
            c = lerp (c, lineC, cov (dx, -0.5f, 0.5f) * cov (dy, -4.0f, 5.0f));
            return c;
        });
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
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff4b4b48));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::textColourId, colour::comboText);
    setColour (juce::Label::textColourId, colour::text);
    setColour (juce::TextButton::textColourOffId, colour::buttonText);
    setColour (juce::TextButton::textColourOnId, colour::amber);
    setColour (juce::TooltipWindow::textColourId, juce::Colour (0xffc8ceca));   // also the knob value popup (menu text)
    setColour (juce::Slider::rotarySliderFillColourId, colour::amber);
    setColour (juce::TextEditor::highlightColourId, colour::amber.withAlpha (0.4f));
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
    return juce::LookAndFeel_V4::getTypefaceForFont (f);   // elsewhere fl::font sizes the system font
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

    // FL menu metrics (measured from FL 2026 screenshots at 100%).
    constexpr int menuRowH = 19, menuHeaderH = 13, knobHeaderH = 16;
    constexpr float menuEm = 10.8f, menuHeaderEm = 8.55f;
    const juce::Colour ddBg { 0xff202122 }, ddEdge { 0xff101112 }, ddHeader { 0xff333434 }, ddHeaderText { 0xff8a8c8d },
                       ddText { 0xffe8e8e8 }, ddHighlight { 0xff8c979e };
    const juce::Colour rmBg { 0xff515456 }, rmText { 0xffc8ceca }, rmHighlight { 0xff04090d }, rmDisabled { 0xff848887 },
                       rmHeader { 0xff303437 }, rmHeaderText { 0xff6e7779 }, rmSeparator { 0xff323639 };

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
        g.setColour (item.isEnabled ? juce::Colour (0xffa0a5a4) : rmDisabled);
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
              ! (bool) slider.getProperties().getWithDefault ("flNoDot", false));
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
    const auto line = bipolar ? colour::thumbRed : colour::amber;
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
        g.setColour (colour::amber.withAlpha (t));
        g.drawRoundedRectangle (frame.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (colour::amber.darker (0.35f).withAlpha (t));
        g.drawRoundedRectangle (frame.reduced (1.5f), 4.0f, 1.0f);
    }
    // at rest the 4.4 x 8 px triangle sits a pixel right of the cell's middle;
    // turned down (8 x 4.4 px) it is centred, a pixel lower (measured)
    const auto cell = b.withSizeKeepingCentre (20.0f, 20.0f);
    drawTurnArrow (g, { cell.getX() + 11.0f, cell.getY() + 10.0f }, { cell.getX() + 10.0f, cell.getY() + 11.2f }, t,
                   colour::icon.interpolatedWith (colour::amber, t), 8.0f, 4.4f);
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
    // FL's digi wheel (measured, docs/EVIDENCE.md "Skin"): a grey box with
    // soft edges (a rounded rectangle of radius 4.7 feathered over 1.7 px) lit
    // from its left middle, the light falling off elliptically; dark up / down
    // triangles 5 px from the right; the number in a large light font, or
    // three short dashes when the box is empty. Under the mouse the box
    // lightens a little and the triangles go.
    const auto alpha = isEnabled() ? 1.0f : 0.5f;
    const auto b = getLocalBounds().toFloat();
    const auto box = juce::Rectangle<float> (b.getX() + 2.81f, b.getY() + 0.67f, b.getWidth() - 3.58f, b.getHeight() - 1.34f);
    const auto hoverAmount = isEnabled() ? juce::jmax (hoverOf (*this), isMouseButtonDown() ? 1.0f : 0.0f) : 0.0f;
    const bool lit = hoverAmount > 0.5f;   // the triangles go once the fade is half way
    const float lift = 12.0f * hoverAmount;
    const auto fillAt = [&] (float x, float y)
    {
        const auto dx = x - (box.getX() + 0.23f), dy = (y - (box.getY() + 10.84f)) * 2.676f;
        return 95.86f + lift - 0.459f * std::hypot (dx, dy);
    };
    {
        const Rgb redTop = rgb (colour::digiRed.brighter (0.15f)), redBottom = rgb (colour::digiRed.darker (0.25f));
        drawPixels (g, b, alpha, [&] (float x, float y, float& coverage) -> Rgb
        {
            const auto cxm = juce::jlimit (box.getX() + 4.654f, box.getRight() - 4.654f, x), cym = juce::jlimit (box.getY() + 4.654f, box.getBottom() - 4.654f, y);
            const auto d = std::hypot (x - cxm, y - cym) - 4.654f;
            coverage = clamp01 (0.5f - d / 1.731f);
            if (red)
                return lerp (redTop, redBottom, (y - box.getY()) / box.getHeight()) + lift;
            const auto l = fillAt (x, y);
            return Rgb { l, l + 5.0f, l + 8.0f };
        });
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
    // Each icon's traced outline (FlIcons.h) and where FL puts its top-left
    // inside its cell: the sheet tabs' 44 x 45 px cells, the effects view's
    // 26 x 18 px cells (which get FL's small wrench), the LFO's 23 x 19 px cells.
    struct IconPlace { const IconShape* shape; int dx, dy; };

    IconPlace iconPlace (Icon icon, bool smallCell)
    {
        switch (icon)
        {
            case Icon::sampler:  return { &samplerIcon, 8, 12 };
            case Icon::envelope: return { &envelopeIcon, 10, 10 };
            case Icon::wrench:   return smallCell ? IconPlace { &fxWrenchIcon, 5, 3 } : IconPlace { &wrenchIcon, 12, 13 };
            case Icon::valve:    return { &valveIcon, 7, 3 };   // its cap fills rows 141-143 of the canvas (measured)
            case Icon::sine:     return { &sineIcon, 6, 5 };
            case Icon::triangle: return { &triangleIcon, 6, 5 };
            case Icon::square:   return { &squareIcon, 7, 6 };
            // the arpeggiator's directions in their 19 px cells (measured)
            case Icon::arpOff:    return { &arpOffIcon, 6, 5 };
            case Icon::arpUp:     return { &arpUpIcon, 6, 7 };
            case Icon::arpDown:   return { &arpDownIcon, 6, 7 };
            case Icon::arpUpDown: return { &arpUpDownIcon, 7, 5 };
            case Icon::arpSticky: return { &arpStickyIcon, 6, 3 };
            case Icon::arpRandom: return { &arpRandomIcon, 6, 4 };
            case Icon::link:      return { &linkIcon, 0, 0 };
            default:             return { nullptr, 0, 0 };   // file buttons: IconButton draws them
        }
    }
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> cell, juce::Colour c)
{
    // Each icon is the smoothed outline of FL's pixels, filled with
    // anti-aliasing, so it matches FL at 100 % and scales cleanly.
    const auto place = iconPlace (icon, cell.getHeight() < 24.0f);
    if (place.shape == nullptr)
        return;
    const auto x0 = std::floor (cell.getX()) + (float) place.dx, y0 = std::floor (cell.getY()) + (float) place.dy;
    g.setColour (c);
    auto at = [x0, y0] (float x, float y) { return juce::Point<float> (x0 + x, y0 + y); };
    auto stroke = [&g] (const juce::Path& path, float width)
    {
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    };
    juce::ignoreUnused (stroke);
    if (icon == Icon::valve)
    {
        // FL's effects icon is a glyph (ClearType-fringed in the capture): a
        // pointed cap, hairline side walls (about 0.36 px, so they show at a
        // third of the colour), a filament of two strokes and a stand of a bar,
        // a neck, a foot and a point. The shapes below were fitted to its
        // pixels' coverage (15x supersampled, squared error 0.60 over 216
        // pixels, ClearType's subpixel filter modelled; the earlier drawing was
        // off by 9.3) and are filled as one
        // shape, so it matches FL at 100 % and stays clean when scaled.
        static const std::vector<std::vector<juce::Point<float>>> shapes = {
            { { 2.33f, -0.37f }, { 2.78f, -0.09f }, { 3.13f, 0.18f }, { 3.45f, 0.46f }, { 3.76f, 0.73f }, { 4.05f, 1.01f }, { 4.33f, 1.29f }, { 4.6f, 1.56f }, { 4.86f, 1.84f }, { 5.12f, 2.11f }, { 5.38f, 2.39f }, { 5.63f, 2.67f }, { 5.87f, 2.94f }, { 6.12f, 3.22f }, { 5.69f, 3.38f }, { 5.01f, 3.29f }, { 4.34f, 3.21f }, { 3.67f, 3.16f }, { 3.0f, 3.13f }, { 2.33f, 3.12f }, { 1.65f, 3.13f }, { 0.98f, 3.16f }, { 0.31f, 3.21f }, { -0.36f, 3.29f }, { -1.03f, 3.38f }, { -1.7f, 3.49f }, { -1.46f, 3.22f }, { -1.22f, 2.94f }, { -0.98f, 2.67f }, { -0.73f, 2.39f }, { -0.47f, 2.11f }, { -0.21f, 1.84f }, { 0.05f, 1.56f }, { 0.33f, 1.29f }, { 0.61f, 1.01f }, { 0.9f, 0.73f }, { 1.2f, 0.46f }, { 1.52f, 0.18f }, { 1.87f, -0.09f } },
            { { -0.47f, 3.2f }, { -0.63f, 9.44f }, { -1.1f, 9.43f }, { -0.95f, 3.19f } },
            { { 5.6f, 3.19f }, { 5.76f, 9.43f }, { 5.28f, 9.44f }, { 5.13f, 3.2f } },
            { { 1.96f, 5.77f }, { 1.75f, 9.61f }, { 1.05f, 9.57f }, { 1.26f, 5.73f } },
            { { 3.39f, 5.73f }, { 3.6f, 9.57f }, { 2.91f, 9.61f }, { 2.7f, 5.77f } },
            { { 0.66f, 9.11f }, { 3.99f, 9.11f }, { 3.99f, 10.13f }, { 0.66f, 10.13f } },
            { { 1.4f, 10.13f }, { 3.26f, 10.13f }, { 3.26f, 11.14f }, { 1.4f, 11.14f } },
            { { 0.05f, 11.14f }, { 4.6f, 11.14f }, { 3.69f, 12.15f }, { 0.96f, 12.15f } },
            { { 1.64f, 12.15f }, { 3.01f, 12.15f }, { 2.33f, 13.18f } },
        };
        juce::Path valve;
        for (const auto& shape : shapes)
        {
            valve.startNewSubPath (shape.front());
            for (size_t i = 1; i < shape.size(); ++i)
                valve.lineTo (shape[i]);
            valve.closeSubPath();
        }
        valve.setUsingNonZeroWinding (true);   // all wound the same way: their union
        g.fillPath (valve, juce::AffineTransform::translation (x0, y0));
        return;
    }
    juce::ignoreUnused (at);
    juce::Path p;
    for (auto& loop : place.shape->loops)
    {
        for (size_t i = 0; i < loop.size(); ++i)
        {
            const juce::Point<float> pt (x0 + loop[i].x, y0 + loop[i].y);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        p.closeSubPath();
    }
    p.setUsingNonZeroWinding (false);   // the loops around holes run the other way
    g.fillPath (p);
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
    // FL's file buttons (measured pixel by pixel from FL 2026): a 1 px dark
    // frame, a 1 px lighter inner edge, a fill darkening downwards, and thin
    // 1 px line icons at fixed positions.
    const auto b = getLocalBounds().toFloat();
    const auto w = b.getWidth();
    drawButton (g, getLocalBounds(), false, highlighted, down, isEnabled());   // FL: the same push button as Reset

    const auto c = isEnabled() ? juce::Colour (0xffdfe4e5) : colour::dim;
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
    // FL's dialog colours (measured from FL 2026's dialogs)
    const juce::Colour dlgBg { 0xff202324 }, dlgEdge { 0xff16191a }, dlgPanel { 0xff282c2d }, dlgPanelEdge { 0xff1d2021 },
                       dlgTitle { 0xffd4dadd }, dlgText { 0xffd0d8da }, dlgSign { 0xff98a2a8 }, dlgButtonText { 0xffabb7b7 };
    constexpr float dlgEm = 11.25f, dlgSmallEm = 10.8f;

    // FL's dialog button: dark frame, lighter top edge, darkening fill
    void paintDialogButton (juce::Graphics& g, juce::Rectangle<int> button, const juce::String& label, bool hover, bool down)
    {
        const auto b = button.toFloat();
        g.setColour (juce::Colour (0xff181b1c));
        g.fillRect (b);
        g.setColour (juce::Colour (0xff343839));
        g.fillRect (b.reduced (1.0f));
        const float lift = down ? -0.06f : hover ? 0.06f : 0.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2e2f).brighter (lift), 0.0f, b.getY() + 2.0f,
                                                 juce::Colour (0xff202425).brighter (lift), 0.0f, b.getBottom() - 1.0f, false));
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
            editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1c2226));
            editor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
            editor.setColour (juce::TextEditor::outlineColourId, colour::amber);
            editor.setColour (juce::TextEditor::focusedOutlineColourId, colour::amber);
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
        g.setColour (sel ? (highlightStyle ? colour::amber : colour::textGreen)
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
