#include "FlPreset.h"
#include "PluginProcessor.h"
#include "Params.h"

#include <cmath>
#include <map>

namespace
{
    // FLP / FST event stream: id < 64 one byte, < 128 two, < 192 four, then
    // text / data with a 7-bit length prefix (nulltest/flp.py).
    struct Event
    {
        int id = 0;
        juce::MemoryBlock data;

        int32_t i32 (size_t offset, int32_t fallback = 0) const
        {
            if (offset + 4 > data.getSize()) return fallback;
            int32_t v; std::memcpy (&v, (const char*) data.getData() + offset, 4); return v;
        }
        uint32_t u32 (size_t offset = 0, uint32_t fallback = 0) const { return (uint32_t) i32 (offset, (int32_t) fallback); }
        int byte (size_t offset, int fallback = 0) const
        {
            return offset < data.getSize() ? (int) ((const uint8_t*) data.getData())[offset] : fallback;
        }
        double f64 (size_t offset, double fallback) const
        {
            if (offset + 8 > data.getSize()) return fallback;
            double v; std::memcpy (&v, (const char*) data.getData() + offset, 8); return v;
        }
        float f32 (size_t offset, float fallback) const
        {
            if (offset + 4 > data.getSize()) return fallback;
            float v; std::memcpy (&v, (const char*) data.getData() + offset, 4); return v;
        }
        uint32_t value() const   // byte / word / dword events
        {
            uint32_t v = 0;
            std::memcpy (&v, data.getData(), juce::jmin ((size_t) 4, data.getSize()));
            return v;
        }
        bool has (size_t bytes) const { return data.getSize() >= bytes; }
    };

    bool readEvents (const juce::File& f, std::vector<Event>& events)
    {
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb) || mb.getSize() < 16)
            return false;
        const auto* b = (const uint8_t*) mb.getData();
        const size_t n = mb.getSize();
        if (std::memcmp (b, "FLhd", 4) != 0)
            return false;
        uint32_t hlen; std::memcpy (&hlen, b + 4, 4);
        size_t p = 8 + hlen;
        if (p + 8 > n || std::memcmp (b + p, "FLdt", 4) != 0)
            return false;
        p += 8;
        while (p < n)
        {
            Event e;
            e.id = b[p++];
            size_t len = e.id < 64 ? 1 : e.id < 128 ? 2 : e.id < 192 ? 4 : 0;
            if (e.id >= 192)
            {
                int shift = 0;
                for (;;)
                {
                    if (p >= n) return false;
                    const auto c = b[p++];
                    len |= (size_t) (c & 0x7f) << shift;
                    shift += 7;
                    if (! (c & 0x80)) break;
                }
            }
            if (p + len > n)
                return false;
            e.data.append (b + p, len);
            p += len;
            events.push_back (std::move (e));
        }
        return true;
    }

    // FL writes paths to its own folders with placeholders.
    juce::File resolveSamplePath (juce::String path, const juce::File& presetFile)
    {
        auto replace = [&path] (const juce::String& key, const juce::Array<juce::File>& roots)
        {
            if (! path.containsIgnoreCase (key))
                return;
            const auto rest = path.fromFirstOccurrenceOf (key, false, true).trimCharactersAtStart ("\\/");
            for (auto& r : roots)
            {
                const auto f = r.getChildFile (rest.replaceCharacter ('\\', '/'));
                if (f.existsAsFile()) { path = f.getFullPathName(); return; }
            }
        };

        juce::Array<juce::File> factory, user;
       #if JUCE_WINDOWS
        for (auto* dir : { "C:\\Program Files\\Image-Line", "C:\\Program Files (x86)\\Image-Line" })
            for (auto& d : juce::File (dir).findChildFiles (juce::File::findDirectories, false, "FL Studio*"))
                factory.add (d);
       #else
        for (auto& d : juce::File ("/Applications").findChildFiles (juce::File::findDirectories, false, "FL Studio*.app"))
            factory.add (d.getChildFile ("Contents/Resources/FL"));
       #endif
        user.add (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Image-Line/FL Studio"));
        replace ("%FLStudioFactoryData%", factory);
        replace ("%FLStudioUserData%", user);

        juce::File f (path.replaceCharacter (juce::File::getSeparatorChar() == '/' ? '\\' : '/', juce::File::getSeparatorChar()));
        if (juce::File::isAbsolutePath (f.getFullPathName()) && f.existsAsFile())
            return f;
        // Moved presets: the sample next to the preset.
        const auto name = path.fromLastOccurrenceOf ("\\", false, false).fromLastOccurrenceOf ("/", false, false);
        const auto beside = presetFile.getSiblingFile (name);
        return beside.existsAsFile() ? beside : juce::File();
    }

    float envPos (int raw)          { return (float) (raw - 100) / 65436.0f; }
    float volGain (double x)        { return (float) ((std::pow (11.0, x) - 1.0) / 10.0); }
    float volKnob (double gain)     { return (float) (std::log (10.0 * juce::jmax (0.0, gain) + 1.0) / std::log (11.0)); }
}

bool flpreset::isPresetFile (const juce::File& f)
{
    return f.hasFileExtension (".fst");
}

flpreset::Result flpreset::import (ChanSamplerProcessor& processor, const juce::File& fst)
{
    Result result;
    std::vector<Event> all;
    if (! readEvents (fst, all))
    {
        result.message = fst.getFileName() + " is not an FL Studio preset.";
        return result;
    }

    // The first channel's events (a preset holds one; a project starts with globals).
    int version = 0;
    std::vector<const Event*> ch;
    int channels = 0;
    for (auto& e : all)
    {
        if (e.id == 199)
            version = juce::String::fromUTF8 ((const char*) e.data.getData(), (int) e.data.getSize()).getIntValue();
        if (e.id == 64 && ++channels > 1)
            break;
        ch.push_back (&e);
    }
    // FL 4 and earlier store pans and volumes at 1/100 of today's scale
    // (centre 64 instead of 6400; measured across FL's preset library).
    const double old = version > 0 && version < 9 ? 100.0 : 1.0;

    std::map<int, const Event*> first;
    std::vector<const Event*> envs, tracking;
    for (auto* e : ch)
    {
        if (e->id == 218) envs.push_back (e);
        else if (e->id == 228) tracking.push_back (e);
        else if (first.count (e->id) == 0) first[e->id] = e;
    }
    auto get = [&first] (int id) -> const Event* { auto it = first.find (id); return it == first.end() ? nullptr : it->second; };

    // Channel type (21): 0 = FL's Sampler; newer FL may also store it as an
    // instrument (4) without a plugin name (201). Generators carry their name.
    auto text = [] (const Event* e) { return e == nullptr ? juce::String() : juce::String::fromUTF8 ((const char*) e->data.getData(), (int) e->data.getSize()).trimCharactersAtEnd (juce::String::charToString (0)); };
    const auto* typeEvent = get (21);
    const int type = typeEvent != nullptr ? (int) typeEvent->value() : 0;
    const auto plugin = text (get (201));
    if (! (type == 0 || (type == 4 && plugin.isEmpty())) || (get (215) == nullptr && envs.empty()))
    {
        result.message = fst.getFileName() + " is not an FL Sampler preset" + (plugin.isNotEmpty() ? " (it is for " + plugin + ")." : ".");
        return result;
    }

    auto& apvts = processor.apvts;
    auto set = [&apvts] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    // Start from Chan Sampler's defaults (FL's), then the sample, then the preset's values.
    for (auto* p : processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());

    if (auto* e = get (196))
    {
        const auto path = juce::String (juce::CharPointer_UTF16 ((const juce::CharPointer_UTF16::CharType*) e->data.getData()),
                                        e->data.getSize() / 2).trimCharactersAtEnd (juce::String::charToString (0));
        const auto file = resolveSamplePath (path, fst);
        if (file == juce::File())
            result.message = "Sample not found: " + path;
        else if (! processor.loadSample (file))
            result.message = processor.getLoadError();
    }

    // Levels (219): channel pan / volume (host side in FL), fine tune, Mod X / Y, filter type.
    float chanPan = 0.0f, chanGain = 1.0f;
    if (auto* e = get (219))
    {
        chanPan = (float) juce::jlimit (-1.0, 1.0, (e->i32 (0, 64) * old - 6400.0) / 6400.0);
        chanGain = volGain (e->i32 (4, 100) * old / 12800.0) / volGain (10000.0 / 12800.0);
        set (pid::fineTune, (float) e->i32 (8));
        if (e->has (24))
        {
            set (pid::cutoff, (float) e->i32 (12, 256) / 256.0f);
            set (pid::resonance, (float) e->i32 (16) / 256.0f);
            set (pid::filterType, (float) e->i32 (20));
        }
    }
    // FUNC Levels (229): pan, volume multiplier, -, Mod X, Mod Y. The channel's
    // own pan / volume are folded in (a plugin channel applies its own).
    {
        double pan = 0.0, vol = 1.0;
        if (auto* e = get (229))
        {
            pan = e->i32 (0) * old / 6400.0;
            vol = e->i32 (4, 128) * old / 12800.0;
            set (pid::xOfs, (float) e->i32 (12) / 256.0f);
            set (pid::yOfs, (float) e->i32 (16) / 256.0f);
        }
        set (pid::pan, (float) juce::jlimit (-1.0, 1.0, pan + chanPan));
        set (pid::volume, juce::jlimit (0.0f, 2.0f, volKnob (volGain (vol) * chanGain)));
    }

    // Parameters (215)
    if (auto* e = get (215))
    {
        set (pid::pcRemoveDC, e->byte (9) ? 1.0f : 0.0f);
        const int echoFlags = e->byte (10);
        set (pid::echoPing, (echoFlags & 2) ? 1.0f : 0.0f);
        set (pid::echoFat,  (echoFlags & 4) ? 1.0f : 0.0f);
        if (e->has (64))
        {
            set (pid::arpDir,   (float) e->i32 (40));
            set (pid::arpRange, (float) e->i32 (44, 1));
            set (pid::arpChord, (float) (e->i32 (48, -1) + 2));
            set (pid::arpTime,  (float) (e->i32 (52, 1024) - 450) / 997.0f);
            set (pid::arpGate,  (float) e->i32 (56, 48) / 48.0f);
            set (pid::arpSlide, e->byte (60) ? 1.0f : 0.0f);
        }
        set (pid::pcNormalize, e->byte (80) ? 1.0f : 0.0f);
        set (pid::pcReversePolarity, e->byte (81) ? 1.0f : 0.0f);
        if (e->has (84)) set (pid::declick, (float) e->byte (83));
        if (e->has (88)) set (pid::pcCrossfade, (float) e->u32 (84) / 1024.0f);
        if (e->has (92)) set (pid::pcTrim, (float) e->u32 (88) / 256.0f);
        if (e->has (96)) set (pid::arpRepeat, (float) e->i32 (92, 1));
        if (e->has (112))
        {
            set (pid::stretchTime, e->f32 (96, 0.0f) / 192.0f);
            set (pid::pitch, (float) e->i32 (100));
            set (pid::stretchMul, (float) std::pow (2.0, e->i32 (104) / 10000.0));
            // FL's stored method: -2 Stretch Pro, -1 Stretch, 0 Resample, 1 e3 generic,
            // 2 e3 mono, 3 Slice stretch, 4 Slice map, 5 Auto, 6..9 e2 generic / transient / mono / speech
            static const std::map<int, StretchMode> modes {
                { -2, StretchMode::stretchPro }, { -1, StretchMode::stretch }, { 0, StretchMode::resample },
                { 1, StretchMode::e3Generic }, { 2, StretchMode::e3Mono }, { 3, StretchMode::sliceStretch },
                { 4, StretchMode::sliceMap }, { 5, StretchMode::autoMode }, { 6, StretchMode::e2Generic },
                { 7, StretchMode::e2Transient }, { 8, StretchMode::e2Mono }, { 9, StretchMode::e2Speech } };
            const auto m = modes.find (e->i32 (108));
            set (pid::stretchMode, (float) (int) (m == modes.end() ? StretchMode::resample : m->second));
        }
        if (e->has (152))   // FL 2026: precomputed start / length and the start offset as doubles
        {
            set (pid::pcStart,  (float) e->f64 (128, 0.0));
            set (pid::pcLength, (float) e->f64 (136, 1.0));
            set (pid::startOffset, (float) e->f64 (144, 0.0));
        }
    }

    // Precomputed effects (word / dword events)
    if (auto* e = get (69)) set (pid::pcEq, ((float) e->value() - 128.0f) / 128.0f);
    if (auto* e = get (70))
    {
        const auto f = e->value();
        set (pid::pcFadeStereo, (f & 1) ? 1.0f : 0.0f);
        set (pid::pcReverse,    (f & 2) ? 1.0f : 0.0f);
        set (pid::pcBoostClip,  (f & 4) ? 1.0f : 0.0f);
        set (pid::pcSwapStereo, (f & 256) ? 1.0f : 0.0f);
    }
    if (auto* e = get (71)) set (pid::pcCut, (float) e->value() / 1024.0f);
    if (auto* e = get (74)) set (pid::pcBoost, (float) e->value() / 256.0f);
    if (auto* e = get (75)) set (pid::pcFadeOut, (float) e->value() / 1024.0f);
    if (auto* e = get (76)) set (pid::pcFadeIn, (float) e->value() / 1024.0f);
    if (auto* e = get (83)) set (pid::pcRes, (float) e->value() / 640.0f);
    if (auto* e = get (85)) set (pid::pcStereoDelay, ((float) e->value() - 2048.0f) / 2048.0f);
    if (auto* e = get (86)) set (pid::pcPogo, ((float) e->value() - 256.0f) / 256.0f);
    if (auto* e = get (131))
    {
        set (pid::pcRmMix,  (float) (e->value() & 0xffff) / 256.0f);
        set (pid::pcRmFreq, (float) (e->value() >> 16) / 256.0f);
    }
    if (auto* e = get (139))
    {
        set (pid::pcReverbType, (float) (e->value() >> 16));
        set (pid::pcReverb, (float) (e->value() & 0xffff) / 256.0f);
    }

    // Group: Cut / By; FL's Cut self sets both to the channel's own group.
    if (auto* e = get (132))
    {
        const int cut = (int) (e->value() & 0xffff), by = (int) (e->value() >> 16);
        if (cut > 0 && cut == by)
            set (pid::cutItself, 1.0f);
        else
        {
            set (pid::cutGroup, (float) juce::jlimit (0, 99, cut));
            set (pid::cutBy, (float) juce::jlimit (0, 99, by));
        }
    }

    // Root note (only stored when not C5)
    if (auto* e = get (135)) set (pid::rootNote, (float) juce::jlimit (0, 127, (int) e->value()));

    // Sample flags (143): 2 load regions, 4 load slice markers, 8 use loop points
    if (auto* e = get (143))
    {
        set (pid::loadRegions, (e->value() & 2) ? 1.0f : 0.0f);
        set (pid::loadSlices,  (e->value() & 4) ? 1.0f : 0.0f);
        set (pid::useLoop,     (e->value() & 8) ? 1.0f : 0.0f);
    }
    // Loop points (251), ping pong (20)
    if (auto* e = get (20)) set (pid::pingPong, e->value() ? 1.0f : 0.0f);
    if (auto* e = get (251); e != nullptr && e->has (20) && e->i32 (0) >= 2)
        if (const int length = processor.getSampleLength(); length > 0)
        {
            set (pid::loopStart, (float) e->i32 (4) / (float) length);
            set (pid::loopEnd,   (float) e->i32 (12) / (float) length);
        }

    // Echo delay (209) and its Mod X / Y (138: byte 2 = X, byte 0 = Y, centre 128)
    if (auto* e = get (209); e != nullptr && e->has (20))
    {
        set (pid::echoFeed,  (float) e->u32 (0) / 25600.0f);
        set (pid::echoPan,   (float) ((e->i32 (4) * old - 6400.0) / 6400.0));
        set (pid::echoPitch, (float) e->i32 (8));
        set (pid::echoCount, (float) e->i32 (12));
        set (pid::echoTime,  (float) e->i32 (16) / 48.0f);
    }
    if (auto* e = get (138))
    {
        set (pid::echoX, (float) (e->byte (2, 128) - 128) / 128.0f);
        set (pid::echoY, (float) (e->byte (0, 128) - 128) / 128.0f);
    }

    // Polyphony (221): max, slide, flags (1 mono, 2 porta)
    if (auto* e = get (221))
    {
        set (pid::maxPoly, (float) juce::jlimit (0, 64, e->i32 (0)));
        set (pid::slide, (float) e->i32 (4, 820) / 1660.0f);
        set (pid::mono,  (e->byte (8) & 1) ? 1.0f : 0.0f);
        set (pid::porta, (e->byte (8) & 2) ? 1.0f : 0.0f);
    }

    // Tracking (228: velocity, then key): mid, pan, mod X, mod Y
    for (size_t i = 0; i < tracking.size() && i < 2; ++i)
    {
        const auto* e = tracking[i];
        const bool vel = i == 0;
        if (vel) set (pid::velMid, (float) e->i32 (0, 100) / 128.0f);
        else     set (pid::keyMid, (float) juce::jlimit (0, 120, e->i32 (0, 60)));
        set (vel ? pid::velPan : pid::keyPan, (float) e->i32 (4) / 256.0f);
        set (vel ? pid::velX   : pid::keyX,   (float) e->i32 (8) / 256.0f);
        set (vel ? pid::velY   : pid::keyY,   (float) e->i32 (12) / 256.0f);
    }

    // Envelopes / LFOs (218 x5: Panning, Volume, Mod X, Mod Y, Pitch). Fields:
    // flags, on, delay, attack, hold, decay, sustain, release, amount, LFO delay,
    // attack, amount, speed, shape, then the attack / decay / release tensions.
    const char* prefixes[] = { pid::panPrefix, pid::volPrefix, pid::modXPrefix, pid::modYPrefix, pid::pitchPrefix };
    for (size_t i = 0; i < envs.size() && i < 5; ++i)
    {
        const auto* e = envs[i];
        const auto p = [pre = prefixes[i]] (const char* s) { return pid::env (pre, s); };
        auto field = [e] (int k, int fallback) { return e->i32 ((size_t) (4 * k), fallback); };
        const int flags = field (0, 0);
        if (i != 0)   // FL's Panning page has no envelope
        {
            set (p ("EnvOn"), field (1, 0) ? 1.0f : 0.0f);
            set (p ("Del"),  envPos (field (2, 100)));
            set (p ("Att"),  envPos (field (3, 20000)));
            set (p ("Hold"), envPos (field (4, 20000)));
            set (p ("Dec"),  envPos (field (5, 30000)));
            set (p ("Sus"),  (float) field (6, 50) / 128.0f);
            set (p ("Rel"),  envPos (field (7, 20000)));
            if (i >= 2)
                set (p ("EnvAmt"), (float) field (8, 0) / 128.0f);
            set (p ("AttT"), (float) field (14, 0) / 128.0f);
            set (p ("DecT"), (float) field (15, 0) / 128.0f);
            set (p ("RelT"), (float) field (16, i == 1 ? -101 : 0) / 128.0f);
            set (p ("EnvTempo"), (flags & 1) ? 1.0f : 0.0f);
        }
        set (p ("LfoDel"),   envPos (field (9, 100)));
        set (p ("LfoAtt"),   envPos (field (10, 20000)));
        set (p ("LfoAmt"),   (float) field (11, 0) / 128.0f);
        set (p ("LfoSpeed"), (float) (field (12, 32950) - 200) / 65336.0f);
        set (p ("LfoShape"), (float) field (13, 0));
        set (p ("LfoTempo"),  (flags & 2) ? 1.0f : 0.0f);
        set (p ("LfoGlobal"), (flags & 32) ? 1.0f : 0.0f);
    }

    result.ok = true;
    return result;
}
