#include "TimeStretch.h"

namespace
{
    constexpr int maxFrame = 8192;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr float pi = juce::MathConstants<float>::pi;

    float wrapPhase (float x)
    {
        return x - twoPi * std::floor ((x + pi) / twoPi);
    }

    int roundToEven (double x) { return juce::jmax (64, 2 * juce::roundToInt (x * 0.5)); }

    int powerOfTwoNear (double x)
    {
        int order = 6;
        while ((1 << (order + 1)) <= (int) (x * 1.4142) && order < 13)
            ++order;
        return order;
    }
}

TimeStretcher::TimeStretcher()
{
    // Everything is allocated here, so configure / reset never allocate on
    // the audio thread.
    window.resize (maxFrame);
    olaL.resize (maxFrame);
    olaR.resize (maxFrame);
    target.resize (maxFrame);
    spec.resize (maxFrame);
    cep.resize (maxFrame);
    work.resize (maxFrame);
    for (int c = 0; c < 2; ++c)
    {
        prevPhase[c].resize (maxFrame / 2 + 1);
        synthPhase[c].resize (maxFrame / 2 + 1);
        prevMag[c].resize (maxFrame / 2 + 1);
    }
    mag.resize (maxFrame / 2 + 1);
    phase.resize (maxFrame / 2 + 1);
    envelope.resize (maxFrame);
    peakOf.resize (maxFrame / 2 + 1);
    fifoL.resize (fifoSize);
    fifoR.resize (fifoSize);
    for (int o = 10; o <= 13; ++o)
        ffts[o - 10] = std::make_unique<juce::dsp::FFT> (o);
}

void TimeStretcher::configure (StretchMode mode, double sourceRate)
{
    const auto scale = sourceRate / 44100.0;
    auto useWsola = [&] (double n44, double tol44)
    {
        algo = Algo::wsola;
        frame = juce::jmin (maxFrame, roundToEven (n44 * scale));
        hop = frame / 2;
        tolerance = juce::jmax (16, juce::roundToInt (tol44 * scale));
        preserveFormants = false;
    };
    auto useVocoder = [&] (double n44, float transient, bool formants)
    {
        algo = Algo::vocoder;
        order = juce::jlimit (10, 13, powerOfTwoNear (n44 * scale));
        frame = 1 << order;
        hop = frame / 4;
        transientThreshold = transient;
        preserveFormants = formants;
    };

    switch (mode)
    {
        case StretchMode::stretch:      useWsola (1024, 256); break;    // realtime: light, low latency
        case StretchMode::e3Mono:       useWsola (1536, 384); break;    // monophonic: longer search
        case StretchMode::e2Mono:       useWsola (1024, 320); break;
        case StretchMode::e2Speech:     useWsola (768, 192);  break;    // speech: short frames
        case StretchMode::sliceStretch: useWsola (1024, 256); break;    // restarted at each slice
        case StretchMode::stretchPro:   useVocoder (4096, 1.6f, true);  break;
        case StretchMode::e2Transient:  useVocoder (1024, 1.3f, false); break;
        case StretchMode::e2Generic:    useVocoder (2048, 2.0f, false); break;
        case StretchMode::e3Generic:
        case StretchMode::autoMode:
        default:                        useVocoder (2048, 1.5f, false); break;
    }

    for (int i = 0; i < frame; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (twoPi * (float) i / (float) frame);
    fft = algo == Algo::vocoder ? ffts[order - 10].get() : nullptr;
}

void TimeStretcher::reset (double sourcePos)
{
    apos = sourcePos;
    prevPos = (int) std::floor (sourcePos);
    first = true;
    std::fill (olaL.begin(), olaL.end(), 0.0f);
    std::fill (olaR.begin(), olaR.end(), 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        std::fill (prevPhase[c].begin(), prevPhase[c].end(), 0.0f);
        std::fill (synthPhase[c].begin(), synthPhase[c].end(), 0.0f);
        std::fill (prevMag[c].begin(), prevMag[c].end(), 0.0f);
    }
    fifoCount = 0;
    readPos = 0.0;
}

void TimeStretcher::emit (float gain)
{
    // Keep the FIFO from overflowing: drop what has been read.
    if (fifoCount + hop > fifoSize)
    {
        const int drop = juce::jlimit (0, fifoCount, (int) readPos - 1);
        std::copy (fifoL.begin() + drop, fifoL.begin() + fifoCount, fifoL.begin());
        std::copy (fifoR.begin() + drop, fifoR.begin() + fifoCount, fifoR.begin());
        fifoCount -= drop;
        readPos -= drop;
        if (fifoCount + hop > fifoSize)
            fifoCount = fifoSize - hop;
    }
    for (int i = 0; i < hop; ++i)
    {
        fifoL[(size_t) (fifoCount + i)] = olaL[(size_t) i] * gain;
        fifoR[(size_t) (fifoCount + i)] = olaR[(size_t) i] * gain;
    }
    fifoCount += hop;
    std::copy (olaL.begin() + hop, olaL.begin() + frame, olaL.begin());
    std::copy (olaR.begin() + hop, olaR.begin() + frame, olaR.begin());
    std::fill (olaL.begin() + (frame - hop), olaL.begin() + frame, 0.0f);
    std::fill (olaR.begin() + (frame - hop), olaR.begin() + frame, 0.0f);
}

float TimeStretcher::cubic (const std::vector<float>& b, int i, float t)
{
    const auto xm1 = i > 0 ? b[(size_t) (i - 1)] : b[(size_t) i];
    const auto x0 = b[(size_t) i], x1 = b[(size_t) (i + 1)], x2 = b[(size_t) (i + 2)];
    const auto c1 = 0.5f * (x1 - xm1);
    const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

void TimeStretcher::processSpectrum (int c, int pos, double pitchRatio, float formantCents)
{
    const int n = frame, half = n / 2;
    for (int k = 0; k <= half; ++k)
    {
        mag[(size_t) k] = std::abs (spec[(size_t) k]);
        phase[(size_t) k] = std::arg (spec[(size_t) k]);
    }

    // Transients (spectral flux jump): take the analysis phases as they are,
    // which keeps attacks sharp instead of smearing them.
    float rise = 0.0f, total = 1.0e-9f;
    for (int k = 1; k < half; ++k)
    {
        rise += juce::jmax (0.0f, mag[(size_t) k] - prevMag[c][(size_t) k]);
        total += prevMag[c][(size_t) k];
    }
    const bool transient = first || rise / total > transientThreshold;

    if (transient)
    {
        for (int k = 0; k <= half; ++k)
            synthPhase[c][(size_t) k] = phase[(size_t) k];
    }
    else
    {
        const int analysisHop = juce::jmax (1, pos - prevPos);
        // Peaks and their regions (identity phase locking, Laroche & Dolson).
        int lastPeak = 0;
        std::vector<int>& peaks = peakOf;
        for (int k = 0; k <= half; ++k)
            peaks[(size_t) k] = -1;
        int prev = -1;
        for (int k = 2; k < half - 1; ++k)
        {
            const auto m = mag[(size_t) k];
            if (m > mag[(size_t) (k - 1)] && m >= mag[(size_t) (k + 1)] && m > mag[(size_t) (k - 2)] && m >= mag[(size_t) (k + 2)])
            {
                // Advance the peak's phase by its measured frequency.
                const auto expected = twoPi * (float) k * (float) analysisHop / (float) n;
                const auto dev = wrapPhase (phase[(size_t) k] - prevPhase[c][(size_t) k] - expected);
                const auto omega = twoPi * (float) k / (float) n + dev / (float) analysisHop;
                synthPhase[c][(size_t) k] += omega * (float) hop;
                // Bins between this peak and the previous one follow the nearer peak.
                const int from = prev < 0 ? 0 : (prev + k) / 2 + 1;
                for (int j = from; j <= k; ++j)
                    peaks[(size_t) j] = k;
                if (prev >= 0)
                    for (int j = prev + 1; j < from; ++j)
                        peaks[(size_t) j] = prev;
                prev = k;
                lastPeak = k;
            }
        }
        for (int j = (prev < 0 ? 0 : prev + 1); j <= half; ++j)
            peaks[(size_t) j] = prev;
        juce::ignoreUnused (lastPeak);

        for (int k = 0; k <= half; ++k)
        {
            const int pk = peaks[(size_t) k];
            if (pk < 0)
            {
                const auto expected = twoPi * (float) k * (float) analysisHop / (float) n;
                const auto dev = wrapPhase (phase[(size_t) k] - prevPhase[c][(size_t) k] - expected);
                synthPhase[c][(size_t) k] += (twoPi * (float) k / (float) n + dev / (float) analysisHop) * (float) hop;
            }
            else if (pk != k)
            {
                synthPhase[c][(size_t) k] = synthPhase[c][(size_t) pk] + phase[(size_t) k] - phase[(size_t) pk];
            }
        }
    }

    // Formants (Stretch Pro): the voice resamples this signal by pitchRatio,
    // which would move the spectral envelope too; pre-correct it so that the
    // envelope ends up at its original place, shifted by FORMANT.
    float corr = 1.0f;
    const bool formants = preserveFormants && (std::abs (pitchRatio - 1.0) > 1.0e-4 || std::abs (formantCents) > 0.5f);
    if (formants)
    {
        // Cepstral envelope of the log magnitude.
        for (int k = 0; k < n; ++k)
        {
            const int b = k <= half ? k : n - k;
            cep[(size_t) k] = { std::log (mag[(size_t) b] + 1.0e-7f), 0.0f };
        }
        ffts[order - 10]->perform (cep.data(), work.data(), true);
        std::copy (work.begin(), work.begin() + n, cep.begin());
        const int lifter = juce::jmax (8, n / 64);
        for (int k = 0; k < n; ++k)
        {
            const bool keep = k < lifter || k > n - lifter;
            cep[(size_t) k] = keep ? cep[(size_t) k] : std::complex<float> (0.0f, 0.0f);   // JUCE already scaled the inverse
        }
        ffts[order - 10]->perform (cep.data(), work.data(), false);
        std::copy (work.begin(), work.begin() + n, cep.begin());
        for (int k = 0; k <= half; ++k)
            envelope[(size_t) k] = cep[(size_t) k].real();
    }
    const auto warp = (float) (pitchRatio / std::pow (2.0, (double) formantCents / 1200.0));

    // The correction moves energy between bands; keep the frame's total.
    double before = 0.0, after = 0.0;
    if (formants)
        for (int k = 0; k <= half; ++k)
        {
            const auto src = (float) k * warp;
            const int i0 = juce::jmin (half, (int) src);
            const auto t = juce::jmin (1.0f, src - (float) i0);
            const auto tg = src >= (float) half ? envelope[(size_t) half] - 6.0f
                                                : envelope[(size_t) i0] + (envelope[(size_t) juce::jmin (half, i0 + 1)] - envelope[(size_t) i0]) * t;
            const auto m2 = mag[(size_t) k] * std::exp (juce::jlimit (-6.0f, 6.0f, tg - envelope[(size_t) k]));
            before += (double) mag[(size_t) k] * mag[(size_t) k];
            after += (double) m2 * m2;
        }
    const auto norm = formants && after > 0.0 ? (float) std::sqrt (before / after) : 1.0f;

    for (int k = 0; k <= half; ++k)
    {
        auto m = mag[(size_t) k];
        if (formants)
        {
            const auto src = (float) k * warp;
            float tgt;
            if (src >= (float) half)
                tgt = envelope[(size_t) half] - 6.0f;
            else
            {
                const int i0 = (int) src;
                const auto t = src - (float) i0;
                tgt = envelope[(size_t) i0] + (envelope[(size_t) juce::jmin (half, i0 + 1)] - envelope[(size_t) i0]) * t;
            }
            corr = std::exp (juce::jlimit (-6.0f, 6.0f, tgt - envelope[(size_t) k]));
            m *= corr * norm;
        }
        spec[(size_t) k] = std::polar (m, synthPhase[c][(size_t) k]);
        if (k > 0 && k < half)
            spec[(size_t) (n - k)] = std::conj (spec[(size_t) k]);
        prevPhase[c][(size_t) k] = phase[(size_t) k];
        prevMag[c][(size_t) k] = mag[(size_t) k];
    }
}
