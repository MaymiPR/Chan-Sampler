#pragma once

// Keyboard focus in a plugin window (Windows; no-ops elsewhere).
//
// FL Studio hands the keys of its plugin window to the plugin through the
// VST3 view's key call and acts on the ones the plugin declines; but once a
// click has moved the Windows focus into the plugin's own window (Windows or
// FL's panel does that, not JUCE), FL sends the keys there instead and no
// longer acts on any the plugin declines: after touching a knob the space bar
// stopped playing (logged in FL: the focus on JUCE's window, space
// offered and declined, then reposted to FL's panel, which ignores it). After
// a click the editor hands the focus to the host's window for the plugin (the
// one with the title), as a click on FL's own Sampler leaves that window
// focused: FL then offers the plugin every key and acts on the ones left.
namespace hostfocus
{
    // The window with the keyboard focus in this thread (nullptr if none).
    void* current();
    // True when the focus is the window or one of its children.
    bool isWithin (void* focus, void* window);
    // Gives the keyboard focus to that window if it still exists.
    void giveTo (void* window);
    // The nearest window above this one that has a title (the host's window
    // for the plugin), or nullptr.
    void* titledAncestor (void* window);
}
