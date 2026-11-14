#include "HostFocus.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace chan
{
namespace hostfocus
{
#if defined (_WIN32)
void* current() { return (void*) GetFocus(); }

bool isWithin (void* focus, void* window)
{
    if (focus == nullptr || window == nullptr) return false;
    return focus == window || IsChild ((HWND) window, (HWND) focus) != FALSE;
}

void giveTo (void* window)
{
    if (window != nullptr && IsWindow ((HWND) window)) SetFocus ((HWND) window);
}

void* titledAncestor (void* window)
{
    for (auto h = window != nullptr ? GetParent ((HWND) window) : nullptr; h != nullptr; h = GetParent (h))
        if (GetWindowTextLengthW (h) > 0) return (void*) h;
    return nullptr;
}
#else
void* current() { return nullptr; }
bool isWithin (void*, void*) { return false; }
void giveTo (void*) {}
void* titledAncestor (void*) { return nullptr; }
#endif
}

namespace
{
// FL Studio (VST3): after a click, the focus goes back to FL's window for the plug-in, as a click on one of FL's own
// instruments leaves it. Not for a right-click (its menu would close the moment the focus left).
struct KeepHostFocus : juce::MouseListener, private juce::Timer
{
    explicit KeepHostFocus (juce::Component& c) : editor (c) { startTimerHz (4); }
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) handBackSoon(); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) handBackSoon(); }

    // after JUCE has handled the click and the focus change it brought
    void handBackSoon()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (&editor), this]
        {
            if (safe != nullptr) handBack();
        });
    }
    // also when a typed value, a menu or a dialog is done with the focus
    void timerCallback() override { handBack(); }

    void handBack()
    {
        if (dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
            return;     // typing a value: the keys are the editor's
        if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
            return;     // a menu or dialog is open: moving the focus would close it
        auto* peer = editor.getPeer();
        if (peer == nullptr || ! hostfocus::isWithin (hostfocus::current(), peer->getNativeHandle()))
            return;
        hostfocus::giveTo (hostfocus::titledAncestor (peer->getNativeHandle()));
    }
    juce::Component& editor;
};

// A host that does not offer the keys to the view: a click gives the editor the focus (not the control clicked, so the
// space bar is not swallowed).
struct FocusOnClick : juce::MouseListener
{
    explicit FocusOnClick (juce::Component& c) : editor (c) {}
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (editor.hasKeyboardFocus (true) || (e.eventComponent != nullptr && e.eventComponent->getWantsKeyboardFocus()))
            return;     // a value box being typed in keeps it
        editor.grabKeyboardFocus();
    }
    juce::Component& editor;
};
}

HostFocus::HostFocus (juce::AudioProcessorEditor& e, bool wantsKeys) : editor (e)
{
    const auto wrapper = e.processor.wrapperType;
    if (wrapper == juce::AudioProcessor::wrapperType_VST3 && juce::PluginHostType().isFruityLoops())
        listener = std::make_unique<KeepHostFocus> (e);
    else if (wantsKeys && wrapper != juce::AudioProcessor::wrapperType_VST3
             && wrapper != juce::AudioProcessor::wrapperType_Standalone && wrapper != juce::AudioProcessor::wrapperType_Undefined)
    {
        e.setWantsKeyboardFocus (true);
        listener = std::make_unique<FocusOnClick> (e);
    }
    if (listener != nullptr) e.addMouseListener (listener.get(), true);
}

HostFocus::~HostFocus()
{
    if (listener != nullptr) editor.removeMouseListener (listener.get());
}
}
