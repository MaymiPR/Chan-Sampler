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
        const auto pos = (int) std::floor (apos);
        // Attack detection on the way: block energies, a jump far above the
        // blocks just before it.
        constexpr int block = 128;
        float blockEnergy[64] = {};
        for (int i = 0; i < n; ++i)
        {
            float l, r;
            read (pos + i, l, r);
            blockEnergy[juce::jmin (63, i / block)] += l * l + r * r;
            spec[(size_t) i] = { l * window[(size_t) i], r * window[(size_t) i] };
        }
        int attackAt = -1;   // the first attack's block in this window
        for (int b = 4; b < juce::jmin (64, n / block) && attackAt < 0; ++b)
        {
            const auto before = 0.25f * (blockEnergy[b - 1] + blockEnergy[b - 2] + blockEnergy[b - 3] + blockEnergy[b - 4]);
            if (blockEnergy[b] > 8.0f * before + 1.0e-4f * block)
                attackAt = b;
        }
        const bool locked = lockFrames > 0;

        fft->perform (spec.data(), work.data(), false);   // JUCE's FFT is not in-place
        processSpectrum (pos, pitchRatio, formantCents, locked);  // work -> spec
        fft->perform (spec.data(), work.data(), true);
        const float scale = 1.0f / 1.5f;   // JUCE scales the inverse FFT; Hann^2 at 75% overlap sums to 1.5
        for (int i = 0; i < n; ++i)
        {
            olaL[(size_t) i] += work[(size_t) i].real() * window[(size_t) i] * scale;
            olaR[(size_t) i] += work[(size_t) i].imag() * window[(size_t) i] * scale;
        }
        prevPos = pos;
        first = false;
        // An attack on the way: the frames over it go 1:1 (the attack is
        // rebuilt as it is), placed so that the frame with the attack at its
        // centre is where the stretch puts it (on time). Afterwards the
        // analysis catches up, a quarter of the gap per frame.
        idealPos += ha;
        ++framesDone;   // the next frame's index
        if (locked)
        {
            apos += hop;
            --lockFrames;
        }
        else if (attackAt >= 0)
        {
            // Frame k's sample i lands at output (k hop + i - hop) / pitch
            // ratio (measured); the attack should sound at its own time
            // (source distance / timeRate). Find the frame that has it near
            // its centre there and step 1:1 up to it.
            const double attackPos = pos + (attackAt + 0.5) * block;
            const double alphaNow = (double) hop / ha;
            const double stretched = (attackPos - startPos) * alphaNow;   // where it belongs, in synthesis samples
            const int centreFrame = juce::jmax (framesDone, (int) std::round ((stretched - 0.5 * frame) / hop));
            const double centreStart = attackPos + (double) centreFrame * hop - stretched;
            apos = juce::jmax ((double) (pos - frame), centreStart - (double) (centreFrame - framesDone) * hop);   // may step back: the source is read by position
            lockFrames = centreFrame - framesDone + 1 + frame / hop / 2;
        }
        else
        {
            apos += juce::jlimit (0.25 * ha, 4.0 * ha, ha + 0.25 * (idealPos - apos));
        }
        emit (1.0f);
    }

    void processSpectrum (int pos, double pitchRatio, float formantCents, bool attack);
    void phaseLock (int channel, int analysisHop, bool transient);
    void formantEnvelopes();
    void emit (float gain);
    static float cubic (const std::vector<float>& b, int i, float t);

    Algo algo = Algo::wsola;
    bool preserveFormants = false;
    float transientThreshold = 1.5f;
    int frame = 1024, hop = 512, tolerance = 256, order = 10;
    float wsolaGain = 1.0f;
    double apos = 0.0, idealPos = 0.0;
    int prevPos = 0, lockFrames = 0, framesDone = 0;
    double startPos = 0.0;
    bool first = true, aligned = false;

    std::unique_ptr<juce::dsp::FFT> ffts[6];   // orders 8..13 (the envelope uses a quarter size)
    juce::dsp::FFT* fft = nullptr;
    std::vector<float> window, olaL, olaR, target;
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
