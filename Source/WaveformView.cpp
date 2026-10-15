#include "WaveformView.h"
#include "FlStyle.h"

namespace
{
    constexpr float grabDistance = 5.0f;
    constexpr float flagH = 13.0f;
}

WaveformView::WaveformView (ChanSamplerProcessor& p) : processor (p)
{
    startTimerHz (60);
}

void WaveformView::setSample (SampleData::Ptr newSample)
{
    sample = std::move (newSample);
    repaint();
}

float WaveformView::getValue (const char* id) const
{
    return processor.apvts.getRawParameterValue (id)->load();
}

void WaveformView::setValue (const char* id, float value)
{
    auto* p = processor.apvts.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 1.0f, value)));
}

juce::Rectangle<float> WaveformView::waveArea() const   { return getLocalBounds().toFloat().withTrimmedBottom (scrollH); }
juce::Rectangle<float> WaveformView::scrollArea() const { return getLocalBounds().toFloat().removeFromBottom (scrollH); }

float WaveformView::fracToX (float frac) const
{
    const auto a = waveArea();
    return a.getX() + (frac - viewStart) / (viewEnd - viewStart) * a.getWidth();
}

float WaveformView::xToFrac (float x) const
{
    const auto a = waveArea();
    return juce::jlimit (0.0f, 1.0f, viewStart + (x - a.getX()) / juce::jmax (1.0f, a.getWidth()) * (viewEnd - viewStart));
}

const char* WaveformView::markerParam (Marker m) const
{
    switch (m)
    {
        case Marker::start:     return pid::startOffset;
        case Marker::loopStart: return pid::loopStart;
        case Marker::loopEnd:   return pid::loopEnd;
        default:                break;
    }
    return nullptr;
}

WaveformView::Marker WaveformView::markerAt (juce::Point<float> p) const
{
    if (scrollArea().contains (p))
        return Marker::scrollThumb;

    // Only markers that are shown can be dragged (FL: Start once the start
    // offset is above 0, the loop once END is above 0).
    Marker best = Marker::none;
    float bestDist = grabDistance;
    const bool startShown = getValue (pid::startOffset) > 0.0f;
    const bool loopShown = getValue (pid::loopEnd) > 0.0f && ! (sample != nullptr && sample->autoLoop);
    for (auto m : { Marker::start, Marker::loopStart, Marker::loopEnd })
    {
        if ((m == Marker::start && ! startShown) || (m != Marker::start && ! loopShown))
            continue;
        const auto x = fracToX (getValue (markerParam (m)));
        // Flags extend the grab area: Start at the top, Loop at the bottom.
        const bool onFlag = (m == Marker::start ? p.y < flagH + 2.0f : p.y > waveArea().getBottom() - flagH - 2.0f)
                         && p.x >= x - 2.0f && p.x <= x + 30.0f;
        const auto d = onFlag ? 0.0f : std::abs (x - p.x);
        if (d < bestDist)
        {
            bestDist = d;
            best = m;
        }
    }
    return best;
}

void WaveformView::timerCallback()
{
    const float now[4] = { getValue (pid::startOffset), getValue (pid::loopStart),
                           getValue (pid::loopEnd), getValue (pid::useLoop) + (sample != nullptr && sample->autoLoop ? 2.0f : 0.0f) };
    const auto ph = processor.getPlayhead();
    if (std::memcmp (now, lastMarkers, sizeof (now)) != 0 || ph != playhead)
    {
        std::memcpy (lastMarkers, now, sizeof (now));
        playhead = ph;
        repaint();
    }
}

void WaveformView::drawLanes (juce::Graphics& g, juce::Rectangle<float> area, float from, float to, float alpha) const
{
    // FL draws one lane: the peaks of both channels together, with a
    // brighter core and pink edges.
    const auto halfH = area.getHeight() * 0.5f;
    const auto mid = area.getCentreY();
    const int buckets = SampleData::numPeakBuckets;
    for (int x = 0; x < (int) area.getWidth(); ++x)
    {
        const auto f0 = from + (to - from) * (float) x / area.getWidth();
        const auto f1 = from + (to - from) * (float) (x + 1) / area.getWidth();
        const auto b0 = juce::jlimit (0, buckets - 1, (int) (f0 * buckets));
        const auto b1 = juce::jlimit (b0 + 1, buckets, (int) (f1 * buckets));
        float lo = 0, hi = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int b = b0; b < b1; ++b)
            {
                lo = juce::jmin (lo, sample->peakMin[ch][(size_t) b]);
                hi = juce::jmax (hi, sample->peakMax[ch][(size_t) b]);
            }
        // Like FL, a boosted sample is drawn clipped at full scale.
        lo = juce::jmax (lo, -1.0f);
        hi = juce::jmin (hi, 1.0f);
        const auto px = area.getX() + (float) x;
        // FL: a thin, light waveform on the dark red view.
        g.setColour (fl::colour::waveCream.withAlpha (alpha * 0.85f));
        g.drawVerticalLine ((int) px, mid - hi * halfH * 0.95f, mid - lo * halfH * 0.95f + 1.0f);
    }
    // Centre line
    g.setColour (fl::colour::waveCream.withAlpha (alpha * 0.9f));
    g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());
}

void WaveformView::drawSamples (juce::Graphics& g, juce::Rectangle<float> area, float from, float to) const
{
    // One lane (the mid of both channels): a line through the samples with
    // a dot on each, as FL draws a sample when zoomed in this far.
    const int total = sample->getTotalFrames();
    const auto halfH = area.getHeight() * 0.5f;
    const auto mid = area.getCentreY();
    const int first = juce::jmax (0, (int) std::floor (from * total) - 1);
    const int last = juce::jmin (total - 1, (int) std::ceil (to * total) + 1);
    const auto* l = sample->audio.getReadPointer (0);
    const auto* r = sample->audio.getReadPointer (juce::jmin (1, sample->audio.getNumChannels() - 1));
    auto pt = [&] (int i)
    {
        const auto x = area.getX() + ((float) i / (float) total - from) / (to - from) * area.getWidth();
        return juce::Point<float> (x, mid - juce::jlimit (-1.0f, 1.0f, 0.5f * (l[i] + r[i])) * halfH * 0.95f);
    };
    juce::Path line;
    for (int i = first; i <= last; ++i)
        i == first ? line.startNewSubPath (pt (i)) : line.lineTo (pt (i));
    g.setColour (fl::colour::waveCream);
    g.strokePath (line, juce::PathStrokeType (1.0f));
    const auto spacing = area.getWidth() / juce::jmax (1.0f, (to - from) * (float) total);
    if (spacing >= 6.0f)
        for (int i = first; i <= last; ++i)
        {
            const auto p = pt (i);
            g.setColour (fl::colour::well);
            g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
            g.setColour (fl::colour::waveCream);
            g.drawEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f, 1.0f);
        }
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto wave = waveArea();
    g.setColour (fl::colour::waveBg);
    g.fillRect (getLocalBounds());

    // Faint grid
    g.setColour (fl::colour::waveGrid);
    for (int i = 1; i < 16; ++i)
        g.drawVerticalLine ((int) (wave.getX() + wave.getWidth() * (float) i / 16.0f), wave.getY(), wave.getBottom());

    auto drawWatermark = [&g, wave]
    {
        g.setColour (fl::colour::watermark);
        g.setFont (fl::font (22.0f));
        g.drawText ("Sampler", wave.reduced (8.0f, 4.0f), juce::Justification::topRight, false);
    };

    if (sample == nullptr || sample->playEnd <= 0)
    {
        drawWatermark();
        g.setColour (fl::colour::dim);
        g.setFont (fl::font (12.0f));
        g.drawText ("Drag a sample here or click the folder button", wave, juce::Justification::centred, false);
        return;
    }

    // A crossfade without loop points makes FL loop from the crossfade point to
    // the end; FL shows that loop with its markers as well.
    const bool userLoop = getValue (pid::useLoop) > 0.5f;
    const bool autoLoop = ! userLoop && sample->autoLoop && sample->getTotalFrames() > 0;
    // FL (measured): the Start marker shows once the start offset is above 0,
    // the loop once END is above 0 (START = END draws a single line), or the
    // crossfade loop. FL draws the loop red even with Use loop points off;
    // here an unused loop is drawn grey.
    const bool startShown = getValue (pid::startOffset) > 0.0f;
    const bool loopShown = autoLoop || getValue (pid::loopEnd) > 0.0f;
    const bool looping = userLoop || autoLoop;
    const auto ls = fracToX (autoLoop ? (float) sample->autoLoopStart / (float) sample->getTotalFrames() : getValue (pid::loopStart));
    const auto le = fracToX (autoLoop ? (float) sample->playEnd / (float) sample->getTotalFrames() : juce::jmax (getValue (pid::loopStart), getValue (pid::loopEnd)));
    const auto sx = fracToX (getValue (pid::startOffset));
    // FL's loop colours (sampled): START line / tab / label pure red, END a
    // darker red, the region a dark red tint. Grey versions when not looping.
    const auto startCol = looping ? juce::Colour (0xffff0000) : juce::Colour (0xff7a8084);
    const auto endCol   = looping ? juce::Colour (0xffac2b27) : juce::Colour (0xff5a5f62);

    if (loopShown)
    {
        g.setColour (looping ? juce::Colour (0xff2e1011) : juce::Colour (0xff2c2f31));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (ls, wave.getY(), juce::jmax (ls + 1.0f, le), wave.getBottom()));
    }
    drawWatermark();

    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (wave.toNearestInt());
        // Zoomed in to single samples (or a very short sample), FL draws
        // each sample as a dot joined by lines instead of the peak overview.
        const auto visibleFrames = (viewEnd - viewStart) * (float) sample->getTotalFrames();
        if (visibleFrames < wave.getWidth() / 2.0f)
            drawSamples (g, wave, viewStart, viewEnd);
        else
            drawLanes (g, wave, viewStart, viewEnd, 1.0f);
    }
    if (startShown)
    {
        g.setColour (juce::Colours::black.withAlpha (0.6f));   // FL darkens the part before Start
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (wave.getX(), wave.getY(), juce::jmax (wave.getX(), sx), wave.getBottom()));

        // Start flag (gold, top)
        g.setColour (fl::colour::startGold.withAlpha (0.8f));
        g.drawVerticalLine ((int) sx, wave.getY(), wave.getBottom());
        g.setColour (fl::colour::startGold);
        g.fillRect (sx, wave.getY(), 4.0f, flagH);
        g.setFont (fl::font (11.5f));
        g.drawText ("Start", juce::Rectangle<float> (sx + 6.0f, wave.getY(), 40.0f, flagH), juce::Justification::centredLeft, false);
    }

    if (loopShown)
    {
        // FL's loop flags: a 1 px line over the full height at each end and a
        // 4 px tab at the bottom (14 px, top corner cut), the END tab facing
        // inwards; "Loop" beside the START tab when there is room. START = END
        // is a single line.
        const auto bottom = wave.getBottom();
        constexpr float tabH = 14.0f, tabW = 4.0f, cut = 3.0f;
        const auto x0 = std::floor (ls), x1 = juce::jmax (x0, std::floor (le) - 1.0f);

        g.setColour (endCol);
        g.fillRect (x1, wave.getY(), 1.0f, wave.getHeight());
        juce::Path endTab;
        endTab.addQuadrilateral (x1 + 1.0f, bottom - tabH, x1 + 1.0f, bottom, x1 + 1.0f - tabW, bottom, x1 + 1.0f - tabW, bottom - tabH + cut);
        g.fillPath (endTab);

        g.setColour (startCol);
        g.fillRect (x0, wave.getY(), 1.0f, wave.getHeight());
        juce::Path startTab;
        startTab.addQuadrilateral (x0, bottom - tabH, x0 + tabW, bottom - tabH + cut, x0 + tabW, bottom, x0, bottom);
        g.fillPath (startTab);

        const auto label = fl::font (11.0f);
        const auto labelW = juce::GlyphArrangement::getStringWidth (label, "Loop");
        if (x1 - x0 >= labelW + 12.0f)
        {
            g.setFont (label);
            g.drawText ("Loop", juce::Rectangle<float> (x0 + 7.0f, bottom - tabH, labelW + 2.0f, tabH), juce::Justification::centredLeft, false);
        }
    }

    // Playback bar (FL: an orange line with a soft glow while a note plays)
    if (playhead >= 0.0f)
    {
        const auto px = fracToX (playhead);
        if (px >= wave.getX() && px <= wave.getRight())
        {
            for (int k = 3; k >= 1; --k)
            {
                g.setColour (fl::colour::playOrange.withAlpha (0.12f * (float) (4 - k)));
                g.fillRect (px - (float) k * 1.5f, wave.getY(), (float) k * 3.0f, wave.getHeight());
            }
            g.setColour (fl::colour::playOrange);
            g.fillRect (px - 0.75f, wave.getY(), 1.5f, wave.getHeight());
        }
    }

    // Zoom / scroll bar: whole-sample overview with the visible window outlined
    const auto sb = scrollArea();
    g.setColour (juce::Colour (0xff282a2b));
    g.fillRect (sb);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (sb.reduced (12.0f, 1.0f).toNearestInt());
        drawLanes (g, sb.reduced (12.0f, 1.0f), 0.0f, 1.0f, 0.6f);
    }
    const auto inner = sb.reduced (12.0f, 0.5f);
    const auto thumb = juce::Rectangle<float>::leftTopRightBottom (inner.getX() + viewStart * inner.getWidth(), inner.getY(),
                                                                   inner.getX() + viewEnd * inner.getWidth(), inner.getBottom());
    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.drawRect (thumb, 1.0f);
    if (playhead >= 0.0f)
    {
        g.setColour (fl::colour::playOrange);
        g.fillRect (inner.getX() + playhead * inner.getWidth() - 1.0f, inner.getY(), 2.0f, inner.getHeight());
    }
    g.setColour (fl::colour::dim);
    g.setFont (fl::font (10.0f));
    g.drawText ("<", sb.withWidth (12.0f), juce::Justification::centred, false);
    g.drawText (">", sb.withTrimmedLeft (sb.getWidth() - 12.0f), juce::Justification::centred, false);
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const auto m = markerAt (e.position);
    setMouseCursor (m == Marker::none ? juce::MouseCursor::NormalCursor
                                      : m == Marker::scrollThumb ? juce::MouseCursor::DraggingHandCursor
                                                                 : juce::MouseCursor::LeftRightResizeCursor);
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Zoom around the mouse position
    const auto anchor = xToFrac (e.position.x);
    const auto factor = w.deltaY > 0 ? 0.8f : 1.25f;
    // Zoom down to about 16 samples across the view, as FL does.
    const auto minLen = sample != nullptr && sample->getTotalFrames() > 0 ? juce::jmin (1.0f, 16.0f / (float) sample->getTotalFrames()) : 0.002f;
    const auto newLen = juce::jlimit (minLen, 1.0f, (viewEnd - viewStart) * factor);
    const auto rel = (anchor - viewStart) / (viewEnd - viewStart);
    viewStart = juce::jlimit (0.0f, 1.0f - newLen, anchor - rel * newLen);
    viewEnd = viewStart + newLen;
    repaint();
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    dragging = markerAt (e.position);
    if (dragging == Marker::scrollThumb)
    {
        const auto inner = scrollArea().reduced (12.0f, 0.0f);
        dragOffset = (e.position.x - inner.getX()) / inner.getWidth() - viewStart;
        if (dragOffset < 0.0f || dragOffset > viewEnd - viewStart)
            dragOffset = (viewEnd - viewStart) * 0.5f;   // clicked outside the thumb: centre it
        mouseDrag (e);
        return;
    }

    if (dragging != Marker::none)
    {
        dragOffset = getValue (pid::loopEnd) - getValue (pid::loopStart);
        for (auto id : { pid::startOffset, pid::loopStart, pid::loopEnd })
            processor.apvts.getParameter (id)->beginChangeGesture();
        return;
    }

    if (sample != nullptr && sample->playEnd > 0)
    {
        previewNote = (int) getValue (pid::rootNote);
        processor.requestPreview (previewNote, true);
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Marker::none)
        return;

    if (dragging == Marker::scrollThumb)
    {
        const auto inner = scrollArea().reduced (12.0f, 0.0f);
        const auto len = viewEnd - viewStart;
        viewStart = juce::jlimit (0.0f, 1.0f - len, (e.position.x - inner.getX()) / inner.getWidth() - dragOffset);
        viewEnd = viewStart + len;
        repaint();
        return;
    }

    const auto frac = xToFrac (e.position.x);

    // The processor applies FL's link: START pushes END, END stops at START.
    if (dragging == Marker::loopStart)    { setValue (pid::loopStart, frac); processor.applyLoopLink (pid::loopStart); }
    else if (dragging == Marker::loopEnd) { setValue (pid::loopEnd, frac);   processor.applyLoopLink (pid::loopEnd); }
    else                                  setValue (pid::startOffset, frac);
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (dragging != Marker::none && dragging != Marker::scrollThumb)
        for (auto id : { pid::startOffset, pid::loopStart, pid::loopEnd })
            processor.apvts.getParameter (id)->endChangeGesture();
    dragging = Marker::none;

    if (previewNote >= 0)
    {
        processor.requestPreview (previewNote, false);
        previewNote = -1;
    }
}
