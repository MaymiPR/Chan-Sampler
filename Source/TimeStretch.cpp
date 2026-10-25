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
        ffts[o - 8] = std::make_unique<FastFFT> (o, fftScratch);
    hfAccL.resize (hfRingSize);
    hfAccR.resize (hfRingSize);
    hfWeight.resize (hfRingSize);
    hfSeg.resize (maxFrame);
    hfScore.resize (maxFrame);
    hfBuf.resize (maxFrame);
    hfWork.resize (maxFrame);
    hfKernel[0].resize (maxFrame / 2);
    hfKernel[1].resize (maxFrame);
    hfCacheL.resize (hfCacheSize);
    hfCacheR.resize (hfCacheSize);
    hfEnergyValue.resize (hfEnergySlots);
    hfEnergyIndex.resize (hfEnergySlots);
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
    // Pitch-synchronous grains of the whole band (the mono and speech modes):
    // the period when none is found, how long a note's start plays 1:1, and
    // whether PITCH respaces the grains (PSOLA, keeps formants) or resamples.
    double fallback44 = 256.0;
    auto useGrains = [&] (double fallback, double hold, bool keepFormants)
    {
        algo = Algo::grains;
        frame = hop = baseHop = juce::jmax (64, juce::roundToInt (256.0 * scale));
        preserveFormants = false;
        fallback44 = fallback;
        hfMonoHold = hold;
        psola = keepFormants;
    };

    // Chosen against FL renders of each mode (clicks, noise, a kick, a
    // Rhodes, a voice and two drum loops at -700 / +700 cents and x1.5 /
    // x0.667; see docs/EVIDENCE.md).
    switch (mode)
    {
        case StretchMode::e3Mono:       useGrains (256, 384, true); break;
        case StretchMode::e2Mono:       useGrains (128, 0, true); break;
        case StretchMode::e2Speech:     useGrains (384, 384, false); break;
        case StretchMode::sliceStretch: useWsola (1024, 256); break;    // restarted at each slice
        case StretchMode::stretchPro:   useVocoder (2048, 1.5f, true);  break;   // Stretch + formants (FL's two differ only with PITCH)
        case StretchMode::stretch:
        case StretchMode::e2Transient:
        case StretchMode::e2Generic:
        case StretchMode::e3Generic:
        case StretchMode::autoMode:
        default:                        useVocoder (2048, 1.5f, false); break;
    }

    // e3 generic: FL's 1.485 kHz split (measured on saws: harmonics up to
    // 1470 Hz take the low path, from 1500 Hz the grains). e2 generic's high
    // band is pieces of the source too; Stretch's and e2 transient's are
    // spectral (noise stretched x1.5: correlation with the source 0.8 / 0.2),
    // so those two keep e3's attack handling over the whole band.
    // A soft start's latency in FL (see softStart), the vocoder's and the
    // grains' (FL's bands differ: e2 generic's low band plays ~280 samples
    // after its high band at x1.5). e3 generic keeps its own: FL's low band
    // there plays a swell ~200 samples earlier when squeezing, but moving
    // ours made FL's sung "ah" pitched down null worse (in FL, -3.3 -> +4.5
    // dB at -1200 cents).
    soft15 = soft067 = hfSoft15 = hfSoft067 = 0.0;
    switch (mode)
    {
        case StretchMode::stretch:     soft15 = 57.0;  soft067 = 405.0; break;
        case StretchMode::stretchPro:  soft15 = 12.0;  soft067 = 267.0; break;
        case StretchMode::e2Transient: soft15 = -50.0; soft067 = 30.0;  break;
        case StretchMode::e2Generic:   soft15 = -157.0; soft067 = -30.0; hfSoft15 = 112.0; hfSoft067 = 96.0; break;
        case StretchMode::e3Generic:
        case StretchMode::autoMode:    hfSoft067 = 60.0; break;   // the grains, with the 1/512 ratio grid (measured with e3_am)
        default: break;
    }
    hybrid = mode == StretchMode::e3Generic || mode == StretchMode::autoMode || mode == StretchMode::e2Generic
          || mode == StretchMode::e2Transient || mode == StretchMode::stretch || mode == StretchMode::stretchPro;
    hfBand = hybrid && mode != StretchMode::e2Transient && mode != StretchMode::stretch && mode != StretchMode::stretchPro;
    e3Estimator = mode == StretchMode::e3Generic || mode == StretchMode::autoMode;
    ratioSteps = e3Estimator || mode == StretchMode::e2Transient ? 512 : mode == StretchMode::e2Generic ? 256 : 0;
    grainsOnly = algo == Algo::grains;
    // e3 times an attack by its peak, the grains by its start.
    hfScale = scale;
    attackSpan = hybrid ? juce::roundToInt (400.0 * scale) : 1;
    constexpr double split = 1485.0;
    lowBins = hfBand ? juce::roundToInt (split * frame / sourceRate) : 0;
    if (hfBand || grainsOnly)
    {
        hfMinPeriod = juce::roundToInt (40.0 * scale);
        hfMaxPeriod = juce::roundToInt (512.0 * scale);
        hfFallback = juce::roundToInt (fallback44 * scale);
        hfWindow = juce::roundToInt (512.0 * scale);
        hfEstOffset = juce::roundToInt (8.0 * scale);
        hfScale = scale;
        hfEstOrder = 11;
        while ((1 << hfEstOrder) < hfWindow + hfMaxPeriod + 1 && hfEstOrder < 13)
            ++hfEstOrder;
        // The high-pass is the complement of a windowed-sinc low-pass whose
        // transition matches the vocoder's bin cut (a frame-long Hann).
        hfHalfKernel = grainsOnly ? 0 : juce::jmin (frame / 2 - 1, 2047);
        hfFilterOrder = hfHalfKernel <= 1023 ? 12 : 13;
        hfBlock = grainsOnly ? 2048 : (1 << hfFilterOrder) - 2 * hfHalfKernel - 2;
        hfEstReuse = 512.0 * scale;
        const int M = hfHalfKernel;
        std::vector<double> lp ((size_t) (2 * M + 1));
        double sum = 0.0;
        for (int k = -M; k <= M; ++k)
        {
            const double x = 2.0 * split / sourceRate * k;
            const double sinc = k == 0 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
            const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * k / (M + 1));
            lp[(size_t) (k + M)] = sinc * w;
            sum += sinc * w;
        }
        for (int o = 12; o <= 13; ++o)
        {
            const int N = 1 << o;
            std::fill (spec.begin(), spec.begin() + N, std::complex<float>());
            for (int k = -M; k <= M; ++k)
                spec[(size_t) ((k + N) & (N - 1))] = { (float) ((k == 0 ? 1.0 : 0.0) - lp[(size_t) (k + M)] / sum), 0.0f };
            ffts[o - 8]->perform (spec.data(), work.data(), false);
            for (int i = 0; i < N; ++i)
                hfKernel[o - 12][(size_t) i] = work[(size_t) i].real();
        }
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
    rephaseShift = phaseShift = 0.0;
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
    hfStarted = false;
    hfCacheLo = hfCacheHi = 0;
    hfEstPos = -1.0e9;
    std::fill (hfEnergyIndex.begin(), hfEnergyIndex.end(), std::numeric_limits<int>::min());
    std::fill (hfAccL.begin(), hfAccL.end(), 0.0f);
    std::fill (hfAccR.begin(), hfAccR.end(), 0.0f);
    std::fill (hfWeight.begin(), hfWeight.end(), 0.0f);
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
    if (hfBand || grainsOnly)
    {
        // The grains for these synthesis samples (complete: no
        // later grain reaches back this far).
        const auto s0 = (long long) std::llround (synthPos);
        for (int i = 0; i < hop; ++i)
        {
            const auto idx = (size_t) ((s0 + i) & hfRingMask);
            const auto w = hfWeight[idx];
            if (w > 1.0e-3f)
            {
                fifoL[(size_t) (fifoCount + i)] += hfAccL[idx] / w;
                fifoR[(size_t) (fifoCount + i)] += hfAccR[idx] / w;
            }
            hfAccL[idx] = hfAccR[idx] = hfWeight[idx] = 0.0f;
        }
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

void TimeStretcher::processSpectrum (int pos, double pitchRatio, float formantCents, bool attack, bool mono)
{
    const int n = frame, half = n / 2;
    // A mono frame: channel 1 is channel 0 (copied, so its state stays exact).
    const int channels = mono ? 1 : 2;

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
    if (mono)
    {
        std::copy (bins[0].begin(), bins[0].begin() + half + 1, bins[1].begin());
        std::copy (mag[0].begin(), mag[0].begin() + half + 1, mag[1].begin());
    }

    const int analysisHop = juce::jmax (1, pos - prevPos);
    for (int c = 0; c < channels; ++c)
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
    if (lowBins > 0)   // e3 generic: the vocoder makes only the band below the split
        for (int c = 0; c < channels; ++c)
            std::fill (out[c].begin() + lowBins, out[c].begin() + half + 1, std::complex<float>());

    // Formants (Stretch Pro): the voice resamples this signal by pitchRatio,
    // which would move the spectral envelope too; pre-correct it so that the
    // envelope ends up at its original place, shifted by FORMANT.
    const bool formants = preserveFormants && (std::abs (pitchRatio - 1.0) > 1.0e-4 || std::abs (formantCents) > 0.5f);
    if (formants)
    {
        formantEnvelopes();
        const auto warp = (float) (pitchRatio / std::pow (2.0, (double) formantCents / 1200.0));
        for (int c = 0; c < channels; ++c)
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

    if (mono)
    {
        std::copy (synth[0].begin(), synth[0].begin() + half + 1, synth[1].begin());
        std::copy (out[0].begin(), out[0].begin() + half + 1, out[1].begin());
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
    // e3 generic keeps only the bins below the split, so only those (and a
    // few above, for the peaks near it) are worth the work.
    const int n = frame, half = lowBins > 0 ? juce::jmin (n / 2, lowBins + 8) : n / 2;
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
        auto turn = (twoPi * (float) k / (float) n + dev / (float) analysisHop) * (float) hop;
        if (phaseShift != 0.0)   // a low-band restart (scheduleRephase): turned by -frequency x the shift
            turn *= (float) (1.0 - phaseShift / hop);
        s[(size_t) k] = normalised (mul (s[(size_t) k], Complex (std::cos (turn), std::sin (turn))));
    };

    // Peaks, and which peak each bin follows (the nearer one).
    int count = 0;
    for (int k = 2; k < half - 1; ++k)
    {
        const auto v = m[(size_t) k];
        const bool peak = (v > m[(size_t) (k - 1)]) & (v >= m[(size_t) (k + 1)]) & (v > m[(size_t) (k - 2)]) & (v >= m[(size_t) (k + 2)]);
        peaks[(size_t) count] = k;   // without a branch: noise has peaks at random
        count += peak ? 1 : 0;
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
    const int n = frame, half = n / 2, quarter = n / 4, lifter = juce::jmax (8, juce::roundToInt (64.0 * hfScale));
    for (int k = 0; k <= half; ++k)
    {
        const Complex v (std::log (mag[0][(size_t) k] + 1.0e-7f), std::log (mag[1][(size_t) k] + 1.0e-7f));
        spec[(size_t) k] = v;
        if (k > 0 && k < half)
            spec[(size_t) (n - k)] = v;
    }
    fft->perform (spec.data(), work.data(), true);   // scaled by 1 / N
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
