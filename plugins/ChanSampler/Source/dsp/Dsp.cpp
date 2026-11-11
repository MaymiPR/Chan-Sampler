#include "Dsp.h"

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
    constexpr double knee = 1.0 / 64.0;
    const auto atKnee = (std::pow (601.0, knee) - 1.0) / 600.0;
    if (u < knee)
        return jmax (0.0, u) / knee * atKnee;
    return (std::pow (601.0, jmin (1.0, u)) - 1.0) / 600.0;
}

double flResonance (double res)
{
    // r measured in FL at raw resonance 0, 32, ..., 640 (cutoff open: y = x + r (y1 - y2),
    // measurements/ChanSampler/res_r.npy), interpolated cubically in log (1 - r). Mod Y maps 0..256 onto it.
    static const double rv[] = { 0.0, 0.1815353218, 0.3301693593, 0.4518676647, 0.5515086796, 0.6330945280,
                                 0.6998893266, 0.7545816900, 0.7993627980, 0.8360268703, 0.8660461431,
                                 0.8906255328, 0.9107490105, 0.9272267638, 0.9407179614, 0.9517637502,
                                 0.9608078304, 0.9682128425, 0.9742758112, 0.9792400659, 0.9833044645 };
    const auto x = jlimit (0.0, 20.0, res * 20.0);   // in units of 32 raw
    if (x <= 0.0)
        return 0.0;
    auto L = [] (int k) { return std::log (1.0 - rv[k]); };
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
    static const double mx[] = { 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 136, 144, 152, 160, 168, 176, 184, 192, 200, 208, 216, 224, 232, 240 };
    static const double hz[] = { 202.3, 260.4, 328.2, 406.2, 495.2, 596.3, 710.8, 840.3, 986.5, 1151.6, 1338.3, 1549.4, 1788.1, 2058.1, 2363.6, 2709.2, 3100.0, 3542.0, 4041.8, 4606.6, 5245.0, 5966.5, 6782.1, 7704.2, 8747.1, 9927.0, 11262.1, 12772.8, 14481.6, 16412.3 };
    if (u > 240.0 / 256.0)
        return 16412.3 * std::exp (3.99 * (u - 240.0 / 256.0));
    return interpolateLog (mx, hz, u * 256.0);
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
    const auto w = jmin (3.0, MathConstants<double>::twoPi * flBiquadF0 (u) / 44100.0);
    return 1.0 - flBiquadQ (res) * (3.19702 - w) * 0.046912;
}

double flBiquadQ (double res)
{
    // Q at Mod Y raw 0, 8, ..., 224 (traced with a Mod Y LFO, l_mody_t1), 256 (static).
    static const double my[] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 136, 144, 152, 160, 168, 176, 184, 192, 200, 208, 216, 224, 256 };
    static const double qs[] = { 0.1000, 0.1319, 0.1660, 0.2035, 0.2440, 0.2876, 0.3345, 0.3850, 0.4393, 0.4980, 0.5615, 0.6302, 0.7046, 0.7851, 0.8722, 0.9662, 1.0676, 1.1769, 1.2946, 1.4213, 1.5580, 1.7056, 1.8651, 2.0380, 2.2253, 2.4283, 2.6475, 2.8827, 3.1322, 4.3697 };
    return interpolateLog (my, qs, res * 256.0);
}

// FL's knob laws (moved here from the parameter layer: the DSP reads them).

float flEnvKnobForSeconds (float seconds)
{
    float lo = 0.0f, hi = 1.0f;
    for (int i = 0; i < 40; ++i)
    {
        const auto mid = 0.5f * (lo + hi);
        (flEnvSeconds (mid) < seconds ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

// Arpeggio time (measured in FL): position = (raw - 450) / 997, raw 1447 is
// Hold. FL plays a whole number of ticks (24 per step): an exponential of
// the integer raw value rounded to half ticks, then halved downwards (the
// two values fitted to
// the 55 measured raw values, measurements/ChanSampler/arp_ticks_dense.txt and the 13
// where the earlier fit was a tick out, measure_arp_period.py).
double flArpTicksForRaw (double raw)
{
    const auto halfTicks = std::nearbyint (1.84558 * (std::pow (24.99798, raw / 1000.0) - 1.0));
    return jmax (1.0, std::floor (halfTicks / 2.0));
}
static double arpRawFromKnob (float knob) { return std::round (450.0 + 997.0 * (double) jlimit (0.0f, 1.0f, knob)); }

float flEnvSeconds (float knob)
{
    // FL's own times, measured at six tempos (nulltest/probe_env_time.py,
    // env_times.txt): FL rounds every stage to whole ticks (dsp::Envelope), so
    // a time read at one tempo is only known to a tick; the tempos together
    // pin it down. Between the points the time follows log (t - c) linearly,
    // c from the fit t = 0.0646133 e^(6.8996 p) - 0.0636541 (p = knob), which
    // also gives the times below raw 1000.
    static const float raws[] = { 1000, 1500, 2500, 5000, 7500, 10000, 12500, 15000, 17500, 20000, 22500, 25000, 27500, 30000,
                                  32500, 35000, 37500, 40000, 42500, 45000, 47500, 50000, 52500, 55000, 57500, 60000, 62500, 65536 };
    static const float ms[] = { 7.812f, 11.088f, 19.085f, 44.334f, 76.798f, 119.861f, 175.285f, 247.253f, 341.0f, 463.086f, 622.265f,
                                828.828f, 1098.203f, 1448.203f, 1904.46f, 2498.024f, 3270.312f, 4276.015f, 5584.774f, 7288.515f,
                                9504.701f, 12391.015f, 16148.047f, 21036.328f, 27396.947f, 35677.991f, 46458.69f, 63999.14f };
    constexpr double c = -63.6541;   // ms
    // Measured: the knob's minimum (raw 100) is no time at all.
    if (knob <= 0.0f)
        return 0.0f;
    const auto raw = jlimit (100.0f, 65536.0f, 100.0f + knob * 65436.0f);
    if (raw < raws[0])
        return (float) jmax (0.0, 0.0646133 * std::exp (6.8996 * (double) knob) - 0.0636541);
    int i = 0;
    while (i < 26 && raw > raws[i + 1])
        ++i;
    const auto t = (raw - raws[i]) / (raws[i + 1] - raws[i]);
    const auto l0 = std::log (ms[i] - c), l1 = std::log (ms[i + 1] - c);
    return (float) (0.001 * (std::exp (l0 + (l1 - l0) * t) + c));
}

double flLfoPeriodT130 (float speedKnob)
{
    return 13.5755 * std::pow (982.5, (double) jlimit (0.0f, 1.0f, speedKnob)) - 13.2745;
}

double flLfoDelayT130 (float delayKnob)
{
    // The speed law on the predelay's raw value (100 .. 65536).
    const auto raw = 100.0 + jlimit (0.0f, 1.0f, delayKnob) * 65436.0;
    return jmax (0.0, 13.5755 * std::pow (982.5, (raw - 200.0) / 65336.0) - 13.2745);
}

int flSlideTicks (float knob)
{
    // Glide length in ticks, measured in FL renders at 17 raw values
    // (nulltest/cases_untested.py): raw 0 -> 1, 500 -> 4, 820 -> 13,
    // 1000 -> 23, 1660 -> 193 (raw = knob * 1660). FL shows one tick less.
    const auto raw = jlimit (0.0, 1660.0, (double) knob * 1660.0);
    return jmax (1, (int) std::lround (0.92049 * std::pow (209.414, raw / 1660.0) - 0.15));
}

double flSlideSteps (float knob)
{
    // What FL shows (steps:ticks): 0:12 at raw 820, 0:22 at 1000, 8:00 at 1660.
    return (flSlideTicks (knob) - 1) / 24.0;
}

double flArpSteps (float knob)
{
    const auto raw = arpRawFromKnob (knob);
    if (raw >= 1447.0)
        return 0.0;                                   // Hold
    return flArpTicksForRaw (raw) / 24.0;
}

float flVolumeGain (float x)
{
    // FL's volume law: (11^x - 1) / 10, x = raw / 12800 (100% = unity, 200% = x12).
    return (std::pow (11.0f, x) - 1.0f) / 10.0f;
}

float outputLevel (float position)
{
    return flVolumeGain (position) * (0.5f / flVolumeGain (defaultOutputPosition));
}

float flTensionToK (float tension)
{
    // Exponents fitted to FL renders at raw tension -128, -101 (FL's default
    // release tension), -96, -64, ..., 128.
    static const float t[] = { -128.0f, -101.0f, -96.0f, -64.0f, -32.0f, 0.0f, 32.0f, 64.0f, 96.0f, 128.0f };
    static const float k[] = { 6.19f, 4.62f, 4.15f, 2.13f, 0.56f, 0.0f, -0.58f, -2.18f, -4.29f, -6.45f };
    const auto raw = jlimit (-128.0f, 128.0f, tension * 128.0f);
    int i = 0;
    while (i < 8 && raw > t[i + 1])
        ++i;
    const auto f = (raw - t[i]) / (t[i + 1] - t[i]);
    return k[i] + (k[i + 1] - k[i]) * f;
}
}
