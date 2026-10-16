#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

#include "Params.h"

namespace dsp
{
    // Curved 0..1 ramp (e^(kx) - 1) / (e^k - 1): k = 0 is linear, k > 0
    // starts slow. FL's envelope tension maps to k via flTensionToK().
    inline float tensionCurve (float x, float k)
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        if (std::abs (k) < 1.0e-4f)
            return x;
        return (std::exp (k * x) - 1.0f) / (std::exp (k) - 1.0f);
    }

    // Inverse of tensionCurve: the x where the curve reaches y.
    inline float tensionCurveInverse (float y, float k)
    {
        y = juce::jlimit (0.0f, 1.0f, y);
        if (std::abs (k) < 1.0e-4f)
            return y;
        return std::log1p (y * (std::exp (k) - 1.0f)) / k;
    }

    inline float sCurve (float x)
    {
        return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * juce::jlimit (0.0f, 1.0f, x));
    }

    // DAHDSR envelope from the INS tab, as FL works it out (measured,
    // nulltest/env_tick_probe.py, env_time_probe.py, tempo_env_cases.py): once
    // per song tick (96 per beat) from the note start, interpolated linearly in
    // between. Every stage lasts a whole number of ticks, N = ceil (time / tick
    // + 1/2), none for no time. From one tick to the next the level moves at
    // most the full range per 8.83 ms, which is what smooths a jump (a stage
    // with no time). The decay runs the whole curve from 1 to 0 and stops at the
    // sustain level; the release joins its curve at the current level; decay
    // and release curves are the attack curve reversed.
    class Envelope
    {
    public:
        struct Params
        {
            float delay = 0, attack = 0, hold = 0, decay = 0.5f, sustain = 1, release = 0.2f;   // seconds / level
            float attackK = 0, decayK = 0, releaseK = 0;                                     // curve exponents
            double tick = 60.0 / (130.0 * 96.0);                                             // song tick, seconds
        };

        static constexpr double fullRangeSeconds = 389.5 / 44100.0;   // the fastest move, 0 to 1

        void noteOn()  { stage = Stage::delay; ticks = 0; phase = 0; from = to = out = 0; segment = 0; pending = true; releaseNext = false; ended = false; }
        void reset()   { stage = Stage::idle; from = to = out = 0; pending = false; }
        bool isIdle() const noexcept     { return stage == Stage::idle && ! pending && from == 0.0f && to == 0.0f; }
        bool isReleased() const noexcept { return stage == Stage::release || stage == Stage::idle || releaseNext; }
        // The release has reached the tick where it is silent (the level may
        // still be moving there). FL ends the voice there with its declick fade.
        bool hasEnded() const noexcept   { return ended; }

        // The release starts now: the next tick already moves along its curve.
        // FL's notes end on a tick, which comes first there; a note-off less
        // than nearTick seconds before a tick is taken as on it.
        void noteOff (const Params& p, double nearTick = 0)
        {
            if (stage == Stage::idle || stage == Stage::release || releaseNext)
                return;
            start (p);
            if (p.tick - phase <= nearTick)
            {
                releaseNext = true;   // from the tick on (advance)
                return;
            }
            beginRelease (p, out, (p.tick - phase) / p.tick);
            // Go on from the level heard now to the release's next tick.
            from = out;
            segment = phase;
            to = limit (p, from, level (p));
        }

        // The level now, without moving on.
        float current (const Params& p)
        {
            start (p);
            const auto span = p.tick - segment;
            return out = from + (to - from) * (float) (span > 0 ? (phase - segment) / span : 1.0);
        }

        // Advances by seconds and returns the level there.
        float advance (const Params& p, float seconds)
        {
            start (p);
            phase += (double) seconds;
            while (phase >= p.tick)
            {
                phase -= p.tick;
                segment = 0;
                from = to;
                ended = ended || stage == Stage::idle;
                if (releaseNext)
                {
                    releaseNext = false;
                    beginRelease (p, from, 1.0);
                }
                to = limit (p, from, level (p));
            }
            const auto span = p.tick - segment;
            return out = from + (to - from) * (float) (span > 0 ? (phase - segment) / span : 1.0);
        }

    private:
        enum class Stage { idle, delay, attack, hold, decay, sustain, release };

        void beginRelease (const Params& p, float level, double ticksAtNext)
        {
            releaseFrom = tensionCurveInverse (level, p.releaseK);
            stage = Stage::release;
            releaseTicks = ticksAtNext;   // ticks into the release at the next tick
        }

        static int stageTicks (float seconds, const Params& p)
        {
            return seconds > 0 ? (int) std::ceil ((double) seconds / p.tick + 0.5) : 0;
        }

        static float limit (const Params& p, float now, float target)
        {
            const auto step = (float) (p.tick / fullRangeSeconds);
            return now + juce::jlimit (-step, step, target - now);
        }

        // The first two ticks. With neither delay nor attack the level starts
        // at the second tick's (no rise from silence).
        void start (const Params& p)
        {
            if (! pending)
                return;
            pending = false;
            from = level (p);
            const auto second = level (p);
            if (p.delay <= 0 && p.attack <= 0)
                from = second;
            to = limit (p, from, second);
            out = from;
        }

        // The level at the next tick, then one tick on. The delay and the
        // attack end are rounded from the note start, the hold's ticks are its
        // own and its last tick is where the decay starts (or, with no decay
        // time, the last tick before the fall); the decay's ticks are its own
        // (it runs at a rate down to the sustain level). Measured over 74
        // attack / hold / tempo combinations (nulltest/env_hold_probe.py), this
        // gets FL's hold end exactly in 48 and within a tick in the rest.
        float level (const Params& p)
        {
            if (stage == Stage::idle)
                return 0.0f;
            if (stage == Stage::release)
            {
                const int n = stageTicks (p.release, p);
                const auto x = n > 0 ? releaseFrom - (float) (releaseTicks / n) : 0.0f;
                releaseTicks += 1.0;
                if (x <= 0.0f) { stage = Stage::idle; return 0.0f; }
                return tensionCurve (x, p.releaseK);
            }
            const int k = ticks++;
            const int delayEnd = stageTicks (p.delay, p);
            const int attackEnd = p.attack > 0 ? stageTicks (p.delay + p.attack, p) : delayEnd;
            const int holdEnd = attackEnd + stageTicks (p.hold, p);
            if (k < delayEnd)  { stage = Stage::delay; return 0.0f; }
            if (k < attackEnd) { stage = Stage::attack; return tensionCurve ((float) (k - delayEnd) / (float) (attackEnd - delayEnd), p.attackK); }
            if (k < holdEnd)   { stage = Stage::hold; return 1.0f; }
            if (stage != Stage::sustain)
            {
                const int n = stageTicks (p.decay, p);
                const int j = k - holdEnd + (p.hold > 0 ? 1 : 0);   // the hold's last tick is the decay's first
                const auto v = n > 0 ? tensionCurve (1.0f - (float) j / (float) n, p.decayK) : 0.0f;
                if (v > p.sustain) { stage = Stage::decay; return v; }
                stage = Stage::sustain;
            }
            return p.sustain;
        }

        Stage stage = Stage::idle;
        int ticks = 0;                 // the next tick (from the note start)
        double releaseTicks = 0;       // the same for the release (it can start between ticks)
        double phase = 0;              // seconds since the last tick
        double segment = 0;            // where the current line starts (a release between ticks)
        float from = 0, to = 0, out = 0, releaseFrom = 0;
        bool pending = false, releaseNext = false, ended = false;
    };

    // Per-note LFO (retriggered at note-on) with delay and fade-in.
    // FL's channel LFO (measured from FL renders, nulltest/lfo_cases.py,
    // tempo_probe.py). FL evaluates it once per song tick (96 per beat) and
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

        void noteOn() { time = 0; depth = 0; }

        // Returns amount * fade * wave, in -1..1, at the end of the step.
        // songSeconds: the song position at the end of the step (Global).
        float advance (const Params& p, float seconds, double bpm, double songSeconds)
        {
            time += (double) seconds;
            return at (p, time, bpm, songSeconds, &depth);
        }

        // Seconds since the note started (after the last advance()).
        double getTime() const noexcept { return time; }

        // The value at a time since the note started (and song position, for
        // Global), for per-sample use.
        static float at (const Params& p, double seconds, double bpm, double songSeconds, float* depthOut = nullptr)
        {
            if (p.amount == 0.0f)
            {
                if (depthOut != nullptr) *depthOut = 0.0f;
                return 0.0f;
            }
            const auto tickSeconds = 60.0 / (juce::jmax (1.0, bpm) * 96.0);
            const auto ref = p.tempo ? 120.0 : juce::jmax (1.0, bpm);
            Timing t;
            t.period = juce::jmax (3.0, std::round (p.periodT130 * ref / 130.0));
            t.delay = juce::jmax (0.0, std::round (p.delayT130 * ref / 130.0));
            t.attackTicks = (p.tempo ? p.attack * 120.0 / juce::jmax (1.0, bpm) : (double) p.attack) / tickSeconds;
            // Global: knots on the song's ticks, the phase from the song start;
            // the predelay / fade still count from the note start.
            const auto noteTicks = seconds / tickSeconds;
            const auto ticks = p.global ? songSeconds / tickSeconds : noteTicks;
            t.noteOffset = p.global ? ticks - noteTicks : 0.0;   // song tick of the note start
            const auto k = std::floor (ticks);
            // FL holds the first tick's value from the note start until tick 1.
            const auto first = p.global ? k : juce::jmax (1.0, k);
            const auto a = valueAt (p, t, first), b = valueAt (p, t, k + 1.0);
            const auto f = (float) (ticks - k);
            if (depthOut != nullptr) *depthOut = std::abs (a.depth + (b.depth - a.depth) * f);
            return a.value + (b.value - a.value) * f;
        }

        // amount * fade-in from the last advance() call.
        float getDepth() const noexcept { return depth; }

    private:
        struct Knot { float value, depth; };
        struct Timing { double period = 416, delay = 0, attackTicks = 0, noteOffset = 0; };

        static Knot valueAt (const Params& p, const Timing& tm, double tick)
        {
            // ticks since the predelay ended, counted from the note start
            const auto sinceDelay = tick - tm.noteOffset - tm.delay;
            // (Global without predelay: running from the note's first sample, measured)
            if (sinceDelay < 0.0 && ! (p.global && tm.delay <= 0.0))
                return { 0.0f, 0.0f };
            // phase: from the end of the predelay, or from the song start (Global)
            auto t = p.global ? tick : sinceDelay;
            if (t < 0.0) t = 0.0;
            const auto period = juce::jmax (1.0, tm.period);
            const auto phase = (float) std::fmod (t / period, 1.0);
            const auto fade = tm.attackTicks > 0.0 ? (float) juce::jlimit (0.0, 1.0, sinceDelay / tm.attackTicks) : 1.0f;
            float wave = 0;
            switch (p.shape)
            {
                case LfoShape::sine:
                    wave = std::sin (phase * juce::MathConstants<float>::twoPi);
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
                                        : since <= 3.0 ? 0.27205 * since : 0.81615 + 0.18385 * (since - 3.0);
                    const auto target = high ? 1.0f : -1.0f;
                    wave = -target + 2.0f * target * (float) progress;
                    break;
                }
            }
            return { p.amount * fade * wave, p.amount * fade };
        }

        double time = 0;
        float depth = 0;
    };

    // FL's channel filter, measured type by type (nulltest/, docs/EVIDENCE.md).
    // u = Mod X (cutoff, 0..1), res = Mod Y (0..1).
    //  Fast LP : y = y1 + f (x - y1) + r (y1 - y2), f = (601^u - 1) / 600,
    //            r from the measured resonance curve (same as the precomputed filter).
    //  LP/BP/HP/BS : RBJ biquads; f0 and Q interpolated from measured tables.
    //  SVF LP  : Chamberlin state variable filter, f = (26^u - 1) / 25, q = 1 - 0.8 res.
    //  LPx2 / SVF LPx2 : two stages in series (SVF LPx2 ~ -48 dB null, LPx2 approximate).
    double flOnePoleF (double u);
    double flResonance (double res);
    double flBiquadF0 (double u, double sampleRate);
    double flBiquadQ (double res);
    double flLpx2Gain (double u, double res);   // LPx2's level compensation (measured)

    class VoiceFilter
    {
    public:
        // A note starts at unity and reaches the LPx2 compensation after a tick.
        void startCompensation (int tickSamples) { compNow = 1.0; compLeft = juce::jmax (1, tickSamples); }

        void reset()
        {
            for (auto& s : st) s = {};
        }

        void setup (FilterType t, double u, double res, double sampleRate)
        {
            type = t;
            // Modulation can push the biquads' cutoff past the knob's end (measured).
            const bool biquad = t != FilterType::fastLP && t != FilterType::svfLP && t != FilterType::svfLPx2;
            u = juce::jlimit (0.0, biquad ? 2.0 : 1.0, u);
            res = juce::jlimit (0.0, 1.0, res);
            switch (type)
            {
                case FilterType::fastLP:
                    f = flOnePoleF (u);
                    r = flResonance (res);
                    break;
                case FilterType::svfLP:
                case FilterType::svfLPx2:
                    f = (std::pow (26.0, u) - 1.0) / 25.0;
                    q = 1.0 - 0.8 * res;
                    q2 = 1.0 - 0.6 * res;   // SVF LPx2's second stage (measured: q1 1 - 0.8 res, q2 1 - 0.6 res)
                    break;
                default:
                {
                    const auto w = juce::jmin (3.0, juce::MathConstants<double>::twoPi * flBiquadF0 (u, sampleRate) / sampleRate);   // FL caps at 3.0 rad (measured)
                    const auto Q = flBiquadQ (res);
                    comp = type == FilterType::lpx2 ? flLpx2Gain (u, res) : 1.0;
                    const auto alpha = std::sin (w) / (2.0 * Q), c = std::cos (w), a0 = 1.0 + alpha;
                    a1 = -2.0 * c / a0;
                    a2 = (1.0 - alpha) / a0;
                    switch (type)
                    {
                        case FilterType::bp: b0 = alpha / a0;            b1 = 0.0;              b2 = -alpha / a0;        break;
                        case FilterType::hp: b0 = (1.0 + c) / 2.0 / a0;  b1 = -(1.0 + c) / a0;  b2 = b0;                 break;
                        case FilterType::bs: b0 = 1.0 / a0;              b1 = -2.0 * c / a0;    b2 = b0;                 break;
                        default:             b0 = (1.0 - c) / 2.0 / a0;  b1 = (1.0 - c) / a0;   b2 = b0;                 break;
                    }
                    break;
                }
            }
        }

        // Coefficients a fraction w of the way from a to b (FL moves a modulated
        // filter this way between ticks; measured). The Fast LP's resonance
        // term instead holds b's value for the whole tick (measured). The state is kept.
        void setBetween (const VoiceFilter& a, const VoiceFilter& b, double w)
        {
            type = a.type;
            if (type != FilterType::fastLP)
                w = 1.0;   // biquads and SVFs: the end-of-tick coefficients for the whole tick (measured)
            auto mix = [w] (double x, double y) { return x + (y - x) * w; };
            f = mix (a.f, b.f);   r = b.r;   q = mix (a.q, b.q);   q2 = mix (a.q2, b.q2);   comp = b.comp;
            b0 = mix (a.b0, b.b0); b1 = mix (a.b1, b.b1); b2 = mix (a.b2, b.b2);
            a1 = mix (a.a1, b.a1); a2 = mix (a.a2, b.a2);
        }

        float process (float x, int ch)
        {
            switch (type)
            {
                case FilterType::fastLP:
                {
                    auto& s = st[ch];
                    const auto y = s.a + f * (x - s.a) + r * (s.a - s.b);
                    s.b = s.a;
                    s.a = y;
                    return (float) y;
                }
                case FilterType::svfLP:   return (float) svf (st[ch], x, q);
                case FilterType::svfLPx2: return (float) svf (st[ch + 2], svf (st[ch], x, q), q2);
                case FilterType::lpx2:
                {
                    const auto y = biquad (st[ch + 2], biquad (st[ch], x)) * compNow;
                    if (ch == 1)   // once per sample: glide to the compensation over the first tick
                    {
                        if (compLeft > 0) { compNow += (comp - compNow) / compLeft; --compLeft; }
                        else              compNow = comp;
                    }
                    return (float) y;
                }
                default:                  return (float) biquad (st[ch], x);
            }
        }

    private:
        struct State { double a = 0, b = 0, c = 0, d = 0; };

        double svf (State& s, double x, double damping) const
        {
            s.a += f * s.b;                       // low
            const auto hp = x - s.a - damping * s.b;
            s.b += f * hp;                        // band
            return s.a;
        }

        double biquad (State& s, double x) const  // direct form I: how FL's filter reacts when the coefficients move (measured)
        {
            const auto y = b0 * x + b1 * s.a + b2 * s.b - a1 * s.c - a2 * s.d;
            s.b = s.a; s.a = x;
            s.d = s.c; s.c = y;
            return y;
        }

        FilterType type = FilterType::fastLP;
        double comp = 1, compNow = 1;   // LPx2 level compensation: target, current
        int compLeft = 0;                // samples left in the first-tick glide
        double f = 1, r = 0, q = 1, q2 = 1, b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        State st[4];   // [0..1] first stage L/R, [2..3] second stage L/R
    };
}
