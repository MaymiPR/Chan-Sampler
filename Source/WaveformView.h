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

private:
    enum class Marker { none, start, loopStart, loopEnd, scrollThumb };
    static constexpr float scrollH = 13.0f;

    void timerCallback() override;
    float getValue (const char* id) const;
    void setValue (const char* id, float value);
    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> scrollArea() const;
    float fracToX (float frac) const;
    float xToFrac (float x) const;
    Marker markerAt (juce::Point<float>) const;
    const char* markerParam (Marker m) const;
    void drawLanes (juce::Graphics&, juce::Rectangle<float> area, float from, float to, float alpha) const;
    void drawSamples (juce::Graphics&, juce::Rectangle<float> area, float from, float to) const;

    ChanSamplerProcessor& processor;
    SampleData::Ptr sample;
    float viewStart = 0.0f, viewEnd = 1.0f;
    Marker dragging = Marker::none;
    float dragOffset = 0.0f;
    int previewNote = -1;
    float lastMarkers[4] {};
    float playhead = -1.0f;   // fraction of the sample, -1 when not playing
};
