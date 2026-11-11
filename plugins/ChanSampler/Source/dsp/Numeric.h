#pragma once

// The comparisons, rounding and constants every block of the DSP uses.

#include <cstdint>
#include <cstring>

namespace cs::dsp
{
// The comparisons, rounding and constants the DSP was measured and fitted with, defined with JUCE's operation
// order so the output stays bit-identical (std::clamp tests the upper limit first; std::lround rounds halves
// away from zero where roundToInt rounds them to even).
template <typename T> constexpr T jlimit (T lo, T hi, T v) noexcept { return v < lo ? lo : (hi < v ? hi : v); }
template <typename T> constexpr T jmin (T a, T b) noexcept { return b < a ? b : a; }
template <typename T> constexpr T jmin (T a, T b, T c) noexcept { return b < a ? (c < b ? c : b) : (c < a ? c : a); }
template <typename T> constexpr T jmax (T a, T b) noexcept { return a < b ? b : a; }
template <typename T> constexpr T jmax (T a, T b, T c) noexcept { return a < b ? (b < c ? c : b) : (a < c ? c : a); }
template <typename T> constexpr T jmax (T a, T b, T c, T d) noexcept { return jmax (a, jmax (b, c, d)); }

// Nearest integer, halves to even: the value plus 2^52 + 2^51 holds it in its low 32 bits.
template <typename T> inline int roundToInt (T value) noexcept
{
    const double shifted = (double) value + 6755399441055744.0;
    std::uint64_t bits;
    std::memcpy (&bits, &shifted, sizeof (bits));
    return (int) (std::int32_t) (std::uint32_t) (bits & 0xffffffffu);
}

template <typename T> struct MathConstants
{
    static constexpr T pi = static_cast<T> (3.141592653589793238L);
    static constexpr T twoPi = static_cast<T> (2 * 3.141592653589793238L);
    static constexpr T halfPi = static_cast<T> (3.141592653589793238L / 2);
    static constexpr T sqrt2 = static_cast<T> (1.4142135623730950488L);
};
}
