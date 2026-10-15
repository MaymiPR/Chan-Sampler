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
        const int n = frame;
        const auto pos = (int) std::floor (apos);
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < n; ++i)
            {
                float l, r;
                read (pos + i, l, r);
                spec[(size_t) i] = { (c == 0 ? l : r) * window[(size_t) i], 0.0f };
            }
            fft->perform (spec.data(), work.data(), false);   // JUCE's FFT is not in-place
            std::copy (work.begin(), work.begin() + n, spec.begin());
            processSpectrum (c, pos, pitchRatio, formantCents);
            fft->perform (spec.data(), work.data(), true);
            std::copy (work.begin(), work.begin() + n, spec.begin());
            auto& ola = c == 0 ? olaL : olaR;
            const float scale = 1.0f / 1.5f;   // JUCE scales the inverse FFT; Hann^2 at 75% overlap sums to 1.5
            for (int i = 0; i < n; ++i)
                ola[(size_t) i] += spec[(size_t) i].real() * window[(size_t) i] * scale;
        }
        prevPos = pos;
        first = false;
        apos += ha;
        emit (1.0f);
    }

    void processSpectrum (int channel, int pos, double pitchRatio, float formantCents);
    void emit (float gain);
    static float cubic (const std::vector<float>& b, int i, float t);

    Algo algo = Algo::wsola;
    bool preserveFormants = false;
    float transientThreshold = 1.5f;
    int frame = 1024, hop = 512, tolerance = 256, order = 10;
    float wsolaGain = 1.0f;
    double apos = 0.0;
    int prevPos = 0;
    bool first = true;

    std::unique_ptr<juce::dsp::FFT> ffts[4];   // orders 10..13
    juce::dsp::FFT* fft = nullptr;
    std::vector<float> window, olaL, olaR, target;
    std::vector<std::complex<float>> spec, cep, work;
    std::vector<float> prevPhase[2], synthPhase[2], prevMag[2], mag, phase, envelope;
    std::vector<int> peakOf;

    static constexpr int fifoSize = 32768;
    std::vector<float> fifoL, fifoR;
    int fifoCount = 0;
    double readPos = 0.0;
};
