// Windows check that the space bar reaches the host while a plugin window is in use:
//   FocusCheck <path to .vst3> [x,y ...]
// Hosts the plugin's editor in a window, clicks into it (at the given design points, default a few spots) the way
// Windows delivers a click, then sends the space key to whichever window has the keyboard focus. PASS when the
// space ends up with the host: either the host kept the focus, or the plugin passed the unhandled key on to its
// parent window. A plug-in whose space bar is known to work in FL Studio is the reference for this check.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <windows.h>

static int spaceSeenByHost = 0;
static WNDPROC hostProc = nullptr;
static LRESULT CALLBACK hostHook (HWND h, UINT m, WPARAM w, LPARAM l)
{
    if ((m == WM_KEYDOWN || m == WM_CHAR) && (w == VK_SPACE || w == ' ')) ++spaceSeenByHost;
    return CallWindowProc (hostProc, h, m, w, l);
}

static BOOL CALLBACK collect (HWND h, LPARAM lp)
{
    reinterpret_cast<std::vector<HWND>*> (lp)->push_back (h);
    return TRUE;
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

int main (int argc, char** argv)
{
    if (argc < 2) { std::cerr << "usage: FocusCheck plugin.vst3 [x,y ...]\n"; return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    juce::OwnedArray<juce::PluginDescription> found;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (found, juce::String (argv[1]));
    if (found.isEmpty()) { std::cerr << "no plugin found\n"; return 2; }
    juce::String err;
    auto inst = fm.createPluginInstance (*found[0], 48000.0, 512, err);
    if (inst == nullptr) { std::cerr << "load failed: " << err << "\n"; return 2; }

    juce::DocumentWindow win ("FocusCheck", juce::Colours::black, 0);
    auto* ed = inst->createEditorIfNeeded();
    win.setContentNonOwned (ed, true);
    win.setVisible (true);
    win.toFront (true);
    for (int i = 0; i < 20; ++i) pump (20);

    // the plugin's own window: a child whose class was registered by the .vst3 module
    std::vector<HWND> children;
    EnumChildWindows ((HWND) win.getWindowHandle(), collect, (LPARAM) &children);
    HWND plug = nullptr;
    for (auto h : children)
    {
        char mod[MAX_PATH] = {};
        GetModuleFileNameA ((HMODULE) GetClassLongPtr (h, GCLP_HMODULE), mod, MAX_PATH);
        if (juce::String (mod).containsIgnoreCase (".vst3")) { plug = h; break; }
    }
    if (plug == nullptr) { std::cout << "no plugin window found\n"; return 1; }
    HWND parent = GetParent (plug);
    hostProc = (WNDPROC) SetWindowLongPtr (parent, GWLP_WNDPROC, (LONG_PTR) hostHook);

    std::vector<POINT> points;
    for (int i = 2; i < argc; ++i)
    {
        const auto s = juce::String (argv[i]);
        points.push_back ({ s.upToFirstOccurrenceOf (",", false, false).getIntValue(), s.fromFirstOccurrenceOf (",", false, false).getIntValue() });
    }
    if (points.empty()) points = { { 57, 100 }, { 185, 200 }, { 312, 125 }, { 185, 20 } };

    int failures = 0;
    for (auto pt : points)
    {
        // a click as Windows delivers it: WM_MOUSEACTIVATE (activate + focus unless refused), then button down / up
        const auto act = SendMessage (plug, WM_MOUSEACTIVATE, (WPARAM) parent, MAKELPARAM (HTCLIENT, WM_LBUTTONDOWN));
        if (act != MA_NOACTIVATE && act != MA_NOACTIVATEANDEAT) SetFocus (plug);
        const LPARAM xy = MAKELPARAM (pt.x, pt.y);
        SendMessage (plug, WM_LBUTTONDOWN, MK_LBUTTON, xy);
        pump (50);
        SendMessage (plug, WM_LBUTTONUP, 0, xy);
        pump (100);

        // a user closes a menu or value box first (Escape), then presses space
        if (HWND f = GetFocus()) { SendMessage (f, WM_KEYDOWN, VK_ESCAPE, 0); SendMessage (f, WM_KEYUP, VK_ESCAPE, 0); }
        pump (100);
        HWND focus = GetFocus();
        HWND target = focus != nullptr ? focus : parent;
        const bool focusInPlugin = (target == plug) || IsChild (plug, target);
        spaceSeenByHost = 0;
        SendMessage (target, WM_KEYDOWN, VK_SPACE, 0x00390001);
        SendMessage (target, WM_CHAR, ' ', 0x00390001);
        SendMessage (target, WM_KEYUP, VK_SPACE, 0xC0390001);
        pump (100);
        const bool ok = ! focusInPlugin || spaceSeenByHost > 0;
        std::cout << "click at " << pt.x << "," << pt.y << ": keyboard focus " << (focusInPlugin ? "in the plugin" : "with the host")
                  << ", space reached the host: " << (ok ? "yes" : "NO") << "\n";
        if (! ok) ++failures;
    }

    SetWindowLongPtr (parent, GWLP_WNDPROC, (LONG_PTR) hostProc);
    win.clearContentComponent();
    delete ed;
    inst = nullptr;
    std::cout << (failures == 0 ? "PASS: the space bar reaches the host\n" : "FAIL: the plugin swallows the space bar\n");
    return failures == 0 ? 0 : 1;
}
