#pragma once

// The plugin: parameters (AudioProcessorValueTreeState), the loaded sample and
// its precomputed copy (rebuilt in the background when an effect changes), the
// engine, saving and loading state, and the sample's link to its file.

#include "CutGroups.h"
#include <array>
#include <mutex>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "Params.h"
#include "SampleData.h"
#include "SamplerEngine.h"

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
    bool loadSample (const juce::File& file);
    bool reloadSample();   // the same file again from disk, settings kept
    // Reloads the sample once its file has changed on disk (saved from an audio
    // editor) and the change has settled; called regularly by the editor.
    void reloadIfFileChanged();
    // The sample's tempo (FL's Detect tempo, or a WAV's ACID tempo); 0 = none.
    double getSampleTempo() const { return (double) apvts.state.getProperty ("sampleTempo", 0.0); }
    void setSampleTempo (double bpm) { apvts.state.setProperty ("sampleTempo", juce::jmax (0.0, bpm), nullptr); }
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
            juce::Thread::sleep (2);
    }
    void removeSample();
    SampleData::Ptr getSampleForDisplay() const;
    juce::File getSampleFile() const { return getRaw()->file; }
    int getSampleLength() const { return getRaw()->audio.getNumSamples(); }
    // FL's linked loop START / END, for the user's edits (message thread).
    void applyLoopLink (const juce::String& changedId);
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

    // Sample-view preview, sent to the engine directly (not as MIDI) so only
    // these notes show the playback bar. Thread-safe, called from the UI.
    void requestPreview (int note, bool on) { (on ? previewOn : previewOff).store (note); }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;
    void rebuildSample();
    void publish (SampleData::Ptr newSample);
    PrecomputeSettings readPrecomputeSettings() const;
    EngineParams readEngineParams() const;
    static void applyTempo (EngineParams&);
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }


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

    juce::AudioFormatManager formatManager;
    std::shared_ptr<const RawSample> raw = std::make_shared<RawSample>();
    // Rebuilds run one at a time; one with the same file and settings as the
    // sample in use is skipped (a new sample restarts the playing voices).
    std::mutex rebuildMutex;
    juce::String loadError;
    std::pair<juce::int64, juce::int64> changedStamp, failedStamp;   // reloadIfFileChanged: (time, size)
    std::shared_ptr<const RawSample> builtFrom;
    PrecomputeSettings builtWith;
    SamplerEngine engine;
    const uint32_t instanceId = cutgroups::newInstanceId();
    cutgroups::Reader cutReader { instanceId };
    const int instanceNumber = cutgroups::claimNumber();   // FL's channel number for Cut self
    std::atomic<int> lastCutSelf { -1 };


    // Sustain pedal: note-offs held while it is down (audio thread only).
    bool sustainDown = false;
    std::array<bool, 128> sustained {};                   // to see Cut self being switched (timer)
    std::vector<int> cutOffsets;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGain { 1.0f };

    // The audio thread reads currentSample under a try-lock. Every published
    // sample stays in releasePool until only the pool references it, so the
    // audio thread never frees memory.
    juce::SpinLock sampleLock;
    SampleData::Ptr currentSample;
    SampleData::Ptr audioThreadSample;
    juce::ReferenceCountedArray<SampleData> releasePool;
    juce::CriticalSection poolLock;

    std::atomic<bool> rebuildPending { false }, building { false };
    std::atomic<double> hostRate { 0.0 };
    std::atomic<int> previewOn { -1 }, previewOff { -1 };
    RebuildThread rebuildThread { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChanSamplerProcessor)
};
