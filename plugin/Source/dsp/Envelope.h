#pragma once

// FL's envelope on its tick grid, and the curves it is drawn with.

#include <cmath>

#include "KnobLaws.h"
#include "Numeric.h"
#include "Tuning.h"

namespace cs::dsp
{
// Curved 0..1 ramp (e^(kx) - 1) / (e^k - 1): k = 0 is linear, k > 0
// starts slow. FL's envelope tension maps to k via flTensionToK().
inline float tensionCurve (float x, float k)
{
    x = jlimit (0.0f, 1.0f, x);
    if (std::abs (k) < 1.0e-4f)
        return x;
    return (std::exp (k * x) - 1.0f) / (std::exp (k) - 1.0f);
}

// Inverse of tensionCurve: the x where the curve reaches y.
inline float tensionCurveInverse (float y, float k)
{
    y = jlimit (0.0f, 1.0f, y);
    if (std::abs (k) < 1.0e-4f)
        return y;
    return std::log1p (y * (std::exp (k) - 1.0f)) / k;
}

inline float sCurve (float x)
{
    return 0.5f - 0.5f * std::cos (MathConstants<float>::pi * jlimit (0.0f, 1.0f, x));
}

// DAHDSR envelope from the INS tab, as FL works it out (measured:
// nulltest/probe_env_tick.py, probe_env_time.py and
// nulltest/cases_tempo_env.py): once per song tick (96 per beat)
// from the note start, interpolated linearly in between. Every stage lasts a whole number of ticks, N = ceil (time / tick
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
        double tick = tickSeconds (130.0);                                               // song tick, seconds
    };

    // the fastest move, 0 to 1
    static double fullRangeSeconds() { return tuning().envFullRangeSamples / 44100.0; }

    void noteOn()  { stage = Stage::delay; ticks = 0; phase = 0; from = to = out = 0; segment = 0; pending = true; releaseNext = false; ended = false; }
    bool isIdle() const noexcept     { return stage == Stage::idle && ! pending && from == 0.0f && to == 0.0f; }
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
        const auto step = (float) (p.tick / fullRangeSeconds());
        return now + jlimit (-step, step, target - now);
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

    // FL steps the attack by adding 1 / N per tick in single precision and
    // ends it once the sum reaches 1: N ticks, or one more when the float
    // sum falls short (N = 12, 23, 28, 49 ... at full level for that tick).
    // Measured: right for all 26 attack lengths in the hold runs of
    // nulltest/probe_env_hold.py (double precision gets 13 wrong).
    int attackTicks (int n)
    {
        if (n != attackStepsFor)
        {
            attackStepsFor = n;
            attackSteps = 0;
            const float step = 1.0f / (float) n;
            for (float sum = 0.0f; sum < 1.0f && attackSteps < 4 * n + 4; ++attackSteps)
                sum += step;
        }
        return attackSteps;
    }

    // The level at the next tick, then one tick on. The delay and the
    // attack end are rounded from the note start, the hold's ticks are its
    // own and its last tick is where the decay starts (or, with no decay
    // time, the last tick before the fall); the decay's ticks are its own
    // (it runs at a rate down to the sustain level). Measured over 225
    // attack / hold / tempo combinations (nulltest/probe_env_hold.py,
    // with the attack's float steps): 221 exact, the rest holds within
    // 0.03 tick of a half tick, where the time table's precision decides.
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
        const int attackFull = attackEnd > delayEnd ? delayEnd + attackTicks (attackEnd - delayEnd) : delayEnd;   // FL's float steps
        const int holdEnd = attackFull + stageTicks (p.hold, p);
        if (k < delayEnd)  { stage = Stage::delay; return 0.0f; }
        if (k < attackEnd) { stage = Stage::attack; return tensionCurve ((float) (k - delayEnd) / (float) (attackEnd - delayEnd), p.attackK); }
        if (k < attackFull) { stage = Stage::attack; return 1.0f; }
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
    int attackStepsFor = -1, attackSteps = 0;   // attackTicks' cache
    double releaseTicks = 0;       // the same for the release (it can start between ticks)
    double phase = 0;              // seconds since the last tick
    double segment = 0;            // where the current line starts (a release between ticks)
    float from = 0, to = 0, out = 0, releaseFrom = 0;
    bool pending = false, releaseNext = false, ended = false;
};
}
