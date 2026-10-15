#include "FlStyle.h"

namespace fl
{
juce::Font font (float height, bool bold)
{
    // FL draws its UI with Segoe UI on Windows.
    // Heights here are JUCE font heights (ascent + descent); FL's sizes are
    // em sizes, about 1.2 times smaller, so scale up to match.
    return juce::Font (juce::FontOptions ("Segoe UI", height * 1.2f, bold ? juce::Font::bold : juce::Font::plain));
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
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
    {
        auto copy = f;
        copy.setTypefaceName ("Segoe UI");
        return juce::Typeface::createSystemTypefaceFor (copy);
    }
    return juce::LookAndFeel_V4::getTypefaceForFont (f);
}

namespace
{
    bool isDropDown (const juce::PopupMenu::Options& o)
    {
        return dynamic_cast<juce::ComboBox*> (o.getTargetComponent()) != nullptr;
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
        h = 5;
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
        // FL: a full-width dark rule (2 px in the knob menus)
        g.setColour (dd ? juce::Colours::black.withAlpha (0.4f) : rmHeader);
        if (dd) g.fillRect (area.withHeight (1).withY (area.getCentreY()).withTrimmedLeft (2).withTrimmedRight (2));
        else    g.fillRect (area.withHeight (2).withY (area.getCentreY() - 1));
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
        {
            juce::Path p;
            p.startNewSubPath (px (0.1f, 0.25f));
            p.lineTo (px (0.4f, 0.25f));
            p.lineTo (px (0.48f, 0.35f));
            p.lineTo (px (0.9f, 0.35f));
            p.lineTo (px (0.9f, 0.82f));
            p.lineTo (px (0.1f, 0.82f));
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (stroke));
            if (icon == Icon::locate)
            {
                g.drawLine (juce::Line<float> (px (0.5f, 0.45f), px (0.5f, 0.72f)), stroke);
                g.drawLine (juce::Line<float> (px (0.5f, 0.72f), px (0.65f, 0.72f)), stroke);
            }
            break;
        }
        case Icon::close:
            g.drawLine (juce::Line<float> (px (0.25f, 0.25f), px (0.75f, 0.75f)), stroke * 1.2f);
            g.drawLine (juce::Line<float> (px (0.75f, 0.25f), px (0.25f, 0.75f)), stroke * 1.2f);
            break;
    }
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    auto bg = juce::Colour (0xff353c40);
    if (highlighted) bg = bg.brighter (0.1f);
    if (down)        bg = bg.darker (0.2f);
    g.setColour (bg.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 2.5f);
    g.setColour (juce::Colour (0xff23282b));
    g.drawRoundedRectangle (r, 2.5f, 1.0f);
    drawIcon (g, icon, r.reduced (r.getHeight() * 0.22f), (isEnabled() ? colour::title : colour::dim));
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
