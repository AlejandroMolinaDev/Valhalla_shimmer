#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PresetManager.h"

namespace ab::ui
{

/** Preset browser: previous / menu / next, save, and A/B comparison. */
class PresetBar final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PresetBar (PresetManager& manager);
    ~PresetBar() override;

    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void updateDisplay();
    void showPresetMenu();
    void showSaveDialog();

    PresetManager& presets;
    juce::TextButton previous { "<" }, next { ">" }, name, save { "Save" }, slotA { "A" }, slotB { "B" },
        copy { "Copy" };
    bool lastModified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace ab::ui
