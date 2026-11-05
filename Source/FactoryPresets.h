#pragma once

#include <utility>
#include <vector>

// Presets built into the plugin. They hold settings only, no sample: loading
// one sets every parameter to its default, then applies these values (plain
// parameter values), and keeps the loaded sample.
struct FactoryPreset
{
    const char* folder;
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<FactoryPreset>& factoryPresets();

class ChanSamplerProcessor;
// Defaults, then the preset's values; the sample stays.
void applyFactoryPreset (ChanSamplerProcessor&, const FactoryPreset&);
