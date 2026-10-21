#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <tuple>

// The "precomputed" sample: the loaded file with every Tools/Effects process
// baked in. The manual describes these as processes that replace the sample
// data rather than realtime effects, so they run once on the message thread and
// the audio thread only ever reads a finished SampleData.
struct RawSample;

struct SampleData : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> audio;   // always 2 channels: [0, playEnd) + bleed tail
    std::shared_ptr<const RawSample> sharedRaw;   // set when audio refers to the file's own data (read only)
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
    std::vector<int> fileMarkers;     // the file's loaded region / slice markers, as FL shows them (frames)
    double sourceRate = 44100.0;
    bool mono = false;                // a mono file (FL draws it in a cooler white)
    int fileBits = 32;                // the file's bit depth (FL's badge: 16, else 32 = float)

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
    bool loadRegions = true, loadSlices = false;
    // Playback rate. FL converts a file at another rate to this rate when
    // loading it (0 = keep the file's rate).
    double targetRate = 0.0;

    auto key() const
    {
        return std::tie (removeDC, reversePolarity, normalize, fadeStereo, reverse, swapStereo, start, length, fadeIn, fadeOut,
                         crossfade, trim, boost, eq, rmMix, rmFreq, boostClip, cut, res, stereoDelay, pogo, reverbType, reverb,
                         useLoop, loopStart, loopEnd, loadRegions, loadSlices, targetRate);
    }
    bool operator== (const PrecomputeSettings& o) const { return key() == o.key(); }
};

// The raw file as loaded, before any precomputed process.
struct RawSample
{
    juce::AudioBuffer<float> audio;   // 1 or 2 channels
    double sampleRate = 44100.0;
    int bitsPerSample = 16;
    int heldBits = 32;                // what FL holds it at: 16 for 16-bit PCM, else 32 (float)
    juce::File file;
    juce::int64 fileTime = 0, fileSize = 0;   // the file's modification time (ms) and size when loaded
    double fileTempo = 0;                     // a WAV's ACID tempo (BPM), 0 when it has none
    juce::String format;              // e.g. "WAV, 16-bit PCM", "WAV, compressed (Ogg Vorbis)", "FLAC"
    // Loop points embedded in the file (WAV "smpl" chunk), in frames.
    int embeddedLoopStart = -1, embeddedLoopEnd = -1;
    // Markers in the file, in frames: cue points (FL's regions) and ACID
    // slice markers ("strc" chunk).
    std::vector<int> regionMarkers, sliceMarkers;
};

// With owner (the shared RawSample raw belongs to), a sample that needs no
// processing at all refers to the file's audio instead of copying it.
SampleData::Ptr buildProcessedSample (const RawSample& raw, const PrecomputeSettings& settings,
                                      std::shared_ptr<const RawSample> owner = {});

// Sample Start / Length knob position (0..1) to a fraction of the sample.
double trimKnobToFraction (float knob);
