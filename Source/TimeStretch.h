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
// Algorithms (our own implementations; FL's Elastique engine is proprietary):
//   WSOLA          - waveform-similarity overlap-add (Stretch, the mono modes, speech)
//   Phase vocoder  - identity phase locking + transient phase resets (e3/e2
//                    generic and transient); Stretch Pro adds formant
//                    preservation by cepstral envelope and the FORMANT shift.
//                    Attacks are kept as they are (like FL's e3): the frames
//                    holding one are taken 1:1 with the source's own phases,
//                    and the time that costs is made up afterwards.
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
            const auto shift = algo == Algo::vocoder ? hop + 0.25 * frame * (1.0 - 1.0 / alpha)
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
        readPos += increment;
    }

    // Pitch ratio = increment * this (output rate / source rate).
    double sourceToOutputRateScale = 1.0;

private:
    enum class Algo { wsola, vocoder };

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
            hop = juce::jlimit (baseHop / 4, baseHop, (int) std::lround (baseHop * juce::jmin (1.0, 0.5 * alpha)));
        // Analysis advance for one synthesis hop.
        const double ha = (double) hop / alpha;
        if (algo == Algo::wsola)
            wsolaFrame (read, ha);
        else
            vocoderFrame (read, ha, pitchRatio, formantCents);
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
            const auto at = (int) std::ceil (startPos) - pos;
            float energy = 0.0f;
            for (int i = juce::jmax (0, at); i < juce::jmin (n, at + block); ++i)
                energy += power[(size_t) i];
            if (at >= 0 && at < n - block && energy > 1.0e-4f * block)
            {
                synthOffset = (double) (frame - hop) - attackShift (std::log2 (pitchRatio)) * pitchRatio;
                readPos = synthOffset;
                apos = idealPos = (double) (pos + at - (frame - hop));
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
            if (attackAt >= 0)
            {
                float peak = 0.0f;
                for (int i = attackAt * block; i < (attackAt + 1) * block; ++i)
                    peak = juce::jmax (peak, power[(size_t) i]);
                int attackSample = attackAt * block;
                for (int i = (attackAt - 1) * block; i < (attackAt + 1) * block; ++i)
                    if (power[(size_t) i] > 0.01f * peak) { attackSample = i; break; }

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
        const float scale = (float) hop / (0.375f * (float) n);   // JUCE scales the inverse FFT; Hann^2 overlaps sum to 0.375 frame / hop
        for (int i = 0; i < n; ++i)
        {
            olaL[(size_t) i] += work[(size_t) i].real() * window[(size_t) i] * scale;
            olaR[(size_t) i] += work[(size_t) i].imag() * window[(size_t) i] * scale;
        }
        prevPos = pos;
        first = false;
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

    // Where FL's e3 sounds an attack against the source, in output samples,
    // by the pitch shift in octaves (measured with clicks at -1200, -600,
    // +600, +1200 cents: down puts it earlier, up later).
    static double attackShift (double octaves)
    {
        static constexpr double at[] = { -8.3, -2.9, 0.0, 2.05, 3.35 };   // -1, -0.5, 0, 0.5, 1 octaves
        const auto x = juce::jlimit (-2.0, 2.0, octaves) * 2.0 + 2.0;
        const int i = juce::jlimit (0, 3, (int) std::floor (x));
        return at[i] + (x - i) * (at[i + 1] - at[i]);
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
};
