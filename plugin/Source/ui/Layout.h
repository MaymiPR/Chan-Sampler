#pragma once

// The editor's geometry in design pixels: the canvas, the top bar, the three sheets and their panels, each at the
// position and size of its counterpart in FL Studio's channel settings window (MEASUREMENTS.md). A panel's box is
// in its sheet; a page's box is in its panel. The controls inside a panel are placed in the editor, beside the
// control they place. Colours are in shared/ui/Theme.h.

#include <juce_gui_basics/juce_gui_basics.h>

namespace cs::layout
{
    struct Box
    {
        int x, y, w, h;
        operator juce::Rectangle<int>() const { return { x, y, w, h }; }
    };

    inline constexpr int canvasW = 563, canvasH = 446;   // scaled to the window as a whole
    inline constexpr Box topBar {   1,   0, 561,  47 };   // FL: the strip from the canvas's top to the sheet, with its rules
    inline constexpr Box sheet  {   1,  46, 561, 399 };   // each of the three sheets, below the bar

    // The Sampler sheet
    namespace sample
    {
        inline constexpr Box file         {   7,   7, 270,  35 };   // the file panel
        inline constexpr Box content      {   7,  50, 270, 110 };   // the Content panel
        inline constexpr Box playback     {   7, 168, 270,  79 };   // the Playback panel
        inline constexpr Box stretch      { 283,   7, 270,  76 };   // the Time stretching panel
        inline constexpr Box effects      { 283,  91, 270, 156 };   // the Precomputed effects panel
        inline constexpr Box effectsTools {   0,   0, 268, 156 };   // its Tools page, in the panel
        inline constexpr Box effectsPage  {   1,  22, 265, 130 };   // its Effects page, in the panel
        inline constexpr Box view         {   7, 255, 546, 134 };   // the sample view and its zoom bar
    }

    // The Envelopes sheet
    namespace ins
    {
        inline constexpr Box envelope   {   7,  42, 287, 195 };   // the Envelope panel
        inline constexpr Box lfo        { 300,  42, 146, 195 };   // the LFO panel
        inline constexpr Box filter     { 452,  42, 101, 195 };   // the Filter panel
        inline constexpr Box targetPage {   0,   0, 452, 240 };   // a target's page: the envelope and the LFO
        inline constexpr Box targets    {   8,   6, 260,  29 };   // the target tabs
        inline constexpr Box rootNote   {   7, 245, 546, 137 };   // the root note panel with the keyboard
    }

    // The Misc sheet
    namespace func
    {
        inline constexpr Box levels       {   7,   7, 178, 100 };   // the Levels adjustment panel
        inline constexpr Box polyphony    { 191,   7, 178, 100 };   // the Polyphony panel
        inline constexpr Box tracking     { 376,   7, 177, 100 };   // the Tracking panel
        inline constexpr Box group        {   7, 115,  69, 122 };   // the Group panel
        inline constexpr Box arpeggiator  {  82, 115, 231, 122 };   // the Arpeggiator panel
        inline constexpr Box echo         { 319, 115, 234, 122 };   // the Echo delay / fat mode panel
        inline constexpr Box trackingPage {   0,   0, 177, 100 };   // the Vol and the Key page, in the panel
    }
}
