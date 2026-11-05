#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <deque>
#include <functional>
#include <vector>

// The parameter a host records the plugin's group edits with (see History):
// one step of its own undo history per edit, named after the edit ("Preset:
// Chop", "Load sample: kick.wav"), its value counting the edits.
class HistoryStepParameter : public juce::AudioParameterInt
{
public:
    HistoryStepParameter (const juce::ParameterID&, const juce::String& title);

    static constexpr int numValues = 65536;   // the count wraps around

    juce::String getName (int maximumStringLength) const override;
    void setStepName (const juce::String&);

private:
    mutable juce::SpinLock nameLock;
    juce::String stepName;
};

// Undo / redo of the user's edits, in one place: parameter changes (knobs,
// sliders, switches, lists, the menus), the sample loaded, replaced or
// removed, and the preset name shown with them. As in FL Studio, a drag is one step (recorded when the gesture
// ends), an edit that changes nothing records nothing, and a new edit after an
// undo drops the redo steps.
//
// How a step is recorded: the first gesture to begin (or an explicit
// Transaction) opens a step with a snapshot of every parameter and the
// sample's file; when the last gesture has ended (and the outermost
// transaction closed) the snapshot is compared with the state now and the
// differences are the step. Changes that arrive outside any gesture (host
// automation, a project loading) are not steps: they are the host's.
//
// In a host (followsHost) the undo history is the host's. A knob's edit is
// already a step there (FL Studio records every automatable parameter the
// plugin changes); a group edit (a Transaction: a preset, a sample loaded, a
// reset) sends its values from another thread, which hosts take as the
// plugin's own changes and not as edits (FL records none of them), and then
// steps the HistoryStepParameter, renamed after the edit, as the one edit the
// host records. When the host undoes or redoes it (sets the step parameter
// back or forth), the group's values and sample are put back here.
class History : public juce::ChangeBroadcaster,
                private juce::AudioProcessorParameter::Listener,
                private juce::AsyncUpdater
{
public:
    // The sample's state a step restores: its file (none = no sample).
    using SampleGetter = std::function<juce::File()>;
    using SampleSetter = std::function<void (const juce::File&)>;
    // The preset's name shown (none = no preset).
    using NameGetter = std::function<juce::String()>;
    using NameSetter = std::function<void (const juce::String&)>;

    History (juce::AudioProcessor&, SampleGetter, SampleSetter, NameGetter, NameSetter, HistoryStepParameter*, bool followsHost);
    ~History() override;

    static constexpr int maxSteps = 100;

    // The host's history is the undo history (a plugin); otherwise this one is (standalone).
    bool followsHost() const noexcept { return hostHistory; }

    bool canUndo() const noexcept { return ! hostHistory && ! undoSteps.empty(); }
    bool canRedo() const noexcept { return ! hostHistory && ! redoSteps.empty(); }
    juce::String getUndoName() const { return undoSteps.empty() ? juce::String() : undoSteps.back().name; }
    juce::String getRedoName() const { return redoSteps.empty() ? juce::String() : redoSteps.back().name; }
    int getNumUndoSteps() const noexcept { return (int) undoSteps.size(); }
    int getNumRedoSteps() const noexcept { return (int) redoSteps.size(); }

    // Message thread. Nothing happens while an edit (gesture) is still open,
    // nor in a host (its own undo and redo do it).
    bool undo();
    bool redo();
    void clear();

    // Groups the edits made while it lives into one step (a preset loaded, a
    // sample loaded, a panel menu's values), named after the action.
    class Transaction
    {
    public:
        Transaction (History&, const juce::String& name);
        ~Transaction();

    private:
        History& history;
        bool quiet = false;
        JUCE_DECLARE_NON_COPYABLE (Transaction)
    };

    // While suspended nothing is recorded (undo / redo themselves, a project loading).
    class ScopedSuspend
    {
    public:
        explicit ScopedSuspend (History& h) : history (h) { ++history.suspended; }
        ~ScopedSuspend() { --history.suspended; }

    private:
        History& history;
        JUCE_DECLARE_NON_COPYABLE (ScopedSuspend)
    };

    bool isRecording() const noexcept { return suspended == 0; }

    // True while a group edit's values are on their way to the host as the
    // plugin's own changes (setParameterValue sends them from another thread).
    static bool isQuiet() noexcept;
    static void runQuietly (const std::function<void()>&);

private:
    struct Snapshot
    {
        std::vector<float> values;   // normalised, by parameter index
        juce::File sample;
        juce::String presetName;
    };
    struct Change { int index; float before, after; };
    struct Step
    {
        juce::String name;
        std::vector<Change> changes;
        bool sampleChanged = false;
        juce::File sampleBefore, sampleAfter;
        bool nameChanged = false;
        juce::String nameBefore, nameAfter;
        int stepBefore = 0, stepAfter = 0;   // the step parameter's values around it (in a host)
    };

    void parameterValueChanged (int index, float) override;
    void parameterGestureChanged (int index, bool starting) override;
    void handleAsyncUpdate() override;
    void open (const juce::String& name);
    void close();
    void tellHostStateChanged();
    Snapshot snapshot() const;
    void apply (const Step&, bool forward);
    void recordInHost (Step&);
    void followHostTo (int step);
    int stepValue() const;

    juce::AudioProcessor& processor;
    SampleGetter getSample;
    SampleSetter setSample;
    NameGetter getName;
    NameSetter setName;
    HistoryStepParameter* stepParam;
    const bool hostHistory;
    int stepIndex = -1;          // the step parameter's index (left out of the snapshots)
    int currentStep = 0;         // the step value the plugin's state is at
    bool settingStep = false;    // the step parameter is being set from here
    std::atomic<int> hostStep { -1 };   // the value the host set it to, handled on the message thread
    std::deque<Step> undoSteps, redoSteps;
    Snapshot opened;
    juce::String openName;
    bool stepOpen = false;
    int gestures = 0, transactions = 0, suspended = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (History)
};
