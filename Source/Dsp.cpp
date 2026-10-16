#include "Dsp.h"

// Curves measured by rendering FL Studio's Sampler and fitting its output
// (nulltest/, docs/EVIDENCE.md).

namespace dsp
{
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
    constexpr double knee = 1.0 / 64.0;
    const auto atKnee = (std::pow (601.0, knee) - 1.0) / 600.0;
    if (u < knee)
        return juce::jmax (0.0, u) / knee * atKnee;
    return (std::pow (601.0, juce::jmin (1.0, u)) - 1.0) / 600.0;
}

double flResonance (double res)
{
    // r measured in FL at raw resonance 0, 32, ..., 640 (cutoff open: y = x + r (y1 - y2),
    // nulltest/res_r.npy), interpolated cubically in log (1 - r). Mod Y maps 0..256 onto it.
    static const double rv[] = { 0.0, 0.1815353218, 0.3301693593, 0.4518676647, 0.5515086796, 0.6330945280,
                                 0.6998893266, 0.7545816900, 0.7993627980, 0.8360268703, 0.8660461431,
                                 0.8906255328, 0.9107490105, 0.9272267638, 0.9407179614, 0.9517637502,
                                 0.9608078304, 0.9682128425, 0.9742758112, 0.9792400659, 0.9833044645 };
    const auto x = juce::jlimit (0.0, 20.0, res * 20.0);   // in units of 32 raw
    if (x <= 0.0)
        return 0.0;
    auto L = [] (int k) { return std::log (1.0 - rv[k]); };
    if (x < 1.0)   // first segment: log (1 - r) is linear from 0
        return 1.0 - std::exp (L (1) * x);
    const int i = juce::jlimit (1, 18, (int) x);
    const int i0 = juce::jlimit (1, 17, i - 1);    // 4 points i0..i0+3, all with r > 0
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

double flBiquadF0 (double u, double)
{
    // LP/BP/HP/BS centre frequency (Hz) at Mod X raw 8, 16, ..., 240: FL's curve
    // traced tick by tick with a Mod X LFO (nulltest/lfo_cases.py l_modx_t1;
    // agrees with the earlier static points). Above that FL keeps rising
    // exponentially, past the knob's end under modulation, until the 3.0 rad
    // cap in VoiceFilter::setup (21056 Hz at 44.1 kHz, reached at Mod X 100%).
    static const double mx[] = { 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 136, 144, 152, 160, 168, 176, 184, 192, 200, 208, 216, 224, 232, 240 };
    static const double hz[] = { 202.3, 260.4, 328.2, 406.2, 495.2, 596.3, 710.8, 840.3, 986.5, 1151.6, 1338.3, 1549.4, 1788.1, 2058.1, 2363.6, 2709.2, 3100.0, 3542.0, 4041.8, 4606.6, 5245.0, 5966.5, 6782.1, 7704.2, 8747.1, 9927.0, 11262.1, 12772.8, 14481.6, 16412.3 };
    if (u > 240.0 / 256.0)
        return 16412.3 * std::exp (3.99 * (u - 240.0 / 256.0));
    return interpolateLog (mx, hz, u * 256.0);
}

double flBiquadQ (double res)
{
    // Q at Mod Y raw 0, 8, ..., 224 (traced with a Mod Y LFO, l_mody_t1), 256 (static).
    static const double my[] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 136, 144, 152, 160, 168, 176, 184, 192, 200, 208, 216, 224, 256 };
    static const double qs[] = { 0.1000, 0.1319, 0.1660, 0.2035, 0.2440, 0.2876, 0.3345, 0.3850, 0.4393, 0.4980, 0.5615, 0.6302, 0.7046, 0.7851, 0.8722, 0.9662, 1.0676, 1.1769, 1.2946, 1.4213, 1.5580, 1.7056, 1.8651, 2.0380, 2.2253, 2.4283, 2.6475, 2.8827, 3.1322, 4.3697 };
    return interpolateLog (my, qs, res * 256.0);
}
}
