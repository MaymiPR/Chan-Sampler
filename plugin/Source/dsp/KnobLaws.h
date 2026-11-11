#pragma once

// FL's knob laws: a knob's position to the time, level or length FL plays (MEASUREMENTS.md "Measured laws"), and
// FL's song tick, which its envelopes, LFOs, glides and filter modulation run on.

#include <cmath>

#include "Numeric.h"

namespace cs::dsp
{
// FL's song tick (96 per beat), which its envelopes, LFOs, glides and filter
// modulation run on.
inline double tickSeconds (double bpm) { return 60.0 / (jmax (1.0, bpm) * 96.0); }

// How fast a stretched note reads its sample: TIME (beats at the tempo, 0 =
// off) sets the length, MUL multiplies it (1 = the sample's own length).
inline double stretchSpeed (double sampleSeconds, float mul, float timeBeats, double bpm)
{
    const auto m = std::abs (mul - 1.0f) < 1.0e-4f ? 1.0 : (double) mul;
    return (timeBeats > 0.01f ? sampleSeconds / ((double) timeBeats * 60.0 / jmax (1.0, bpm)) : 1.0) / m;
}

// OUT is FL's channel volume knob (measured in FL: gain (11^x - 1) / 10 of
// its position, 78% = raw 10000 of 12800 by default, 100% = 0 dB at its top).
// Measured in FL too: its plugin wrapper outputs exactly twice what a default
// FL Sampler channel does (0.7792 vs 0.3896 at every velocity), so a default
// Chan Sampler outputs 0.5 in total. FL applies its own channel volume to
// plugin channels as well, so OUT's gain is scaled to give 0.5 at its default;
// the FUNC Levels VOL is FL's "Volume multiplier" (100% = unity).
inline constexpr float defaultOutputPosition = 10000.0f / 12800.0f;
// The plug-in's output gain at an OUT position.
float outputLevel (float position);

// FL's channel volume law: (11^x - 1) / 10 for x = 0..2 (1 = unity).
float flVolumeGain (float x);

// Envelope / LFO time knobs: FL's raw 100 .. 65536 as a knob position.
inline float flEnvKnobForRaw (int raw) { return (float) (raw - 100) / 65436.0f; }

// Envelope time knob (FL position, raw = 100 + pos * 65436) to seconds, from
// times measured in FL (about 64 s at the maximum).
float flEnvSeconds (float knob);

// Inverse of flEnvSeconds: the knob position for a time in seconds.
float flEnvKnobForSeconds (float seconds);

// FL's LFO runs on the song's ticks (96 per beat). The speed knob (raw = 200
// + pos * 65336) gives the period in ticks at 130 BPM: 13.5755 * 982.5^pos -
// 13.2745, fitted to 42 speeds measured from FL renders (default raw 32950 =
// 416 ticks = 2 s). The predelay knob (raw = 100 + pos * 65436) follows the
// same law on its raw value (measured at 11 values). Unrounded: the LFO
// rounds them at the reference tempo, the period to at least 3 ticks (see
// Lfo).
double flLfoPeriodT130 (float speedKnob);
double flLfoDelayT130 (float delayKnob);

// Portamento slide knob (raw = pos * 1660) to steps (16ths).
double flSlideSteps (float knob);
int flSlideTicks (float knob);

// Arpeggio time knob to steps; 0 means Hold.
double flArpSteps (float knob);
// The ticks one arpeggio step plays at an integer raw value of its time knob.
double flArpTicksForRaw (double raw);

// Envelope tension (FL raw / 128, -1..1) to the curve exponent k of
// (e^(kx) - 1) / (e^k - 1), interpolated from measurements.
float flTensionToK (float tension);
}
