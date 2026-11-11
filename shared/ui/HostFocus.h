#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

// Keeps the host's keys working after a click in the editor (Windows; nothing happens elsewhere).
//
// FL Studio hands the keys of its plug-in window to the plug-in through the VST3 view's key call and acts on the ones
// the plug-in declines (cmake/PatchJuceVst3Keys.cmake makes JUCE decline them). Once a click has moved the Windows
// focus into the plug-in's own window (Windows or FL's panel does that, not JUCE), FL sends the keys there instead and
// acts on none the plug-in declines: after touching a knob the space bar stops playing. After each click, HostFocus
// hands the focus back to the host's window for the plug-in (the nearest ancestor with a title), unless a text box is
// being typed in or a menu or dialog is open. FocusCheck (tools/) verifies the result on the built VST3.
//
// In a host that does not offer the keys to the view (not VST3, not the Standalone), an editor that handles keys of its
// own (undo, a typed value) passes wantsKeys = true: a click then gives the editor the focus, and JUCE passes every key
// it does not use on to the host's window.
//
// Use: a member of the editor, constructed after noFocus():  chan::HostFocus hostFocus { *this };
namespace chan
{
class HostFocus
{
public:
    explicit HostFocus (juce::AudioProcessorEditor& editor, bool wantsKeys = false);
    ~HostFocus();

private:
    juce::AudioProcessorEditor& editor;
    std::unique_ptr<juce::MouseListener> listener;

    JUCE_DECLARE_NON_COPYABLE (HostFocus)
};

// The native calls (Windows; no-ops elsewhere), on window handles as void*.
namespace hostfocus
{
void* current();                              // the window with the keyboard focus in this thread, or nullptr
bool isWithin (void* focus, void* window);    // the focus is the window or one of its children
void giveTo (void* window);                   // gives the keyboard focus to that window if it still exists
void* titledAncestor (void* window);          // the nearest window above this one with a title (the host's), or nullptr
}
}
