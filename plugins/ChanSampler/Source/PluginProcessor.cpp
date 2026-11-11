#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

namespace cs
{

ChanSamplerProcessor::ChanSamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout (loopTextLength)),
      history (*this, [this] { return getRaw()->file; },
               [this] (const juce::File& f) { if (f == juce::File()) removeSample(); else if (! loadSample (f)) setMissingSample (f); },
               [this] { return apvts.state.getProperty ("presetName", "").toString(); },
               [this] (const juce::String& name) { apvts.state.setProperty ("presetName", name, nullptr); },
               dynamic_cast<HistoryStepParameter*> (apvts.getParameter (id::historyStep)),
               wrapperType != wrapperType_Standalone && wrapperType != wrapperType_Undefined)   // a plugin: the host's history
{
    formatManager.registerBasicFormats();
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            rawParams[std::string_view (ranged->paramID.toRawUTF8())] = apvts.getRawParameterValue (ranged->paramID);

    for (auto& id : precomputeParamIDs())
        apvts.addParameterListener (id, this);
    for (auto* id : stretchParamIDs)
        apvts.addParameterListener (id, this);

    publish (dsp::buildProcessedSample (*raw, readPrecomputeSettings()));
    rebuildThread.startThread();
    startTimer (50);
}

ChanSamplerProcessor::~ChanSamplerProcessor()
{
    stopTimer();
    cutgroups::releaseNumber (instanceNumber);
    for (auto& id : precomputeParamIDs())
        apvts.removeParameterListener (id, this);
    for (auto* id : stretchParamIDs)
        apvts.removeParameterListener (id, this);
    rebuildThread.signalThreadShouldExit();
    rebuildThread.notify();
    rebuildThread.stopThread (4000);
}

bool ChanSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ChanSamplerProcessor::prepareToPlay (double sampleRate, int)
{
    // Files at another rate are converted to the playback rate (as in FL).
    if (hostRate.exchange (sampleRate) != sampleRate)
        rebuildNow();
    engine.prepare (sampleRate);
    if (needsStretchers())
        engine.ensureStretchers();
    cutOffsets.reserve (256);
    midiEvents.reserve (2048);
    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (dsp::outputLevel (param (id::output)));
}

void ChanSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Offline (bounce / export): never start on a sample whose rebuild is
    // still running; waiting is fine when not in real time.
    if (isNonRealtime() && (rebuildPending.load() || building.load()))
        waitForPendingRebuilds();

    // Notes from the on-screen keyboard (preview) join the host's MIDI.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    // Sustain pedal (CC64): note-offs wait while it is down. FL handles the
    // pedal itself; other hosts send it to the instrument.
    if (sustainDown || ! midi.isEmpty())
    {
        juce::MidiBuffer filtered;
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isSustainPedalOn())
            {
                sustainDown = true;
            }
            else if (msg.isSustainPedalOff())
            {
                sustainDown = false;
                for (int n = 0; n < 128; ++n)
                    if (sustained[(size_t) n])
                    {
                        sustained[(size_t) n] = false;
                        filtered.addEvent (juce::MidiMessage::noteOff (msg.getChannel(), n), m.samplePosition);
                    }
            }
            else if (msg.isNoteOff() && sustainDown)
            {
                sustained[(size_t) msg.getNoteNumber()] = true;
            }
            else
            {
                if (msg.isNoteOn())
                    sustained[(size_t) msg.getNoteNumber()] = false;
                if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    sustained.fill (false);
                filtered.addEvent (msg, m.samplePosition);
            }
        }
        midi.swapWith (filtered);
    }

    {
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked())
            audioThreadSample = currentSample;
    }

    auto params = readEngineParams();
    int64_t hostTime = -1;   // timeline position while playing, for the cut groups
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                params.bpm = *bpm;
            if (pos->getIsPlaying())
            {
                if (auto t = pos->getTimeInSamples())
                    hostTime = *t;
                if (auto sec = pos->getTimeInSeconds())
                    params.songSeconds = *sec;   // Global LFO phase
            }
        }
    displayBpm.store (params.bpm);
    applyTempo (params);
    // An export can't wait for the timer: make the stretchers here when not in real time.
    if (isNonRealtime() && dsp::Engine::usesStretcher (params.stretchMode) && ! engine.hasStretchers())
        engine.ensureStretchers();
    if (const int n = previewOff.exchange (-1); n >= 0)
        engine.previewNote (n, false, params);
    if (const int n = previewOn.exchange (-1); n >= 0 && audioThreadSample != nullptr)
        engine.previewNote (n, true, params);
    // Group: cut by other instances' notes, and post this block's note starts.
    const int cutBy = (int) param (id::cutBy), cutGroup = (int) param (id::cutGroup);
    cutReader.collect (cutBy, hostTime, buffer.getNumSamples(), cutOffsets);
    midiEvents.clear();
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        dsp::MidiEvent e;
        e.position = m.samplePosition;
        if (msg.isNoteOn())
        {
            e.kind = dsp::MidiEvent::Kind::noteOn;
            e.note = msg.getNoteNumber();
            e.velocity = msg.getFloatVelocity();
        }
        else if (msg.isNoteOff())
        {
            e.kind = dsp::MidiEvent::Kind::noteOff;
            e.note = msg.getNoteNumber();
        }
        else if (msg.isPitchWheel())
        {
            e.kind = dsp::MidiEvent::Kind::pitchWheel;
            e.pitchWheel = msg.getPitchWheelValue();
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            e.kind = dsp::MidiEvent::Kind::allNotesOff;
        }
        midiEvents.push_back (e);
    }
    engine.render (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), midiEvents.data(),
                   (int) midiEvents.size(), audioThreadSample.get(), params, cutOffsets.empty() ? nullptr : &cutOffsets);
    if (cutGroup > 0)
        for (const auto start : engine.getNoteStarts())
            cutgroups::post (instanceId, cutGroup, hostTime < 0 ? -1 : hostTime + start);
    engine.updatePlayhead (audioThreadSample.get());

    // OUT: FL's channel volume knob, 0.5 in total at its default (see defaultOutputPosition).
    outputGain.setTargetValue (dsp::outputLevel (param (id::output)));
    outputGain.applyGain (buffer, buffer.getNumSamples());
}

//==============================================================================
dsp::PrecomputeSettings ChanSamplerProcessor::readPrecomputeSettings() const
{
    dsp::PrecomputeSettings s;
    s.removeDC        = param (id::pcRemoveDC) > 0.5f;
    s.reversePolarity = param (id::pcReversePolarity) > 0.5f;
    s.normalize       = param (id::pcNormalize) > 0.5f;
    s.fadeStereo      = param (id::pcFadeStereo) > 0.5f;
    s.reverse         = param (id::pcReverse) > 0.5f;
    s.swapStereo      = param (id::pcSwapStereo) > 0.5f;
    s.start       = param (id::pcStart);
    s.length      = param (id::pcLength);
    s.fadeIn      = param (id::pcFadeIn);
    s.fadeOut     = param (id::pcFadeOut);
    s.crossfade   = param (id::pcCrossfade);
    s.trim        = param (id::pcTrim);
    s.boost       = param (id::pcBoost);
    s.boostClip   = param (id::pcBoostClip) > 0.5f;
    s.eq          = param (id::pcEq);
    s.rmMix       = param (id::pcRmMix);
    s.rmFreq      = param (id::pcRmFreq);
    s.cut         = param (id::pcCut);
    s.res         = param (id::pcRes);
    s.reverbType  = (int) param (id::pcReverbType);
    s.reverb      = param (id::pcReverb);
    s.stereoDelay = param (id::pcStereoDelay);
    s.pogo        = param (id::pcPogo);
    s.useLoop     = param (id::useLoop) > 0.5f;
    s.loopStart   = param (id::loopStart);
    s.loopEnd     = param (id::loopEnd);
    s.targetRate  = hostRate.load();
    s.loadRegions = param (id::loadRegions) > 0.5f;
    s.loadSlices  = param (id::loadSlices) > 0.5f;
    return s;
}

// Tempo-dependent envelope timing (measured in FL): envelopes run on the song's
// ticks; with an envelope's Tempo switch on, its times are for 120 BPM.
void ChanSamplerProcessor::applyTempo (dsp::EngineParams& p)
{
    const auto bpm = juce::jmax (1.0, p.bpm);
    for (auto* t : { &p.panT, &p.volT, &p.xT, &p.yT, &p.pitchT })
    {
        auto& e = t->env;
        e.tick = dsp::tickSeconds (bpm);
        if (t->envTempo)
        {
            const auto k = (float) (120.0 / bpm);
            e.delay *= k; e.attack *= k; e.hold *= k; e.decay *= k; e.release *= k;
        }
    }
}

dsp::EngineParams ChanSamplerProcessor::readEngineParams() const
{
    auto target = [this] (const char* prefix)
    {
        // prefix + name in a local buffer (no allocation on the audio thread)
        auto id = [prefix] (const char* s)
        {
            std::array<char, 48> buf {};
            const auto n = std::strlen (prefix), m = std::strlen (s);
            jassert (n + m < buf.size());
            std::memcpy (buf.data(), prefix, n);
            std::memcpy (buf.data() + n, s, juce::jmin (m, buf.size() - 1 - n));
            return buf;
        };
        auto get = [this, &id] (const char* s) { return rawParam (id (s).data())->load(); };
        dsp::EngineParams::Target t;
        if (rawParam (id ("EnvOn").data()) == nullptr)
        {
            t.envOn = false;   // no envelope on this target (Panning)
        }
        else
        {
            t.envOn    = get ("EnvOn") > 0.5f;
            t.envTempo = get ("EnvTempo") > 0.5f;
            if (auto* amt = rawParam (id ("EnvAmt").data()))
                t.envAmt = amt->load();
            t.env.delay    = dsp::flEnvSeconds (get ("Del"));
            // Measured: the minimum raw attack (100, position 0) is instant.
            const auto att = get ("Att");
            t.env.attack   = att <= 0.5f / 65436.0f ? 0.0f : dsp::flEnvSeconds (att);
            t.env.hold     = dsp::flEnvSeconds (get ("Hold"));
            t.env.decay    = dsp::flEnvSeconds (get ("Dec"));
            t.env.sustain  = get ("Sus");
            t.env.release  = dsp::flEnvSeconds (get ("Rel"));
            t.env.attackK  = dsp::flTensionToK (get ("AttT"));
            t.env.decayK   = dsp::flTensionToK (get ("DecT"));
            t.env.releaseK = dsp::flTensionToK (get ("RelT"));
        }
        t.lfo.shape   = (dsp::LfoShape) (int) get ("LfoShape");
        t.lfo.periodT130 = dsp::flLfoPeriodT130 (get ("LfoSpeed"));
        t.lfo.amount  = get ("LfoAmt");
        t.lfo.delayT130 = dsp::flLfoDelayT130 (get ("LfoDel"));
        t.lfo.tempo   = get ("LfoTempo") > 0.5f;
        t.lfo.global  = get ("LfoGlobal") > 0.5f;
        t.lfo.attack  = get ("LfoAtt") > 0.0f ? dsp::flEnvSeconds (get ("LfoAtt")) : 0.0f;
        return t;
    };

    dsp::EngineParams p;
    p.pitchRange = (int) param (id::pitchRange);

    p.startOffset = param (id::startOffset);
    p.useLoop     = param (id::useLoop) > 0.5f;
    p.loopStart   = param (id::loopStart);
    p.loopEnd     = param (id::loopEnd);
    p.pingPong    = param (id::pingPong) > 0.5f;
    p.declick     = (dsp::DeclickMode) (int) param (id::declick);

    p.rootNote = (int) param (id::rootNote);
    p.fineTune = param (id::fineTune);
    p.pitch    = param (id::pitch) * 0.01f;            // cents
    p.stretchMode = (dsp::StretchMode) (int) param (id::stretchMode);
    p.stretchMul  = param (id::stretchMul);
    p.stretchTime = param (id::stretchTime);
    p.formant     = param (id::formant);
    p.stretchMode = dsp::resolveStretchMode (p.stretchMode, param (id::pitch), p.stretchMul, p.stretchTime);

    p.panT   = target (id::panPrefix);
    p.volT   = target (id::volPrefix);
    p.xT     = target (id::modXPrefix);
    p.yT     = target (id::modYPrefix);
    p.pitchT = target (id::pitchPrefix);

    p.filterType = (dsp::FilterType) (int) param (id::filterType);
    p.cutoff     = param (id::cutoff);
    p.resonance  = param (id::resonance);

    p.volume = dsp::flVolumeGain (param (id::volume));
    p.pan    = param (id::pan);
    p.xOfs   = param (id::xOfs);
    p.yOfs   = param (id::yOfs);

    p.velPan = param (id::velPan);
    p.velX   = param (id::velX);
    p.velY   = param (id::velY);
    p.velMid = param (id::velMid);
    p.keyPan = param (id::keyPan);
    p.keyX   = param (id::keyX);
    p.keyY   = param (id::keyY);
    p.keyMid = (int) param (id::keyMid);

    p.slideTicks = dsp::flSlideTicks (param (id::slide));
    p.mono      = param (id::mono) > 0.5f;
    p.porta     = param (id::porta) > 0.5f;
    p.cutItself = param (id::cutItself) > 0.5f;

    p.arpDir    = (int) param (id::arpDir);
    p.arpRange  = (int) param (id::arpRange);
    p.arpRepeat = (int) param (id::arpRepeat);
    p.arpTime   = (float) dsp::flArpSteps (param (id::arpTime));   // steps, 0 = hold
    p.arpGate   = param (id::arpGate);
    p.arpSlide  = param (id::arpSlide) > 0.5f;
    p.arpChord  = (int) param (id::arpChord);

    p.echoFeed  = param (id::echoFeed);
    p.echoPan   = param (id::echoPan);
    p.echoX     = param (id::echoX);
    p.echoY     = param (id::echoY);
    p.echoPitch = param (id::echoPitch) * 0.01f;        // cents
    p.echoTime  = param (id::echoTime);
    p.echoCount = (int) param (id::echoCount);
    p.echoPing  = param (id::echoPing) > 0.5f;
    p.echoFat   = param (id::echoFat) > 0.5f;
    return p;
}

//==============================================================================
void ChanSamplerProcessor::resetParameters()
{
    const History::Transaction edit (history, "Reset");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != id::historyStep && rp->getValue() != rp->getDefaultValue())
                setParameterValue (*rp, rp->getDefaultValue());
}

juce::File ChanSamplerProcessor::userFolder (const juce::String& name)
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Chan Sampler").getChildFile (name);
}

void ChanSamplerProcessor::applyLoopLink (const juce::String& changedId)
{
    // FL's linked loop knobs (measured): START moves alone while it is below
    // END and pushes END to START + 1 sample when it passes it; END moves
    // alone but stops at START (START = END is allowed). END = 0 means no loop.
    // Applied to the user's edits only (knobs, menu, the sample view), so host
    // automation sets exactly the values it shows; the engine treats an END
    // below START as START.
    const auto start = param (id::loopStart), end = param (id::loopEnd);
    const auto oneSample = 1.0f / (float) juce::jmax (1, getSampleLength());
    float newEnd = end;
    if (changedId == id::loopStart && start > 0.0f && start > end)
        newEnd = juce::jmin (1.0f, start + oneSample);
    else if (changedId == id::loopEnd && end < start)
        newEnd = start;
    if (newEnd != end)
        setParameterAsGesture (*apvts.getParameter (id::loopEnd), newEnd);
}

bool ChanSamplerProcessor::needsStretchers() const
{
    const auto mode = dsp::resolveStretchMode ((dsp::StretchMode) (int) param (id::stretchMode), param (id::pitch), param (id::stretchMul), param (id::stretchTime));
    return dsp::Engine::usesStretcher (mode);
}

void ChanSamplerProcessor::parameterChanged (const juce::String& parameterID, float)
{
    // A stretch mode switched on from the editor gets its stretchers at once
    // (host automation runs on the audio thread: the timer makes them then).
    if (std::any_of (std::begin (stretchParamIDs), std::end (stretchParamIDs), [&] (const char* id) { return parameterID == id; }))
    {
        if (juce::MessageManager::existsAndIsCurrentThread() && needsStretchers())
            engine.ensureStretchers();
        return;
    }

    // Loop points only touch the sample data when a loop crossfade is baked in.
    const bool loopParam = parameterID == id::useLoop || parameterID == id::loopStart || parameterID == id::loopEnd;
    if (loopParam && param (id::pcCrossfade) <= 0.0f)
        return;

    rebuildPending = true;
    rebuildThread.notify();
}

void ChanSamplerProcessor::timerCallback()
{
    // FL: switching Cut self on sets Cut and By to the channel's own number, so
    // other channels can be cut by it too (here the instance number); off
    // clears them again if they still hold it.
    const int cutSelf = param (id::cutItself) > 0.5f ? 1 : 0;
    if (lastCutSelf >= 0 && cutSelf != lastCutSelf)
    {
        auto set = [this] (const char* id, float v) { setParameterAsGesture (*apvts.getParameter (id), v); };
        if (cutSelf == 1)
        {
            set (id::cutGroup, (float) instanceNumber);
            set (id::cutBy, (float) instanceNumber);
        }
        else
        {
            if ((int) param (id::cutGroup) == instanceNumber) set (id::cutGroup, 0.0f);
            if ((int) param (id::cutBy) == instanceNumber)    set (id::cutBy, 0.0f);
        }
    }
    lastCutSelf = cutSelf;

    if (! engine.hasStretchers() && needsStretchers())
        engine.ensureStretchers();

    const juce::ScopedLock sl (poolLock);
    releasePool.erase (std::remove_if (releasePool.begin(), releasePool.end(), [] (const auto& p) { return p.use_count() == 1; }),
                       releasePool.end());
}

void ChanSamplerProcessor::rebuildSample()
{
    // Build from a snapshot of the raw sample without holding the pool lock.
    const std::lock_guard<std::mutex> one (rebuildMutex);
    const auto source = getRaw();
    auto settings = readPrecomputeSettings();
    if (std::abs (settings.targetRate - source->sampleRate) <= 0.5)
        settings.targetRate = 0.0;   // the file is already at the playback rate: nothing to convert
    if (source == builtFrom && settings == builtWith)
        return;
    publish (dsp::buildProcessedSample (*source, settings, source));
    builtFrom = source;
    builtWith = settings;
    sendChangeMessage();
}

void ChanSamplerProcessor::publish (dsp::SampleData::Ptr newSample)
{
    {
        const juce::ScopedLock sl (poolLock);
        releasePool.push_back (newSample);
    }
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    currentSample = newSample;
}

dsp::SampleData::Ptr ChanSamplerProcessor::getSampleForDisplay() const
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return currentSample;
}

//==============================================================================
namespace
{
    // What a RIFF / WAVE file says about itself, whichever reader decodes it
    // (FL's compressed WAVs hold Ogg Vorbis, which the Ogg reader reads
    // without the WAV's chunks): the first "fmt " chunk's format tag, the
    // first "smpl" loop, the root note ("inst" / "smpl"), the ACID tempo, the
    // cue points (FL's "regions") and
    // the ACID "strc" chunk (FL's "slice markers": a 28-byte header with the
    // entry count, then 24-byte entries whose second field is the position in
    // frames). One pass over the chunks.
    struct WavChunks
    {
        bool riffWave = false;
        juce::uint16 formatTag = 1;   // 1 PCM, 3 float, 0xfffe extensible, others compressed
        int loopStart = -1, loopEnd = -1;
        int instNote = -1, unityNote = -1;   // as stored (-1: no chunk)
        double acidTempo = 0;
        std::vector<int> regions, slices;
    };

    WavChunks readWavChunks (const juce::File& file)
    {
        WavChunks w;
        juce::FileInputStream in (file);
        if (! in.openedOk() || in.readInt() != (int) juce::ByteOrder::littleEndianInt ("RIFF"))
            return w;
        in.readInt();
        if (in.readInt() != (int) juce::ByteOrder::littleEndianInt ("WAVE"))
            return w;
        w.riffWave = true;
        bool formatRead = false;
        while (! in.isExhausted())
        {
            const auto id = in.readInt();
            const auto size = (juce::uint32) in.readInt();
            const auto next = in.getPosition() + size + (size & 1);
            if (id == (int) juce::ByteOrder::littleEndianInt ("fmt ") && ! formatRead)
            {
                w.formatTag = (juce::uint16) in.readShort();
                formatRead = true;
            }
            else if (id == (int) juce::ByteOrder::littleEndianInt ("smpl") && size >= 36)
            {
                // 36-byte header (MIDI unity note at 12, loop count at 28), then
                // 24-byte loops: id, type, start, end, ...
                in.setPosition (in.getPosition() + 12);
                w.unityNote = in.readInt();
                in.setPosition (in.getPosition() + 12);
                if (in.readInt() > 0 && size >= 60)
                {
                    in.setPosition (in.getPosition() + 4 + 8);
                    w.loopStart = in.readInt();
                    w.loopEnd = in.readInt();
                }
            }
            else if (id == (int) juce::ByteOrder::littleEndianInt ("inst") && size >= 7)
            {
                w.instNote = (int) (juce::uint8) in.readByte();   // the unshifted note
            }
            else if (id == (int) juce::ByteOrder::littleEndianInt ("acid") && size >= 24)
            {
                in.setPosition (in.getPosition() + 20);   // flags, root note, two reserved, beats, meter
                w.acidTempo = (double) in.readFloat();
            }
            else if (id == (int) juce::ByteOrder::littleEndianInt ("cue ") && size >= 4)
            {
                const int count = in.readInt();
                for (int i = 0; i < count && (juce::uint32) (4 + 24 * (i + 1)) <= size; ++i)
                {
                    juce::uint32 f[6];
                    for (auto& v : f) v = (juce::uint32) in.readInt();
                    w.regions.push_back ((int) f[5]);   // sample offset
                }
            }
            else if (id == (int) juce::ByteOrder::littleEndianInt ("strc") && size >= 28)
            {
                const auto header = (juce::uint32) in.readInt();
                const int count = in.readInt();
                in.setPosition (next - size - (size & 1) + header);
                for (int i = 0; i < count && header + (juce::uint32) (24 * (i + 1)) <= size; ++i)
                {
                    juce::uint32 f[6];
                    for (auto& v : f) v = (juce::uint32) in.readInt();
                    if (f[0] != 2)   // the first entry is a header entry
                        w.slices.push_back ((int) f[1]);
                }
            }
            if (! in.setPosition (next))
                break;
        }
        return w;
    }
}

bool ChanSamplerProcessor::reloadSample()
{
    const History::ScopedSuspend noHistory (history);
    const auto file = getSampleFile();
    if (! file.existsAsFile())
        return false;
    const auto saved = apvts.copyState();
    if (! loadSample (file))
        return false;
    apvts.replaceState (saved);
    rebuildSample();
    return true;
}

void ChanSamplerProcessor::reloadIfFileChanged()
{
    const auto r = getRaw();
    if (! r->file.existsAsFile())
        return;
    const std::pair<juce::int64, juce::int64> now { r->file.getLastModificationTime().toMilliseconds(), r->file.getSize() };
    if (now == std::make_pair (r->fileTime, r->fileSize))
    {
        changedStamp = {};
        return;
    }
    if (now != changedStamp)   // wait one more call: the editor may still be writing
    {
        changedStamp = now;
        return;
    }
    if (now != failedStamp && ! reloadSample())
        failedStamp = now;     // e.g. saved in a format we cannot read: try again on the next change
}

juce::MemoryBlock ChanSamplerProcessor::getSampleAsWav() const
{
    const auto r = getRaw();
    juce::MemoryBlock block;
    if (r->audio.getNumSamples() == 0)
        return block;
    const int bits = r->bitsPerSample <= 16 ? 16 : r->bitsPerSample <= 24 ? 24 : 32;   // 32 = float
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (block, false);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (r->sampleRate).withNumChannels (r->audio.getNumChannels()).withBitsPerSample (bits));
    if (writer == nullptr)
        return {};
    writer->writeFromAudioSampleBuffer (juceView (r->audio), 0, r->audio.getNumSamples());
    writer.reset();   // writes the header's final sizes
    return block;
}

bool ChanSamplerProcessor::loadSample (const juce::File& file)
{
    const History::Transaction edit (history, "Load sample: " + file.getFileName());
    loadError = "Could not read " + file.getFileName() + ": not a supported audio file, or the file is damaged.";
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    // Broken or hostile headers: refuse what no real file has, never trust a
    // length the file cannot hold (an uncompressed WAV / AIFF stores every
    // frame), and refuse what would need more memory than a sample can use,
    // also once converted to the playback rate (checked with
    // nulltest/check_corrupt_files.py).
    const auto rate = reader->sampleRate;
    if (reader->numChannels < 1 || reader->numChannels > 64 || ! (rate >= 1000.0 && rate <= 768000.0) || reader->bitsPerSample > 64)
        return false;
    juce::int64 frames = reader->lengthInSamples;
    const auto formatName = reader->getFormatName();
    if ((formatName.containsIgnoreCase ("WAV") || formatName.containsIgnoreCase ("AIFF")) && reader->bitsPerSample >= 8)
        frames = juce::jmin (frames, file.getSize() / ((juce::int64) reader->numChannels * (reader->bitsPerSample / 8)));
    const auto playRate = hostRate.load() > 0.0 ? hostRate.load() : rate;
    constexpr double maxSamples = 1 << 28;   // 1 GB of float audio per copy
    if (frames <= 0)
        return false;
    if ((double) frames * juce::jmax (1.0, playRate / rate) * juce::jmin (2, (int) reader->numChannels) > maxSamples)
    {
        loadError = file.getFileName() + " is too long to load (" + juce::String ((double) frames / rate / 60.0, 1)
                  + " minutes). Chan Sampler holds samples in memory, up to about "
                  + juce::String (juce::roundToInt (maxSamples / juce::jmin (2, (int) reader->numChannels) / juce::jmax (rate, playRate) / 60.0)) + " minutes at this rate.";
        return false;
    }
    loadError.clear();

    RawSample r;
    r.file = file;
    r.fileTime = file.getLastModificationTime().toMilliseconds();
    r.fileSize = file.getSize();
    r.sampleRate = rate;
    r.bitsPerSample = (int) reader->bitsPerSample;
    const int numChannels = juce::jlimit (1, 2, (int) reader->numChannels);
    const int length = (int) frames;
    r.audio.setSize (numChannels, length);
    auto into = juceView (r.audio);
    reader->read (&into, 0, length, 0, true, numChannels > 1);

    auto wav = readWavChunks (file);
    if (file.hasFileExtension (".wav;.wave"))
    {
        r.regionMarkers = std::move (wav.regions);
        r.sliceMarkers = std::move (wav.slices);
    }
    // FL keeps 16-bit PCM as 16-bit and decodes everything else (24 / 32-bit,
    // float, compressed, also compressed audio inside a WAV) to 32-bit float:
    // its sample view badge shows 16 or 32 (measured).
    {
        const bool pcmContainer = formatName.containsIgnoreCase ("WAV") || formatName.containsIgnoreCase ("AIFF") || formatName.containsIgnoreCase ("FLAC");
        const auto wavTag = wav.formatTag;
        const bool compressedWav = wav.riffWave && wavTag != 1 && wavTag != 3 && wavTag != 0xfffe;
        r.heldBits = pcmContainer && ! compressedWav && ! reader->usesFloatingPointData && reader->bitsPerSample <= 16 ? 16 : 32;
        // For the properties window
        auto container = wav.riffWave ? juce::String ("WAV") : formatName.upToFirstOccurrenceOf (" file", false, true).trim();
        if (compressedWav)
            r.format = container + ", compressed (" + (wavTag == 0x674f || wavTag == 0x6750 || wavTag == 0x6751 ? juce::String ("Ogg Vorbis")
                                                       : wavTag == 0x55 ? juce::String ("MP3")
                                                       : "format 0x" + juce::String::toHexString ((int) wavTag)) + ")";
        else if (pcmContainer)
            r.format = container + ", " + juce::String (reader->bitsPerSample) + "-bit " + (reader->usesFloatingPointData ? "float" : "PCM");
        else
            r.format = container;
    }

    // Embedded loop points (WAV "smpl" chunk) auto-populate the loop controls.
    const auto& md = reader->metadataValues;
    if (md.getValue ("NumSampleLoops", "0").getIntValue() > 0)
    {
        r.embeddedLoopStart = md.getValue ("Loop0Start", "-1").getIntValue();
        r.embeddedLoopEnd   = md.getValue ("Loop0End",   "-1").getIntValue();
    }
    else if (wav.loopStart >= 0)   // a compressed WAV (FL's own samples): read from the chunks
    {
        r.embeddedLoopStart = wav.loopStart;
        r.embeddedLoopEnd   = wav.loopEnd;
    }
    r.fileTempo = juce::jmax (0.0, md.getValue (juce::WavAudioFormat::acidTempo, juce::String (wav.acidTempo)).getDoubleValue());

    {
        const juce::ScopedLock sl (poolLock);
        raw = std::make_shared<const RawSample> (std::move (r));
    }
    loopTextLength.store (length);   // loop knob texts are sample positions

    const auto loaded = getRaw();
    // FL keeps a tempo with each sample (Detect tempo); an ACID WAV brings its own.
    setSampleTempo (loaded->fileTempo);
    auto set = [this] (const char* id, float value)
    {
        auto* p = apvts.getParameter (id);
        setParameterValue (*p, p->convertTo0to1 (value));
    };
    // The root note comes with the sample (measured in FL by loading
    // files into its Sampler): the "inst" chunk's note, else the "smpl"
    // chunk's MIDI unity note when it is a note (0..127; some files hold
    // note x 256 there, which FL ignores), else C5. A restored project or
    // preset puts its own root back afterwards.
    {
        const auto valid = [] (int n) { return n >= 0 && n <= 127; };
        const int stored = valid (wav.instNote) ? wav.instNote : (valid (wav.unityNote) ? wav.unityNote : 60);
        set (id::rootNote, (float) (file.hasFileExtension (".wav;.wave") ? stored : 60));
    }
    // A file's own loop points become the loop markers; Use loop points is
    // left as the user set it (FL does not switch it on).
    if (loaded->embeddedLoopStart >= 0 && loaded->embeddedLoopEnd > loaded->embeddedLoopStart)
    {
        set (id::startOffset, 0.0f);
        set (id::loopStart, (float) loaded->embeddedLoopStart / (float) length);
        set (id::loopEnd,   (float) loaded->embeddedLoopEnd / (float) length);   // END is inclusive, as in the file
    }
    else
    {
        // A new sample starts clean (no Start or Loop markers in the view):
        // start offset and loop points back to 0. A restored project puts
        // its own values back afterwards (setStateInformation).
        set (id::startOffset, 0.0f);
        set (id::loopStart, 0.0f);
        set (id::loopEnd, 0.0f);
    }

    rebuildSample();
    return true;
}

void ChanSamplerProcessor::removeSample()
{
    const History::Transaction edit (history, "Remove sample");
    {
        const juce::ScopedLock sl (poolLock);
        raw = std::make_shared<const RawSample>();
    }
    loopTextLength.store (0);
    rebuildSample();
}

void ChanSamplerProcessor::setMissingSample (const juce::File& file)
{
    RawSample r;
    r.file = file;
    r.missing = true;
    {
        const juce::ScopedLock sl (poolLock);
        raw = std::make_shared<const RawSample> (std::move (r));
    }
    loopTextLength.store (0);
    rebuildSample();
}

juce::String ChanSamplerProcessor::getSampleInfo() const
{
    const auto r = getRaw();
    if (r->audio.getNumSamples() == 0)
        return "No sample loaded";

    return juce::String (r->sampleRate, 0) + " Hz | "
         + juce::String (r->bitsPerSample) + "-bit | "
         + (r->audio.getNumChannels() > 1 ? "Stereo" : "Mono") + " | "
         + juce::String (r->audio.getNumSamples() / r->sampleRate, 2) + " s";
}

//==============================================================================
void ChanSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("samplePath", getRaw()->file.getFullPathName(), nullptr);
    state.setProperty ("stateVersion", currentStateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChanSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // A project loading is the host's doing, not an edit: the history starts afresh.
    const History::ScopedSuspend noHistory (history);
    history.clear();
    auto state = juce::ValueTree::fromXml (*xml);
    const juce::String path = state.getProperty ("samplePath").toString();
    upgradeState (state);
    apvts.replaceState (state);
    lastCutSelf = param (id::cutItself) > 0.5f ? 1 : 0;   // a restored Cut self keeps its saved groups

    // The project's sample, or none: never the sample that was here before.
    if (path.isEmpty())
    {
        removeSample();
    }
    else
    {
        // Restore the project's own loop points, not the file's embedded ones.
        const auto savedLoop = apvts.copyState();
        const juce::File file (path);
        if (file.existsAsFile() && loadSample (file))
        {
            apvts.replaceState (savedLoop);
            rebuildSample();
        }
        else
        {
            setMissingSample (file);   // the project's settings stay as saved
        }
    }
    if (needsStretchers())
        engine.ensureStretchers();
}

// States before version 2 saved OUT as a trim in dB; it is FL's channel volume knob now.
void ChanSamplerProcessor::upgradeState (juce::ValueTree& state)
{
    if ((int) state.getProperty ("stateVersion", 1) < 2)
        if (auto out = state.getChildWithProperty ("id", id::output); out.isValid())
            out.setProperty ("value", outputPositionForOldDb ((float) out.getProperty ("value")), nullptr);
}

void ChanSamplerProcessor::loadPreset (juce::ValueTree state, const juce::String& name)
{
    // One edit (a project loading is the host's, setStateInformation): the
    // preset's sample, then every parameter as saved (a missing one at its
    // default), so the file's own root note and loop points give way to the preset's.
    const History::Transaction edit (history, "Preset: " + name);
    upgradeState (state);
    const juce::String path = state.getProperty ("samplePath").toString();
    if (path.isEmpty())
        removeSample();
    else if (const juce::File file (path); file != getSampleFile() && ! (file.existsAsFile() && loadSample (file)))
        setMissingSample (file);
    for (auto* p : getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || rp->getParameterID() == id::historyStep)
            continue;
        const auto saved = state.getChildWithProperty ("id", rp->getParameterID());
        const auto value = saved.isValid() ? rp->convertTo0to1 ((float) saved.getProperty ("value")) : rp->getDefaultValue();
        if (rp->getValue() != value)
            setParameterValue (*rp, value);
    }
    lastCutSelf = param (id::cutItself) > 0.5f ? 1 : 0;   // the preset's Cut and By stay as saved
    // The rest of what it saved (the sample's tempo, the view), but not the window's size
    for (int i = 0; i < state.getNumProperties(); ++i)
        if (const auto key = state.getPropertyName (i); key != juce::Identifier ("uiWidth") && key != juce::Identifier ("samplePath")
                                                        && key != juce::Identifier ("stateVersion"))
            apvts.state.setProperty (key, state.getProperty (key), nullptr);
    if (needsStretchers())
        engine.ensureStretchers();
}

juce::AudioProcessorEditor* ChanSamplerProcessor::createEditor()
{
    return new ChanSamplerEditor (*this);
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cs::ChanSamplerProcessor();
}
