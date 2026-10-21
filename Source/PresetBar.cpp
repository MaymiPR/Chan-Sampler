#include "PresetBar.h"
#include "PluginProcessor.h"
#include "FlPreset.h"
#include "FlStyle.h"
#include "FactoryPresets.h"

PresetBar::PresetBar (ChanSamplerProcessor& p) : processor (p)
{
    setTooltip ("Presets");
    startTimerHz (4);   // the name follows host loads / undo
}

juce::File PresetBar::presetFolder()
{
    return ChanSamplerProcessor::userFolder ("Presets");
}

juce::Array<juce::File> PresetBar::presetFiles() const
{
    auto files = presetFolder().findChildFiles (juce::File::findFiles, true, juce::String ("*") + extension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getRelativePathFrom (presetFolder()).compareNatural (b.getRelativePathFrom (presetFolder())) < 0; });
    return files;
}

juce::String PresetBar::currentName() const
{
    return processor.apvts.state.getProperty ("presetName", "").toString();
}

void PresetBar::setCurrentName (const juce::String& name)
{
    processor.apvts.state.setProperty ("presetName", name, nullptr);
    repaint();
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1b1d1e));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (juce::Colour (0xff3a3d3f));
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);

    g.setColour (fl::colour::dim);
    for (int side = 0; side < 2; ++side)
    {
        const auto a = side == 0 ? leftArrow() : rightArrow();
        const auto d = side == 0 ? 1.0f : -1.0f;
        juce::Path chevron;
        chevron.startNewSubPath (a.getCentreX() + d * 1.5f, a.getCentreY() - 3.5f);
        chevron.lineTo (a.getCentreX() - d * 1.5f, a.getCentreY());
        chevron.lineTo (a.getCentreX() + d * 1.5f, a.getCentreY() + 3.5f);
        g.strokePath (chevron, juce::PathStrokeType (1.3f));
    }

    const auto name = currentName();
    g.setColour (name.isEmpty() ? fl::colour::dim : juce::Colours::white.withAlpha (0.85f));
    g.setFont (fl::font (12.0f));
    g.drawText (name.isEmpty() ? juce::String ("Presets") : name,
                r.withTrimmedLeft (17.0f).withTrimmedRight (17.0f), juce::Justification::centred, true);
}

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    if (leftArrow().contains (e.position))       step (-1);
    else if (rightArrow().contains (e.position)) step (1);
    else                                         showMenu();
}

void PresetBar::step (int delta)
{
    // The factory presets, then the user's
    const auto& factory = factoryPresets();
    const auto files = presetFiles();
    const int total = (int) factory.size() + files.size();
    if (total == 0)
        return;
    int index = -1;
    for (int i = 0; i < total; ++i)
        if ((i < (int) factory.size() ? juce::String (factory[(size_t) i].name) : files[i - (int) factory.size()].getFileNameWithoutExtension()) == currentName())
            index = i;
    index = index < 0 ? (delta > 0 ? 0 : total - 1) : (index + delta + total) % total;
    if (index < (int) factory.size())
        loadFactory (index);
    else
        load (files[index - (int) factory.size()]);
}

void PresetBar::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Save preset as...", [this] { saveAs(); });
    menu.addItem ("Import FL Sampler preset (.fst)...", [this] { importFst(); });
    menu.addItem ("Initialize", [this] { initialize(); });
    menu.addSeparator();

    // Factory presets (built in, settings only)
    {
        juce::PopupMenu factory;
        std::map<juce::String, juce::PopupMenu> groups;
        const auto& list = factoryPresets();
        juce::StringArray order;
        for (size_t i = 0; i < list.size(); ++i)
        {
            const juce::String folder (list[i].folder);
            order.addIfNotAlreadyThere (folder);
            groups[folder].addItem (list[i].name, true, currentName() == list[i].name, [this, i] { loadFactory ((int) i); });
        }
        for (auto& folder : order)
            factory.addSubMenu (folder, groups[folder]);
        menu.addSubMenu ("Factory", factory);
        menu.addSeparator();
    }

    // Presets, with one submenu per folder
    const auto files = presetFiles();
    std::map<juce::String, juce::PopupMenu> folders;
    for (auto& f : files)
    {
        const auto folder = f.getParentDirectory().getRelativePathFrom (presetFolder());
        const bool ticked = f.getFileNameWithoutExtension() == currentName();
        auto& target = (folder == "." || folder.isEmpty()) ? menu : folders[folder];
        target.addItem (f.getFileNameWithoutExtension(), true, ticked, [this, f] { load (f); });
    }
    for (auto& [name, sub] : folders)
        menu.addSubMenu (name, sub);
    if (files.isEmpty())
        menu.addItem ("(no presets yet)", false, false, [] {});
    menu.addSeparator();
    menu.addItem ("Open presets folder", [] { presetFolder().createDirectory(); presetFolder().startAsProcess(); });

    fl::showContextMenu (menu, *this, false);
}

void PresetBar::load (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr)
    {
        if (onMessage) onMessage ("Could not read " + file.getFileName(), false);
        return;
    }
    // A preset without a sample (settings only) keeps the loaded one.
    if (! xml->hasAttribute ("samplePath"))
        xml->setAttribute ("samplePath", processor.getSampleFile().getFullPathName());
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary (*xml, state);
    processor.setStateInformation (state.getData(), (int) state.getSize());
    setCurrentName (file.getFileNameWithoutExtension());
}

void PresetBar::loadFactory (int index)
{
    const auto& preset = factoryPresets()[(size_t) index];
    applyFactoryPreset (processor, preset);
    setCurrentName (preset.name);
}

void PresetBar::saveAs()
{
    presetFolder().createDirectory();
    const auto start = presetFolder().getChildFile ((currentName().isNotEmpty() ? currentName() : juce::String ("New preset")) + extension);
    chooser = std::make_unique<juce::FileChooser> ("Save preset", start, juce::String ("*") + extension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension (extension);
                              setCurrentName (file.getFileNameWithoutExtension());
                              juce::MemoryBlock state;
                              processor.getStateInformation (state);
                              if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
                                  if (! xml->writeTo (file))
                                      if (onMessage) onMessage ("Could not save " + file.getFullPathName(), false);
                          });
}

void PresetBar::importFst()
{
    chooser = std::make_unique<juce::FileChooser> ("Import FL Sampler preset", juce::File(), "*.fst");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              const auto r = flpreset::import (processor, file);
                              if (r.ok)
                                  setCurrentName (file.getFileNameWithoutExtension());
                              if (onMessage && (! r.ok || r.message.isNotEmpty()))
                                  onMessage (r.message, r.ok);
                          });
}

void PresetBar::initialize()
{
    processor.resetParameters();
    processor.removeSample();
    setCurrentName ({});
}
