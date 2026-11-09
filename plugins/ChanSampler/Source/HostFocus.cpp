#include "HostFocus.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>

namespace hostfocus
{
    void* current() { return (void*) GetFocus(); }

    bool isWithin (void* focus, void* window)
    {
        if (focus == nullptr || window == nullptr)
            return false;
        return focus == window || IsChild ((HWND) window, (HWND) focus) != FALSE;
    }

    void giveTo (void* window)
    {
        if (window != nullptr && IsWindow ((HWND) window))
            SetFocus ((HWND) window);
    }

    void* titledAncestor (void* window)
    {
        for (auto h = window != nullptr ? GetParent ((HWND) window) : nullptr; h != nullptr; h = GetParent (h))
            if (GetWindowTextLengthW (h) > 0)
                return (void*) h;
        return nullptr;
    }
}
#else
namespace hostfocus
{
    void* current() { return nullptr; }
    bool isWithin (void*, void*) { return false; }
    void giveTo (void*) {}
    void* titledAncestor (void*) { return nullptr; }
}
#endif
