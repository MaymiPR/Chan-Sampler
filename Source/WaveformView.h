#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

// FL-style sample view: the precomputed waveform with a draggable Start flag,
// Loop flags and region, a zoom/scroll bar, and mouse-wheel zoom.
// Clicking the waveform previews the sample at the root key.
class WaveformView : public juce::Component,
                     private juce::Timer
{
public:
    explicit WaveformView (ChanSamplerProcessor&);

    void setSample (SampleData::Ptr newSample);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // FL's view options (right-click menu), kept with the plugin state
    bool spectrumView() const;
    bool multichannelView() const;
    int colourMode() const;   // FL's Colorful waveforms: 0 off, 1 color map, 2 multiband (default)

    // Shows frac0..frac1 of the sample (tests / snapshots).
    void setView (float frac0, float frac1) { viewStart = frac0; viewEnd = frac1; repaint(); }
    void showPropertiesForTesting() { showProperties(); }

private:
    enum class Marker { none, start, loopStart, loopEnd, scrollThumb };
    // FL's sample view (measured from FL 2026): an 8 px strip, the view, then an
    // 18 px zoom / scroll bar with 14 px arrow buttons.
    static constexpr float topH = 8.0f, scrollH = 18.0f, buttonW = 14.0f;

    void timerCallback() override;
    float getValue (const char* id) const;
    void setValue (const char* id, float value);
    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> scrollArea() const;
    float fracToX (float frac) const;
    float xToFrac (float x) const;
    Marker markerAt (juce::Point<float>) const;
    const char* markerParam (Marker m) const;
    void drawLanes (juce::Graphics&, juce::Rectangle<float> area, float from, float to, juce::Colour colour, bool colourful) const;
    // Colorful waveforms: smoothed per-band energies (level[0] Multiband,
    // level[1] Color map; source 0 both channels, 1 left, 2 right), made when
    // first drawn.
    struct BandEnergy
    {
        SampleData::Ptr owner;
        std::vector<float> level[2][3];
        int group[2] { 8, 1 };   // 64-frame blocks per level block
    };
    mutable BandEnergy bands[3];
    const BandEnergy& bandEnergy (int source) const;
    std::vector<juce::Colour> columnColours (float from, float to, int width) const;
    void setColourFill (juce::Graphics&, juce::Rectangle<float> area, float from, float to, juce::Colour plain, bool colourful) const;
    juce::Colour waveColour() const;
    void showMenu();
    void saveAs();
    void showProperties();
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float> area);
    juce::Image spectrumImage;
    int laneChannel = -1;   // drawLanes: one channel only (multichannel view), -1 both
    juce::String spectrumKey;
    std::unique_ptr<juce::FileChooser> chooser;
    void drawSamples (juce::Graphics&, juce::Rectangle<float> area, float from, float to) const;

    ChanSamplerProcessor& processor;
    SampleData::Ptr sample;
    float viewStart = 0.0f, viewEnd = 1.0f;
    // Trackpad zoom: scroll gathered before zooming starts, and timing guards
    float smoothScroll = 0.0f;
    bool smoothZooming = false;
    double lastSmoothMs = 0.0, lastPressMs = -1.0e9;
    Marker dragging = Marker::none;
    float dragOffset = 0.0f;
    int previewNote = -1;
    float lastMarkers[4] {};
    float playhead = -1.0f;   // fraction of the sample, -1 when not playing
    float playDirection = 1.0f;   // +1 forwards, -1 backwards (the trail is drawn behind)
};
