#include "OsFile.h"

#if JUCE_WINDOWS
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include <shlobj.h>
 #pragma comment (lib, "shell32.lib")
#endif

namespace osfile
{
bool showProperties (const juce::File& file)
{
    if (! file.exists())
        return false;
   #if JUCE_WINDOWS
    // The same sheet as Explorer's right-click > Properties (modeless, owned by the shell).
    return SHObjectProperties (nullptr, SHOP_FILEPATH, file.getFullPathName().toWideCharPointer(), nullptr) != FALSE;
   #elif JUCE_MAC
    // Finder's Get Info window for the file.
    const auto path = file.getFullPathName().replace ("\\", "\\\\").replace ("\"", "\\\"");
    juce::StringArray args { "osascript",
                             "-e", "tell application \"Finder\" to open information window of (POSIX file \"" + path + "\" as alias)",
                             "-e", "tell application \"Finder\" to activate" };
    static juce::ChildProcess process;   // kept alive while osascript runs
    return process.start (args, 0);
   #else
    return false;
   #endif
}
}
