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
    constexpr size_t bins1 = maxFrame / 2 + 1;
    window.resize (maxFrame);
    olaL.resize (maxFrame);
    olaR.resize (maxFrame);
    target.resize (maxFrame);
    power.resize (maxFrame);
    spec.resize (maxFrame);
    work.resize (maxFrame);
    for (int c = 0; c < 2; ++c)
    {
        bins[c].resize (bins1);
        prevBins[c].resize (bins1);
        synth[c].resize (bins1);
        out[c].resize (bins1);
        mag[c].resize (bins1);
        prevMag[c].resize (bins1);
        envelope[c].resize (bins1);
    }
    corr.resize (bins1);
    peakTurn.resize (bins1);
    peakOf.resize (bins1);
    peaks.resize (bins1);
    fifoL.resize (fifoSize);
    fifoR.resize (fifoSize);
    for (int o = 8; o <= 13; ++o)
        ffts[o - 8] = std::make_unique<juce::dsp::FFT> (o);
}

void TimeStretcher::configure (StretchMode mode, double sourceRate)
{
    const auto scale = sourceRate / 44100.0;
    auto useWsola = [&] (double n44, double tol44)
    {
        algo = Algo::wsola;
        frame = juce::jmin (maxFrame, roundToEven (n44 * scale));
        hop = baseHop = frame / 2;
        tolerance = juce::jmax (16, juce::roundToInt (tol44 * scale));
        preserveFormants = false;
    };
    auto useVocoder = [&] (double n44, float transient, bool formants)
    {
        algo = Algo::vocoder;
        order = juce::jlimit (10, 13, powerOfTwoNear (n44 * scale));
        frame = 1 << order;
        hop = baseHop = frame / 4;
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
    fft = algo == Algo::vocoder ? ffts[order - 8].get() : nullptr;
}

void TimeStretcher::reset (double sourcePos)
{
    apos = idealPos = startPos = sourcePos;
    lockFrames = 0;
    synthOffset = 0.0;
    synthPos = 0.0;
    hop = baseHop;
    framesDone = 0;
    aligned = false;
    prevPos = (int) std::floor (sourcePos);
    first = true;
    std::fill (olaL.begin(), olaL.end(), 0.0f);
    std::fill (olaR.begin(), olaR.end(), 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        std::fill (prevBins[c].begin(), prevBins[c].end(), std::complex<float>());
        std::fill (synth[c].begin(), synth[c].end(), std::complex<float> (1.0f, 0.0f));
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
    synthPos += hop;
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

namespace
{
    using Complex = std::complex<float>;

    inline Complex mul (Complex a, Complex b)        { return { a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real() }; }
    inline Complex mulConj (Complex a, Complex b)    { return { a.real() * b.real() + a.imag() * b.imag(), a.imag() * b.real() - a.real() * b.imag() }; }
    inline Complex unit (Complex x, float m)         { return m > 1.0e-20f ? Complex (x.real() / m, x.imag() / m) : Complex (1.0f, 0.0f); }
    inline Complex normalised (Complex x)
    {
        const auto m = std::sqrt (x.real() * x.real() + x.imag() * x.imag());
        return unit (x, m);
    }
}

void TimeStretcher::processSpectrum (int pos, double pitchRatio, float formantCents, bool attack)
{
    const int n = frame, half = n / 2;

    // Split the packed FFT (left + i right) into the two channels' spectra.
    for (int k = 0; k <= half; ++k)
    {
        const auto z = work[(size_t) k];
        const auto zc = std::conj (work[(size_t) ((n - k) & (n - 1))]);
        const Complex l (0.5f * (z.real() + zc.real()), 0.5f * (z.imag() + zc.imag()));
        const Complex d (0.5f * (z.real() - zc.real()), 0.5f * (z.imag() - zc.imag()));
        const Complex r (d.imag(), -d.real());   // d / i
        bins[0][(size_t) k] = l;
        bins[1][(size_t) k] = r;
        mag[0][(size_t) k] = std::sqrt (l.real() * l.real() + l.imag() * l.imag());
        mag[1][(size_t) k] = std::sqrt (r.real() * r.real() + r.imag() * r.imag());
    }

    const int analysisHop = juce::jmax (1, pos - prevPos);
    for (int c = 0; c < 2; ++c)
    {
        // Transients (spectral flux jump): take the analysis phases as they
        // are, which keeps attacks sharp instead of smearing them.
        float rise = 0.0f, total = 1.0e-9f;
        for (int k = 1; k < half; ++k)
        {
            rise += juce::jmax (0.0f, mag[c][(size_t) k] - prevMag[c][(size_t) k]);
            total += prevMag[c][(size_t) k];
        }
        phaseLock (c, analysisHop, first || attack || rise / total > transientThreshold);
    }

    // Formants (Stretch Pro): the voice resamples this signal by pitchRatio,
    // which would move the spectral envelope too; pre-correct it so that the
    // envelope ends up at its original place, shifted by FORMANT.
    const bool formants = preserveFormants && (std::abs (pitchRatio - 1.0) > 1.0e-4 || std::abs (formantCents) > 0.5f);
    if (formants)
    {
        formantEnvelopes();
        const auto warp = (float) (pitchRatio / std::pow (2.0, (double) formantCents / 1200.0));
        for (int c = 0; c < 2; ++c)
        {
            const auto& env = envelope[c];
            // The correction moves energy between bands; keep the frame's total.
            double before = 0.0, after = 0.0;
            for (int k = 0; k <= half; ++k)
            {
                const auto src = (float) k * warp;
                float tgt;
                if (src >= (float) half)
                    tgt = env[(size_t) half] - 6.0f;
                else
                {
                    const int i0 = (int) src;
                    tgt = env[(size_t) i0] + (env[(size_t) juce::jmin (half, i0 + 1)] - env[(size_t) i0]) * (src - (float) i0);
                }
                const auto g = std::exp (juce::jlimit (-6.0f, 6.0f, tgt - env[(size_t) k]));
                corr[(size_t) k] = g;
                const auto m = mag[c][(size_t) k];
                before += (double) m * m;
                after += (double) (m * g) * (m * g);
            }
            const auto norm = after > 0.0 ? (float) std::sqrt (before / after) : 1.0f;
            for (int k = 0; k <= half; ++k)
                out[c][(size_t) k] *= corr[(size_t) k] * norm;
        }
    }

    // Pack the two synthesis spectra for one inverse FFT (left + i right).
    // DC and Nyquist keep only their real parts, as the real output would.
    for (int k = 0; k <= half; ++k)
    {
        auto l = out[0][(size_t) k], r = out[1][(size_t) k];
        if (k == 0 || k == half)
        {
            l = { l.real(), 0.0f };
            r = { r.real(), 0.0f };
        }
        spec[(size_t) k] = { l.real() - r.imag(), l.imag() + r.real() };
        if (k > 0 && k < half)
            spec[(size_t) (n - k)] = { l.real() + r.imag(), r.real() - l.imag() };
    }
    for (int c = 0; c < 2; ++c)
    {
        std::copy (bins[c].begin(), bins[c].begin() + half + 1, prevBins[c].begin());
        std::copy (mag[c].begin(), mag[c].begin() + half + 1, prevMag[c].begin());
    }
}

void TimeStretcher::phaseLock (int c, int analysisHop, bool transient)
{
    // Identity phase locking (Laroche & Dolson) with phases kept as unit
    // phasors: each peak advances by its measured frequency, the bins around
    // it keep their phase offset to it. Only the peaks need an angle.
    const int n = frame, half = n / 2;
    auto& x = bins[c];
    auto& m = mag[c];
    auto& s = synth[c];
    auto& y = out[c];
    if (transient)
    {
        for (int k = 0; k <= half; ++k)
        {
            s[(size_t) k] = unit (x[(size_t) k], m[(size_t) k]);
            y[(size_t) k] = x[(size_t) k];
        }
        return;
    }

    auto advance = [&] (int k)   // the bin's phase moves by its measured frequency over one synthesis hop
    {
        const auto d = mulConj (x[(size_t) k], prevBins[c][(size_t) k]);
        const auto expected = twoPi * (float) k * (float) analysisHop / (float) n;
        const auto dev = wrapPhase (std::atan2 (d.imag(), d.real()) - expected);
        const auto turn = (twoPi * (float) k / (float) n + dev / (float) analysisHop) * (float) hop;
        s[(size_t) k] = normalised (mul (s[(size_t) k], Complex (std::cos (turn), std::sin (turn))));
    };

    // Peaks, and which peak each bin follows (the nearer one).
    int count = 0;
    for (int k = 2; k < half - 1; ++k)
    {
        const auto v = m[(size_t) k];
        if (v > m[(size_t) (k - 1)] && v >= m[(size_t) (k + 1)] && v > m[(size_t) (k - 2)] && v >= m[(size_t) (k + 2)])
            peaks[(size_t) count++] = k;
    }
    if (count == 0)
    {
        for (int k = 0; k <= half; ++k)
        {
            advance (k);
            y[(size_t) k] = s[(size_t) k] * m[(size_t) k];
        }
        return;
    }
    int prev = -1;
    for (int i = 0; i < count; ++i)
    {
        const int k = peaks[(size_t) i];
        const int from = prev < 0 ? 0 : (prev + k) / 2 + 1;
        for (int j = from; j <= k; ++j)
            peakOf[(size_t) j] = k;
        if (prev >= 0)
            for (int j = prev + 1; j < from; ++j)
                peakOf[(size_t) j] = prev;
        prev = k;
        advance (k);
        // Rotation from this frame's analysis phase to the synthesis phase
        peakTurn[(size_t) k] = mulConj (s[(size_t) k], unit (x[(size_t) k], m[(size_t) k]));
    }
    for (int j = prev + 1; j <= half; ++j)
        peakOf[(size_t) j] = prev;

    for (int k = 0; k <= half; ++k)
    {
        const int pk = peakOf[(size_t) k];
        if (pk != k)
        {
            const auto t = peakTurn[(size_t) pk];
            s[(size_t) k] = mul (unit (x[(size_t) k], m[(size_t) k]), t);
            y[(size_t) k] = mul (x[(size_t) k], t);
        }
        else
            y[(size_t) k] = s[(size_t) k] * m[(size_t) k];
    }
}

void TimeStretcher::formantEnvelopes()
{
    // Cepstral envelopes of both channels' log magnitudes (one packed inverse
    // FFT; both are real and even). Only the first n / 64 coefficients are
    // kept, so the envelope is smooth: it is evaluated every 4th bin by a
    // quarter-size FFT and interpolated in between.
    const int n = frame, half = n / 2, quarter = n / 4, lifter = juce::jmax (8, n / 64);
    for (int k = 0; k <= half; ++k)
    {
        const Complex v (std::log (mag[0][(size_t) k] + 1.0e-7f), std::log (mag[1][(size_t) k] + 1.0e-7f));
        spec[(size_t) k] = v;
        if (k > 0 && k < half)
            spec[(size_t) (n - k)] = v;
    }
    fft->perform (spec.data(), work.data(), true);   // JUCE scales the inverse
    std::fill (spec.begin(), spec.begin() + quarter, Complex());
    for (int q = 0; q < lifter; ++q)
        spec[(size_t) q] = work[(size_t) q];
    for (int q = 1; q < lifter; ++q)
        spec[(size_t) (quarter - q)] = work[(size_t) (n - q)];
    ffts[order - 2 - 8]->perform (spec.data(), work.data(), false);
    for (int k = 0; k <= half; ++k)
    {
        const int p = k >> 2;
        const auto t = (float) (k & 3) * 0.25f;
        const auto a = work[(size_t) p], b = work[(size_t) juce::jmin (quarter / 2, p + 1)];
        envelope[0][(size_t) k] = a.real() + (b.real() - a.real()) * t;
        envelope[1][(size_t) k] = a.imag() + (b.imag() - a.imag()) * t;
    }
}
