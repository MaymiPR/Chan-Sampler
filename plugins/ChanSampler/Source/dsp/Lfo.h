#pragma once

// FL's channel LFO on its tick grid.

#include <cmath>

#include "KnobLaws.h"
#include "Modes.h"
#include "Numeric.h"
#include "Tuning.h"

namespace cs::dsp
{
// Per-note LFO (retriggered at note-on) with delay and fade-in.
// FL's channel LFO (measured from FL renders, nulltest/cases_lfo.py,
// nulltest/probe_tempo.py). FL evaluates it once per song tick (96 per beat) and
// interpolates linearly in between. The speed and predelay knobs give a
// length T in ticks at 130 BPM; the period is round (T * ref / 130) ticks
// with ref = the project tempo (Tempo off: the same length in seconds at
// any tempo) or 120 (Tempo on: a fixed musical length). The attack fade is
// in seconds (x 120 / tempo with Tempo on). Global takes the phase from the
// song position instead of the note start. All shapes start at 0 going up.
class Lfo
{
public:
    struct Params
    {
        LfoShape shape = LfoShape::sine;
        double periodT130 = 416, delayT130 = 0;   // ticks at 130 BPM (unrounded)
        float amount = 0, attack = 0;             // attack in seconds
        bool tempo = false, global = false;
    };

    void noteOn() { time = 0; }

    // Returns amount * fade * wave, in -1..1, at the end of the step.
    // songSeconds: the song position at the end of the step (Global).
    float advance (const Params& p, float seconds, double bpm, double songSeconds)
    {
        time += (double) seconds;
        return at (p, time, bpm, songSeconds);
    }

    // Seconds since the note started (after the last advance()).
    double getTime() const noexcept { return time; }

    // The value at a time since the note started (and song position, for
    // Global), for per-sample use.
    static float at (const Params& p, double seconds, double bpm, double songSeconds)
    {
        if (p.amount == 0.0f)
            return 0.0f;
        const auto tick = tickSeconds (bpm);
        const auto ref = p.tempo ? 120.0 : jmax (1.0, bpm);
        Timing t;
        t.period = jmax (tuning().lfoMinPeriodTicks, std::round (p.periodT130 * ref / 130.0));
        t.delay = jmax (0.0, std::round (p.delayT130 * ref / 130.0));
        t.attackTicks = (p.tempo ? p.attack * 120.0 / jmax (1.0, bpm) : (double) p.attack) / tick;
        // Global: knots on the song's ticks, the phase from the song start;
        // the predelay / fade still count from the note start.
        const auto noteTicks = seconds / tick;
        const auto ticks = p.global ? songSeconds / tick : noteTicks;
        t.noteOffset = p.global ? ticks - noteTicks : 0.0;   // song tick of the note start
        const auto k = std::floor (ticks);
        // FL holds the first tick's value from the note start until tick 1.
        const auto first = p.global ? k : jmax (1.0, k);
        const auto a = valueAt (p, t, first), b = valueAt (p, t, k + 1.0);
        const auto f = (float) (ticks - k);
        return a + (b - a) * f;
    }

private:
    struct Timing { double period = 416, delay = 0, attackTicks = 0, noteOffset = 0; };

    static float valueAt (const Params& p, const Timing& tm, double tick)
    {
        // ticks since the predelay ended, counted from the note start
        const auto sinceDelay = tick - tm.noteOffset - tm.delay;
        // (Global without predelay: running from the note's first sample, measured)
        if (sinceDelay < 0.0 && ! (p.global && tm.delay <= 0.0))
            return 0.0f;
        // phase: from the end of the predelay, or from the song start (Global)
        auto t = p.global ? tick : sinceDelay;
        if (t < 0.0) t = 0.0;
        const auto period = jmax (1.0, tm.period);
        const auto phase = (float) std::fmod (t / period, 1.0);
        const auto fade = tm.attackTicks > 0.0 ? (float) jlimit (0.0, 1.0, sinceDelay / tm.attackTicks) : 1.0f;
        float wave = 0;
        switch (p.shape)
        {
            case LfoShape::sine:
                wave = std::sin (phase * MathConstants<float>::twoPi);
                break;
            case LfoShape::triangle:
                wave = phase < 0.25f ? 4.0f * phase : (phase < 0.75f ? 2.0f - 4.0f * phase : 4.0f * phase - 4.0f);
                break;
            case LfoShape::square:
            {
                // Each edge crosses over 4 ticks: 0.27205 of the way per tick
                // for 3 ticks, then the rest. The first half starts high.
                const bool high = phase < 0.5f;
                const auto since = std::fmod (t, period * 0.5);
                const auto progress = t < period * 0.5 ? 1.0
                                    : since >= 4.0 ? 1.0
                                    : since <= 3.0 ? tuning().lfoEdgeStep * since
                                                   : tuning().lfoEdgeAtThree + tuning().lfoEdgeLast * (since - 3.0);
                const auto target = high ? 1.0f : -1.0f;
                wave = -target + 2.0f * target * (float) progress;
                break;
            }
        }
        return p.amount * fade * wave;
    }

    double time = 0;
};
}
