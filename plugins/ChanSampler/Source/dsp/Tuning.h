#pragma once
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

// The fitted constants of the DSP, in one struct so the offline renderer (tools/CsRender.cpp) can override any of
// them by name while fitting (tune:key=value on its command line, or tuning=<file>); the plug-in uses the defaults.
// Chan Sampler's other constants are exact readings (tables and tick grids, MEASUREMENTS.md) rather than fits, so
// they stay where they are used. Add a field and a line in set() per fitted constant; the comment names the
// MEASUREMENTS.md section that decided it.
namespace cs::dsp
{
struct Tuning
{
    // The sample interpolator's window: a 7-term cosine series over 15 input samples each side (MEASUREMENTS.md
    // "Measured laws", Playback interpolation; nulltest/probes/window_fit.py). Read once, when the interpolator's
    // table is first built.
    double interpWindow[7] = { 0.362684569, 0.509910674, 0.132162708, -0.006084749,
                               0.001926633, -0.000879890, 0.000296751 };

    // sets a constant by name; false for an unknown key
    bool set (const std::string& key, double v)
    {
        for (int i = 0; i < 7; ++i)
            if (key == "interpWindow" + std::to_string (i)) { interpWindow[i] = v; return true; }
        return false;
    }

    // key=value lines, one per line; returns 0 or the number of the first bad line
    int load (const char* path)
    {
        std::ifstream f (path);
        if (! f) { std::printf ("cannot read tuning file %s\n", path); return 1; }
        std::string s;
        int n = 0;
        while (std::getline (f, s))
        {
            ++n;
            while (! s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
            if (s.empty() || s[0] == '#') continue;
            const auto eq = s.find ('=');
            if (eq == std::string::npos || ! set (s.substr (0, eq), std::strtod (s.c_str() + eq + 1, nullptr)))
            {
                std::printf ("bad tuning line %d: %s\n", n, s.c_str());
                return n;
            }
        }
        return 0;
    }
};

// the tuning in use: the defaults in the plug-in, overridden by CsRender
inline Tuning& tuning()
{
    static Tuning t;
    return t;
}
}
