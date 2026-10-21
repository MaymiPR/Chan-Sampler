#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "FlParts.h"
#include "FlStyle.h"
#include "PluginProcessor.h"
#include "WaveformView.h"

// Channel settings window modelled on FL Studio's. Every control sits at the
// position and size of its counterpart in FL's own form definition
// (TPluginForm in FLEngine_x64.dll, decoded with REA; see docs/EVIDENCE.md),
// on a 563 x 446 canvas that is scaled to the window size.
class ChanSamplerEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener
{
public:
    explicit ChanSamplerEditor (ChanSamplerProcessor&);
    ~ChanSamplerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;

    // For Tests/SnapshotTool: shows a sheet (0 SMP, 1 INS, 2 FUNC) and INS target page.
    void showSheetForTesting (int sheet, int insPage);
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    static constexpr int canvasW = 563, canvasH = 446;

    // Builders: positions are FL form coordinates relative to the parent.
    juce::Slider& knob (juce::Component& parent, const juce::String& paramId, int x, int y, int size,
                        juce::Colour ring = fl::colour::amber);
    juce::Slider& deadKnob (juce::Component& parent, int x, int y, int size, const juce::String& what);
    juce::Slider& hslider (juce::Component& parent, const juce::String& paramId, int x, int y, int w, int h);
    juce::Label& caption (juce::Component& parent, const juce::String& text, int x, int y, int w, int h);
    juce::ToggleButton& led (juce::Component& parent, const juce::String& paramId, const juce::String& text, int x, int y, int w, int h);
    juce::ToggleButton& deadLed (juce::Component& parent, const juce::String& text, int x, int y, int w, int h, const juce::String& what);
    juce::ComboBox& combo (juce::Component& parent, const juce::String& paramId, int x, int y, int w, int h);
    fl::DigiBox& digi (juce::Component& parent, const juce::String& paramId, int x, int y, int w, int h, bool red);
    fl::DigiBox& deadDigi (juce::Component& parent, const juce::String& text, int x, int y, int w, int h, bool red, const juce::String& what);
    void showAboutMenu (juce::Component& target);
    juce::Label& text (juce::Component& parent, const juce::String& s, int x, int y, int w, int h, float size, bool bold = false,
                       juce::Justification just = juce::Justification::centredLeft);
    fl::Panel& panel (juce::Component& parent, const juce::String& title, int x, int y, int w, int h, int headerInset = 0);
    juce::Component& group (juce::Component& parent, int x, int y, int w, int h);

    template <typename T>
    T& own (std::unique_ptr<T> c, juce::Component& parent, juce::Rectangle<int> bounds)
    {
        auto& ref = *c;
        ref.setBounds (bounds);
        parent.addAndMakeVisible (ref);
        owned.push_back (std::move (c));
        return ref;
    }

    void buildTopBar();
    void buildSampleSheet();
    void buildInsSheet();
    void buildFuncSheet();
    juce::Component& buildTargetPage (const char* prefix, bool hasAmount, bool hasEnvelope = true);

    void showSheet (int index);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshSample();
    void loadFile (const juce::File& file);
    // The panel menus: FL's echo presets, and envelope presets (FL ships none;
    // its menu lists the ones saved with Save as).
    void showEchoPresets (juce::Component& anchor);
    void showEnvelopeMenu (const juce::String& prefix, juce::Component& anchor);
    void setParam (const juce::String& id, float value);
    void chooseFile();

    ChanSamplerProcessor& samplerProcessor;
    fl::LookAndFeel lnf;

    juce::Component canvas, smpSheet, insSheet, funcSheet;
    std::vector<std::unique_ptr<juce::Component>> owned;
    WaveformView waveform;
    juce::Label* fileName = nullptr;
    juce::Label* sampleInfo = nullptr;
    fl::IconTabs* sheetTabs = nullptr;
    fl::TextTabs* targetTabs = nullptr;
    bool rememberSize = false;   // set once the editor has its initial size

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> paramAttachments;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChanSamplerEditor)
};
