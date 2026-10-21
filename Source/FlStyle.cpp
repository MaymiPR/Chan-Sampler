#include "FlStyle.h"

namespace fl
{
juce::Font font (float height, bool bold)
{
    // FL draws its UI with Segoe UI on Windows.
    // Heights here are JUCE font heights (ascent + descent); FL's sizes are
    // em sizes, about 1.2 times smaller, so scale up to match.
    const auto style = bold ? juce::Font::bold : juce::Font::plain;
   #if JUCE_WINDOWS
    return juce::Font (juce::FontOptions ("Segoe UI", height * 1.2f, style));
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
    return juce::Font (juce::FontOptions (height * 1.2f, style)).withHorizontalScale (bold ? boldScale : plainScale);
   #endif
}

namespace
{
    juce::Point<float> onCircle (juce::Point<float> c, float r, float angle)
    {
        return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) };
    }

    // FL's round switch: a dark well with a grey ring; on, an orange light.
    void drawLed (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool enabled, bool green = false)
    {
        const auto alpha = enabled ? 1.0f : 0.45f;
        g.setColour (colour::ledRing.withMultipliedAlpha (alpha));
        g.fillEllipse (r);
        g.setColour (juce::Colour (0xff131516).withMultipliedAlpha (alpha));
        g.fillEllipse (r.reduced (1.0f));

        const auto inner = r.reduced (r.getWidth() * 0.2f);
        if (on)
        {
            const auto core = green ? juce::Colour (0xffe4ffd8) : colour::ledGlow;
            const auto edge = green ? juce::Colour (0xff8fe86a) : colour::ledOn;
            juce::ColourGradient grad (core.withMultipliedAlpha (alpha), inner.getCentreX(), inner.getCentreY() - inner.getHeight() * 0.15f,
                                       edge.withMultipliedAlpha (alpha), inner.getRight(), inner.getBottom(), true);
            g.setGradientFill (grad);
            g.fillEllipse (inner);
        }
        else
        {
            g.setColour (colour::ledOff.withMultipliedAlpha (alpha));
            g.fillEllipse (inner);
        }
    }
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, colour::menuBg);
    setColour (juce::PopupMenu::textColourId, colour::menuText);
    setColour (juce::PopupMenu::headerTextColourId, colour::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff4b4b48));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::ComboBox::textColourId, colour::title);
    setColour (juce::Label::textColourId, colour::text);
    setColour (juce::TextButton::textColourOffId, colour::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
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
    g.setFont (font (9.5f));
    g.drawText (name, area.withTrimmedLeft (4), juce::Justification::centredLeft, true);
}

void LookAndFeel::getIdealPopupMenuSectionHeaderSizeWithOptions (const juce::String& text, int, int& w, int& h,
                                                                 const juce::PopupMenu::Options& o)
{
    w = (int) juce::GlyphArrangement::getStringWidth (font (9.5f), text) + 10;
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

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const auto r = d * 0.5f;
    const auto ringT = juce::jmax (1.5f, r * 0.12f);
    const auto bodyR = r - ringT * 0.75f;
    const auto alpha = slider.isEnabled() ? 1.0f : 0.4f;
    const bool big = d > 50.0f;

    // Tick marks around the large MOD X / MOD Y knobs
    if (big)
    {
        g.setColour (colour::dim.withAlpha (0.6f * alpha));
        for (int i = 0; i <= 10; ++i)
        {
            const auto a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            g.drawLine (juce::Line<float> (onCircle (c, r + 0.5f, a), onCircle (c, r + 3.5f, a)), 1.0f);
        }
    }

    // Body: FL's near-black knob with a dark rim and a soft top light.
    g.setColour (colour::knobRim.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f + 2.0f, bodyR * 2.0f + 2.0f).withCentre (c));
    juce::ColourGradient body (colour::knobTop.withMultipliedAlpha (alpha), c.x - bodyR * 0.3f, c.y - bodyR * 0.6f,
                               colour::knobBottom.withMultipliedAlpha (alpha), c.x + bodyR * 0.4f, c.y + bodyR, true);
    if (big)
    {
        body = juce::ColourGradient (juce::Colour (0xff3a3d40).withMultipliedAlpha (alpha), c.x - bodyR * 0.3f, c.y - bodyR * 0.4f,
                                     juce::Colour (0xff0f1011).withMultipliedAlpha (alpha), c.x + bodyR, c.y + bodyR, true);
    }
    g.setGradientFill (body);
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.05f * alpha));
    g.drawEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c).reduced (0.5f), 0.8f);
    if (big)
    {
        g.setColour (juce::Colour (0xff1c1e20).withMultipliedAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (bodyR * 1.35f, bodyR * 1.35f).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.08f * alpha));
        g.drawEllipse (juce::Rectangle<float> (bodyR * 1.35f, bodyR * 1.35f).withCentre (c), 1.0f);
    }

    // Value ring as in FL: from the minimum for one-sided knobs, from the
    // centre for two-sided ones (pitch, EQ, panning...).
    const auto ringColour = slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (alpha);
    const bool bipolar = slider.isDoubleClickReturnEnabled()
                      && std::abs (slider.valueToProportionOfLength (slider.getDoubleClickReturnValue()) - 0.5) < 1.0e-3;
    const auto defaultPos = bipolar ? 0.5f : 0.0f;
    const auto angle = startAngle + pos * (endAngle - startAngle);
    if (! big && std::abs (pos - defaultPos) > 0.002f)
    {
        const auto defAngle = startAngle + defaultPos * (endAngle - startAngle);
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r - ringT * 0.5f, r - ringT * 0.5f, 0.0f,
                           juce::jmin (angle, defAngle), juce::jmax (angle, defAngle), true);
        g.setColour (ringColour);
        g.strokePath (arc, juce::PathStrokeType (ringT, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }

    // Pointer dot
    const auto dotR = juce::jmax (1.1f, bodyR * (big ? 0.06f : 0.09f));
    const auto dot = onCircle (c, bodyR * (big ? 0.82f : 0.72f), angle);
    g.setColour (big ? juce::Colour (0xff7dff9e).withMultipliedAlpha (alpha) : ringColour);
    g.fillEllipse (juce::Rectangle<float> (dotR * 2.0f, dotR * 2.0f).withCentre (dot));
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float,
                                    float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    // Horizontal tick-mark slider (fine tune, tracking mid, formant)
    const auto alpha = slider.isEnabled() ? 1.0f : 0.4f;
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto midY = area.getCentreY();
    const auto left = area.getX() + 6.0f, right = area.getRight() - 6.0f;

    g.setColour (colour::dim.withAlpha (0.7f * alpha));
    for (int i = 0; i <= 20; ++i)
    {
        const auto tx = left + (right - left) * (float) i / 20.0f;
        const auto len = (i % 5 == 0) ? 4.0f : 2.5f;
        g.drawVerticalLine ((int) tx, midY + 2.0f, midY + 2.0f + len);
    }
    g.setColour (colour::well.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (left, midY - 1.5f, right - left, 3.0f, 1.5f);

    const auto defaultPos = slider.isDoubleClickReturnEnabled()
                              ? (float) slider.valueToProportionOfLength (slider.getDoubleClickReturnValue()) : 0.0f;
    const auto defX = left + (right - left) * defaultPos;
    g.setColour (colour::tickPink.withMultipliedAlpha (alpha));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (defX, pos), midY - 1.5f, juce::jmax (defX, pos), midY + 1.5f));

    const auto thumbR = juce::jmin (9.0f, area.getHeight() * 0.45f);
    const auto thumb = juce::Rectangle<float> (thumbR * 2.0f, thumbR * 2.0f).withCentre ({ pos, midY });
    g.setColour (juce::Colours::black.withAlpha (0.4f * alpha));
    g.fillEllipse (thumb.translated (0.0f, 1.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9ba2a7).withMultipliedAlpha (alpha), thumb.getCentreX(), thumb.getY(),
                                             juce::Colour (0xff353b3f).withMultipliedAlpha (alpha), thumb.getCentreX(), thumb.getBottom(), false));
    g.fillEllipse (thumb);
    g.setColour (juce::Colour (0xff23282b).withMultipliedAlpha (alpha));
    g.drawVerticalLine ((int) pos, thumb.getY() + 3.0f, thumb.getBottom() - 3.0f);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    const auto bounds = b.getLocalBounds().toFloat();
    const auto ledSize = juce::jmin (bounds.getHeight() - 4.0f, 13.0f);
    // "textFirst": FL's label-then-LED switches (envelope / LFO Tempo, Global)
    const bool textFirst = (bool) b.getProperties()["textFirst"];
    const auto f = font (juce::jmin (11.5f, bounds.getHeight() * 0.6f));
    const auto textW = juce::GlyphArrangement::getStringWidth (f, b.getButtonText());
    const auto ledX = textFirst ? juce::jmin (bounds.getRight() - ledSize - 2.0f, bounds.getX() + textW + 6.0f) : bounds.getX() + 2.0f;
    const auto led = juce::Rectangle<float> (ledSize, ledSize).withPosition (ledX, bounds.getCentreY() - ledSize * 0.5f);
    drawLed (g, led, b.getToggleState(), b.isEnabled(), (bool) b.getProperties()["greenLed"]);

    if (b.getButtonText().isNotEmpty())
    {
        g.setColour (b.isEnabled() ? colour::text : colour::dim);
        g.setFont (f);
        const auto textArea = textFirst ? bounds.withRight (led.getX() - 4.0f) : bounds.withTrimmedLeft (ledSize + 7.0f);
        g.drawText (b.getButtonText(), textArea, juce::Justification::centredLeft, true);
    }
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    // FL's drop-down: a dark field with a thin outline and a small right arrow.
    const auto alpha = box.isEnabled() ? 1.0f : 0.45f;
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (colour::comboBg.withMultipliedAlpha (alpha));
    g.fillRect (r);
    g.setColour (colour::panelEdge.withMultipliedAlpha (alpha));
    g.drawRect (r, 1.0f);

    // Right-pointing arrow; it points down while the list is open (as in FL).
    juce::Path arrow;
    const auto ax = (float) w - 8.0f, ay = (float) h * 0.5f;
    if (box.isPopupActive())
        arrow.addTriangle (ax - 3.5f, ay - 1.5f, ax + 3.5f, ay - 1.5f, ax, ay + 2.5f);
    else
        arrow.addTriangle (ax - 2.0f, ay - 3.5f, ax - 2.0f, ay + 3.5f, ax + 2.0f, ay);
    g.setColour (colour::dim.withMultipliedAlpha (alpha));
    g.fillPath (arrow);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jmin (12.0f, (float) box.getHeight() * 0.55f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (2, 1, box.getWidth() - 16, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    // FL's flat dark button (Reset, Cut self).
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto bg = b.getToggleState() ? colour::amber : colour::buttonBg;
    if (highlighted) bg = bg.brighter (0.15f);
    if (down)        bg = bg.darker (0.2f);
    g.setColour (bg.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
    g.fillRect (r);
    g.setColour (colour::panelEdge);
    g.drawRect (r, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (juce::jmin (11.5f, (float) buttonHeight * 0.55f));
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
    return font (12.0f);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (colour::well);
    g.setColour (colour::panelEdge);
    g.drawRect (0, 0, w, h);
    g.setColour (colour::text);
    g.setFont (font (13.0f));
    g.drawFittedText (text, 6, 2, w - 12, h - 4, juce::Justification::centredLeft, 3);
}

//==============================================================================
Panel::Panel (const juce::String& t, int inset) : title (t), headerInset (inset)
{
}

void MenuArrow::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    juce::Path p;
    p.addTriangle (c.x - 2.0f, c.y - 3.5f, c.x - 2.0f, c.y + 3.5f, c.x + 2.5f, c.y);
    g.setColour (highlighted || down ? colour::title : colour::text);
    g.fillPath (p);
}

void Panel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (colour::panel);
    g.fillRect (r);
    g.setColour (colour::panelEdge);
    g.drawRect (r, 1.0f);

    if (title.isEmpty() && headerInset == 0)
        return;

    // Header row as in FL: the title on a panel-coloured tab with a slanted
    // end; the rest of the row shows the darker sheet colour.
    constexpr float headerH = 20.0f;
    const auto f = font (13.0f);
    const auto titleX = (float) headerInset + (title.isEmpty() ? 0.0f : 6.0f);
    const auto tabEnd = titleX + juce::GlyphArrangement::getStringWidth (f, title) + 8.0f;

    juce::Path back;
    back.startNewSubPath (tabEnd, 0.0f);
    back.lineTo (r.getRight(), 0.0f);
    back.lineTo (r.getRight(), headerH);
    back.lineTo (tabEnd + 10.0f, headerH);
    back.closeSubPath();
    g.setColour (colour::sheet);
    g.fillPath (back);
    g.setColour (colour::panelEdge);
    g.drawLine (tabEnd, 0.0f, tabEnd + 10.0f, headerH, 1.0f);
    g.drawLine (tabEnd + 10.0f, headerH, r.getRight(), headerH, 1.0f);

    g.setColour (colour::title);
    g.setFont (f);
    g.drawText (title, juce::Rectangle<float> (titleX, 0.0f, tabEnd - titleX, headerH), juce::Justification::centredLeft, false);
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
    const auto alpha = isEnabled() ? 1.0f : 0.5f;
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    if (red)
    {
        g.setGradientFill (juce::ColourGradient (colour::digiRed.brighter (0.15f).withMultipliedAlpha (alpha), 0.0f, r.getY(),
                                                 colour::digiRed.darker (0.25f).withMultipliedAlpha (alpha), 0.0f, r.getBottom(), false));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (colour::digiGrey.brighter (0.1f).withMultipliedAlpha (alpha), 0.0f, r.getY(),
                                                 colour::digiGrey.darker (0.2f).withMultipliedAlpha (alpha), 0.0f, r.getBottom(), false));
    }
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f * alpha));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    // Small up/down chevrons on the right
    g.setColour (juce::Colours::white.withAlpha (0.35f * alpha));
    const auto cx = r.getRight() - 5.0f;
    g.drawLine (cx - 2.0f, r.getCentreY() - 3.0f, cx, r.getCentreY() - 5.0f, 1.0f);
    g.drawLine (cx, r.getCentreY() - 5.0f, cx + 2.0f, r.getCentreY() - 3.0f, 1.0f);
    g.drawLine (cx - 2.0f, r.getCentreY() + 3.0f, cx, r.getCentreY() + 5.0f, 1.0f);
    g.drawLine (cx, r.getCentreY() + 5.0f, cx + 2.0f, r.getCentreY() + 3.0f, 1.0f);

    const auto text = fixedText.isNotEmpty() ? fixedText : getTextFromValue (getValue());
    g.setColour (juce::Colours::white.withMultipliedAlpha (alpha));
    g.setFont (font (r.getHeight() * (red ? 0.8f : 0.62f)));
    g.drawText (text, r.withTrimmedRight (7.0f), juce::Justification::centred, false);
}

//==============================================================================
void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> a, juce::Colour c)
{
    g.setColour (c);
    const auto s = juce::jmin (a.getWidth(), a.getHeight());
    const auto b = juce::Rectangle<float> (s, s).withCentre (a.getCentre());
    const auto stroke = juce::jmax (1.2f, s * 0.08f);
    auto px = [&b] (float fx, float fy) { return juce::Point<float> (b.getX() + fx * b.getWidth(), b.getY() + fy * b.getHeight()); };

    switch (icon)
    {
        case Icon::sampler:
        {
            static const float hts[] = { 0.1f, 0.3f, 0.55f, 0.25f, 0.95f, 0.45f, 0.8f, 0.3f, 0.6f, 0.2f, 0.1f };
            juce::Path p;
            p.startNewSubPath (px (0.0f, 0.5f));
            for (int i = 0; i < 11; ++i)
                p.lineTo (px (0.05f + 0.09f * (float) i, 0.5f + ((i % 2) ? 0.5f : -0.5f) * hts[i]));
            p.lineTo (px (1.0f, 0.5f));
            g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case Icon::envelope:
        {
            const juce::Point<float> pts[] = { px (0.15f, 0.85f), px (0.42f, 0.15f), px (0.85f, 0.85f) };
            juce::Path p;
            p.startNewSubPath (pts[0]);
            p.lineTo (pts[1]);
            p.quadraticTo (px (0.55f, 0.8f), pts[2]);
            g.strokePath (p, juce::PathStrokeType (stroke));
            for (auto& pt : pts)
                g.drawEllipse (juce::Rectangle<float> (s * 0.2f, s * 0.2f).withCentre (pt), stroke);
            break;
        }
        case Icon::wrench:
        {
            juce::Path head;
            const auto hc = px (0.66f, 0.34f);
            head.addCentredArc (hc.x, hc.y, s * 0.2f, s * 0.2f, 0.0f, 1.2f, juce::MathConstants<float>::twoPi - 0.4f, true);
            g.strokePath (head, juce::PathStrokeType (s * 0.13f));
            g.drawLine (juce::Line<float> (px (0.52f, 0.48f), px (0.18f, 0.84f)), s * 0.15f);
            break;
        }
        case Icon::valve:
        {
            g.drawRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (px (0.3f, 0.1f).x, px (0.3f, 0.1f).y,
                                                                               px (0.7f, 0.7f).x, px (0.7f, 0.7f).y), s * 0.18f, stroke);
            g.drawLine (juce::Line<float> (px (0.42f, 0.55f), px (0.42f, 0.3f)), stroke * 0.8f);
            g.drawLine (juce::Line<float> (px (0.58f, 0.55f), px (0.58f, 0.3f)), stroke * 0.8f);
            for (float fx : { 0.38f, 0.5f, 0.62f })
                g.drawLine (juce::Line<float> (px (fx, 0.72f), px (fx, 0.92f)), stroke * 0.8f);
            break;
        }
        case Icon::sine:
        {
            juce::Path p;
            for (int i = 0; i <= 24; ++i)
            {
                const auto t = (float) i / 24.0f;
                const auto pt = px (0.05f + 0.9f * t, 0.5f - 0.35f * std::sin (t * juce::MathConstants<float>::twoPi));
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;
        }
        case Icon::triangle:
        {
            juce::Path p;
            p.startNewSubPath (px (0.05f, 0.5f));
            p.lineTo (px (0.28f, 0.15f));
            p.lineTo (px (0.72f, 0.85f));
            p.lineTo (px (0.95f, 0.5f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;
        }
        case Icon::square:
        {
            juce::Path p;
            p.startNewSubPath (px (0.05f, 0.85f));
            p.lineTo (px (0.05f, 0.15f));
            p.lineTo (px (0.5f, 0.15f));
            p.lineTo (px (0.5f, 0.85f));
            p.lineTo (px (0.95f, 0.85f));
            p.lineTo (px (0.95f, 0.15f));
            g.strokePath (p, juce::PathStrokeType (stroke));
            break;
        }
        case Icon::folder:
        case Icon::locate:
        case Icon::close:
            break;   // file buttons only: IconButton draws them pixel by pixel
    }
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // FL's file buttons (measured pixel by pixel from FL 2026): a 1 px dark
    // frame, a 1 px lighter inner edge, a fill darkening downwards, and thin
    // 1 px line icons at fixed positions.
    const auto b = getLocalBounds().toFloat();
    const auto w = b.getWidth(), h = b.getHeight();
    auto shade = [highlighted, down] (juce::Colour c) { return down ? c.darker (0.15f) : highlighted ? c.brighter (0.2f) : c; };
    g.setColour (juce::Colour (0xff141717));
    g.fillRect (b);
    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xff282b2c)), 0.0f, 1.0f, shade (juce::Colour (0xff1e2121)), 0.0f, h - 1.0f, false));
    g.fillRect (b.reduced (1.0f));
    g.setGradientFill (juce::ColourGradient (shade (juce::Colour (0xff202324)), 0.0f, 1.0f, shade (juce::Colour (0xff171a1a)), 0.0f, h - 1.0f, false));
    g.fillRect (b.reduced (2.0f));

    const auto c = isEnabled() ? juce::Colour (0xffdfe4e5) : colour::dim;
    g.setColour (c);
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
            drawIcon (g, icon, b.reduced (h * 0.22f), c);
            break;
    }
}

//==============================================================================
void showContextMenu (juce::PopupMenu& menu, juce::Component& target, bool atMouse)
{
    target.getProperties().set (contextMenuProperty, true);
    menu.setLookAndFeel (&target.getLookAndFeel());   // otherwise JUCE uses its default look
    auto options = juce::PopupMenu::Options().withTargetComponent (&target);
    menu.showMenuAsync (atMouse ? options.withMousePosition() : options);
}

namespace
{
    // FL's dialog colours (measured from FL 2026's dialogs)
    const juce::Colour dlgBg { 0xff202324 }, dlgEdge { 0xff16191a }, dlgPanel { 0xff282c2d }, dlgPanelEdge { 0xff1d2021 },
                       dlgTitle { 0xffd4dadd }, dlgText { 0xffd0d8da }, dlgSign { 0xff98a2a8 }, dlgButtonText { 0xffabb7b7 };

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
        g.setFont (font (12.0f));
        g.drawText (label, button, juce::Justification::centred, false);
    }

    void paintDialogFrame (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& title, juce::Rectangle<int> closeBox, bool closeHover)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.25f));   // keeps the eye on the dialog
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (box.translated (2, 3));
        g.setColour (dlgEdge);
        g.fillRect (box);
        g.setColour (dlgBg);
        g.fillRect (box.reduced (1));
        g.setColour (dlgTitle);
        g.setFont (font (12.5f));
        g.drawText (title, box.getX() + 6, box.getY(), box.getWidth() - 34, 25, juce::Justification::centredLeft, true);
        const auto cc = closeBox.toFloat().reduced (4.5f);
        g.setColour (closeHover ? juce::Colours::white : dlgTitle);
        g.drawLine (cc.getX(), cc.getY(), cc.getRight(), cc.getBottom(), 1.3f);
        g.drawLine (cc.getRight(), cc.getY(), cc.getX(), cc.getBottom(), 1.3f);
    }

    class Dialog : public juce::Component
    {
    public:
        Dialog (const juce::String& t, const juce::String& m, bool w, InfoRows r = {})
            : title (t), message (m), warning (w), rows (std::move (r))
        {
            setWantsKeyboardFocus (true);
        }

        void resized() override
        {
            if (! rows.empty())
            {
                layoutTable();
                return;
            }
            constexpr int titleH = 25, signW = 46, buttonH = 31;
            const int boxW = juce::jmin (380, getWidth() - 24);
            juce::AttributedString as;
            as.append (message, font (12.5f), dlgText);
            as.setWordWrap (juce::AttributedString::byWord);
            text.createLayout (as, (float) (boxW - 8 - signW - 16));
            const int panelH = juce::jmax (64, (int) std::ceil (text.getHeight()) + 32);
            const int boxH = titleH + 1 + panelH + 12 + buttonH + 12;
            box = juce::Rectangle<int> (boxW, juce::jmin (boxH, getHeight() - 12)).withCentre (getLocalBounds().getCentre());
            panel = juce::Rectangle<int> (box.getX() + 4, box.getY() + titleH + 1, boxW - 8, panelH);
            textArea = panel.toFloat().withTrimmedLeft ((float) signW).reduced (8.0f, 16.0f);
            button = juce::Rectangle<int> (116, buttonH).withCentre ({ box.getCentreX(), panel.getBottom() + 12 + buttonH / 2 });
            closeBox = juce::Rectangle<int> (box.getRight() - 22, box.getY() + 4, 17, 17);
        }

        void paint (juce::Graphics& g) override
        {
            paintDialogFrame (g, box, title, closeBox, closeHover);

            g.setColour (dlgPanelEdge);
            g.fillRect (panel);
            g.setColour (dlgPanel);
            g.fillRect (panel.reduced (1));
            if (rows.empty())
            {
                g.setColour (dlgSign);
                g.setFont (font (40.0f));
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
                close();
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::returnKey || k == juce::KeyPress::escapeKey || k == juce::KeyPress::spaceKey)
            {
                close();
                return true;
            }
            return false;
        }

    private:
        static constexpr int labelW = 104, ruleH = 13;

        void layoutTable()
        {
            constexpr int titleH = 25, buttonH = 31;
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
                    as.append (value, font (12.0f), dlgText);
                    as.setWordWrap (juce::AttributedString::byChar);
                    tl.createLayout (as, valueW);
                    panelH += juce::jmax (20, (int) std::ceil (tl.getHeight()) + 5);
                }
                else
                    panelH += ruleH;
                layouts.push_back (std::move (tl));
            }
            panelH += 10;
            const int boxH = titleH + 1 + panelH + 12 + buttonH + 12;
            box = juce::Rectangle<int> (boxW, juce::jmin (boxH, getHeight() - 12)).withCentre (getLocalBounds().getCentre());
            panel = juce::Rectangle<int> (box.getX() + 4, box.getY() + titleH + 1, boxW - 8, juce::jmin (panelH, box.getHeight() - titleH - buttonH - 26));
            button = juce::Rectangle<int> (116, buttonH).withCentre ({ box.getCentreX(), panel.getBottom() + 12 + buttonH / 2 });
            closeBox = juce::Rectangle<int> (box.getRight() - 22, box.getY() + 4, 17, 17);
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
                g.setFont (font (12.0f));
                g.drawText (label, x0, y, labelW - 8, 20, juce::Justification::centredLeft, true);
                layouts[i].draw (g, juce::Rectangle<float> ((float) xv, (float) y + 2.5f, (float) (panel.getRight() - 12 - xv), (float) h));
                y += h;
            }
        }

        void close()
        {
            setVisible (false);
            juce::Component::SafePointer<Dialog> self (this);
            juce::MessageManager::callAsync ([self] { delete self.getComponent(); });
        }

        juce::String title, message;
        bool warning;
        InfoRows rows;
        std::vector<juce::TextLayout> layouts;
        juce::TextLayout text;
        juce::Rectangle<int> box, panel, button, closeBox;
        juce::Rectangle<float> textArea;
        bool buttonHover = false, buttonDown = false, closeHover = false;
    };

    // FL's question dialog with a column of choices (Tempo detection).
    class ChoiceDialog : public juce::Component
    {
    public:
        ChoiceDialog (const juce::String& t, const juce::String& m, const juce::StringArray& c, std::function<void (int)> f)
            : title (t), message (m), choices (c), onChoice (std::move (f))
        {
            setWantsKeyboardFocus (true);
        }

        void resized() override
        {
            constexpr int titleH = 25, signW = 70, buttonW = 104, buttonH = 23, gap = 4;   // FL: 31 / 6, but our window is smaller
            const int boxW = juce::jmin (546, getWidth() - 24);
            juce::AttributedString as;
            as.append (message, font (12.5f), dlgText);
            as.setWordWrap (juce::AttributedString::byWord);
            text.createLayout (as, (float) (boxW - 8 - signW - 16));
            const int panelH = juce::jmax (64, (int) std::ceil (text.getHeight()) + 24);
            const int listH = choices.size() * (buttonH + gap) - gap;
            const int boxH = titleH + 1 + panelH + 12 + listH + 10;
            box = juce::Rectangle<int> (boxW, juce::jmin (boxH, getHeight() - 12)).withCentre (getLocalBounds().getCentre());
            panel = juce::Rectangle<int> (box.getX() + 4, box.getY() + titleH + 1, boxW - 8, panelH);
            textArea = panel.toFloat().withTrimmedLeft ((float) signW).reduced (8.0f, 12.0f);
            buttons.clear();
            for (int i = 0; i < choices.size(); ++i)
                buttons.push_back (juce::Rectangle<int> (buttonW, buttonH)
                                       .withCentre ({ box.getCentreX(), panel.getBottom() + 12 + i * (buttonH + gap) + buttonH / 2 }));
            closeBox = juce::Rectangle<int> (box.getRight() - 22, box.getY() + 4, 17, 17);
        }

        void paint (juce::Graphics& g) override
        {
            paintDialogFrame (g, box, title, closeBox, closeHover);
            g.setColour (dlgPanelEdge);
            g.fillRect (panel);
            g.setColour (dlgPanel);
            g.fillRect (panel.reduced (1));
            g.setColour (dlgSign);
            g.setFont (font (64.0f));
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
            setVisible (false);
            auto f = std::move (onChoice);
            juce::Component::SafePointer<ChoiceDialog> self (this);
            juce::MessageManager::callAsync ([self, f, choice]
            {
                delete self.getComponent();
                if (f != nullptr && choice >= 0)
                    f (choice);
            });
        }

        juce::String title, message;
        juce::StringArray choices;
        std::function<void (int)> onChoice;
        juce::TextLayout text;
        juce::Rectangle<int> box, panel, closeBox;
        juce::Rectangle<float> textArea;
        std::vector<juce::Rectangle<int>> buttons;
        int hover = -1, down = -1;
        bool closeHover = false;
    };

    // A one-line entry in FL's dialog style (the Type in choices).
    class TextDialog : public juce::Component
    {
    public:
        TextDialog (const juce::String& t, const juce::String& p, const juce::String& initial, std::function<void (const juce::String&)> f)
            : title (t), prompt (p), onText (std::move (f))
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
            constexpr int titleH = 25, buttonH = 31;
            const int boxW = juce::jmin (300, getWidth() - 24);
            box = juce::Rectangle<int> (boxW, titleH + 1 + 86 + 12 + buttonH + 12).withCentre (getLocalBounds().getCentre());
            panel = juce::Rectangle<int> (box.getX() + 4, box.getY() + titleH + 1, boxW - 8, 86);
            editor.setBounds (juce::Rectangle<int> (140, 24).withCentre ({ panel.getCentreX(), panel.getY() + 58 }));
            button = juce::Rectangle<int> (116, buttonH).withCentre ({ box.getCentreX(), panel.getBottom() + 12 + buttonH / 2 });
            closeBox = juce::Rectangle<int> (box.getRight() - 22, box.getY() + 4, 17, 17);
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
            paintDialogFrame (g, box, title, closeBox, closeHover);
            g.setColour (dlgPanelEdge);
            g.fillRect (panel);
            g.setColour (dlgPanel);
            g.fillRect (panel.reduced (1));
            g.setColour (dlgText);
            g.setFont (font (12.5f));
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
            setVisible (false);
            auto f = std::move (onText);
            const auto t = editor.getText();
            juce::Component::SafePointer<TextDialog> self (this);
            juce::MessageManager::callAsync ([self, f, t, apply]
            {
                delete self.getComponent();
                if (apply && f != nullptr)
                    f (t);
            });
        }

        juce::String title, prompt;
        std::function<void (const juce::String&)> onText;
        juce::TextEditor editor;
        juce::Rectangle<int> box, panel, button, closeBox;
        bool buttonHover = false, buttonDown = false, closeHover = false;
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
        auto cell = juce::Rectangle<float> (cellW * (float) i, 0.0f, cellW, (float) getHeight());
        const bool sel = (int) i == selected;
        if (sel)
        {
            g.setColour (large ? colour::sheet : colour::well);
            if (large) g.fillRect (cell);
            else       g.fillRoundedRectangle (cell.reduced (1.0f), 2.0f);
        }
        const auto iconArea = cell.reduced (large ? cellW * 0.22f : 3.0f);
        drawIcon (g, icons[i], iconArea, sel ? selectedColour : juce::Colour (0xffc4ccd0).withAlpha (isEnabled() ? 1.0f : 0.5f));
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
        const int w = (int) juce::GlyphArrangement::getStringWidth (f, items[i]) + 10;
        if (i == index)
            return { x, 0, w, getHeight() };
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
        // FL: the selected item is coloured text (green on a dark tab for the
        // INS targets, orange for the small selectors); no button box.
        if (sel && ! highlightStyle)
        {
            g.setColour (colour::sheet);
            g.fillRect (r.reduced (0.0f, 2.0f));
        }
        g.setColour (sel ? (highlightStyle ? colour::amber : colour::green)
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
