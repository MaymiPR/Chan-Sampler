#include "SampleExchange.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <shellapi.h>
 #include <shlobj.h>
 #pragma comment (lib, "shell32.lib")
#endif

namespace exchange
{
namespace
{
    // A new file name in folder for pasted audio.
    juce::File pastedFile (const juce::File& folder, const juce::String& extension)
    {
        folder.createDirectory();
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S");
        return folder.getNonexistentChildFile ("Pasted audio " + stamp, extension, false);
    }

    juce::File saveData (const juce::File& folder, const void* data, size_t size, const juce::String& extension)
    {
        if (data == nullptr || size < 12)
            return {};
        auto file = pastedFile (folder, extension);
        return file.replaceWithData (data, size) ? file : juce::File();
    }
}

#if JUCE_WINDOWS
namespace
{
    // The clipboard can be held briefly by another program: retry for a moment.
    bool openClipboard (HWND owner)
    {
        for (int i = 0; i < 10; ++i)
        {
            if (OpenClipboard (owner))
                return true;
            juce::Thread::sleep (10);
        }
        return false;
    }

    HGLOBAL globalCopy (const void* data, size_t size)
    {
        auto h = GlobalAlloc (GMEM_MOVEABLE | GMEM_ZEROINIT, size);
        if (h == nullptr)
            return nullptr;
        if (auto* p = GlobalLock (h))
        {
            std::memcpy (p, data, size);
            GlobalUnlock (h);
            return h;
        }
        GlobalFree (h);
        return nullptr;
    }

    bool setData (UINT format, HGLOBAL h)
    {
        if (h == nullptr)
            return false;
        if (SetClipboardData (format, h) != nullptr)
            return true;
        GlobalFree (h);
        return false;
    }
}

bool copyToClipboard (const juce::File& file, const juce::MemoryBlock& wav, void* nativeWindow)
{
    if (! openClipboard ((HWND) nativeWindow))
        return false;
    EmptyClipboard();
    bool any = false;

    if (file.existsAsFile())
    {
        // CF_HDROP: a DROPFILES header, then the path, double-null-terminated.
        const auto path = file.getFullPathName();
        const auto chars = (size_t) path.length();
        std::vector<char> drop (sizeof (DROPFILES) + (chars + 2) * sizeof (wchar_t), 0);
        auto* df = reinterpret_cast<DROPFILES*> (drop.data());
        df->pFiles = sizeof (DROPFILES);
        df->fWide = TRUE;
        std::memcpy (drop.data() + sizeof (DROPFILES), path.toWideCharPointer(), chars * sizeof (wchar_t));
        any |= setData (CF_HDROP, globalCopy (drop.data(), drop.size()));

        const DWORD effect = DROPEFFECT_COPY;   // Explorer pastes a copy, never moves the sample
        setData (RegisterClipboardFormatW (L"Preferred DropEffect"), globalCopy (&effect, sizeof (effect)));
    }
    if (wav.getSize() > 0)
        any |= setData (CF_WAVE, globalCopy (wav.getData(), wav.getSize()));

    CloseClipboard();
    return any;
}

bool clipboardHasAudio()
{
    return IsClipboardFormatAvailable (CF_HDROP) || IsClipboardFormatAvailable (CF_WAVE);
}

juce::File pasteFromClipboard (const juce::File& folder)
{
    if (! openClipboard (nullptr))
        return {};
    juce::File result;

    if (auto h = (HDROP) GetClipboardData (CF_HDROP))
    {
        const auto count = DragQueryFileW (h, 0xffffffff, nullptr, 0);
        for (UINT i = 0; i < count && result == juce::File(); ++i)
        {
            const auto len = DragQueryFileW (h, i, nullptr, 0);
            std::vector<wchar_t> name (len + 1, 0);
            DragQueryFileW (h, i, name.data(), len + 1);
            const juce::File f (juce::String (name.data()));
            if (f.existsAsFile())
                result = f;
        }
    }
    if (result == juce::File())
    {
        if (auto h = GetClipboardData (CF_WAVE))
        {
            if (auto* p = GlobalLock (h))
            {
                result = saveData (folder, p, (size_t) GlobalSize (h), ".wav");
                GlobalUnlock (h);
            }
        }
    }

    CloseClipboard();
    return result;
}

bool openInEditor (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;
    const auto path = file.getFullPathName();
    // The "edit" verb when the file type has an editor, else the default program.
    if ((INT_PTR) ShellExecuteW (nullptr, L"edit", path.toWideCharPointer(), nullptr, nullptr, SW_SHOWNORMAL) > 32)
        return true;
    return file.startAsProcess();
}

#elif JUCE_MAC
// SampleExchangeMac.mm
bool macCopyToClipboard (const char* path, const void* wav, size_t size);
bool macClipboardHasAudio();
bool macPasteFromClipboard (std::string& path, std::vector<char>& data, std::string& extension);

bool copyToClipboard (const juce::File& file, const juce::MemoryBlock& wav, void*)
{
    const auto path = file.existsAsFile() ? file.getFullPathName() : juce::String();
    return macCopyToClipboard (path.toRawUTF8(), wav.getData(), wav.getSize());
}

bool clipboardHasAudio() { return macClipboardHasAudio(); }

juce::File pasteFromClipboard (const juce::File& folder)
{
    std::string path, extension;
    std::vector<char> data;
    if (! macPasteFromClipboard (path, data, extension))
        return {};
    if (! path.empty())
        return juce::File (juce::String::fromUTF8 (path.c_str()));
    return saveData (folder, data.data(), data.size(), extension);
}

bool openInEditor (const juce::File& file)
{
    return file.existsAsFile() && file.startAsProcess();
}

#else
bool copyToClipboard (const juce::File&, const juce::MemoryBlock&, void*) { return false; }
bool clipboardHasAudio() { return false; }
juce::File pasteFromClipboard (const juce::File&) { return {}; }
bool openInEditor (const juce::File& file) { return file.existsAsFile() && file.startAsProcess(); }
#endif
}
