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

    /** The preset browser menu (Init, factory presets by category, user presets, folder). */
    juce::PopupMenu buildPresetMenu();
    /** Applies a result of buildPresetMenu(); 0 (dismissed) does nothing. */
    void handlePresetMenuResult (int result);
    /** Refreshes the "*" marker when a parameter was changed since the last preset load. */
    void pollModified();

    static constexpr int kInitItem = 10001;
    static constexpr int kOpenFolderItem = 10000;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override { pollModified(); }
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
