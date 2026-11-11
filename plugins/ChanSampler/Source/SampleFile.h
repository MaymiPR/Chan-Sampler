#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/SampleData.h"

namespace cs
{

// The loaded sample file: its audio and what the precomputed processes read (dsp::SampleSource), plus the file's own
// facts that the editor shows and a project keeps.
struct RawSample : dsp::SampleSource
{
    int bitsPerSample = 16;
    juce::File file;
    // A project's sample that could not be loaded (not found, or unreadable):
    // no audio, but the path stays, so the project keeps it and the sample
    // reloads once the file is back.
    bool missing = false;
    juce::int64 fileTime = 0, fileSize = 0;   // the file's modification time (ms) and size when loaded
    double fileTempo = 0;                     // a WAV's ACID tempo (BPM), 0 when it has none
    juce::String format;              // e.g. "WAV, 16-bit PCM", "WAV, compressed (Ogg Vorbis)", "FLAC"
    // Loop points embedded in the file (WAV "smpl" chunk), in frames.
    int embeddedLoopStart = -1, embeddedLoopEnd = -1;
};

// A juce::AudioBuffer over a dsp::Buffer's channels (no copy): JUCE's readers decode into it, its writers write from
// it. Valid while the dsp::Buffer keeps its size.
inline juce::AudioBuffer<float> juceView (dsp::Buffer& b)
{
    return { b.getArrayOfWritePointers(), b.getNumChannels(), b.getNumSamples() };
}

inline juce::AudioBuffer<float> juceView (const dsp::Buffer& b)
{
    return { const_cast<float* const*> (b.getArrayOfReadPointers()), b.getNumChannels(), b.getNumSamples() };
}

// A tempo as the sample view shows it: "128", "63.998" (up to three decimals, trailing zeros dropped).
inline juce::String tempoText (double bpm)
{
    auto t = juce::String (bpm, 3);
    while (t.containsChar ('.') && (t.endsWithChar ('0') || t.endsWithChar ('.')))
        t = t.dropLastCharacters (1);
    return t;
}
}
