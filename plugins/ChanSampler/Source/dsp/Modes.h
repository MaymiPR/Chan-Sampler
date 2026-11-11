#pragma once

// The engine's modes as the parameters choose them (FL's menus, in their order).

#include <cmath>

namespace cs::dsp
{
enum class DeclickMode { outOnly, transient, transientBleed, generic, smooth, crossfade };
enum class FilterType  { fastLP, lp, bp, hp, bs, lpx2, svfLP, svfLPx2 };
enum class LfoShape    { sine, triangle, square };

// FL's stretch methods in its own menu order (StretchModeSelect).
enum class StretchMode { resample, stretch, stretchPro, e3Generic, e3Mono, sliceStretch, sliceMap,
                         autoMode, e2Generic, e2Transient, e2Mono, e2Speech };

// FL's Auto stretch mode: plain resampling while nothing needs stretching,
// "e3 generic" once PITCH, MUL or TIME is set (as FL shows under the menu).
inline StretchMode resolveStretchMode (StretchMode mode, float pitchCents, float mul, float timeBeats)
{
    if (mode != StretchMode::autoMode)
        return mode;
    const bool stretching = std::abs (pitchCents) > 0.5f || std::abs (mul - 1.0f) > 1.0e-4f || timeBeats > 0.01f;
    return stretching ? StretchMode::e3Generic : StretchMode::resample;
}
}
