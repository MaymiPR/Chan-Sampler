#include "TempoDetect.h"

#include <juce_dsp/juce_dsp.h>

namespace tempo
{
namespace
{
    // Onset strength per hop: the summed rise of the log spectrum.
    std::vector<float> onsetStrength (const juce::AudioBuffer<float>& audio, double sampleRate, double& framesPerSecond)
    {
        constexpr int order = 10, size = 1 << order;
        const int hop = juce::jmax (64, (int) std::round (512.0 * sampleRate / 44100.0));
        const int frames = juce::jmin (audio.getNumSamples(), (int) (sampleRate * 120.0));   // two minutes are plenty
        framesPerSecond = sampleRate / hop;

        juce::dsp::FFT fft (order);
        std::vector<float> window ((size_t) size), buffer ((size_t) size * 2), previous ((size_t) size / 2 + 1, 0.0f);
        for (int i = 0; i < size; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) size);

        std::vector<float> flux;
        const int channels = audio.getNumChannels();
        for (int start = -size / 2; start + size / 2 < frames; start += hop)
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            for (int i = 0; i < size; ++i)
            {
                const int n = start + i;
                if (n < 0 || n >= frames)
                    continue;
                float x = 0;
                for (int c = 0; c < channels; ++c)
                    x += audio.getSample (c, n);
                buffer[(size_t) i] = x * window[(size_t) i];
            }
            fft.performFrequencyOnlyForwardTransform (buffer.data(), true);
            float rise = 0;
            for (size_t k = 1; k < previous.size(); ++k)
            {
                const auto m = std::log1p (100.0f * buffer[k]);
                rise += juce::jmax (0.0f, m - previous[k]);
                previous[k] = m;
            }
            flux.push_back (rise);
        }

        // Remove the slow level so the beat's pulses stand out.
        if (! flux.empty())
        {
            const int span = juce::jmax (1, (int) framesPerSecond / 2);
            std::vector<float> out (flux.size());
            double sum = 0;
            int count = 0, lo = 0, hi = 0;
            for (size_t i = 0; i < flux.size(); ++i)
            {
                while (hi < (int) flux.size() && hi <= (int) i + span) { sum += flux[(size_t) hi++]; ++count; }
                while (lo < (int) i - span) { sum -= flux[(size_t) lo++]; --count; }
                out[i] = juce::jmax (0.0f, flux[i] - (float) (sum / juce::jmax (1, count)));
            }
            flux.swap (out);
        }
        return flux;
    }

    // Normalised autocorrelation at a fractional lag.
    double autocorrelation (const std::vector<float>& x, double lag)
    {
        const int whole = (int) lag;
        const auto f = lag - whole;
        if (whole + 1 >= (int) x.size())
            return 0.0;
        double sum = 0, energy = 0;
        for (size_t i = 0; i + (size_t) whole + 1 < x.size(); ++i)
        {
            const double later = x[i + (size_t) whole] * (1.0 - f) + x[i + (size_t) whole + 1] * f;
            sum += x[i] * later;
            energy += x[i] * x[i];
        }
        return energy > 0 ? sum / energy : 0.0;
    }

    // How well a beat length (in hops) fits: the beat itself, plus its half
    // and its double (the bar's pulses and the off-beats).
    double score (const std::vector<float>& onsets, double framesPerSecond, double bpm)
    {
        const auto beat = 60.0 / bpm * framesPerSecond;
        return autocorrelation (onsets, beat) + 0.5 * autocorrelation (onsets, 2.0 * beat) + 0.25 * autocorrelation (onsets, 4.0 * beat)
             + 0.25 * autocorrelation (onsets, 0.5 * beat);
    }
}

double detect (const juce::AudioBuffer<float>& audio, double sampleRate, double lowBpm, double highBpm)
{
    if (audio.getNumSamples() == 0 || sampleRate <= 0 || highBpm <= lowBpm)
        return 0.0;
    double fps = 0;
    const auto onsets = onsetStrength (audio, sampleRate, fps);
    if (onsets.size() < 8)
        return 0.0;

    // The best tempo in the range, then refined between its neighbours.
    double best = lowBpm, bestScore = -1.0e9;
    for (double bpm = lowBpm; bpm <= highBpm + 1.0e-9; bpm += 0.25)
        if (const auto s = score (onsets, fps, bpm); s > bestScore)
        {
            bestScore = s;
            best = bpm;
        }
    for (double step = 0.1; step >= 0.001; step *= 0.1)
    {
        const auto from = best;
        for (int i = -3; i <= 3; ++i)
        {
            const auto bpm = juce::jlimit (lowBpm, highBpm, from + i * step);
            if (const auto s = score (onsets, fps, bpm); s > bestScore)
            {
                bestScore = s;
                best = bpm;
            }
        }
    }

    // A loop is a whole number of beats long, most often 4, 8, 16... : the
    // tempos that make it so are preferred when they fit nearly as well.
    const auto seconds = (double) audio.getNumSamples() / sampleRate;
    if (seconds >= 1.0)
    {
        double loopBest = 0, loopScore = -1.0e9;
        for (int n = juce::jmax (1, (int) std::ceil (lowBpm * seconds / 60.0)); n <= (int) std::floor (highBpm * seconds / 60.0); ++n)
        {
            const auto bpm = n * 60.0 / seconds;
            const auto s = score (onsets, fps, bpm);
            const bool powerOfTwo = n >= 4 && (n & (n - 1)) == 0;
            const auto weighted = s * (powerOfTwo ? 1.25 : n % 4 == 0 ? 1.1 : 1.0);
            if (s >= 0.75 * bestScore && weighted > loopScore)
            {
                loopScore = weighted;
                loopBest = bpm;
            }
        }
        if (loopBest > 0)
            best = loopBest;
    }
    return best;
}

juce::String format (double bpm)
{
    auto t = juce::String (bpm, 3);
    while (t.containsChar ('.') && (t.endsWithChar ('0') || t.endsWithChar ('.')))
        t = t.dropLastCharacters (1);
    return t;
}
}
