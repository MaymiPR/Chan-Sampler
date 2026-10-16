#pragma once

#include <juce_core/juce_core.h>

// The operating system's own file windows, as FL uses them.
namespace osfile
{
    // FL's "Get properties": the system's file properties window
    // (Explorer's Properties sheet on Windows, Finder's Get Info on macOS).
    // Returns false when the system has none.
    bool showProperties (const juce::File&);
}
