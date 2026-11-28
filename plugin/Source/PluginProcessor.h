#pragma once

// The plugin: parameters (AudioProcessorValueTreeState), the loaded sample and
// its precomputed copy (rebuilt in the background when an effect changes), the
// engine, saving and loading state, and the sample's link to its file.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include "CutGroups.h"
#include "History.h"
#include "Params.h"
#include "SampleFile.h"
#include "dsp/Engine.h"
#include "dsp/SampleData.h"

namespace cs
{

class ChanSamplerProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::AudioProcessorValueTreeState::Listener,
                             private juce::Timer
{
public:
    ChanSamplerProcessor();
    ~ChanSamplerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Bypassed, the plug-in still reads the MIDI and moves its voices, and outputs silence: a key released during
    // the bypass ends its note, the pedal and the arpeggiator's held keys stay true, and nothing hangs afterwards.
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlockBypassed;   // the double-precision one stays JUCE's (not hidden)
    using juce::AudioProcessor::processBlock;   // the double-precision one stays JUCE's (not hidden)

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chan Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Message thread API
    // Reads the file and makes it the sample before it returns: for what must be in place when the call ends (a
    // project or a preset loading, the history, the tests).
    bool loadSample (const juce::File& file);
    // The same on the worker thread, for the editor (the chooser, a drop, a paste, a reload): a long file does not
    // hold the host's window. The sample changes on the message thread once it is read and its precomputed copy
    // built; done (message thread) then says whether it loaded (getLoadError says why not). A request replaced by
    // a later one, or by a sample put in place meanwhile, ends without done. keepSettings: the settings in use
    // stay, not the file's own (a reload), and the history takes no step.
    void loadSampleInBackground (const juce::File& file, std::function<void (bool loaded)> done = {}, bool keepSettings = false);
    // Work too long for the message thread (reading a file, detecting a tempo) on the processor's worker thread,
    // one job after the other; completion then runs on the message thread, unless the processor is gone by then.
    void runInBackground (std::function<void()> work, std::function<void()> completion);
    void restoreSample (const juce::File& file);   // the history's: the sample without its file's own values
    // Where a saved sample is now: its saved path while a file is there; otherwise a file of the same name in the
    // user's Samples folder (Documents/Chan Sampler/Samples) or in the folder a sample was last loaded from; the
    // saved path when there is none (the sample is then missing, its path kept).
    static juce::File findSample (const juce::File& saved);
    void reloadSample (std::function<void (bool reloaded)> done = {});   // the same file again from disk, settings kept; in the background
    // Reloads the sample once its file has changed on disk (saved from an audio
    // editor) and the change has settled; called regularly by the editor.
    void reloadIfFileChanged();
    // The sample's tempo (FL's Detect tempo, or a WAV's ACID tempo); 0 = none.
    double getSampleTempo() const { return (double) apvts.state.getProperty ("sampleTempo", 0.0); }
    void setSampleTempo (double bpm) { apvts.state.setProperty ("sampleTempo", juce::jmax (0.0, bpm), nullptr); }
    // The user's change of it (Detect tempo): one step of the history, and the host is told the state changed.
    void editSampleTempo (double bpm);
    // The file's audio (before precomputed effects) as a WAV file in memory.
    juce::MemoryBlock getSampleAsWav() const;
    juce::String getLoadError() const { return loadError; }   // why the last loadSample failed
    void rebuildNow() { rebuildSample(); }
    float getPlayhead() const noexcept { return engine.getPlayhead(); }

    // Blocks until background rebuilds triggered by parameter changes are done
    // (offline rendering / tests).
    void waitForPendingRebuilds()
    {
        while (rebuildPending.load() || building.load())
        {
            rebuildThread.notify();   // a change that arrived off the message thread has not woken it yet
            juce::Thread::sleep (2);
        }
    }
    void removeSample();
    // A project's sample that could not be loaded: no sample plays, the path is kept.
    void setMissingSample (const juce::File& file);
    dsp::SampleData::Ptr getSampleForDisplay() const;
    juce::File getSampleFile() const { return getRaw()->file; }
    int getSampleLength() const { return getRaw()->audio.getNumSamples(); }
    // Every parameter back to its default (presets start from here).
    void resetParameters();
    // A saved preset (a state as getStateInformation writes it) as one edit.
    void loadPreset (juce::ValueTree state, const juce::String& name);
    // Documents/Chan Sampler/<name>: the user's presets, envelopes and pasted audio.
    static juce::File userFolder (const juce::String& name);
    // FL's linked loop START / END, for the user's edits (message thread).
    // endGestureOpen: the caller holds loop END's gesture open (a marker drag), so END moves inside it.
    void applyLoopLink (const juce::String& changedId, bool endGestureOpen = false);
    // Cut self switched: Cut and By follow (message thread).
    void followCutSelf();
    juce::String getSampleInfo() const;
    // The loaded file as read (before precomputed effects)
    std::shared_ptr<const RawSample> getRaw() const
    {
        const juce::ScopedLock sl (poolLock);
        return raw;
    }
    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // The loaded sample's length in frames, for the loop knobs' text (before
    // apvts: its parameters read it).
    std::atomic<int> loopTextLength { 0 };
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    // Undo / redo of the user's edits (parameters and the sample). Declared
    // after apvts: it listens to the parameters.
    History history;

    // The parameters that decide whether the time stretchers are needed.
    static constexpr const char* stretchParamIDs[] = { id::stretchMode, id::pitch, id::stretchMul, id::stretchTime };

    // Sample-view preview, sent to the engine directly (not as MIDI) so only
    // these notes show the playback bar. Thread-safe, called from the UI.
    void requestPreview (int note, bool on) { (on ? previewOn : previewOff).store (note); }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    // True when the current settings play through the time stretchers.
    bool needsStretchers() const;
    void timerCallback() override;
    void rebuildSample();
    void publish (dsp::SampleData::Ptr newSample);
    struct Decoded
    {
        std::shared_ptr<const RawSample> raw;   // none: the file could not be read, error says why
        int rootNote = 60;                      // the file's own, C5 when it names none
        juce::String error;
    };
    Decoded decodeSample (const juce::File& file, double playbackRate);
    void applySample (const Decoded&, dsp::SampleData::Ptr prebuilt, const dsp::PrecomputeSettings& prebuiltWith);
    // The settings a build of this source uses: no rate conversion for a file already at the playback rate.
    static dsp::PrecomputeSettings settingsFor (const RawSample& source, dsp::PrecomputeSettings settings);
    dsp::PrecomputeSettings readPrecomputeSettings() const;
    dsp::EngineParams readEngineParams() const;
    static void applyTempo (dsp::EngineParams&);
    // Parameter values by ID without building a juce::String or searching
    // the tree (the audio thread reads about 120 a block): looked up once.
    std::atomic<float>* rawParam (std::string_view id) const
    {
        const auto it = rawParams.find (id);
        return it != rawParams.end() ? it->second.value : nullptr;
    }
    // A parameter's value; one that is not a number (a damaged state, preset or automation) reads as its default,
    // so that nothing downstream indexes a table or sizes a buffer with it.
    float param (std::string_view id) const
    {
        const auto& entry = rawParams.find (id)->second;
        const float v = entry.value->load();
        return std::isfinite (v) ? v : entry.fallback;
    }
    float param (const char* id) const { return param (std::string_view (id)); }
    float param (const juce::String& id) const { return param (std::string_view (id.toRawUTF8())); }

    // Rebuilds the precomputed sample off the message thread whenever a
    // precomputed setting changes, so the waveform follows knobs live.
    class RebuildThread : public juce::Thread
    {
    public:
        explicit RebuildThread (ChanSamplerProcessor& p) : juce::Thread ("ChanSampler rebuild"), owner (p) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wait (-1);
                while (! threadShouldExit() && owner.rebuildPending.load())
                {
                    owner.building = true;
                    owner.rebuildPending = false;
                    owner.rebuildSample();
                    owner.building = false;
                }
            }
        }

    private:
        ChanSamplerProcessor& owner;
    };

    // One thread for the jobs of runInBackground. Joined when the processor goes away, never killed; the jobs
    // still waiting then are dropped.
    class Worker : public juce::Thread
    {
    public:
        Worker() : juce::Thread ("ChanSampler worker") {}
        void add (std::function<void()> work, std::function<void()> completion, std::shared_ptr<std::atomic<bool>> ownerAlive)
        {
            {
                const juce::ScopedLock sl (lock);
                jobs.push_back ({ std::move (work), std::move (completion), std::move (ownerAlive) });
            }
            notify();
        }
        void run() override
        {
            while (! threadShouldExit())
            {
                Job job;
                {
                    const juce::ScopedLock sl (lock);
                    if (! jobs.empty())
                    {
                        job = std::move (jobs.front());
                        jobs.pop_front();
                    }
                }
                if (job.work == nullptr)
                {
                    wait (-1);
                    continue;
                }
                job.work();
                juce::MessageManager::callAsync ([ownerAlive = std::move (job.ownerAlive), completion = std::move (job.completion)]
                {
                    if (ownerAlive->load() && completion != nullptr)
                        completion();
                });
            }
        }

    private:
        struct Job
        {
            std::function<void()> work, completion;
            std::shared_ptr<std::atomic<bool>> ownerAlive;
        };
        juce::CriticalSection lock;
        std::deque<Job> jobs;
    };

    struct RawParam { std::atomic<float>* value = nullptr; float fallback = 0.0f; };   // fallback: the default
    std::unordered_map<std::string_view, RawParam> rawParams;   // views of the parameters' own IDs
    juce::AudioFormatManager formatManager;
    std::shared_ptr<const RawSample> raw = std::make_shared<RawSample>();
    // Rebuilds run one at a time; one with the same file and settings as the
    // sample in use is skipped (a new sample restarts the playing voices).
    std::mutex rebuildMutex;
    juce::String loadError;
    std::pair<juce::int64, juce::int64> changedStamp, failedStamp;   // reloadIfFileChanged: (time, size)
    std::shared_ptr<const RawSample> builtFrom;
    dsp::PrecomputeSettings builtWith;
    dsp::Engine engine;
    const uint32_t instanceId = cutgroups::newInstanceId();
    cutgroups::Reader cutReader { instanceId };
    const int instanceNumber = cutgroups::claimNumber();   // FL's channel number for Cut self
    std::atomic<bool> stopRebuild { false };              // set when the processor goes away: a build in progress gives up
    std::atomic<int> lastCutSelf { -1 };                  // to see Cut self being switched (timer)
    std::vector<int> cutOffsets;                          // this block's cuts by other instances
    std::vector<dsp::MidiEvent> midiEvents;               // this block's MIDI for the engine (reserved)
    juce::MidiBuffer filteredMidi;                        // this block's MIDI after the sustain pedal (reserved)

    // Sustain pedal: note-offs held while it is down (audio thread only).
    bool sustainDown = false;
    std::array<bool, 128> sustained {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGain { 1.0f };
    // Saved with every state; 2: OUT is FL's channel volume knob (before, a trim in dB).
    static constexpr int currentStateVersion = 2;
    static void upgradeState (juce::ValueTree&);   // an older saved state in today's terms

    // The audio thread reads currentSample under a try-lock. Every published
    // sample stays in releasePool until only the pool references it, so the
    // audio thread never frees memory.
    juce::SpinLock sampleLock;
    dsp::SampleData::Ptr currentSample;
    dsp::SampleData::Ptr audioThreadSample;
    std::vector<dsp::SampleData::Ptr> releasePool;
    juce::CriticalSection poolLock;

    std::atomic<bool> rebuildPending { false }, building { false };
    std::atomic<double> hostRate { 0.0 };
    std::atomic<int> previewOn { -1 }, previewOff { -1 };
    RebuildThread rebuildThread { *this };
    Worker worker;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);   // false: no completion runs any more
    std::shared_ptr<std::atomic<bool>> loadCancel;   // the background load in progress gives up when set (message thread)
    std::atomic<int> loadTicket { 0 };               // counts the samples asked for: a background load applies only if still the latest
    std::atomic<int> loadsRunning { 0 };             // background loads not yet completed

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChanSamplerProcessor)
};
}
