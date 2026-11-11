#pragma once

// FL's channel filter (Fast LP, RBJ biquads, state-variable, the x2 cascades) and its parameter mappings.

#include <cmath>

#include "Modes.h"
#include "Numeric.h"
#include "Tuning.h"

namespace cs::dsp
{
// FL's channel filter, measured type by type (nulltest/, MEASUREMENTS.md).
// u = Mod X (cutoff, 0..1), res = Mod Y (0..1).
//  Fast LP : y = y1 + f (x - y1) + r (y1 - y2), f = (601^u - 1) / 600,
//            r from the measured resonance curve (same as the precomputed filter).
//  LP/BP/HP/BS : RBJ biquads; f0 and Q interpolated from measured tables.
//  SVF LP  : Chamberlin state variable filter, f = (26^u - 1) / 25, q = 1 - 0.8 res.
//  LPx2 / SVF LPx2 : two stages in series (SVF LPx2 nulls exactly; LPx2, with its
//            level compensation, at -44 to -63 dB; slew-limited from unity at a note's start).
double flOnePoleF (double u);
double flResonance (double res);
double flBiquadF0 (double u);   // Hz
double flBiquadQ (double res);
double flLpx2Gain (double u, double res);   // LPx2's level compensation (measured)

class VoiceFilter
{
public:
    // A note starts at unity and glides to the LPx2 compensation tick by
    // tick, linearly within each, moving at most maxStepPerTick per tick
    // (FL's envelope slew: one tick at maximum resonance isn't enough).
    void startCompensation (int tickSamples, double maxStepPerTick)
    {
        compNow = 1.0;
        compTick = jmax (1, tickSamples);
        compStep = maxStepPerTick;
        compLeft = 0;
        gliding = true;
        compStarted = false;
    }

    void reset()
    {
        for (auto& s : st) s = {};
    }

    void setup (FilterType t, double u, double res, double sampleRate)
    {
        type = t;
        // Modulation can push the biquads' cutoff past the knob's end (measured).
        const bool biquad = t != FilterType::fastLP && t != FilterType::svfLP && t != FilterType::svfLPx2;
        u = jlimit (0.0, biquad ? 2.0 : 1.0, u);
        res = jlimit (0.0, 1.0, res);
        const auto& tn = tuning();
        switch (type)
        {
            case FilterType::fastLP:
                f = flOnePoleF (u);
                r = flResonance (res);
                break;
            case FilterType::svfLP:
            case FilterType::svfLPx2:
                f = (std::pow (tn.svfBase, u) - 1.0) / (tn.svfBase - 1.0);
                q = 1.0 - tn.svfDamping * res;
                q2 = 1.0 - tn.svfDamping2 * res;   // SVF LPx2's second stage (measured: q1 1 - 0.8 res, q2 1 - 0.6 res)
                break;
            default:
            {
                const auto w = jmin (tn.biquadMaxRadians, MathConstants<double>::twoPi * flBiquadF0 (u) / sampleRate);   // FL caps at 3.0 rad (measured)
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
                if (ch == 1)   // once per sample
                {
                    if (gliding && compLeft == 0)   // a tick starts: its target
                    {
                        if (compStarted && compTo == comp)
                            gliding = false;
                        else
                        {
                            compTo = std::abs (comp - compNow) <= compStep ? comp : compNow + (comp > compNow ? compStep : -compStep);
                            compLeft = compTick;
                            compStarted = true;
                        }
                    }
                    if (gliding) { compNow += (compTo - compNow) / compLeft; --compLeft; }
                    else         compNow = comp;
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
    double comp = 1, compNow = 1, compTo = 1, compStep = 1;   // LPx2 level compensation: target, current, this tick's end, slew per tick
    int compLeft = 0, compTick = 1;                           // samples left in this tick, tick length
    bool gliding = false, compStarted = false;
    double f = 1, r = 0, q = 1, q2 = 1, b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    State st[4];   // [0..1] first stage L/R, [2..3] second stage L/R
};
}
