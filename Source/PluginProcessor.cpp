#include "PluginProcessor.h"
#include "PluginEditor.h"

ChanSamplerProcessor::ChanSamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    formatManager.registerBasicFormats();

    for (auto& id : precomputeParamIDs())
        apvts.addParameterListener (id, this);

    publish (buildProcessedSample (*raw, readPrecomputeSettings()));
    rebuildThread.startThread();
    startTimer (50);
}

ChanSamplerProcessor::~ChanSamplerProcessor()
{
    stopTimer();
    cutgroups::releaseNumber (instanceNumber);
    for (auto& id : precomputeParamIDs())
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
    {
        rebuildNow();
    }
    engine.prepare (sampleRate);
    cutOffsets.reserve (256);
    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (pid::output), -47.9f));
}

void ChanSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

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
    if (const int n = previewOff.exchange (-1); n >= 0)
        engine.previewNote (n, false, params);
    if (const int n = previewOn.exchange (-1); n >= 0 && audioThreadSample != nullptr)
        engine.previewNote (n, true, params);
    // Group: cut by other instances' notes, and post this block's note starts.
    const int cutBy = (int) param (pid::cutBy), cutGroup = (int) param (pid::cutGroup);
    cutReader.collect (cutBy, hostTime, buffer.getNumSamples(), cutOffsets);
    engine.render (buffer, midi, audioThreadSample.get(), params, cutOffsets.empty() ? nullptr : &cutOffsets);
    if (cutGroup > 0)
        for (const auto start : engine.getNoteStarts())
            cutgroups::post (instanceId, cutGroup, hostTime < 0 ? -1 : hostTime + start);
    engine.updatePlayhead (audioThreadSample.get());

    // Output trim (default -11.4 dB = a fresh FL Sampler channel's level).
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (pid::output), -47.9f));
    outputGain.applyGain (buffer, buffer.getNumSamples());
}

//==============================================================================
PrecomputeSettings ChanSamplerProcessor::readPrecomputeSettings() const
{
    PrecomputeSettings s;
    s.removeDC        = param (pid::pcRemoveDC) > 0.5f;
    s.reversePolarity = param (pid::pcReversePolarity) > 0.5f;
    s.normalize       = param (pid::pcNormalize) > 0.5f;
    s.fadeStereo      = param (pid::pcFadeStereo) > 0.5f;
    s.reverse         = param (pid::pcReverse) > 0.5f;
    s.swapStereo      = param (pid::pcSwapStereo) > 0.5f;
    s.start       = param (pid::pcStart);
    s.length      = param (pid::pcLength);
    s.fadeIn      = param (pid::pcFadeIn);
    s.fadeOut     = param (pid::pcFadeOut);
    s.crossfade   = param (pid::pcCrossfade);
    s.trim        = param (pid::pcTrim);
    s.boost       = param (pid::pcBoost);
    s.boostClip   = param (pid::pcBoostClip) > 0.5f;
    s.eq          = param (pid::pcEq);
    s.rmMix       = param (pid::pcRmMix);
    s.rmFreq      = param (pid::pcRmFreq);
    s.cut         = param (pid::pcCut);
    s.res         = param (pid::pcRes);
    s.reverbType  = (int) param (pid::pcReverbType);
    s.reverb      = param (pid::pcReverb);
    s.stereoDelay = param (pid::pcStereoDelay);
    s.pogo        = param (pid::pcPogo);
    s.useLoop     = param (pid::useLoop) > 0.5f;
    s.loopStart   = param (pid::loopStart);
    s.loopEnd     = param (pid::loopEnd);
    s.targetRate  = hostRate.load();
    s.loadRegions = param (pid::loadRegions) > 0.5f;
    s.loadSlices  = param (pid::loadSlices) > 0.5f;
    return s;
}

// Tempo-dependent envelope timing (measured in FL): the jump ramps and the
// shortest attack (4 ticks = 19.23 ms at 130 BPM) are counted in song ticks;
// with an envelope's Tempo switch on, its times are for 120 BPM.
void ChanSamplerProcessor::applyTempo (EngineParams& p)
{
    const auto bpm = juce::jmax (1.0, p.bpm);
    for (auto* t : { &p.panT, &p.volT, &p.xT, &p.yT, &p.pitchT })
    {
        auto& e = t->env;
        e.tickScale = (float) (130.0 / bpm);
        if (t->envTempo)
        {
            const auto k = (float) (120.0 / bpm);
            e.delay *= k; e.attack *= k; e.hold *= k; e.decay *= k; e.release *= k;
        }
        if (e.attack > 0.0f)
            e.attack = juce::jmax (flMinAttackSeconds * e.tickScale, e.attack);
    }
}

EngineParams ChanSamplerProcessor::readEngineParams() const
{
    auto target = [this] (const char* prefix)
    {
        auto get = [this, prefix] (const char* s) { return param (pid::env (prefix, s)); };
        EngineParams::Target t;
        if (apvts.getParameter (pid::env (prefix, "EnvOn")) == nullptr)
        {
            t.envOn = false;   // no envelope on this target (Panning)
        }
        else
        {
        t.envOn  = get ("EnvOn") > 0.5f;
        t.envTempo = get ("EnvTempo") > 0.5f;
        if (auto* amt = apvts.getRawParameterValue (pid::env (prefix, "EnvAmt")))
            t.envAmt = amt->load();
        t.env.delay    = flEnvSeconds (get ("Del"));
        // Measured: the minimum raw attack (100, position 0) is instant; anything above it
        // ramps over at least 19.23 ms.
        const auto att = get ("Att");
        t.env.attack   = att <= 0.5f / 65436.0f ? 0.0f : flEnvSeconds (att);   // floor applied with the tempo (applyTempo)
        t.env.hold     = flEnvSeconds (get ("Hold"));
        t.env.decay    = flEnvSeconds (get ("Dec"));
        t.env.sustain  = get ("Sus");
        t.env.release  = flEnvSeconds (get ("Rel"));
        t.env.attackK  = flTensionToK (get ("AttT"));
        t.env.decayK   = flTensionToK (get ("DecT"));
        t.env.releaseK = flTensionToK (get ("RelT"));
        }
        t.lfo.shape   = (LfoShape) (int) get ("LfoShape");
        t.lfo.periodT130 = flLfoPeriodT130 (get ("LfoSpeed"));
        t.lfo.amount  = get ("LfoAmt");
        t.lfo.delayT130 = flLfoDelayT130 (get ("LfoDel"));
        t.lfo.tempo   = get ("LfoTempo") > 0.5f;
        t.lfo.global  = get ("LfoGlobal") > 0.5f;
        t.lfo.attack  = get ("LfoAtt") > 0.0f ? flEnvSeconds (get ("LfoAtt")) : 0.0f;
        return t;
    };

    EngineParams p;
    p.pitchRange = (int) param (pid::pitchRange);

    p.startOffset = param (pid::startOffset);
    p.useLoop     = param (pid::useLoop) > 0.5f;
    p.loopStart   = param (pid::loopStart);
    p.loopEnd     = param (pid::loopEnd);
    p.pingPong    = param (pid::pingPong) > 0.5f;
    p.declick     = (DeclickMode) (int) param (pid::declick);

    p.rootNote = (int) param (pid::rootNote);
    p.fineTune = param (pid::fineTune);
    p.pitch    = param (pid::pitch) * 0.01f;            // cents
    p.stretchMode = (StretchMode) (int) param (pid::stretchMode);
    p.stretchMul  = param (pid::stretchMul);
    p.stretchTime = param (pid::stretchTime);
    p.formant     = param (pid::formant);
    p.stretchMode = resolveStretchMode (p.stretchMode, param (pid::pitch), p.stretchMul, p.stretchTime);

    p.panT   = target (pid::panPrefix);
    p.volT   = target (pid::volPrefix);
    p.xT     = target (pid::modXPrefix);
    p.yT     = target (pid::modYPrefix);
    p.pitchT = target (pid::pitchPrefix);

    p.filterType = (FilterType) (int) param (pid::filterType);
    p.cutoff     = param (pid::cutoff);
    p.resonance  = param (pid::resonance);

    p.volume = flVolumeGain (param (pid::volume));
    p.pan    = param (pid::pan);
    p.xOfs   = param (pid::xOfs);
    p.yOfs   = param (pid::yOfs);

    p.velPan = param (pid::velPan);
    p.velX   = param (pid::velX);
    p.velY   = param (pid::velY);
    p.velMid = param (pid::velMid);
    p.keyPan = param (pid::keyPan);
    p.keyX   = param (pid::keyX);
    p.keyY   = param (pid::keyY);
    p.keyMid = (int) param (pid::keyMid);

    p.slideTicks = flSlideTicks (param (pid::slide));
    p.mono      = param (pid::mono) > 0.5f;
    p.porta     = param (pid::porta) > 0.5f;
    p.cutItself = param (pid::cutItself) > 0.5f;

    p.arpDir    = (int) param (pid::arpDir);
    p.arpRange  = (int) param (pid::arpRange);
    p.arpRepeat = (int) param (pid::arpRepeat);
    p.arpTime   = (float) flArpSteps (param (pid::arpTime));   // steps, 0 = hold
    p.arpGate   = param (pid::arpGate);
    p.arpSlide  = param (pid::arpSlide) > 0.5f;
    p.arpChord  = (int) param (pid::arpChord);

    p.echoFeed  = param (pid::echoFeed);
    p.echoPan   = param (pid::echoPan);
    p.echoX     = param (pid::echoX);
    p.echoY     = param (pid::echoY);
    p.echoPitch = param (pid::echoPitch) * 0.01f;        // cents
    p.echoTime  = param (pid::echoTime);
    p.echoCount = (int) param (pid::echoCount);
    p.echoPing  = param (pid::echoPing) > 0.5f;
    p.echoFat   = param (pid::echoFat) > 0.5f;
    return p;
}

//==============================================================================
void ChanSamplerProcessor::applyLoopLink (const juce::String& changedId)
{
    // FL's linked loop knobs (measured): START moves alone while it is below
    // END and pushes END to START + 1 sample when it passes it; END moves
    // alone but stops at START (START = END is allowed). END = 0 means no loop.
    // Applied to the user's edits only (knobs, menu, the sample view), so host
    // automation sets exactly the values it shows; the engine treats an END
    // below START as START.
    const auto start = param (pid::loopStart), end = param (pid::loopEnd);
    const auto oneSample = 1.0f / (float) juce::jmax (1, getSampleLength());
    float newEnd = end;
    if (changedId == pid::loopStart && start > 0.0f && start > end)
        newEnd = juce::jmin (1.0f, start + oneSample);
    else if (changedId == pid::loopEnd && end < start)
        newEnd = start;
    if (newEnd != end)
    {
        auto* e = apvts.getParameter (pid::loopEnd);
        e->beginChangeGesture();
        e->setValueNotifyingHost (e->convertTo0to1 (newEnd));
        e->endChangeGesture();
    }
}

void ChanSamplerProcessor::parameterChanged (const juce::String& parameterID, float)
{
    // Loop points only touch the sample data when a loop crossfade is baked in.
    const bool loopParam = parameterID == pid::useLoop || parameterID == pid::loopStart || parameterID == pid::loopEnd;
    if (loopParam && param (pid::pcCrossfade) <= 0.0f)
        return;

    rebuildPending = true;
    rebuildThread.notify();
}

void ChanSamplerProcessor::timerCallback()
{

    // FL: switching Cut self on sets Cut and By to the channel's own number, so
    // other channels can be cut by it too (here the instance number); off
    // clears them again if they still hold it.
    const int cutSelf = param (pid::cutItself) > 0.5f ? 1 : 0;
    if (lastCutSelf >= 0 && cutSelf != lastCutSelf)
    {
        auto set = [this] (const char* id, float v)
        {
            auto* p = apvts.getParameter (id);
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        };
        if (cutSelf == 1)
        {
            set (pid::cutGroup, (float) instanceNumber);
            set (pid::cutBy, (float) instanceNumber);
        }
        else
        {
            if ((int) param (pid::cutGroup) == instanceNumber) set (pid::cutGroup, 0.0f);
            if ((int) param (pid::cutBy) == instanceNumber)    set (pid::cutBy, 0.0f);
        }
    }
    lastCutSelf = cutSelf;

    const juce::ScopedLock sl (poolLock);
    for (int i = releasePool.size(); --i >= 0;)
        if (releasePool.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            releasePool.remove (i);
}

void ChanSamplerProcessor::rebuildSample()
{
    // Build from a snapshot of the raw sample without holding the lock.
    const auto source = getRaw();
    publish (buildProcessedSample (*source, readPrecomputeSettings()));
    sendChangeMessage();
}

void ChanSamplerProcessor::publish (SampleData::Ptr newSample)
{
    {
        const juce::ScopedLock sl (poolLock);
        releasePool.add (newSample);
    }
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    currentSample = newSample;
}

SampleData::Ptr ChanSamplerProcessor::getSampleForDisplay() const
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return currentSample;
}

//==============================================================================
namespace
{
    // Markers in a WAV file: cue points (FL's "regions") and the ACID "strc"
    // chunk (FL's "slice markers": a 28-byte header with the entry count, then
    // 24-byte entries whose second field is the position in frames).
    void readWavMarkers (const juce::File& file, std::vector<int>& regions, std::vector<int>& slices)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk() || in.readInt() != (int) juce::ByteOrder::littleEndianInt ("RIFF"))
            return;
        in.readInt();
        if (in.readInt() != (int) juce::ByteOrder::littleEndianInt ("WAVE"))
            return;
        while (! in.isExhausted())
        {
            const auto id = in.readInt();
            const auto size = (juce::uint32) in.readInt();
            const auto next = in.getPosition() + size + (size & 1);
            if (id == (int) juce::ByteOrder::littleEndianInt ("cue ") && size >= 4)
            {
                const int count = in.readInt();
                for (int i = 0; i < count && (juce::uint32) (4 + 24 * (i + 1)) <= size; ++i)
                {
                    juce::uint32 f[6];
                    for (auto& v : f) v = (juce::uint32) in.readInt();
                    regions.push_back ((int) f[5]);   // sample offset
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
                        slices.push_back ((int) f[1]);
                }
            }
            if (! in.setPosition (next))
                break;
        }
    }
}

bool ChanSamplerProcessor::reloadSample()
{
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

bool ChanSamplerProcessor::loadSample (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max())
        return false;

    RawSample r;
    r.file = file;
    r.sampleRate = reader->sampleRate;
    r.bitsPerSample = (int) reader->bitsPerSample;
    const int numChannels = juce::jlimit (1, 2, (int) reader->numChannels);
    const int length = (int) reader->lengthInSamples;
    r.audio.setSize (numChannels, length);
    reader->read (&r.audio, 0, length, 0, true, numChannels > 1);

    if (file.hasFileExtension (".wav;.wave"))
        readWavMarkers (file, r.regionMarkers, r.sliceMarkers);
    // FL keeps 16-bit PCM as 16-bit and decodes everything else (24 / 32-bit,
    // float, compressed, also compressed audio inside a WAV) to 32-bit float:
    // its sample view badge shows 16 or 32 (measured).
    {
        const auto name = reader->getFormatName();
        const bool pcmContainer = name.containsIgnoreCase ("WAV") || name.containsIgnoreCase ("AIFF") || name.containsIgnoreCase ("FLAC");
        bool compressedWav = false;
        if (name.containsIgnoreCase ("WAV"))
            if (juce::FileInputStream in (file); in.openedOk())
            {
                in.setPosition (12);
                while (! in.isExhausted())
                {
                    const auto id = in.readInt();
                    const auto size = (juce::uint32) in.readInt();
                    if (id == (int) juce::ByteOrder::littleEndianInt ("fmt "))
                    {
                        const auto tag = (juce::uint16) in.readShort();
                        compressedWav = tag != 1 && tag != 3 && tag != 0xfffe;
                        break;
                    }
                    if (! in.setPosition (in.getPosition() + size + (size & 1)))
                        break;
                }
            }
        r.heldBits = pcmContainer && ! compressedWav && ! reader->usesFloatingPointData && reader->bitsPerSample <= 16 ? 16 : 32;
    }

    // Embedded loop points (WAV "smpl" chunk) auto-populate the loop controls.
    const auto& md = reader->metadataValues;
    if (md.getValue ("NumSampleLoops", "0").getIntValue() > 0)
    {
        r.embeddedLoopStart = md.getValue ("Loop0Start", "-1").getIntValue();
        r.embeddedLoopEnd   = md.getValue ("Loop0End",   "-1").getIntValue();
    }

    {
        const juce::ScopedLock sl (poolLock);
        raw = std::make_shared<const RawSample> (std::move (r));
    }
    displaySampleLength.store (length);   // loop knob texts are sample positions

    const auto loaded = getRaw();
    auto set = [this] (const char* id, float value)
    {
        auto* p = apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    if (loaded->embeddedLoopStart >= 0 && loaded->embeddedLoopEnd > loaded->embeddedLoopStart)
    {
        set (pid::startOffset, 0.0f);
        set (pid::loopStart, (float) loaded->embeddedLoopStart / (float) length);
        set (pid::loopEnd,   (float) loaded->embeddedLoopEnd / (float) length);   // END is inclusive, as in the file
        set (pid::useLoop,   1.0f);
    }
    else
    {
        // A new sample starts clean (no Start or Loop markers in the view):
        // start offset and loop points back to 0. A restored project puts
        // its own values back afterwards (setStateInformation).
        set (pid::startOffset, 0.0f);
        set (pid::loopStart, 0.0f);
        set (pid::loopEnd, 0.0f);
    }

    rebuildSample();
    return true;
}

void ChanSamplerProcessor::removeSample()
{
    {
        const juce::ScopedLock sl (poolLock);
        raw = std::make_shared<const RawSample>();
    }
    displaySampleLength.store (0);
    rebuildSample();
}

juce::String ChanSamplerProcessor::getSampleInfo() const
{
    const auto r = getRaw();
    if (r->audio.getNumSamples() == 0)
        return "No sample loaded";

    return juce::String (r->sampleRate, 0) + " Hz  |  "
         + juce::String (r->bitsPerSample) + "-bit  |  "
         + (r->audio.getNumChannels() > 1 ? "Stereo" : "Mono") + "  |  "
         + juce::String (r->audio.getNumSamples() / r->sampleRate, 2) + " s";
}

//==============================================================================
void ChanSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("samplePath", getRaw()->file.getFullPathName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChanSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const juce::String path = state.getProperty ("samplePath").toString();
    apvts.replaceState (state);
    lastCutSelf = param (pid::cutItself) > 0.5f ? 1 : 0;   // a restored Cut self keeps its saved groups

    if (path.isNotEmpty() && juce::File (path).existsAsFile())
    {
        // Restore the project's own loop points, not the file's embedded ones.
        const auto savedLoop = apvts.copyState();
        loadSample (juce::File (path));
        apvts.replaceState (savedLoop);
        rebuildSample();
    }
}

juce::AudioProcessorEditor* ChanSamplerProcessor::createEditor()
{
    return new ChanSamplerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChanSamplerProcessor();
}
