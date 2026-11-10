#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The layout follows FL Studio's channel settings window: every size and
// position is a measurement (FL's default theme at 100 %; see MEASUREMENTS.md
// "Skin"). The colours are Chan Sampler's own palette, and all drawing is our
// own, in code.
//
// The suite's colours and type: every colour of the editors is a token here, named for what it paints.
namespace chansampler::ui
{
    // The design tokens: Chan Sampler's palette, true black, neutral greys and
    // white, violet for the value arcs, the lit switches and the selected tabs.
    // Nothing else in the UI holds a colour.
    namespace colour
    {
        // Surfaces
        const juce::Colour sheet       { 0xff000000 };   // sheet background (gaps between panels)
        const juce::Colour panel       { 0xff0a0a0a };   // panel body
        const juce::Colour panelTab    { 0xff141414 };   // the header tab holding a panel's title
        const juce::Colour panelEdge   { 0xff262626 };   // panel outline (left, right, bottom) and the header strip right of the tab
        const juce::Colour well        { 0xff000000 };   // top bar, displays
        const juce::Colour barLine     { 0xff262626 };   // the rules above and below the top bar
        const juce::Colour tabSelected { 0xff161616 };   // the selected sheet tab's cell in the top bar

        // Knobs
        const juce::Colour knobOuter       { 0xff000000 };   // outermost 1 px ring
        const juce::Colour knobBevelTop    { 0xff2b2b2b };   // 2 px bevel ring, lit from above
        const juce::Colour knobBevelBottom { 0xff101010 };
        const juce::Colour knobGapTop      { 0xff0c0c0c };   // 1 px dark gap between bevel and body
        const juce::Colour knobGapBottom   { 0xff050505 };
        const juce::Colour knobBodyTop     { 0xff363636 };   // the body, darkening downwards
        const juce::Colour knobBodyBottom  { 0xff060606 };
        const juce::Colour knobBodyRim     { 0xff707070 };   // the light along its top rim
        const juce::Colour knobBodyFoot    { 0xff1c1c1c };   // and the fainter one at its foot
        const juce::Colour arc             { 0xffa78bfa };   // value arcs, the Mid slider's fill, the lit "Key" tab
        const juce::Colour arcAlt          { 0xfff0f0f0 };   // INS arcs, VOL, the echo's FEED
        // Each knob's arc (and its dot, at 73% of the arc colour) by what it controls
        const juce::Colour knobPan         { 0xff7dd3fc };   // PAN
        const juce::Colour knobModX        { 0xfff0f0f0 };   // MOD X
        const juce::Colour knobModY        { 0xffa78bfa };   // the tracking and echo MOD Y
        const juce::Colour knobPitch       { 0xfff472b6 };   // the echo's PITCH
        const juce::Colour bigRingLight    { 0xff4c4c4c };   // the filter knobs' wide ring: lit at its top, dark at its sides
        const juce::Colour bigRingDark     { 0xff050505 };
        const juce::Colour bigRingFoot     { 0xff131313 };
        const juce::Colour bigCollar       { 0xff0a0a0a };   // between the ring and the cap
        const juce::Colour bigCapTop       { 0xff555555 };   // the cap, lit from above
        const juce::Colour bigCapMid       { 0xff242424 };
        const juce::Colour bigCapBottom    { 0xff282828 };
        const juce::Colour bigCapLight     { 0x99ffffff };   // the soft light up to its right
        const juce::Colour bigShadow       { 0x80000000 };   // its shadow on the panel
        const juce::Colour bigTick         { 0xff333333 };   // the tick marks round it
        const juce::Colour bigDotX         { 0xffa78bfa };   // the filter knobs' dots: MOD X, MOD Y
        const juce::Colour bigDotY         { 0xfff0f0f0 };

        // Round switches (LEDs)
        const juce::Colour ledRing        { 0xff4a4a4a };
        const juce::Colour ledGap         { 0xff000000 };
        const juce::Colour ledInnerTop    { 0xff141414 };
        const juce::Colour ledInnerBottom { 0xff2e2e2e };
        const juce::Colour ledInnerRim    { 0xff474747 };   // the light along the disc's upper rim
        const juce::Colour ledOnCentre    { 0xff9b7bf7 };
        const juce::Colour ledOnEdge      { 0xffddd6fe };
        const juce::Colour ledOnBottom    { 0xfff3efff };
        const juce::Colour ledOnTop       { 0xffffffff };
        const juce::Colour ledOnGap       { 0xff3a1680 };
        const juce::Colour ledOnRing      { 0xff4a4a4a };
        const juce::Colour ledAltCentre   { 0xffe8e8e8 };   // the second colour (the channel ON light)
        const juce::Colour ledAltEdge     { 0xffffffff };
        const juce::Colour ledSmallTop    { 0xff363636 };   // the 11 px switch's disc (CLIP): lit top, dark foot
        const juce::Colour ledSmallMid    { 0xff1e1e1e };
        const juce::Colour ledSmallBottom { 0xff0a0a0a };
        const juce::Colour ledSmallOnBottom { 0xffddd6fe };   // lit: the foot of its disc

        // Text
        const juce::Colour title     { 0xffffffff };   // panel titles
        const juce::Colour text      { 0xffebebeb };   // labels, switch captions
        const juce::Colour caption   { 0xffcccccc };   // the small bold knob captions
        const juce::Colour dim       { 0xff8c8c8c };   // secondary text (sample info, hints)
        const juce::Colour fileName  { 0xffdadada };   // the loaded file's name
        const juce::Colour textLit   { 0xffa78bfa };   // the selected envelope target's name
        const juce::Colour tabIcon   { 0xffa78bfa };   // the selected icon in a row of icon tabs
        const juce::Colour accent    { 0xffa78bfa };   // Chan Sampler's own: the mark, the product name, the selected sheet tab
        const juce::Colour icon      { 0xffcccccc };   // unselected icons, the menu arrow
        const juce::Colour fxIconOn  { 0xffa78bfa };   // the effects view's chosen icon (wrench / valve)
        const juce::Colour fxIconOff { 0xff9c9c9c };   // its other icon
        const juce::Colour envLine   { 0xffebebeb };   // envelope / LFO curves
        const juce::Colour envPoint  { 0xffffffff };   // the envelope's draggable points (ring)
        const juce::Colour envPointFill { 0xff222222 };

        // Drop-down fields
        const juce::Colour comboTop       { 0xff141414 };   // fill, darkening downwards
        const juce::Colour comboBottom    { 0xff0b0b0b };
        const juce::Colour comboEdge      { 0xff2b2b2b };   // 1 px frame
        const juce::Colour comboInnerTop  { 0xff282828 };   // lighter inner edge along the top
        const juce::Colour comboInnerSide { 0xff202020 };
        const juce::Colour comboInnerBottom { 0xff191919 };
        const juce::Colour comboArrow     { 0xff8c8c8c };
        const juce::Colour comboText      { 0xffebebeb };

        // Push buttons (Reset, Cut self)
        const juce::Colour buttonFrame       { 0xff2b2b2b };
        const juce::Colour buttonCornerOuter { 0xff121212 };   // the rounded corners' two shades
        const juce::Colour buttonCornerInner { 0xff181818 };
        const juce::Colour buttonEdgeTop     { 0xff2c2c2c };   // light inner edge along the top
        const juce::Colour buttonEdgeBottom  { 0xff141414 };   // inner row at the bottom
        const juce::Colour buttonSideTop     { 0xff262626 };   // inner edges down the sides, fading
        const juce::Colour buttonSideBottom  { 0xff141414 };
        const juce::Colour buttonTop         { 0xff161616 };   // fill, darkening downwards
        const juce::Colour buttonBottom      { 0xff0b0b0b };
        const juce::Colour buttonText        { 0xffe4e4e4 };
        // The A / B reverb switch: an upright pill
        const juce::Colour pillEdge     { 0xff4a4a4a };
        const juce::Colour pillInside   { 0xff050505 };
        const juce::Colour pillKnob     { 0xffd4d4d4 };
        const juce::Colour pillKnobEdge { 0xffebebeb };

        // Digital value boxes
        const juce::Colour digiTop    { 0xff363636 };
        const juce::Colour digiBottom { 0xff212121 };
        const juce::Colour digiEdge   { 0xff121212 };   // bottom edge and corners
        const juce::Colour digiArrow  { 0xffa0a0a0 };
        const juce::Colour digiText   { 0xfff4f4f4 };
        const juce::Colour digiNone   { 0xff8c8c8c };   // the dashes of an empty box
        const juce::Colour digiRed    { 0xff5c2636 };   // the second variant (pitch range)

        // Sliders
        const juce::Colour sliderTrack   { 0xff0e0e0e };   // the groove's edges
        const juce::Colour sliderGroove  { 0xff1c1c1c };   // its floor
        const juce::Colour sliderTick    { 0xff333333 };   // the 1 px ticks below it
        const juce::Colour sliderTickTop { 0xff3e3e3e };   // their top pixel
        const juce::Colour sliderBand    { 0xff0a0a0a };   // the strip the ticks stand on
        const juce::Colour sliderBandTop { 0xff0c0c0c };
        const juce::Colour thumbFine     { 0xffa78bfa };   // the fine tune thumb's line
        const juce::Colour thumbTop      { 0xff3a3a3a };   // the thumb's ball, darkening downwards
        const juce::Colour thumbMid      { 0xff161616 };
        const juce::Colour thumbBottom   { 0xff060606 };
        const juce::Colour thumbRim      { 0xff0a0a0a };
        const juce::Colour thumbLight    { 0xff7c7c7c };   // the band inside its rim at the top
        const juce::Colour thumbFoot     { 0xff303030 };   // the softer light at its foot

        // Sample view
        const juce::Colour fieldEdge  { 0xff303030 };   // the preset bar's outline
        const juce::Colour startFlag  { 0xffebebeb };   // Start flag
        const juce::Colour playBar    { 0xffa78bfa };   // playback bar (overview)

        // Menus and dialogs
        const juce::Colour menuBg     { 0xff141414 };
        const juce::Colour menuText   { 0xffebebeb };
        const juce::Colour ledOn      { 0xfff472b6 };   // the file name when the sample is missing
    }

    // The sample view.
    namespace view
    {
        const juce::Colour frame      { 0xff000000 };   // strip above the view, around the bar
        const juce::Colour bg         { 0xff050505 };
        const juce::Colour grid       { 0xff171717 };   // 0.1 s (or 10x finer / coarser) and the +/-50% lines
        const juce::Colour gridMajor  { 0xff2a2a2a };   // the next power of ten (whole seconds)
        const juce::Colour gridMajorEdge { 0xff0c0c0c };   // 1 px each side of it
        const juce::Colour viewEdge   { 0xff171717 };   // the column at the view's right
        const juce::Colour waveMono   { 0xffffffff };
        const juce::Colour waveStereo { 0xfff2f2f2 };
        const juce::Colour watermark  { 0xff2c2c2c };
        const juce::Colour barDark    { 0xff000000 };
        const juce::Colour barLight   { 0xff262626 };
        const juce::Colour track      { 0xff0e0e0e };
        const juce::Colour overview   { 0xff9c9c9c };
        const juce::Colour arrow      { 0xff8c8c8c };
    }

    // The root note keyboard: white keys, the root in violet.
    namespace key
    {
        const juce::Colour strip       { 0xffbabec5 };   // label strip; also the flat C keys
        const juce::Colour rule        { 0xffa3a7ae };
        const juce::Colour whiteTop    { 0xffbabec5 };   // white keys shade lighter downwards
        const juce::Colour whiteBottom { 0xffcfd3da };
        const juce::Colour root        { 0xffa78bfa };
        const juce::Colour label       { 0xff272224 };
        const juce::Colour rootLabel   { 0xff1e1240 };
        const juce::Colour pressed     { 0xffc4b5fd };   // a played key
        const juce::Colour blackEdge   { 0xff2f3031 };
        const juce::Colour blackTop    { 0xff3a3b3c };   // the body shades lighter downwards
        const juce::Colour blackBottom { 0xff575859 };   // also the lip
        const juce::Colour blackFade   { 0xff4e4f50 };
        const juce::Colour blackLitTop { 0xff4c4d4e };   // the lit left column
        const juce::Colour blackLitBottom { 0xff6a6b6c };
        const juce::Colour blackShadow { 0xff979aa0 };   // the row under a black key
        const juce::Colour bar         { 0xff1a1a1a };
        const juce::Colour barArrow    { 0xff8c8c8c };
        const juce::Colour grip        { 0xff3a3a3a };
        const juce::Colour gripLine    { 0xff2a2a2a };
    }

    // FL's text: Segoe UI at the given em size in pixels (FL's labels are 11.7 px,
    // its knob captions 7 px bold).
    juce::Font font (float em, bool bold = false);
    // Text used to match the macOS font's width to Segoe UI's.
    inline constexpr const char* fontReferenceText = "Remove DC offset Normalize Reverse polarity Fade stereo 0123456789 START OFFSET";
}
