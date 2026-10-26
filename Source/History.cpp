#include "History.h"

History::History (juce::AudioProcessor& p, SampleGetter getter, SampleSetter setter)
    : processor (p), getSample (std::move (getter)), setSample (std::move (setter))
{
    for (auto* param : processor.getParameters())
        param->addListener (this);
}

History::~History()
{
    for (auto* param : processor.getParameters())
        param->removeListener (this);
}

History::Snapshot History::snapshot() const
{
    Snapshot s;
    const auto& params = processor.getParameters();
    s.values.reserve ((size_t) params.size());
    for (auto* param : params)
        s.values.push_back (param->getValue());
    s.sample = getSample != nullptr ? getSample() : juce::File();
    return s;
}

void History::parameterGestureChanged (int index, bool starting)
{
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
        if (now.values[i] != opened.values[i])
            step.changes.push_back ({ (int) i, opened.values[i], now.values[i] });
    if (now.sample != opened.sample)
    {
        step.sampleChanged = true;
        step.sampleBefore = opened.sample;
        step.sampleAfter = now.sample;
    }
    if (step.changes.empty() && ! step.sampleChanged)
        return;   // nothing changed: no step
    if (step.name.isEmpty())
        step.name = step.sampleChanged ? juce::String ("sample") : params[step.changes.front().index]->getName (64);
    undoSteps.push_back (std::move (step));
    while ((int) undoSteps.size() > maxSteps)
        undoSteps.pop_front();
    redoSteps.clear();
    sendChangeMessage();
}

void History::apply (const Step& step, bool forward)
{
    const ScopedSuspend suspend (*this);
    if (step.sampleChanged && setSample != nullptr)
        setSample (forward ? step.sampleAfter : step.sampleBefore);
    const auto& params = processor.getParameters();
    for (const auto& c : step.changes)
    {
        if (c.index < 0 || c.index >= params.size())
            continue;
        auto* param = params[c.index];
        param->beginChangeGesture();
        param->setValueNotifyingHost (forward ? c.after : c.before);
        param->endChangeGesture();
    }
}

bool History::undo()
{
    if (undoSteps.empty() || stepOpen)
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
    if (redoSteps.empty() || stepOpen)
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
    sendChangeMessage();
}

History::Transaction::Transaction (History& h, const juce::String& name) : history (h)
{
    ++history.transactions;
    if (history.suspended == 0 && ! history.stepOpen)
        history.open (name);
    else if (history.stepOpen && history.openName.isEmpty())
        history.openName = name;
}

History::Transaction::~Transaction()
{
    history.transactions = juce::jmax (0, history.transactions - 1);
    if (history.transactions == 0 && history.gestures == 0)
        history.close();
}
