#include "History.h"
#include <thread>

namespace cs
{

HistoryStepParameter::HistoryStepParameter (const juce::ParameterID& id, const juce::String& title)
    : juce::AudioParameterInt (id, title, 0, numValues - 1, 0), stepName (title) {}

juce::String HistoryStepParameter::getName (int maximumStringLength) const
{
    const juce::SpinLock::ScopedLockType sl (nameLock);
    return stepName.substring (0, maximumStringLength);
}

void HistoryStepParameter::setStepName (const juce::String& title)
{
    const juce::SpinLock::ScopedLockType sl (nameLock);
    stepName = title;
}

//==============================================================================
namespace
{
    int quietEdits = 0;   // message thread: group edits open in a host
}

bool History::isQuiet() noexcept
{
    return quietEdits > 0;
}

// Parameter values set off the message thread reach the host as the plugin's
// own changes (JUCE's VST3 wrapper sends them with the next audio block), not
// as edits for its undo history.
void History::runQuietly (const std::function<void()>& fn)
{
    std::thread worker (fn);
    worker.join();
}

History::History (juce::AudioProcessor& p, SampleGetter getter, SampleSetter setter, NameGetter nameGetter, NameSetter nameSetter,
                  HistoryStepParameter* step, bool followsHost)
    : processor (p), getSample (std::move (getter)), setSample (std::move (setter)),
      getName (std::move (nameGetter)), setName (std::move (nameSetter)), stepParam (step), hostHistory (followsHost && step != nullptr)
{
    for (auto* param : processor.getParameters())
        param->addListener (this);
    if (stepParam != nullptr)
        stepIndex = stepParam->getParameterIndex();
    currentStep = stepValue();
}

History::~History()
{
    cancelPendingUpdate();
    for (auto* param : processor.getParameters())
        param->removeListener (this);
}

int History::stepValue() const
{
    return stepParam != nullptr ? stepParam->get() : 0;
}

History::Snapshot History::snapshot() const
{
    Snapshot s;
    const auto& params = processor.getParameters();
    s.values.reserve ((size_t) params.size());
    for (auto* param : params)
        s.values.push_back (param->getValue());
    s.sample = getSample != nullptr ? getSample() : juce::File();
    s.presetName = getName != nullptr ? getName() : juce::String();
    return s;
}

void History::parameterValueChanged (int index, float value)
{
    // The host moved the step parameter (its undo or redo, a project, automation):
    // any thread, followed on the message thread.
    if (! hostHistory || index != stepIndex || settingStep)
        return;
    hostStep.store (juce::roundToInt (value * (float) (HistoryStepParameter::numValues - 1)));
    triggerAsyncUpdate();
}

void History::handleAsyncUpdate()
{
    const auto step = hostStep.exchange (-1);
    if (step >= 0)
        followHostTo (step);
}

void History::parameterGestureChanged (int index, bool starting)
{
    // In a host a knob's edit is the host's own step.
    if (hostHistory)
        return;
    // Gestures come from the user's controls on the message thread (a host
    // automates values without gestures).
    if (starting)
    {
        ++gestures;
        if (suspended == 0 && ! stepOpen)
        {
            auto* param = processor.getParameters()[index];
            open (param != nullptr ? param->getName (64) : juce::String());
        }
    }
    else
    {
        gestures = juce::jmax (0, gestures - 1);
        if (gestures == 0 && transactions == 0)
            close();
    }
}

// The parameters reach the host through their gestures (so its automation
// and, in hosts that record plugin edits, its own undo follow an undo here);
// a sample change is not a parameter, so the host is told the state changed
// (VST3: the project is marked modified).
void History::tellHostStateChanged()
{
    processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
}

void History::open (const juce::String& name)
{
    stepOpen = true;
    openName = name;
    opened = snapshot();
}

void History::close()
{
    if (! stepOpen)
        return;
    stepOpen = false;
    const auto now = snapshot();
    Step step;
    step.name = openName;
    const auto& params = processor.getParameters();
    for (size_t i = 0; i < now.values.size() && i < opened.values.size(); ++i)
        if ((int) i != stepIndex && now.values[i] != opened.values[i])
            step.changes.push_back ({ (int) i, opened.values[i], now.values[i] });
    if (now.sample != opened.sample)
    {
        step.sampleChanged = true;
        step.sampleBefore = opened.sample;
        step.sampleAfter = now.sample;
    }
    if (now.presetName != opened.presetName)
    {
        step.nameChanged = true;
        step.nameBefore = opened.presetName;
        step.nameAfter = now.presetName;
    }
    if (step.changes.empty() && ! step.sampleChanged && ! step.nameChanged)
        return;   // nothing changed: no step
    if (step.name.isEmpty())
        step.name = step.sampleChanged ? juce::String ("sample")
                  : step.changes.empty() ? juce::String ("preset name") : params[step.changes.front().index]->getName (64);
    if (step.sampleChanged)
        tellHostStateChanged();
    if (hostHistory)
        recordInHost (step);
    undoSteps.push_back (std::move (step));
    while ((int) undoSteps.size() > maxSteps)
        undoSteps.pop_front();
    redoSteps.clear();
    sendChangeMessage();
}

// The group edit as one edit for the host: the step parameter, named after it, one value on.
void History::recordInHost (Step& step)
{
    step.stepBefore = currentStep;
    step.stepAfter = (currentStep + 1) % HistoryStepParameter::numValues;
    stepParam->setStepName (step.name);
    processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withParameterInfoChanged (true));
    const juce::ScopedValueSetter<bool> own (settingStep, true);
    stepParam->beginChangeGesture();
    stepParam->setValueNotifyingHost (stepParam->convertTo0to1 ((float) step.stepAfter));
    stepParam->endChangeGesture();
    currentStep = step.stepAfter;
}

// The host's undo or redo of group edits (it may jump over several at once).
void History::followHostTo (int step)
{
    if (step == currentStep)
        return;
    const auto before = [step] (const Step& s) { return s.stepBefore == step; };
    const auto after = [step] (const Step& s) { return s.stepAfter == step; };
    if (std::any_of (undoSteps.begin(), undoSteps.end(), before))
    {
        while (currentStep != step && ! undoSteps.empty() && undoSteps.back().stepAfter == currentStep)
        {
            auto s = std::move (undoSteps.back());
            undoSteps.pop_back();
            apply (s, false);
            currentStep = s.stepBefore;
            redoSteps.push_back (std::move (s));
        }
    }
    else if (std::any_of (redoSteps.begin(), redoSteps.end(), after))
    {
        while (currentStep != step && ! redoSteps.empty() && redoSteps.back().stepBefore == currentStep)
        {
            auto s = std::move (redoSteps.back());
            redoSteps.pop_back();
            apply (s, true);
            currentStep = s.stepAfter;
            undoSteps.push_back (std::move (s));
        }
    }
    currentStep = step;   // anything else (automation, a project's value) is the host's state now
    sendChangeMessage();
}

void History::apply (const Step& step, bool forward)
{
    const ScopedSuspend suspend (*this);
    const juce::ScopedValueSetter<int> quiet (quietEdits, quietEdits + (hostHistory ? 1 : 0));
    if (step.sampleChanged && setSample != nullptr)
    {
        setSample (forward ? step.sampleAfter : step.sampleBefore);
        tellHostStateChanged();
    }
    if (step.nameChanged && setName != nullptr)
        setName (forward ? step.nameAfter : step.nameBefore);
    const auto& params = processor.getParameters();
    auto setAll = [&] (bool asGestures)
    {
        for (const auto& c : step.changes)
        {
            if (c.index < 0 || c.index >= params.size())
                continue;
            auto* param = params[c.index];
            if (asGestures) param->beginChangeGesture();
            param->setValueNotifyingHost (forward ? c.after : c.before);
            if (asGestures) param->endChangeGesture();
        }
    };
    if (isQuiet())
        runQuietly ([&] { setAll (false); });
    else
        setAll (true);
}

bool History::undo()
{
    if (hostHistory || undoSteps.empty() || stepOpen)
        return false;
    auto step = std::move (undoSteps.back());
    undoSteps.pop_back();
    apply (step, false);
    redoSteps.push_back (std::move (step));
    sendChangeMessage();
    return true;
}

bool History::redo()
{
    if (hostHistory || redoSteps.empty() || stepOpen)
        return false;
    auto step = std::move (redoSteps.back());
    redoSteps.pop_back();
    apply (step, true);
    undoSteps.push_back (std::move (step));
    sendChangeMessage();
    return true;
}

void History::clear()
{
    undoSteps.clear();
    redoSteps.clear();
    stepOpen = false;
    gestures = 0;
    currentStep = stepValue();
    sendChangeMessage();
}

History::Transaction::Transaction (History& h, const juce::String& name) : history (h)
{
    ++history.transactions;
    if (history.suspended == 0 && ! history.stepOpen)
        history.open (name);
    else if (history.stepOpen && history.openName.isEmpty())
        history.openName = name;
    // In a host the group's values go quietly; a project loading (suspended) sets them as the host restores it.
    quiet = history.hostHistory && history.suspended == 0;
    if (quiet)
        ++quietEdits;
}

History::Transaction::~Transaction()
{
    if (quiet)
        --quietEdits;
    history.transactions = juce::jmax (0, history.transactions - 1);
    if (history.transactions == 0 && history.gestures == 0)
        history.close();
}
}
