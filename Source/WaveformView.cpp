#include "WaveformView.h"
#include "FlStyle.h"
#include "SampleExchange.h"
#include "TempoDetect.h"
#include "TimeStretch.h"
#include <thread>

namespace
{
    constexpr float grabDistance = 5.0f;
    constexpr float flagH = 13.0f;
}

WaveformView::WaveformView (ChanSamplerProcessor& p) : processor (p)
{
    startTimerHz (60);
}

WaveformView::~WaveformView()
{
    ++renderGeneration;   // a running render stops at its next check
    renderPool.removeAllJobs (true, 10000);
}

void WaveformView::setSample (SampleData::Ptr newSample)
{
    fileSample = std::move (newSample);
    sample = fileSample;
    shownKey = {};
    requestedKey = {};
    ++renderGeneration;
    updateStretchPreview();
    repaint();
}

//==============================================================================
namespace
{
    // The sample as a note at the root key plays it with these stretch
    // settings, at the sample's own rate (the engine's own maths: pitch ratio
    // from PITCH, speed from TIME / MUL; Slice map resamples, changing both).
    SampleData::Ptr renderStretched (const SampleData& s, StretchMode mode, double cents, double speed, float formant,
                                     const std::atomic<int>& generation, int myGeneration)
    {
        const int inFrames = juce::jmax (1, s.playEnd);
        const double ratio = std::pow (2.0, cents / 1200.0);
        const bool resample = mode == StretchMode::sliceMap;
        const double sourceStep = resample ? speed * ratio : speed;   // source frames per output frame
        const auto outFrames = (int) juce::jlimit (1.0, 30.0 * 60.0 * s.sourceRate, std::round ((double) inFrames / sourceStep));

        SampleData::Ptr out = new SampleData();
        out->sourceRate = s.sourceRate;
        out->mono = s.mono;
        out->fileBits = 32;   // FL's badge shows its stretched copy as 32-bit (float), the plain file as it is
        out->lengthTrimmed = s.lengthTrimmed;
        out->audio.setSize (2, outFrames);
        out->playEnd = outFrames;
        const double scale = (double) outFrames / (double) inFrames;
        auto scaled = [scale] (int frame) { return (int) std::round (frame * scale); };
        out->autoLoop = s.autoLoop;
        out->autoLoopStart = scaled (s.autoLoopStart);
        out->loopStartShift = scaled (s.loopStartShift);
        for (auto f : s.onsets)      out->onsets.push_back (scaled (f));
        for (auto f : s.fileMarkers) out->fileMarkers.push_back (scaled (f));

        const auto* srcL = s.audio.getReadPointer (0);
        const auto* srcR = s.audio.getReadPointer (s.audio.getNumChannels() > 1 ? 1 : 0);
        auto* l = out->audio.getWritePointer (0);
        auto* r = out->audio.getWritePointer (1);
        if (resample)
        {
            for (int i = 0; i < outFrames; ++i)
            {
                const auto pos = i * sourceStep;
                const int k = (int) pos;
                const auto f = (float) (pos - k);
                const int a = juce::jmin (k, inFrames - 1), b = juce::jmin (k + 1, inFrames - 1);
                l[i] = srcL[a] + f * (srcL[b] - srcL[a]);
                r[i] = srcR[a] + f * (srcR[b] - srcR[a]);
            }
        }
        else
        {
            TimeStretcher stretcher;
            stretcher.configure (mode, s.sourceRate);
            stretcher.reset (0.0);
            auto read = [&] (int k, float& sl, float& sr)
            {
                if (k < 0 || k >= inFrames) { sl = sr = 0.0f; return; }
                sl = srcL[k];
                sr = srcR[k];
            };
            for (int i = 0; i < outFrames; ++i)
            {
                if ((i & 4095) == 0 && generation.load() != myGeneration)
                    return nullptr;   // the settings changed again
                stretcher.next (read, ratio, speed, formant, l[i], r[i]);
            }
        }
        out->computePeaks();
        return out;
    }
}

WaveformView::StretchKey WaveformView::stretchKey() const
{
    StretchKey k;
    k.source = fileSample.get();
    auto get = [this] (const char* id) { return getValue (id); };
    k.cents = get (pid::pitch);
    k.mul = get (pid::stretchMul);
    k.time = get (pid::stretchTime);
    k.formant = get (pid::formant);
    k.bpm = k.time > 0.01f ? displayBpm.load() : 0.0;
    k.mode = (int) resolveStretchMode ((StretchMode) juce::roundToInt (get (pid::stretchMode)), k.cents, k.mul, k.time);
    return k;
}

void WaveformView::updateStretchPreview()
{
    const auto key = stretchKey();
    if (key == shownKey || key == requestedKey || fileSample == nullptr || fileSample->playEnd <= 0)
        return;
    // FL draws the precomputed modes (e3, e2, slice, Auto) as they play;
    // Resample, Stretch and Stretch Pro play in real time and FL draws the file.
    const auto keyMode = (StretchMode) key.mode;
    const bool realtime = keyMode == StretchMode::resample || keyMode == StretchMode::stretch || keyMode == StretchMode::stretchPro;
    const bool plain = realtime || (std::abs (key.cents) < 0.5f && std::abs (key.mul - 1.0f) < 1.0e-4f && key.time <= 0.01f);
    if (plain)
    {
        // Nothing stretched, or a real-time mode: the file's sample as it is.
        ++renderGeneration;
        shownKey = key;
        requestedKey = {};
        settleFrames = -1;
        if (sample != fileSample)
        {
            sample = fileSample;
            repaint();
        }
        return;
    }
    if (! (key == pendingKey) || settleFrames < 0)
    {
        pendingKey = key;
        settleFrames = 6;   // about 0.1 s after the last change
        return;
    }
    // FL redraws once the knob is let go, not while it turns.
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        settleFrames = 6;
        return;
    }
    if (--settleFrames > 0)
        return;
    settleFrames = -1;

    const auto generation = ++renderGeneration;
    requestedKey = key;
    SampleData::Ptr source = fileSample;
    const auto mode = (StretchMode) key.mode;
    const auto speed = stretchSpeed ((double) source->playEnd / source->sourceRate, key.mul, key.time, key.bpm);
    juce::Component::SafePointer<WaveformView> safe (this);
    renderPool.addJob ([this, safe, source, mode, key, speed, generation]
    {
        auto rendered = renderStretched (*source, mode, key.cents, speed, key.formant, renderGeneration, generation);
        if (rendered == nullptr)
            return;
        juce::MessageManager::callAsync ([safe, rendered, key, generation]
        {
            if (safe == nullptr || safe->renderGeneration.load() != generation)
                return;
            safe->sample = rendered;
            safe->shownKey = key;
            safe->repaint();
        });
    });
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
    updateStretchPreview();

    // Twice a second: reload the sample when its file was changed (saved from
    // an audio editor it was dragged into).
    if (--fileCheckCountdown <= 0)
    {
        fileCheckCountdown = 30;
        processor.reloadIfFileChanged();
    }

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
    const juce::Colour gridMajor  { 0xff090b0c };   // the next power of ten (whole seconds)
    const juce::Colour gridMajorEdge { 0xff17191a };   // 1 px each side of it
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

void WaveformView::drawLanes (juce::Graphics& g, juce::Rectangle<float> area, float from, float to, juce::Colour colour, bool colourful) const
{
    // FL draws one smooth, anti-aliased shape: per pixel column the lowest and
    // highest value of both channels (measured: within ~1 px of FL's capture),
    // the space between a smooth upper and lower edge filled, at least 1.5 px
    // thick, so a zoomed-in wave is a clean ribbon. Values come from the audio
    // itself up to 8192 frames per column, from the peak table beyond that.
    const auto halfH = area.getHeight() * 0.5f - 1.0f;
    const auto mid = area.getCentreY();
    const int total = sample->getTotalFrames();
    // Work in physical pixels, so a Retina / high-DPI screen gets full detail.
    const auto scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int width = (int) std::ceil (area.getWidth() * scale);
    if (total <= 0 || width <= 0)
        return;
    const auto framesPerColumn = (double) (to - from) * total / area.getWidth();
    const int chL = laneChannel >= 0 ? laneChannel : 0, chR = laneChannel >= 0 ? laneChannel : juce::jmin (1, sample->audio.getNumChannels() - 1);
    const auto* l0 = sample->audio.getReadPointer (chL);
    const auto* r0 = sample->audio.getReadPointer (chR);

    // Zoomed in (under 32 frames per pixel) FL shows a clean line through the
    // wave, not its peaks: a 1.5 px anti-aliased line through points every half
    // pixel, each the average over its span (so fast detail does not alias).
    if (framesPerColumn < 32.0)
    {
        juce::Path line;
        const auto step = 0.5f / scale;
        bool started = false;
        for (float px = 0.0f; px <= area.getWidth(); px += step)
        {
            const auto f = from + (to - from) * px / area.getWidth();
            if (f > 1.0f)
                break;
            const int i = juce::jlimit (0, total - 1, (int) std::lround (f * total));
            const int span = juce::jmax (1, (int) std::lround (framesPerColumn * step));
            float sum = 0.0f;
            int used = 0;
            for (int k = juce::jmax (0, i - span / 2); k < juce::jmin (total, i - span / 2 + span); ++k, ++used)
                sum += 0.5f * (l0[k] + r0[k]);
            const auto v = used > 0 ? sum / (float) used : 0.5f * (l0[i] + r0[i]);
            const auto pt = juce::Point<float> (area.getX() + px, mid - juce::jlimit (-1.0f, 1.0f, v) * halfH);
            if (! started) { line.startNewSubPath (pt); started = true; }
            else           line.lineTo (pt);
        }
        setColourFill (g, area, from, to, colour, colourful);
        g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        return;
    }
    const int buckets = SampleData::numPeakBuckets;
    const auto* l = l0;
    const auto* r = r0;
    auto y = [&] (float v) { return mid - juce::jlimit (-1.0f, 1.0f, v) * halfH; };

    std::vector<juce::Point<float>> upper, lower;
    upper.reserve ((size_t) width);
    lower.reserve ((size_t) width);
    for (int x = 0; x < width; ++x)
    {
        const auto f0 = from + (to - from) * (float) x / (float) width;
        const auto f1 = from + (to - from) * (float) (x + 1) / (float) width;
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
            for (int ch : { chL, chR })
                for (int b = b0; b < b1; ++b)
                {
                    lo = juce::jmin (lo, sample->peakMin[ch][(size_t) b]);
                    hi = juce::jmax (hi, sample->peakMax[ch][(size_t) b]);
                }
        }
        const auto px = area.getX() + ((float) x + 0.5f) / scale;
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
    setColourFill (g, area, from, to, colour, colourful);
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
    setColourFill (g, area, from, to, waveColour(), true);
    g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto spacing = area.getWidth() / juce::jmax (1.0f, (to - from) * (float) total);
    if (spacing >= 6.0f)
        for (int i = first; i <= last; ++i)
        {
            const auto p = pt (i);
            g.setColour (fl::colour::well);
            g.fillEllipse (p.x - 2.5f, p.y - 2.5f, 5.0f, 5.0f);
            setColourFill (g, area, from, to, waveColour(), true);
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
    // darker still (the step grows or shrinks to keep the lines at least 15 px
    // apart, see below), and two lines at +/-50% of full scale.
    auto drawGrid = [&]
    {
        g.setColour (flview::grid);
        g.fillRect (wave.getX(), std::floor (wave.getCentreY() - wave.getHeight() * 0.25f), wave.getWidth(), 1.0f);
        g.fillRect (wave.getX(), std::floor (wave.getCentreY() + wave.getHeight() * 0.25f), wave.getWidth(), 1.0f);
        const double seconds = sample != nullptr && sample->getTotalFrames() > 0 ? sample->getTotalFrames() / sample->sourceRate : 1.0;
        const double t0 = viewStart * seconds, t1 = viewEnd * seconds;
        const double pps = wave.getWidth() / juce::jmax (1.0e-9, t1 - t0);
        // The finest step at least 15 px apart: powers of ten, and from 0.1 s
        // up also 2 and 5 times (FL: 0.1 s at 27 and 36 px rather than 0.05 s
        // at 18 px; 0.2 s where 0.1 s would be 9 px). The next power of ten
        // is drawn darker with a soft edge each side.
        double decade = 0.0001, step = decade;
        for (;;)
        {
            const bool fine = decade < 0.05;
            if (decade * pps >= 15.0)                  { step = decade;        break; }
            if (! fine && 2.0 * decade * pps >= 15.0)  { step = 2.0 * decade;  break; }
            if (! fine && 5.0 * decade * pps >= 15.0)  { step = 5.0 * decade;  break; }
            decade *= 10.0;
        }
        const double major = decade * 10.0;
        const auto perMajor = (long long) std::llround (major / step);
        for (auto k = (long long) std::ceil (t0 / step - 1.0e-6); (double) k * step <= t1 + 1.0e-9; ++k)
        {
            const auto x = (float) std::floor (wave.getX() + ((double) k * step - t0) * pps);
            if (x >= wave.getRight())
                break;
            if (k % perMajor == 0)
            {
                g.setColour (flview::gridMajorEdge);
                g.fillRect (juce::Rectangle<float> (x - 1.0f, wave.getY(), 3.0f, wave.getHeight()).getIntersection (wave));
                g.setColour (flview::gridMajor);
            }
            else
                g.setColour (flview::grid);
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
        const auto raw = processor.getRaw();
        if (raw->missing)
            g.drawFittedText ((raw->file.existsAsFile() ? "Can't read the project's sample:\n" : "The project's sample was not found:\n")
                                  + raw->file.getFullPathName() + "\nIt reloads when the file is back, or load another.",
                              wave.reduced (12.0f, 4.0f).toNearestInt(), juce::Justification::centred, 4);
        else
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
        if (spectrumView())
        {
            drawSpectrum (g, wave);
            drawWatermark();   // FL keeps it over the spectrum
        }
        else if (multichannelView() && ! sample->mono)
        {
            // Multichannel view: left above, right below, one lane each.
            auto top = wave.withHeight (wave.getHeight() * 0.5f), bottom = wave.withTrimmedTop (wave.getHeight() * 0.5f);
            for (int ch = 0; ch < 2; ++ch)
            {
                laneChannel = ch;
                drawLanes (g, ch == 0 ? top : bottom, viewStart, viewEnd, waveColour(), true);
            }
            laneChannel = -1;
            g.setColour (flview::grid);
            g.fillRect (wave.getX(), std::floor (wave.getCentreY()), wave.getWidth(), 1.0f);
        }
        else if (visibleFrames < wave.getWidth() / 2.0f)
            drawSamples (g, wave, viewStart, viewEnd);
        else
            drawLanes (g, wave, viewStart, viewEnd, waveColour(), true);
    }

    // The file's region / slice markers (when loaded): small orange tabs
    // hanging from the top of the view (FL's look, measured).
    if (! sample->fileMarkers.empty())
    {
        g.setColour (juce::Colour (0xffffb871));
        for (auto m : sample->fileMarkers)
        {
            const auto x = std::floor (fracToX ((float) m / (float) sample->getTotalFrames()));
            if (x < wave.getX() - 4.0f || x > wave.getRight())
                continue;
            juce::Path tab;
            tab.addTriangle (x, wave.getY(), x + 4.0f, wave.getY(), x, wave.getY() + 4.0f);
            g.fillPath (tab);
        }
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
        drawLanes (g, inner.reduced (0.0f, 1.0f), 0.0f, 1.0f, flview::overview, true);   // FL colours the overview too
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
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    lastPressMs = juce::Time::getMillisecondCounterHiRes();
    draggingOut = false;
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
    {
        // Dragging the waveform takes the sample's file out of the view.
        const auto file = processor.getSampleFile();
        if (previewNote >= 0 && ! draggingOut && e.getDistanceFromDragStart() > 6 && file.existsAsFile())
        {
            processor.requestPreview (previewNote, false);
            previewNote = -1;
            draggingOut = true;
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                         [safe = juce::Component::SafePointer<WaveformView> (this)]
                                                                         {
                                                                             if (safe != nullptr)
                                                                                 safe->draggingOut = false;
                                                                         });
        }
        return;
    }

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

//==============================================================================
bool WaveformView::spectrumView() const     { return (bool) processor.apvts.state.getProperty ("viewSpectrum", false); }
bool WaveformView::multichannelView() const { return (bool) processor.apvts.state.getProperty ("viewMultichannel", false); }
int WaveformView::colourMode() const        { return juce::jlimit (0, 2, (int) processor.apvts.state.getProperty ("colorfulWaveforms", 2)); }

void WaveformView::showMenu()
{
    const bool hasFile = processor.getSampleFile().existsAsFile();
    const bool hasAudio = sample != nullptr && sample->getTotalFrames() > 0;
    juce::PopupMenu menu;
    // FL's sample menu (FL 2026), without its host-only entries (audio
    // editor, pitch corrector, time warper, open in a new channel).
    // FL's underlined letters choose an entry while the menu is open; Copy and
    // Paste also answer to Ctrl+C / Ctrl+V in the window (keyShortcut).
    auto withShortcut = [] (const char* text, char key, bool enabled, std::function<void()> action)
    {
        juce::PopupMenu::Item item (text);
        item.isEnabled = enabled;
        item.action = std::move (action);
        item.shortcutKeyDescription = fl::commandShortcutText (key);
        return item;
    };
    menu.addItem ("&Reload", hasFile, false, [this] { processor.reloadSample(); });
    menu.addItem ("&Save as...", hasAudio, false, [this] { saveAs(); });
    menu.addItem ("Locate file", hasFile, false, [this] { processor.getSampleFile().revealToUser(); });
    menu.addItem (withShortcut ("&Copy", 'c', hasAudio, [this] { copySample(); }));
    menu.addItem (withShortcut ("&Paste", 'v', exchange::clipboardHasAudio(), [this] { pasteSample(); }));
    menu.addSeparator();
    menu.addItem ("&Detect tempo", hasAudio, false, [this] { detectTempo ({}); });
    menu.addItem ("&Get properties", hasAudio, false, [this] { showProperties(); });
    menu.addSeparator();
    menu.addItem ("Spectrum view", true, spectrumView(), [this]
    {
        processor.apvts.state.setProperty ("viewSpectrum", ! spectrumView(), nullptr);
        repaint();
    });
    // FL greys this out only while the spectrum view is on.
    menu.addItem ("Multichannel waveform view", ! spectrumView(), multichannelView(), [this]
    {
        processor.apvts.state.setProperty ("viewMultichannel", ! multichannelView(), nullptr);
        repaint();
    });
    // FL's Options > General settings > Colorful waveforms, kept per plugin
    // here (Multiband by default).
    juce::PopupMenu colours;
    const char* names[] = { "Off", "Color map", "Multiband" };
    for (int i = 0; i < 3; ++i)
        colours.addItem (names[i], true, colourMode() == i, [this, i]
        {
            processor.apvts.state.setProperty ("colorfulWaveforms", i, nullptr);
            repaint();
        });
    menu.addSubMenu ("Colorful waveforms", colours);
    fl::showContextMenu (menu, *this, true);
}

void WaveformView::detectTempo (std::function<void (double)> done)
{
    const auto raw = processor.getRaw();
    const int frames = raw->audio.getNumSamples();
    if (frames == 0)
        return;
    const auto rate = raw->sampleRate;
    const auto current = processor.getSampleTempo();
    const auto estimate = current > 0 ? current : tempo::detect (raw->audio, rate, 75.0, 150.0);
    const auto project = displayBpm.load();
    auto describe = [frames, rate] (double bpm)
    {
        return tempo::format (bpm) + " BPM, " + tempo::format (std::round (tempo::beats (frames, rate, bpm) * 1000.0) / 1000.0) + " beats";
    };

    juce::String message = "Choose the tempo range (BPM) this sample most likely falls in, or keep the tempo it has now.\n\n";
    message << (current > 0 ? "This sample's tempo is " + describe (current) + "."
                            : "This sample has no tempo information. If the estimate (" + describe (estimate) + ") looks wrong, choose a range.")
            << "\n\nTo play it in step with the project, choose the project's tempo. If it should not follow any tempo, choose No tempo.";

    const juce::StringArray choices { tempo::format (estimate) + (current > 0 ? " (current)" : " (estimated)"),
                                      "50 to 100", "75 to 150", "100 to 200", "150 to 300",
                                      "Type in (BPM)", "Type in (beats)", tempo::format (project) + " (project)", "No tempo" };
    const auto name = raw->file.getFileNameWithoutExtension().isNotEmpty() ? raw->file.getFileNameWithoutExtension() : juce::String ("Sample");
    juce::Component::SafePointer<WaveformView> safe (this);
    fl::showChoices (*this, "Tempo detection - " + name, message, choices,
                     [safe, raw, frames, rate, estimate, project, done] (int choice)
    {
        if (safe == nullptr)
            return;
        auto apply = [safe, done] (double bpm)
        {
            if (safe == nullptr)
                return;
            safe->processor.setSampleTempo (bpm);
            if (done != nullptr)
                done (bpm);
        };
        static const double ranges[][2] = { { 50, 100 }, { 75, 150 }, { 100, 200 }, { 150, 300 } };
        switch (choice)
        {
            case 0: apply (estimate); break;
            case 1: case 2: case 3: case 4:
                apply (tempo::detect (raw->audio, rate, ranges[choice - 1][0], ranges[choice - 1][1]));
                break;
            case 5:
                fl::askText (*safe, "Tempo detection", "Tempo (BPM)", tempo::format (estimate), [apply] (const juce::String& t)
                {
                    if (const auto bpm = t.getDoubleValue(); bpm >= 10.0 && bpm <= 999.0)
                        apply (bpm);
                });
                break;
            case 6:
                fl::askText (*safe, "Tempo detection", "Length (beats)", tempo::format (std::round (tempo::beats (frames, rate, estimate))),
                             [apply, frames, rate] (const juce::String& t)
                {
                    if (const auto n = t.getDoubleValue(); n > 0.0)
                        apply (juce::jlimit (10.0, 999.0, n * 60.0 * rate / frames));
                });
                break;
            case 7: apply (project); break;
            default: apply (0.0); break;
        }
    });
}

bool WaveformView::keyShortcut (const juce::KeyPress& key)
{
    // Windows hands Ctrl + a letter over as its control character (Ctrl+C = 3).
    const auto mods = key.getModifiers();
    if (! mods.isCommandDown() || mods.isShiftDown() || mods.isAltDown())
        return false;
    auto code = key.getKeyCode();
    if (code >= 1 && code <= 26)
        code += 'a' - 1;
    code = (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
    if (code == 'c')
    {
        if (sample != nullptr && sample->getTotalFrames() > 0)
            copySample();
        return true;
    }
    if (code == 'v')
    {
        if (exchange::clipboardHasAudio())
            pasteSample();
        return true;
    }
    return false;
}

void WaveformView::copySample()
{
    auto* peer = getPeer();
    exchange::copyToClipboard (processor.getSampleFile(), processor.getSampleAsWav(), peer != nullptr ? peer->getNativeHandle() : nullptr);
}

void WaveformView::pasteSample()
{
    // Pasted audio data (not a file) is kept as a WAV file, so projects can find it again.
    const auto file = exchange::pasteFromClipboard (ChanSamplerProcessor::userFolder ("Pasted audio"));
    if (file == juce::File())
        fl::showDialog (*this, "Chan Sampler", "The clipboard holds no audio.", true);
    else if (! processor.loadSample (file))
        fl::showDialog (*this, "Chan Sampler", processor.getLoadError(), true);
}

void WaveformView::saveAs()
{
    // The sample as Chan Sampler plays it (precomputed effects applied), 24-bit WAV.
    const auto source = processor.getSampleFile();
    const auto start = source.existsAsFile() ? source.getSiblingFile (source.getFileNameWithoutExtension() + " (processed).wav")
                                             : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Sample.wav");
    chooser = std::make_unique<juce::FileChooser> ("Save sample as", start, "*.wav");
    auto keep = sample;
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [keep, safe = juce::Component::SafePointer<WaveformView> (this)] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File() || keep == nullptr)
                                  return;
                              file = file.withFileExtension (".wav");
                              file.deleteFile();
                              juce::WavAudioFormat wav;
                              const int channels = keep->mono ? 1 : 2;
                              auto fileStream = std::make_unique<juce::FileOutputStream> (file);
                              bool saved = false;
                              if (! fileStream->failedToOpen())
                              {
                                  std::unique_ptr<juce::OutputStream> stream = std::move (fileStream);
                                  if (auto out = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (keep->sourceRate).withNumChannels (channels).withBitsPerSample (24)))
                                      saved = out->writeFromAudioSampleBuffer (keep->audio, 0, keep->playEnd);
                              }
                              if (! saved && safe != nullptr)
                                  fl::showDialog (*safe, "Chan Sampler", "Could not save " + file.getFullPathName() + ".", true);
                          });
}

void WaveformView::showProperties()
{
    // The sample's file and audio facts in an FL-styled sheet (FL itself opens
    // the system's file properties; this keeps FL's look).
    const auto raw = processor.getRaw();
    const auto& file = raw->file;
    auto number = [] (juce::int64 v)
    {
        auto t = juce::String (v);
        for (int i = t.length() - 3; i > (v < 0 ? 1 : 0); i -= 3)
            t = t.substring (0, i) + "," + t.substring (i);
        return t;
    };
    const int frames = raw->audio.getNumSamples();
    const auto seconds = raw->sampleRate > 0 ? frames / raw->sampleRate : 0.0;
    fl::InfoRows rows;
    rows.push_back ({ "Name", file.getFileName().isNotEmpty() ? file.getFileName() : juce::String ("(no file)") });
    if (file != juce::File())
        rows.push_back ({ "Location", file.getParentDirectory().getFullPathName() });
    rows.push_back ({ "Format", raw->format.isNotEmpty() ? raw->format : juce::String ("-") });
    if (file.existsAsFile())
    {
        rows.push_back ({ "Size", juce::File::descriptionOfSizeInBytes (file.getSize()) + " (" + number (file.getSize()) + " bytes)" });
        rows.push_back ({ "Modified", file.getLastModificationTime().toString (true, true, false, true) });
    }
    rows.push_back ({ {}, {} });
    rows.push_back ({ "Sample rate", number (juce::roundToInt (raw->sampleRate)) + " Hz" });
    rows.push_back ({ "Channels", raw->audio.getNumChannels() > 1 ? "Stereo" : "Mono" });
    rows.push_back ({ "Length", juce::String (seconds, 3) + " s (" + number (frames) + " samples)" });
    rows.push_back ({ "Held as", raw->heldBits <= 16 ? "16-bit" : "32-bit float" });
    if (const auto bpm = processor.getSampleTempo(); bpm > 0)
        rows.push_back ({ "Tempo", tempo::format (bpm) + " BPM (" + tempo::format (tempo::beats (frames, raw->sampleRate, bpm)) + " beats)" });
    if (raw->embeddedLoopStart >= 0 && raw->embeddedLoopEnd > raw->embeddedLoopStart)
        rows.push_back ({ "Loop points", number (raw->embeddedLoopStart) + " - " + number (raw->embeddedLoopEnd) });
    if (! raw->regionMarkers.empty() || ! raw->sliceMarkers.empty())
        rows.push_back ({ "Markers", juce::String ((int) raw->regionMarkers.size()) + " regions, " + juce::String ((int) raw->sliceMarkers.size()) + " slice markers" });
    fl::showInfo (*this, (file.getFileName().isNotEmpty() ? file.getFileName() : juce::String ("Sample")) + " properties", rows);
}

//==============================================================================
// Colorful waveforms (FL's Options > General settings), measured in FL with
// tone sweeps, two-tone mixtures, tone switches, decays, silences and FL's
// own drum, loop and vocal samples:
// - Three bands from first-order filters in a tree: low = low-pass 168 Hz;
//   the rest (high-pass 152 Hz) splits at 1284 Hz into mid and high.
// - Each band's energy is taken in blocks and follows rises at once and falls
//   smoothly: Multiband 32-frame blocks falling with a 700-frame time
//   constant, Color map 64-frame blocks and 100 frames (so FL's Color map
//   flickers on low notes, Multiband does not). Multiband's short blocks
//   show a drum's click in its own colour: 707 Kick starts blue, 909 Kick
//   blue to olive within 50 frames (FL 2026 captures of short drums, 8
//   frames per pixel; the earlier 512 came from 2 s samples, where the
//   click is under a pixel). Each pixel column reads the
//   level at its centre (a quarter block late), interpolated between blocks.
//   Stereo samples use the mean of both channels' energies.
// - Multiband: red / green / blue in proportion to the band RMS, R + G + B =
//   245 (stereo) or 257 (mono samples are drawn 5% brighter, as FL's mono
//   wave colour is). Color map: (0.4 mid + high) / (low + mid + high) placed
//   on FL's palette (red-pink, orange, cream, green, teal, blue, violet), also
//   5% brighter for mono.
// The level does not change the colour; silence keeps the last colour.
// Mean error against FL's captures: 1-2.5 RGB steps for Multiband (3.8 on a
// stereo drum loop), the Color map's flicker follows FL's in character.
namespace
{
    constexpr int bandBlock = 32;   // frames per band-energy block

    struct OnePole
    {
        float b = 0, a = 0, x1 = 0, y1 = 0;
        bool high = false;
        OnePole (double fc, double sr, bool hp) : high (hp)
        {
            const auto k = std::tan (juce::MathConstants<double>::pi * juce::jmin (fc, sr * 0.49) / sr);
            b = (float) (hp ? 1.0 / (1.0 + k) : k / (1.0 + k));
            a = (float) ((k - 1.0) / (1.0 + k));
        }
        float process (float x)
        {
            const auto y = high ? b * (x - x1) - a * y1 : b * (x + x1) - a * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    juce::Colour colourMapAt (float t, float gain)
    {
        // FL's Color map: the colours of tones from 30 Hz to 20 kHz, placed by
        // the same band balance (the first entry extends the low end).
        static const float at[] = { 0.0294f, 0.0694f, 0.1070f, 0.1394f, 0.1808f, 0.2264f, 0.2817f, 0.3401f, 0.4076f, 0.4747f, 0.5472f,
                                    0.6109f, 0.6825f, 0.7388f, 0.7983f, 0.8454f, 0.8869f, 0.9211f, 0.9465f, 0.9706f, 0.9903f };
        static const float rgb[][3] = { { 146, 60, 80 }, { 151, 66, 83 }, { 158, 71, 88 }, { 169, 83, 93 }, { 181, 97, 98 }, { 194, 117, 99 },
                                        { 203, 141, 108 }, { 211, 169, 125 }, { 221, 205, 159 }, { 225, 220, 178 }, { 217, 222, 177 },
                                        { 195, 210, 164 }, { 167, 196, 167 }, { 128, 175, 154 }, { 96, 147, 147 }, { 81, 131, 151 },
                                        { 78, 119, 151 }, { 85, 110, 147 }, { 90, 104, 145 }, { 96, 99, 143 }, { 98, 94, 140 } };
        constexpr int n = (int) std::size (at);
        int i = 0;
        while (i < n - 2 && t > at[i + 1]) ++i;
        const auto u = juce::jlimit (0.0f, 1.0f, (t - at[i]) / (at[i + 1] - at[i]));
        auto mix = [&] (int c) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (gain * (rgb[i][c] + u * (rgb[i + 1][c] - rgb[i][c])))); };
        return juce::Colour (mix (0), mix (1), mix (2));
    }

    // Band-energy followers (see above), one value per block
    void follow (const std::vector<float>* blocks, int group, double timeConstant, std::vector<float>* out)
    {
        const int nBlocks = (int) blocks[0].size();
        const int n = nBlocks / group + 1;
        const auto release = (float) std::exp (-(double) (group * bandBlock) / timeConstant);
        for (int b = 0; b < 3; ++b)
        {
            out[b].assign ((size_t) n, 0.0f);
            float acc = 0.0f;
            for (int k = 0; k < n; ++k)
            {
                float e = 0.0f;
                for (int j = k * group; j < juce::jmin (nBlocks, (k + 1) * group); ++j)
                    e += blocks[b][(size_t) j];
                e /= (float) group;
                acc = (k == 0 || e > acc) ? e : release * acc + (1.0f - release) * e;
                out[b][(size_t) k] = acc;
            }
        }
    }
}

const WaveformView::BandEnergy& WaveformView::bandEnergy (int source) const
{
    auto& b = bands[(size_t) source];
    if (b.owner == sample)
        return b;
    b.owner = sample;
    for (auto& mode : b.level)
        for (auto& e : mode)
            e.clear();
    const int total = sample->getTotalFrames();
    if (total <= 0)
        return b;
    const int chans = juce::jmin (2, sample->audio.getNumChannels());
    const auto sr = sample->sourceRate > 0 ? sample->sourceRate : 44100.0;
    const int blocks = (total + bandBlock - 1) / bandBlock;
    std::vector<float> eb[3];
    for (auto& e : eb)
        e.assign ((size_t) blocks, 0.0f);
    // Source 0: both channels (their energies averaged), 1 left, 2 right.
    const int first = source == 2 ? chans - 1 : 0, last = source == 1 ? 0 : chans - 1;
    const auto weight = 1.0f / (float) ((last - first + 1) * bandBlock);
    for (int ch = first; ch <= last; ++ch)
    {
        const auto* x = sample->audio.getReadPointer (ch);
        OnePole low (167.68, sr, false), rest (151.66, sr, true), mid (1284.15, sr, false), high (1284.15, sr, true);
        for (int k = 0; k < blocks; ++k)
        {
            float a = 0, m = 0, h = 0;
            for (int i = k * bandBlock; i < juce::jmin (total, (k + 1) * bandBlock); ++i)
            {
                const auto lo = low.process (x[i]);
                const auto r = rest.process (x[i]);
                const auto mi = mid.process (r), hi = high.process (r);
                a += lo * lo;
                m += mi * mi;
                h += hi * hi;
            }
            eb[0][(size_t) k] += a * weight;
            eb[1][(size_t) k] += m * weight;
            eb[2][(size_t) k] += h * weight;
        }
    }
    // Measured at 44.1 kHz; at other rates the blocks and time constants keep
    // their length in time.
    const auto rateScale = sr / 44100.0;
    b.group[0] = juce::jmax (1, (int) std::lround (rateScale));
    b.group[1] = juce::jmax (1, (int) std::lround (2.0 * rateScale));
    follow (eb, b.group[0], 700.0 * rateScale, b.level[0]);   // Multiband
    follow (eb, b.group[1], 100.0 * rateScale, b.level[1]);   // Color map
    return b;
}

std::vector<juce::Colour> WaveformView::columnColours (float from, float to, int width) const
{
    std::vector<juce::Colour> out ((size_t) juce::jmax (0, width), waveColour());
    const int mode = colourMode();
    const int total = sample != nullptr ? sample->getTotalFrames() : 0;
    if (mode == 0 || total <= 0 || width <= 0)
        return out;
    const auto& b = bandEnergy (laneChannel >= 0 && ! sample->mono ? laneChannel + 1 : 0);
    const auto& level = b.level[mode == 2 ? 0 : 1];
    const int n = (int) level[0].size();
    if (n < 2)
        return out;
    const double blockFrames = (double) b.group[mode == 2 ? 0 : 1] * bandBlock;
    const float gain = sample->mono ? 1.05f : 1.0f;
    juce::Colour last = waveColour();
    bool any = false;
    for (int x = 0; x < width; ++x)
    {
        const auto centre = (double) (from + (to - from) * ((float) x + 0.5f) / (float) width) * total;
        const auto pos = centre / blockFrames - 0.25;
        const int k = juce::jlimit (0, n - 2, (int) std::floor (pos));
        const auto u = (float) juce::jlimit (0.0, 1.0, pos - k);
        float r[3];
        for (int c = 0; c < 3; ++c)
            r[c] = std::sqrt (juce::jmax (0.0f, level[c][(size_t) k] + u * (level[c][(size_t) k + 1] - level[c][(size_t) k])));
        const auto sum = r[0] + r[1] + r[2];
        if (sum > 1.0e-7f)
        {
            if (mode == 2)
            {
                const auto s = 245.0f * gain / sum;
                last = juce::Colour ((juce::uint8) juce::jmin (255, juce::roundToInt (r[0] * s)), (juce::uint8) juce::jmin (255, juce::roundToInt (r[1] * s)),
                                     (juce::uint8) juce::jmin (255, juce::roundToInt (r[2] * s)));
            }
            else
                last = colourMapAt ((0.4f * r[1] + r[2]) / sum, gain);
            if (! any)   // silence before the first sound takes the first colour
                std::fill (out.begin(), out.begin() + x, last);
            any = true;
        }
        out[(size_t) x] = last;
    }
    return out;
}

void WaveformView::setColourFill (juce::Graphics& g, juce::Rectangle<float> area, float from, float to, juce::Colour plain, bool colourful) const
{
    const auto scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int width = (int) std::ceil (area.getWidth() * scale);
    if (! colourful || colourMode() == 0 || width < 2)
    {
        g.setColour (plain);
        return;
    }
    const auto cols = columnColours (from, to, width);
    juce::ColourGradient grad (cols.front(), area.getX(), 0.0f, cols.back(), area.getRight(), 0.0f, false);
    for (int x = 1; x + 1 < width; ++x)
        if (cols[(size_t) x] != cols[(size_t) x - 1] || cols[(size_t) x] != cols[(size_t) x + 1])
            grad.addColour (((double) x + 0.5) / (double) width, cols[(size_t) x]);
    g.setGradientFill (grad);
}

//==============================================================================
// FL's spectrum view, modelled on measurements of FL 2026 (tone ladders, fine
// frequency sweeps, clicks, noise): black, frequency linear from 0 at the
// bottom to half the sample rate at the top, each pixel row the loudest
// frequency in its band. FL is sharp in both time and frequency: a click
// spreads like a 328-sample Blackman-Harris window, a steady tone stays two
// rows thick. Modelled as the short window's spectrum shaped by a long
// (Gaussian, sigma 152 samples) spectrum's local peak structure. Levels rise
// 3 dB per octave (0 at 3 kHz) and run through FL's palette from black at
// -112 dB through red and yellow to white at 0 dB. Matches FL's captures
// within about 1 dB on average.
namespace
{
    juce::Colour spectrumColour (float db)
    {
        // FL's palette by level (measured from a 6 dB ladder of a 3 kHz tone)
        static const float at[] = { -112, -102, -96, -90, -84, -78, -72, -66, -60, -54, -48, -42, -36, -30, -24, -18, -12, -6, 0 };
        static const juce::uint8 rgb[][3] = { { 0, 0, 0 }, { 12, 22, 33 }, { 20, 31, 47 }, { 31, 40, 60 }, { 46, 44, 66 }, { 64, 44, 66 },
                                              { 87, 44, 66 }, { 111, 44, 66 }, { 139, 44, 66 }, { 164, 44, 66 }, { 185, 49, 66 }, { 208, 93, 66 },
                                              { 227, 152, 66 }, { 242, 208, 66 }, { 253, 250, 66 }, { 255, 255, 104 }, { 255, 255, 166 },
                                              { 255, 255, 214 }, { 255, 255, 255 } };
        constexpr int n = (int) std::size (at);
        if (db <= at[0])     return juce::Colours::black;
        if (db >= at[n - 1]) return juce::Colours::white;
        int i = 0;
        while (db > at[i + 1]) ++i;
        const auto u = (db - at[i]) / (at[i + 1] - at[i]);
        auto mix = [&] (int c) { return (juce::uint8) juce::roundToInt ((float) rgb[i][c] + u * (float) (rgb[i + 1][c] - rgb[i][c])); };
        return juce::Colour (mix (0), mix (1), mix (2));
    }
}

void WaveformView::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = juce::jmax (1, (int) std::ceil (area.getWidth() * scale)), h = juce::jmax (2, (int) std::ceil (area.getHeight() * scale));
    const auto key = juce::String::toHexString ((juce::pointer_sized_int) sample.get()) + " " + juce::String (viewStart) + " " + juce::String (viewEnd)
                   + " " + juce::String (w) + "x" + juce::String (h);
    if (key != spectrumKey)
    {
        spectrumKey = key;
        spectrumImage = juce::Image (juce::Image::RGB, w, h, true, juce::SoftwareImageType());   // filled from several threads
        const int total = sample->getTotalFrames();
        const auto* l = sample->audio.getReadPointer (0);
        const auto* r = sample->audio.getReadPointer (juce::jmin (1, sample->audio.getNumChannels() - 1));
        const auto sr = sample->sourceRate > 0 ? sample->sourceRate : 44100.0;
        // Measured at 44.1 kHz; at other rates the windows keep their length
        // in time (and the local-peak span its width in Hz).
        const double rateScale = sr / 44100.0;
        const int shortLen = juce::jmax (16, (int) std::lround (328.0 * rateScale));
        const double longSigma = 152.0 * rateScale;
        const int longLen = (int) std::lround (8.0 * longSigma);
        int specOrder = 11;   // 2048 points at 44.1 / 48 kHz
        while ((1 << specOrder) < longLen)
            ++specOrder;
        const int specSize = 1 << specOrder;
        const int peakSpan = juce::jmax (1, (int) std::lround (32.0 * (44100.0 / 2048.0) / (sr / specSize)));
        const int bins = specSize / 2;

        std::vector<float> shortWin ((size_t) shortLen), longWin ((size_t) longLen);
        double shortSum = 0;
        for (int i = 0; i < shortLen; ++i)
        {
            const auto x = juce::MathConstants<double>::twoPi * i / (shortLen - 1);
            shortWin[(size_t) i] = (float) (0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x));
            shortSum += shortWin[(size_t) i];
        }
        for (int i = 0; i < longLen; ++i)
        {
            const auto d = (i - (longLen - 1) * 0.5) / longSigma;
            longWin[(size_t) i] = (float) std::exp (-0.5 * d * d);
        }
        // Per bin: the 3 dB/octave tilt and the normalisation (a tone of
        // amplitude A reads 20 log A at 3 kHz).
        std::vector<float> offset ((size_t) bins);
        for (int k = 0; k < bins; ++k)
            offset[(size_t) k] = (float) (10.0 * std::log10 (juce::jmax (1, k) * sr / specSize / 3000.0) + 20.0 * std::log10 (2.0 / shortSum));
        // Rows (0 = bottom) show the loudest bin between (row - 1) and row
        // times nyquist / (rows - 1) (measured edges).
        std::vector<int> rowFirst ((size_t) h), rowLast ((size_t) h);
        for (int row = 0; row < h; ++row)
        {
            const auto lo = (double) (row - 1) / (h - 1) * bins, hi = (double) row / (h - 1) * bins;
            int a = juce::jmax (0, (int) std::ceil (lo)), b = juce::jmin (bins - 1, (int) std::ceil (hi) - 1);
            if (b < a)
                a = b = juce::jlimit (0, bins - 1, (int) std::lround (hi));
            rowFirst[(size_t) row] = a;
            rowLast[(size_t) row] = b;
        }

        auto columns = [&] (int x0, int x1)
        {
            juce::dsp::FFT fft (specOrder);
            std::vector<float> a ((size_t) (2 * specSize)), b ((size_t) (2 * specSize)), level ((size_t) bins), pre ((size_t) bins), post ((size_t) bins);
            juce::Image::BitmapData px (spectrumImage, 0, 0, w, h, juce::Image::BitmapData::writeOnly);
            for (int x = x0; x < x1; ++x)
            {
                const auto frac = viewStart + (viewEnd - viewStart) * ((float) x + 0.5f) / (float) w;
                const int centre = (int) (frac * (float) total);
                std::fill (a.begin(), a.end(), 0.0f);
                std::fill (b.begin(), b.end(), 0.0f);
                for (int i = 0; i < shortLen; ++i)
                {
                    const int k = centre - shortLen / 2 + i;
                    if (k >= 0 && k < total)
                        a[(size_t) i] = 0.5f * (l[k] + r[k]) * shortWin[(size_t) i];
                }
                for (int i = 0; i < longLen; ++i)
                {
                    const int k = centre - longLen / 2 + i;
                    if (k >= 0 && k < total)
                        b[(size_t) i] = 0.5f * (l[k] + r[k]) * longWin[(size_t) i];
                }
                fft.performFrequencyOnlyForwardTransform (a.data(), true);
                fft.performFrequencyOnlyForwardTransform (b.data(), true);
                // Local peak of the long spectrum over +/-peakSpan bins (prefix /
                // suffix maxima per block).
                const int span = 2 * peakSpan + 1;
                for (int k = 0; k < bins; ++k)
                    pre[(size_t) k] = (k % span == 0) ? b[(size_t) k] : juce::jmax (pre[(size_t) k - 1], b[(size_t) k]);
                for (int k = bins - 1; k >= 0; --k)
                    post[(size_t) k] = (k % span == span - 1 || k == bins - 1) ? b[(size_t) k] : juce::jmax (post[(size_t) k + 1], b[(size_t) k]);
                for (int k = 0; k < bins; ++k)
                {
                    const int lo = juce::jmax (0, k - peakSpan), hi = juce::jmin (bins - 1, k + peakSpan);
                    const auto peak = juce::jmax (post[(size_t) lo], pre[(size_t) hi]);
                    const auto m = a[(size_t) k] * (peak > 0.0f ? b[(size_t) k] / peak : 0.0f);
                    level[(size_t) k] = m > 1.0e-12f ? 20.0f * std::log10 (m) + offset[(size_t) k] : -200.0f;
                }
                for (int row = 0; row < h; ++row)
                {
                    float best = -200.0f;
                    for (int k = rowFirst[(size_t) row]; k <= rowLast[(size_t) row]; ++k)
                        best = juce::jmax (best, level[(size_t) k]);
                    px.setPixelColour (x, h - 1 - row, spectrumColour (best));
                }
            }
        };
        // Columns are independent: spread them over a few threads.
        const int threads = juce::jlimit (1, 8, (int) std::thread::hardware_concurrency());
        std::vector<std::thread> pool;
        for (int t = 1; t < threads; ++t)
            pool.emplace_back (columns, w * t / threads, w * (t + 1) / threads);
        columns (0, w / threads);
        for (auto& t : pool)
            t.join();
    }
    g.drawImage (spectrumImage, area);
}
