#pragma once

#include <juce_core/juce_core.h>

// The sample's way in and out of other programs (the sample menu): the system
// clipboard. Dragging the sample out of the view uses JUCE's external file drag
// (WaveformView).
namespace exchange
{
    // Puts the sample on the clipboard: its file (paste into Explorer / Finder or
    // a DAW) and its audio as WAV data (paste into an audio editor). nativeWindow:
    // the editor's window handle (Windows needs a clipboard owner).
    bool copyToClipboard (const juce::File& file, const juce::MemoryBlock& wav, void* nativeWindow);

    // True when the clipboard holds a file or audio data.
    bool clipboardHasAudio();

    // The clipboard's audio as a file: a copied file as it is, or copied audio
    // data saved as a new file in folder. File() when there is none.
    juce::File pasteFromClipboard (const juce::File& folder);
}
