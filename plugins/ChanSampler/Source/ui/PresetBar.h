#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ChanSamplerProcessor;

// The plugin's own preset menu (top bar): the current preset's name with
// previous / next arrows; a click opens Save preset as..., Import FL Sampler
// preset..., the factory presets (built in, settings only), the presets in
// Documents/Chan Sampler/Presets (folders as submenus), Initialize and Open
// presets folder. A saved preset is the plugin state as XML (every parameter
// and the sample path), the same as a host save; one without a sample path
// keeps the loaded sample.
class PresetBar : public juce::Component,
                  public juce::SettableTooltipClient,
                  private juce::Timer
{
public:
    explicit PresetBar (ChanSamplerProcessor&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static juce::File presetFolder();
    static constexpr const char* extension = ".cspreset";

    // Called after an import or load that the editor should report (message, ok).
    std::function<void (const juce::String&, bool)> onMessage;

private:
    void timerCallback() override;
    juce::Array<juce::File> presetFiles() const;
    juce::String currentName() const;
    void setCurrentName (const juce::String&);
    void showMenu();
    void step (int delta);
    void load (const juce::File&);
    void loadFactory (int index);
    void saveAs();
    void importFst();
    void initialize();
    juce::Rectangle<float> leftArrow() const  { return getLocalBounds().toFloat().removeFromLeft (16.0f); }
    juce::Rectangle<float> rightArrow() const { return getLocalBounds().toFloat().removeFromRight (16.0f); }

    ChanSamplerProcessor& processor;
    std::unique_ptr<juce::FileChooser> chooser;
    float flash[2] = { 0.0f, 0.0f };   // the arrows' click flash, fading
};
