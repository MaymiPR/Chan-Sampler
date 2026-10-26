#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <deque>
#include <functional>
#include <vector>

// Undo / redo of the user's edits, in one place: parameter changes (knobs,
// sliders, switches, lists, the menus), and the sample loaded, replaced or
// removed. As in FL Studio, a drag is one step (recorded when the gesture
// ends), an edit that changes nothing records nothing, and a new edit after an
// undo drops the redo steps.
//
// How a step is recorded: the first gesture to begin (or an explicit
// Transaction) opens a step with a snapshot of every parameter and the
// sample's file; when the last gesture has ended (and the outermost
// transaction closed) the snapshot is compared with the state now and the
// differences are the step. Changes that arrive outside any gesture (host
// automation, a project loading) are not steps: they are the host's.
class History : public juce::ChangeBroadcaster,
                private juce::AudioProcessorParameter::Listener
{
public:
    // The sample's state a step restores: its file (none = no sample).
    using SampleGetter = std::function<juce::File()>;
    using SampleSetter = std::function<void (const juce::File&)>;

    History (juce::AudioProcessor&, SampleGetter, SampleSetter);
    ~History() override;

    static constexpr int maxSteps = 100;

    bool canUndo() const noexcept { return ! undoSteps.empty(); }
    bool canRedo() const noexcept { return ! redoSteps.empty(); }
    juce::String getUndoName() const { return undoSteps.empty() ? juce::String() : undoSteps.back().name; }
    juce::String getRedoName() const { return redoSteps.empty() ? juce::String() : redoSteps.back().name; }
    int getNumUndoSteps() const noexcept { return (int) undoSteps.size(); }
    int getNumRedoSteps() const noexcept { return (int) redoSteps.size(); }

    // Message thread. Nothing happens while an edit (gesture) is still open.
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

private:
    struct Snapshot
    {
        std::vector<float> values;   // normalised, by parameter index
        juce::File sample;
    };
    struct Change { int index; float before, after; };
    struct Step
    {
        juce::String name;
        std::vector<Change> changes;
        bool sampleChanged = false;
        juce::File sampleBefore, sampleAfter;
    };

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool starting) override;
    void open (const juce::String& name);
    void close();
    Snapshot snapshot() const;
    void apply (const Step&, bool forward);

    juce::AudioProcessor& processor;
    SampleGetter getSample;
    SampleSetter setSample;
    std::deque<Step> undoSteps, redoSteps;
    Snapshot opened;
    juce::String openName;
    bool stepOpen = false;
    int gestures = 0, transactions = 0, suspended = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (History)
};
