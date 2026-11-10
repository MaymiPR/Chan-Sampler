#pragma once
// exp, log, sin and the functions built from them, computed to the same last bit on every platform. The C library's own
// versions differ by a unit in the last place between MSVC, glibc and Apple's libm, and a DSP that decides between
// near-equal values (a release law's charge compared with its source, a threshold crossing) turns that into a
// different output on one platform: a render golden that passes on Windows then fails on Linux and macOS. These use
// only +, -, *, / and the exact frexp, ldexp and floor, so they agree wherever IEEE-754 doubles round to nearest and
// the compiler does not fuse a multiply and an add (the root CMakeLists.txt turns that off for GCC and Clang).
// Accuracy: within about two units in the last place of the true value. Use them in Source/dsp instead of <cmath>
// for every value that feeds a decision; <cmath> stays fine for a value that is only ever added or multiplied on.
// Plain C++ without JUCE, like the rest of Source/dsp.
#include <cmath>
#include <limits>

namespace chansampler::math
{
inline constexpr double ln2Hi = 6.93147180369123816490e-01;   // ln 2 split so that k * ln2Hi is exact for |k| < 2^20
inline constexpr double ln2Lo = 1.90821492927058770002e-10;
inline constexpr double invLn2 = 1.44269504088896338700e+00;
inline constexpr double ln10 = 2.30258509299404568402e+00;
inline constexpr double invLn10 = 4.34294481903251827651e-01;

// e^x: x = k ln 2 + r with |r| <= ln 2 / 2, e^r by its Taylor series to r^13 (the rest is below 1e-17), then 2^k
inline double exp (double x)
{
    if (std::isnan (x)) return x;
    if (x > 709.78) return std::numeric_limits<double>::infinity();
    if (x < -745.2) return 0.0;
    const double k = std::floor (x * invLn2 + 0.5);
    const double r = (x - k * ln2Hi) - k * ln2Lo;
    double p = 1.0 / 6227020800.0;                     // 1 / 13!
    p = 1.0 / 479001600.0 + r * p;
    p = 1.0 / 39916800.0 + r * p;
    p = 1.0 / 3628800.0 + r * p;
    p = 1.0 / 362880.0 + r * p;
    p = 1.0 / 40320.0 + r * p;
    p = 1.0 / 5040.0 + r * p;
    p = 1.0 / 720.0 + r * p;
    p = 1.0 / 120.0 + r * p;
    p = 1.0 / 24.0 + r * p;
    p = 1.0 / 6.0 + r * p;
    p = 0.5 + r * p;
    p = 1.0 + r * p;
    p = 1.0 + r * p;
    return std::ldexp (p, (int) k);
}

// ln x: x = m 2^e with m in [sqrt(1/2), sqrt(2)), ln m = 2 atanh (s), s = (m - 1) / (m + 1), by its series to s^23
inline double log (double x)
{
    if (std::isnan (x) || x < 0.0) return std::numeric_limits<double>::quiet_NaN();
    if (x == 0.0) return -std::numeric_limits<double>::infinity();
    if (std::isinf (x)) return x;
    int e = 0;
    double m = std::frexp (x, &e);
    if (m < 0.70710678118654752440) { m *= 2.0; --e; }
    const double f = m - 1.0;
    const double s = f / (2.0 + f), z = s * s;
    double p = 1.0 / 23.0;
    p = 1.0 / 21.0 + z * p;
    p = 1.0 / 19.0 + z * p;
    p = 1.0 / 17.0 + z * p;
    p = 1.0 / 15.0 + z * p;
    p = 1.0 / 13.0 + z * p;
    p = 1.0 / 11.0 + z * p;
    p = 1.0 / 9.0 + z * p;
    p = 1.0 / 7.0 + z * p;
    p = 1.0 / 5.0 + z * p;
    p = 1.0 / 3.0 + z * p;
    const double lm = 2.0 * s + 2.0 * s * (z * p);
    return (double) e * ln2Hi + ((double) e * ln2Lo + lm);
}

inline double log10 (double x) { return log (x) * invLn10; }
inline double pow10 (double y) { return exp (y * ln10); }                  // 10^y
inline double dbToGain (double db) { return pow10 (db / 20.0); }
inline double gainToDb (double g) { return 20.0 * log10 (g); }
// a^b for a > 0 (the only case a DSP needs; a == 0 gives 0 for b > 0, the library's answer otherwise)
inline double pow (double a, double b) { return a == 0.0 ? (b > 0.0 ? 0.0 : std::pow (a, b)) : exp (b * log (a)); }
inline double tanh (double x)
{
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    const double e = exp (2.0 * x);
    return (e - 1.0) / (e + 1.0);
}
// the one-pole coefficient of a time constant in ms at a sample rate: the same on every platform
inline double onePoleCoef (double ms, double fs) { return exp (-1.0 / (ms * 0.001 * fs)); }

inline constexpr double pi = 3.14159265358979323846;
// sin x: x reduced to [-pi, pi] (exact floor), then its Taylor series to x^27 (the rest is below 1e-16 on that range)
inline double sin (double x)
{
    if (! std::isfinite (x)) return std::numeric_limits<double>::quiet_NaN();
    x -= 2.0 * pi * std::floor ((x + pi) / (2.0 * pi));
    const double z = x * x;
    double p = 1.0;
    for (int k = 27; k > 1; k -= 2)
        p = 1.0 - z * p / ((double) k * (double) (k - 1));
    return x * p;
}
inline double cos (double x) { return sin (x + 0.5 * pi); }
// the frequency of a MIDI key (69 = 440 Hz), equal temperament
inline double keyToHz (double key) { return 440.0 * exp ((key - 69.0) * (ln2Hi + ln2Lo) / 12.0); }

inline float expf (float x) { return (float) exp ((double) x); }
inline float logf (float x) { return (float) log ((double) x); }
inline float tanhf (float x) { return (float) tanh ((double) x); }
inline float dbToGainf (float db) { return (float) dbToGain ((double) db); }
}
