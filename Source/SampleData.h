#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// The "precomputed" sample: the loaded file with every Tools/Effects process
// baked in. The manual describes these as processes that replace the sample
// data rather than realtime effects, so they run once on the message thread and
// the audio thread only ever reads a finished SampleData.
struct SampleData : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> audio;   // always 2 channels: [0, playEnd) + bleed tail
    int playEnd = 0;                  // end of the playable region in frames
    bool lengthTrimmed = false;       // true when Length cut the sample short

    // FL's CROSSFADE without loop points turns the sample into a loop
    // [autoLoopStart, playEnd) with a crossfade baked into its end (measured).
    bool autoLoop = false;
    int autoLoopStart = 0;
    // With loop points and a crossfade, the loop restarts this far after START.
    int loopStartShift = 0;

    // Detected transients (frames), used by the slice stretch modes.
    std::vector<int> onsets;
    double sourceRate = 44100.0;

    // Min/max overview for the waveform view (per channel, per bucket).
    static constexpr int numPeakBuckets = 2048;
    std::vector<float> peakMin[2], peakMax[2];

    int getTotalFrames() const noexcept { return audio.getNumSamples(); }
    void computePeaks();
};

struct PrecomputeSettings
{
    bool removeDC = false, reversePolarity = false, normalize = false;
    bool fadeStereo = false, reverse = false, swapStereo = false;
    float start = 0.0f, length = 1.0f;
    float fadeIn = 0.0f, fadeOut = 0.0f, crossfade = 0.0f, trim = 0.0f;
    float boost = 0.0f, eq = 0.0f, rmMix = 0.0f, rmFreq = 0.5f;
    bool boostClip = false;
    float cut = 1.0f, res = 0.0f, stereoDelay = 0.0f, pogo = 0.0f;
    int reverbType = 0;   // 0 = A (close, tight), 1 = B (distant, large)
    float reverb = 0.0f;
    bool useLoop = false;
    float loopStart = 0.0f, loopEnd = 1.0f;
    // Playback rate. FL converts a file at another rate to this rate when
    // loading it (0 = keep the file's rate).
    double targetRate = 0.0;
};

// The raw file as loaded, before any precomputed process.
struct RawSample
{
    juce::AudioBuffer<float> audio;   // 1 or 2 channels
    double sampleRate = 44100.0;
    int bitsPerSample = 16;
    juce::File file;
    // Loop points embedded in the file (WAV "smpl" chunk), in frames.
    int embeddedLoopStart = -1, embeddedLoopEnd = -1;
};

SampleData::Ptr buildProcessedSample (const RawSample& raw, const PrecomputeSettings& settings);

// Sample Start / Length knob position (0..1) to a fraction of the sample.
double trimKnobToFraction (float knob);
