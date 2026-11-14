# Gives JUCE's VST3 editor the keys its host offers it.
#
# A VST3 host passes keyboard input to the plugin's view through
# IPlugView::onKeyDown / onKeyUp, and acts on a key itself when the view says
# it did not use it (FL Studio offers a key this way before running its own
# shortcut). JUCE 9.0.5's VST3 editor (like 9.0.4's and 8.0.16's) does not implement them, so it declines
# every key: Ctrl+Z, Ctrl+Y and Ctrl+C / Ctrl+V never reach a plug-in in
# FL Studio, whose own (empty) undo runs instead. This adds the two calls: the
# key goes through the editor's normal key handling, and the host gets every
# key the editor does not use.
#
# Run at configure time on the JUCE the build uses, fetched or supplied with
# FETCHCONTENT_SOURCE_DIR_JUCE: it changes that JUCE's source file in place.
# The JUCE is therefore this repository's own copy under build/_deps, never
# another project's checkout. It changes the file once and recognises its own
# change afterwards, by the marker or by the two functions it adds.
#   cmake -DJUCE_DIR=<juce source> -P PatchJuceVst3Keys.cmake

set(file "${JUCE_DIR}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
file(READ "${file}" source)
string(FIND "${source}" "VST3_HOST_KEYS" already)
# The marker, or the added onKeyDown itself (a JUCE patched by an earlier form of this script), means the file is
# patched already: the functions are never added twice.
string(FIND "${source}" "tresult PLUGIN_API onKeyDown (char16 key, int16 keyCode, int16 modifiers) override" added)
if(NOT already EQUAL -1 OR NOT added EQUAL -1)
    return()
endif()

set(anchor "        tresult PLUGIN_API removed() override\n")
string(FIND "${source}" "${anchor}" at)
if(at EQUAL -1)
    message(FATAL_ERROR "PatchJuceVst3Keys: JUCE's VST3 editor has changed; update cmake/PatchJuceVst3Keys.cmake")
endif()

set(keys [=[
        // VST3_HOST_KEYS (added by cmake/PatchJuceVst3Keys.cmake):
        // the keys the host offers the view go through the editor's key
        // handling; kResultFalse lets the host use the ones it did not take.
        tresult PLUGIN_API onKeyDown (char16 key, int16 keyCode, int16 modifiers) override
        {
            ignoreUnused (keyCode);
            if (key == 0 || component == nullptr || component->pluginEditor == nullptr)
                return kResultFalse;
            auto* peer = component->getPeer();
            if (peer == nullptr)
                return kResultFalse;
            // Steinberg::KeyModifier: shift 1, alternate 2, command 4 (Ctrl on
            // Windows, Cmd on macOS), control 8 (the Ctrl key on macOS)
            int mods = 0;
            if ((modifiers & 1) != 0) mods |= ModifierKeys::shiftModifier;
            if ((modifiers & 2) != 0) mods |= ModifierKeys::altModifier;
            if ((modifiers & 4) != 0) mods |= ModifierKeys::commandModifier;
            if ((modifiers & 8) != 0) mods |= ModifierKeys::ctrlModifier;
            auto c = (juce_wchar) key;
            if (c >= 1 && c <= 26)   // a Ctrl + letter as its control character
                c += 'a' - 1;
            const KeyPress press ((int) CharacterFunctions::toUpperCase (c), ModifierKeys (mods), c);
            return peer->handleKeyPress (press) ? kResultTrue : kResultFalse;
        }

        tresult PLUGIN_API onKeyUp (char16, int16, int16) override
        {
            return kResultFalse;
        }

]=])
string(SUBSTRING "${source}" 0 ${at} head)
string(SUBSTRING "${source}" ${at} -1 tail)
file(WRITE "${file}" "${head}${keys}${tail}")
message(STATUS "Patched JUCE's VST3 editor to take the host's keys (cmake/PatchJuceVst3Keys.cmake)")
