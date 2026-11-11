#include "Filter.h"

#include <cstddef>

namespace cs::dsp
{

// Curves measured by rendering FL Studio's Sampler and fitting its output
// (nulltest/, MEASUREMENTS.md).

namespace
{
    template <size_t N>
    double interpolateLog (const double (&xs)[N], const double (&ys)[N], double x)
    {
        if (x <= xs[0])
            return ys[0] * std::pow (ys[1] / ys[0], (x - xs[0]) / (xs[1] - xs[0]));
        for (size_t i = 0; i + 1 < N; ++i)
            if (x <= xs[i + 1])
                return ys[i] * std::pow (ys[i + 1] / ys[i], (x - xs[i]) / (xs[i + 1] - xs[i]));
        return ys[N - 1];
    }
}

double flOnePoleF (double u)
{
    // Below Mod X 1/64 FL goes linearly to 0 (silence at 0, measured).
    const auto& t = tuning();
    const double knee = t.onePoleKnee;
    const auto atKnee = (std::pow (t.onePoleBase, knee) - 1.0) / (t.onePoleBase - 1.0);
    if (u < knee)
        return jmax (0.0, u) / knee * atKnee;
    return (std::pow (t.onePoleBase, jmin (1.0, u)) - 1.0) / (t.onePoleBase - 1.0);
}

double flResonance (double res)
{
    // r measured in FL at raw resonance 0, 32, ..., 640 (cutoff open: y = x + r (y1 - y2),
    // measurements/ChanSampler/res_r.npy), interpolated cubically in log (1 - r). Mod Y maps 0..256 onto it.
    const auto& rv = tuning().resonanceTable;
    const auto x = jlimit (0.0, 20.0, res * 20.0);   // in units of 32 raw
    if (x <= 0.0)
        return 0.0;
    auto L = [&rv] (int k) { return std::log (1.0 - rv[k]); };
    if (x < 1.0)   // first segment: log (1 - r) is linear from 0
        return 1.0 - std::exp (L (1) * x);
    const int i = jlimit (1, 18, (int) x);
    const int i0 = jlimit (1, 17, i - 1);    // 4 points i0..i0+3, all with r > 0
    double sum = 0.0;
    for (int a = 0; a < 4; ++a)
    {
        double w = 1.0;
        for (int b = 0; b < 4; ++b)
            if (b != a)
                w *= (x - (i0 + b)) / (double) (a - b);
        sum += w * L (i0 + a);
    }
    return 1.0 - std::exp (sum);
}

double flBiquadF0 (double u)
{
    // LP/BP/HP/BS centre frequency (Hz) at Mod X raw 8, 16, ..., 240: FL's curve
    // traced tick by tick with a Mod X LFO (nulltest/cases_lfo.py l_modx_t1;
    // agrees with the earlier static points). Above that FL keeps rising
    // exponentially, past the knob's end under modulation, until the 3.0 rad
    // cap in VoiceFilter::setup (21056 Hz at 44.1 kHz, reached at Mod X 100%).
    const auto& t = tuning();
    if (u > 240.0 / 256.0)
        return t.biquadF0Hz[29] * std::exp (t.biquadF0Rise * (u - 240.0 / 256.0));
    return interpolateLog (t.biquadF0Knots, t.biquadF0Hz, u * 256.0);
}

double flLpx2Gain (double u, double res)
{
    // LPx2 = two identical LP biquads (each with the LP's Q), then a level
    // compensation that grows with resonance and shrinks with cutoff; FL glides
    // to it from unity over the note's first tick. It is linear in the stage's
    // Q times its frequency:
    // 1 - Q (a - w) b, with a and b fitted to the 8 x 8 compensation measured on
    // noise against FL at Mod X 32..256 x Mod Y 0..256 (within 0.001 of every
    // measured value; w in radians per sample at 44.1 kHz, capped as the filter).
    const auto& t = tuning();
    const auto w = jmin (t.biquadMaxRadians, MathConstants<double>::twoPi * flBiquadF0 (u) / 44100.0);
    return 1.0 - flBiquadQ (res) * (t.lpx2CompA - w) * t.lpx2CompB;
}

double flBiquadQ (double res)
{
    // Q at Mod Y raw 0, 8, ..., 224 (traced with a Mod Y LFO, l_mody_t1), 256 (static).
    return interpolateLog (tuning().biquadQKnots, tuning().biquadQ, res * 256.0);
}
}
