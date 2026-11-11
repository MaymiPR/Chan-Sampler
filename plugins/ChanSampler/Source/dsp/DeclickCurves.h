#pragma once

#include <algorithm>
#include <cmath>

#include "Tuning.h"

namespace cs::dsp
{

// Declick fade curves measured in FL (constant, noise and sine samples):
// Transient (no bleeding)'s out-fade (at the sample end and at cuts) and the
// Crossfade mode's in- and out-fades. Lengths are at 44.1 kHz and keep their
// length in time at other rates. See nulltest/cases_declick.py.
namespace declick
{
    // Transient (no bleeding) out-fade: not a gain fade but a one-pole
    // high-pass (the input minus a one-pole low-pass, state starting at 0)
    // whose cutoff sweeps up exponentially, so a held value decays smoothly
    // while the audio's highs ring on; then a closing gain silences it by 440
    // samples. A constant decays as exp (-L (n)), L (n) = c (e^(b n) - 1).
    // Measured at 44.1 kHz with a constant, noise and a sine (Tuning: declickTransient*).
    inline int transientOutLength() { return tuning().declickTransientOutLength; }

    // x: samples into the fade at 44.1 kHz (fractional at other rates).
    inline double transientL (double x)
    {
        return x <= 0 ? 0.0 : tuning().declickTransientC * (std::exp (tuning().declickTransientB * x) - 1.0);
    }

    // The low-pass coefficient for the sample from x0 to x1 (44.1 kHz samples).
    inline float transientCoefficient (double x0, double x1)
    {
        return (float) (1.0 - std::exp (-std::min (50.0, transientL (x1) - transientL (x0))));
    }

    inline float transientGain (double x)
    {
        const auto& tn = tuning();
        const auto& table = tn.declickTransientGain;
        if (x < tn.declickTransientGainStart)
            return 1.0f;
        constexpr int last = (int) (sizeof (table) / sizeof (float)) - 1;
        const auto t = (x - tn.declickTransientGainStart) / tn.declickTransientGainStep;
        const int i = (int) t;
        if (i >= last)
            return 0.0f;
        const auto f = (float) (t - i);
        return table[i] + f * (table[i + 1] - table[i]);
    }

    // Slice map's slice ends: the Transient high-pass above at half speed
    // (silent after 880 samples), starting this far before the slice's end
    // (the next slice or the sample end; FL, a drum loop at MUL x1.5 and x2).
    inline int sliceOutLead() { return tuning().declickSliceOutLead; }
    inline double sliceOutSpeed() { return tuning().declickSliceOutSpeed; }
    // Below MUL x1 the play head reaches the next slice before the slice
    // ends: then the same fade starts this far before the play head gets
    // there, and the slice fades out under the next one (FL, a drum loop at
    // x0.667).
    inline int sliceCutLead() { return tuning().declickSliceCutLead; }

    // Crossfade fades: degree-10 polynomials in u = n / length (max error
    // 0.6 / 3192 against the measurement).
    inline int crossfadeInLength() { return tuning().declickCrossfadeInLength; }
    inline int crossfadeOutLength() { return tuning().declickCrossfadeOutLength; }

    inline float poly (const double (&c)[11], double u)
    {
        double y = 0;
        for (int k = 10; k >= 0; --k)
            y = y * u + c[k];
        return (float) std::clamp (y, 0.0, 1.0);
    }

    inline float crossfadeIn (double u)  { return u >= 1.0 ? 1.0f : poly (tuning().declickCrossfadeInPoly, std::max (0.0, u)); }
    inline float crossfadeOut (double u) { return u >= 1.0 ? 0.0f : poly (tuning().declickCrossfadeOutPoly, std::max (0.0, u)); }
}
}
