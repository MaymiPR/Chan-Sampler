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

    // DAHDSR envelope from the INS tab, advanced at control rate. Measured
    // FL behaviour: decay is rate-based (its time is a full 1 -> 0 travel),
    // a release joins its curve at the current level, and decay/release
    // curves are the attack curve reversed. Any jump (a stage with no time)
    // is smoothed by a fixed 9.6 ms ramp (measured shape), which delay and hold start 4.76 ms
    // early so that it is centred on the nominal time.
    class Envelope
    {
    public:
        struct Params
        {
            float delay = 0, attack = 0, hold = 0, decay = 0.5f, sustain = 1, release = 0.2f;   // seconds / level
            float attackK = 0, decayK = 0, releaseK = 0;                                     // curve exponents
            // FL's jump ramps are counted in song ticks: the measured lengths are
            // for 130 BPM, and this is 130 / tempo.
            float tickScale = 1.0f;
        };

        // FL's jump ramp (measured, identical after delay, hold and release):
        // 0.5398 of the way in 4.762 ms, then linear to the end at 9.592 ms.
        // Shapes measured in FL (nulltest/jump_down.npy, jump_up.npy): progress
        // every 8 samples at 44.1 kHz over 424 samples; falls and rises differ.
        static constexpr float jumpLead = 0.004762f, jumpTotal = 424.0f / 44100.0f;
        // Falls inside the envelope (hold end -> instant decay) keep FL's
        // two-slope ramp: 0.5398 of the way at 4.76 ms, then linear.
        static float jumpShapeHold (float seconds)
        {
            constexpr float mid = 0.5398f, total = 0.009592f;   // (at 130 BPM; callers scale the time)
            return seconds < jumpLead ? mid * seconds / jumpLead
                                      : juce::jmin (1.0f, mid + (1 - mid) * (seconds - jumpLead) / (total - jumpLead));
        }

        static float jumpShape (float seconds, bool rising)
        {
            static const float down[] = { 0.0000f, 0.0207f, 0.0410f, 0.0617f, 0.0821f, 0.1028f, 0.1234f, 0.1438f, 0.1645f, 0.1848f, 0.2055f, 0.2259f, 0.2466f, 0.2669f, 0.2876f, 0.3080f, 0.3286f, 0.3490f, 0.3697f, 0.3900f, 0.4107f, 0.4314f, 0.4518f, 0.4724f, 0.4928f, 0.5135f, 0.5338f, 0.5529f, 0.5708f, 0.5896f, 0.6087f, 0.6281f, 0.6479f, 0.6679f, 0.6883f, 0.7086f, 0.7290f, 0.7491f, 0.7691f, 0.7888f, 0.8083f, 0.8274f, 0.8459f, 0.8637f, 0.8810f, 0.8976f, 0.9132f, 0.9283f, 0.9427f, 0.9558f, 0.9684f, 0.9799f, 0.9903f, 1.0000f };
            static const float up[]   = { 0.0000f, 0.0207f, 0.0410f, 0.0617f, 0.0821f, 0.1028f, 0.1231f, 0.1438f, 0.1642f, 0.1848f, 0.2052f, 0.2259f, 0.2462f, 0.2669f, 0.2873f, 0.3080f, 0.3286f, 0.3490f, 0.3697f, 0.3900f, 0.4107f, 0.4311f, 0.4518f, 0.4721f, 0.4928f, 0.5132f, 0.5338f, 0.5526f, 0.5699f, 0.5871f, 0.6043f, 0.6216f, 0.6388f, 0.6560f, 0.6729f, 0.6902f, 0.7074f, 0.7246f, 0.7419f, 0.7591f, 0.7763f, 0.7935f, 0.8108f, 0.8280f, 0.8452f, 0.8622f, 0.8794f, 0.8966f, 0.9138f, 0.9311f, 0.9483f, 0.9655f, 0.9828f, 1.0000f };
            const float* t = rising ? up : down;
            constexpr int count = (int) (sizeof (down) / sizeof (down[0]));
            const auto x = juce::jlimit (0.0f, 424.0f, seconds * 44100.0f) / 8.0f;
            const int i = juce::jmin (count - 2, (int) x);
            const auto f = juce::jmin (1.0f, x - (float) i);
            // the last entry sits at sample 424, 8 samples apart except the final step
            return t[i] + (t[i + 1] - t[i]) * f;
        }

        void noteOn()  { stage = Stage::delay; time = 0; value = out = 0; started = false; jumpTime = -1; }
        void noteOff() { if (stage != Stage::idle && stage != Stage::release) { releaseFrom = out; stage = Stage::release; time = 0; } }
        void reset()   { stage = Stage::idle; value = out = 0; jumpTime = -1; }
        bool isIdle() const noexcept     { return stage == Stage::idle && jumpTime < 0; }
        bool isReleased() const noexcept { return stage == Stage::release || stage == Stage::idle; }

        float advance (const Params& p, float seconds)
        {
            const auto previous = value;
            compute (p, seconds);

            // A change faster than the jump ramp starts a new ramp from the
            // level heard so far (never at the very start of a note).
            const auto sc = juce::jmax (1.0e-3f, p.tickScale);
            if (started && std::abs (value - previous) > seconds / (jumpTotal * sc) * 1.05f + 1.0e-4f)
            {
                jumpFrom = out;
                jumpTime = 0;
            }
            started = true;

            if (jumpTime >= 0)
            {
                jumpTime += seconds;
                if (jumpTime >= jumpTotal * sc)
                    jumpTime = -1;
                else
                {
                    const auto shape = (value <= jumpFrom && stage != Stage::release && stage != Stage::idle)
                                           ? jumpShapeHold (jumpTime / sc) : jumpShape (jumpTime / sc, value > jumpFrom);
                    return out = jumpFrom + (value - jumpFrom) * shape;
                }
            }
            return out = value;
        }

    private:
        void compute (const Params& p, float seconds)
        {
            time += (double) seconds;
            const auto lead = jumpLead * p.tickScale;
            const auto delay = (double) juce::jmax (0.0f, p.delay - lead);
            const auto hold  = (double) juce::jmax (0.0f, p.hold - lead);
            for (;;)
            {
                switch (stage)
                {
                    case Stage::idle:    value = 0; return;
                    case Stage::delay:   if (time >= delay)    { time -= delay;    stage = Stage::attack; continue; }
                                         value = 0; return;
                    case Stage::attack:  if (time >= p.attack) { time -= p.attack; stage = Stage::hold;   continue; }
                                         value = tensionCurve ((float) (time / p.attack), p.attackK); return;
                    case Stage::hold:    if (time >= hold)     { time -= hold;     stage = Stage::decay;  continue; }
                                         value = 1; return;
                    case Stage::decay:
                    {
                        const auto dur = p.decay * (1 - p.sustain);
                        if (time >= dur) { stage = Stage::sustain; continue; }
                        value = p.sustain + (1 - p.sustain) * tensionCurve ((float) (1.0 - time / dur), p.decayK);
                        return;
                    }
                    case Stage::sustain: value = p.sustain; return;
                    case Stage::release:
                    {
                        // Measured in FL: the release joins its curve at the point
                        // where the curve equals the current level and runs from
                        // there (from sustain 0.39 at tension -101 it takes 79% of
                        // the release time).
                        const auto x0 = tensionCurveInverse (releaseFrom, p.releaseK);
                        if (time >= (double) (x0 * p.release)) { stage = Stage::idle; continue; }
                        value = tensionCurve ((float) (x0 - time / p.release), p.releaseK);
                        return;
                    }
                }
            }
        }

        enum class Stage { idle, delay, attack, hold, decay, sustain, release };
        Stage stage = Stage::idle;
        double time = 0;                   // double: the volume envelope steps per sample
        float value = 0, out = 0, releaseFrom = 0;
        float jumpFrom = 0, jumpTime = -1;
        bool started = false;
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
