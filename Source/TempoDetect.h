#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// FL's "Detect tempo" (sample menu, TIME > Autodetect): a sample's tempo in a
// range of BPM. The onset strength (spectral flux) is autocorrelated over the
// range's beat lengths; a loop is a whole number of beats long, so a tempo
// close to one of those lengths snaps to it (FL: a 2-bar loop at 128 BPM gives
// 128, and 64 in the 50 to 100 range).
namespace tempo
{
    double detect (const juce::AudioBuffer<float>& audio, double sampleRate, double lowBpm, double highBpm);

    // The sample's length in beats at a tempo.
    inline double beats (int frames, double sampleRate, double bpm)
    {
        return sampleRate > 0 ? (double) frames / sampleRate * bpm / 60.0 : 0.0;
    }

    // "128", "63.998": up to three decimals, trailing zeros dropped.
    juce::String format (double bpm);
}
