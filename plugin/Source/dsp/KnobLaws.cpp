#include "KnobLaws.h"
#include "Tuning.h"

namespace cs::dsp
{

// Laws measured by rendering FL Studio's Sampler (nulltest/, MEASUREMENTS.md).

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
// the 55 measured raw values, measurements/arp_ticks_dense.txt and the 13
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
