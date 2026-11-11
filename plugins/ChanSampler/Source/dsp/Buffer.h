#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace cs::dsp
{

// The DSP's audio: channels of float frames, owned or referring to another buffer's channels (a sample that plays the
// file's own audio). The JUCE side wraps one in a juce::AudioBuffer that refers to the same memory (SampleFile.h), so
// a file is decoded straight into it. New frames are always cleared.
class Buffer
{
public:
    Buffer() = default;
    Buffer (int channels, int frames) { setSize (channels, frames); }

    Buffer (const Buffer& other) { *this = other; }
    Buffer& operator= (const Buffer& other)
    {
        if (this != &other)
        {
            data.assign ((size_t) other.numChannels * (size_t) other.numFrames, 0.0f);
            numChannels = other.numChannels;
            numFrames = other.numFrames;
            pointChannels();
            for (int ch = 0; ch < numChannels; ++ch)
                std::copy (other.channels[(size_t) ch], other.channels[(size_t) ch] + numFrames, channels[(size_t) ch]);
        }
        return *this;
    }
    Buffer (Buffer&&) noexcept = default;              // the vectors move their storage: the channel pointers stay valid
    Buffer& operator= (Buffer&&) noexcept = default;

    int getNumChannels() const noexcept { return numChannels; }
    int getNumSamples() const noexcept { return numFrames; }
    const float* getReadPointer (int ch) const noexcept { return channels[(size_t) ch]; }
    float* getWritePointer (int ch) noexcept { return channels[(size_t) ch]; }
    float* const* getArrayOfWritePointers() noexcept { return channels.data(); }
    const float* const* getArrayOfReadPointers() const noexcept { return channels.data(); }
    float getSample (int ch, int i) const noexcept { return channels[(size_t) ch][i]; }
    void setSample (int ch, int i, float v) noexcept { channels[(size_t) ch][i] = v; }

    // The size in channels and frames; keepExisting keeps the frames both sizes share, the rest is cleared.
    void setSize (int newChannels, int newFrames, bool keepExisting = false)
    {
        std::vector<float> next ((size_t) newChannels * (size_t) newFrames, 0.0f);
        if (keepExisting)
            for (int ch = 0; ch < std::min (numChannels, newChannels); ++ch)
                std::copy (channels[(size_t) ch], channels[(size_t) ch] + std::min (numFrames, newFrames),
                           next.data() + (size_t) ch * (size_t) newFrames);
        data = std::move (next);
        numChannels = newChannels;
        numFrames = newFrames;
        pointChannels();
    }

    // Refers to another buffer's channels (read only from then on: the owner keeps them).
    void setDataToReferTo (float* const* refer, int newChannels, int newFrames)
    {
        data.clear();
        numChannels = newChannels;
        numFrames = newFrames;
        channels.assign (refer, refer + newChannels);
    }

    void clear() noexcept
    {
        for (auto* c : channels)
            std::fill (c, c + numFrames, 0.0f);
    }

    void applyGain (float gain) noexcept
    {
        for (int ch = 0; ch < numChannels; ++ch)
            applyGain (ch, 0, numFrames, gain);
    }

    void applyGain (int ch, int start, int num, float gain) noexcept
    {
        auto* d = channels[(size_t) ch] + start;
        for (int i = 0; i < num; ++i)
            d[i] *= gain;
    }

    void copyFrom (int destCh, int destStart, const Buffer& source, int sourceCh, int sourceStart, int num) noexcept
    {
        if (num > 0)
            std::copy (source.getReadPointer (sourceCh) + sourceStart, source.getReadPointer (sourceCh) + sourceStart + num,
                       channels[(size_t) destCh] + destStart);
    }

    // The largest absolute value in [start, start + num) of one channel, or of every channel.
    float getMagnitude (int ch, int start, int num) const noexcept
    {
        float m = 0.0f;
        for (int i = start; i < start + num; ++i)
            m = std::max (m, std::abs (channels[(size_t) ch][i]));
        return m;
    }

    float getMagnitude (int start, int num) const noexcept
    {
        float m = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            m = std::max (m, getMagnitude (ch, start, num));
        return m;
    }

    void reverse (int start, int num) noexcept
    {
        for (auto* c : channels)
            std::reverse (c + start, c + start + num);
    }

private:
    void pointChannels()
    {
        channels.resize ((size_t) numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
            channels[(size_t) ch] = data.data() + (size_t) ch * (size_t) numFrames;
    }

    std::vector<float> data;
    std::vector<float*> channels;
    int numChannels = 0, numFrames = 0;
};
}
