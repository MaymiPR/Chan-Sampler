#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "History.h"
#include "dsp/Chords.h"
#include "dsp/KnobLaws.h"
#include "dsp/Modes.h"
#include "dsp/Tuning.h"

// Host parameters as FL Studio's Sampler shows them (MEASUREMENTS.md): ids, names, ranges, defaults, display text and
// typed-value parsers. The ids are ours and never change once released (saved projects find parameters by them).
namespace cs
{

// Parameter IDs. Grouping follows FL Studio's channel settings window
// (top bar, SMP / INS / FUNC sheets); see MEASUREMENTS.md.
namespace id
{
    // MIDI pitch bend range (semitones)
    inline constexpr const char* pitchRange = "pitchRange";

    // SMP > Content / Playback
    inline constexpr const char* declick     = "declick";
    inline constexpr const char* loadRegions = "loadRegions";   // slice at the file's cue markers
    inline constexpr const char* loadSlices  = "loadSlices";    // slice at its ACID markers
    inline constexpr const char* startOffset = "startOffset";
    inline constexpr const char* useLoop     = "useLoop";
    inline constexpr const char* loopStart   = "loopStart";
    inline constexpr const char* loopEnd     = "loopEnd";
    inline constexpr const char* pingPong    = "pingPong";

    // SMP > Time stretching
    inline constexpr const char* pitch = "pitch";
    inline constexpr const char* stretchMode = "stretchMode";
    inline constexpr const char* stretchMul  = "stretchMul";
    inline constexpr const char* stretchTime = "stretchTime";
    inline constexpr const char* formant     = "formant";   // Stretch Pro formant shift (cents)

    // Output level (matches a fresh FL Sampler channel by default)
    inline constexpr const char* output = "output";

    // SMP > Precomputed effects > Tools
    inline constexpr const char* pcRemoveDC        = "pcRemoveDC";
    inline constexpr const char* pcReversePolarity = "pcReversePolarity";
    inline constexpr const char* pcNormalize       = "pcNormalize";
    inline constexpr const char* pcFadeStereo      = "pcFadeStereo";
    inline constexpr const char* pcReverse         = "pcReverse";
    inline constexpr const char* pcSwapStereo      = "pcSwapStereo";
    inline constexpr const char* pcStart           = "pcStart";
    inline constexpr const char* pcLength          = "pcLength";
    inline constexpr const char* pcFadeIn          = "pcFadeIn";
    inline constexpr const char* pcFadeOut         = "pcFadeOut";
    inline constexpr const char* pcCrossfade       = "pcCrossfade";
    inline constexpr const char* pcTrim            = "pcTrim";

    // SMP > Precomputed effects > Effects
    inline constexpr const char* pcBoost       = "pcBoost";
    inline constexpr const char* pcBoostClip   = "pcBoostClip";
    inline constexpr const char* pcEq          = "pcEq";
    inline constexpr const char* pcRmMix       = "pcRmMix";
    inline constexpr const char* pcRmFreq      = "pcRmFreq";
    inline constexpr const char* pcCut         = "pcCut";
    inline constexpr const char* pcRes         = "pcRes";
    inline constexpr const char* pcReverbType  = "pcReverbType";
    inline constexpr const char* pcReverb      = "pcReverb";
    inline constexpr const char* pcStereoDelay = "pcStereoDelay";
    inline constexpr const char* pcPogo        = "pcPogo";

    // INS > Filter
    inline constexpr const char* filterType = "filterType";
    inline constexpr const char* cutoff     = "cutoff";      // MOD X
    inline constexpr const char* resonance  = "resonance";   // MOD Y

    // FUNC > Levels adjustment
    inline constexpr const char* pan    = "pan";
    inline constexpr const char* volume = "volume";
    inline constexpr const char* xOfs   = "xOfs";
    inline constexpr const char* yOfs   = "yOfs";

    // FUNC > Polyphony / Group
    inline constexpr const char* maxPoly   = "maxPoly";
    inline constexpr const char* mono      = "mono";
    inline constexpr const char* porta     = "porta";
    inline constexpr const char* slide     = "slide";
    inline constexpr const char* cutItself = "cutItself";
    inline constexpr const char* cutGroup  = "cutGroup";    // FUNC > Group: "Cut" (this channel cuts group n)
    inline constexpr const char* cutBy     = "cutBy";       // FUNC > Group: "By" (cut by group n)

    // FUNC > Tracking (velocity and keyboard)
    inline constexpr const char* velPan = "velPan";
    inline constexpr const char* velX   = "velX";
    inline constexpr const char* velY   = "velY";
    inline constexpr const char* velMid = "velMid";
    inline constexpr const char* keyPan = "keyPan";
    inline constexpr const char* keyX   = "keyX";
    inline constexpr const char* keyY   = "keyY";
    inline constexpr const char* keyMid = "keyMid";

    // Root note
    inline constexpr const char* rootNote = "rootNote";
    inline constexpr const char* fineTune = "fineTune";

    // The host's undo step for group edits (History)
    inline constexpr const char* historyStep = "historyStep";

    // FUNC > Arpeggiator
    inline constexpr const char* arpDir    = "arpDir";
    inline constexpr const char* arpRange  = "arpRange";
    inline constexpr const char* arpRepeat = "arpRepeat";
    inline constexpr const char* arpTime   = "arpTime";
    inline constexpr const char* arpGate   = "arpGate";
    inline constexpr const char* arpSlide  = "arpSlide";
    inline constexpr const char* arpChord  = "arpChord";

    // FUNC > Echo delay / fat mode
    inline constexpr const char* echoFeed   = "echoFeed";
    inline constexpr const char* echoPan    = "echoPan";
    inline constexpr const char* echoX      = "echoX";
    inline constexpr const char* echoY      = "echoY";
    inline constexpr const char* echoPitch  = "echoPitch";
    inline constexpr const char* echoTime   = "echoTime";
    inline constexpr const char* echoCount  = "echoCount";
    inline constexpr const char* echoPing   = "echoPing";
    inline constexpr const char* echoFat    = "echoFat";

    // Envelope / LFO targets (INS tabs: Panning, Volume, Mod X, Mod Y, Pitch).
    // Each has <prefix>EnvOn, Del, Att, Hold, Dec, Sus, Rel, AttT, DecT, RelT,
    // EnvAmt (not volume), LfoShape, LfoSpeed, LfoAmt, LfoDel, LfoAtt.
    inline constexpr const char* panPrefix   = "pan";
    inline constexpr const char* volPrefix   = "vol";
    inline constexpr const char* modXPrefix  = "x";
    inline constexpr const char* modYPrefix  = "y";
    inline constexpr const char* pitchPrefix = "p";

    // The INS targets' envelope and LFO, by prefix (each spelled out; env () builds them from the prefix)
    // Panning
    inline constexpr const char* panLfoTempo = "panLfoTempo";
    inline constexpr const char* panLfoGlobal = "panLfoGlobal";
    inline constexpr const char* panLfoShape = "panLfoShape";
    inline constexpr const char* panLfoSpeed = "panLfoSpeed";
    inline constexpr const char* panLfoAmt = "panLfoAmt";
    inline constexpr const char* panLfoDel = "panLfoDel";
    inline constexpr const char* panLfoAtt = "panLfoAtt";
    // Volume
    inline constexpr const char* volEnvOn = "volEnvOn";
    inline constexpr const char* volDel = "volDel";
    inline constexpr const char* volAtt = "volAtt";
    inline constexpr const char* volHold = "volHold";
    inline constexpr const char* volDec = "volDec";
    inline constexpr const char* volSus = "volSus";
    inline constexpr const char* volRel = "volRel";
    inline constexpr const char* volAttT = "volAttT";
    inline constexpr const char* volDecT = "volDecT";
    inline constexpr const char* volRelT = "volRelT";
    inline constexpr const char* volEnvTempo = "volEnvTempo";
    inline constexpr const char* volLfoTempo = "volLfoTempo";
    inline constexpr const char* volLfoGlobal = "volLfoGlobal";
    inline constexpr const char* volLfoShape = "volLfoShape";
    inline constexpr const char* volLfoSpeed = "volLfoSpeed";
    inline constexpr const char* volLfoAmt = "volLfoAmt";
    inline constexpr const char* volLfoDel = "volLfoDel";
    inline constexpr const char* volLfoAtt = "volLfoAtt";
    // Mod X
    inline constexpr const char* xEnvOn = "xEnvOn";
    inline constexpr const char* xDel = "xDel";
    inline constexpr const char* xAtt = "xAtt";
    inline constexpr const char* xHold = "xHold";
    inline constexpr const char* xDec = "xDec";
    inline constexpr const char* xSus = "xSus";
    inline constexpr const char* xRel = "xRel";
    inline constexpr const char* xAttT = "xAttT";
    inline constexpr const char* xDecT = "xDecT";
    inline constexpr const char* xRelT = "xRelT";
    inline constexpr const char* xEnvAmt = "xEnvAmt";
    inline constexpr const char* xEnvTempo = "xEnvTempo";
    inline constexpr const char* xLfoTempo = "xLfoTempo";
    inline constexpr const char* xLfoGlobal = "xLfoGlobal";
    inline constexpr const char* xLfoShape = "xLfoShape";
    inline constexpr const char* xLfoSpeed = "xLfoSpeed";
    inline constexpr const char* xLfoAmt = "xLfoAmt";
    inline constexpr const char* xLfoDel = "xLfoDel";
    inline constexpr const char* xLfoAtt = "xLfoAtt";
    // Mod Y
    inline constexpr const char* yEnvOn = "yEnvOn";
    inline constexpr const char* yDel = "yDel";
    inline constexpr const char* yAtt = "yAtt";
    inline constexpr const char* yHold = "yHold";
    inline constexpr const char* yDec = "yDec";
    inline constexpr const char* ySus = "ySus";
    inline constexpr const char* yRel = "yRel";
    inline constexpr const char* yAttT = "yAttT";
    inline constexpr const char* yDecT = "yDecT";
    inline constexpr const char* yRelT = "yRelT";
    inline constexpr const char* yEnvAmt = "yEnvAmt";
    inline constexpr const char* yEnvTempo = "yEnvTempo";
    inline constexpr const char* yLfoTempo = "yLfoTempo";
    inline constexpr const char* yLfoGlobal = "yLfoGlobal";
    inline constexpr const char* yLfoShape = "yLfoShape";
    inline constexpr const char* yLfoSpeed = "yLfoSpeed";
    inline constexpr const char* yLfoAmt = "yLfoAmt";
    inline constexpr const char* yLfoDel = "yLfoDel";
    inline constexpr const char* yLfoAtt = "yLfoAtt";
    // Pitch
    inline constexpr const char* pEnvOn = "pEnvOn";
    inline constexpr const char* pDel = "pDel";
    inline constexpr const char* pAtt = "pAtt";
    inline constexpr const char* pHold = "pHold";
    inline constexpr const char* pDec = "pDec";
    inline constexpr const char* pSus = "pSus";
    inline constexpr const char* pRel = "pRel";
    inline constexpr const char* pAttT = "pAttT";
    inline constexpr const char* pDecT = "pDecT";
    inline constexpr const char* pRelT = "pRelT";
    inline constexpr const char* pEnvAmt = "pEnvAmt";
    inline constexpr const char* pEnvTempo = "pEnvTempo";
    inline constexpr const char* pLfoTempo = "pLfoTempo";
    inline constexpr const char* pLfoGlobal = "pLfoGlobal";
    inline constexpr const char* pLfoShape = "pLfoShape";
    inline constexpr const char* pLfoSpeed = "pLfoSpeed";
    inline constexpr const char* pLfoAmt = "pLfoAmt";
    inline constexpr const char* pLfoDel = "pLfoDel";
    inline constexpr const char* pLfoAtt = "pLfoAtt";

    inline juce::String env (const char* prefix, const char* suffix) { return juce::String (prefix) + suffix; }
}

// Every parameter in the order the layout adds them (the order hosts list them in).
inline constexpr int numParams = 161;
inline constexpr const char* const allIds[numParams] {
    id::pitchRange,
    id::declick,
    id::loadRegions,
    id::loadSlices,
    id::startOffset,
    id::useLoop,
    id::loopStart,
    id::loopEnd,
    id::pingPong,
    id::output,
    id::stretchMode,
    id::stretchMul,
    id::stretchTime,
    id::formant,
    id::pitch,
    id::pcRemoveDC,
    id::pcReversePolarity,
    id::pcNormalize,
    id::pcFadeStereo,
    id::pcReverse,
    id::pcSwapStereo,
    id::pcStart,
    id::pcLength,
    id::pcFadeIn,
    id::pcFadeOut,
    id::pcCrossfade,
    id::pcTrim,
    id::pcBoost,
    id::pcBoostClip,
    id::pcEq,
    id::pcRmMix,
    id::pcRmFreq,
    id::pcCut,
    id::pcRes,
    id::pcReverbType,
    id::pcReverb,
    id::pcStereoDelay,
    id::pcPogo,
    id::panLfoTempo,
    id::panLfoGlobal,
    id::panLfoShape,
    id::panLfoSpeed,
    id::panLfoAmt,
    id::panLfoDel,
    id::panLfoAtt,
    id::volEnvOn,
    id::volDel,
    id::volAtt,
    id::volHold,
    id::volDec,
    id::volSus,
    id::volRel,
    id::volAttT,
    id::volDecT,
    id::volRelT,
    id::volEnvTempo,
    id::volLfoTempo,
    id::volLfoGlobal,
    id::volLfoShape,
    id::volLfoSpeed,
    id::volLfoAmt,
    id::volLfoDel,
    id::volLfoAtt,
    id::xEnvOn,
    id::xDel,
    id::xAtt,
    id::xHold,
    id::xDec,
    id::xSus,
    id::xRel,
    id::xAttT,
    id::xDecT,
    id::xRelT,
    id::xEnvAmt,
    id::xEnvTempo,
    id::xLfoTempo,
    id::xLfoGlobal,
    id::xLfoShape,
    id::xLfoSpeed,
    id::xLfoAmt,
    id::xLfoDel,
    id::xLfoAtt,
    id::yEnvOn,
    id::yDel,
    id::yAtt,
    id::yHold,
    id::yDec,
    id::ySus,
    id::yRel,
    id::yAttT,
    id::yDecT,
    id::yRelT,
    id::yEnvAmt,
    id::yEnvTempo,
    id::yLfoTempo,
    id::yLfoGlobal,
    id::yLfoShape,
    id::yLfoSpeed,
    id::yLfoAmt,
    id::yLfoDel,
    id::yLfoAtt,
    id::pEnvOn,
    id::pDel,
    id::pAtt,
    id::pHold,
    id::pDec,
    id::pSus,
    id::pRel,
    id::pAttT,
    id::pDecT,
    id::pRelT,
    id::pEnvAmt,
    id::pEnvTempo,
    id::pLfoTempo,
    id::pLfoGlobal,
    id::pLfoShape,
    id::pLfoSpeed,
    id::pLfoAmt,
    id::pLfoDel,
    id::pLfoAtt,
    id::filterType,
    id::cutoff,
    id::resonance,
    id::pan,
    id::volume,
    id::xOfs,
    id::yOfs,
    id::maxPoly,
    id::mono,
    id::porta,
    id::slide,
    id::cutItself,
    id::cutGroup,
    id::cutBy,
    id::velPan,
    id::velX,
    id::velY,
    id::velMid,
    id::keyPan,
    id::keyX,
    id::keyY,
    id::keyMid,
    id::arpDir,
    id::arpRange,
    id::arpRepeat,
    id::arpTime,
    id::arpGate,
    id::arpSlide,
    id::arpChord,
    id::echoFeed,
    id::echoPan,
    id::echoX,
    id::echoY,
    id::echoPitch,
    id::echoTime,
    id::echoCount,
    id::echoPing,
    id::echoFat,
    id::rootNote,
    id::fineTune,
    id::historyStep,
};
// What a preset stores: every parameter but the host's undo step (historyStep).
inline constexpr const char* const presetIds[numParams - 1] {
    id::pitchRange,
    id::declick,
    id::loadRegions,
    id::loadSlices,
    id::startOffset,
    id::useLoop,
    id::loopStart,
    id::loopEnd,
    id::pingPong,
    id::output,
    id::stretchMode,
    id::stretchMul,
    id::stretchTime,
    id::formant,
    id::pitch,
    id::pcRemoveDC,
    id::pcReversePolarity,
    id::pcNormalize,
    id::pcFadeStereo,
    id::pcReverse,
    id::pcSwapStereo,
    id::pcStart,
    id::pcLength,
    id::pcFadeIn,
    id::pcFadeOut,
    id::pcCrossfade,
    id::pcTrim,
    id::pcBoost,
    id::pcBoostClip,
    id::pcEq,
    id::pcRmMix,
    id::pcRmFreq,
    id::pcCut,
    id::pcRes,
    id::pcReverbType,
    id::pcReverb,
    id::pcStereoDelay,
    id::pcPogo,
    id::panLfoTempo,
    id::panLfoGlobal,
    id::panLfoShape,
    id::panLfoSpeed,
    id::panLfoAmt,
    id::panLfoDel,
    id::panLfoAtt,
    id::volEnvOn,
    id::volDel,
    id::volAtt,
    id::volHold,
    id::volDec,
    id::volSus,
    id::volRel,
    id::volAttT,
    id::volDecT,
    id::volRelT,
    id::volEnvTempo,
    id::volLfoTempo,
    id::volLfoGlobal,
    id::volLfoShape,
    id::volLfoSpeed,
    id::volLfoAmt,
    id::volLfoDel,
    id::volLfoAtt,
    id::xEnvOn,
    id::xDel,
    id::xAtt,
    id::xHold,
    id::xDec,
    id::xSus,
    id::xRel,
    id::xAttT,
    id::xDecT,
    id::xRelT,
    id::xEnvAmt,
    id::xEnvTempo,
    id::xLfoTempo,
    id::xLfoGlobal,
    id::xLfoShape,
    id::xLfoSpeed,
    id::xLfoAmt,
    id::xLfoDel,
    id::xLfoAtt,
    id::yEnvOn,
    id::yDel,
    id::yAtt,
    id::yHold,
    id::yDec,
    id::ySus,
    id::yRel,
    id::yAttT,
    id::yDecT,
    id::yRelT,
    id::yEnvAmt,
    id::yEnvTempo,
    id::yLfoTempo,
    id::yLfoGlobal,
    id::yLfoShape,
    id::yLfoSpeed,
    id::yLfoAmt,
    id::yLfoDel,
    id::yLfoAtt,
    id::pEnvOn,
    id::pDel,
    id::pAtt,
    id::pHold,
    id::pDec,
    id::pSus,
    id::pRel,
    id::pAttT,
    id::pDecT,
    id::pRelT,
    id::pEnvAmt,
    id::pEnvTempo,
    id::pLfoTempo,
    id::pLfoGlobal,
    id::pLfoShape,
    id::pLfoSpeed,
    id::pLfoAmt,
    id::pLfoDel,
    id::pLfoAtt,
    id::filterType,
    id::cutoff,
    id::resonance,
    id::pan,
    id::volume,
    id::xOfs,
    id::yOfs,
    id::maxPoly,
    id::mono,
    id::porta,
    id::slide,
    id::cutItself,
    id::cutGroup,
    id::cutBy,
    id::velPan,
    id::velX,
    id::velY,
    id::velMid,
    id::keyPan,
    id::keyX,
    id::keyY,
    id::keyMid,
    id::arpDir,
    id::arpRange,
    id::arpRepeat,
    id::arpTime,
    id::arpGate,
    id::arpSlide,
    id::arpChord,
    id::echoFeed,
    id::echoPan,
    id::echoX,
    id::echoY,
    id::echoPitch,
    id::echoTime,
    id::echoCount,
    id::echoPing,
    id::echoFat,
    id::rootNote,
    id::fineTune,
};

// Host tempo for the knob menus' "Set > n steps" entries (updated by the processor).
inline std::atomic<double> displayBpm { 130.0 };

// Sets a parameter (normalised value) from the plugin's own code; inside a
// group edit in a host, as the plugin's change and not an edit (History).
inline void setParameterValue (juce::RangedAudioParameter& p, float normalised)
{
    if (History::isQuiet())
        History::runQuietly ([&] { p.setValueNotifyingHost (normalised); });
    else
        p.setValueNotifyingHost (normalised);
}

// Sets a parameter (plain value) as one gesture, as a user's edit does
// (inside a group edit in a host: as part of that edit).
inline void setParameterAsGesture (juce::RangedAudioParameter& p, float plainValue)
{
    if (History::isQuiet())
    {
        setParameterValue (p, p.convertTo0to1 (plainValue));
        return;
    }
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
    p.endChangeGesture();
}

// The OUT position for a level saved before OUT was FL's knob: a trim in dB
// (0 dB = unity, -48 to +12), capped at OUT's top.
inline float outputPositionForOldDb (float db)
{
    if (db <= -47.9f)
        return 0.0f;   // the old trim's bottom was silence
    const auto knobGain = juce::Decibels::decibelsToGain ((double) db) / (0.5 / dsp::flVolumeGain (dsp::defaultOutputPosition));
    return (float) juce::jlimit (0.0, 1.0, std::log (10.0 * knobGain + 1.0) / std::log (11.0));
}

// Every knob's range, display text and typed-value parser follow FL Studio's
// Sampler: a knob's 0..1 position (what FL's "Copy value" puts on the
// clipboard) maps to the same value here, and the text matches FL's hint bar.
// Positions and texts were read from FL itself (nulltest/, MEASUREMENTS.md).
namespace layout
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatAttr = juce::AudioParameterFloatAttributes;
    using ToText = std::function<juce::String (float, int)>;
    using FromText = std::function<float (const juce::String&)>;

    inline juce::ParameterID paramId (const juce::String& s) { return { s, 1 }; }

    inline double firstNumber (const juce::String& t)
    {
        auto s = t.trim();
        const auto start = s.indexOfAnyOf ("-+0123456789.");
        return start < 0 ? 0.0 : s.substring (start).getDoubleValue();
    }

    // "S:TT" (steps:ticks, 24 ticks per step) or "B:SS:TT" (bars:steps:ticks).
    inline double parseStepsTicks (const juce::String& t)
    {
        auto parts = juce::StringArray::fromTokens (t.trim(), ":", {});
        if (parts.size() == 3) return parts[0].getDoubleValue() * 16.0 + parts[1].getDoubleValue() + parts[2].getDoubleValue() / 24.0;
        if (parts.size() == 2) return parts[0].getDoubleValue() + parts[1].getDoubleValue() / 24.0;
        return firstNumber (t);
    }

    inline juce::String stepsTicks (double steps)
    {
        const auto totalTicks = (int) std::round (steps * 24.0);
        return juce::String (totalTicks / 24) + ":" + juce::String (totalTicks % 24).paddedLeft ('0', 2);
    }

    inline juce::String barsStepsTicks (double beats)
    {
        const auto totalTicks = (int) std::round (beats * 4.0 * 24.0);
        const int bars = totalTicks / (16 * 24);
        const int steps = (totalTicks / 24) % 16;
        return juce::String (bars) + ":" + juce::String (steps).paddedLeft ('0', 2) + ":" + juce::String (totalTicks % 24).paddedLeft ('0', 2);
    }

    // Display / parse pairs
    inline ToText pct()         { return [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }; }
    inline ToText pctSpace()    { return [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }; }
    inline FromText fromPct()   { return [] (const juce::String& t) { return (float) (firstNumber (t) / 100.0); }; }

    inline ToText leftRight()
    {
        return [] (float v, int)
        {
            const auto p = juce::roundToInt (v * 100.0f);
            return p == 0 ? juce::String ("Centered") : juce::String (std::abs (p)) + (p < 0 ? "% left" : "% right");
        };
    }
    inline FromText fromLeftRight()
    {
        return [] (const juce::String& t)
        {
            const auto v = std::abs (firstNumber (t)) / 100.0;
            return (float) (t.containsIgnoreCase ("left") || t.trim().startsWith ("-") ? -v : v);
        };
    }

    inline ToText cents()       { return [] (float v, int) { const auto c = juce::roundToInt (v); return (c > 0 ? "+" : "") + juce::String (c) + " cents"; }; }
    inline FromText fromNum()   { return [] (const juce::String& t) { return (float) firstNumber (t); }; }

    inline FloatAttr attr (ToText to, FromText from)
    {
        return FloatAttr().withStringFromValueFunction (std::move (to)).withValueFromStringFunction (std::move (from));
    }

    inline FloatAttr percentAttr()  { return attr (pct(), fromPct()); }

    // Envelope / LFO times: FL's position is (raw - 100) / 65436, but its
    // percentage is raw / 65536 (raw 20000 shows 31%, 16193 25%, 53784 82%),
    // read as a position as on the other knobs (50 is 0.5).
    inline FloatAttr envTimeAttr()
    {
        return attr ([] (float v, int)
                     {
                         const auto raw = juce::roundToInt (100.0 + (double) v * 65436.0);
                         return juce::String (juce::roundToInt (raw * 100.0 / 65536.0)) + "%";
                     },
                     fromPct());
    }

    inline void addFloat (Layout& l, const juce::String& pidStr, const juce::String& name,
                   juce::NormalisableRange<float> range, float def, FloatAttr a)
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (paramId (pidStr), name, range, def, a));
    }

    inline void addBool (Layout& l, const juce::String& pidStr, const juce::String& name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (paramId (pidStr), name, def));
    }

    inline void addChoice (Layout& l, const juce::String& pidStr, const juce::String& name,
                    const juce::StringArray& items, int def)
    {
        l.add (std::make_unique<juce::AudioParameterChoice> (paramId (pidStr), name, items, def));
    }

    // FL knobs hold whole raw values; these ranges snap to the same steps so a
    // typed or pasted value lands where FL's does (1 / steps per raw unit).
    inline juce::NormalisableRange<float> step (int steps, float start = 0.0f)
    {
        return { start, 1.0f, 1.0f / (float) steps };
    }

    // DAHDSR envelope + LFO for one INS target, with FL's defaults.
    // FL's Panning page has no envelope, only the LFO.
    inline void addTarget (Layout& l, const char* prefix, const juce::String& target, bool hasAmount, bool hasEnvelope = true)
    {
        auto p = [prefix] (const char* s) { return id::env (prefix, s); };
        const bool isVolume = juce::String (prefix) == id::volPrefix;
        const juce::NormalisableRange<float> unit { 0.0f, 1.0f }, bipolar { -1.0f, 1.0f };
        if (hasEnvelope)
        {
            addBool  (l, p ("EnvOn"),  target + " envelope", false);
            addFloat (l, p ("Del"),    target + " predelay time", unit, 0.0f,           envTimeAttr());
            addFloat (l, p ("Att"),    target + " attack time",   unit, dsp::flEnvKnobForRaw (20000), envTimeAttr());
            addFloat (l, p ("Hold"),   target + " hold time",     unit, dsp::flEnvKnobForRaw (20000), envTimeAttr());
            addFloat (l, p ("Dec"),    target + " decay time",    unit, dsp::flEnvKnobForRaw (30000), envTimeAttr());
            addFloat (l, p ("Sus"),    target + " sustain level", unit, 50.0f / 128.0f, percentAttr());
            addFloat (l, p ("Rel"),    target + " release time",  unit, dsp::flEnvKnobForRaw (20000), envTimeAttr());
            addFloat (l, p ("AttT"),   target + " attack tension",  bipolar, 0.0f, percentAttr());
            addFloat (l, p ("DecT"),   target + " decay tension",   bipolar, 0.0f, percentAttr());
            addFloat (l, p ("RelT"),   target + " release tension", bipolar, isVolume ? -101.0f / 128.0f : 0.0f, percentAttr());
            if (hasAmount)
                addFloat (l, p ("EnvAmt"), target + " envelope amount", bipolar, 0.0f, percentAttr());
        }

        // FL's Tempo / Global switches (event 218 flags: 1 envelope tempo, 2 LFO tempo, 32 global phase)
        if (hasEnvelope)
            addBool (l, p ("EnvTempo"), target + " envelope tempo-based time", false);
        addBool  (l, p ("LfoTempo"),  target + " LFO tempo-based time", false);
        addBool  (l, p ("LfoGlobal"), target + " LFO global phase", false);
        addChoice (l, p ("LfoShape"), target + " LFO shape", { "Sine", "Triangle", "Pulse" }, 0);
        // LFO speed: position = (raw - 200) / 65336, default raw 32950 (0.5 Hz).
        addFloat  (l, p ("LfoSpeed"), target + " LFO speed",        unit, (32950.0f - 200.0f) / 65336.0f, percentAttr());
        addFloat  (l, p ("LfoAmt"),   target + " LFO amount",       bipolar, 0.0f, percentAttr());
        addFloat  (l, p ("LfoDel"),   target + " LFO predelay time", unit, 0.0f, envTimeAttr());
        addFloat  (l, p ("LfoAtt"),   target + " LFO attack time",   unit, dsp::flEnvKnobForRaw (20000), envTimeAttr());
    }

}

// FL Studio names MIDI note 60 "C5".
namespace layout
{
    inline const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

inline juce::String flNoteName (int midiNote)
{
    const auto* const* names = layout::noteNames;
    return juce::String (names[midiNote % 12]) + juce::String (midiNote / 12);
}

inline int parseNoteName (const juce::String& text)
{
    const auto* const* names = layout::noteNames;
    auto t = text.trim().toUpperCase();
    for (int n = 11; n >= 0; --n)
        if (t.startsWith (names[n]))
            return juce::jlimit (0, 127, n + 12 * t.substring ((int) strlen (names[n])).getIntValue());
    return juce::jlimit (0, 127, t.getIntValue());
}

inline const juce::StringArray& precomputeParamIDs()
{
    static const juce::StringArray ids {
        id::pcRemoveDC, id::pcReversePolarity, id::pcNormalize, id::pcFadeStereo,
        id::pcReverse, id::pcSwapStereo, id::pcStart, id::pcLength, id::pcFadeIn,
        id::pcFadeOut, id::pcCrossfade, id::pcTrim, id::pcBoost, id::pcBoostClip, id::pcEq,
        id::pcRmMix, id::pcRmFreq, id::pcCut, id::pcRes, id::pcReverbType, id::pcReverb,
        id::pcStereoDelay, id::pcPogo, id::loadRegions, id::loadSlices,
        // The loop crossfade is baked into the data, so it depends on the loop points.
        id::useLoop, id::loopStart, id::loopEnd
    };
    return ids;
}

// The layout; sampleLength is the instance's loaded sample in frames, for the loop point knobs' text (FL shows them
// as sample positions).
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const std::atomic<int>& sampleLength)
{
    using namespace layout;
    Layout l;
    const juce::NormalisableRange<float> unit { 0.0f, 1.0f }, bipolar { -1.0f, 1.0f };

    // MIDI pitch bend range. FL's channel VOL / PAN / PITCH / RANGE / TRACK
    // belong to the host channel, which applies them to any plugin.
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::pitchRange), "Pitch bend range", 0, 48, 2));

    // Content / Playback. Option names are FL's own (its declicking menu's).
    addChoice (l, id::declick, "Declicking mode",
               { "Out only (no bleeding)", "Transient (no bleeding)", "Transient (bleeding)",
                 "Generic (bleeding)", "Smooth (bleeding)", "Crossfade (bleeding)" }, 0);
    // Markers stored in the file replace the detected transients of the slice
    // methods (measured in FL): regions = WAV cue points, slice markers = the
    // ACID "strc" chunk. FL's defaults: regions on, slice markers off.
    addBool  (l, id::loadRegions, "Load regions", true);
    addBool  (l, id::loadSlices,  "Load slice markers", false);
    addFloat (l, id::startOffset, "Sample start offset", unit, 0.0f, percentAttr());
    // FL Sampler defaults (a new channel): Use loop points off, START = END = 0
    // (no loop set). FL's Basic project template has it on in its channels.
    addBool  (l, id::useLoop,   "Use loop points", false);

    // Loop points: FL shows sample positions (position = frame / sample length).
    auto frames = [&sampleLength] (float v, int) { return juce::String (juce::roundToInt (v * (float) sampleLength.load())); };
    auto fromFrames = [&sampleLength] (const juce::String& t)
    {
        const auto n = sampleLength.load();
        return n > 0 ? (float) (firstNumber (t) / n) : 0.0f;
    };
    addFloat (l, id::loopStart, "Loop start position", unit, 0.0f, attr (frames, fromFrames));
    addFloat (l, id::loopEnd,   "Loop end position",   unit, 0.0f, attr (frames, fromFrames));
    addBool  (l, id::pingPong,  "Ping pong loop", false);

    // OUT: FL's channel volume knob, shown as FL shows it ("78%  -5.2dB  0.55").
    addFloat (l, id::output, "Output", unit, dsp::defaultOutputPosition,
              attr ([] (float v, int)
                    {
                        const auto g = dsp::flVolumeGain (v);
                        return juce::String (juce::roundToInt (v * 100.0f)) + "%  "
                             + (g > 0.0f ? juce::String (juce::Decibels::gainToDecibels (g), 1) + "dB" : juce::String ("-INFdB"))
                             + "  " + juce::String (g, 2);
                    }, fromPct()));

    // Time stretching (SMP > Time stretching).
    addChoice (l, id::stretchMode, "Stretch method",
               { "Resample", "Stretch", "Stretch Pro", "e3 generic", "e3 mono", "Slice stretch", "Slice map",
                 "Auto", "e2 generic", "e2 transient", "e2 mono", "e2 speech" }, 0);
    // MUL: 2^(raw / 10000), raw +/-20000, so the knob is linear in octaves (x0.25 .. x4).
    juce::NormalisableRange<float> mulRange { 0.25f, 4.0f,
        [] (float, float, float n) { return std::pow (2.0f, 4.0f * n - 2.0f); },
        [] (float, float, float v) { return (std::log2 (v) + 2.0f) / 4.0f; } };
    addFloat (l, id::stretchMul, "Time stretch (multiplicator)", mulRange, 1.0f,
              attr ([] (float v, int) { return juce::String (v * 100.0f, 2) + "%"; }, fromPct()));
    // TIME: position = beats / 192, shown as bars:steps:ticks.
    addFloat (l, id::stretchTime, "Time stretch (time)", { 0.0f, 192.0f }, 0.0f,
              attr ([] (float v, int) { return barsStepsTicks (v); },
                    [] (const juce::String& t) { return (float) (parseStepsTicks (t) / 4.0); }));
    // FORMANT (Stretch Pro only, measured in FL): +/-1200 cents, linear, centre
    // shows "preserve"; a plain typed number is a percentage of the slider.
    addFloat (l, id::formant, "Formant shift amount", { -1200.0f, 1200.0f }, 0.0f,
              attr ([] (float v, int)
                    {
                        const auto c = juce::roundToInt (v);
                        return c == 0 ? juce::String ("preserve") : (c > 0 ? "+" : "") + juce::String (c) + " cents";
                    },
                    [] (const juce::String& t)
                    {
                        if (t.containsIgnoreCase ("preserve")) return 0.0f;
                        if (t.containsIgnoreCase ("cent")) return (float) firstNumber (t);
                        return (float) ((firstNumber (t) / 100.0 - 0.5) * 2400.0);
                    }));
    // PITCH: +/-1200 cents.
    addFloat (l, id::pitch, "Pitch shift", { -1200.0f, 1200.0f }, 0.0f, attr (cents(), fromNum()));

    // Precomputed tools (FL does not automate them; here they are, so a host's
    // undo history records them)
    addBool (l, id::pcRemoveDC,        "Remove DC offset", false);
    addBool (l, id::pcReversePolarity, "Reverse polarity", false);
    addBool (l, id::pcNormalize,       "Normalize",        false);
    addBool (l, id::pcFadeStereo,      "Fade stereo",      false);
    addBool (l, id::pcReverse,         "Reverse",          false);
    addBool (l, id::pcSwapStereo,      "Swap stereo",      false);
    addFloat (l, id::pcStart,     "Sample start offset (precomputed)", unit, 0.0f, attr (pctSpace(), fromPct()));
    addFloat (l, id::pcLength,    "Sample end offset",          unit, 1.0f, attr (pctSpace(), fromPct()));
    addFloat (l, id::pcFadeIn,    "Quick fade in",              step (1024), 0.0f, percentAttr());   // raw / 1024
    addFloat (l, id::pcFadeOut,   "Quick fade out",             step (1024), 0.0f, percentAttr());   // raw / 1024
    addFloat (l, id::pcCrossfade, "Crossfade loop",             step (1024), 0.0f, percentAttr());   // raw / 1024
    addFloat (l, id::pcTrim,      "Silence trimming threshold", step (256), 0.0f, percentAttr());   // raw / 256

    // Precomputed effects
    addFloat (l, id::pcBoost, "Boosts the gain", step (256), 0.0f, percentAttr());                   // raw / 256
    addBool  (l, id::pcBoostClip, "Add clipping at 0dB", false);
    addFloat (l, id::pcEq,          "Shifts the frequency balance", step (128, -1.0f), 0.0f, percentAttr());
    addFloat (l, id::pcRmMix,       "Ring modulation mix",          step (256), 0.0f, percentAttr());
    addFloat (l, id::pcRmFreq,      "Ring modulation frequency",    step (256), 0.5f, percentAttr());
    // Cutoff: position = (raw - 16) / 1008, shown as raw / 1024.
    addFloat (l, id::pcCut,         "Filter cutoff", { 16.0f / 1024.0f, 1.0f, 1.0f / 1024.0f }, 1.0f, percentAttr());
    addFloat (l, id::pcRes,         "Filter resonance",             step (640), 0.0f, percentAttr());  // raw / 640
    addChoice (l, id::pcReverbType, "Reverb type", { "A", "B" }, 1);   // FL default: B
    addFloat (l, id::pcReverb,      "Reverb amount",                step (256), 0.0f, percentAttr());
    addFloat (l, id::pcStereoDelay, "Stereo delay",                 step (2048, -1.0f), 0.0f, attr (leftRight(), fromLeftRight()));
    addFloat (l, id::pcPogo,        "Pogo effect",                  step (256, -1.0f), 0.0f, percentAttr());

    // INS targets
    addTarget (l, id::panPrefix,   "Panning", false, false);
    addTarget (l, id::volPrefix,   "Volume",  false);
    addTarget (l, id::modXPrefix,  "Mod X",   true);
    addTarget (l, id::modYPrefix,  "Mod Y",   true);
    addTarget (l, id::pitchPrefix, "Pitch",   true);

    addChoice (l, id::filterType, "Filter type",
               { "Fast LP", "LP", "BP", "HP", "BS", "LPx2", "SVF LP", "SVF LPx2" }, 0);
    addFloat  (l, id::cutoff,    "Filter frequency (modulation X)", unit, 1.0f, percentAttr());   // raw / 256
    addFloat  (l, id::resonance, "Filter bandwidth (modulation Y)", unit, 0.0f, percentAttr());   // raw / 256

    // FUNC: Levels adjustment (FL's channel levels: pan raw 0..12800, centre 6400)
    addFloat (l, id::pan,    "Panning offset", bipolar, 0.0f, attr (leftRight(), fromLeftRight()));       // raw +/-6400
    // FL's Levels "Volume multiplier" (event 229, raw / 12800; measured: gain
    // (11^x - 1) / 10, 100% = unity at the knob's centre, 200% = 12.00).
    addFloat (l, id::volume, "Volume multiplier", { 0.0f, 2.0f }, 1.0f,
              attr ([] (float v, int)
                    {
                        const auto g = dsp::flVolumeGain (v);
                        return juce::String (juce::roundToInt (v * 100.0f)) + "%  "
                             + (g > 0.0f ? juce::String (juce::Decibels::gainToDecibels (g), 1) + "dB" : juce::String ("-INFdB"))
                             + "  " + juce::String (g, 2);
                    }, fromPct()));
    addFloat (l, id::xOfs, "Modulation X offset", bipolar, 0.0f, percentAttr());                            // raw +/-256
    addFloat (l, id::yOfs, "Modulation Y offset", bipolar, 0.0f, percentAttr());

    // FUNC: Polyphony / Group
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::maxPoly), "Maximum polyphony", 0, 64, 0,
               juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? juce::String ("--") : juce::String (v); })));
    addBool  (l, id::mono,  "Monophonic mode", false);
    addBool  (l, id::porta, "Portamento mode", false);
    // Slide: position = raw / 1660, shown as steps:ticks; a new Sampler in FL
    // starts at raw 500 (0:03).
    addFloat (l, id::slide, "Portamento time", unit, 500.0f / 1660.0f,
              attr ([] (float v, int) { return stepsTicks (dsp::flSlideSteps (v)); },
                    [] (const juce::String& t)
                    {
                        const auto ticks = juce::jmax (0.0, parseStepsTicks (t)) * 24.0 + 1.0;
                        const auto& law = dsp::tuning();   // the inverse of dsp::flSlideTicks
                        return (float) juce::jlimit (0.0, 1.0, std::log ((ticks + law.slideTicksC) / law.slideTicksA) / std::log (law.slideTicksBase));
                    }));
    addBool  (l, id::cutItself, "Cut itself", false);
    // Group (FL's Cut / By), shared between Chan Sampler instances (CutGroups.h); 0 = none ("--").
    for (auto* gid : { id::cutGroup, id::cutBy })
        l.add (std::make_unique<juce::AudioParameterInt> (paramId (gid), gid == id::cutGroup ? "Cut group" : "Cut by group", 0, 99, 0,
                   juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? juce::String ("--") : juce::String (v); })));

    // FUNC: Tracking (raw +/-256)
    addFloat (l, id::velPan, "Velocity to panning",      bipolar, 0.0f, percentAttr());
    addFloat (l, id::velX,   "Velocity to modulation X", bipolar, 0.0f, percentAttr());
    addFloat (l, id::velY,   "Velocity to modulation Y", bipolar, 0.0f, percentAttr());
    addFloat (l, id::velMid, "Velocity middle value", unit, 100.0f / 128.0f, percentAttr());
    addFloat (l, id::keyPan, "Key to panning",      bipolar, 0.0f, percentAttr());
    addFloat (l, id::keyX,   "Key to modulation X", bipolar, 0.0f, percentAttr());
    addFloat (l, id::keyY,   "Key to modulation Y", bipolar, 0.0f, percentAttr());
    // FL's key range is C0..C10 (note 0..120; C5 = 0.5, measured with Copy value).
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::keyMid), "Middle value", 0, 120, 60,
               juce::AudioParameterIntAttributes()
                   .withStringFromValueFunction ([] (int v, int) { return flNoteName (v); })
                   .withValueFromStringFunction ([] (const juce::String& t) { return parseNoteName (t); })));

    // Arpeggiator
    addChoice (l, id::arpDir, "Arpeggio direction",
               { "Off", "Up", "Down", "Up / down", "Up / down (sticky)", "Random" }, 0);
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::arpRange), "Arpeggio range (octaves)", 1, 6, 1));
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::arpRepeat), "Arpeggio repeat", 1, 16, 1));
    addFloat (l, id::arpTime, "Arpeggio time", unit, (1024.0f - 450.0f) / 997.0f,
              attr ([] (float v, int) { const auto s = dsp::flArpSteps (v); return s <= 0.0 ? juce::String ("Hold") : stepsTicks (s); },
                    [] (const juce::String& t)
                    {
                        if (t.containsIgnoreCase ("hold")) return 1.0f;
                        const auto ticks = juce::jlimit (1.0, 96.0, parseStepsTicks (t) * 24.0);
                        // the smallest raw value that plays this many ticks
                        for (int raw = 450; raw < 1447; ++raw)
                            if (dsp::flArpTicksForRaw (raw) >= std::round (ticks))
                                return (float) (raw - 450) / 997.0f;
                        return (float) (1446 - 450) / 997.0f;
                    }));
    addFloat (l, id::arpGate, "Arpeggio gate", unit, 1.0f, percentAttr());
    addBool  (l, id::arpSlide, "Arpeggio slide", false);
    juce::StringArray chords { "Auto", "Auto (sustain)", "(none)" };
    for (auto& ch : dsp::chordPresets)
        chords.add (ch.shortName);   // FL shows the short names (sus2, 6...)
    addChoice (l, id::arpChord, "Arpeggio chord", chords, 1);   // FL default: Auto (sustain) (raw -1)

    // Echo delay / fat mode. Each echo is an extra note voice.
    addFloat (l, id::echoFeed, "Delay feedback", unit, 0.0f,                         // raw / 25600, shown /12800
              attr ([] (float v, int) { return juce::String (juce::roundToInt (v * 200.0f)) + "%"; },
                    [] (const juce::String& t) { return (float) (firstNumber (t) / 200.0); }));
    // Echo pan: position = 0.5 + (raw - 6400) / 25600, so 100% sits at 0.25 / 0.75.
    addFloat (l, id::echoPan,   "Delay panning",      { -2.0f, 2.0f }, 0.0f, attr (leftRight(), fromLeftRight()));
    addFloat (l, id::echoX,     "Delay modulation X", bipolar, 0.0f, percentAttr());
    addFloat (l, id::echoY,     "Delay modulation Y", bipolar, 0.0f, percentAttr());
    addFloat (l, id::echoPitch, "Delay pitch", { -1200.0f, 1200.0f }, 0.0f, attr (cents(), fromNum()));
    addFloat (l, id::echoTime,  "Delay time", { 0.0f, 8.0f }, 3.0f,                // raw / 48 steps
              attr ([] (float v, int) { return stepsTicks (v); }, [] (const juce::String& t) { return (float) parseStepsTicks (t); }));
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::echoCount), "Echoes", 1, 16, 4));
    addBool  (l, id::echoPing, "Ping pong", false);
    addBool  (l, id::echoFat,  "Fat mode", false);

    // Root note (FL default C5)
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::rootNote), "Root note", 0, 127, 60,
               juce::AudioParameterIntAttributes()
                   .withStringFromValueFunction ([] (int v, int) { return flNoteName (v); })
                   .withValueFromStringFunction ([] (const juce::String& t) { return parseNoteName (t); })));
    addFloat (l, id::fineTune, "Fine tuning", { -100.0f, 100.0f }, 0.0f,
              attr ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " cents"; }, fromNum()));

    // The host's undo step for group edits, renamed after each (History)
    l.add (std::make_unique<HistoryStepParameter> (paramId (id::historyStep), "History step"));

    return l;
}
}
