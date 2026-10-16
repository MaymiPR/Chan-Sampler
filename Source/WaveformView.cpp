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

juce::Rectangle<float> WaveformView::waveArea() const   { return getLocalBounds().toFloat().withTrimmedTop (topH).withTrimmedBottom (scrollH); }
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
        if (ph >= 0.0f && playhead >= 0.0f && ph != playhead)
            playDirection = ph > playhead ? 1.0f : -1.0f;
        playhead = ph;
        repaint();
    }
}

// FL 2026's sample view colours (sampled from FL screenshots).
namespace flview
{
    const juce::Colour frame      { 0xff202324 };   // strip above the view, around the bar
    const juce::Colour bg         { 0xff1b1d1e };
    const juce::Colour grid       { 0xff131516 };   // 0.1 s (or 10x finer / coarser) and the +/-50% lines
    const juce::Colour gridMajor  { 0xff090b0c };   // whole seconds (10x the fine grid)
    const juce::Colour waveMono   { 0xfff6feff };
    const juce::Colour waveStereo { 0xfff2eded };
    const juce::Colour watermark  { 0xff41464b };
    const juce::Colour barDark    { 0xff101213 };
    const juce::Colour barLight   { 0xff313334 };
    const juce::Colour track      { 0xff282a2b };
    const juce::Colour overview   { 0xffa3a5a6 };
    const juce::Colour arrow      { 0xff6a6d6f };
}

juce::Colour WaveformView::waveColour() const
{
    return sample != nullptr && sample->mono ? flview::waveMono : flview::waveStereo;
}

void WaveformView::drawLanes (juce::Graphics& g, juce::Rectangle<float> area, float from, float to, juce::Colour colour) const
{
    // FL draws one smooth, anti-aliased shape: per pixel column the lowest and
    // highest value of both channels (measured: within ~1 px of FL's capture),
    // the space between a smooth upper and lower edge filled, at least 1.5 px
    // thick, so a zoomed-in wave is a clean ribbon. Values come from the audio
    // itself up to 8192 frames per column, from the peak table beyond that.
    const auto halfH = area.getHeight() * 0.5f - 1.0f;
    const auto mid = area.getCentreY();
    const int total = sample->getTotalFrames();
    const int width = (int) area.getWidth();
    if (total <= 0 || width <= 0)
        return;
    const auto framesPerColumn = (double) (to - from) * total / width;
    const auto* l0 = sample->audio.getReadPointer (0);
    const auto* r0 = sample->audio.getReadPointer (juce::jmin (1, sample->audio.getNumChannels() - 1));

    // Zoomed in (under 100 frames per pixel) FL shows a clean line through the
    // wave, not its peaks: a 1.5 px anti-aliased line through points every half
    // pixel (both channels averaged).
    if (framesPerColumn < 100.0)
    {
        juce::Path line;
        const auto step = 0.5f;
        bool started = false;
        for (float px = 0.0f; px <= (float) width; px += step)
        {
            const auto f = from + (to - from) * px / area.getWidth();
            if (f > 1.0f)
                break;
            const int i = juce::jlimit (0, total - 1, (int) std::lround (f * total));
            const auto pt = juce::Point<float> (area.getX() + px, mid - juce::jlimit (-1.0f, 1.0f, 0.5f * (l0[i] + r0[i])) * halfH);
            if (! started) { line.startNewSubPath (pt); started = true; }
            else           line.lineTo (pt);
        }
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }
    const int buckets = SampleData::numPeakBuckets;
    const auto* l = sample->audio.getReadPointer (0);
    const auto* r = sample->audio.getReadPointer (juce::jmin (1, sample->audio.getNumChannels() - 1));
    auto y = [&] (float v) { return mid - juce::jlimit (-1.0f, 1.0f, v) * halfH; };

    std::vector<juce::Point<float>> upper, lower;
    upper.reserve ((size_t) width);
    lower.reserve ((size_t) width);
    for (int x = 0; x < width; ++x)
    {
        const auto f0 = from + (to - from) * (float) x / area.getWidth();
        const auto f1 = from + (to - from) * (float) (x + 1) / area.getWidth();
        if (f0 >= 1.0f)
            break;
        float lo = 0, hi = 0;
        if (framesPerColumn <= 8192.0)
        {
            const int i0 = juce::jlimit (0, total - 1, (int) std::floor (f0 * total));
            const int i1 = juce::jlimit (i0 + 1, total, (int) std::floor (f1 * total));
            lo = hi = l[i0];
            for (int i = i0; i < i1; ++i)
                for (auto v : { l[i], r[i] })
                {
                    lo = juce::jmin (lo, v);
                    hi = juce::jmax (hi, v);
                }
        }
        else
        {
            const auto b0 = juce::jlimit (0, buckets - 1, (int) (f0 * buckets));
            const auto b1 = juce::jlimit (b0 + 1, buckets, (int) (f1 * buckets));
            for (int ch = 0; ch < 2; ++ch)
                for (int b = b0; b < b1; ++b)
                {
                    lo = juce::jmin (lo, sample->peakMin[ch][(size_t) b]);
                    hi = juce::jmax (hi, sample->peakMax[ch][(size_t) b]);
                }
        }
        const auto px = area.getX() + (float) x + 0.5f;
        const auto yHi = y (hi), yLo = y (lo);
        const auto centre = 0.5f * (yHi + yLo), half = juce::jmax (0.75f, 0.5f * (yLo - yHi) + 0.5f);
        upper.push_back ({ px, centre - half });
        lower.push_back ({ px, centre + half });
    }
    if (upper.empty())
        return;
    juce::Path shape;
    shape.startNewSubPath (upper.front().translated (-0.5f, 0.0f));
    for (auto& p : upper)
        shape.lineTo (p);
    shape.lineTo (upper.back().translated (0.5f, 0.0f));
    shape.lineTo (lower.back().translated (0.5f, 0.0f));
    for (auto it = lower.rbegin(); it != lower.rend(); ++it)
        shape.lineTo (*it);
    shape.lineTo (lower.front().translated (-0.5f, 0.0f));
    shape.closeSubPath();
    g.setColour (colour);
    g.fillPath (shape);
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
        return juce::Point<float> (x, mid - juce::jlimit (-1.0f, 1.0f, 0.5f * (l[i] + r[i])) * (halfH - 1.0f));
    };
    juce::Path line;
    for (int i = first; i <= last; ++i)
        i == first ? line.startNewSubPath (pt (i)) : line.lineTo (pt (i));
    g.setColour (waveColour());
    g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto spacing = area.getWidth() / juce::jmax (1.0f, (to - from) * (float) total);
    if (spacing >= 6.0f)
        for (int i = first; i <= last; ++i)
        {
            const auto p = pt (i);
            g.setColour (fl::colour::well);
            g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
            g.setColour (waveColour());
            g.drawEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f, 1.0f);
        }
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto wave = waveArea();
    g.fillAll (flview::frame);
    g.setColour (flview::bg);
    g.fillRect (wave);

    // FL's grid (measured): dark lines every 0.1 s of the sample, every second
    // darker still (the step grows or shrinks 10x to keep the lines at least
    // 20 px apart), and two lines at +/-50% of full scale.
    auto drawGrid = [&]
    {
        g.setColour (flview::grid);
        g.fillRect (wave.getX(), std::floor (wave.getCentreY() - wave.getHeight() * 0.25f), wave.getWidth(), 1.0f);
        g.fillRect (wave.getX(), std::floor (wave.getCentreY() + wave.getHeight() * 0.25f), wave.getWidth(), 1.0f);
        const double seconds = sample != nullptr && sample->getTotalFrames() > 0 ? sample->getTotalFrames() / sample->sourceRate : 1.0;
        const double t0 = viewStart * seconds, t1 = viewEnd * seconds;
        const double pps = wave.getWidth() / juce::jmax (1.0e-9, t1 - t0);
        double step = 0.0001;
        while (step * pps < 20.0)
            step *= 10.0;
        for (auto k = (long long) std::ceil (t0 / step - 1.0e-6); (double) k * step <= t1 + 1.0e-9; ++k)
        {
            const auto x = (float) std::floor (wave.getX() + ((double) k * step - t0) * pps);
            if (x >= wave.getRight())
                break;
            g.setColour (k % 10 == 0 ? flview::gridMajor : flview::grid);
            g.fillRect (x, wave.getY(), 1.0f, wave.getHeight());
        }
    };

    auto drawWatermark = [&g, wave]
    {
        g.setColour (flview::watermark);
        g.setFont (fl::font (22.0f));
        g.drawText ("Sampler", wave.reduced (8.0f, 4.0f), juce::Justification::topRight, false);
    };

    if (sample == nullptr || sample->playEnd <= 0)
    {
        drawGrid();
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
    drawGrid();
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
            drawLanes (g, wave, viewStart, viewEnd, waveColour());
    }

    // FL's format badge (bottom right): the bit depth it holds the sample at
    // (16 for 16-bit files, otherwise 32-bit float) and a channel icon with
    // one notch for mono, two for stereo (measured with 16 / 24 / 32-bit files).
    {
        const auto iconR = juce::Rectangle<float> (wave.getRight() - 15.0f, wave.getBottom() - 13.0f, 9.0f, 9.0f);
        g.setColour (juce::Colour (0xffdfe5e8));
        g.drawRect (iconR.withTrimmedTop (1.0f), 1.0f);
        if (sample->mono)
            g.fillRect (iconR.getCentreX() - 1.0f, iconR.getY() - 1.0f, 2.0f, 2.0f);
        else
        {
            g.fillRect (iconR.getX() - 0.5f, iconR.getY() - 1.0f, 2.0f, 2.0f);
            g.fillRect (iconR.getRight() - 1.5f, iconR.getY() - 1.0f, 2.0f, 2.0f);
        }
        g.setFont (fl::font (11.5f));
        g.drawText (sample->fileBits <= 16 ? "16" : "32", juce::Rectangle<float> (iconR.getX() - 26.0f, iconR.getY() - 2.0f, 21.0f, 13.0f),
                    juce::Justification::centredRight, false);
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

    // Playback bar (sampled from FL): a 1 px line of #eb6d2e with an 11 px
    // trail behind it, 35% next to the line and fading by 0.71 per pixel.
    if (playhead >= 0.0f)
    {
        const auto px = std::floor (fracToX (playhead));
        if (px >= wave.getX() && px <= wave.getRight())
        {
            const juce::Colour line (0xffeb6d2e);
            float alpha = 0.351f;
            for (int k = 1; k <= 11; ++k, alpha *= 0.71f)
            {
                const auto x = px - playDirection * (float) k;
                if (x >= wave.getX() && x < wave.getRight())
                {
                    g.setColour (line.withAlpha (alpha));
                    g.fillRect (x, wave.getY(), 1.0f, wave.getHeight());
                }
            }
            g.setColour (line);
            g.fillRect (px, wave.getY(), 1.0f, wave.getHeight());
        }
    }

    // Zoom / scroll bar (FL's look): dark and light edges, the whole sample's
    // overview in grey, arrow buttons; the visible window between two bright edges.
    const auto sb = scrollArea();
    g.setColour (flview::barDark);
    g.fillRect (sb.getX(), sb.getY(), sb.getWidth(), 1.0f);
    g.setColour (flview::barLight);
    g.fillRect (sb.getX(), sb.getY() + 1.0f, sb.getWidth(), 1.0f);
    g.fillRect (sb.getX(), sb.getBottom() - 1.0f, sb.getWidth(), 1.0f);
    const auto trackArea = sb.withTrimmedTop (2.0f).withTrimmedBottom (1.0f);
    g.setColour (flview::track);
    g.fillRect (trackArea);
    const auto inner = trackArea.reduced (buttonW, 0.0f);
    {
        // FL: outside the visible window the track is dark and finely hatched;
        // inside it the overview shows, between two lighter edges.
        const auto t0 = std::floor (inner.getX() + viewStart * inner.getWidth());
        const auto t1 = std::ceil (inner.getX() + viewEnd * inner.getWidth());
        for (int row = 0; row < (int) inner.getHeight(); ++row)
        {
            g.setColour (row % 2 == 0 ? juce::Colour (0xff191b1c) : juce::Colour (0xff1c1e1f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (inner.getX(), inner.getY() + (float) row, t0, inner.getY() + (float) row + 1.0f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (t1, inner.getY() + (float) row, inner.getRight(), inner.getY() + (float) row + 1.0f));
        }
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (t0, inner.getY(), t1, inner.getBottom()).toNearestInt());
        drawLanes (g, inner.reduced (0.0f, 1.0f), 0.0f, 1.0f, flview::overview);
        if (viewStart > 0.0f || viewEnd < 1.0f)
        {
            g.setColour (flview::barLight);
            g.fillRect (t0, inner.getY(), 1.0f, inner.getHeight());
            g.fillRect (t1 - 1.0f, inner.getY(), 1.0f, inner.getHeight());
        }
    }
    if (playhead >= 0.0f)
    {
        g.setColour (fl::colour::playOrange);
        g.fillRect (inner.getX() + playhead * inner.getWidth() - 1.0f, inner.getY(), 2.0f, inner.getHeight());
    }
    // Arrow buttons: small chevrons
    g.setColour (flview::arrow);
    for (int side = 0; side < 2; ++side)
    {
        const auto cx = side == 0 ? trackArea.getX() + buttonW * 0.5f : trackArea.getRight() - buttonW * 0.5f;
        const auto cy = trackArea.getCentreY();
        const auto d = side == 0 ? 1.0f : -1.0f;
        juce::Path chevron;
        chevron.startNewSubPath (cx + d * 1.5f, cy - 3.5f);
        chevron.lineTo (cx - d * 1.5f, cy);
        chevron.lineTo (cx + d * 1.5f, cy + 3.5f);
        g.strokePath (chevron, juce::PathStrokeType (1.2f));
    }
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
    // Zoom around the mouse position. A mouse wheel zooms one step per notch.
    // A trackpad (smooth scrolling, as on a Mac) sends a stream of small
    // deltas plus momentum, which made clicks zoom by accident: it zooms in
    // proportion to the finger travel, only once a deliberate gesture has
    // built up, and never during or right after a click.
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (e.mods.isAnyMouseButtonDown() || nowMs - lastPressMs < 400.0)
        return;

    float factor;
    if (w.isSmooth)
    {
        if (w.isInertial || std::abs (w.deltaX) > std::abs (w.deltaY))
            return;
        if (nowMs - lastSmoothMs > 250.0)   // a new gesture
        {
            smoothScroll = 0.0f;
            smoothZooming = false;
        }
        lastSmoothMs = nowMs;
        if (! smoothZooming)
        {
            smoothScroll += w.deltaY;
            if (std::abs (smoothScroll) < 0.15f)
                return;
            smoothZooming = true;
        }
        factor = std::exp2 (-1.5f * w.deltaY);   // a full two-finger swipe zooms a few times
    }
    else
    {
        if (w.deltaY == 0.0f)
            return;
        factor = w.deltaY > 0 ? 0.8f : 1.25f;
    }

    const auto anchor = xToFrac (e.position.x);
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
    lastPressMs = juce::Time::getMillisecondCounterHiRes();
    smoothZooming = false;
    smoothScroll = 0.0f;
    dragging = markerAt (e.position);
    if (dragging == Marker::scrollThumb)
    {
        const auto inner = scrollArea().reduced (buttonW, 0.0f);
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
        const auto inner = scrollArea().reduced (buttonW, 0.0f);
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
    lastPressMs = juce::Time::getMillisecondCounterHiRes();   // no zoom from scroll right after a click
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
