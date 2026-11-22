#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Factory presets (embedded XML) + user presets (Documents/Aurora Bloom/Presets) + A/B slots.
    Everything here runs on the message thread. */
class PresetManager : public juce::ChangeBroadcaster
{
public:
    struct Preset
    {
        juce::String name;
        juce::String category;
        bool isFactory = true;
        juce::String factoryXml;  // factory presets
        juce::File file;          // user presets
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState& state);

    static juce::File getUserPresetFolder();

    /** Re-scans the user preset folder. */
    void refresh();
    const std::vector<Preset>& getPresets() const noexcept { return presets; }

    bool loadPreset (int index);
    void loadNext();
    void loadPrevious();
    bool saveUserPreset (const juce::String& name);

    /** Index of the current preset in getPresets(), or -1 (e.g. "Init" or a deleted file). */
    int getCurrentIndex() const;
    juce::String getCurrentName() const;

    /** Thread-safe: called from setStateInformation(), which some hosts call off the message thread. */
    void restoreCurrentName (const juce::String& name);

    /** True once a parameter changed after the last preset load. */
    bool isModified() const noexcept { return modified.load(); }
    void markModified() noexcept { modified = true; }
    void clearModified() noexcept { modified = false; }

    // --- A/B comparison --------------------------------------------------------------------------
    int getActiveSlot() const noexcept { return activeSlot; }
    void toggleSlot();
    void copyToOtherSlot();

    // --- Preset XML (also used by the tests) -------------------------------------------------------
    static std::unique_ptr<juce::XmlElement> createPresetXml (const juce::AudioProcessorValueTreeState&,
                                                              const juce::String& name, const juce::String& category);
    static bool applyPresetXml (juce::AudioProcessorValueTreeState&, const juce::XmlElement&);

private:
    juce::AudioProcessorValueTreeState& apvts;
    void setCurrentName (const juce::String& name);

    std::vector<Preset> presets;
    juce::String currentName { "Init" };
    mutable juce::CriticalSection nameLock;
    std::atomic<bool> modified { false };

    std::array<juce::ValueTree, 2> slots;
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
