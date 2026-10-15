#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs. Grouping follows FL Studio's channel settings window
// (top bar, SMP / INS / FUNC sheets); see docs/EVIDENCE.md.
namespace pid
{
    // MIDI pitch bend range (semitones)
    inline constexpr auto pitchRange = "pitchRange";

    // SMP > Content / Playback
    inline constexpr auto declick     = "declick";
    inline constexpr auto startOffset = "startOffset";
    inline constexpr auto useLoop     = "useLoop";
    inline constexpr auto loopStart   = "loopStart";
    inline constexpr auto loopEnd     = "loopEnd";
    inline constexpr auto pingPong    = "pingPong";

    // SMP > Time stretching (only the PITCH knob is functional: "Resample" mode)
    inline constexpr auto pitch = "pitch";
    inline constexpr auto stretchMode = "stretchMode";
    inline constexpr auto stretchMul  = "stretchMul";
    inline constexpr auto stretchTime = "stretchTime";
    inline constexpr auto formant     = "formant";   // Stretch Pro formant shift (cents)

    // Output level (matches a fresh FL Sampler channel by default)
    inline constexpr auto output = "output";

    // SMP > Precomputed effects > Tools
    inline constexpr auto pcRemoveDC        = "pcRemoveDC";
    inline constexpr auto pcReversePolarity = "pcReversePolarity";
    inline constexpr auto pcNormalize       = "pcNormalize";
    inline constexpr auto pcFadeStereo      = "pcFadeStereo";
    inline constexpr auto pcReverse         = "pcReverse";
    inline constexpr auto pcSwapStereo      = "pcSwapStereo";
    inline constexpr auto pcStart           = "pcStart";
    inline constexpr auto pcLength          = "pcLength";
    inline constexpr auto pcFadeIn          = "pcFadeIn";
    inline constexpr auto pcFadeOut         = "pcFadeOut";
    inline constexpr auto pcCrossfade       = "pcCrossfade";
    inline constexpr auto pcTrim            = "pcTrim";

    // SMP > Precomputed effects > Effects
    inline constexpr auto pcBoost       = "pcBoost";
    inline constexpr auto pcBoostClip   = "pcBoostClip";
    inline constexpr auto pcEq          = "pcEq";
    inline constexpr auto pcRmMix       = "pcRmMix";
    inline constexpr auto pcRmFreq      = "pcRmFreq";
    inline constexpr auto pcCut         = "pcCut";
    inline constexpr auto pcRes         = "pcRes";
    inline constexpr auto pcReverbType  = "pcReverbType";
    inline constexpr auto pcReverb      = "pcReverb";
    inline constexpr auto pcStereoDelay = "pcStereoDelay";
    inline constexpr auto pcPogo        = "pcPogo";

    // INS > Filter
    inline constexpr auto filterType = "filterType";
    inline constexpr auto cutoff     = "cutoff";      // MOD X
    inline constexpr auto resonance  = "resonance";   // MOD Y

    // FUNC > Levels adjustment
    inline constexpr auto pan    = "pan";
    inline constexpr auto volume = "volume";
    inline constexpr auto xOfs   = "xOfs";
    inline constexpr auto yOfs   = "yOfs";

    // FUNC > Polyphony / Group
    inline constexpr auto maxPoly   = "maxPoly";
    inline constexpr auto mono      = "mono";
    inline constexpr auto porta     = "porta";
    inline constexpr auto slide     = "slide";
    inline constexpr auto cutItself = "cutItself";
    inline constexpr auto cutGroup  = "cutGroup";    // FUNC > Group: "Cut" (this channel cuts group n)
    inline constexpr auto cutBy     = "cutBy";       // FUNC > Group: "By" (cut by group n)

    // FUNC > Tracking (velocity and keyboard)
    inline constexpr auto velPan = "velPan";
    inline constexpr auto velX   = "velX";
    inline constexpr auto velY   = "velY";
    inline constexpr auto velMid = "velMid";
    inline constexpr auto keyPan = "keyPan";
    inline constexpr auto keyX   = "keyX";
    inline constexpr auto keyY   = "keyY";
    inline constexpr auto keyMid = "keyMid";

    // Root note
    inline constexpr auto rootNote = "rootNote";
    inline constexpr auto fineTune = "fineTune";

    // FUNC > Arpeggiator
    inline constexpr auto arpDir    = "arpDir";
    inline constexpr auto arpRange  = "arpRange";
    inline constexpr auto arpRepeat = "arpRepeat";
    inline constexpr auto arpTime   = "arpTime";
    inline constexpr auto arpGate   = "arpGate";
    inline constexpr auto arpSlide  = "arpSlide";
    inline constexpr auto arpChord  = "arpChord";

    // FUNC > Echo delay / fat mode
    inline constexpr auto echoFeed   = "echoFeed";
    inline constexpr auto echoPan    = "echoPan";
    inline constexpr auto echoX      = "echoX";
    inline constexpr auto echoY      = "echoY";
    inline constexpr auto echoPitch  = "echoPitch";
    inline constexpr auto echoTime   = "echoTime";
    inline constexpr auto echoCount  = "echoCount";
    inline constexpr auto echoPing   = "echoPing";
    inline constexpr auto echoFat    = "echoFat";

    // Envelope / LFO targets (INS tabs: Panning, Volume, Mod X, Mod Y, Pitch).
    // Each has <prefix>EnvOn, Del, Att, Hold, Dec, Sus, Rel, AttT, DecT, RelT,
    // EnvAmt (not volume), LfoShape, LfoSpeed, LfoAmt, LfoDel, LfoAtt.
    inline constexpr auto panPrefix   = "pan";
    inline constexpr auto volPrefix   = "vol";
    inline constexpr auto modXPrefix  = "x";
    inline constexpr auto modYPrefix  = "y";
    inline constexpr auto pitchPrefix = "p";

    inline juce::String env (const char* prefix, const char* suffix) { return juce::String (prefix) + suffix; }
}

enum class DeclickMode { outOnly, transient, transientBleed, generic, smooth, crossfade };
enum class FilterType  { fastLP, lp, bp, hp, bs, lpx2, svfLP, svfLPx2 };
enum class LfoShape    { sine, triangle, square };

// FL's stretch methods in its own menu order (StretchModeSelect).
enum class StretchMode { resample, stretch, stretchPro, e3Generic, e3Mono, sliceStretch, sliceMap,
                         autoMode, e2Generic, e2Transient, e2Mono, e2Speech };

// Default output level. Measured in FL: its plugin wrapper outputs exactly
// twice what a default FL Sampler channel does (0.7792 vs 0.3896 at every
// velocity), so a default Chan Sampler outputs 0.5 in total. FL applies its
// own channel volume (78% by default) to plugin channels as well, so the
// plugin does not; the FUNC Levels VOL is FL's "Volume multiplier" (100% =
// unity), and the output trim supplies the 0.5: -6.0206 dB.
inline constexpr float defaultOutputDb = -6.0206f;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// FL's Auto stretch mode: plain resampling while nothing needs stretching,
// "e3 generic" once PITCH, MUL or TIME is set (as FL shows under the menu).
inline StretchMode resolveStretchMode (StretchMode mode, float pitchCents, float mul, float timeBeats)
{
    if (mode != StretchMode::autoMode)
        return mode;
    const bool stretching = std::abs (pitchCents) > 0.5f || std::abs (mul - 1.0f) > 1.0e-4f || timeBeats > 0.01f;
    return stretching ? StretchMode::e3Generic : StretchMode::resample;
}

// Parameters whose change requires rebuilding the precomputed sample.
const juce::StringArray& precomputeParamIDs();

// FL Studio names MIDI note 60 "C5".
juce::String flNoteName (int midiNote);
int parseNoteName (const juce::String& text);

// Length of the loaded sample in frames, for the loop point knobs' text.
extern std::atomic<int> displaySampleLength;

// Host tempo for the knob menus' "Set > n steps" entries (updated by the processor).
extern std::atomic<double> displayBpm;

// Inverse of flEnvSeconds: the knob position for a time in seconds.
float flEnvKnobForSeconds (float seconds);

// Envelope time knob (FL position, raw = 100 + pos * 65436) to seconds, from a
// table measured in FL (linear below raw 5000, about 64 s at the maximum).
float flEnvSeconds (float knob);

// FL's LFO is tempo-synced (96 ticks per beat). Speed knob (raw = 200 + pos *
// 65336) to the period: max (3, round (13.5755 * 982.5^pos - 13.2745)) ticks,
// fitted to 42 speeds measured from FL renders (default raw 32950 = 416 ticks
// = 2 s at 130 BPM). The predelay knob (raw = 100 + pos * 65436) uses the same
// law on its raw value, without the 3-tick minimum (measured at 11 values).
int flLfoPeriodTicks (float speedKnob);
int flLfoDelayTicks (float delayKnob);
// The same lengths before rounding (ticks at 130 BPM); the LFO rounds them at
// the reference tempo (see dsp::Lfo).
double flLfoPeriodT130 (float speedKnob);
double flLfoDelayT130 (float delayKnob);
// Hz at a tempo, for display.
inline float flLfoHz (float knob, double bpm = 130.0) { return (float) (bpm / 60.0 * 96.0 / flLfoPeriodTicks (knob)); }

// Portamento slide knob (raw = pos * 1660) to steps (16ths).
double flSlideSteps (float knob);
int flSlideTicks (float knob);

// Arpeggio time knob to steps; 0 means Hold.
double flArpSteps (float knob);

// FL's channel volume law: (11^x - 1) / 10 for x = 0..2 (1 = unity).
float flVolumeGain (float x);

// FL never ramps the attack faster than this (measured: 848 samples).
inline constexpr float flMinAttackSeconds = 0.01923f;

// Envelope tension (FL raw / 128, -1..1) to the curve exponent k of
// (e^(kx) - 1) / (e^k - 1), interpolated from measurements.
float flTensionToK (float tension);
