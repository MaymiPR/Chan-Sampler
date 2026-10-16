#include "Params.h"
#include "Chords.h"

// Every knob's range, display text and typed-value parser follow FL Studio's
// Sampler: a knob's 0..1 position (what FL's "Copy value" puts on the
// clipboard) maps to the same value here, and the text matches FL's hint bar.
// Positions and texts were read from FL itself (nulltest/, docs/EVIDENCE.md).

std::atomic<int> displaySampleLength { 0 };
std::atomic<double> displayBpm { 130.0 };

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

namespace
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatAttr = juce::AudioParameterFloatAttributes;
    using ToText = std::function<juce::String (float, int)>;
    using FromText = std::function<float (const juce::String&)>;

    juce::ParameterID id (const juce::String& s) { return { s, 1 }; }

    double firstNumber (const juce::String& t)
    {
        auto s = t.trim();
        const auto start = s.indexOfAnyOf ("-+0123456789.");
        return start < 0 ? 0.0 : s.substring (start).getDoubleValue();
    }

    // "S:TT" (steps:ticks, 24 ticks per step) or "B:SS:TT" (bars:steps:ticks).
    double parseStepsTicks (const juce::String& t)
    {
        auto parts = juce::StringArray::fromTokens (t.trim(), ":", {});
        if (parts.size() == 3) return parts[0].getDoubleValue() * 16.0 + parts[1].getDoubleValue() + parts[2].getDoubleValue() / 24.0;
        if (parts.size() == 2) return parts[0].getDoubleValue() + parts[1].getDoubleValue() / 24.0;
        return firstNumber (t);
    }

    juce::String stepsTicks (double steps)
    {
        const auto totalTicks = (int) std::round (steps * 24.0);
        return juce::String (totalTicks / 24) + ":" + juce::String (totalTicks % 24).paddedLeft ('0', 2);
    }

    juce::String barsStepsTicks (double beats)
    {
        const auto totalTicks = (int) std::round (beats * 4.0 * 24.0);
        const int bars = totalTicks / (16 * 24);
        const int steps = (totalTicks / 24) % 16;
        return juce::String (bars) + ":" + juce::String (steps).paddedLeft ('0', 2) + ":" + juce::String (totalTicks % 24).paddedLeft ('0', 2);
    }

    // Display / parse pairs
    ToText pct()         { return [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }; }
    ToText pctSpace()    { return [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }; }
    FromText fromPct()   { return [] (const juce::String& t) { return (float) (firstNumber (t) / 100.0); }; }

    ToText leftRight()
    {
        return [] (float v, int)
        {
            const auto p = juce::roundToInt (v * 100.0f);
            return p == 0 ? juce::String ("Centered") : juce::String (std::abs (p)) + (p < 0 ? "% left" : "% right");
        };
    }
    FromText fromLeftRight()
    {
        return [] (const juce::String& t)
        {
            const auto v = std::abs (firstNumber (t)) / 100.0;
            return (float) (t.containsIgnoreCase ("left") || t.trim().startsWith ("-") ? -v : v);
        };
    }

    ToText cents()       { return [] (float v, int) { const auto c = juce::roundToInt (v); return (c > 0 ? "+" : "") + juce::String (c) + " cents"; }; }
    FromText fromNum()   { return [] (const juce::String& t) { return (float) firstNumber (t); }; }

    FloatAttr attr (ToText to, FromText from, bool automatable = true)
    {
        return FloatAttr().withStringFromValueFunction (std::move (to)).withValueFromStringFunction (std::move (from)).withAutomatable (automatable);
    }

    FloatAttr percentAttr (bool automatable = true)  { return attr (pct(), fromPct(), automatable); }

    void addFloat (Layout& l, const juce::String& pidStr, const juce::String& name,
                   juce::NormalisableRange<float> range, float def, FloatAttr a)
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (id (pidStr), name, range, def, a));
    }

    void addBool (Layout& l, const juce::String& pidStr, const juce::String& name, bool def, bool automatable = true)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (id (pidStr), name, def,
                   juce::AudioParameterBoolAttributes().withAutomatable (automatable)));
    }

    void addChoice (Layout& l, const juce::String& pidStr, const juce::String& name,
                    const juce::StringArray& items, int def, bool automatable = true)
    {
        l.add (std::make_unique<juce::AudioParameterChoice> (id (pidStr), name, items, def,
                   juce::AudioParameterChoiceAttributes().withAutomatable (automatable)));
    }

    // FL knobs hold whole raw values; these ranges snap to the same steps so a
    // typed or pasted value lands where FL's does (1 / steps per raw unit).
    juce::NormalisableRange<float> step (int steps, float start = 0.0f)
    {
        return { start, 1.0f, 1.0f / (float) steps };
    }

    // Envelope / LFO time knobs: FL position = (raw - 100) / 65436, shown as %.
    float envPos (int raw) { return (float) (raw - 100) / 65436.0f; }

    // DAHDSR envelope + LFO for one INS target, with FL's defaults.
    // FL's Panning page has no envelope, only the LFO.
    void addTarget (Layout& l, const char* prefix, const juce::String& target, bool hasAmount, bool hasEnvelope = true)
    {
        auto p = [prefix] (const char* s) { return pid::env (prefix, s); };
        const bool isVolume = juce::String (prefix) == pid::volPrefix;
        const juce::NormalisableRange<float> unit { 0.0f, 1.0f }, bipolar { -1.0f, 1.0f };
        if (hasEnvelope)
        {
            addBool  (l, p ("EnvOn"),  target + " envelope", false);
            addFloat (l, p ("Del"),    target + " predelay time", unit, 0.0f,           percentAttr());
            addFloat (l, p ("Att"),    target + " attack time",   unit, envPos (20000), percentAttr());
            addFloat (l, p ("Hold"),   target + " hold time",     unit, envPos (20000), percentAttr());
            addFloat (l, p ("Dec"),    target + " decay time",    unit, envPos (30000), percentAttr());
            addFloat (l, p ("Sus"),    target + " sustain level", unit, 50.0f / 128.0f, percentAttr());
            addFloat (l, p ("Rel"),    target + " release time",  unit, envPos (20000), percentAttr());
            addFloat (l, p ("AttT"),   target + " attack tension",  bipolar, 0.0f, percentAttr());
            addFloat (l, p ("DecT"),   target + " decay tension",   bipolar, 0.0f, percentAttr());
            addFloat (l, p ("RelT"),   target + " release tension", bipolar, isVolume ? -101.0f / 128.0f : 0.0f, percentAttr());
            if (hasAmount)
                addFloat (l, p ("EnvAmt"), target + " envelope amount", bipolar, 0.0f, percentAttr());
        }

        // FL's Tempo / Global switches (event 218 flags: 1 envelope tempo, 2 LFO tempo, 32 global phase)
        if (hasEnvelope)
            addBool (l, p ("EnvTempo"), target + " envelope tempo-based time", false);
        addBool  (l, p ("LfoTempo"),  target + " LFO tempo-based time", false);
        addBool  (l, p ("LfoGlobal"), target + " LFO global phase", false);
        addChoice (l, p ("LfoShape"), target + " LFO shape", { "Sine", "Triangle", "Pulse" }, 0);
        // LFO speed: position = (raw - 200) / 65336, default raw 32950 (0.5 Hz).
        addFloat  (l, p ("LfoSpeed"), target + " LFO speed",        unit, (32950.0f - 200.0f) / 65336.0f, percentAttr());
        addFloat  (l, p ("LfoAmt"),   target + " LFO amount",       bipolar, 0.0f, percentAttr());
        addFloat  (l, p ("LfoDel"),   target + " LFO predelay time", unit, 0.0f, percentAttr());
        addFloat  (l, p ("LfoAtt"),   target + " LFO attack time",   unit, envPos (20000), percentAttr());
    }

    double interpLog (const double* xs, const double* ys, int n, double x)
    {
        if (x <= xs[0])     return ys[0] * std::pow (ys[1] / ys[0], (x - xs[0]) / (xs[1] - xs[0]));
        if (x >= xs[n - 1]) return ys[n - 1] * std::pow (ys[n - 1] / ys[n - 2], (x - xs[n - 1]) / (xs[n - 1] - xs[n - 2]));
        int i = 0;
        while (x > xs[i + 1]) ++i;
        return ys[i] * std::pow (ys[i + 1] / ys[i], (x - xs[i]) / (xs[i + 1] - xs[i]));
    }

    // Arpeggio time: FL knob positions for 1/4, 1/2, 1, 2 and 4 steps (read
    // from FL by typing each value and copying the position); 1 = Hold.
    // Arpeggio time (measured in FL): position = (raw - 450) / 997, raw 1447 is
    // Hold. FL plays a whole number of ticks (24 per step):
    // round (3.88852 * 25^p - 1.08184) with p from the integer raw value, which
    // reproduces all 77 measured raw values (nulltest/arp_ticks_dense.txt).
    double arpTicksForRaw (double raw)
    {
        const auto p = (raw - 450.0) / 997.0;
        return juce::jmax (1.0, std::round (3.88852 * std::pow (25.0, p) - 1.08184));
    }
    double arpRawFromKnob (float knob) { return std::round (450.0 + 997.0 * (double) juce::jlimit (0.0f, 1.0f, knob)); }
}

float flEnvSeconds (float knob)
{
    // Attack times measured from FL renders at raw 2500, 5000, ..., 62500, 65536.
    static const float ms[] = { 24.036f, 48.072f, 81.723f, 124.988f, 177.868f, 250.009f, 346.188f, 466.365f,
                                624.989f, 831.738f, 1100.945f, 1451.91f, 1908.638f, 2504.798f, 3274.036f, 4278.807f,
                                5591.254f, 7293.248f, 9509.471f, 12393.98f, 16149.056f, 21038.385f, 27398.73f,
                                35683.054f, 46455.421f };
    // Measured: the knob's minimum (raw 100) is no time at all.
    if (knob <= 0.0f)
        return 0.0f;
    const auto raw = juce::jlimit (100.0f, 65536.0f, 100.0f + knob * 65436.0f);
    if (raw <= 5000.0f)
        return raw * 48.072f / 5000.0f * 0.001f;          // linear region
    if (raw >= 62500.0f)                                   // last segment up to 64.0 s
        return 0.001f * std::exp (std::log (46455.421f) + (std::log (64004.629f) - std::log (46455.421f)) * (raw - 62500.0f) / 3036.0f);

    const auto pos = raw / 2500.0f - 1.0f;
    const int i = juce::jlimit (0, 23, (int) pos);
    const auto t = pos - (float) i;
    return 0.001f * std::exp (std::log (ms[i]) + (std::log (ms[i + 1]) - std::log (ms[i])) * t);
}

namespace
{
    double lfoTicksForRaw (double raw)
    {
        return std::round (13.5755 * std::pow (982.5, (raw - 200.0) / 65336.0) - 13.2745);
    }
}

int flLfoPeriodTicks (float speedKnob)
{
    return (int) juce::jmax (3.0, lfoTicksForRaw (200.0 + juce::jlimit (0.0f, 1.0f, speedKnob) * 65336.0));
}

double flLfoPeriodT130 (float speedKnob)
{
    return 13.5755 * std::pow (982.5, (double) juce::jlimit (0.0f, 1.0f, speedKnob)) - 13.2745;
}

double flLfoDelayT130 (float delayKnob)
{
    const auto raw = 100.0 + juce::jlimit (0.0f, 1.0f, delayKnob) * 65436.0;
    return juce::jmax (0.0, 13.5755 * std::pow (982.5, (raw - 200.0) / 65336.0) - 13.2745);
}

int flLfoDelayTicks (float delayKnob)
{
    return (int) juce::jmax (0.0, lfoTicksForRaw (100.0 + juce::jlimit (0.0f, 1.0f, delayKnob) * 65436.0));
}

int flSlideTicks (float knob)
{
    // Glide length in ticks, measured in FL renders at 17 raw values
    // (nulltest/untested_cases.py): raw 0 -> 1, 500 -> 4, 820 -> 13,
    // 1000 -> 23, 1660 -> 193 (raw = knob * 1660). FL shows one tick less.
    const auto raw = juce::jlimit (0.0, 1660.0, (double) knob * 1660.0);
    return juce::jmax (1, (int) std::lround (0.92049 * std::pow (209.414, raw / 1660.0) - 0.15));
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
    return arpTicksForRaw (raw) / 24.0;
}

float flVolumeGain (float x)
{
    // FL's volume law: (11^x - 1) / 10, x = raw / 12800 (100% = unity, 200% = x12).
    return (std::pow (11.0f, x) - 1.0f) / 10.0f;
}

float flTensionToK (float tension)
{
    // Exponents fitted to FL renders at raw tension -128, -101 (FL's default
    // release tension), -96, -64, ..., 128.
    static const float t[] = { -128.0f, -101.0f, -96.0f, -64.0f, -32.0f, 0.0f, 32.0f, 64.0f, 96.0f, 128.0f };
    static const float k[] = { 6.19f, 4.62f, 4.15f, 2.13f, 0.56f, 0.0f, -0.6f, -2.19f, -4.29f, -6.46f };
    const auto raw = juce::jlimit (-128.0f, 128.0f, tension * 128.0f);
    int i = 0;
    while (i < 8 && raw > t[i + 1])
        ++i;
    const auto f = (raw - t[i]) / (t[i + 1] - t[i]);
    return k[i] + (k[i + 1] - k[i]) * f;
}

juce::String flNoteName (int midiNote)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[midiNote % 12]) + juce::String (midiNote / 12);
}

int parseNoteName (const juce::String& text)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto t = text.trim().toUpperCase();
    for (int n = 11; n >= 0; --n)
        if (t.startsWith (names[n]))
            return juce::jlimit (0, 127, n + 12 * t.substring ((int) strlen (names[n])).getIntValue());
    return juce::jlimit (0, 127, t.getIntValue());
}

const juce::StringArray& precomputeParamIDs()
{
    static const juce::StringArray ids {
        pid::pcRemoveDC, pid::pcReversePolarity, pid::pcNormalize, pid::pcFadeStereo,
        pid::pcReverse, pid::pcSwapStereo, pid::pcStart, pid::pcLength, pid::pcFadeIn,
        pid::pcFadeOut, pid::pcCrossfade, pid::pcTrim, pid::pcBoost, pid::pcBoostClip, pid::pcEq,
        pid::pcRmMix, pid::pcRmFreq, pid::pcCut, pid::pcRes, pid::pcReverbType, pid::pcReverb,
        pid::pcStereoDelay, pid::pcPogo, pid::loadRegions, pid::loadSlices,
        // The loop crossfade is baked into the data, so it depends on the loop points.
        pid::useLoop, pid::loopStart, pid::loopEnd
    };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Layout l;
    const juce::NormalisableRange<float> unit { 0.0f, 1.0f }, bipolar { -1.0f, 1.0f };

    // MIDI pitch bend range. FL's channel VOL / PAN / PITCH / RANGE / TRACK
    // belong to the host channel, which applies them to any plugin.
    l.add (std::make_unique<juce::AudioParameterInt> (id (pid::pitchRange), "Pitch bend range", 0, 48, 2));

    // Content / Playback. Option names are FL's own (DeclickModeSelect).
    addChoice (l, pid::declick, "Declicking mode",
               { "Out only (no bleeding)", "Transient (no bleeding)", "Transient (bleeding)",
                 "Generic (bleeding)", "Smooth (bleeding)", "Crossfade (bleeding)" }, 0);
    // Markers stored in the file replace the detected transients of the slice
    // methods (measured in FL): regions = WAV cue points, slice markers = the
    // ACID "strc" chunk. FL's defaults: regions on, slice markers off.
    addBool  (l, pid::loadRegions, "Load regions", true);
    addBool  (l, pid::loadSlices,  "Load slice markers", false);
    addFloat (l, pid::startOffset, "Sample start offset", unit, 0.0f, percentAttr());
    // FL Sampler defaults: Use loop points off, START = END = 0 (no loop set).
    addBool  (l, pid::useLoop,   "Use loop points", false);

    // Loop points: FL shows sample positions (position = frame / sample length).
    auto frames = [] (float v, int) { return juce::String (juce::roundToInt (v * (float) displaySampleLength.load())); };
    auto fromFrames = [] (const juce::String& t)
    {
        const auto n = displaySampleLength.load();
        return n > 0 ? (float) (firstNumber (t) / n) : 0.0f;
    };
    addFloat (l, pid::loopStart, "Loop start position", unit, 0.0f, attr (frames, fromFrames));
    addFloat (l, pid::loopEnd,   "Loop end position",   unit, 0.0f, attr (frames, fromFrames));
    addBool  (l, pid::pingPong,  "Ping pong loop", false);

    addFloat (l, pid::output, "Output", { -48.0f, 12.0f, 0.0f, 2.0f }, defaultOutputDb,
              attr ([] (float v, int) { return juce::String (v, 1) + " dB"; }, fromNum()));

    // Time stretching (SMP > Time stretching).
    addChoice (l, pid::stretchMode, "Stretch method",
               { "Resample", "Stretch", "Stretch Pro", "e3 generic", "e3 mono", "Slice stretch", "Slice map",
                 "Auto", "e2 generic", "e2 transient", "e2 mono", "e2 speech" }, 0);
    // MUL: 2^(raw / 10000), raw +/-20000, so the knob is linear in octaves (x0.25 .. x4).
    juce::NormalisableRange<float> mulRange { 0.25f, 4.0f,
        [] (float, float, float n) { return std::pow (2.0f, 4.0f * n - 2.0f); },
        [] (float, float, float v) { return (std::log2 (v) + 2.0f) / 4.0f; } };
    addFloat (l, pid::stretchMul, "Time stretch (multiplicator)", mulRange, 1.0f,
              attr ([] (float v, int) { return juce::String (v * 100.0f, 2) + "%"; }, fromPct(), false));
    // TIME: position = beats / 192, shown as bars:steps:ticks.
    addFloat (l, pid::stretchTime, "Time stretch (time)", { 0.0f, 192.0f }, 0.0f,
              attr ([] (float v, int) { return barsStepsTicks (v); },
                    [] (const juce::String& t) { return (float) (parseStepsTicks (t) / 4.0); }, false));
    // FORMANT (Stretch Pro only, measured in FL): +/-1200 cents, linear, centre
    // shows "preserve"; a plain typed number is a percentage of the slider.
    addFloat (l, pid::formant, "Formant shift amount", { -1200.0f, 1200.0f }, 0.0f,
              attr ([] (float v, int)
                    {
                        const auto c = juce::roundToInt (v);
                        return c == 0 ? juce::String ("preserve") : (c > 0 ? "+" : "") + juce::String (c) + " cents";
                    },
                    [] (const juce::String& t)
                    {
                        if (t.containsIgnoreCase ("preserve")) return 0.0f;
                        if (t.containsIgnoreCase ("cent")) return (float) firstNumber (t);
                        return (float) ((firstNumber (t) / 100.0 - 0.5) * 2400.0);
                    }));
    // PITCH: +/-1200 cents.
    addFloat (l, pid::pitch, "Pitch shift", { -1200.0f, 1200.0f }, 0.0f, attr (cents(), fromNum(), false));

    // Precomputed tools (not automatable, as in FL)
    addBool (l, pid::pcRemoveDC,        "Remove DC offset", false, false);
    addBool (l, pid::pcReversePolarity, "Reverse polarity", false, false);
    addBool (l, pid::pcNormalize,       "Normalize",        false, false);
    addBool (l, pid::pcFadeStereo,      "Fade stereo",      false, false);
    addBool (l, pid::pcReverse,         "Reverse",          false, false);
    addBool (l, pid::pcSwapStereo,      "Swap stereo",      false, false);
    addFloat (l, pid::pcStart,     "Sample start offset (precomputed)", unit, 0.0f, attr (pctSpace(), fromPct(), false));
    addFloat (l, pid::pcLength,    "Sample end offset",          unit, 1.0f, attr (pctSpace(), fromPct(), false));
    addFloat (l, pid::pcFadeIn,    "Quick fade in",              step (1024), 0.0f, percentAttr (false));   // raw / 1024
    addFloat (l, pid::pcFadeOut,   "Quick fade out",             step (1024), 0.0f, percentAttr (false));   // raw / 1024
    addFloat (l, pid::pcCrossfade, "Crossfade loop",             step (1024), 0.0f, percentAttr (false));   // raw / 1024
    addFloat (l, pid::pcTrim,      "Silence trimming threshold", step (256), 0.0f, percentAttr (false));   // raw / 256

    // Precomputed effects
    addFloat (l, pid::pcBoost, "Boosts the gain", step (256), 0.0f, percentAttr (false));                   // raw / 256
    addBool  (l, pid::pcBoostClip, "Add clipping at 0dB", false, false);
    addFloat (l, pid::pcEq,          "Shifts the frequency balance", step (128, -1.0f), 0.0f, percentAttr (false));
    addFloat (l, pid::pcRmMix,       "Ring modulation mix",          step (256), 0.0f, percentAttr (false));
    addFloat (l, pid::pcRmFreq,      "Ring modulation frequency",    step (256), 0.5f, percentAttr (false));
    // Cutoff: position = (raw - 16) / 1008, shown as raw / 1024.
    addFloat (l, pid::pcCut,         "Filter cutoff", { 16.0f / 1024.0f, 1.0f, 1.0f / 1024.0f }, 1.0f, percentAttr (false));
    addFloat (l, pid::pcRes,         "Filter resonance",             step (640), 0.0f, percentAttr (false));  // raw / 640
    addChoice (l, pid::pcReverbType, "Reverb type", { "A", "B" }, 1, false);   // FL default: B
    addFloat (l, pid::pcReverb,      "Reverb amount",                step (256), 0.0f, percentAttr (false));
    addFloat (l, pid::pcStereoDelay, "Stereo delay",                 step (2048, -1.0f), 0.0f, attr (leftRight(), fromLeftRight(), false));
    addFloat (l, pid::pcPogo,        "Pogo effect",                  step (256, -1.0f), 0.0f, percentAttr (false));

    // INS targets
    addTarget (l, pid::panPrefix,   "Panning", false, false);
    addTarget (l, pid::volPrefix,   "Volume",  false);
    addTarget (l, pid::modXPrefix,  "Mod X",   true);
    addTarget (l, pid::modYPrefix,  "Mod Y",   true);
    addTarget (l, pid::pitchPrefix, "Pitch",   true);

    addChoice (l, pid::filterType, "Filter type",
               { "Fast LP", "LP", "BP", "HP", "BS", "LPx2", "SVF LP", "SVF LPx2" }, 0);
    addFloat  (l, pid::cutoff,    "Filter frequency (modulation X)", unit, 1.0f, percentAttr());   // raw / 256
    addFloat  (l, pid::resonance, "Filter bandwidth (modulation Y)", unit, 0.0f, percentAttr());   // raw / 256

    // FUNC: Levels adjustment (FL's channel levels: pan raw 0..12800, centre 6400)
    addFloat (l, pid::pan,    "Panning offset", bipolar, 0.0f, attr (leftRight(), fromLeftRight()));       // raw +/-6400
    // FL's Levels "Volume multiplier" (event 229, raw / 12800; measured: gain
    // (11^x - 1) / 10, 100% = unity at the knob's centre, 200% = 12.00).
    addFloat (l, pid::volume, "Volume multiplier", { 0.0f, 2.0f }, 1.0f,
              attr ([] (float v, int)
                    {
                        const auto g = flVolumeGain (v);
                        return juce::String (juce::roundToInt (v * 100.0f)) + "%  "
                             + (g > 0.0f ? juce::String (juce::Decibels::gainToDecibels (g), 1) + "dB" : juce::String ("-INFdB"))
                             + "  " + juce::String (g, 2);
                    }, fromPct()));
    addFloat (l, pid::xOfs, "Modulation X offset", bipolar, 0.0f, percentAttr());                            // raw +/-256
    addFloat (l, pid::yOfs, "Modulation Y offset", bipolar, 0.0f, percentAttr());

    // FUNC: Polyphony / Group
    l.add (std::make_unique<juce::AudioParameterInt> (id (pid::maxPoly), "Maximum polyphony", 0, 64, 0,
               juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? juce::String ("--") : juce::String (v); })));
    addBool  (l, pid::mono,  "Monophonic mode", false);
    addBool  (l, pid::porta, "Portamento mode", false);
    // Slide: position = raw / 1660 (default 820 = 0:12), shown as steps:ticks.
    addFloat (l, pid::slide, "Portamento time", unit, 820.0f / 1660.0f,
              attr ([] (float v, int) { return stepsTicks (flSlideSteps (v)); },
                    [] (const juce::String& t)
                    {
                        const auto ticks = juce::jmax (0.0, parseStepsTicks (t)) * 24.0 + 1.0;
                        return (float) juce::jlimit (0.0, 1.0, std::log ((ticks + 0.15) / 0.92049) / std::log (209.414));
                    }));
    addBool  (l, pid::cutItself, "Cut itself", false);
    // Group (FL's Cut / By), shared between Chan Sampler instances (CutGroups.h); 0 = none ("--").
    for (auto* gid : { pid::cutGroup, pid::cutBy })
        l.add (std::make_unique<juce::AudioParameterInt> (id (gid), gid == pid::cutGroup ? "Cut group" : "Cut by group", 0, 99, 0,
                   juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? juce::String ("--") : juce::String (v); })));

    // FUNC: Tracking (raw +/-256)
    addFloat (l, pid::velPan, "Velocity to panning",      bipolar, 0.0f, percentAttr());
    addFloat (l, pid::velX,   "Velocity to modulation X", bipolar, 0.0f, percentAttr());
    addFloat (l, pid::velY,   "Velocity to modulation Y", bipolar, 0.0f, percentAttr());
    addFloat (l, pid::velMid, "Velocity middle value", unit, 100.0f / 128.0f, percentAttr());
    addFloat (l, pid::keyPan, "Key to panning",      bipolar, 0.0f, percentAttr());
    addFloat (l, pid::keyX,   "Key to modulation X", bipolar, 0.0f, percentAttr());
    addFloat (l, pid::keyY,   "Key to modulation Y", bipolar, 0.0f, percentAttr());
    // FL's key range is C0..C10 (note 0..120; C5 = 0.5, measured with Copy value).
    l.add (std::make_unique<juce::AudioParameterInt> (id (pid::keyMid), "Middle value", 0, 120, 60,
               juce::AudioParameterIntAttributes()
                   .withStringFromValueFunction ([] (int v, int) { return flNoteName (v); })
                   .withValueFromStringFunction ([] (const juce::String& t) { return parseNoteName (t); })));

    // Arpeggiator
    addChoice (l, pid::arpDir, "Arpeggio direction",
               { "Off", "Up", "Down", "Up / down", "Up / down (sticky)", "Random" }, 0);
    l.add (std::make_unique<juce::AudioParameterInt> (id (pid::arpRange), "Arpeggio range (octaves)", 1, 6, 1));
    l.add (std::make_unique<juce::AudioParameterInt> (id (pid::arpRepeat), "Arpeggio repeat", 1, 16, 1));
    addFloat (l, pid::arpTime, "Arpeggio time", unit, (1024.0f - 450.0f) / 997.0f,
              attr ([] (float v, int) { const auto s = flArpSteps (v); return s <= 0.0 ? juce::String ("Hold") : stepsTicks (s); },
                    [] (const juce::String& t)
                    {
                        if (t.containsIgnoreCase ("hold")) return 1.0f;
                        const auto ticks = juce::jlimit (1.0, 96.0, parseStepsTicks (t) * 24.0);
                        // the smallest raw value that plays this many ticks
                        for (int raw = 450; raw < 1447; ++raw)
                            if (arpTicksForRaw (raw) >= std::round (ticks))
                                return (float) (raw - 450) / 997.0f;
                        return (float) (1446 - 450) / 997.0f;
                    }));
    addFloat (l, pid::arpGate, "Arpeggio gate", unit, 1.0f, percentAttr());
    addBool  (l, pid::arpSlide, "Arpeggio slide", false);
    juce::StringArray chords { "Auto", "Auto (sustain)", "(none)" };
    for (auto& ch : chordPresets)
        chords.add (ch.shortName);   // FL shows the short names (sus2, 6...)
    addChoice (l, pid::arpChord, "Arpeggio chord", chords, 1);   // FL default: Auto (sustain) (raw -1)

    // Echo delay / fat mode. Each echo is an extra note voice.
    addFloat (l, pid::echoFeed, "Delay feedback", unit, 0.0f,                         // raw / 25600, shown /12800
              attr ([] (float v, int) { return juce::String (juce::roundToInt (v * 200.0f)) + "%"; },
                    [] (const juce::String& t) { return (float) (firstNumber (t) / 200.0); }));
    // Echo pan: position = 0.5 + (raw - 6400) / 25600, so 100% sits at 0.25 / 0.75.
    addFloat (l, pid::echoPan,   "Delay panning",      { -2.0f, 2.0f }, 0.0f, attr (leftRight(), fromLeftRight()));
    addFloat (l, pid::echoX,     "Delay modulation X", bipolar, 0.0f, percentAttr());
    addFloat (l, pid::echoY,     "Delay modulation Y", bipolar, 0.0f, percentAttr());
    addFloat (l, pid::echoPitch, "Delay pitch", { -1200.0f, 1200.0f }, 0.0f, attr (cents(), fromNum()));
    addFloat (l, pid::echoTime,  "Delay time", { 0.0f, 8.0f }, 3.0f,                // raw / 48 steps
              attr ([] (float v, int) { return stepsTicks (v); }, [] (const juce::String& t) { return (float) parseStepsTicks (t); }));
    l.add (std::make_unique<juce::AudioParameterInt> (id (pid::echoCount), "Echoes", 1, 16, 4));
    addBool  (l, pid::echoPing, "Ping pong", false);
    addBool  (l, pid::echoFat,  "Fat mode", false);

    // Root note (FL default C5)
    l.add (std::make_unique<juce::AudioParameterInt> (id (pid::rootNote), "Root note", 0, 127, 60,
               juce::AudioParameterIntAttributes()
                   .withStringFromValueFunction ([] (int v, int) { return flNoteName (v); })
                   .withValueFromStringFunction ([] (const juce::String& t) { return parseNoteName (t); })));
    addFloat (l, pid::fineTune, "Fine tuning", { -100.0f, 100.0f }, 0.0f,
              attr ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " cents"; }, fromNum()));

    return l;
}
