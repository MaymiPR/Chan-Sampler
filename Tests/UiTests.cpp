// Offline checks of the editor's shared controls and the undo history, with
// the editor built but not shown (a display is still needed: xvfb on Linux).
//
//   ChanSamplerUiTests <sample.wav>
//
// Checks: every drop-down list is the shared fl::ComboBox (so all of them get
// the same arrow, animation and menu); a choice made in any list reaches its
// parameter and is still shown after the editor is rebuilt; the history
// records gestures as single steps, skips edits that change nothing, drops
// the redo steps on a new edit, keeps to its depth, and restores a sample.

#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << (detail.isEmpty() ? "" : "  (" + detail + ")") << "\n";
        failures += ok ? 0 : 1;
    }

    void pump (int ms = 20)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    template <typename T>
    void forEach (juce::Component& c, const std::function<void (T&)>& f)
    {
        if (auto* t = dynamic_cast<T*> (&c))
            f (*t);
        for (auto* child : c.getChildren())
            forEach<T> (*child, f);
    }

    juce::RangedAudioParameter* paramOf (ChanSamplerProcessor& p, const juce::String& id) { return p.apvts.getParameter (id); }

    // A user's drag as the attachments send it: one gesture around the values.
    void gesture (juce::RangedAudioParameter& p, std::initializer_list<float> normalised)
    {
        p.beginChangeGesture();
        for (auto v : normalised)
            p.setValueNotifyingHost (v);
        p.endChangeGesture();
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: ChanSamplerUiTests sample.wav\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File sample (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));

    ChanSamplerProcessor processor;
    processor.prepareToPlay (44100.0, 512);
    auto makeEditor = [&] { return std::unique_ptr<ChanSamplerEditor> (static_cast<ChanSamplerEditor*> (processor.createEditor())); };

    // --- Drop-down lists: one shared component, and the choice sticks
    {
        auto editor = makeEditor();
        pump();
        int lists = 0, shared = 0;
        forEach<juce::ComboBox> (*editor, [&] (juce::ComboBox& box)
        {
            ++lists;
            if (dynamic_cast<fl::ComboBox*> (&box) != nullptr)
                ++shared;
            else
                std::cout << "      plain juce::ComboBox: " << box.getTooltip() << " (" << box.getNumItems() << " items)\n";
        });
        check (lists == 4, "four drop-down lists in the editor", juce::String (lists));
        check (shared == lists, "every drop-down list is fl::ComboBox", juce::String (shared) + " of " + juce::String (lists));

        // each list: choose every item, the parameter follows and the box keeps showing it
        const char* ids[] = { pid::declick, pid::stretchMode, pid::filterType, pid::arpChord };
        for (auto* id : ids)
        {
            auto* param = dynamic_cast<juce::AudioParameterChoice*> (paramOf (processor, id));
            std::vector<juce::ComboBox*> boxes;
            forEach<juce::ComboBox> (*editor, [&] (juce::ComboBox& box)
            {
                if (box.getNumItems() == param->choices.size() && box.getItemText (0) == param->choices[0])
                    boxes.push_back (&box);
            });
            check (boxes.size() == 1, juce::String ("one list for ") + id, juce::String ((int) boxes.size()));
            if (boxes.size() != 1) continue;
            auto& box = *boxes.front();
            bool allOk = true;
            for (int i = 0; i < param->choices.size(); ++i)
            {
                box.setSelectedItemIndex (i, juce::sendNotificationSync);
                pump (5);
                const int shown = box.getSelectedItemIndex();
                const int stored = param->getIndex();
                if (shown != i || stored != i)
                {
                    allOk = false;
                    std::cout << "      " << id << " item " << i << ": box shows " << shown << ", parameter holds " << stored << "\n";
                }
            }
            check (allOk, juce::String ("every choice of ") + id + " reaches its parameter and stays shown");
            // the choice survives a sheet change and a rebuilt editor
            const int last = param->choices.size() - 1;
            box.setSelectedItemIndex (last, juce::sendNotificationSync);
            pump (5);
            editor->showSheetForTesting (2, -1);
            editor->showSheetForTesting (0, -1);
            pump (5);
            check (box.getSelectedItemIndex() == last, juce::String (id) + " keeps its choice across sheets");
        }
        editor.reset();
        auto again = makeEditor();
        pump();
        int kept = 0;
        for (auto* id : ids)
        {
            auto* param = dynamic_cast<juce::AudioParameterChoice*> (paramOf (processor, id));
            forEach<juce::ComboBox> (*again, [&] (juce::ComboBox& box)
            {
                if (box.getNumItems() == param->choices.size() && box.getItemText (0) == param->choices[0]
                    && box.getSelectedItemIndex() == param->choices.size() - 1)
                    ++kept;
            });
        }
        check (kept == 4, "a rebuilt editor shows the stored choices", juce::String (kept) + " of 4");
    }

    // --- History
    auto& h = processor.history;
    processor.resetParameters();
    h.clear();
    {
        auto* feed = paramOf (processor, pid::echoFeed);
        check (! h.canUndo() && ! h.canRedo(), "history starts empty");
        gesture (*feed, { 0.1f, 0.2f, 0.3f });
        check (h.getNumUndoSteps() == 1, "a drag of several values is one step", juce::String (h.getNumUndoSteps()));
        check (h.getUndoName() == feed->getName (64), "the step is named after the parameter", h.getUndoName());
        gesture (*feed, { 0.3f });
        check (h.getNumUndoSteps() == 1, "setting the same value adds no step", juce::String (h.getNumUndoSteps()));
        h.undo();
        check (std::abs (feed->getValue() - 0.0f) < 1.0e-6f && h.canRedo() && ! h.canUndo(), "undo restores the value", juce::String (feed->getValue()));
        h.redo();
        check (std::abs (feed->getValue() - 0.3f) < 1.0e-6f && h.canUndo() && ! h.canRedo(), "redo puts it back", juce::String (feed->getValue()));
        h.undo();
        gesture (*feed, { 0.5f });
        check (! h.canRedo() && h.getNumUndoSteps() == 1, "a new edit after undo clears the redo steps");

        // two parameters in one gesture window (the envelope's decay point, a loop link) make one step
        auto* a = paramOf (processor, pid::cutoff);
        auto* b = paramOf (processor, pid::resonance);
        a->beginChangeGesture();
        b->beginChangeGesture();
        a->setValueNotifyingHost (0.25f);
        b->setValueNotifyingHost (0.75f);
        a->endChangeGesture();
        b->endChangeGesture();
        check (h.getNumUndoSteps() == 2, "overlapping gestures make one step", juce::String (h.getNumUndoSteps()));
        h.undo();
        check (std::abs (a->getValue() - 1.0f) < 1.0e-6f && std::abs (b->getValue() - 0.0f) < 1.0e-6f, "undo restores both");
        h.redo();

        // a transaction groups complete gestures
        {
            const History::Transaction edit (h, "Echo preset");
            gesture (*paramOf (processor, pid::echoPan), { 0.2f });
            gesture (*paramOf (processor, pid::echoTime), { 0.6f });
        }
        check (h.getNumUndoSteps() == 3 && h.getUndoName() == "Echo preset", "a transaction is one named step", h.getUndoName());

        // host automation (no gesture) is not a step
        feed->setValueNotifyingHost (0.9f);
        check (h.getNumUndoSteps() == 3, "a value set without a gesture is not a step");

        // the depth limit
        for (int i = 0; i < History::maxSteps + 20; ++i)
            gesture (*feed, { (float) (i % 50) / 100.0f });
        check (h.getNumUndoSteps() == History::maxSteps, "the history keeps to its depth", juce::String (h.getNumUndoSteps()));
    }

    // --- The sample: load, undo (none), redo (back), with its parameters
    {
        h.clear();
        auto* root = paramOf (processor, pid::rootNote);
        gesture (*root, { root->convertTo0to1 (48.0f) });
        check (processor.loadSample (sample), "the sample loads", processor.getLoadError());
        processor.waitForPendingRebuilds();
        check (h.getNumUndoSteps() == 2 && h.getUndoName() == "Load sample", "loading a sample is a step", h.getUndoName());
        const auto rootAfterLoad = root->getValue();
        h.undo();
        processor.waitForPendingRebuilds();
        check (processor.getSampleFile() == juce::File() && processor.getSampleLength() == 0, "undoing the load removes the sample");
        check (std::abs (root->convertFrom0to1 (root->getValue()) - 48.0f) < 1.0e-3f, "and restores the root note the load had changed",
               juce::String (root->convertFrom0to1 (root->getValue())));
        h.redo();
        processor.waitForPendingRebuilds();
        check (processor.getSampleFile() == sample && processor.getSampleLength() > 0 && root->getValue() == rootAfterLoad, "redo loads it again");
        processor.removeSample();
        check (h.getUndoName() == "Remove sample", "removing it is a step", h.getUndoName());
        h.undo();
        processor.waitForPendingRebuilds();
        check (processor.getSampleFile() == sample && processor.getSampleLength() > 0, "undo brings it back");
        // a project loading (the host) starts the history afresh
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        processor.setStateInformation (state.getData(), (int) state.getSize());
        check (! h.canUndo() && ! h.canRedo(), "a project loaded clears the history");
    }

    // --- The editor's undo keys and buttons
    {
        auto editor = makeEditor();
        pump();
        auto* feed = paramOf (processor, pid::echoFeed);
        const auto before = feed->getValue();
        gesture (*feed, { 0.4f });
        pump();
        juce::Button* undo = nullptr;
        forEach<juce::Button> (*editor, [&] (juce::Button& b) { if (b.getTooltip().startsWith ("Undo ")) undo = &b; });
        check (undo != nullptr && undo->isEnabled(), "the undo button is enabled after an edit", undo != nullptr ? undo->getTooltip() : "no button");
        if (undo != nullptr)
        {
            undo->triggerClick();
            pump();
            check (std::abs (feed->getValue() - before) < 1.0e-6f, "the undo button undoes");
            check (! undo->isEnabled(), "and is disabled with nothing left to undo");
        }
        gesture (*feed, { 0.4f });
        pump();
        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0), editor.get());
        check (std::abs (feed->getValue() - before) < 1.0e-6f, "Ctrl+Z undoes");
        const bool used = editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::altModifier, 0), editor.get());
        check (! used && std::abs (feed->getValue() - before) < 1.0e-6f, "Ctrl+Alt+Z is left to the host");
        editor->keyPressed (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0), editor.get());
        check (std::abs (feed->getValue() - 0.4f) < 1.0e-6f, "Ctrl+Y redoes");
        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0), editor.get());
        editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0), editor.get());
        check (std::abs (feed->getValue() - 0.4f) < 1.0e-6f, "Ctrl+Shift+Z redoes");
        // one press handed over twice (FL: to the window and through the VST3
        // key call, as the key and as its character) acts once; a press after
        // the key-repeat interval acts again
        gesture (*feed, { 0.1f });
        gesture (*feed, { 0.2f });
        gesture (*feed, { 0.3f });
        pump (40);
        editor->keyPressed (juce::KeyPress ('Z', juce::ModifierKeys::commandModifier, 'z'), editor.get());
        editor->keyPressed (juce::KeyPress (26, juce::ModifierKeys::commandModifier, 26), editor.get());
        check (std::abs (feed->getValue() - 0.2f) < 1.0e-6f, "one press delivered twice undoes once", juce::String (feed->getValue()));
        juce::Thread::sleep (40);
        editor->keyPressed (juce::KeyPress ('Z', juce::ModifierKeys::commandModifier, 'z'), editor.get());
        check (std::abs (feed->getValue() - 0.1f) < 1.0e-6f, "the next press undoes again", juce::String (feed->getValue()));
        // with nothing to undo / redo the keys are not used, so the host gets them
        juce::Thread::sleep (40);
        h.clear();
        check (! editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0), editor.get())
                   && ! editor->keyPressed (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0), editor.get()),
               "Ctrl+Z / Ctrl+Y with an empty history go on to the host");
        check (editor->getWantsKeyboardFocus(), "the editor takes the keyboard focus (so the keys reach it in a plugin window)");
        // the buttons' tooltips name the keys
        gesture (*feed, { 0.6f });
        h.undo();
        pump();
        juce::Button* redo = nullptr;
        forEach<juce::Button> (*editor, [&] (juce::Button& b) { if (b.getTooltip().startsWith ("Redo ")) redo = &b; });
        check (redo != nullptr && redo->getTooltip().contains ("+Y)"), "the redo button names Ctrl+Y", redo != nullptr ? redo->getTooltip() : "no button");
    }

    // --- The wheel: the value moves at once, the knob eases to it (FL: ~36 ms)
    {
        auto editor = makeEditor();
        pump();
        juce::Slider* any = nullptr;
        forEach<ParamKnob> (*editor, [&] (ParamKnob& k) { if (any == nullptr && k.isRotary() && k.isShowing()) any = &k; });
        if (any == nullptr)
            forEach<ParamKnob> (*editor, [&] (ParamKnob& k) { if (any == nullptr && k.isRotary()) any = &k; });
        check (any != nullptr, "a knob to scroll");
        if (any != nullptr)
        {
            any->setValue (any->proportionOfLengthToValue (0.5), juce::sendNotificationSync);
            pump();
            const auto before = any->getValue();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), any->getLocalBounds().getCentre().toFloat(),
                                      juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, any, any, juce::Time::getCurrentTime(),
                                      any->getLocalBounds().getCentre().toFloat(), juce::Time::getCurrentTime(), 0, false);
            any->mouseWheelMove (e, { 0.0f, 0.46875f, false, false });
            const auto after = any->getValue();
            const auto* shown = any->getProperties().getVarPointer ("shown");
            check (after > before, "a notch moves the value at once");
            check (shown != nullptr && std::abs ((double) *shown - any->valueToProportionOfLength (before)) < 1.0e-6,
                   "and the knob is drawn where it was, to ease from there");
            pump (60);
            const auto* mid = any->getProperties().getVarPointer ("shown");
            const auto p0 = any->valueToProportionOfLength (before), p1 = any->valueToProportionOfLength (after);
            const auto midP = mid != nullptr ? (double) *mid : p1;
            check (mid == nullptr || (midP > p0 && midP < p1 + 1.0e-9), "after 60 ms it is on its way", juce::String ((midP - p0) / (p1 - p0), 2));
            pump (400);
            check (any->getProperties().getVarPointer ("shown") == nullptr, "and after 400 ms it is there");
            any->mouseWheelMove (e, { 0.0f, 0.46875f, false, false });
            any->setValue (any->proportionOfLengthToValue (0.2), juce::sendNotificationSync);
            check (any->getProperties().getVarPointer ("shown") == nullptr, "any other change shows at once");
        }
    }

    // --- The panel menu arrows stay turned while their menu is open
    {
        auto editor = makeEditor();
        pump();
        fl::MenuArrow* arrow = nullptr;
        forEach<fl::MenuArrow> (*editor, [&] (fl::MenuArrow& a) { if (arrow == nullptr) arrow = &a; });
        check (arrow != nullptr, "a panel menu arrow");
        if (arrow != nullptr)
        {
            juce::PopupMenu menu;
            menu.addItem ("one", [] {});
            arrow->showMenu (menu);
            pump (400);
            check (fl::turnOf (*arrow) > 0.99f, "turned while its menu is open (the mouse elsewhere)", juce::String (fl::turnOf (*arrow)));
            juce::PopupMenu::dismissAllActiveMenus();
            pump (500);
            check (fl::turnOf (*arrow) < 0.01f, "turned back once the menu closes", juce::String (fl::turnOf (*arrow)));
        }
    }

    std::cout << "\n" << (failures == 0 ? "All UI tests passed" : juce::String (failures) + " UI test(s) failed") << "\n";
    return failures == 0 ? 0 : 1;
}
