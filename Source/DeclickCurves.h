#pragma once

#include <algorithm>
#include <cmath>

// Declick fade curves measured in FL (constant, noise and sine samples):
// Transient (no bleeding)'s out-fade (at the sample end and at cuts) and the
// Crossfade mode's in- and out-fades. Lengths are at 44.1 kHz and keep their
// length in time at other rates. See nulltest/declick_cases.py.
namespace declick
{
    // Transient (no bleeding) out-fade: not a gain fade but a one-pole
    // high-pass (the input minus a one-pole low-pass, state starting at 0)
    // whose cutoff sweeps up exponentially, so a held value decays smoothly
    // while the audio's highs ring on; then a closing gain silences it by 440
    // samples. A constant decays as exp (-L (n)), L (n) = c (e^(b n) - 1).
    // Measured at 44.1 kHz with a constant, noise and a sine.
    constexpr double transientB = 0.0228, transientC = 0.002015;
    constexpr int transientOutLength = 440;
    constexpr int transientGainStart = 330, transientGainStep = 5;
    inline constexpr float transientGainTable[] = {
        0.9991f, 0.9958f, 0.9958f, 0.9958f, 0.9958f, 0.9927f, 0.9904f, 0.9890f, 0.9787f, 0.9670f, 0.9652f, 0.9652f,
        0.9621f, 0.9477f, 0.8931f, 0.8169f, 0.6981f, 0.5102f, 0.3225f, 0.1524f, 0.0978f, 0.0098f, 0.0f
    };

    // x: samples into the fade at 44.1 kHz (fractional at other rates).
    inline double transientL (double x) { return x <= 0 ? 0.0 : transientC * (std::exp (transientB * x) - 1.0); }

    // The low-pass coefficient for the sample from x0 to x1 (44.1 kHz samples).
    inline float transientCoefficient (double x0, double x1)
    {
        return (float) (1.0 - std::exp (-std::min (50.0, transientL (x1) - transientL (x0))));
    }

    inline float transientGain (double x)
    {
        if (x < transientGainStart)
            return 1.0f;
        constexpr int last = (int) (sizeof (transientGainTable) / sizeof (float)) - 1;
        const auto t = (x - transientGainStart) / transientGainStep;
        const int i = (int) t;
        if (i >= last)
            return 0.0f;
        const auto f = (float) (t - i);
        return transientGainTable[i] + f * (transientGainTable[i + 1] - transientGainTable[i]);
    }

    // Crossfade fades: degree-10 polynomials in u = n / length (max error
    // 0.6 / 3192 against the measurement).
    constexpr int crossfadeInLength = 8750, crossfadeOutLength = 8781;
    inline constexpr double crossfadeInPoly[] = { -6.876493618e-06, 0.0108449024, 5.072820867, 0.6442403826, -14.64723224, -20.27575314, 125.7882847, -202.5588682, 163.4490074, -68.31700329, 11.83346862 };
    inline constexpr double crossfadeOutPoly[] = { 0.9999750188, -0.005822820603, -0.751950526, -1.28526727, 6.712290069, -31.06397872, 79.25906093, -129.9780921, 133.2606847, -73.45669163, 16.30997087 };

    inline float poly (const double (&c)[11], double u)
    {
        double y = 0;
        for (int k = 10; k >= 0; --k)
            y = y * u + c[k];
        return (float) std::clamp (y, 0.0, 1.0);
    }

    inline float crossfadeIn (double u)  { return u >= 1.0 ? 1.0f : poly (crossfadeInPoly, std::max (0.0, u)); }
    inline float crossfadeOut (double u) { return u >= 1.0 ? 0.0f : poly (crossfadeOutPoly, std::max (0.0, u)); }
}
