// Offline checks of the editor's shared controls and the undo history, with
// the editor built but not shown (a display is still needed: xvfb on Linux).
//
//   ChanSamplerUiTests <sample.wav>
//
// Checks: every drop-down list is the shared chan::ui::ComboBox (so all of them get
// the same arrow, animation and menu); a choice made in any list reaches its
// parameter and is still shown after the editor is rebuilt; the history
// records gestures as single steps, skips edits that change nothing, drops
// the redo steps on a new edit, keeps to its depth, and restores a sample; as
// a plugin, group edits are one step of the host's history (the History step
// parameter) that the host's undo and redo put back.

#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <thread>
#if JUCE_LINUX
 #include <dlfcn.h>
#endif

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PresetImport.h"
#include "Presets.h"

using namespace cs;

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& name, const juce::String& detail = {})
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << name << (detail.isEmpty() ? "" : "  (" + detail + ")") << "\n";
        failures += ok ? 0 : 1;
    }

   #if JUCE_LINUX
    // JUCE puts in an X error handler that ignores errors only for its own
    // application class (as Chan Sampler's standalone app has); this is a
    // plain program, so Xlib's default handler would end it on the first
    // error. Under CI's xvfb, with no window manager, the first real window
    // (a popup menu) gets its WM_PROTOCOLS property written with atom 0, as
    // nothing has created that atom (every desktop's window manager does), and
    // the server answers BadAtom. The same handler as JUCE's applications'.
    int ignoreXError (void*, void*) { return 0; }

    void ignoreXErrors()
    {
        if (auto* x11 = dlopen ("libX11.so.6", RTLD_LAZY))
            if (auto* set = reinterpret_cast<void* (*) (int (*) (void*, void*))> (dlsym (x11, "XSetErrorHandler")))
                set (ignoreXError);
    }
   #endif

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
   #if JUCE_LINUX
    ignoreXErrors();
   #endif
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
            if (dynamic_cast<chan::ui::ComboBox*> (&box) != nullptr)
                ++shared;
            else
                std::cout << "      plain juce::ComboBox: " << box.getTooltip() << " (" << box.getNumItems() << " items)\n";
        });
        check (lists == 4, "four drop-down lists in the editor", juce::String (lists));
        check (shared == lists, "every drop-down list is chan::ui::ComboBox", juce::String (shared) + " of " + juce::String (lists));

        // each list: choose every item, the parameter follows and the box keeps showing it
        const char* ids[] = { id::declick, id::stretchMode, id::filterType, id::arpChord };
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
        auto* feed = paramOf (processor, id::echoFeed);
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
        auto* a = paramOf (processor, id::cutoff);
        auto* b = paramOf (processor, id::resonance);
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
            gesture (*paramOf (processor, id::echoPan), { 0.2f });
            gesture (*paramOf (processor, id::echoTime), { 0.6f });
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
        auto* root = paramOf (processor, id::rootNote);
        gesture (*root, { root->convertTo0to1 (48.0f) });
        check (processor.loadSample (sample), "the sample loads", processor.getLoadError());
        processor.waitForPendingRebuilds();
        check (h.getNumUndoSteps() == 2 && h.getUndoName() == "Load sample: " + sample.getFileName(), "loading a sample is a step", h.getUndoName());
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
        auto* feed = paramOf (processor, id::echoFeed);
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

    // --- A key that does nothing here is not used, so the host gets it: Ctrl+C with no sample to copy
    {
        ChanSamplerProcessor empty;
        std::unique_ptr<ChanSamplerEditor> editor (static_cast<ChanSamplerEditor*> (empty.createEditor()));
        pump();
        check (! editor->keyPressed (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0), editor.get()),
               "Ctrl+C with no sample goes on to the host");
    }

    // --- Undo of a sample change brings the sample back with the values the user had, not the ones its file sets
    // (a WAV that carries its own root note, which the user had changed)
    {
        juce::MemoryOutputStream wav;
        const int frames = 2000;
        wav.write ("RIFF", 4);  wav.writeInt (4 + 24 + 44 + 8 + frames * 2);  wav.write ("WAVE", 4);
        wav.write ("fmt ", 4);  wav.writeInt (16);  wav.writeShort (1);  wav.writeShort (1);  wav.writeInt (44100);
        wav.writeInt (88200);   wav.writeShort (2);  wav.writeShort (16);
        wav.write ("smpl", 4);  wav.writeInt (36);
        for (int v : { 0, 0, 22675, 48, 0, 0, 0, 0, 0 })      // the fourth field is the file's root note: 48
            wav.writeInt (v);
        wav.write ("data", 4);  wav.writeInt (frames * 2);
        for (int i = 0; i < frames; ++i)
            wav.writeShort ((short) (8000.0 * std::sin (i * 0.05)));
        const auto withRoot = juce::File::createTempFile (".wav");
        withRoot.replaceWithData (wav.getData(), wav.getDataSize());

        processor.resetParameters();
        auto* root = paramOf (processor, id::rootNote);
        processor.loadSample (withRoot);
        const auto fileRoot = juce::roundToInt (root->convertFrom0to1 (root->getValue()));
        gesture (*root, { root->convertTo0to1 (60.0f) });      // the user's own root note
        h.clear();
        processor.loadSample (sample);                         // another sample: one step
        h.undo();
        const auto back = juce::roundToInt (root->convertFrom0to1 (root->getValue()));
        check (fileRoot == 48 && processor.getSampleFile() == withRoot && back == 60,
               "undo of a sample change keeps the root note the user had set",
               "the file's " + juce::String (fileRoot) + ", after undo " + juce::String (back));
        processor.loadSample (sample);
        withRoot.deleteFile();
        processor.resetParameters();
        h.clear();
    }

    // --- A PITCH drag away from 0 switches Resample to Auto when it is let go: the drag and the switch are one step
    {
        processor.resetParameters();
        auto* mode = paramOf (processor, id::stretchMode);
        auto* pitchParam = paramOf (processor, id::pitch);
        mode->setValueNotifyingHost (mode->convertTo0to1 ((float) dsp::StretchMode::resample));
        auto editor = makeEditor();
        pump (50);
        h.clear();
        ParamKnob* knob = nullptr;
        forEach<ParamKnob> (*editor, [&] (ParamKnob& k) { if (knob == nullptr && k.getTooltip().startsWithIgnoreCase ("Pitch shift")) knob = &k; });
        check (knob != nullptr, "the PITCH knob");
        if (knob != nullptr)
        {
            const auto centre = knob->getLocalBounds().getCentre().toFloat();
            const auto now = juce::Time::getCurrentTime();
            auto event = [&] (juce::Point<float> at, bool dragged)
            {
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys::leftButtonModifier, 0.0f,
                                         0.0f, 0.0f, 0.0f, 0.0f, knob, knob, now, centre, now, 1, dragged);
            };
            knob->mouseDown (event (centre, false));
            knob->mouseDrag (event (centre.translated (0.0f, -40.0f), true));
            knob->mouseUp (event (centre.translated (0.0f, -40.0f), true));
            pump (50);
            const bool moved = std::abs (pitchParam->convertFrom0to1 (pitchParam->getValue())) > 0.5f;
            const bool isAuto = juce::roundToInt (mode->convertFrom0to1 (mode->getValue())) == (int) dsp::StretchMode::autoMode;
            check (moved && isAuto && h.getNumUndoSteps() == 1, "a PITCH drag and the switch to Auto it causes are one step",
                   juce::String (h.getNumUndoSteps()) + " steps, moved " + juce::String ((int) moved) + ", Auto " + juce::String ((int) isAuto));
        }
        editor.reset();
        processor.resetParameters();
        h.clear();
    }

    // --- A project whose sample is no longer at its saved path finds it by its name in the folder a sample was last
    // loaded from (a sample library that moved); with no file of that name anywhere it stays missing, its path kept
    {
        const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cs_path_test");
        base.deleteRecursively();
        const auto oldPlace = base.getChildFile ("old"), newPlace = base.getChildFile ("new");
        oldPlace.createDirectory();
        newPlace.createDirectory();
        sample.copyFileTo (oldPlace.getChildFile ("moved.wav"));
        sample.copyFileTo (newPlace.getChildFile ("other.wav"));
        processor.loadSample (oldPlace.getChildFile ("moved.wav"));
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        oldPlace.getChildFile ("moved.wav").moveFileTo (newPlace.getChildFile ("moved.wav"));   // the library moved
        processor.loadSample (newPlace.getChildFile ("other.wav"));                                // and the user loaded from there
        processor.setStateInformation (saved.getData(), (int) saved.getSize());
        check (processor.getSampleFile() == newPlace.getChildFile ("moved.wav") && processor.getSampleLength() > 0,
               "a sample that moved is found by its name where a sample was last loaded from",
               processor.getSampleFile().getFileName() + ", " + juce::String (processor.getSampleLength()) + " frames");
        newPlace.getChildFile ("moved.wav").deleteFile();
        processor.setStateInformation (saved.getData(), (int) saved.getSize());
        check (processor.getSampleFile() == oldPlace.getChildFile ("moved.wav") && processor.getSampleLength() == 0,
               "a sample found nowhere stays missing with its saved path");
        base.deleteRecursively();
        processor.loadSample (sample);
        processor.resetParameters();
        h.clear();
    }

    // --- An editor closed in the middle of a drag closes the gestures the drag opened (an open gesture would keep
    // the history from ever closing a step)
    {
        struct Gestures : juce::AudioProcessorParameter::Listener
        {
            void parameterValueChanged (int, float) override {}
            void parameterGestureChanged (int, bool starting) override { open += starting ? 1 : -1; }
            int open = 0;
        } seen;
        for (auto* p : processor.getParameters())
        {
            p->addListener (&seen);
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->getParameterID().endsWith ("EnvOn"))
                    rp->setValueNotifyingHost (1.0f);          // an envelope's points can be dragged when it is on
        }
        auto editor = makeEditor();
        pump (50);
        int opened = 0;
        forEach<EnvelopeDisplay> (*editor, [&] (EnvelopeDisplay& view)
        {
            if (opened > 0)
                return;
            for (int y = 2; y < view.getHeight() && seen.open == 0; y += 2)
                for (int x = 2; x < view.getWidth() && seen.open == 0; x += 2)
                {
                    const juce::Point<float> at ((float) x, (float) y);
                    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f, 0.0f,
                                              0.0f, 0.0f, 0.0f, &view, &view, juce::Time::getCurrentTime(), at,
                                              juce::Time::getCurrentTime(), 1, false);
                    view.mouseDown (e);
                }
            opened = seen.open;
        });
        editor.reset();
        pump (50);
        check (opened > 0 && seen.open == 0, "an editor closed in mid-drag closes the drag's gestures",
               juce::String (opened) + " opened, " + juce::String (seen.open) + " left open");
        for (auto* p : processor.getParameters())
            p->removeListener (&seen);
        processor.resetParameters();
        h.clear();
    }

    // --- A dialog left open when the editor closes is deleted with it
    {
        auto editor = makeEditor();
        chan::ui::showDialog (*editor, "Test", "A dialog left open");
        pump (50);
        const int shown = chan::ui::numOpenDialogs();
        editor.reset();
        pump (100);
        check (shown == 1 && chan::ui::numOpenDialogs() == 0, "a dialog left open is deleted with the editor",
               juce::String (shown) + " shown, " + juce::String (chan::ui::numOpenDialogs()) + " left");
    }

    // --- Values that are not a number (a damaged state, preset or automation): the plug-in plays on, its output
    // finite, each such value read as the parameter's default
    {
        processor.loadSample (sample);
        for (auto* p : processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->getParameterID() != id::historyStep)
                    rp->setValueNotifyingHost (std::numeric_limits<float>::quiet_NaN());
        processor.waitForPendingRebuilds();
        pump (200);
        juce::AudioBuffer<float> block (2, 512);
        bool finite = true;
        for (int i = 0; i < 40; ++i)
        {
            block.clear();
            juce::MidiBuffer midi;
            if (i == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            if (i == 20) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            processor.processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int k = 0; k < 512; ++k)
                    finite = finite && std::isfinite (block.getSample (ch, k));
        }
        check (finite, "values that are not a number give a finite output");
        processor.resetParameters();
        processor.waitForPendingRebuilds();
        pump (200);
        h.clear();
    }

    // --- Cut self and the two groups it sets are one step, and one undo takes all three back
    {
        pump (120);                       // the processor's timer has seen the state it starts from
        h.clear();
        auto* cut = paramOf (processor, id::cutItself);
        auto* group = paramOf (processor, id::cutGroup);
        auto* by = paramOf (processor, id::cutBy);
        gesture (*cut, { 1.0f });
        pump (120);
        check (h.getNumUndoSteps() == 1 && group->getValue() > 0.0f && by->getValue() > 0.0f,
               "Cut self and the two groups it sets are one step", juce::String (h.getNumUndoSteps()) + " steps");
        while (h.canUndo())
            h.undo();
        pump (120);
        check (cut->getValue() < 0.5f && group->getValue() == 0.0f && by->getValue() == 0.0f, "undo takes Cut self and both groups back");
        h.clear();
    }

    // --- A marker drag has its gestures open: the loop link moves END inside them, with no gesture of its own
    {
        struct Gestures : juce::AudioProcessorParameter::Listener
        {
            void parameterValueChanged (int, float) override {}
            void parameterGestureChanged (int, bool starting) override { open += starting ? 1 : -1; deepest = juce::jmax (deepest, open); }
            int open = 0, deepest = 0;
        } seen;
        auto* start = paramOf (processor, id::loopStart);
        auto* end = paramOf (processor, id::loopEnd);
        end->setValueNotifyingHost (0.5f);
        end->addListener (&seen);
        start->beginChangeGesture();
        end->beginChangeGesture();
        start->setValueNotifyingHost (0.7f);
        processor.applyLoopLink (id::loopStart, true);
        start->endChangeGesture();
        end->endChangeGesture();
        end->removeListener (&seen);
        check (seen.deepest == 1 && seen.open == 0 && end->getValue() > 0.7f, "the loop link opens no gesture inside a drag's",
               juce::String (seen.deepest) + " deep");
        processor.resetParameters();
        h.clear();
    }

    // --- A preset file whose event claims more bytes than a file can hold is refused, not read
    {
        juce::MemoryOutputStream bytes;
        bytes.write ("FLhd", 4);
        bytes.writeInt (6);
        bytes.writeRepeatedByte (0, 6);
        bytes.write ("FLdt", 4);
        bytes.writeInt (12);
        bytes.writeByte ((char) 0xc0);          // a text or data event: its length follows, 7 bits a byte
        bytes.writeRepeatedByte (0xff, 10);     // ten bytes that each say another follows
        bytes.writeByte (0x01);
        const auto file = juce::File::createTempFile (".fst");
        file.replaceWithData (bytes.getData(), bytes.getDataSize());
        ChanSamplerProcessor target;
        const auto result = presetimport::import (target, file);
        file.deleteFile();
        check (! result.ok, "a preset with an impossible event length is refused", result.message);
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
            // Eased, not jumped: some frame draws it part way. Sampled every 5 ms for up
            // to 400 ms, as a busy machine (a CI runner) can deliver the first frame late.
            const auto p0 = any->valueToProportionOfLength (before), p1 = any->valueToProportionOfLength (after);
            double midP = p0;
            for (int t = 0; t < 400 && ! (midP > p0 + 1.0e-9 && midP < p1 - 1.0e-9); t += 5)
            {
                pump (5);
                const auto* mid = any->getProperties().getVarPointer ("shown");
                midP = mid != nullptr ? (double) *mid : p1;
            }
            check (midP > p0 + 1.0e-9 && midP < p1 - 1.0e-9, "on its way it is drawn part way", juce::String ((midP - p0) / (p1 - p0), 2));
            pump (400);
            check (any->getProperties().getVarPointer ("shown") == nullptr, "and after 400 ms it is there");
            any->mouseWheelMove (e, { 0.0f, 0.46875f, false, false });
            any->setValue (any->proportionOfLengthToValue (0.2), juce::sendNotificationSync);
            check (any->getProperties().getVarPointer ("shown") == nullptr, "any other change shows at once");
        }
    }

    // --- The panel menu arrows stay turned while their menu is open (a real popup menu)
    {
        auto editor = makeEditor();
        pump();
        chan::ui::MenuArrow* arrow = nullptr;
        forEach<chan::ui::MenuArrow> (*editor, [&] (chan::ui::MenuArrow& a) { if (arrow == nullptr) arrow = &a; });
        check (arrow != nullptr, "a panel menu arrow");
        if (arrow != nullptr)
        {
            juce::PopupMenu menu;
            menu.addItem ("one", [] {});
            arrow->showMenu (menu);
            // The arrow eases round; sampled every 20 ms for up to 2 s, as a busy machine
            // (a CI runner) can deliver its frames late
            pump (400);
            for (int t = 0; t < 1600 && chan::ui::turnOf (*arrow) <= 0.99f
                            && juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0; t += 20)
                pump (20);
            // JUCE closes a menu whose program is not in the foreground: under
            // a bare xvfb (no window manager) nothing ever is
            if (juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0)
                std::cout << "SKIP  turned while its menu is open: the menu was closed by the environment (no foreground window here)" << std::endl;
            else
                check (chan::ui::turnOf (*arrow) > 0.99f, "turned while its menu is open (the mouse elsewhere)", juce::String (chan::ui::turnOf (*arrow)));
            juce::PopupMenu::dismissAllActiveMenus();
            pump (500);
            for (int t = 0; t < 1500 && chan::ui::turnOf (*arrow) >= 0.01f; t += 20)
                pump (20);
            check (chan::ui::turnOf (*arrow) < 0.01f, "turned back once the menu closes", juce::String (chan::ui::turnOf (*arrow)));
        }
    }

    // --- Type in value: FL's box and FL's readings per unit (measured in FL); Escape closes it safely
    {
        auto editor = makeEditor();
        pump();
        auto knobFor = [&] (const juce::String& name)
        {
            ParamKnob* found = nullptr;
            forEach<ParamKnob> (*editor, [&] (ParamKnob& k) { if (found == nullptr && k.getTooltip().startsWithIgnoreCase (name)) found = &k; });
            return found;
        };
        auto near = [] (std::optional<float> v, double want, double tol = 1.0e-3) { return v.has_value() && std::abs ((double) *v - want) < tol; };
        auto shown = [] (std::optional<float> v) { return v.has_value() ? juce::String (*v, 4) : juce::String ("nothing"); };
        auto* out = knobFor ("Output");
        auto* vol = knobFor ("Volume multiplier");
        auto* pan = knobFor ("Panning offset");
        auto* feed = knobFor ("Delay feedback");
        auto* pitch = knobFor ("Delay pitch");
        auto* time = knobFor ("Delay time");
        check (out != nullptr && vol != nullptr && pan != nullptr && feed != nullptr && pitch != nullptr && time != nullptr,
               "the OUT, VOL, PAN, FEED, PITCH and TIME knobs");
        if (out != nullptr && vol != nullptr && pan != nullptr && feed != nullptr && pitch != nullptr && time != nullptr)
        {
            using U = ParamMenu::Unit;
            check (out->menu.unit() == U::decibels && vol->menu.unit() == U::decibels && pan->menu.unit() == U::centred
                   && feed->menu.unit() == U::percent && pitch->menu.unit() == U::cents && time->menu.unit() == U::steps, "each knob's unit as in FL");
            // OUT is FL's channel volume: -6 is -6 dB at 0.748 (FL), 0 dB at the top, 78 % shows -5.2dB
            check (near (out->menu.typedPosition ("-6"), 0.748) && near (out->menu.typedPosition ("-6dB"), 0.748), "OUT: -6 and -6dB at FL's 0.748",
                   shown (out->menu.typedPosition ("-6")));
            check (near (out->menu.typedPosition ("0"), 1.0) && near (out->menu.typedPosition ("6"), 1.0), "OUT: 0 dB and above are its top");
            check (out->menu.typedText (10000.0f / 12800.0f) == "-5.2dB", "OUT at 78% shows -5.2dB", out->menu.typedText (10000.0f / 12800.0f));
            check (near (out->menu.typedPosition ("0.5", true), 0.5) && near (out->menu.typedPosition ("50%", true), 0.5), "A: 0.5 and 50% are the middle");
            check (out->menu.typedText (0.780307471752167f, true).startsWith ("0.7803074"), "A shows the position in full", out->menu.typedText (0.780307471752167f, true));
            // Levels VOL (FL: -6 is 75 %, 6 is 127 %, -inf is silence)
            check (near (vol->menu.typedPosition ("-6"), 0.748 / 2.0) && near (vol->menu.typedPosition ("6"), 1.27 / 2.0, 0.005),
                   "VOL: -6 dB is 75 %, +6 dB 127 %", shown (vol->menu.typedPosition ("6")));
            check (near (vol->menu.typedPosition ("-inf dB"), 0.0) && vol->menu.typedText (0.5f) == "0.0dB", "VOL: -inf is silence, 100 % shows 0.0dB");
            // % (FL: 0..1 is a fraction, more a percentage); FEED shows twice its position
            check (near (feed->menu.typedPosition ("50"), 0.25) && near (feed->menu.typedPosition ("0.5"), 0.25) && near (feed->menu.typedPosition ("50%"), 0.25),
                   "FEED: 50, 0.5 and 50% are 50 %", shown (feed->menu.typedPosition ("0.5")));
            check (near (feed->menu.typedPosition ("1"), 0.5) && near (feed->menu.typedPosition ("2"), 0.01), "FEED: 1 is 100 %, 2 is 2 %");
            // C (FL: whole percents, minus or left for the left)
            check (near (pan->menu.typedPosition ("50"), 0.75) && near (pan->menu.typedPosition ("-50"), 0.25)
                   && near (pan->menu.typedPosition ("50% left"), 0.25) && near (pan->menu.typedPosition ("0.5"), 0.5), "PAN: 50 right, -50 and 50% left, 0.5 centred");
            check (pan->menu.typedText (0.5f) == "Centered", "PAN at the centre shows Centered", pan->menu.typedText (0.5f));
            // P and s (FL: 1 is +1 cent; a bare number counts ticks)
            check (near (pitch->menu.typedPosition ("1"), (1.0 + 1200.0) / 2400.0, 1.0e-5) && pitch->menu.typedText (0.5f) == "0 cents", "PITCH: 1 is +1 cent");
            check (near (time->menu.typedPosition ("2:00"), 2.0 / 8.0) && near (time->menu.typedPosition ("2"), 2.0 / 24.0 / 8.0, 1.0e-4)
                   && time->menu.typedText (3.0f / 8.0f) == "3:00", "TIME: 2:00 is 2 steps, 2 is 2 ticks, 3:00 shows as 3:00");
            // Envelope times (FL: the percentage is raw / 65536, A reads it back, 50 is 0.5)
            auto* att = knobFor ("Volume attack time");
            auto* relT = knobFor ("Volume release tension");
            check (att != nullptr && relT != nullptr, "the volume ATT and release TENSION knobs");
            if (att != nullptr && relT != nullptr)
            {
                check (att->menu.typedText (dsp::flEnvKnobForRaw (20000)) == "31%" && att->menu.typedText (dsp::flEnvKnobForRaw (16193)) == "25%"
                       && att->menu.typedText (dsp::flEnvKnobForRaw (53784)) == "82%" && att->menu.typedText (dsp::flEnvKnobForRaw (54722)) == "83%",
                       "ATT shows FL's 31, 25, 82 and 83 %", att->menu.typedText (dsp::flEnvKnobForRaw (20000)));
                check (att->menu.typedText (dsp::flEnvKnobForRaw (20000), true) == "0.31" && near (att->menu.typedPosition ("50"), 0.5),
                       "ATT: A shows 0.31, 50 is 0.5", att->menu.typedText (dsp::flEnvKnobForRaw (20000), true));
                const auto relDefault = paramOf (processor, id::env (id::volPrefix, "RelT"))->getDefaultValue();
                check (relT->menu.typedText (relDefault) == "-79%" && relT->menu.typedText (relDefault, true) == "0.105",
                       "release TENSION: -79%, A 0.105", relT->menu.typedText (relDefault, true));
            }
            auto& slide = *paramOf (processor, id::slide);
            check (slide.getText (slide.getDefaultValue(), 64) == "0:03", "SLIDE starts at FL's 0:03", slide.getText (slide.getDefaultValue(), 64));

            // The box: Escape closes it unapplied, Return applies, a closed editor takes it along
            auto& outParam = *paramOf (processor, id::output);
            auto findBox = [&]
            {
                juce::TextEditor* box = nullptr;
                forEach<juce::TextEditor> (*editor, [&] (juce::TextEditor& t) { if (box == nullptr && t.isVisible()) box = &t; });
                return box;
            };
            const auto before = outParam.getValue();
            out->menu.typeIn();
            pump();
            auto* box = findBox();
            check (box != nullptr && box->getText() == out->menu.typedText (before), "the box opens with the knob's value in dB",
                   box != nullptr ? box->getText() : juce::String ("no box"));
            if (box != nullptr)
            {
                box->setText ("-12", false);
                box->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
                pump (100);
                check (findBox() == nullptr && outParam.getValue() == before, "Escape closes it and keeps the value");
            }
            out->menu.typeIn();
            pump();
            if (auto* box2 = findBox())
            {
                box2->setText ("-12", false);
                box2->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                pump (100);
                check (findBox() == nullptr && std::abs ((double) outParam.getValue() - (double) *out->menu.typedPosition ("-12")) < 1.0e-6,
                       "Return applies -12 dB", out->menu.typedText (outParam.getValue()));
            }
            // A menu edit of an envelope knob switches the envelope on, as a drag does: inside the edit's step, so
            // one undo takes the value and the switch back.
            if (att != nullptr)
            {
                auto* envOn = paramOf (processor, id::env (id::volPrefix, "EnvOn"));
                gesture (*envOn, { 0.0f });
                h.clear();
                att->menu.typeIn();
                pump();
                if (auto* box3 = findBox())
                {
                    box3->setText ("50", false);
                    box3->keyPressed (juce::KeyPress (juce::KeyPress::returnKey));
                    pump (100);
                    const bool switchedOn = envOn->getValue() > 0.5f;
                    h.undo();
                    check (switchedOn && envOn->getValue() < 0.5f, "undo of an envelope knob's menu edit switches the envelope off again",
                           juce::String (h.getNumUndoSteps()) + " steps left");
                }
                else
                {
                    check (false, "the ATT knob's box opens");
                }
            }
            out->menu.typeIn();
            pump();
            editor.reset();   // the editor closes (FL closes the window) with the box open
            pump (100);
            check (true, "closing the editor with the box open is safe");
        }
    }

    // --- In a host (a plugin) the host's history is the undo history
    {
        // What a host hears: every parameter change and whether it came from
        // the message thread (an edit a host records; JUCE's VST3 wrapper sends
        // the others with the next audio block), and the gestures.
        struct HostEar : juce::AudioProcessorListener
        {
            struct Heard { int index; bool messageThread; };
            std::vector<Heard> changes;
            juce::CriticalSection lock;
            void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override
            {
                const juce::ScopedLock sl (lock);
                changes.push_back ({ index, juce::MessageManager::getInstance()->isThisTheMessageThread() });
            }
            void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
            void clear() { const juce::ScopedLock sl (lock); changes.clear(); }
            std::vector<int> edits()
            {
                const juce::ScopedLock sl (lock);
                std::vector<int> e;
                for (const auto& c : changes)
                    if (c.messageThread)
                        e.push_back (c.index);
                return e;
            }
        };

        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        ChanSamplerProcessor plugin;
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
        plugin.prepareToPlay (44100.0, 512);
        auto& hh = plugin.history;
        HostEar ear;
        plugin.addListener (&ear);
        auto* step = dynamic_cast<HistoryStepParameter*> (paramOf (plugin, id::historyStep));
        check (step != nullptr && hh.followsHost(), "a plugin follows the host's history");
        const int stepIndex = step != nullptr ? step->getParameterIndex() : -1;
        auto hostSetsStep = [&] (int value)
        {
            step->setValueNotifyingHost (step->convertTo0to1 ((float) value));   // as the host's undo / redo does
            pump (50);
            plugin.waitForPendingRebuilds();
        };
        auto oneEdit = [&] (const juce::String& what, int stepValue)
        {
            const auto edits = ear.edits();
            check (edits.size() == 1 && edits.front() == stepIndex && step->get() == stepValue,
                   what + " is one edit for the host, the step parameter's",
                   juce::String ((int) edits.size()) + " edits, step " + juce::String (step->get()));
        };

        bool allAutomatable = true;
        for (auto* p : plugin.getParameters())
            if (! p->isAutomatable())
            {
                allAutomatable = false;
                std::cout << "      not automatable: " << p->getName (64) << "\n";
            }
        check (allAutomatable, "every parameter is automatable (a host's history records only those)");

        auto* feed = paramOf (plugin, id::echoFeed);
        gesture (*feed, { 0.4f });
        check (! hh.canUndo() && ! hh.undo() && step->get() == 0, "a knob's edit is the host's own step, not one here");

        // a factory preset: its values as the plugin's changes, then one step named after it
        const auto& presets = factoryPresets();
        const auto chop = std::find_if (presets.begin(), presets.end(), [] (const FactoryPreset& p) { return juce::String (p.name) == "Chop"; });
        auto* att = paramOf (plugin, id::env (id::volPrefix, "Att"));
        auto presetName = [&] { return plugin.apvts.state.getProperty ("presetName", "").toString(); };
        ear.clear();
        {
            const History::Transaction edit (hh, "Preset: Chop");   // as the preset bar loads it, with its name
            applyFactoryPreset (plugin, *chop);
            plugin.apvts.state.setProperty ("presetName", "Chop", nullptr);
        }
        oneEdit ("a preset", 1);
        check (ear.changes.size() > 1, "its values reach the host as the plugin's changes", juce::String ((int) ear.changes.size()));
        check (step->getName (64) == "Preset: Chop", "the step is named after the preset", step->getName (64));
        const auto attChop = att->getValue();
        hostSetsStep (0);
        check (att->getValue() == att->getDefaultValue() && std::abs (feed->getValue() - 0.4f) < 1.0e-6f,
               "the host's undo puts the values from before the preset back");
        check (presetName().isEmpty(), "and the preset name from before", presetName());
        hostSetsStep (1);
        check (att->getValue() == attChop && feed->getValue() == feed->getDefaultValue(), "the host's redo applies the preset again");
        check (presetName() == "Chop", "with its name", presetName());

        // a sample loaded: one step; the host's undo removes it, its redo loads it again
        ear.clear();
        check (plugin.loadSample (sample), "the sample loads in the plugin", plugin.getLoadError());
        plugin.waitForPendingRebuilds();
        oneEdit ("loading a sample", 2);
        check (step->getName (64) == "Load sample: " + sample.getFileName(), "the step names the file", step->getName (64));
        hostSetsStep (1);
        check (plugin.getSampleFile() == juce::File() && plugin.getSampleLength() == 0, "the host's undo removes the sample");
        hostSetsStep (2);
        check (plugin.getSampleFile() == sample && plugin.getSampleLength() > 0, "the host's redo loads it again");
        // the host jumping back over both (its history list clicked further back)
        hostSetsStep (0);
        check (plugin.getSampleFile() == juce::File() && std::abs (feed->getValue() - 0.4f) < 1.0e-6f, "a jump back undoes both");
        hostSetsStep (2);
        check (plugin.getSampleFile() == sample && att->getValue() == attChop, "and a jump forward redoes both");

        // a user's preset: one step
        juce::MemoryBlock saved;
        plugin.getStateInformation (saved);
        gesture (*feed, { 0.7f });
        ear.clear();
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize()))
            plugin.loadPreset (juce::ValueTree::fromXml (*xml), "Mine");
        plugin.waitForPendingRebuilds();
        oneEdit ("a user's preset", 3);
        check (feed->getValue() == feed->getDefaultValue() && plugin.getSampleFile() == sample, "the preset's values are set", juce::String (feed->getValue()));
        hostSetsStep (2);
        check (std::abs (feed->getValue() - 0.7f) < 1.0e-6f, "and the host's undo takes them back");

        // the editor: no undo arrows, and Ctrl+Z / Ctrl+Y go to the host
        {
            auto editor = std::unique_ptr<ChanSamplerEditor> (static_cast<ChanSamplerEditor*> (plugin.createEditor()));
            pump();
            int arrows = 0;
            forEach<juce::Button> (*editor, [&] (juce::Button& b)
            {
                if (b.getTooltip().startsWith ("Undo") || b.getTooltip().startsWith ("Redo") || b.getTooltip().startsWith ("Nothing to"))
                    ++arrows;
            });
            check (arrows == 0, "a plugin's editor has no undo / redo arrows", juce::String (arrows));
            check (! editor->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0), editor.get())
                       && ! editor->keyPressed (juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0), editor.get()),
                   "Ctrl+Z / Ctrl+Y go on to the host");
        }

        // Automation reaches a parameter's listeners on the audio thread, where they only set a flag (no message
        // posted, no thread woken): the processor's timer follows up on the message thread.
        {
            std::thread audio ([&] { step->setValueNotifyingHost (step->convertTo0to1 (3.0f)); });
            audio.join();
            pump (200);
            plugin.waitForPendingRebuilds();
            check (feed->getValue() == feed->getDefaultValue(), "a step the host set off the message thread is followed",
                   juce::String (feed->getValue()));

            const auto before = plugin.getSampleForDisplay();
            auto* reverse = paramOf (plugin, id::pcReverse);
            std::thread automation ([&] { reverse->setValueNotifyingHost (1.0f); });
            automation.join();
            pump (300);
            check (plugin.getSampleForDisplay() != before, "a precomputed setting automated off the message thread rebuilds the sample");
        }
        plugin.removeListener (&ear);
    }

    std::cout << "\n" << (failures == 0 ? "All UI tests passed" : juce::String (failures) + " UI test(s) failed") << "\n";
    return failures == 0 ? 0 : 1;
}
