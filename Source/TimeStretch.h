#pragma once

#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <vector>

#include "Params.h"

// Real-time time stretching for the stretch modes (FL keeps pitch and length
// independent in these modes: a note's key changes the pitch, MUL / TIME the
// length; measured with FL renders, see docs/EVIDENCE.md).
//
// Each voice owns one stretcher. It analyses the source around a moving
// analysis position and builds a time-stretched copy at the source's own pitch
// into a FIFO; the voice reads that FIFO at its pitch increment, so
//   source advance per output sample = timeRate,
//   pitch ratio                     = increment (source frames / output sample).
//
// Algorithms (our own implementations; FL's Elastique engine is proprietary;
// each mode chosen against FL renders, see docs/EVIDENCE.md):
//   WSOLA          - waveform-similarity overlap-add (Slice stretch)
//   Phase vocoder  - identity phase locking + transient phase resets (e3/e2
//                    generic, e2 transient, Stretch: e3's attack handling;
//                    Stretch Pro adds formant preservation by cepstral
//                    envelope and the FORMANT shift).
//                    Attacks are kept as they are (like FL's e3): the frames
//                    holding one are taken 1:1 with the source's own phases,
//                    and the time that costs is made up afterwards.
//   e3 generic     - FL's structure (measured, nulltest/E3_NOTES.md): the
//                    vocoder only below 1.485 kHz; above, pitch-synchronous
//                    grains of the source (PSOLA): grains two periods long,
//                    one per period, a period repeated or skipped when the
//                    drift from the ideal position passes half a period, and
//                    a grain centred on each attack at its time. e2 generic
//                    the same.
//   Grains         - those grains over the whole band (e3 / e2 mono, e2
//                    speech): the mono modes respace them for PITCH (PSOLA,
//                    which keeps a voice's formants, as FL's do), e2 speech
//                    resamples them like the other modes.
class TimeStretcher
{
public:
    TimeStretcher();

    // Chooses the algorithm and frame size for a mode at the source's rate.
    void configure (StretchMode mode, double sourceRate);
    // Restarts at a source position (note start, slice start).
    void reset (double sourcePos);

    // One output sample. read (index, l, r) returns a source frame (silence
    // outside the sample; loops wrapped by the caller). formantCents shifts
    // the spectral envelope (Stretch Pro only).
    template <typename Reader>
    void next (Reader&& read, double increment, double timeRate, float formantCents, float& outL, float& outR)
    {
        const double alpha = juce::jlimit (0.05, 20.0, increment / juce::jmax (1.0e-6, timeRate));
        if (! aligned)
        {
            // Measured with clicks: the vocoder's output would run early by a
            // hop plus a quarter frame x (1 - 1 / alpha); WSOLA's late by a
            // quarter frame plus 0.58 / -0.42 frames per octave down / up.
            // Shift the analysis start so the source and the output line up
            // at any pitch.
            aligned = true;
            const auto octaves = std::log2 (alpha);
            const auto shift = algo == Algo::grains ? 0.0
                             : algo == Algo::vocoder ? vocoderHop (alpha) + 0.25 * frame * (1.0 - 1.0 / alpha)
                                                     : -frame * (0.25 - (octaves < 0.0 ? 0.58 : 0.42) * octaves);
            apos -= shift;
            idealPos -= shift;
            prevPos = (int) std::floor (apos);
        }
        while (fifoCount - (int) readPos < 4)
            synthesise (read, alpha, increment * sourceToOutputRateScale, formantCents);

        const int i = (int) readPos;
        const auto t = (float) (readPos - i);
        outL = cubic (fifoL, i, t);
        outR = cubic (fifoR, i, t);
        // The grains are made at the output's pitch (PSOLA), so the FIFO is
        // only resampled between the source's and the output's rates.
        readPos += algo == Algo::grains && psola ? 1.0 / sourceToOutputRateScale : increment;
    }

    // Pitch ratio = increment * this (output rate / source rate).
    double sourceToOutputRateScale = 1.0;

private:
    enum class Algo { wsola, vocoder, grains };

    template <typename Reader>
    void synthesise (Reader& read, double alpha, double pitchRatio, float formantCents)
    {
        // The vocoder's synthesis hop: a quarter frame, smaller below an
        // octave up (half the analysis rate, an eighth of a frame unpitched,
        // a sixteenth an octave down), so the source isn't smeared over
        // several frames (an octave down, a kick kept twice its length).
        // Chosen against FL's e3 renders of a kick, a Rhodes, a voice and
        // two drum loops at -1200 ... +1200 cents (nulltest/e3_pitch_cases.py);
        // it costs about twice the CPU of quarter-frame hops.
        if (algo == Algo::vocoder)
            hop = vocoderHop (alpha);
        // Analysis advance for one synthesis hop.
        const double ha = (double) hop / alpha;
        attackAlpha = alpha;
        attackPitch = pitchRatio;
        if (algo == Algo::grains)
        {
            // Synthesis runs at the source's rate and the output's pitch:
            // alpha / pitch synthesis samples per source sample.
            const double a = psola ? alpha / pitchRatio : alpha;
            attackAlpha = a;
            hfAdvance (read, a, pitchRatio, synthPos + hop);
            first = false;
            emit (1.0f);
        }
        else if (algo == Algo::wsola)
            wsolaFrame (read, ha);
        else
            vocoderFrame (read, ha, pitchRatio, formantCents);
    }

    int vocoderHop (double alpha) const
    {
        return juce::jlimit (baseHop / 4, baseHop, (int) std::lround (baseHop * juce::jmin (1.0, 0.5 * alpha)));
    }

    template <typename Reader>
    void wsolaFrame (Reader& read, double ha)
    {
        const int n = frame;
        const int overlap = n - hop;
        int best = 0;
        const auto base = (int) std::floor (apos);
        if (! first)
        {
            // Find the shift that best continues the previous frame.
            const int natural = prevPos + hop;
            for (int i = 0; i < overlap; i += 2)
            {
                float l, r;
                read (natural + i, l, r);
                target[(size_t) (i / 2)] = l + r;
            }
            double bestScore = -1.0e30;
            for (int d = -tolerance; d <= tolerance; d += 2)
            {
                double dot = 0.0, energy = 1.0e-9;
                for (int i = 0; i < overlap; i += 4)
                {
                    float l, r;
                    read (base + d + i, l, r);
                    const auto x = (double) (l + r);
                    dot += x * target[(size_t) (i / 2)];
                    energy += x * x;
                }
                const auto score = dot / std::sqrt (energy);
                if (score > bestScore) { bestScore = score; best = d; }
            }
        }
        const int pos = first ? base : base + best;
        for (int i = 0; i < n; ++i)
        {
            float l, r;
            read (pos + i, l, r);
            olaL[(size_t) i] += l * window[(size_t) i];
            olaR[(size_t) i] += r * window[(size_t) i];
        }
        prevPos = pos;
        first = false;
        apos += ha;
        emit (wsolaGain);
    }

    template <typename Reader>
    void vocoderFrame (Reader& read, double ha, double pitchRatio, float formantCents)
    {
        // Both channels go through one complex FFT each way: left as the real
        // part, right as the imaginary part (both are real signals).
        const int n = frame;
        auto pos = (int) std::floor (apos);
        constexpr int block = 128;
        float blockEnergy[64] = {};
        auto readWindow = [&]
        {
            std::fill (std::begin (blockEnergy), std::end (blockEnergy), 0.0f);
            for (int i = 0; i < n; ++i)
            {
                float l, r;
                read (pos + i, l, r);
                power[(size_t) i] = l * l + r * r;
                blockEnergy[juce::jmin (63, i / block)] += power[(size_t) i];
                spec[(size_t) i] = { l * window[(size_t) i], r * window[(size_t) i] };
            }
        };
        readWindow();

        bool locked = lockFrames > 0;
        if (! locked && framesDone == 0)
        {
            // The note's start is an attack when it isn't silent (FL's e3
            // starts a note at full level, not faded in): the frames start a
            // frame less a hop before it, so the start already has every
            // overlapping frame (1:1), and the output starts at it.
            const auto start = (int) std::ceil (startPos);
            float energy = 0.0f;
            for (int i = 0; i < block; ++i)
            {
                float l, r;
                read (start + i, l, r);
                energy += l * l + r * r;
            }
            if (energy > 1.0e-4f * block)
            {
                const int at = start - pos;
                synthOffset = (double) (frame - hop) - attackShift (std::log2 (pitchRatio)) * pitchRatio;
                readPos = synthOffset;
                apos = (double) (pos + at - (frame - hop));
                // Afterwards the analysis heads for where the output's
                // alignment wants it (see next(): synthesis s plays source
                // startPos + (s - synthOffset) / alpha, the frame starting a
                // hop plus a quarter frame x (1 - 1 / alpha) before that).
                const double alphaNow = (double) hop / ha;
                idealPos = startPos - synthOffset / alphaNow - (hop + 0.25 * frame * (1.0 - 1.0 / alphaNow));
                lockFrames = frame / hop;   // up to the frame that starts at it
                locked = true;
                pos = (int) std::floor (apos);
                readWindow();
            }
        }
        if (! locked)
        {
            // Attack detection: a block's energy far above the blocks just
            // before it, then the attack to the sample (FL's e3 puts each
            // one at the same place, measured with clicks): the first sample
            // over a tenth of the attack block's peak level.
            int attackAt = -1;
            for (int b = 4; b < juce::jmin (64, n / block) && attackAt < 0; ++b)
            {
                const auto before = 0.25f * (blockEnergy[b - 1] + blockEnergy[b - 2] + blockEnergy[b - 3] + blockEnergy[b - 4]);
                if (blockEnergy[b] > 8.0f * before + 1.0e-4f * block)
                    attackAt = b;
            }
            int attackSample = -1;
            if (attackAt >= 0)
            {
                float peak = 0.0f;
                for (int i = attackAt * block; i < (attackAt + 1) * block; ++i)
                    peak = juce::jmax (peak, power[(size_t) i]);
                attackSample = attackAt * block;
                for (int i = (attackAt - 1) * block; i < (attackAt + 1) * block; ++i)
                    if (power[(size_t) i] > 0.01f * peak) { attackSample = i; break; }
                attackSample = loudestAfter (read, pos + attackSample) - pos;
            }
            if (attackSample >= 0)
            {

                // The frames over it go 1:1 (the attack is rebuilt as it is),
                // starting with this one, placed so that the frame with the
                // attack at its centre is where the stretch puts it (on
                // time). Frame k's sample i lands at output (k hop + i - hop)
                // / pitch ratio (measured); the attack should sound at its
                // own time (source distance / timeRate), shifted like FL's.
                const double attackPos = pos + attackSample;
                const double alphaNow = (double) hop / ha;
                const double stretched = (attackPos - startPos) * alphaNow + attackShift (std::log2 (pitchRatio)) * pitchRatio + synthOffset;
                const int ahead = juce::jmax (0, (int) std::round ((stretched - 0.5 * frame - synthPos) / hop));   // frames to the centred one
                const double centreStart = attackPos + (synthPos + (double) ahead * hop) - stretched;
                apos = juce::jmax ((double) (pos - frame), centreStart - (double) ahead * hop);   // may step back: the source is read by position
                lockFrames = ahead + 1;   // up to the one centred on it
                locked = true;
                pos = (int) std::floor (apos);
                readWindow();
            }
        }

        fft->perform (spec.data(), work.data(), false);   // JUCE's FFT is not in-place
        processSpectrum (pos, pitchRatio, formantCents, locked);  // work -> spec
        fft->perform (spec.data(), work.data(), true);
        float scale = (float) hop / (0.375f * (float) n);   // JUCE scales the inverse FFT; Hann^2 overlaps sum to 0.375 frame / hop
        if (hybrid && locked)
            scale *= hop > ha ? 1.2f : 1.1f;   // FL's e3 plays the band below the split louder while it holds an attack (measured with noise bursts: x1.1-1.4, mean ~1.2 stretching, ~1.1 squeezing)
        for (int i = 0; i < n; ++i)
        {
            olaL[(size_t) i] += work[(size_t) i].real() * window[(size_t) i] * scale;
            olaR[(size_t) i] += work[(size_t) i].imag() * window[(size_t) i] * scale;
        }
        prevPos = pos;
        first = false;
        if (hfBand)
            hfAdvance (read, (double) hop / ha, pitchRatio, synthPos + hop);
        // Locked frames step 1:1; afterwards the analysis catches up, a
        // quarter of the gap per frame.
        idealPos += ha;
        ++framesDone;
        if (locked)
        {
            apos += hop;
            --lockFrames;
        }
        else
        {
            apos += juce::jlimit (0.25 * ha, 4.0 * ha, ha + 0.25 * (idealPos - apos));
        }
        emit (1.0f);
    }

    // Where an attack sounds against the source, in output samples, by the
    // pitch shift in octaves (measured with 1 kHz clicks in FL's e3 at -1200,
    // -600, +600, +1200 cents: down earlier, up later). e3 generic times
    // attacks like FL instead (loudestAfter), which accounts for this.
    double attackShift (double octaves) const
    {
        if (hybrid)
            return 0.0;
        static constexpr double at[] = { -8.3, -2.9, 0.0, 2.05, 3.35 };   // -1, -0.5, 0, 0.5, 1 octaves
        const auto x = juce::jlimit (-2.0, 2.0, octaves) * 2.0 + 2.0;
        const int i = juce::jlimit (0, 3, (int) std::floor (x));
        return at[i] + (x - i) * (at[i + 1] - at[i]);
    }

    //==============================================================================
    // e3 generic's high band: grains of the high-passed source, centred at
    // synthesis positions hfT (one per period), read from source positions hfP.
    template <typename Reader>
    void hfAdvance (Reader& read, double alpha, double pitchRatio, double until)
    {
        if (! hfStarted)
        {
            // Synthesis position s plays source startPos + (s - synthOffset) / alpha
            // (where the vocoder's output starts).
            hfStarted = true;
            hfT = 0.0;
            hfIdeal = startPos - synthOffset / alpha;
            hfP = hfIdeal;
            hfLastOnset = -1.0e9;
            hfLockUntil = -1.0e9;
            hfSearched = std::numeric_limits<int>::min();
            if (grainsOnly)
            {
                // A note that doesn't start silent plays 1:1 from its first
                // sample (FL's mono modes: the start is the source's own).
                const auto start = (int) std::ceil (startPos);
                float energy = 0.0f;
                for (int i = 0; i < 128; ++i)
                {
                    float l, r;
                    read (start + i, l, r);
                    energy += l * l + r * r;
                }
                if (energy > 1.0e-4f * 128.0f)
                {
                    hfP = hfIdeal = start;
                    hfLastOnset = start;
                    hfLockUntil = start + hfHold (alpha);
                }
            }
        }
        const double shift = attackShift (std::log2 (pitchRatio)) * pitchRatio;
        // PSOLA (the mono modes): the grains, a source period apart, are
        // placed a period / pitch apart, which shifts the pitch and keeps the
        // spectral envelope (FL's e3 / e2 mono keep a voice's formants).
        const double spacing = grainsOnly && psola ? 1.0 / pitchRatio : 1.0;
        while (hfT - hfMaxPeriod < until)
        {
            const int P = hfPeriod (read, hfP);
            // An attack coming within the crossover filter's reach: from here
            // the grains play the source 1:1 in the attack's timing (where the
            // stretch puts it), so the attack and the split's ringing around
            // it line up with the vocoder's band (FL plays every attack once,
            // on time); the drift is caught up after it.
            if (hfP >= hfLockUntil)
            {
                // From where the last search ended: the period can shrink
                // between grains (a click's own period as it comes near),
                // which would leave a gap.
                // The grain modes look ahead of the ideal position too: when
                // squeezing it runs up to half a period ahead of the grains.
                const double ahead = grainsOnly ? juce::jmax (hfP, hfIdeal) + P : hfP;
                const int from = juce::jmax (hfSearched, (int) std::lround (hfP) - P), to = (int) std::lround (ahead) + P + hfHalfKernel;
                hfSearched = juce::jmax (hfSearched, to);
                const int o = to > from ? hfOnset (read, from, to - from) : -1;
                if (o >= 0)
                {
                    hfLastOnset = o;
                    double at = hfT + ((double) o - hfIdeal) * alpha + shift;
                    if (grainsOnly)
                        at = juce::jmax (at, hfT);   // found late: now, not skipped
                    hfP = o - (at - hfT) / spacing;
                    hfLockUntil = o + hfHold (alpha);
                }
            }
            hfGrain (read, hfT, hfP, P, P);
            hfT += P * spacing;
            hfP += P;
            hfIdeal += P * spacing / alpha;
            if (hfP < hfLockUntil)
                continue;
            double dev = hfP - hfIdeal;
            while (dev > 0.5 * P) { hfP -= P; dev -= P; }
            while (dev < -0.5 * P) { hfP += P; dev += P; }
        }
    }

    // Where FL's e3 times an attack: the first sample, from where it starts
    // to the end of its search window, that reaches 95% of the loudest one.
    // With PITCH the window is 400 samples (pitched noise bursts: 16 of 20 to
    // the sample). With MUL / TIME only, it ends on a 512-sample grid counted
    // from the note's start (a grid window made pitched cases worse in FL):
    // at the first grid point (offset ~410 at x1.5 / x2, ~364 at x0.667) at
    // least ~240 (x1.5 / x2) / ~144 (x0.667) samples after the attack's start
    // (measured: 64 identical bursts stepped 8 samples through the grid, and
    // 20 flat / decaying / rising ones: 80 of 84 timed to the sample at x1.5).
    template <typename Reader>
    int loudestAfter (Reader& read, int start)
    {
        if (attackSpan <= 1)
            return start;
        int span = attackSpan;   // with PITCH: 400 samples (measured with pitched noise bursts)
        if (std::abs (attackPitch - 1.0) < 1.0e-3)
        {
            // Length only (MUL / TIME): the grid-ended window.
            const double x = juce::jlimit (0.0, 1.0, (std::log2 (attackAlpha) + 0.585) / 1.17);
            const double grid = 512.0 * hfScale, offset = (364.0 + x * 46.0) * hfScale, gap = (144.0 + x * 96.0) * hfScale;
            const double rel = start - startPos;
            const double end = startPos + offset + grid * std::ceil ((rel + gap - offset) / grid);
            span = juce::jlimit (1, 4096, (int) std::lround (end - start));
        }
        float peak = 0.0f;
        for (int i = 0; i < span; ++i)
        {
            float l, r;
            read (start + i, l, r);
            peak = juce::jmax (peak, l * l + r * r);
        }
        for (int i = 0; i < span; ++i)
        {
            float l, r;
            read (start + i, l, r);
            if (l * l + r * r >= 0.9025f * peak)
                return start + i;
        }
        return start;
    }

    // How long FL's e3 plays an attack 1:1 before the grains catch up, in
    // samples, by the stretch (measured: the first grain run after a note
    // start of noise at 0.5, 0.667, 0.8, 1.25, 1.5 and 2 times the length).
    double hfHold (double alpha) const
    {
        if (grainsOnly)
            return hfScale * hfMonoHold;
        static constexpr double oct[] = { -1.0, -0.585, -0.322, 0.322, 0.585, 1.0 };
        static constexpr double len[] = { 336.0, 368.0, 528.0, 704.0, 1000.0, 1150.0 };
        const double x = juce::jlimit (-1.0, 1.0, std::log2 (alpha));
        int i = 0;
        while (i < 4 && x > oct[i + 1])
            ++i;
        return hfScale * (len[i] + (x - oct[i]) / (oct[i + 1] - oct[i]) * (len[i + 1] - len[i]));
    }

    // The period at a source position, as FL's e3 finds it (measured with
    // noise plus one echo: the jumps equal the echo's delay from 40 to 512
    // samples, random outside): the normalised autocorrelation's peak over
    // 40..512 samples (at 44.1 kHz) of the high band, 512 samples from the
    // grain's centre on. Kept for grains less than 512 samples apart.
    template <typename Reader>
    int hfPeriod (Reader& read, double p)
    {
        if (std::abs (p - hfEstPos) < hfEstReuse)
            return hfEstPeriod;
        hfEstPos = p;
        const int W = hfWindow, lo = hfMinPeriod, hi = hfMaxPeriod;
        const int a = (int) std::floor (p) + hfEstOffset;
        const int len = W + hi + 1, N = 1 << hfEstOrder;
        hfEnsure (read, a, a + len);
        double eRef = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const float m = i < len ? hfCacheL[(size_t) ((a + i) & (hfCacheSize - 1))] + hfCacheR[(size_t) ((a + i) & (hfCacheSize - 1))] : 0.0f;
            hfSeg[(size_t) i] = m;
            hfBuf[(size_t) i] = { i < W ? m : 0.0f, m };   // reference + i segment
            if (i < W)
                eRef += (double) m * m;
        }
        hfEstPeriod = hfFallback;
        if (eRef < 1.0e-10)
            return hfEstPeriod;
        auto* f = ffts[hfEstOrder - 8].get();
        f->perform (hfBuf.data(), hfWork.data(), false);
        for (int k = 0; k < N; ++k)
        {
            const auto x = hfWork[(size_t) k], y = std::conj (hfWork[(size_t) ((N - k) & (N - 1))]);
            const std::complex<float> ref (0.5f * (x + y)), seg ((x - y) * std::complex<float> (0.0f, -0.5f));
            hfBuf[(size_t) k] = std::conj (ref) * seg;
        }
        f->perform (hfBuf.data(), hfWork.data(), true);
        double eSeg = 0.0;
        for (int i = 0; i < W; ++i)
            eSeg += (double) hfSeg[(size_t) (lo + i)] * hfSeg[(size_t) (lo + i)];
        double bestScore = 0.3;
        for (int l = lo; l <= hi; ++l)
        {
            if (l > lo)
                eSeg += (double) hfSeg[(size_t) (l + W - 1)] * hfSeg[(size_t) (l + W - 1)]
                      - (double) hfSeg[(size_t) (l - 1)] * hfSeg[(size_t) (l - 1)];
            const double score = hfWork[(size_t) l].real() / std::sqrt (eRef * juce::jmax (1.0e-12, eSeg));
            hfScore[(size_t) l] = (float) score;
            if (score > bestScore) { bestScore = score; hfEstPeriod = l; }
        }
        if (grainsOnly)
        {
            // PSOLA needs the period itself: the best peak after the
            // correlation first falls (a low tone still correlates well at
            // short lags), then the first peak close to it (a multiple of the
            // period correlates as well).
            int from = lo;
            while (from <= hi && hfScore[(size_t) from] >= hfDip)
                ++from;
            double best = 0.3;
            hfEstPeriod = hfFallback;
            for (int l = from + 1; l < hi; ++l)
                if (hfScore[(size_t) l] > best && hfScore[(size_t) l] >= hfScore[(size_t) (l - 1)] && hfScore[(size_t) l] >= hfScore[(size_t) (l + 1)])
                {
                    best = hfScore[(size_t) l];
                    hfEstPeriod = l;
                }
            for (int l = from + 1; l < hfEstPeriod; ++l)
                if (hfScore[(size_t) l] >= hfPeakShare * best && hfScore[(size_t) l] >= hfScore[(size_t) (l - 1)] && hfScore[(size_t) l] >= hfScore[(size_t) (l + 1)])
                {
                    hfEstPeriod = l;
                    break;
                }
        }
        return hfEstPeriod;
    }

    // A 128-sample block's energy (block k covers 128 k ...), cached.
    template <typename Reader>
    float hfBlockEnergy (Reader& read, int k)
    {
        const auto slot = (size_t) (k & (hfEnergySlots - 1));
        if (hfEnergyIndex[slot] != k)
        {
            float e = 0.0f;
            for (int i = 0; i < 128; ++i)
            {
                float l, r;
                read (k * 128 + i, l, r);
                e += l * l + r * r;
            }
            hfEnergyIndex[slot] = k;
            hfEnergyValue[slot] = e;
        }
        return hfEnergyValue[slot];
    }

    // An attack starting in [p0, p0 + span): a 128-sample block far above the
    // four before it (the vocoder's rule), to the first loud sample.
    template <typename Reader>
    int hfOnset (Reader& read, int p0, int span)
    {
        constexpr int block = 128;
        const int k0 = (int) std::floor ((double) p0 / block), k1 = (int) std::floor ((double) (p0 + span - 1) / block);
        for (int k = k0; k <= k1; ++k)
        {
            const int start = k * block;
            if (start <= hfLastOnset + 1024)
                continue;
            const float before = 0.25f * (hfBlockEnergy (read, k - 1) + hfBlockEnergy (read, k - 2) + hfBlockEnergy (read, k - 3) + hfBlockEnergy (read, k - 4));
            const float e = hfBlockEnergy (read, k);
            if (e > 8.0f * before + 1.0e-4f * block)
            {
                float peak = 0.0f;
                for (int i = 0; i < block; ++i)
                {
                    float l, r;
                    read (start + i, l, r);
                    peak = juce::jmax (peak, l * l + r * r);
                }
                for (int i = -block; i < block; ++i)
                {
                    float l, r;
                    read (start + i, l, r);
                    if (l * l + r * r > 0.01f * peak)
                        return loudestAfter (read, start + i);
                }
                return loudestAfter (read, start);
            }
        }
        return -1;
    }

    // The high-passed source, filtered once per block (overlap-save with the
    // split's kernel) into a cache over the source positions in use.
    template <typename Reader>
    void hfEnsure (Reader& read, int lo, int hi)
    {
        const int B = hfBlock;
        const int blo = (int) std::floor ((double) lo / B) * B, bhi = (int) std::ceil ((double) hi / B) * B;
        if (hfCacheHi <= hfCacheLo || blo < hfCacheLo - hfCacheSize / 2 || bhi > hfCacheHi + hfCacheSize / 2)
            hfCacheLo = hfCacheHi = blo;
        while (blo < hfCacheLo)
        {
            while (hfCacheHi - (hfCacheLo - B) > hfCacheSize)
                hfCacheHi -= B;
            hfCacheLo -= B;
            hfFilterBlock (read, hfCacheLo);
        }
        while (bhi > hfCacheHi)
        {
            while (hfCacheHi + B - hfCacheLo > hfCacheSize)
                hfCacheLo += B;
            hfFilterBlock (read, hfCacheHi);
            hfCacheHi += B;
        }
    }

    template <typename Reader>
    void hfFilterBlock (Reader& read, int b)
    {
        const int M = hfHalfKernel, B = hfBlock, N = 1 << hfFilterOrder;
        if (grainsOnly)   // the whole band: the source as it is
        {
            for (int i = 0; i < B; ++i)
            {
                const auto slot = (size_t) ((b + i) & (hfCacheSize - 1));
                read (b + i, hfCacheL[slot], hfCacheR[slot]);
            }
            return;
        }
        for (int i = 0; i < N; ++i)
        {
            float l = 0.0f, r = 0.0f;
            if (i < B + 2 * M)
                read (b - M + i, l, r);
            hfBuf[(size_t) i] = { l, r };
        }
        auto* f = ffts[hfFilterOrder - 8].get();
        f->perform (hfBuf.data(), hfWork.data(), false);
        const auto& k = hfKernel[hfFilterOrder - 12];
        for (int i = 0; i < N; ++i)
            hfWork[(size_t) i] *= k[(size_t) i];
        f->perform (hfWork.data(), hfBuf.data(), true);
        for (int i = 0; i < B; ++i)
        {
            const auto slot = (size_t) ((b + i) & (hfCacheSize - 1));
            hfCacheL[slot] = hfBuf[(size_t) (M + i)].real();
            hfCacheR[slot] = hfBuf[(size_t) (M + i)].imag();
        }
    }

    // One grain: the high-passed source around p, a rising Hann half over
    // `left` samples and a falling one over `right` (a period each), added
    // at synthesis t.
    template <typename Reader>
    void hfGrain (Reader& read, double t, double p, int left, int right)
    {
        const int tc = (int) std::lround (t), pc = (int) std::lround (p);
        hfEnsure (read, pc - left, pc + right);
        for (int i = -left; i < right; ++i)
        {
            const float w = i < 0 ? 0.5f + 0.5f * std::cos (hfPi * (float) i / (float) left)
                                  : 0.5f + 0.5f * std::cos (hfPi * (float) i / (float) right);
            const auto idx = (size_t) ((tc + i) & hfRingMask);
            const auto slot = (size_t) ((pc + i) & (hfCacheSize - 1));
            hfAccL[idx] += w * hfCacheL[slot];
            hfAccR[idx] += w * hfCacheR[slot];
            hfWeight[idx] += w;
        }
    }

    void processSpectrum (int pos, double pitchRatio, float formantCents, bool attack);
    void phaseLock (int channel, int analysisHop, bool transient);
    void formantEnvelopes();
    void emit (float gain);
    static float cubic (const std::vector<float>& b, int i, float t);

    Algo algo = Algo::wsola;
    bool preserveFormants = false;
    float transientThreshold = 1.5f;
    int frame = 1024, hop = 512, baseHop = 512, tolerance = 256, order = 10;
    double synthPos = 0.0;      // where the next frame starts in the synthesised stream
    float wsolaGain = 1.0f;
    double apos = 0.0, idealPos = 0.0;
    int prevPos = 0, lockFrames = 0, framesDone = 0;
    double startPos = 0.0;
    double synthOffset = 0.0;   // where the output starts in the synthesised stream
    int attackSpan = 1;         // > 1: time attacks like FL's e3 (loudestAfter)
    double attackAlpha = 1.0, attackPitch = 1.0;   // the stretch and pitch ratio in use (attack timing)
    bool first = true, aligned = false;

    std::unique_ptr<juce::dsp::FFT> ffts[6];   // orders 8..13 (the envelope uses a quarter size)
    juce::dsp::FFT* fft = nullptr;
    std::vector<float> window, olaL, olaR, target, power;
    std::vector<std::complex<float>> spec, work;
    // Per channel: this frame's and the last frame's analysis spectra, the
    // synthesis phase as a unit phasor, magnitudes and the formant envelope.
    std::vector<std::complex<float>> bins[2], prevBins[2], synth[2], out[2], peakTurn;
    std::vector<float> mag[2], prevMag[2], envelope[2], corr;
    std::vector<int> peakOf, peaks;

    static constexpr int fifoSize = 32768;
    std::vector<float> fifoL, fifoR;
    int fifoCount = 0;
    double readPos = 0.0;

    // e3 generic's split and high-band grains (see hfAdvance)
    static constexpr float hfPi = juce::MathConstants<float>::pi;
    static constexpr int hfRingSize = 1 << 15, hfRingMask = hfRingSize - 1;
    bool hybrid = false, hfBand = false, grainsOnly = false, psola = true, hfStarted = false;
    int lowBins = 0;                       // vocoder bins kept (below the split)
    int hfWindow = 1024, hfMinPeriod = 90, hfMaxPeriod = 600, hfFallback = 256, hfEstOrder = 11, hfHalfKernel = 1023;
    int hfFilterOrder = 12, hfBlock = 2048, hfCacheLo = 0, hfCacheHi = 0, hfEstPeriod = 256, hfEstOffset = 8;
    double hfEstPos = -1.0e9, hfEstReuse = 128.0, hfScale = 1.0;
    static constexpr int hfCacheSize = 1 << 15, hfEnergySlots = 1 << 12;
    double hfT = 0.0, hfP = 0.0, hfIdeal = 0.0, hfLastOnset = -1.0e9, hfLockUntil = -1.0e9;
    int hfSearched = 0;   // where the attack search has reached
    float hfPeakShare = 0.9f, hfDip = 0.5f;
    double hfMonoHold = 384.0;
    std::vector<float> hfAccL, hfAccR, hfWeight, hfSeg, hfScore, hfCacheL, hfCacheR, hfEnergyValue;
    std::vector<int> hfEnergyIndex;
    std::vector<float> hfKernel[2];         // high-pass spectra, FFT orders 12 and 13
    std::vector<std::complex<float>> hfBuf, hfWork;
};
