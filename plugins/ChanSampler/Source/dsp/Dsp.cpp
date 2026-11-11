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
    const auto halfTicks = std::nearbyint (tuning().arpTicksA * (std::pow (tuning().arpTicksBase, raw / 1000.0) - 1.0));
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
    const auto& tn = tuning();
    const auto& raws = tn.envTimeKnots;
    const auto& ms = tn.envTimeMs;
    const double c = tn.envTimeOffsetMs;   // ms
    // Measured: the knob's minimum (raw 100) is no time at all.
    if (knob <= 0.0f)
        return 0.0f;
    const auto raw = jlimit (100.0f, 65536.0f, 100.0f + knob * 65436.0f);
    if (raw < raws[0])
        return (float) jmax (0.0, tn.envTimeLowA * std::exp (tn.envTimeLowB * (double) knob) - tn.envTimeLowC);
    int i = 0;
    while (i < 26 && raw > raws[i + 1])
        ++i;
    const auto t = (raw - raws[i]) / (raws[i + 1] - raws[i]);
    const auto l0 = std::log (ms[i] - c), l1 = std::log (ms[i + 1] - c);
    return (float) (0.001 * (std::exp (l0 + (l1 - l0) * t) + c));
}

double flLfoPeriodT130 (float speedKnob)
{
    const auto& t = tuning();
    return t.lfoPeriodA * std::pow (t.lfoPeriodBase, (double) jlimit (0.0f, 1.0f, speedKnob)) - t.lfoPeriodC;
}

double flLfoDelayT130 (float delayKnob)
{
    // The speed law on the predelay's raw value (100 .. 65536).
    const auto raw = 100.0 + jlimit (0.0f, 1.0f, delayKnob) * 65436.0;
    const auto& t = tuning();
    return jmax (0.0, t.lfoPeriodA * std::pow (t.lfoPeriodBase, (raw - 200.0) / 65336.0) - t.lfoPeriodC);
}

int flSlideTicks (float knob)
{
    // Glide length in ticks, measured in FL renders at 17 raw values
    // (nulltest/cases_untested.py): raw 0 -> 1, 500 -> 4, 820 -> 13,
    // 1000 -> 23, 1660 -> 193 (raw = knob * 1660). FL shows one tick less.
    const auto raw = jlimit (0.0, 1660.0, (double) knob * 1660.0);
    const auto& t = tuning();
    return jmax (1, (int) std::lround (t.slideTicksA * std::pow (t.slideTicksBase, raw / 1660.0) - t.slideTicksC));
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
    const auto base = tuning().volumeLawBase;
    return (std::pow (base, x) - 1.0f) / (base - 1.0f);
}

float outputLevel (float position)
{
    return flVolumeGain (position) * (0.5f / flVolumeGain (defaultOutputPosition));
}

float flTensionToK (float tension)
{
    // Exponents fitted to FL renders at raw tension -128, -101 (FL's default
    // release tension), -96, -64, ..., 128.
    const auto& t = tuning().tensionKnots;
    const auto& k = tuning().tensionK;
    const auto raw = jlimit (-128.0f, 128.0f, tension * 128.0f);
    int i = 0;
    while (i < 8 && raw > t[i + 1])
        ++i;
    const auto f = (raw - t[i]) / (t[i + 1] - t[i]);
    return k[i] + (k[i + 1] - k[i]) * f;
}
}
