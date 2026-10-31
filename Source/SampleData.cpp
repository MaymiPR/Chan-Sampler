#include "SampleData.h"
#include "TrimTail.h"
#include "Dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Every process here was measured against FL Studio's Sampler by rendering
// test projects through FL and null-testing (nulltest/, docs/EVIDENCE.md).
// "Raw" values below are FL's stored parameter values.

namespace
{
    // FL's sample-rate conversion on load, measured from impulse trains in
    // 22.05/32/48/88.2 kHz files (nulltest/probes/src_extract.py): an 88-tap windowed
    // sinc, k(d) = c sinc(c d) w(d), |d| < 44 input samples, c = min(1, 1/ratio),
    // w a 9-term cosine series (close to Hann). Matches FL to 1 LSB.
    juce::AudioBuffer<float> convertRate (const juce::AudioBuffer<float>& in, double fromRate, double toRate)
    {
        static constexpr double a[] = { 0.498387586, 0.490065433, 0.009996963, 0.002969411, -0.002640382,
                                        0.002159460, -0.001503978, 0.000750707, -0.000168888 };
        constexpr int half = 44, steps = 1024;
        const double ratio = fromRate / toRate;
        const double c = juce::jmin (1.0, 1.0 / ratio);

        std::vector<float> table ((size_t) (half * steps + 2));
        for (size_t i = 0; i < table.size(); ++i)
        {
            const double d = (double) i / steps;
            if (d >= half) { table[i] = 0.0f; continue; }
            double w = 0.0;
            for (int m = 0; m < 9; ++m)
                w += a[m] * std::cos (juce::MathConstants<double>::pi * m * d / half);
            const double x = juce::MathConstants<double>::pi * c * d;
            table[i] = (float) ((d == 0.0 ? c : std::sin (x) / (juce::MathConstants<double>::pi * d)) * w);
        }

        const int n = in.getNumSamples();
        const int outLen = juce::jmax (1, (int) std::ceil (n / ratio));
        juce::AudioBuffer<float> out (in.getNumChannels(), outLen);
        for (int ch = 0; ch < in.getNumChannels(); ++ch)
        {
            const auto* x = in.getReadPointer (ch);
            auto* y = out.getWritePointer (ch);
            for (int o = 0; o < outLen; ++o)
            {
                const double pos = o * ratio;
                const int base = (int) std::floor (pos);
                double sum = 0.0;
                for (int j = base - half + 1; j <= base + half; ++j)
                {
                    if (j < 0 || j >= n)
                        continue;
                    const double xd = std::abs (pos - j) * steps;
                    const int ti = (int) xd;
                    if (ti >= half * steps)
                        continue;
                    const double f = xd - ti;
                    sum += x[j] * (table[(size_t) ti] + (table[(size_t) ti + 1] - table[(size_t) ti]) * f);
                }
                y[o] = (float) sum;
            }
        }
        return out;
    }

    // Measured: gain = 1 + 16 * knob; CLIP is a hard clip at +/-1.
    void boost (juce::AudioBuffer<float>& b, float knob, bool clip)
    {
        if (knob > 0.0f)
            b.applyGain (1.0f + 16.0f * knob);
        if (clip)
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                juce::FloatVectorOperations::clip (b.getWritePointer (ch), b.getReadPointer (ch), -1.0f, 1.0f, b.getNumSamples());
    }

    // Measured: y[n] = x[n] + 0.75 * eq * y[n-1].
    void tiltEq (juce::AudioBuffer<float>& b, float amount)
    {
        if (std::abs (amount) < 1.0e-6f)
            return;
        const auto k = 0.75 * (double) amount;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            double y = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] = (float) (y = d[i] + k * y);
        }
    }

    // Measured: y = x * ((1 - mix) + mix * m), with the modulator advancing
    // (raw + 8) / 5760 cycles per sample (61 Hz .. 2021 Hz at 44.1 kHz).
    // Stereo: m = sin on the left, cos on the right (90 degrees apart).
    // A mono file stays mono in FL, so both channels get the sine (measured).
    void ringMod (juce::AudioBuffer<float>& b, float mix, float freqKnob, bool mono)
    {
        if (mix <= 0.0f)
            return;
        const auto inc = juce::MathConstants<double>::twoPi * (256.0 * freqKnob + 8.0) / 5760.0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] *= (1.0f - mix) + mix * (float) (ch == 0 || mono ? std::sin (inc * i) : std::cos (inc * i));
        }
    }

    // Measured: y[n] = y[n-1] + f (x[n] - y[n-1]) + r (y[n-1] - y[n-2]),
    // f = (601^u - 1) / 600 with u = raw cutoff / 1024 (time-domain fit), r
    // interpolated from the measured resonance points.
    bool lowpassOff (float cutKnob, float resKnob)
    {
        return dsp::flOnePoleF (juce::jmax (1.0f / 64.0f, cutKnob)) >= 1.0 && dsp::flResonance (resKnob) <= 0.0;
    }

    void lowpass (juce::AudioBuffer<float>& b, float cutKnob, float resKnob)
    {
        if (lowpassOff (cutKnob, resKnob))
            return;
        const auto f = dsp::flOnePoleF (juce::jmax (1.0f / 64.0f, cutKnob));
        const auto r = dsp::flResonance (resKnob);

        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer (ch);
            double y1 = 0.0, y2 = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const auto y = y1 + f * (d[i] - y1) + r * (y1 - y2);
                y2 = y1;
                y1 = y;
                d[i] = (float) y;
            }
        }
    }

    // FL's precomputed reverb, identified from its impulse responses
    // (nulltest/probes/rvb, docs/EVIDENCE.md); it matches FL to the 16-bit LSB.
    // Both types are Schroeder reverbs run on each channel separately:
    // parallel combs whose feedback gives every comb the same decay time,
    // T60 = mix / 256 seconds (g = 10^(-3 D / T60)), then a chain of allpasses
    // (gain 0.7), then a smoothing filter; the dry signal is kept at 0.7.
    //   A: combs 1777 1847 1993 2137 (0.15 each), allpasses 43 127 389,
    //      output (w[n] / 2 + w[n-1] + w[n-2] / 2).
    //   B: combs 2467 2767 3217 3533 3877 4127 (0.027 each), allpasses
    //      67 97|79 (left|right) 101 197 599, output one-pole y = w + 0.7 y[-1].
    // FL then trims the tail at the last sample above 65 / 32768.
    void reverb (juce::AudioBuffer<float>& b, int type, float amount)
    {
        const auto mix = amount * 256.0f;
        if (mix <= 0.0f)
            return;

        static constexpr int combsA[] = { 1777, 1847, 1993, 2137 };
        static constexpr int combsB[] = { 2467, 2767, 3217, 3533, 3877, 4127 };
        const bool isA = type == 0;
        const int* combs = isA ? combsA : combsB;
        const int numCombs = isA ? 4 : 6;
        const double combGain = isA ? 0.15 : 0.16201 / 6.0;
        const double t60 = mix / 256.0 * 44100.0;                  // in samples

        const int n = b.getNumSamples();
        const int tail = (int) juce::jmin (20.0 * 44100.0, t60 + 8000.0);
        const int total = n + tail;
        b.setSize (2, total, true, true);

        std::vector<double> in ((size_t) total), acc ((size_t) total), line;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < total; ++i)
                in[(size_t) i] = d[i];

            std::fill (acc.begin(), acc.end(), 0.0);
            for (int k = 0; k < numCombs; ++k)
            {
                const int D = combs[k];
                const double g = std::pow (10.0, -3.0 * D / t60);
                line.assign ((size_t) total, 0.0);
                for (int i = 0; i < total; ++i)
                {
                    line[(size_t) i] = in[(size_t) i] + (i >= D ? g * line[(size_t) (i - D)] : 0.0);
                    acc[(size_t) i] += combGain * line[(size_t) i];
                }
            }

            const int apA[] = { 43, 127, 389 };
            const int apB[] = { 67, ch == 0 ? 97 : 79, 101, 197, 599 };
            const int* aps = isA ? apA : apB;
            for (int k = 0; k < (isA ? 3 : 5); ++k)
            {
                const int D = aps[k];
                line.assign ((size_t) total, 0.0);
                for (int i = 0; i < total; ++i)
                {
                    const auto xd = i >= D ? acc[(size_t) (i - D)] : 0.0;
                    const auto yd = i >= D ? line[(size_t) (i - D)] : 0.0;
                    line[(size_t) i] = -0.7 * acc[(size_t) i] + xd + 0.7 * yd;
                }
                std::swap (acc, line);
            }

            double y1 = 0.0;
            for (int i = 0; i < total; ++i)
            {
                double wet;
                if (isA)
                    wet = 0.5 * acc[(size_t) i] + (i >= 1 ? acc[(size_t) (i - 1)] : 0.0) + (i >= 2 ? 0.5 * acc[(size_t) (i - 2)] : 0.0);
                else
                    wet = y1 = acc[(size_t) i] + 0.7 * y1;
                d[i] = (float) (0.7 * in[(size_t) i] + wet);
            }
        }

        // Trim trailing near-silence (measured: 65/32768 kept, 64.02/32768 cut).
        constexpr float threshold = 0.00197f;
        int last = total - 1;
        while (last > 0 && std::abs (b.getSample (0, last)) < threshold && std::abs (b.getSample (1, last)) < threshold)
            --last;
        b.setSize (2, last + 1, true, true);
    }

    // Measured: delays one channel by |raw - 2048| samples (up to 2048);
    // turning right (positive) delays the left channel. The sample grows by
    // the delay so the delayed channel is not cut off.
    void stereoDelay (juce::AudioBuffer<float>& b, float amount)
    {
        const int delay = (int) std::round (std::abs (amount) * 2048.0f);
        if (delay <= 0)
            return;
        const int n = b.getNumSamples();
        b.setSize (2, n + delay, true, true);
        auto* d = b.getWritePointer (amount > 0.0f ? 0 : 1);
        for (int i = n + delay - 1; i >= 0; --i)
            d[i] = i >= delay ? d[i - delay] : 0.0f;
    }

    // Measured: a length-preserving time warp. Read position (0..1) is
    // (1 - |a|) x + |a| w(x), with w(x) = sin(pi x / 2) to the right (slows
    // down towards the end) and w(x) = 1 - (2 / pi) asin(1 - x) to the left
    // (starts fast).
    void pogo (juce::AudioBuffer<float>& b, float amount)
    {
        const int n = b.getNumSamples();
        if (std::abs (amount) < 1.0e-4f || n < 4)
            return;

        juce::AudioBuffer<float> src (b);
        const auto a = std::abs ((double) amount);
        for (int i = 0; i < n; ++i)
        {
            const auto x = (double) i / (n - 1);
            const auto w = amount > 0.0f ? std::sin (juce::MathConstants<double>::halfPi * x)
                                         : 1.0 - 2.0 / juce::MathConstants<double>::pi * std::asin (1.0 - x);
            const auto s = ((1.0 - a) * x + a * w) * (n - 1);
            const int k = juce::jlimit (0, n - 2, (int) s);
            const auto t = (float) (s - k);
            for (int ch = 0; ch < 2; ++ch)
            {
                // 4-point Hermite; neighbours outside the sample are silence
                // (measured: nulls at -97 dB, clamping to the edge does not).
                const auto* d = src.getReadPointer (ch);
                const auto xm1 = k > 0 ? d[k - 1] : 0.0f;
                const auto x2 = k + 2 < n ? d[k + 2] : 0.0f;
                const auto c1 = 0.5f * (d[k + 1] - xm1);
                const auto c2 = xm1 - 2.5f * d[k] + 2.0f * d[k + 1] - 0.5f * x2;
                const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (d[k] - d[k + 1]);
                b.getWritePointer (ch)[i] = ((c3 * t + c2) * t + c1) * t + d[k];
            }
        }
    }

    // Measured: trims after the last frame whose louder channel exceeds raw / 32768.
    int findTrimmedLength (const juce::AudioBuffer<float>& b, float trimKnob)
    {
        const auto threshold = trimKnob * 256.0f / 32768.0f;
        for (int i = b.getNumSamples() - 1; i >= 0; --i)
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                if (std::abs (b.getSample (ch, i)) > threshold)
                    return i + 1;
        return juce::jmin (1, b.getNumSamples());
    }

    // Measured: linear ramps relative to the sample length. IN rises to full
    // at the end with slope 4 * knob; OUT falls from the start with slope 4 * knob.
    void fades (juce::AudioBuffer<float>& b, float fadeIn, float fadeOut)
    {
        if (fadeIn <= 0.0f && fadeOut <= 0.0f)
            return;
        const int n = b.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const auto x = (float) i / (float) n;
            const auto g = juce::jlimit (0.0f, 1.0f, 1.0f - (1.0f - x) * 4.0f * fadeIn)
                         * juce::jlimit (0.0f, 1.0f, 1.0f - x * 4.0f * fadeOut);
            b.getWritePointer (0)[i] *= g;
            b.getWritePointer (1)[i] *= g;
        }
    }

    // Measured: left gain cos(x pi / 2), right gain sin(x pi / 2).
    void applyFadeStereo (juce::AudioBuffer<float>& b)
    {
        const int n = b.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const auto x = (float) i / (float) juce::jmax (1, n - 1);
            b.getWritePointer (0)[i] *= std::cos (x * juce::MathConstants<float>::halfPi);
            b.getWritePointer (1)[i] *= std::sin (x * juce::MathConstants<float>::halfPi);
        }
    }

    // FL's CROSSFADE (measured): the loop start sits at knob / 2 of the sample (knob = raw / 1024).
    // Without loop points the whole sample from there loops; with them the
    // crossfade applies at the loop points. The last stretch before the loop
    // end blends linearly into the audio just before the loop start.
    void loopCrossfade (juce::AudioBuffer<float>& b, const PrecomputeSettings& s, SampleData& out)
    {
        const int n = b.getNumSamples();
        if (s.crossfade <= 0.0f || n < 8)
            return;

        int ls = (int) std::lround (s.loopStart * (float) n), le = (int) std::lround (juce::jmax (s.loopStart, s.loopEnd) * (float) n), len;
        if (s.useLoop && le - ls >= 4)   // loop points set and in use
        {
            // Measured in FL: the fade is CROSSFADE x length / 4 long and ends at
            // the (inclusive) END; it blends in the audio from START, and the
            // loop then restarts that far after START (each pass is shorter).
            le = juce::jmin (n, le + 1);
            len = juce::jmin ((int) (s.crossfade / 4.0 * n), le - ls - 2);
            if (len < 1)
                return;
            out.loopStartShift = len;
            ls += len;
        }
        else
        {
            ls = juce::jmax (1, (int) (s.crossfade / 2.0 * n));   // FL truncates
            le = n;
            len = ls;
            out.autoLoop = true;
            out.autoLoopStart = ls;
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = b.getWritePointer (ch);
            for (int i = 0; i < len; ++i)
            {
                const auto t = (float) i / (float) len;
                d[le - len + i] = d[le - len + i] * (1.0f - t) + d[ls - len + i] * t;
            }
        }
    }

    // The file's region / slice markers become the view's markers and, with
    // the sample's start, the slice points (they replace the detected
    // transients, measured in FL). False when there are none.
    bool useFileMarkers (SampleData& out, std::vector<int> marks)
    {
        if (marks.empty())
            return false;
        std::sort (marks.begin(), marks.end());
        marks.erase (std::unique (marks.begin(), marks.end()), marks.end());
        out.fileMarkers = marks;
        marks.push_back (0);
        std::sort (marks.begin(), marks.end());
        marks.erase (std::unique (marks.begin(), marks.end()), marks.end());
        while (! marks.empty() && marks.back() >= out.playEnd)
            marks.pop_back();
        out.onsets = std::move (marks);
        return true;
    }

    // The slice modes' transients, as FL finds them: a 128-sample block's
    // energy far above the four before it (the rule e3's attacks use), then
    // the hit leaves the level before it, and the slice starts at the zero
    // crossing at or before that (FL Slice map on a drum loop: every slice
    // starts on an exact zero or on the first sample past a sign change;
    // a sung "ah" fading in has none). In stereo, the first sign change in
    // either channel, and an exact zero only in both (FL, stereo test files;
    // nulltest/probes/slice_starts.py).
    std::vector<int> detectOnsets (const juce::AudioBuffer<float>& b)
    {
        constexpr int block = 128;
        std::vector<int> onsets { 0 };
        const int n = b.getNumSamples();
        const auto* l = b.getReadPointer (0);
        const auto* r = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0);
        auto level = [&] (int i) { return std::abs (l[i]) + std::abs (r[i]); };
        std::vector<double> e;
        for (int i = 0; i + block <= n; i += block)
        {
            double sum = 0;
            for (int k = i; k < i + block; ++k)
                sum += (double) l[k] * l[k] + (double) r[k] * r[k];
            e.push_back (sum);
        }
        double peak = 0;
        for (auto v : e) peak = juce::jmax (peak, v);
        for (size_t i = 4; i < e.size(); ++i)
        {
            const double before = 0.25 * (e[i - 1] + e[i - 2] + e[i - 3] + e[i - 4]);
            if (e[i] <= 8.0 * before + 1.0e-4 * block || e[i] <= peak * 0.01)
                continue;
            // The hit's own peak, the level before it, and the first sample
            // clearly above that.
            const int from = (int) (i - 1) * block, to = juce::jmin (n, (int) i * block + 4 * block);
            float hit = 0.0f, floor = 0.0f;
            for (int k = from; k < to; ++k)
                hit = juce::jmax (hit, level (k));
            for (int k = juce::jmax (0, from - 8 * block); k < from; ++k)
                floor = juce::jmax (floor, level (k));
            const float threshold = juce::jmax (floor, 0.03f * hit);
            int pos = (int) i * block;
            for (int k = from; k < to; ++k)
                if (level (k) > threshold) { pos = k; break; }
            // Either channel's sign change ends the walk back; a digitally
            // silent channel never does.
            auto crosses = [&] (const float* x, int k) { return x[k - 1] * x[k] < 0.0f; };
            while (pos > 0 && (l[pos] != 0.0f || r[pos] != 0.0f) && ! crosses (l, pos) && ! crosses (r, pos))
                --pos;
            if (pos - onsets.back() >= 2048)
                onsets.push_back (pos);
        }
        return onsets;
    }
}

double trimKnobToFraction (float knob)
{
    // FL stores Sample Start / Length as plain fractions (doubles), and
    // its rounding of fraction x frames depends on the exact double (0.05 x
    // 88200 is 4410 frames, 0.3 x 88200 rounds up to 26461). The parameter is
    // a float, so take the shortest decimal that reads back as it (0.05f is
    // 0.05): the double FL holds for the same value (0.05f itself would give 4411).
    const auto f = juce::jlimit (0.0f, 1.0f, knob);
    for (int digits = 1; digits <= 9; ++digits)
    {
        char text[32];
        std::snprintf (text, sizeof (text), "%.*g", digits, (double) f);
        const auto d = std::strtod (text, nullptr);
        if ((float) d == f)
            return juce::jlimit (0.0, 1.0, d);
    }
    return (double) f;
}

void SampleData::computePeaks()
{
    const int n = getTotalFrames();
    for (int ch = 0; ch < 2; ++ch)
    {
        peakMin[ch].assign (numPeakBuckets, 0.0f);
        peakMax[ch].assign (numPeakBuckets, 0.0f);
        if (playEnd <= 0 || n == 0)
            continue;

        const auto* d = audio.getReadPointer (ch);
        for (int bkt = 0; bkt < numPeakBuckets; ++bkt)
        {
            const int a = (int) ((int64_t) bkt * playEnd / numPeakBuckets);
            const int e = juce::jmax (a + 1, (int) ((int64_t) (bkt + 1) * playEnd / numPeakBuckets));
            auto range = juce::FloatVectorOperations::findMinAndMax (d + a, juce::jmin (e, playEnd) - a);
            peakMin[ch][(size_t) bkt] = range.getStart();
            peakMax[ch][(size_t) bkt] = range.getEnd();
        }
    }
}

namespace
{
    // True when every whole-sample tool and effect leaves the audio as it is
    // (the same conditions the functions above return early on).
    bool processingOff (const PrecomputeSettings& s)
    {
        return ! s.swapStereo && ! s.reversePolarity && ! s.removeDC && ! s.reverse && s.boost <= 0.0f && ! s.boostClip
            && std::abs (s.eq) < 1.0e-6f && s.rmMix <= 0.0f && lowpassOff (s.cut, s.res) && s.reverb <= 0.0f
            && (int) std::round (std::abs (s.stereoDelay) * 2048.0f) <= 0 && std::abs (s.pogo) < 1.0e-4f && ! s.normalize
            && s.trim <= 0.0f && s.fadeIn <= 0.0f && s.fadeOut <= 0.0f && ! s.fadeStereo && s.crossfade <= 0.0f;
    }
}

SampleData::Ptr buildProcessedSample (const RawSample& raw, const PrecomputeSettings& s, std::shared_ptr<const RawSample> owner)
{
    SampleData::Ptr out = new SampleData();
    out->sourceRate = raw.sampleRate;
    out->mono = raw.audio.getNumChannels() == 1;
    out->fileBits = raw.heldBits;

    const int n = raw.audio.getNumSamples();
    if (n == 0 || raw.audio.getNumChannels() == 0)
    {
        out->computePeaks();
        return out;
    }

    // Work in stereo throughout; mono files are duplicated. A file at another
    // rate is first converted to the playback rate, as FL does on load.
    // Otherwise the trimmed part is taken straight from the file (no copy of
    // the whole sample: long files stay within memory).
    const bool convert = s.targetRate > 0.0 && std::abs (s.targetRate - raw.sampleRate) > 0.5;
    juce::AudioBuffer<float> b;
    if (convert)
    {
        b.setSize (2, n);
        for (int ch = 0; ch < 2; ++ch)
            b.copyFrom (ch, 0, raw.audio, juce::jmin (ch, raw.audio.getNumChannels() - 1), 0, n);
        b = convertRate (b, raw.sampleRate, s.targetRate);
        out->sourceRate = s.targetRate;
    }
    const juce::AudioBuffer<float>& whole = convert ? b : raw.audio;
    const int converted = whole.getNumSamples();
    auto wholeChannel = [&whole] (int ch) { return juce::jmin (ch, whole.getNumChannels() - 1); };

    // 1. Whole-sample tools (swap, polarity and DC removal act per sample, so
    // they run on the trimmed part below; the DC is the whole sample's mean)
    float dc[2] {};
    if (s.removeDC)
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* d = whole.getReadPointer (wholeChannel (s.swapStereo ? 1 - ch : ch));
            double sum = 0.0;
            for (int i = 0; i < converted; ++i) sum += d[i];
            dc[ch] = (float) (sum / juce::jmax (1, converted)) * (s.reversePolarity ? -1.0f : 1.0f);
        }

    // 2. Sample Start / Length with FL's short trim fades (measured in FL):
    // start = round (start * (N - 1)), length as below, at least 40 frames.
    // A start exactly between two frames (e.g. 50%) rounds up for the play
    // position but down for the length count, so FL's segment then runs one
    // frame past the end of the sample (silence; measured, with Reverse too).
    const auto startPos = trimKnobToFraction (s.start) * (converted - 1);
    int start = juce::jlimit (0, converted - 1, (int) std::floor (startPos + 0.5));
    int remaining = converted - juce::jlimit (0, converted - 1, (int) std::ceil (startPos - 0.5));
    constexpr int minLength = 40;
    // Length: the fraction of the span from the exact start position to the
    // last frame, rounded half to even, one frame more reversed (fitted to
    // FL's end in 85 forward and 15 reversed in-FL trims of noise, all exact).
    // 100% is the whole remainder, untrimmed.
    const auto lengthFraction = trimKnobToFraction (s.length);
    const bool lengthCut = lengthFraction < 1.0;
    auto span = (int) std::nearbyint (lengthFraction * ((converted - 1) - startPos));
    // A segment shorter than FL's shortest is lengthened by that much rather
    // than held at it (a 30-frame trim plays about 70 frames, 40 about 80, 41
    // about 40); within that much of the end of the sample it grows backwards,
    // starting earlier (nulltest/cases/trim_short_cases.py; docs/BINARY-NOTES.md).
    // LENGTH 0 keeps its own measured shape (the shortest segment, below).
    constexpr int shortest = minLength + 1;
    if (lengthCut && span < shortest)
    {
        if (start >= converted - 1 - (shortest + 1))
        {
            start = juce::jmax (0, start - shortest);
            remaining = converted - start;
        }
        if (span > 0)
            span += shortest;
    }
    // FL keeps the segment's last frame too; forwards it is filtered to about
    // 0, so long segments leave it out (their measured table ends before it),
    // short ones keep it, as their end filter's window covers it (TrimTail.h).
    const bool keepLast = s.reverse || (span > 0 && span + 1 < trimtail::size);
    const int len = ! lengthCut ? remaining
                                : juce::jlimit (juce::jmin (minLength, remaining), remaining, span + (keepLast ? 1 : 0));
    // FL's shortest segment (LENGTH at its minimum) carries two silent frames
    // after its 40 (measured: forwards the end holds silence, reversed it
    // starts two frames late).
    const bool atMinimum = len == minLength && len < remaining
                        && span < minLength;
    const int bufLen = len + (atMinimum ? 2 : 0);

    // Nothing to process (the usual case): play the file's own audio, without
    // a second copy in memory (long samples).
    if (owner != nullptr && owner.get() == &raw && ! convert && start == 0 && bufLen == converted && processingOff (s))
    {
        float* channels[2] { const_cast<float*> (raw.audio.getReadPointer (0)),
                             const_cast<float*> (raw.audio.getReadPointer (juce::jmin (1, raw.audio.getNumChannels() - 1))) };
        out->audio.setDataToReferTo (channels, 2, converted);   // read only from here on
        out->sharedRaw = std::move (owner);
        out->playEnd = converted;
        std::vector<int> marks;
        for (auto* src : { s.loadRegions ? &raw.regionMarkers : nullptr, s.loadSlices ? &raw.sliceMarkers : nullptr })
            if (src != nullptr)
                for (auto m : *src)
                    if (m >= 0 && m < converted)
                        marks.push_back (m);
        if (! useFileMarkers (*out, std::move (marks)))
            out->onsets = detectOnsets (out->audio);
        out->computePeaks();
        return out;
    }

    juce::AudioBuffer<float> main (2, bufLen);
    main.clear();
    for (int ch = 0; ch < 2; ++ch)
        main.copyFrom (ch, 0, whole, wholeChannel (s.swapStereo ? 1 - ch : ch), start, juce::jmin (len, converted - start));
    // A segment that Length cuts short ends, for FL's end filter, at its last
    // sound: trailing digital silence is left out (measured on a trim landing
    // in a gap of silence: FL fades where the sound stops, -77 dB against
    // FL's render, not inside the silence, -49 dB).
    int end = len;
    if (lengthCut)   // FL filters the end whenever LENGTH < 100%, even at the end of the sample
    {
        int last = -1;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto* d = main.getReadPointer (ch);
            for (int i = juce::jmin (len, converted - start) - 1; i > last; --i)
                if (d[i] != 0.0f) { last = i; break; }
        }
        if (last >= 0)
            end = last + 1;
    }
    b.setSize (0, 0);   // the converted copy is no longer needed
    if (s.reversePolarity)
        for (int ch = 0; ch < 2; ++ch)
            main.applyGain (ch, 0, juce::jmin (len, converted - start), -1.0f);
    if (s.removeDC)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = main.getWritePointer (ch);
            for (int i = 0; i < juce::jmin (len, converted - start); ++i) d[i] -= dc[ch];
        }
    // FL's trimmed start is a short filter as well (TrimTail.h): output n mixes
    // the input from n up to about 3n.
    auto applyHead = [&] (float* d)
    {
        if (start <= 0)
            return;
        const int count = juce::jmin (len, trimtail::headSize);
        float in[trimtail::headSize] {};
        for (int i = 0; i < count; ++i)
            in[i] = d[i];
        for (int i = 0; i < count; ++i)
        {
            float acc = 0.0f;
            for (int e = 0; e < count; ++e)
                acc += trimtail::head22[i * trimtail::headSize + e] * in[e];
            d[i] = acc;
        }
    };
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = main.getWritePointer (ch);
        applyHead (d);
        if (lengthCut)
        {
            // FL's trimmed end is a linear filter over the last frames, not a
            // plain fade (measured, TrimTail.h): each output mixes the input
            // from its own position back to about twice its distance from the end.
            const int tailLen = juce::jmin (end, trimtail::size);
            float in[trimtail::size + 1] {};
            for (int dist = 1; dist <= tailLen; ++dist)
                in[dist] = d[end - dist];
            if (end == trimtail::shortSize && len == trimtail::shortSize)   // FL's shortest segment has its own table
            {
                for (int dist = 1; dist <= len; ++dist)
                {
                    float acc = 0.0f;
                    for (int k = 1; k <= len; ++k)
                        acc += trimtail::short40[(dist - 1) * len + (k - 1)] * in[k];
                    d[end - dist] = acc;
                }
            }
            else if (end < trimtail::size)   // shorter than the table: FL's window is the whole segment
            {
                trimtail::applyEnd (d, end);
            }
            else
            {
                // Reversed, the segment keeps FL's last frame, which the table
                // (measured forwards) ends before: it ends a frame earlier,
                // and that last frame is filtered to about 0.
                const int tableEnd = s.reverse && end == len ? end - 1 : end;
                if (tableEnd != end)
                {
                    for (int dist = 1; dist <= tailLen; ++dist)
                        in[dist] = d[tableEnd - dist];
                    d[end - 1] = 0.0f;
                }
                for (int dist = 1; dist <= tailLen; ++dist)
                {
                    const float* c = trimtail::coef + trimtail::start[dist - 1];
                    const int count = juce::jmin (trimtail::count[dist - 1], tailLen - dist + 1);
                    float acc = 0.0f;
                    for (int k = 0; k < count; ++k)
                        acc += c[k] * in[dist + k];
                    d[tableEnd - dist] = acc;
                }
            }
        }
    }
    // FL reverses the trimmed part (measured: the segment plays backwards).
    if (s.reverse)
        main.reverse (0, bufLen);
    out->lengthTrimmed = lengthCut;

    // 3. Effects
    boost (main, s.boost, s.boostClip);
    tiltEq (main, s.eq);
    ringMod (main, s.rmMix, s.rmFreq, raw.audio.getNumChannels() == 1);
    lowpass (main, s.cut, s.res);
    reverb (main, s.reverbType, s.reverb);
    stereoDelay (main, s.stereoDelay);
    pogo (main, s.pogo);

    // 4. Level and shape
    if (s.normalize)
    {
        const auto peak = main.getMagnitude (0, main.getNumSamples());
        if (peak > 0.0f)
            main.applyGain (1.0f / peak);
    }

    // TRIM at 0 keeps trailing digital silence (FL does; the end-of-sample
    // fade then acts on silence instead of the last audible sample).
    const int trimmed = s.trim > 0.0f ? findTrimmedLength (main, s.trim) : main.getNumSamples();
    if (trimmed < main.getNumSamples())
        main.setSize (2, trimmed, true);

    fades (main, s.fadeIn, s.fadeOut);
    if (s.fadeStereo)
        applyFadeStereo (main);
    loopCrossfade (main, s, *out);

    out->playEnd = main.getNumSamples();

    // Slice points: the file's own markers when loaded (they replace the
    // detected transients, measured in FL), carried through the rate
    // conversion, Sample Start and Reverse.
    std::vector<int> marks;
    auto addMarks = [&] (const std::vector<int>& src)
    {
        const double ratio = (double) converted / (double) n;
        for (auto m : src)
        {
            int p = (int) std::lround (m * ratio) - start;
            if (p < 0 || p >= len)
                continue;
            marks.push_back (s.reverse ? len - p : p);
        }
    };
    if (s.loadRegions) addMarks (raw.regionMarkers);
    if (s.loadSlices)  addMarks (raw.sliceMarkers);
    if (! useFileMarkers (*out, std::move (marks)))
        out->onsets = detectOnsets (main);
    out->audio = std::move (main);
    out->computePeaks();
    return out;
}
