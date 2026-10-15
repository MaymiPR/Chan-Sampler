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
    return (std::pow (601.0, juce::jlimit (1.0 / 64.0, 1.0, u)) - 1.0) / 600.0;
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
    // LP/BP/HP/BS centre frequency (Hz) at Mod X raw 16, 32, ..., 256.
    static const double mx[] = { 16, 32, 64, 96, 128, 160, 192, 224, 240, 256 };
    static const double hz[] = { 260.5, 408.2, 840.9, 1550.3, 2710.1, 4605.8, 7703.5, 12759.2, 16373.4, 20058.3 };
    return interpolateLog (mx, hz, u * 256.0);
}

double flBiquadQ (double res)
{
    // Q at Mod Y raw 0, 32, ..., 256.
    static const double my[] = { 0, 32, 64, 96, 128, 160, 192, 224, 256 };
    static const double qs[] = { 0.1, 0.2438, 0.4394, 0.7052, 1.067, 1.5581, 2.2263, 3.1347, 4.3697 };
    return interpolateLog (my, qs, res * 256.0);
}
}
