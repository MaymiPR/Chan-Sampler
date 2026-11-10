#include "Params.h"
#include "dsp/Chords.h"

namespace cs
{

// Every knob's range, display text and typed-value parser follow FL Studio's
// Sampler: a knob's 0..1 position (what FL's "Copy value" puts on the
// clipboard) maps to the same value here, and the text matches FL's hint bar.
// Positions and texts were read from FL itself (nulltest/, MEASUREMENTS.md).

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

    juce::ParameterID paramId (const juce::String& s) { return { s, 1 }; }

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

    FloatAttr attr (ToText to, FromText from)
    {
        return FloatAttr().withStringFromValueFunction (std::move (to)).withValueFromStringFunction (std::move (from));
    }

    FloatAttr percentAttr()  { return attr (pct(), fromPct()); }

    // Envelope / LFO times: FL's position is (raw - 100) / 65436, but its
    // percentage is raw / 65536 (raw 20000 shows 31%, 16193 25%, 53784 82%),
    // read as a position as on the other knobs (50 is 0.5).
    FloatAttr envTimeAttr()
    {
        return attr ([] (float v, int)
                     {
                         const auto raw = juce::roundToInt (100.0 + (double) v * 65436.0);
                         return juce::String (juce::roundToInt (raw * 100.0 / 65536.0)) + "%";
                     },
                     fromPct());
    }

    void addFloat (Layout& l, const juce::String& pidStr, const juce::String& name,
                   juce::NormalisableRange<float> range, float def, FloatAttr a)
    {
        l.add (std::make_unique<juce::AudioParameterFloat> (paramId (pidStr), name, range, def, a));
    }

    void addBool (Layout& l, const juce::String& pidStr, const juce::String& name, bool def)
    {
        l.add (std::make_unique<juce::AudioParameterBool> (paramId (pidStr), name, def));
    }

    void addChoice (Layout& l, const juce::String& pidStr, const juce::String& name,
                    const juce::StringArray& items, int def)
    {
        l.add (std::make_unique<juce::AudioParameterChoice> (paramId (pidStr), name, items, def));
    }

    // FL knobs hold whole raw values; these ranges snap to the same steps so a
    // typed or pasted value lands where FL's does (1 / steps per raw unit).
    juce::NormalisableRange<float> step (int steps, float start = 0.0f)
    {
        return { start, 1.0f, 1.0f / (float) steps };
    }

    // DAHDSR envelope + LFO for one INS target, with FL's defaults.
    // FL's Panning page has no envelope, only the LFO.
    void addTarget (Layout& l, const char* prefix, const juce::String& target, bool hasAmount, bool hasEnvelope = true)
    {
        auto p = [prefix] (const char* s) { return id::env (prefix, s); };
        const bool isVolume = juce::String (prefix) == id::volPrefix;
        const juce::NormalisableRange<float> unit { 0.0f, 1.0f }, bipolar { -1.0f, 1.0f };
        if (hasEnvelope)
        {
            addBool  (l, p ("EnvOn"),  target + " envelope", false);
            addFloat (l, p ("Del"),    target + " predelay time", unit, 0.0f,           envTimeAttr());
            addFloat (l, p ("Att"),    target + " attack time",   unit, flEnvKnobForRaw (20000), envTimeAttr());
            addFloat (l, p ("Hold"),   target + " hold time",     unit, flEnvKnobForRaw (20000), envTimeAttr());
            addFloat (l, p ("Dec"),    target + " decay time",    unit, flEnvKnobForRaw (30000), envTimeAttr());
            addFloat (l, p ("Sus"),    target + " sustain level", unit, 50.0f / 128.0f, percentAttr());
            addFloat (l, p ("Rel"),    target + " release time",  unit, flEnvKnobForRaw (20000), envTimeAttr());
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
        addFloat  (l, p ("LfoDel"),   target + " LFO predelay time", unit, 0.0f, envTimeAttr());
        addFloat  (l, p ("LfoAtt"),   target + " LFO attack time",   unit, flEnvKnobForRaw (20000), envTimeAttr());
    }

    // Arpeggio time (measured in FL): position = (raw - 450) / 997, raw 1447 is
    // Hold. FL plays a whole number of ticks (24 per step): an exponential of
    // the integer raw value rounded to half ticks, then halved downwards (the
    // two values fitted to
    // the 55 measured raw values, measurements/ChanSampler/arp_ticks_dense.txt and the 13
    // where the earlier fit was a tick out, measure_arp_period.py).
    double arpTicksForRaw (double raw)
    {
        const auto halfTicks = std::nearbyint (1.84558 * (std::pow (24.99798, raw / 1000.0) - 1.0));
        return juce::jmax (1.0, std::floor (halfTicks / 2.0));
    }
    double arpRawFromKnob (float knob) { return std::round (450.0 + 997.0 * (double) juce::jlimit (0.0f, 1.0f, knob)); }
}

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
    const auto raw = juce::jlimit (100.0f, 65536.0f, 100.0f + knob * 65436.0f);
    if (raw < raws[0])
        return (float) juce::jmax (0.0, 0.0646133 * std::exp (6.8996 * (double) knob) - 0.0636541);
    int i = 0;
    while (i < 26 && raw > raws[i + 1])
        ++i;
    const auto t = (raw - raws[i]) / (raws[i + 1] - raws[i]);
    const auto l0 = std::log (ms[i] - c), l1 = std::log (ms[i + 1] - c);
    return (float) (0.001 * (std::exp (l0 + (l1 - l0) * t) + c));
}

double flLfoPeriodT130 (float speedKnob)
{
    return 13.5755 * std::pow (982.5, (double) juce::jlimit (0.0f, 1.0f, speedKnob)) - 13.2745;
}

double flLfoDelayT130 (float delayKnob)
{
    // The speed law on the predelay's raw value (100 .. 65536).
    const auto raw = 100.0 + juce::jlimit (0.0f, 1.0f, delayKnob) * 65436.0;
    return juce::jmax (0.0, 13.5755 * std::pow (982.5, (raw - 200.0) / 65336.0) - 13.2745);
}

int flSlideTicks (float knob)
{
    // Glide length in ticks, measured in FL renders at 17 raw values
    // (nulltest/cases_untested.py): raw 0 -> 1, 500 -> 4, 820 -> 13,
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

float outputLevel (float position)
{
    return flVolumeGain (position) * (0.5f / flVolumeGain (defaultOutputPosition));
}

float outputPositionForOldDb (float db)
{
    if (db <= -47.9f)
        return 0.0f;   // the old trim's bottom was silence
    const auto knobGain = juce::Decibels::decibelsToGain ((double) db) / (0.5 / flVolumeGain (defaultOutputPosition));
    return (float) juce::jlimit (0.0, 1.0, std::log (10.0 * knobGain + 1.0) / std::log (11.0));
}

float flTensionToK (float tension)
{
    // Exponents fitted to FL renders at raw tension -128, -101 (FL's default
    // release tension), -96, -64, ..., 128.
    static const float t[] = { -128.0f, -101.0f, -96.0f, -64.0f, -32.0f, 0.0f, 32.0f, 64.0f, 96.0f, 128.0f };
    static const float k[] = { 6.19f, 4.62f, 4.15f, 2.13f, 0.56f, 0.0f, -0.58f, -2.18f, -4.29f, -6.45f };
    const auto raw = juce::jlimit (-128.0f, 128.0f, tension * 128.0f);
    int i = 0;
    while (i < 8 && raw > t[i + 1])
        ++i;
    const auto f = (raw - t[i]) / (t[i + 1] - t[i]);
    return k[i] + (k[i + 1] - k[i]) * f;
}

namespace
{
    const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::String flNoteName (int midiNote)
{
    const auto* const* names = noteNames;
    return juce::String (names[midiNote % 12]) + juce::String (midiNote / 12);
}

int parseNoteName (const juce::String& text)
{
    const auto* const* names = noteNames;
    auto t = text.trim().toUpperCase();
    for (int n = 11; n >= 0; --n)
        if (t.startsWith (names[n]))
            return juce::jlimit (0, 127, n + 12 * t.substring ((int) strlen (names[n])).getIntValue());
    return juce::jlimit (0, 127, t.getIntValue());
}

const juce::StringArray& precomputeParamIDs()
{
    static const juce::StringArray ids {
        id::pcRemoveDC, id::pcReversePolarity, id::pcNormalize, id::pcFadeStereo,
        id::pcReverse, id::pcSwapStereo, id::pcStart, id::pcLength, id::pcFadeIn,
        id::pcFadeOut, id::pcCrossfade, id::pcTrim, id::pcBoost, id::pcBoostClip, id::pcEq,
        id::pcRmMix, id::pcRmFreq, id::pcCut, id::pcRes, id::pcReverbType, id::pcReverb,
        id::pcStereoDelay, id::pcPogo, id::loadRegions, id::loadSlices,
        // The loop crossfade is baked into the data, so it depends on the loop points.
        id::useLoop, id::loopStart, id::loopEnd
    };
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const std::atomic<int>& sampleLength)
{
    Layout l;
    const juce::NormalisableRange<float> unit { 0.0f, 1.0f }, bipolar { -1.0f, 1.0f };

    // MIDI pitch bend range. FL's channel VOL / PAN / PITCH / RANGE / TRACK
    // belong to the host channel, which applies them to any plugin.
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::pitchRange), "Pitch bend range", 0, 48, 2));

    // Content / Playback. Option names are FL's own (its declicking menu's).
    addChoice (l, id::declick, "Declicking mode",
               { "Out only (no bleeding)", "Transient (no bleeding)", "Transient (bleeding)",
                 "Generic (bleeding)", "Smooth (bleeding)", "Crossfade (bleeding)" }, 0);
    // Markers stored in the file replace the detected transients of the slice
    // methods (measured in FL): regions = WAV cue points, slice markers = the
    // ACID "strc" chunk. FL's defaults: regions on, slice markers off.
    addBool  (l, id::loadRegions, "Load regions", true);
    addBool  (l, id::loadSlices,  "Load slice markers", false);
    addFloat (l, id::startOffset, "Sample start offset", unit, 0.0f, percentAttr());
    // FL Sampler defaults (a new channel): Use loop points off, START = END = 0
    // (no loop set). FL's Basic project template has it on in its channels.
    addBool  (l, id::useLoop,   "Use loop points", false);

    // Loop points: FL shows sample positions (position = frame / sample length).
    auto frames = [&sampleLength] (float v, int) { return juce::String (juce::roundToInt (v * (float) sampleLength.load())); };
    auto fromFrames = [&sampleLength] (const juce::String& t)
    {
        const auto n = sampleLength.load();
        return n > 0 ? (float) (firstNumber (t) / n) : 0.0f;
    };
    addFloat (l, id::loopStart, "Loop start position", unit, 0.0f, attr (frames, fromFrames));
    addFloat (l, id::loopEnd,   "Loop end position",   unit, 0.0f, attr (frames, fromFrames));
    addBool  (l, id::pingPong,  "Ping pong loop", false);

    // OUT: FL's channel volume knob, shown as FL shows it ("78%  -5.2dB  0.55").
    addFloat (l, id::output, "Output", unit, defaultOutputPosition,
              attr ([] (float v, int)
                    {
                        const auto g = flVolumeGain (v);
                        return juce::String (juce::roundToInt (v * 100.0f)) + "%  "
                             + (g > 0.0f ? juce::String (juce::Decibels::gainToDecibels (g), 1) + "dB" : juce::String ("-INFdB"))
                             + "  " + juce::String (g, 2);
                    }, fromPct()));

    // Time stretching (SMP > Time stretching).
    addChoice (l, id::stretchMode, "Stretch method",
               { "Resample", "Stretch", "Stretch Pro", "e3 generic", "e3 mono", "Slice stretch", "Slice map",
                 "Auto", "e2 generic", "e2 transient", "e2 mono", "e2 speech" }, 0);
    // MUL: 2^(raw / 10000), raw +/-20000, so the knob is linear in octaves (x0.25 .. x4).
    juce::NormalisableRange<float> mulRange { 0.25f, 4.0f,
        [] (float, float, float n) { return std::pow (2.0f, 4.0f * n - 2.0f); },
        [] (float, float, float v) { return (std::log2 (v) + 2.0f) / 4.0f; } };
    addFloat (l, id::stretchMul, "Time stretch (multiplicator)", mulRange, 1.0f,
              attr ([] (float v, int) { return juce::String (v * 100.0f, 2) + "%"; }, fromPct()));
    // TIME: position = beats / 192, shown as bars:steps:ticks.
    addFloat (l, id::stretchTime, "Time stretch (time)", { 0.0f, 192.0f }, 0.0f,
              attr ([] (float v, int) { return barsStepsTicks (v); },
                    [] (const juce::String& t) { return (float) (parseStepsTicks (t) / 4.0); }));
    // FORMANT (Stretch Pro only, measured in FL): +/-1200 cents, linear, centre
    // shows "preserve"; a plain typed number is a percentage of the slider.
    addFloat (l, id::formant, "Formant shift amount", { -1200.0f, 1200.0f }, 0.0f,
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
    addFloat (l, id::pitch, "Pitch shift", { -1200.0f, 1200.0f }, 0.0f, attr (cents(), fromNum()));

    // Precomputed tools (FL does not automate them; here they are, so a host's
    // undo history records them)
    addBool (l, id::pcRemoveDC,        "Remove DC offset", false);
    addBool (l, id::pcReversePolarity, "Reverse polarity", false);
    addBool (l, id::pcNormalize,       "Normalize",        false);
    addBool (l, id::pcFadeStereo,      "Fade stereo",      false);
    addBool (l, id::pcReverse,         "Reverse",          false);
    addBool (l, id::pcSwapStereo,      "Swap stereo",      false);
    addFloat (l, id::pcStart,     "Sample start offset (precomputed)", unit, 0.0f, attr (pctSpace(), fromPct()));
    addFloat (l, id::pcLength,    "Sample end offset",          unit, 1.0f, attr (pctSpace(), fromPct()));
    addFloat (l, id::pcFadeIn,    "Quick fade in",              step (1024), 0.0f, percentAttr());   // raw / 1024
    addFloat (l, id::pcFadeOut,   "Quick fade out",             step (1024), 0.0f, percentAttr());   // raw / 1024
    addFloat (l, id::pcCrossfade, "Crossfade loop",             step (1024), 0.0f, percentAttr());   // raw / 1024
    addFloat (l, id::pcTrim,      "Silence trimming threshold", step (256), 0.0f, percentAttr());   // raw / 256

    // Precomputed effects
    addFloat (l, id::pcBoost, "Boosts the gain", step (256), 0.0f, percentAttr());                   // raw / 256
    addBool  (l, id::pcBoostClip, "Add clipping at 0dB", false);
    addFloat (l, id::pcEq,          "Shifts the frequency balance", step (128, -1.0f), 0.0f, percentAttr());
    addFloat (l, id::pcRmMix,       "Ring modulation mix",          step (256), 0.0f, percentAttr());
    addFloat (l, id::pcRmFreq,      "Ring modulation frequency",    step (256), 0.5f, percentAttr());
    // Cutoff: position = (raw - 16) / 1008, shown as raw / 1024.
    addFloat (l, id::pcCut,         "Filter cutoff", { 16.0f / 1024.0f, 1.0f, 1.0f / 1024.0f }, 1.0f, percentAttr());
    addFloat (l, id::pcRes,         "Filter resonance",             step (640), 0.0f, percentAttr());  // raw / 640
    addChoice (l, id::pcReverbType, "Reverb type", { "A", "B" }, 1);   // FL default: B
    addFloat (l, id::pcReverb,      "Reverb amount",                step (256), 0.0f, percentAttr());
    addFloat (l, id::pcStereoDelay, "Stereo delay",                 step (2048, -1.0f), 0.0f, attr (leftRight(), fromLeftRight()));
    addFloat (l, id::pcPogo,        "Pogo effect",                  step (256, -1.0f), 0.0f, percentAttr());

    // INS targets
    addTarget (l, id::panPrefix,   "Panning", false, false);
    addTarget (l, id::volPrefix,   "Volume",  false);
    addTarget (l, id::modXPrefix,  "Mod X",   true);
    addTarget (l, id::modYPrefix,  "Mod Y",   true);
    addTarget (l, id::pitchPrefix, "Pitch",   true);

    addChoice (l, id::filterType, "Filter type",
               { "Fast LP", "LP", "BP", "HP", "BS", "LPx2", "SVF LP", "SVF LPx2" }, 0);
    addFloat  (l, id::cutoff,    "Filter frequency (modulation X)", unit, 1.0f, percentAttr());   // raw / 256
    addFloat  (l, id::resonance, "Filter bandwidth (modulation Y)", unit, 0.0f, percentAttr());   // raw / 256

    // FUNC: Levels adjustment (FL's channel levels: pan raw 0..12800, centre 6400)
    addFloat (l, id::pan,    "Panning offset", bipolar, 0.0f, attr (leftRight(), fromLeftRight()));       // raw +/-6400
    // FL's Levels "Volume multiplier" (event 229, raw / 12800; measured: gain
    // (11^x - 1) / 10, 100% = unity at the knob's centre, 200% = 12.00).
    addFloat (l, id::volume, "Volume multiplier", { 0.0f, 2.0f }, 1.0f,
              attr ([] (float v, int)
                    {
                        const auto g = flVolumeGain (v);
                        return juce::String (juce::roundToInt (v * 100.0f)) + "%  "
                             + (g > 0.0f ? juce::String (juce::Decibels::gainToDecibels (g), 1) + "dB" : juce::String ("-INFdB"))
                             + "  " + juce::String (g, 2);
                    }, fromPct()));
    addFloat (l, id::xOfs, "Modulation X offset", bipolar, 0.0f, percentAttr());                            // raw +/-256
    addFloat (l, id::yOfs, "Modulation Y offset", bipolar, 0.0f, percentAttr());

    // FUNC: Polyphony / Group
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::maxPoly), "Maximum polyphony", 0, 64, 0,
               juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? juce::String ("--") : juce::String (v); })));
    addBool  (l, id::mono,  "Monophonic mode", false);
    addBool  (l, id::porta, "Portamento mode", false);
    // Slide: position = raw / 1660, shown as steps:ticks; a new Sampler in FL
    // starts at raw 500 (0:03).
    addFloat (l, id::slide, "Portamento time", unit, 500.0f / 1660.0f,
              attr ([] (float v, int) { return stepsTicks (flSlideSteps (v)); },
                    [] (const juce::String& t)
                    {
                        const auto ticks = juce::jmax (0.0, parseStepsTicks (t)) * 24.0 + 1.0;
                        return (float) juce::jlimit (0.0, 1.0, std::log ((ticks + 0.15) / 0.92049) / std::log (209.414));
                    }));
    addBool  (l, id::cutItself, "Cut itself", false);
    // Group (FL's Cut / By), shared between Chan Sampler instances (CutGroups.h); 0 = none ("--").
    for (auto* gid : { id::cutGroup, id::cutBy })
        l.add (std::make_unique<juce::AudioParameterInt> (paramId (gid), gid == id::cutGroup ? "Cut group" : "Cut by group", 0, 99, 0,
                   juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v == 0 ? juce::String ("--") : juce::String (v); })));

    // FUNC: Tracking (raw +/-256)
    addFloat (l, id::velPan, "Velocity to panning",      bipolar, 0.0f, percentAttr());
    addFloat (l, id::velX,   "Velocity to modulation X", bipolar, 0.0f, percentAttr());
    addFloat (l, id::velY,   "Velocity to modulation Y", bipolar, 0.0f, percentAttr());
    addFloat (l, id::velMid, "Velocity middle value", unit, 100.0f / 128.0f, percentAttr());
    addFloat (l, id::keyPan, "Key to panning",      bipolar, 0.0f, percentAttr());
    addFloat (l, id::keyX,   "Key to modulation X", bipolar, 0.0f, percentAttr());
    addFloat (l, id::keyY,   "Key to modulation Y", bipolar, 0.0f, percentAttr());
    // FL's key range is C0..C10 (note 0..120; C5 = 0.5, measured with Copy value).
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::keyMid), "Middle value", 0, 120, 60,
               juce::AudioParameterIntAttributes()
                   .withStringFromValueFunction ([] (int v, int) { return flNoteName (v); })
                   .withValueFromStringFunction ([] (const juce::String& t) { return parseNoteName (t); })));

    // Arpeggiator
    addChoice (l, id::arpDir, "Arpeggio direction",
               { "Off", "Up", "Down", "Up / down", "Up / down (sticky)", "Random" }, 0);
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::arpRange), "Arpeggio range (octaves)", 1, 6, 1));
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::arpRepeat), "Arpeggio repeat", 1, 16, 1));
    addFloat (l, id::arpTime, "Arpeggio time", unit, (1024.0f - 450.0f) / 997.0f,
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
    addFloat (l, id::arpGate, "Arpeggio gate", unit, 1.0f, percentAttr());
    addBool  (l, id::arpSlide, "Arpeggio slide", false);
    juce::StringArray chords { "Auto", "Auto (sustain)", "(none)" };
    for (auto& ch : chordPresets)
        chords.add (ch.shortName);   // FL shows the short names (sus2, 6...)
    addChoice (l, id::arpChord, "Arpeggio chord", chords, 1);   // FL default: Auto (sustain) (raw -1)

    // Echo delay / fat mode. Each echo is an extra note voice.
    addFloat (l, id::echoFeed, "Delay feedback", unit, 0.0f,                         // raw / 25600, shown /12800
              attr ([] (float v, int) { return juce::String (juce::roundToInt (v * 200.0f)) + "%"; },
                    [] (const juce::String& t) { return (float) (firstNumber (t) / 200.0); }));
    // Echo pan: position = 0.5 + (raw - 6400) / 25600, so 100% sits at 0.25 / 0.75.
    addFloat (l, id::echoPan,   "Delay panning",      { -2.0f, 2.0f }, 0.0f, attr (leftRight(), fromLeftRight()));
    addFloat (l, id::echoX,     "Delay modulation X", bipolar, 0.0f, percentAttr());
    addFloat (l, id::echoY,     "Delay modulation Y", bipolar, 0.0f, percentAttr());
    addFloat (l, id::echoPitch, "Delay pitch", { -1200.0f, 1200.0f }, 0.0f, attr (cents(), fromNum()));
    addFloat (l, id::echoTime,  "Delay time", { 0.0f, 8.0f }, 3.0f,                // raw / 48 steps
              attr ([] (float v, int) { return stepsTicks (v); }, [] (const juce::String& t) { return (float) parseStepsTicks (t); }));
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::echoCount), "Echoes", 1, 16, 4));
    addBool  (l, id::echoPing, "Ping pong", false);
    addBool  (l, id::echoFat,  "Fat mode", false);

    // Root note (FL default C5)
    l.add (std::make_unique<juce::AudioParameterInt> (paramId (id::rootNote), "Root note", 0, 127, 60,
               juce::AudioParameterIntAttributes()
                   .withStringFromValueFunction ([] (int v, int) { return flNoteName (v); })
                   .withValueFromStringFunction ([] (const juce::String& t) { return parseNoteName (t); })));
    addFloat (l, id::fineTune, "Fine tuning", { -100.0f, 100.0f }, 0.0f,
              attr ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " cents"; }, fromNum()));

    // The host's undo step for group edits, renamed after each (History)
    l.add (std::make_unique<HistoryStepParameter> (paramId (id::historyStep), "History step"));

    return l;
}
}
