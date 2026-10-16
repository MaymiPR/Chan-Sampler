#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ChanSamplerProcessor;

// Imports an FL Studio Sampler channel preset (.fst): the sample and every
// setting Chan Sampler has (field meanings and scales measured against FL's
// Sampler, see docs/EVIDENCE.md and nulltest/). The preset's own channel
// volume and pan are folded into Chan Sampler's Levels.
namespace flpreset
{
    struct Result
    {
        bool ok = false;
        juce::String message;   // why it failed, or what could not be restored
    };

    Result import (ChanSamplerProcessor& processor, const juce::File& fst);

    bool isPresetFile (const juce::File& f);
}
